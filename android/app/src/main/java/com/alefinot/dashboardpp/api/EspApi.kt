package com.alefinot.dashboardpp.api

import kotlinx.coroutines.sync.Mutex
import kotlinx.coroutines.sync.withLock
import okhttp3.OkHttpClient
import okhttp3.Request
import java.util.concurrent.TimeUnit

/**
 * One HTTP identity for talking to the ESP.
 *
 * The core's WebServer serves a single client at a time (NetworkServer listens
 * with a backlog of 4 and every reply is `Connection: close`), which is why the
 * WebUI itself fetches its live cells through one request per tick. The app must
 * not be the second client that overruns that backlog, so every native poller
 * shares this client — one connection pool, one dispatcher, and the [EspApiLane]
 * below keeps a second request from starting while one is still running.
 */
object EspHttp {
    val client: OkHttpClient by lazy {
        OkHttpClient.Builder()
            .connectTimeout(2, TimeUnit.SECONDS)
            .readTimeout(3, TimeUnit.SECONDS)
            .callTimeout(5, TimeUnit.SECONDS)
            .retryOnConnectionFailure(false)
            .build()
    }

    /**
     * Same pool and dispatcher, different timeouts — `newBuilder()` is cheap and
     * explicitly shares them, so discovery probes stay short without opening a
     * second client.
     */
    fun withTimeouts(connectMs: Int, readMs: Int): OkHttpClient =
        client.newBuilder()
            .connectTimeout(connectMs.toLong(), TimeUnit.MILLISECONDS)
            .readTimeout(readMs.toLong(), TimeUnit.MILLISECONDS)
            .build()
}

/** A completed HTTP answer: status plus the body, already read and closed. */
data class HttpReply(val status: Int, val body: String) {
    val ok: Boolean get() = status in 200..299
}

/**
 * Serialises requests to the device: at most one in flight, the rest queue.
 * Mirrors the WebUI's `laneFetch` for the same reason (see [EspHttp]).
 */
class ApiLane {
    private val mutex = Mutex()

    suspend fun <T> run(request: suspend () -> T): T = mutex.withLock { request() }
}

/** App-wide lane shared by the trip recorder and the Live tab poller. */
object EspApiLane {
    val lane = ApiLane()

    /**
     * Set while the Web UI tab is on screen. The WebView's own live poll is then
     * the only traffic on the device, so the native pollers stand down entirely
     * instead of queueing behind it — that keeps the total at one client.
     */
    @Volatile
    var webUiVisible: Boolean = false
}

/** The read-only endpoints the app consumes. All of them go through [EspApiLane]. */
object EspApi {

    private fun get(ip: String, path: String): HttpReply {
        EspHttp.client.newCall(Request.Builder().url("http://$ip$path").build())
            .execute().use { response ->
                return HttpReply(response.code, response.body?.string() ?: "")
            }
    }

    /** Null when the device could not be reached at all (offline, asleep, gone). */
    suspend fun getJson(ip: String, path: String): HttpReply? =
        EspApiLane.lane.run {
            try {
                get(ip, path)
            } catch (e: Exception) {
                null
            }
        }

    /** GET /api/snapshot, parsed. Null on transport failure *or* bad payload. */
    suspend fun snapshot(ip: String): Snapshot? =
        getJson(ip, "/api/snapshot")?.takeIf { it.ok }
            ?.let { Snapshot.parse(it.body) }

    /** GET /api/live, parsed — the fallback for firmware without /api/snapshot. */
    suspend fun live(ip: String): LiveReadings? =
        getJson(ip, "/api/live")?.takeIf { it.ok }
            ?.let { LiveReadings.parse(it.body) }

    /**
     * Feature detection. The endpoint was added in the firmware after V1.4.5, so
     * a device that answers 404 (or anything else that is not a snapshot body)
     * simply has less to give us. Comparing `ver` / `/api/ota/check` version
     * strings would break the moment someone sets a VERSION_OVERRIDE label, so
     * the app asks the device what it actually serves instead.
     */
    suspend fun supportsSnapshot(ip: String): Boolean = snapshot(ip) != null

    /**
     * Sentinel stored in [com.alefinot.dashboardpp.trips.TripRuntimeState.lastError]
     * when the device answers /api/live but has no /api/snapshot: the firmware
     * predates the endpoint, so the Trips tab is disabled with "needs update"
     * rather than shown broken.
     */
    const val FIRMWARE_UPDATE_REQUIRED = "firmware-update-required"
}
