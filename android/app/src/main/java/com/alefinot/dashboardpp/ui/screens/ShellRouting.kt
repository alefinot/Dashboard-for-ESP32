package com.alefinot.dashboardpp.ui.screens

import com.alefinot.dashboardpp.viewmodel.ConnectMode
import com.alefinot.dashboardpp.viewmodel.ConnectionUiState

/**
 * What the shell shows for a connection state — the decision that used to live in
 * `DashboardRoot`'s `when`, drawn there as six branches and three separate screens.
 *
 * The vehicle page made this decision small on purpose. There used to be a screen per
 * state: a splash while the app hunted, a full-page scan ring while it swept the subnet,
 * and three dead ends ("no device found", "connection lost", "no Wi-Fi") that each had
 * one job — get you back to a connection — and in doing so hid the trips and the
 * settings that have nothing to do with the ESP. Every one of them is a line on the page
 * that was already there now, because the page the app opens on is the one where the
 * connection is made: the garage.
 *
 * [reason] is what that line says: why there is nothing connected, or which ladder is
 * running. With the scan ring deleted it is the only place a wait can be explained, and
 * the three modes say different things because a sweep of the whole network and a retry
 * of two remembered addresses are not the same wait.
 */
data class ShellView(
    val conn: ConnectionUiState.Connected?,
    val reason: String?,
    /**
     * What the covers' FIND DEVICE has to do: when the app has given up without a
     * network, or has nothing to reach for, re-scanning the subnet is pointless and what
     * has to be retried is whether a network exists now.
     */
    val findIsRetryNetwork: Boolean,
)

object ShellRouting {
    fun view(state: ConnectionUiState): ShellView = when (state) {
        // The garage is the first frame, so the state a cold start draws is a shell with
        // nothing connected yet and nothing said about it yet either.
        is ConnectionUiState.Idle -> ShellView(null, state.reason, true)
        is ConnectionUiState.Connecting -> ShellView(null, state.mode.stageLine(), false)
        is ConnectionUiState.Connected -> ShellView(state, null, false)
        is ConnectionUiState.Failed -> ShellView(null, state.reason, false)
    }
}

/**
 * The stage line for a running ladder. The view model publishes the mode, not the
 * sentence: what the words are is a page decision, and they belong next to the page's
 * other rules rather than inside the loop that decides the ladder.
 */
fun ConnectMode.stageLine(): String = when (this) {
    // A launch-time reconnect: the app is trying the addresses this vehicle's dashboard
    // answered from before, and the user has not asked for anything.
    ConnectMode.SILENT -> "Reaching the selected vehicle's dashboard…"
    // A retry of those addresses at a press: the line says what it is doing by naming
    // what it is relying on, which is also why it is fast.
    ConnectMode.PROBE -> "Trying the addresses that answered before…"
    // Nothing remembered answered (or nothing is remembered), so the network itself is
    // being asked: mDNS and a subnet sweep, which is the slow one.
    ConnectMode.SEARCH -> "Locating a Dashboard++ on this network…"
}

/**
 * The three rules the offline shell and the connected one share, separated from the
 * composables so they can be argued about in a test instead of on a tablet.
 */
object ShellRules {
    /** The recorder needs a device to sample: offline, it does not run. */
    fun recorderWants(ip: String?, tripsAvailable: Boolean, recordingEnabled: Boolean): Boolean =
        ip != null && tripsAvailable && recordingEnabled

    /**
     * A dimmed glyph, not a missing one: the bar keeps five slots in the same order
     * whatever happens, so the swipe never passes through a hole and every page stays
     * reachable. The ESP slot dims for no device; the trips pin dims for firmware with
     * no trip store — both say "nothing here right now", neither says "gone". The
     * vehicle slot is never dimmed: it works with nothing connected, and that is the
     * whole reason it is the home page.
     */
    fun glyphDimmed(tab: AppTab, connected: Boolean, tripsAvailable: Boolean): Boolean = when (tab) {
        AppTab.WEB -> !connected
        AppTab.TRIPS -> !tripsAvailable
        else -> false
    }

    /**
     * Which pages are still pages. Everything is, offline — Live and the ESP slot
     * answer with a cover — and the one rule that predates the offline shell stays:
     * firmware with no trip store does not get a trips page.
     */
    fun pageReachable(tab: AppTab, connected: Boolean, tripsAvailable: Boolean): Boolean =
        tab != AppTab.TRIPS || tripsAvailable

    /**
     * The one case in which the shell moves the user off a page they are standing on.
     * It exists for firmware with no trip store, where the trips page would show an
     * error instead of a list. It is *not* the case of a missing device: offline the
     * trip files are the phone's own, so the page is real and a connection dropping
     * while someone reads a trip leaves them where they were. Getting this wrong turns
     * the offline shell into the dead-end screen it replaced, one swipe at a time.
     */
    fun pageUnserved(tripsAvailable: Boolean, connected: Boolean): Boolean =
        connected && !tripsAvailable

    /**
     * Whether the ESP page may be warmed (built and fetched while the tab is shut).
     * It is Chromium work spent on a page the user is not looking at, so it is allowed
     * once per connection, only when there is a device to fetch from — offline there is
     * nothing to fetch and the offline shell's whole point is that it holds no WebView
     * process — and never once the user has opened the slot themselves.
     */
    fun shouldWarm(connected: Boolean, seen: Boolean, warmed: Boolean): Boolean =
        connected && !seen && !warmed
}
