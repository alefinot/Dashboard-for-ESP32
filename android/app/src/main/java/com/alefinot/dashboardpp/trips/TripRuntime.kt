package com.alefinot.dashboardpp.trips

import com.alefinot.dashboardpp.api.LiveReadings
import com.alefinot.dashboardpp.api.Snapshot
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow

/**
 * The live picture the recorder publishes for the UI (trip recording plan §5).
 * A single process-wide holder because the foreground service owns the only
 * poller: the Live tab renders these values instead of starting its own
 * connection, which is what keeps the device at one request per second.
 */
data class TripRuntimeState(
    val ip: String? = null,
    /** True while the service is alive and polling. */
    val polling: Boolean = false,
    /** True while samples are being appended to a trip file. */
    val recording: Boolean = false,
    val tripPath: String? = null,
    val tripStartMs: Long = 0L,
    val snapshot: Snapshot? = null,
    /** Running summary of the open trip; null until the first sample lands. */
    val summary: TripSummary? = null,
    val lastOkMs: Long = 0L,
    /** Set when polling stands down because the Web UI tab owns the connection. */
    val pausedForWebUi: Boolean = false,
    /** /api/live fallback, shown when the firmware has no /api/snapshot. */
    val live: LiveReadings? = null,
    val lastError: String? = null,
) {
    /** Samples are stale if nothing arrived for this long. */
    val stale: Boolean
        get() = polling && (System.currentTimeMillis() - lastOkMs) > 5_000L
}

/**
 * The owner the app hands the recorder when it starts it: which address is being
 * sampled, which card that address belongs to, that card's name at this moment,
 * and what the dashboard reported as its eFuse identity. `/api/snapshot` carries
 * none of it, and a trip file outlives the connection that created it, so the app
 * says this once, at the only moment it knows it [plan §6].
 */
data class RecorderTarget(val ip: String, val vehicleId: String, val vehicleName: String, val eff: String)

object TripRuntime {
    private val _state = MutableStateFlow(TripRuntimeState())
    val state: StateFlow<TripRuntimeState> = _state

    fun update(block: (TripRuntimeState) -> TripRuntimeState) {
        _state.value = block(_state.value)
    }

    /** Called when the device is forgotten (disconnect) so nothing leaks over. */
    fun clearConnection() {
        _state.value = TripRuntimeState(pausedForWebUi = _state.value.pausedForWebUi)
    }
}

/**
 * Whether this device can be recorded from. The cheap first pass is the version
 * string the app already read during discovery; the runtime answer comes from
 * the endpoint itself (404 on /api/snapshot while /api/live answers), which the
 * service records as [TripRuntimeState.lastError].
 */
object TripsEnabled {
    /** First firmware with /api/snapshot. */
    const val MIN_VERSION = "1.4.6"

    /**
     * Numeric comparison rather than lexicographic ("1.10.2" is older than
     * "1.4.6" to a string compare). Unknown or oddly shaped version strings are
     * treated as capable and left to the runtime 404 probe, so a version label
     * the parser does not recognise never disables a feature the device has.
     */
    fun forFirmware(version: String?): Boolean {
        if (version.isNullOrBlank()) return true
        val parts = version.trim().removePrefix("V").split(".")
        val nums = parts.take(3).mapNotNull { it.takeWhile { c -> c.isDigit() }.toIntOrNull() }
        if (nums.size < 2) return true
        val min = MIN_VERSION.split(".").map { it.toInt() }
        for (i in 0 until 3) {
            val a = nums.getOrElse(i) { 0 }
            val b = min.getOrElse(i) { 0 }
            if (a != b) return a > b
        }
        return true
    }
}
