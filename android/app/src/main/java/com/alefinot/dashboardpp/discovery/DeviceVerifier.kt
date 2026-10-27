package com.alefinot.dashboardpp.discovery

import org.json.JSONObject
import okhttp3.OkHttpClient
import okhttp3.Request
import com.alefinot.dashboardpp.vehicles.VehicleMatch
import java.util.concurrent.TimeUnit

sealed interface VerifyResult {
    /**
     * [mac] is the unit's base eFuse MAC, read from the same /api/perf body that
     * proved the signature - identity therefore costs zero extra requests. It is
     * null for firmware that does not report it, which is a normal result: the
     * vehicle rules fall back to the address.
     */
    data class Verified(val ip: String, val version: String?, val mac: String?) : VerifyResult
    object Failed : VerifyResult
}

/**
 * Confirms a candidate IP is really the Dashboard++ ESP32 by checking the
 * /api/perf JSON signature, and reads the device identity out of that same
 * answer; the firmware version for the settings screen is the one extra request.
 */
class DeviceVerifier {
    private val client = OkHttpClient.Builder()
        .connectTimeout(2, TimeUnit.SECONDS)
        .readTimeout(2, TimeUnit.SECONDS)
        .build()

    suspend fun verify(ip: String): VerifyResult {
        return try {
            val response = client.newCall(
                Request.Builder().url("http://$ip/api/perf").build()
            ).execute()
            val body = response.body?.string() ?: ""
            response.close()
            val ok = response.code in 200..299 &&
                body.contains("cpu_temp") &&
                body.contains("fps_current") &&
                body.contains("uptime_s")
            if (!ok) return VerifyResult.Failed
            val mac = VehicleMatch.normalizeMac(readMac(body))
            val version = fetchVersion(ip)
            VerifyResult.Verified(ip, version, mac)
        } catch (e: Exception) {
            VerifyResult.Failed
        }
    }

    /**
     * The `mac` field of an already-accepted /api/perf body. Its own try/catch on
     * purpose: a device that passed the signature check must stay verified even if
     * that field is absent, JSON-null, or the body is not parseable, so a read
     * failure is "no identity" rather than "not the dashboard". org.json is the
     * right reader here (as it is for /api/ota/check) because /api/perf is a nested
     * document - api/FlatJson deliberately returns null on nesting.
     */
    private fun readMac(body: String): String? {
        return try {
            JSONObject(body).optString("mac").ifEmpty { null }
        } catch (e: Exception) {
            null
        }
    }

    private fun fetchVersion(ip: String): String? {
        return try {
            val response = client.newCall(
                Request.Builder().url("http://$ip/api/ota/check").build()
            ).execute()
            val body = response.body?.string() ?: ""
            response.close()
            if (response.code in 200..299) {
                val json = JSONObject(body)
                val v = json.optString("current_version")
                if (v.isNotEmpty()) v else null
            } else null
        } catch (e: Exception) {
            null
        }
    }

}
