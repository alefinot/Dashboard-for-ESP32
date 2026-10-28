package com.alefinot.dashboardpp.ui.screens

import com.alefinot.dashboardpp.viewmodel.ConnectMode
import com.alefinot.dashboardpp.viewmodel.ConnectionUiState

/**
 * What the root puts up for a connection state — the decision that used to live in
 * `DashboardRoot`'s `when`, drawn there as six branches and three separate screens.
 *
 * It is a pure function now because the interesting part of it is not the drawing:
 * it is that *every* state below is the same shell. The three that used to be dead
 * ends ("no device found", "connection lost", "no Wi-Fi") each had one job — get you
 * back to a connection — and in doing so they hid the trips and the settings that have
 * nothing to do with the ESP. `Shell(conn = null)` is that same page with those parts
 * still standing, and the vehicle page is the page it opens on: which is why there is
 * no splash and no scan screen in this list any more.
 */
sealed interface ShellMode {
    /**
     * Hunting the *user* asked for: the scan ring, with the line for the stage. The
     * silent reconnect at launch is deliberately not here — it changes nothing on
     * screen, and the card being reached for says SEARCHING in its own status line.
     */
    data class Scanning(val title: String) : ShellMode

    /**
     * The tabs. [conn] is null when there is no device: Live and the ESP slot answer
     * with a cover instead of a page, Trips and App Settings work in full, and the
     * recorder stays down. [reason] is the line the covers quote — it comes from the
     * view model, which is the only thing that knows what it tried.
     *
     * [findIsRetryNetwork] says what FIND DEVICE has to do: when the app gave up
     * without a network, or has nothing to reach for, re-scanning the subnet is
     * pointless and what has to be retried is whether a network exists now.
     */
    data class Shell(
        val conn: ConnectionUiState.Connected?,
        val reason: String?,
        val findIsRetryNetwork: Boolean,
    ) : ShellMode
}

object ShellRouting {
    fun mode(state: ConnectionUiState): ShellMode = when (state) {
        // The garage is the first frame, so the state a cold start draws is a shell
        // with nothing connected yet — no splash to sit through, and no scan ring while
        // a handful of remembered addresses are dialed in the background.
        is ConnectionUiState.Idle -> ShellMode.Shell(null, state.reason, true)
        is ConnectionUiState.Connecting -> when (state.mode) {
            ConnectMode.SILENT -> ShellMode.Shell(null, null, false)
            ConnectMode.SEARCH -> ShellMode.Scanning("Locating Dashboard++…")
            ConnectMode.PROBE -> ShellMode.Scanning("Reaching the selected vehicle…")
        }
        is ConnectionUiState.Connected -> ShellMode.Shell(state, null, false)
        is ConnectionUiState.Failed -> ShellMode.Shell(null, state.reason, false)
    }
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
     * no trip store — both say "nothing here right now", neither says "gone".
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
