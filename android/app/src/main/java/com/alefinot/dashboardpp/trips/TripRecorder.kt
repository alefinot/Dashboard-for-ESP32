package com.alefinot.dashboardpp.trips

import com.alefinot.dashboardpp.api.Snapshot
import java.io.File

/** What trip is currently open, if any. */
data class TripRef(val path: String, val startMs: Long)

/**
 * Everything needed to recognise the same ESP power session later — including
 * after the app died — so a trip sealed when the device went out of range can
 * still be continued by the same boot of the ESP (plan §3).
 */
data class SessionRecord(
    val ip: String,
    /** Last `up` (ESP uptime in s) accepted for this session. */
    val lastUp: Long,
    /** Phone time of that last accepted sample. */
    val lastSampleMs: Long,
    /** Highest `max` (ESP session peak speed) seen in this session. */
    val lastMaxSpeed: Double,
    val tripPath: String?,
    val tripStartMs: Long,
    /** True once the trip was sealed; a sealed trip may still be re-opened. */
    val sealed: Boolean,
)

enum class TripEvent { NONE, OPENED, REOPENED, APPENDED, SEALED_THEN_OPENED, SEALED }

/**
 * The trip session state machine (trip recording plan §3): one trip per ESP
 * power session, sealed when the device stops answering, re-opened if the same
 * session comes back within 24 h, split after 12 h of continuous recording.
 *
 * Deliberately free of Android and HTTP: it is handed samples, a clock and a
 * [TripWriter], so every boundary case is unit-testable on the JVM.
 */
class TripRecorder(
    private val writer: TripWriter,
    private val pathFor: (startMs: Long) -> String,
    private val nowMs: () -> Long = System::currentTimeMillis,
    initialSession: SessionRecord? = null,
) {
    var openTrip: TripRef? = null
        private set

    var lastSnapshot: Snapshot? = null
        private set

    /** Phone time of the last successful sample — the seal timer reads this. */
    var lastSampleMs: Long = 0L
        private set

    /** Why the last seal happened, for the log and the notification copy. */
    var lastSealReason: String? = null
        private set

    private var accumulator = TripAccumulator()
    private var ip: String = initialSession?.ip ?: ""
    private var session: SessionRecord? = initialSession

    /**
     * The `#vehicle=` line for the next file this recorder creates ("" = no tag).
     *
     * The app - not the recorder - decides who a trip belongs to: the recorder is
     * handed samples, and a sample carries no identity. What it is handed instead
     * is this line, rendered by the service from the vehicle card that was selected
     * when the drive started. It is deliberately a snapshot rather than a live
     * lookup: renaming a vehicle later must not rewrite a drive that already
     * happened, and [bindVehicle] during an open trip changes the next file, never
     * the one being written.
     */
    private var tag: String = ""

    /**
     * Say whose drive this is. Empty [vehicleId] (no card selected) means no tag:
     * an untagged file is honest, while a tag with a half-known id would be a guess.
     */
    fun bindVehicle(vehicleId: String, eff: String, name: String) {
        val next = TripFormat.tagLine(vehicleId, eff, name)
        if (next != tag) tag = next
    }

    /**
     * A trip sealed by the 12 h cap must not be re-opened by the next sample of
     * the same ESP session — otherwise the cap would never actually split the
     * the file. Remembered here rather than in [SessionRecord] so the persisted
     * session format stays unchanged. The cost of holding it in memory only: if
     * the app itself restarts inside the 24 h re-open window, that trip may be
     * continued past the cap — still one file per ESP power session, which is the
     * rule that matters, just longer than 12 h.
     */
    private var cappedTripPath: String? = null

    fun ip(): String = ip

    /**
     * Point the recorder at a device. A different ip means a different unit, so
     * any persisted session for the previous one must not be reused — and an open
     * trip is sealed first, because two dashboards can never be one drive: their
     * uptimes are unrelated, and merging them would invent a single trip out of
     * two devices.
     */
    fun bindIp(newIp: String) {
        if (ip != newIp) {
            if (openTrip != null && ip.isNotEmpty()) seal("device-changed")
            ip = newIp
            session = null
        }
    }

    val isRecording: Boolean get() = openTrip != null

    /** Running summary of the open trip, straight from RAM (no file re-read). */
    fun summary(): TripSummary? = accumulator.summary()

    fun sessionRecord(): SessionRecord? = session

    /** Feed one successful snapshot; returns what happened for logging. */
    fun onSnapshot(newIp: String, s: Snapshot): TripEvent {
        bindIp(newIp)
        val now = nowMs()
        var opened = false
        var reopened = false
        var sealedBeforeOpen = false

        if (openTrip != null) {
            val prev = lastSnapshot
            if (prev != null && rebooted(prev, s)) {
                seal("esp-reboot")
                sealedBeforeOpen = true
            } else if (now - (openTrip?.startMs ?: now) >= TripPolicy.MAX_TRIP_MS) {
                cappedTripPath = openTrip?.path
                seal("session-cap-12h")
                sealedBeforeOpen = true
            }
        }

        if (openTrip == null) {
            val reuse = reusableSession(s, now)
            if (reuse != null && reuse.tripPath != null) {
                writer.reopen(reuse.tripPath)
                seedAccumulator(reuse.tripPath)
                openTrip = TripRef(reuse.tripPath, reuse.tripStartMs)
                reopened = true
                opened = true
            } else {
                val path = pathFor(now)
                writer.create(path, tag)
                accumulator = TripAccumulator()
                openTrip = TripRef(path, now)
                opened = true
            }
        }

        val trip = openTrip ?: return TripEvent.NONE
        lastSampleMs = now
        lastSnapshot = s
        val sample = TripSample(now, s)
        accumulator.add(sample)
        writer.append(trip.path, TripFormat.line(sample))
        session = SessionRecord(
            ip = ip,
            lastUp = s.up,
            lastSampleMs = now,
            lastMaxSpeed = maxOf(session?.lastMaxSpeed ?: 0.0, s.max),
            tripPath = trip.path,
            tripStartMs = trip.startMs,
            sealed = false,
        )
        return when {
            reopened -> TripEvent.REOPENED
            sealedBeforeOpen -> TripEvent.SEALED_THEN_OPENED
            opened -> TripEvent.OPENED
            else -> TripEvent.APPENDED
        }
    }

    /**
     * Called every tick whether or not the poll worked. Seals the trip once the
     * device has been silent past [TripPolicy.SEAL_AFTER_MS].
     */
    fun onTick(): TripEvent {
        val trip = openTrip ?: return TripEvent.NONE
        val last = lastSampleMs
        if (last != 0L && nowMs() - last > TripPolicy.SEAL_AFTER_MS) {
            seal("device-silent")
            return TripEvent.SEALED
        }
        return TripEvent.NONE
    }

    /** Wi-Fi / LAN went away: the device cannot answer, so close the trip now. */
    fun onNetworkLost(): TripEvent {
        if (openTrip == null) return TripEvent.NONE
        seal("network-lost")
        return TripEvent.SEALED
    }

    /** Service stopping: seal, leaving a complete file. */
    fun stop(): TripEvent {
        if (openTrip == null) return TripEvent.NONE
        seal("service-stop")
        return TripEvent.SEALED
    }

    private fun rebooted(prev: Snapshot, s: Snapshot): Boolean {
        // Uptime went backwards: the ESP rebooted, end of session.
        if (s.up < prev.up) return true
        // `max` is RAM-only state on the ESP: it returns to 0 on a reboot and
        // nowhere else (no NVS, no manual reset). If it dropped while `up` kept
        // climbing, the unit restarted inside a window we were not polling in.
        return s.max < prev.max - MAX_SPEED_EPS
    }

    private fun reusableSession(s: Snapshot, now: Long): SessionRecord? {
        val r = session ?: return null
        val path = r.tripPath ?: return null
        if (!r.sealed) return null
        if (path == cappedTripPath) return null
        if (!writer.exists(path)) return null
        if (now - r.lastSampleMs > TripPolicy.REOPEN_WINDOW_MS) return null
        if (s.up < r.lastUp) return null
        // Same boot: the ESP's clock may not be set, but its uptime still advances
        // at one second per second, so it cannot have gained more time than the
        // phone has elapsed (plus slack for phone sleep and the ESP's NTP jump).
        val elapsedMs = now - r.lastSampleMs
        val upDeltaMs = (s.up - r.lastUp) * 1000L
        if (upDeltaMs > elapsedMs + TripPolicy.UP_SLACK_MS) return null
        if (s.max < r.lastMaxSpeed - MAX_SPEED_EPS) return null
        return r
    }

    /**
     * Re-opened files already hold samples; the running summary has to start from
     * them or the "Recording…" row would show only the part after the gap.
     */
    private fun seedAccumulator(path: String) {
        accumulator = TripAccumulator()
        writer.readDataLines(path).forEach { line ->
            TripFormat.parseLine(line)?.let { accumulator.add(it) }
        }
    }

    private fun seal(reason: String) {
        val trip = openTrip ?: return
        writer.seal(trip.path)
        openTrip = null
        session = (session ?: SessionRecord(
            ip = ip,
            lastUp = lastSnapshot?.up ?: 0L,
            lastSampleMs = lastSampleMs,
            lastMaxSpeed = 0.0,
            tripPath = trip.path,
            tripStartMs = trip.startMs,
            sealed = false,
        )).copy(sealed = true)
        lastSealReason = reason
    }

    companion object {
        /** Guard against float noise when comparing the ESP's session peak. */
        private const val MAX_SPEED_EPS = 0.15
    }
}

/**
 * Text form of a [SessionRecord] for preference storage: it is what lets a trip
 * be continued after the app was killed between drives. The ip is not stored in
 * the value — it is the storage key — so [decode] takes it as an argument.
 */
object SessionCodec {
    private const val V = "s1"

    fun encode(r: SessionRecord): String = listOf(
        V, r.lastUp, r.lastSampleMs, String.format(java.util.Locale.US, "%.2f", r.lastMaxSpeed),
        if (r.sealed) 1 else 0, r.tripStartMs, r.tripPath ?: "",
    ).joinToString(";")

    fun decode(ip: String, text: String): SessionRecord? {
        val p = text.split(";", limit = 7)
        if (p.size != 7 || p[0] != V) return null
        return try {
            SessionRecord(
                ip = ip,
                lastUp = p[1].toLong(),
                lastSampleMs = p[2].toLong(),
                lastMaxSpeed = p[3].toDouble(),
                sealed = p[4] == "1",
                tripStartMs = p[5].toLong(),
                tripPath = p[6].ifEmpty { null },
            )
        } catch (e: Exception) {
            null
        }
    }
}

/** Reading sealed (or still-open) trip files back into memory. */
object TripReader {
    fun samples(path: String): List<TripSample> {
        val out = ArrayList<TripSample>()
        forEachLine(path) { line ->
            if (!TripFormat.skippable(line)) {
                TripFormat.parseLine(line)?.let { out.add(it) }
            }
        }
        return out
    }

    /**
     * Summary of a file on disk. A malformed or truncated last line is skipped —
     * a crash mid-write costs that one sample, not the trip.
     */
    fun summarize(path: String): TripSummary? {
        val acc = TripAccumulator()
        var any = false
        forEachLine(path) { line ->
            if (!TripFormat.skippable(line)) {
                TripFormat.parseLine(line)?.let {
                    acc.add(it)
                    any = true
                }
            }
        }
        return if (any) acc.summary() else null
    }

    private fun forEachLine(path: String, block: (String) -> Unit) {
        val f = File(path)
        if (!f.exists()) return
        try {
            f.forEachLine(action = block)
        } catch (e: Exception) {
            // Partial reads (storage revoked, file deleted mid-read) leave the
            // caller with whatever was parsed already.
        }
    }
}
