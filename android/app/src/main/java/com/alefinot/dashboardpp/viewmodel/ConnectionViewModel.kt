package com.alefinot.dashboardpp.viewmodel

import android.content.Context
import android.net.ConnectivityManager
import android.net.Network
import androidx.compose.runtime.mutableStateOf
import com.alefinot.dashboardpp.discovery.DeviceVerifier
import com.alefinot.dashboardpp.discovery.Net
import com.alefinot.dashboardpp.discovery.NsdDiscoverer
import com.alefinot.dashboardpp.discovery.SubnetScanner
import com.alefinot.dashboardpp.discovery.VerifyResult
import com.alefinot.dashboardpp.vehicles.VehicleStore
import kotlinx.coroutines.CompletableDeferred
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.currentCoroutineContext
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.first
import kotlinx.coroutines.isActive
import kotlinx.coroutines.launch
import kotlinx.coroutines.withTimeoutOrNull
import java.util.concurrent.atomic.AtomicBoolean

/**
 * The app's half of "is a dashboard connected, and which vehicle's dashboard is it".
 *
 * The ladder itself is not decided here — [ConnectionCore] decides it one step at a
 * time, and this class is the loop plus the network calls: it reads the garage, dials
 * what the core says to dial, hands the answer back, and publishes whatever state and
 * records the core produced. Keeping the two apart is what lets the identity rules (a
 * MAC outranks an address, an unclaimed unit is offered rather than adopted) be tested
 * without a tablet.
 *
 * Two things the old version of this class did are deliberately gone:
 *
 *  - **The boot ladder.** Cold start used to check a cached IP, guess the SoftAP, then
 *    run mDNS and a subnet sweep — up to 15 s of network before the app showed
 *    anything, and the thing it was hunting for was *any* Dashboard++. Now cold start
 *    reads the garage, marks the selected card, and silently probes only that card's
 *    remembered addresses: a handful of requests, no multicast lock, and the vehicle
 *    page is the first frame either way.
 *  - **The last-connected cache.** `ConnectionCache` remembered one IP for the whole
 *    app. That is now the selected card's own `ips` list, which is better in every way
 *    that matter: it is per vehicle, it is capped most-recent-first, and it is
 *    something the user can see and edit.
 */
class ConnectionViewModel(
    context: Context,
    private val vehicles: VehicleStore = VehicleStore(context),
) {
    private val appContext = context.applicationContext
    private val job = SupervisorJob()
    private val scope = CoroutineScope(Dispatchers.Default + job)
    private val verifier = DeviceVerifier()
    private var ladder: Job? = null
    private var nsd: NsdDiscoverer? = null
    private var scanner: SubnetScanner? = null
    private val discoveryActive = AtomicBoolean(false)
    private val cm: ConnectivityManager? =
        appContext.getSystemService(Context.CONNECTIVITY_SERVICE) as? ConnectivityManager
    private var networkWatcher: ConnectivityManager.NetworkCallback? = null

    val uiState = mutableStateOf<ConnectionUiState>(ConnectionUiState.Idle(null))
    val scanProgress = mutableStateOf(0f)

    /** The address of the last successful connect — the vehicle page's status line. */
    val lastKnownIp = mutableStateOf<String?>(null)

    /** Error line for the manual-address panel. */
    val manualError = mutableStateOf<String?>(null)

    /**
     * A dashboard that answered and is claimed by no card. The vehicle page turns this
     * into a NAME IT offer; nothing writes it into a record until the user says so,
     * because adopting silently is how two cards end up owning one dashboard.
     */
    val unclaimed = mutableStateOf<UnclaimedUnit?>(null)

    /**
     * One line for the vehicle page, set when a connect landed on a card other than the
     * one that was selected — the unit's MAC outranks the address that was dialed, so
     * the selection moved (plan §4) and the page has to say why it shows a different
     * vehicle than the card that was pressed.
     */
    val notice = mutableStateOf<String?>(null)

    /** The card the app is currently reaching for. */
    val selectedVehicleId = mutableStateOf<String?>(null)

    /** The garage, for the vehicle page. */
    fun vehicleCards() = vehicles.all()

    fun boot() {
        watchNetwork()
        scope.launch {
            selectedVehicleId.value = vehicles.selectedId()
            startAttempt(ConnectMode.SILENT)
        }
    }

    /** RE-SCAN NETWORK, from App Settings: the full ladder for the selected card. */
    fun rescan() {
        startAttempt(ConnectMode.SEARCH)
    }

    /** The covers' RETRY NETWORK: check that a network exists, then reach again. */
    fun retryNoWifi() {
        scope.launch {
            if (!Net.hasLocalLan(appContext)) {
                uiState.value = ConnectionUiState.Failed(
                    "Still no local network detected. Connect to Wi-Fi, or start " +
                        "the phone's hotspot / USB tethering (the ESP32 joins it as " +
                        "a client).",
                )
                return@launch
            }
            startAttempt(ConnectMode.SEARCH)
        }
    }

    /**
     * Reach for [vehicleId] — the vehicle page's CONNECT / RECONNECT.
     *
     * [mode] is SEARCH for a card the user wants found (its remembered addresses, then
     * the dashboard's own SoftAP, then mDNS and the subnet sweep) and PROBE for a card
     * they want retried now (addresses only). Either way the previous unit is dropped
     * first: one device at a time, exactly as before, and the recorder follows the
     * address in [ConnectionUiState.Connected], so it seals and stops on its own.
     */
    fun connectVehicle(vehicleId: String, mode: ConnectMode = ConnectMode.SEARCH) {
        ladder?.cancel()
        scope.launch {
            vehicles.select(vehicleId)
            selectedVehicleId.value = vehicleId
            notice.value = null
            unclaimed.value = null
            startAttempt(mode)
        }
    }

    /**
     * Highlight [vehicleId] without reaching for it.
     *
     * A connected session always belongs to the card it connected through, so selecting
     * a different card while one is connected ends that connection — otherwise the next
     * drive would be tagged to a vehicle that is not the one being driven. The recorder
     * stops because the state is no longer `Connected`, which is the same path a
     * DISCONNECT takes.
     */
    fun selectVehicle(vehicleId: String) {
        ladder?.cancel()
        scope.launch {
            vehicles.select(vehicleId)
            selectedVehicleId.value = vehicleId
            notice.value = null
            unclaimed.value = null
            if (uiState.value is ConnectionUiState.Connected) {
                uiState.value = ConnectionUiState.Idle(null)
            }
        }
    }

    /** The connected card's DISCONNECT. The selection stays; the unit goes. */
    fun disconnect() {
        ladder?.cancel()
        if (uiState.value !is ConnectionUiState.Connected) return
        uiState.value = ConnectionUiState.Idle(null)
    }

    /**
     * A typed address is a probe like any other: the answer goes through the same
     * identity rules, so typing an address never creates a vehicle on its own. What
     * resolves decides what happens — the unit belongs to a card and connects, it
     * belongs to nobody and becomes a NAME IT offer, or it reports no identity at all
     * and the app says so rather than guessing a card from an address.
     */
    fun submitManualIp(ip: String) {
        scope.launch {
            val address = ip.trim()
            if (!Net.isValidIpv4(address)) {
                manualError.value = "Enter a valid IP address, e.g. 192.168.1.42"
                return@launch
            }
            manualError.value = null
            val outcome = probe(address)
            if (outcome is ProbeOutcome.Fail) {
                manualError.value = "No Dashboard++ response from that address."
                return@launch
            }
            val r = ConnectionCore.decide(
                vehicles.all().first(), selectedVehicleId.value,
                tried = listOf(address), scanDone = true,
                outcome = outcome, mode = ConnectMode.SEARCH, nowMs = now(),
            )
            r.persist?.let { vehicles.put(it) }
            unclaimed.value = r.adoption
            r.notice?.let { notice.value = it }
            when (val n = r.next) {
                is NextAction.Connect -> applyConnect(n)
                is NextAction.Failed -> {
                    uiState.value = ConnectionUiState.Failed(n.reason)
                    manualError.value = n.reason
                }
                is NextAction.Idle -> manualError.value = n.reason
                else -> Unit // unreachable: the ladder is not running here
            }
        }
    }

    /** A Web UI load that failed: the page says so, and the ladder decides what next. */
    fun onWebLost(ip: String) {
        scope.launch {
            uiState.value = ConnectionUiState.Failed(
                "Lost connection to $ip. The device may be off or on a different network.",
            )
        }
    }

    // ------------------------------------------------------------- the ladder loop

    private fun startAttempt(mode: ConnectMode) {
        ladder?.cancel()
        ladder = scope.launch { runLadder(mode) }
    }

    /**
     * Drive [ConnectionCore.decide] until it produces a terminal step.
     *
     * [tried] and [scanDone] are the attempt's memory — the core is pure, so the loop
     * carries what has been dialed and whether the network has already been swept, and
     * hands it back on every step. The card list is re-read each step because a step
     * may have written one (a connect adopts an identity), and because a card can be
     * deleted from under an attempt: cancelling this job is how a new selection
     * supersedes an old one, and [currentCoroutineContext] is what notices.
     */
    private suspend fun runLadder(mode: ConnectMode) {
        unclaimed.value = null
        uiState.value = ConnectionUiState.Connecting(mode)
        // The SoftAP guess is only worth making when the phone is actually sitting on
        // the dashboard's own access point, and only on a ladder the user asked for.
        val onSoftAp = mode == ConnectMode.SEARCH && Net.isSoftAp(Net.currentSsid(appContext))
        var tried = emptyList<String>()
        var scanDone = false
        var outcome: ProbeOutcome? = null
        while (currentCoroutineContext().isActive) {
            val r = ConnectionCore.decide(
                vehicles.all().first(), selectedVehicleId.value,
                tried, scanDone, outcome, mode, now(),
            )
            r.persist?.let { vehicles.put(it) }
            unclaimed.value = r.adoption
            r.notice?.let { notice.value = it }
            when (val n = r.next) {
                is NextAction.Idle -> {
                    uiState.value = ConnectionUiState.Idle(n.reason)
                    return
                }
                is NextAction.Failed -> {
                    uiState.value = ConnectionUiState.Failed(n.reason)
                    return
                }
                is NextAction.Probe -> {
                    tried = tried + n.address
                    outcome = probe(n.address)
                }
                is NextAction.ScanThenProbe -> {
                    // Plan §4's order: the card's own addresses, then the SoftAP guess,
                    // then the sweep. The sweep is last because it is the slowest and
                    // the least specific thing available.
                    if (!onSoftAp || Net.SOFT_AP_IP in tried) {
                        scanDone = true
                        outcome = sweep()
                    } else {
                        tried = tried + Net.SOFT_AP_IP
                        outcome = probe(Net.SOFT_AP_IP)
                    }
                }
                is NextAction.Connect -> {
                    applyConnect(n)
                    return
                }
            }
        }
    }

    private suspend fun applyConnect(n: NextAction.Connect) {
        // The step before a Connect may have suspended (writing an adopted identity),
        // so a tap on another card can have superseded this attempt while it was in
        // flight. Re-checking here is what keeps a dead ladder from publishing a
        // connection the user has already moved past.
        if (!currentCoroutineContext().isActive) return
        if (n.vehicleId != selectedVehicleId.value) {
            // The unit's MAC outranks the address that was dialed, so the selection
            // moves to the card that owns it — and `notice`, already published by the
            // caller, is what tells the user the page changed under them.
            selectedVehicleId.value = n.vehicleId
            scope.launch { vehicles.select(n.vehicleId) }
        }
        lastKnownIp.value = n.ip
        uiState.value = ConnectionUiState.Connected(n.ip, n.version, n.mac, n.vehicleId)
    }

    private suspend fun probe(ip: String): ProbeOutcome = when (val r = verifier.verify(ip)) {
        is VerifyResult.Verified -> ProbeOutcome.Ok(r.ip, r.version, r.mac)
        is VerifyResult.Failed -> ProbeOutcome.Fail(ip)
    }

    /**
     * mNSD discovery and the subnet scan in parallel; whoever hands back a verified
     * Dashboard++ first wins. The 15 s timeout covers the whole sweep (a healthy LAN
     * finishes the subnet pass in a few seconds). A device that is mid-boot is given
     * one retry, because mDNS often announces a host before its HTTP server is up.
     */
    private suspend fun sweep(): ProbeOutcome {
        if (!Net.hasLocalLan(appContext)) return ProbeOutcome.Fail(SWEEP)
        discoveryActive.set(true)
        scanProgress.value = 0f
        val found = CompletableDeferred<ProbeOutcome.Ok>()
        val nsd = NsdDiscoverer(appContext)
        val scanner = SubnetScanner(appContext)
        this.nsd = nsd
        this.scanner = scanner
        nsd.start { ip, _ -> handleFound(ip, found) }
        scanner.start(
            onProgress = { p -> scanProgress.value = p },
            onFound = { ip -> handleFound(ip, found) },
        )
        val hit = withTimeoutOrNull(SWEEP_TIMEOUT_MS) { found.await() }
        discoveryActive.set(false)
        cleanupScan()
        return hit ?: ProbeOutcome.Fail(SWEEP)
    }

    private fun handleFound(ip: String, found: CompletableDeferred<ProbeOutcome.Ok>) {
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
                found.complete(ProbeOutcome.Ok(result.ip, result.version, result.mac))
            }
        }
    }

    private fun cleanupScan() {
        nsd?.stop()
        scanner?.cancel()
        nsd = null
        scanner = null
    }

    // ------------------------------------------------------------ the network watcher

    /**
     * Follow the default network so the app stops needing a finger for what the tablet
     * already knows: an SM-T590 that joins the house Wi-Fi, or starts the car's hotspot
     * after the fact, used to sit on RETRY NETWORK until it was tapped.
     *
     * `registerDefaultNetworkCallback` (rather than `registerNetworkCallback` with no
     * NetworkRequest, which throws) follows the network the system would actually use,
     * so a VPN interface or a transient hotspot does not become a separate hunt, and
     * [ReconnectPolicy] decides what an event is worth. The two delays are the debounce:
     * a freshly joined network still needs DHCP and the ESP's own mDNS advertisement,
     * and an interface flapping behind a dock handover would otherwise start a sweep
     * every time it settles.
     *
     * What it reaches for is scoped to the selected card, and a watch never sweeps the
     * network on its own: an unattended sweep is how a tablet that joins the neighbour's
     * Wi-Fi ends up connected to a dashboard in someone else's car.
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
                            uiState.value,
                            Net.hasLocalLan(appContext),
                            selectedVehicleId.value != null,
                        )
                    ) {
                        return@launch
                    }
                    startAttempt(ConnectMode.PROBE)
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
                        uiState.value = ConnectionUiState.Failed(
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
        ladder?.cancel()
        cleanupScan()
        job.cancel()
    }

    private fun now(): Long = System.currentTimeMillis()

    private companion object {
        /** A joined network needs DHCP and the ESP's mDNS advertisement to settle. */
        const val NETWORK_RETRY_DELAY_MS = 1_500L

        /** Short enough to notice a real walk-away, long enough to survive a blip. */
        const val NETWORK_LOST_DELAY_MS = 800L

        /** Worst case for mDNS plus the subnet sweep. */
        const val SWEEP_TIMEOUT_MS = 15_000L

        /** Stands in for "the sweep found nothing" in a probe outcome. */
        const val SWEEP = "sweep"
    }
}
