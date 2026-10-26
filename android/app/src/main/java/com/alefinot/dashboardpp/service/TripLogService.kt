package com.alefinot.dashboardpp.service

import android.app.Notification
import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.PendingIntent
import android.app.Service
import android.content.Context
import android.content.Intent
import android.content.pm.ServiceInfo
import android.os.IBinder
import android.os.PowerManager
import androidx.core.app.NotificationCompat
import androidx.core.app.ServiceCompat
import androidx.core.content.ContextCompat
import com.alefinot.dashboardpp.MainActivity
import com.alefinot.dashboardpp.R
import com.alefinot.dashboardpp.api.EspApi
import com.alefinot.dashboardpp.api.EspApiLane
import com.alefinot.dashboardpp.api.Snapshot
import com.alefinot.dashboardpp.trips.CsvTripWriter
import com.alefinot.dashboardpp.trips.RecorderSettings
import com.alefinot.dashboardpp.trips.TripEvent
import com.alefinot.dashboardpp.trips.TripPolicy
import com.alefinot.dashboardpp.trips.TripRecorder
import com.alefinot.dashboardpp.trips.TripRuntime
import com.alefinot.dashboardpp.trips.TripStore
import com.alefinot.dashboardpp.trips.TripSummary
import java.util.Locale
import java.util.concurrent.TimeUnit
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.cancel
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.first
import kotlinx.coroutines.isActive
import kotlinx.coroutines.launch

/**
 * The recording lane (trip recording plan §5): a foreground service that owns one
 * 1 Hz poll of `/api/snapshot`, appends each sample to the open trip file and
 * keeps the session state machine alive while the app is in the background.
 *
 * It is a foreground service with an ongoing notification because that is the
 * only shape Android permits for work that must continue while the phone is
 * driving; the notification also says what is happening, which is the honest
 * trade for a partial wake lock. Polling stands down while the Web UI tab is on
 * screen — that tab's own 1 Hz fetch becomes the only traffic to the device, so
 * the total never exceeds one request per second (plan §2, one-lane rule).
 */
class TripLogService : Service() {
    private val scope = CoroutineScope(SupervisorJob() + Dispatchers.IO)
    private var pollJob: Job? = null
    private var recorder: TripRecorder? = null
    private var writer: CsvTripWriter? = null
    private var store: TripStore? = null
    private var settings: RecorderSettings? = null
    private var wakeLock: PowerManager.WakeLock? = null
    private var ip: String = ""
    private var telemetryInNotification = true

    override fun onBind(intent: Intent?): IBinder? = null

    override fun onCreate() {
        super.onCreate()
        ensureChannel()
    }

    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
        when (intent?.action) {
            ACTION_STOP -> {
                shutdown()
                return START_NOT_STICKY
            }
        }
        val target = intent?.getStringExtra(EXTRA_IP)
        if (target.isNullOrEmpty()) {
            shutdown()
            return START_NOT_STICKY
        }
        ip = target
        // A foreground service must announce itself immediately after
        // startForegroundService(); without this the process is ANR'd in ~5 s.
        ServiceCompat.startForeground(
            this, NOTIF_ID, buildNotification(null, null),
            ServiceInfo.FOREGROUND_SERVICE_TYPE_CONNECTED_DEVICE,
        )
        beginPolling()
        return START_STICKY
    }

    override fun onDestroy() {
        shutdown()
        super.onDestroy()
    }

    fun shutdown() {
        pollJob?.cancel()
        pollJob = null
        recorder?.stop()
        recorder = null
        writer?.closeAll()
        writer = null
        releaseWake()
        TripRuntime.update { it.copy(polling = false, recording = false) }
        stopForeground(STOP_FOREGROUND_REMOVE)
        stopSelf()
    }

    /**
     * The poll loop. One request in flight (the [EspApiLane] guarantees it), one
     * per second while a session is live, backing off when the device stopped
     * answering so a trip that has already sealed does not keep the radio busy.
     */
    private fun beginPolling() {
        if (pollJob?.isActive == true) return
        pollJob = scope.launch {
            if (!awaitRecorder()) return@launch
            var probed = false
            while (isActive) {
                val pausedForWebUi = EspApiLane.webUiVisible
                TripRuntime.update { it.copy(ip = this@TripLogService.ip, polling = true, pausedForWebUi = pausedForWebUi) }
                var nextDelay = POLL_MS
                if (!pausedForWebUi) {
                    val snap = EspApi.snapshot(ip)
                    if (snap != null) {
                        handleSnapshot(snap)
                    } else if (!probed) {
                        // First contact attempt. If /api/live answers but
                        // /api/snapshot does not, the device runs firmware from
                        // before this endpoint existed and cannot be recorded —
                        // the UI shows "needs update" instead of a dead tab.
                        probed = true
                        val live = EspApi.live(ip)
                        if (live != null) {
                            TripRuntime.update {
                                it.copy(live = live, lastError = EspApi.FIRMWARE_UPDATE_REQUIRED,
                                    lastOkMs = System.currentTimeMillis())
                            }
                            shutdown()
                            break
                        }
                        nextDelay = handleFailure()
                    } else {
                        nextDelay = handleFailure()
                    }
                }
                recorder?.onTick()?.let { ev ->
                    if (ev == TripEvent.SEALED) {
                        TripRuntime.update { it.copy(recording = false, tripPath = null, summary = null) }
                        persistSession()
                    }
                }
                if (shouldGiveUp()) {
                    shutdown()
                    break
                }
                delay(nextDelay)
            }
        }
    }

    /** True when the device has been unreachable long enough to stop the service. */
    private fun shouldGiveUp(): Boolean {
        val state = TripRuntime.state.value
        if (state.recording) return false
        if (state.lastOkMs == 0L) return false
        return System.currentTimeMillis() - state.lastOkMs > TripPolicy.IDLE_RETRY_MS
    }

    /** Back-off schedule while the device is silent: 1 s, then 5 s, then 15 s. */
    private fun handleFailure(): Long {
        val state = TripRuntime.state.value
        val since = System.currentTimeMillis() - state.lastOkMs
        TripRuntime.update { it.copy(lastError = "no-answer") }
        return when {
            state.lastOkMs == 0L -> SLOW_POLL_MS
            since < TripPolicy.SEAL_AFTER_MS -> SLOW_POLL_MS
            else -> BACKOFF_POLL_MS
        }
    }

    /** One accepted sample: append it, publish it, show it. */
    private fun handleSnapshot(s: Snapshot) {
        val rec = recorder ?: return
        val event = rec.onSnapshot(ip, s)
        val summary = rec.summary()
        val now = System.currentTimeMillis()
        TripRuntime.update {
            it.copy(
                snapshot = s,
                summary = summary,
                lastOkMs = now,
                recording = rec.isRecording,
                tripPath = rec.openTrip?.path,
                tripStartMs = rec.openTrip?.startMs ?: 0L,
                lastError = null,
                live = null,
            )
        }
        applyWake(rec.isRecording)
        notify(s, summary)
        when (event) {
            TripEvent.OPENED, TripEvent.REOPENED, TripEvent.SEALED_THEN_OPENED -> persistSession()
            TripEvent.APPENDED -> if (summary != null && summary.samples % 30 == 0) persistSession()
            else -> Unit
        }
    }

    /**
     * Builds the recorder, or returns false when recording must not run at all.
     * The user's switch is checked here as well as where the service is started:
     * "off" has to mean no polling, no wake lock and no file, even if something
     * else started the service by mistake.
     */
    private suspend fun awaitRecorder(): Boolean {
        if (recorder != null) return true
        val tripStore = TripStore(this)
        store = tripStore
        val csvWriter = CsvTripWriter(tripStore)
        writer = csvWriter
        val appSettings = RecorderSettings(this)
        settings = appSettings
        telemetryInNotification = try {
            appSettings.notificationEnabled.first()
        } catch (e: Exception) {
            true
        }
        val wanted = try {
            appSettings.recordingEnabled.first()
        } catch (e: Exception) {
            true
        }
        if (!wanted) {
            TripRuntime.update { it.copy(lastError = "disabled-by-user") }
            shutdown()
            return false
        }
        // Carry the last session across service restarts, so the ESP rebooting
        // while the app was closed still continues the same trip file.
        val prior = try {
            appSettings.sessionFor(ip)
        } catch (e: Exception) {
            null
        }
        recorder = TripRecorder(csvWriter, { startMs -> tripStore.newName(startMs) }, {
            System.currentTimeMillis()
        }, prior)
        return true
    }

    private fun persistSession() {
        val record = recorder?.sessionRecord() ?: return
        try {
            scope.launch {
                try {
                    settings?.saveSession(record)
                } catch (e: Exception) {
                    // Losing the session note costs the 24 h re-open, never the trip.
                }
            }
        } catch (e: Exception) {
            // scope already cancelled during shutdown
        }
    }

    // ---- notification -------------------------------------------------------

    private fun ensureChannel() {
        val manager = getSystemService(NotificationManager::class.java) ?: return
        if (manager.getNotificationChannel(CHANNEL) != null) return
        val channel = NotificationChannel(
            CHANNEL, "Trip recording", NotificationManager.IMPORTANCE_LOW,
        ).apply {
            description = "Shown while drive data is being logged"
            setShowBadge(false)
        }
        manager.createNotificationChannel(channel)
    }

    private fun notify(snapshot: Snapshot?, summary: TripSummary?) {
        val manager = getSystemService(NotificationManager::class.java) ?: return
        try {
            manager.notify(NOTIF_ID, buildNotification(snapshot, summary))
        } catch (e: Exception) {
            // Notification permission revoked mid-drive; recording continues.
        }
    }

    private fun buildNotification(snapshot: Snapshot?, summary: TripSummary?): Notification {
        val recording = recorder?.isRecording == true
        val title = when {
            recording && summary != null ->
                "Recording · ${formatDuration(summary.durationMs)}" +
                    " · ${format1(summary.distanceKm)} km"
            recording -> "Recording this drive"
            else -> "Waiting for the dashboard"
        }
        val text = when {
            !telemetryInNotification -> "Logging is on"
            snapshot == null -> "No answer from the device yet"
            else -> "${snapshot.spd.toInt()} km/h · ${format1(snapshot.trpL)} L · " +
                "${format1(snapshot.volts)} V · ${snapshot.sats} sats"
        }
        val open = Intent(this, MainActivity::class.java).apply {
            putExtra(EXTRA_OPEN_TAB, TAB_LIVE)
            addFlags(Intent.FLAG_ACTIVITY_SINGLE_TOP or Intent.FLAG_ACTIVITY_CLEAR_TOP)
        }
        val contentIntent = PendingIntent.getActivity(
            this, 0, open,
            PendingIntent.FLAG_IMMUTABLE or PendingIntent.FLAG_UPDATE_CURRENT,
        )
        val stopIntent = Intent(this, TripLogService::class.java).apply {
            action = ACTION_STOP
            putExtra(EXTRA_IP, ip)
        }
        val stopPending = PendingIntent.getService(
            this, 1, stopIntent,
            PendingIntent.FLAG_IMMUTABLE or PendingIntent.FLAG_UPDATE_CURRENT,
        )
        return NotificationCompat.Builder(this, CHANNEL)
            .setSmallIcon(R.drawable.ic_stat_dashboard)
            .setContentTitle(title)
            .setContentText(text)
            .setContentIntent(contentIntent)
            .setOngoing(true)
            .setOnlyAlertOnce(true)
            .setSilent(true)
            .setPriority(NotificationCompat.PRIORITY_LOW)
            .setCategory(NotificationCompat.CATEGORY_PROGRESS)
            .addAction(0, "Stop recording", stopPending)
            .setVisibility(
                if (telemetryInNotification) {
                    NotificationCompat.VISIBILITY_PRIVATE
                } else {
                    // The toggle exists for exactly this: numbers off the lock screen.
                    NotificationCompat.VISIBILITY_SECRET
                },
            )
            .build()
    }

    private fun formatDuration(ms: Long): String {
        val s = TimeUnit.MILLISECONDS.toSeconds(ms)
        return String.format(Locale.US, "%d:%02d:%02d", s / 3600, (s / 60) % 60, s % 60)
    }

    private fun format1(v: Double): String = String.format(Locale.US, "%.1f", v)

    // ---- wake lock ---------------------------------------------------------

    private fun applyWake(recording: Boolean) {
        if (!recording) {
            releaseWake()
            return
        }
        if (wakeLock?.isHeld == true) return
        val power = getSystemService(PowerManager::class.java) ?: return
        val lock = power.newWakeLock(PowerManager.PARTIAL_WAKE_LOCK, WAKE_TAG).apply {
            setReferenceCounted(false)
            // Never hold indefinitely: a stuck session cannot drain the battery
            // past the recording cap.
            acquire(TimeUnit.HOURS.toMillis(13))
        }
        wakeLock = lock
    }

    private fun releaseWake() {
        try {
            wakeLock?.takeIf { it.isHeld }?.release()
        } catch (e: Exception) {
            // already released
        }
        wakeLock = null
    }

    companion object {
        const val ACTION_STOP = "com.alefinot.dashboardpp.action.STOP_TRIPS"
        const val EXTRA_IP = "ip"
        const val EXTRA_OPEN_TAB = "open_tab"
        const val TAB_LIVE = "live"

        private const val CHANNEL = "trip_recording"
        private const val NOTIF_ID = 4201
        private const val WAKE_TAG = "dashboardpp:trip"
        private const val POLL_MS = 1_000L
        private const val SLOW_POLL_MS = 5_000L
        private const val BACKOFF_POLL_MS = 15_000L

        fun start(context: Context, ip: String) {
            val intent = Intent(context, TripLogService::class.java).apply {
                putExtra(EXTRA_IP, ip)
            }
            ContextCompat.startForegroundService(context, intent)
        }

        fun stop(context: Context) {
            val intent = Intent(context, TripLogService::class.java).apply { action = ACTION_STOP }
            try {
                context.startService(intent)
            } catch (e: Exception) {
                // Already stopped.
            }
        }
    }
}
