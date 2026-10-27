package com.alefinot.dashboardpp.viewmodel

import android.content.Context
import androidx.compose.runtime.mutableStateOf
import com.alefinot.dashboardpp.discovery.ConnectionCache
import com.alefinot.dashboardpp.discovery.DeviceVerifier
import com.alefinot.dashboardpp.discovery.Net
import com.alefinot.dashboardpp.discovery.NsdDiscoverer
import com.alefinot.dashboardpp.discovery.SubnetScanner
import com.alefinot.dashboardpp.discovery.VerifyResult
import kotlinx.coroutines.CompletableDeferred
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.delay
import kotlinx.coroutines.launch
import kotlinx.coroutines.withTimeoutOrNull
import java.util.concurrent.atomic.AtomicBoolean

class ConnectionViewModel(context: Context) {
    private val appContext = context.applicationContext
    private val job = SupervisorJob()
    private val scope = CoroutineScope(Dispatchers.Default + job)
    private val cache = ConnectionCache(appContext)
    private val verifier = DeviceVerifier()
    private var nsd: NsdDiscoverer? = null
    private var scanner: SubnetScanner? = null
    private var discoveryJob: Job? = null
    private val discoveryActive = AtomicBoolean(false)

    val uiState = mutableStateOf<ConnectionUiState>(ConnectionUiState.Booting)
    val scanProgress = mutableStateOf(0f)
    val lastKnownIp = mutableStateOf<String?>(null)
    val manualError = mutableStateOf<String?>(null)

    fun boot() {
        scope.launch {
            delay(1200) // splash
            val cached = cache.lastIp()
            lastKnownIp.value = cached
            if (cached != null) {
                uiState.value = ConnectionUiState.CheckingCache
                if (tryConnect(cached)) return@launch
            }
            if (!Net.hasLocalLan(appContext)) {
                uiState.value = ConnectionUiState.NoWifi(
                    "No local network detected. Connect to Wi-Fi, or start " +
                        "the phone's hotspot / USB tethering (the ESP32 joins " +
                        "it as a client)."
                )
                return@launch
            }
            val ssid = Net.currentSsid(appContext)
            if (Net.isSoftAp(ssid)) {
                uiState.value = ConnectionUiState.CheckingSoftAp
                if (tryConnect(Net.SOFT_AP_IP)) return@launch
            }
            runDiscovery()
        }
    }

    fun retryNoWifi() {
        scope.launch {
            if (!Net.hasLocalLan(appContext)) {
                uiState.value = ConnectionUiState.NoWifi(
                    "Still no local network detected."
                )
                return@launch
            }
            val cached = cache.lastIp()
            lastKnownIp.value = cached
            if (cached != null) {
                uiState.value = ConnectionUiState.CheckingCache
                if (tryConnect(cached)) return@launch
            }
            val ssid = Net.currentSsid(appContext)
            if (Net.isSoftAp(ssid)) {
                uiState.value = ConnectionUiState.CheckingSoftAp
                if (tryConnect(Net.SOFT_AP_IP)) return@launch
            }
            runDiscovery()
        }
    }

    fun rescan() {
        scope.launch { runDiscovery() }
    }

    fun showManualEntry() {
        manualError.value = null
        uiState.value = ConnectionUiState.ManualEntryNeeded
    }

    fun submitManualIp(ip: String) {
        scope.launch {
            if (!Net.isValidIpv4(ip.trim())) {
                manualError.value = "Enter a valid IP address, e.g. 192.168.1.42"
                return@launch
            }
            manualError.value = null
            val r = verifier.verify(ip.trim())
            when (r) {
                is VerifyResult.Verified -> connect(r)
                is VerifyResult.Failed -> {
                    manualError.value = "No Dashboard++ response from that address."
                }
            }
        }
    }

    fun reconnect(ip: String) {
        scope.launch {
            val r = verifier.verify(ip)
            when (r) {
                is VerifyResult.Verified -> connect(r)
                is VerifyResult.Failed -> {
                    uiState.value = ConnectionUiState.ConnectionLost(
                        "Cannot reach $ip. The device may be off, or on a different network."
                    )
                }
            }
        }
    }

    fun forgetDevice(ip: String) {
        cache.clear()
        lastKnownIp.value = null
        runDiscovery()
    }

    fun onWebLost(ip: String) {
        scope.launch {
            uiState.value = ConnectionUiState.ConnectionLost(
                "Lost connection to $ip. The device may be off or on a different network."
            )
        }
    }

    fun dispose() {
        cleanupScan()
        job.cancel()
    }

    private suspend fun tryConnect(ip: String): Boolean {
        val r = verifier.verify(ip)
        if (r is VerifyResult.Verified) {
            connect(r)
            return true
        }
        return false
    }

    private fun connect(r: VerifyResult.Verified) {
        cache.setIp(r.ip)
        lastKnownIp.value = r.ip
        uiState.value = ConnectionUiState.Connected(r.ip, r.version)
    }

    /**
     * Runs NSD discovery and the subnet scan in parallel. Whoever hands us
     * a verified IP first wins; the 15s timeout covers the whole discovery
     * (worst-case scan time is a few seconds on a healthy LAN).
     * A new run cancels the previous one's timeout so a stale timeout
     * can't override a fresh discovery result.
     */
    private fun runDiscovery() {
        discoveryJob?.cancel()
        discoveryJob = scope.launch {
            cleanupScan()
            // No network means there is nothing to scan, and spending 15 s trying
            // ends in a message that blames the device ("no Dashboard++ found on this
            // network") when the truth is that there is no network. The offline shell
            // reads the difference: NoWifi is the state whose cover offers RETRY
            // NETWORK and the system Wi-Fi panel.
            if (!Net.hasLocalLan(appContext)) {
                discoveryActive.set(false)
                uiState.value = ConnectionUiState.NoWifi(
                    "No local network detected. Connect to Wi-Fi, or start " +
                        "the phone's hotspot / USB tethering (the ESP32 joins " +
                        "it as a client)."
                )
                return@launch
            }
            discoveryActive.set(true)
            uiState.value = ConnectionUiState.Discovering
            scanProgress.value = 0f
            val nsd = NsdDiscoverer(appContext)
            val scanner = SubnetScanner(appContext)
            this@ConnectionViewModel.nsd = nsd
            this@ConnectionViewModel.scanner = scanner
            val found = CompletableDeferred<Unit>()
            nsd.start { ip, _ -> handleFound(ip, found) }
            scanner.start(
                onProgress = { p -> scanProgress.value = p },
                onFound = { ip -> handleFound(ip, found) }
            )
            val success = withTimeoutOrNull(15_000) { found.await() } != null
            discoveryActive.set(false)
            cleanupScan()
            if (!success) {
                uiState.value = ConnectionUiState.ManualEntryNeeded
            }
        }
    }

    private fun handleFound(ip: String, found: CompletableDeferred<Unit>) {
        scope.launch {
            val r = verifier.verify(ip)
            val result = if (r is VerifyResult.Failed) {
                // The ESP can be mid-boot when first probed — give it a retry.
                delay(400)
                verifier.verify(ip)
            } else {
                r
            }
            if (result is VerifyResult.Verified && discoveryActive.compareAndSet(true, false)) {
                found.complete(Unit)
                connect(result)
            }
        }
    }

    private fun cleanupScan() {
        nsd?.stop()
        scanner?.cancel()
        nsd = null
        scanner = null
    }
}
