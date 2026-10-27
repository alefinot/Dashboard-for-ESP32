package com.alefinot.dashboardpp.viewmodel

import android.content.Context
import android.net.ConnectivityManager
import android.net.Network
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
    private val cm: ConnectivityManager? =
        appContext.getSystemService(Context.CONNECTIVITY_SERVICE) as? ConnectivityManager
    private var networkWatcher: ConnectivityManager.NetworkCallback? = null

    val uiState = mutableStateOf<ConnectionUiState>(ConnectionUiState.Booting)
    val scanProgress = mutableStateOf(0f)
    val lastKnownIp = mutableStateOf<String?>(null)
    val manualError = mutableStateOf<String?>(null)

    fun boot() {
        watchNetwork()
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

    /**
     * Follow the default network so the app stops needing a finger for what the tablet
     * already knows. An SM-T590 that joins the house Wi-Fi, or starts the car's hotspot
     * after the fact, used to sit on RETRY NETWORK until it was tapped - and with the
     * shell in place that idle state is now a whole interface the user browses while
     * the ESP half of it waits.
     *
     * `registerDefaultNetworkCallback` (rather than `registerNetworkCallback` with no
     * NetworkRequest, which throws) follows the network the system would actually use,
     * so a VPN interface or a transient hotspot does not become a separate hunt, and
     * [ReconnectPolicy] decides what an event is worth. The two delays are the debounce:
     * a freshly joined network still needs DHCP and the ESP's own mDNS advertisement,
     * and an interface flapping behind a dock handover would otherwise start a 15 s
     * scan every time it settles.
     *
     * If registration itself fails there is nothing to show: the covers' RETRY NETWORK
     * button still runs the same ladder by hand.
     */
    private fun watchNetwork() {
        val sys = cm ?: return
        if (networkWatcher != null) return
        val cb = object : ConnectivityManager.NetworkCallback() {
            override fun onAvailable(network: Network) {
                scope.launch {
                    delay(NETWORK_RETRY_DELAY_MS)
                    if (discoveryActive.get()) return@launch
                    if (!ReconnectPolicy.shouldHunt(
                            uiState.value, Net.hasLocalLan(appContext),
                        )
                    ) {
                        return@launch
                    }
                    // The same ladder the covers' button runs: cached IP, the SoftAP
                    // guess, then mDNS and the subnet sweep.
                    retryNoWifi()
                }
            }

            override fun onLost(network: Network) {
                scope.launch {
                    delay(NETWORK_LOST_DELAY_MS)
                    if (discoveryActive.get()) return@launch
                    val state = uiState.value
                    if (!ReconnectPolicy.shouldRecheck(state)) return@launch
                    val ip = (state as ConnectionUiState.Connected).ip
                    // One real request, so a two-second Wi-Fi blip does not throw the
                    // user out of a session that is still alive.
                    if (verifier.verify(ip) is VerifyResult.Failed) {
                        uiState.value = ConnectionUiState.ConnectionLost(
                            "The network carrying $ip went away. If the ESP is still " +
                                "reachable, tap RETRY NETWORK.",
                        )
                    }
                }
            }
        }
        networkWatcher = cb
        try {
            sys.registerDefaultNetworkCallback(cb)
        } catch (e: Exception) {
            networkWatcher = null
        }
    }

    private fun stopNetworkWatch() {
        val w = networkWatcher ?: return
        networkWatcher = null
        try {
            cm?.unregisterNetworkCallback(w)
        } catch (e: Exception) {
            // Already gone: the system drops callbacks with the process anyway.
        }
    }

    fun dispose() {
        stopNetworkWatch()
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
        uiState.value = ConnectionUiState.Connected(r.ip, r.version, r.mac)
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

    private companion object {
        /** A joined network needs DHCP and the ESP's mDNS advertisement to settle. */
        const val NETWORK_RETRY_DELAY_MS = 1_500L

        /** Short enough to notice a real walk-away, long enough to survive a blip. */
        const val NETWORK_LOST_DELAY_MS = 800L
    }
}
