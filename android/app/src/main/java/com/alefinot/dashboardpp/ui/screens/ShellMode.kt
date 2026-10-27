package com.alefinot.dashboardpp.ui.screens

import com.alefinot.dashboardpp.viewmodel.ConnectionUiState

/**
 * What the root puts up for a connection state — the decision that used to live in
 * `DashboardRoot`'s `when`, drawn there as six branches and three separate screens.
 *
 * It is a pure function now because the interesting part of it is not the drawing:
 * it is that four of the states below are *the same shell*. The three that used to
 * be dead ends ("no device found", "connection lost", "no Wi-Fi") each had one job
 * — get you back to a connection — and in doing so they hid the trips and the
 * settings that have nothing to do with the ESP. `Shell(conn = null)` is that same
 * page with those parts still standing.
 */
sealed interface ShellMode {
    /** The splash, for as long as the view model is booting. */
    object Splash : ShellMode

    /** Hunting for a device: the scan ring, with the line for the stage. */
    data class Scanning(val title: String) : ShellMode

    /**
     * The tabs. [conn] is null when there is no device: Live and the ESP slot answer
     * with a cover instead of a page, Trips and App Settings work in full, and the
     * recorder stays down. [reason] is the line the covers quote — it comes from the
     * view model, which is the only thing that knows what it tried.
     *
     * [findIsRetryNetwork] says what FIND DEVICE has to do: with a network but no
     * device, re-scan the subnet; with no network at all, re-scanning is pointless
     * and what has to be retried is whether a network exists now.
     */
    data class Shell(
        val conn: ConnectionUiState.Connected?,
        val reason: String?,
        val findIsRetryNetwork: Boolean,
    ) : ShellMode
}

object ShellRouting {
    fun mode(state: ConnectionUiState): ShellMode = when (state) {
        is ConnectionUiState.Booting -> ShellMode.Splash
        is ConnectionUiState.CheckingCache -> ShellMode.Scanning("Checking saved device…")
        is ConnectionUiState.CheckingSoftAp -> ShellMode.Scanning("Checking SoftAP…")
        is ConnectionUiState.Discovering -> ShellMode.Scanning("Locating Dashboard++…")
        is ConnectionUiState.Connected -> ShellMode.Shell(state, null, false)
        is ConnectionUiState.ManualEntryNeeded ->
            ShellMode.Shell(null, "No Dashboard++ found on this network.", false)
        is ConnectionUiState.ConnectionLost -> ShellMode.Shell(null, state.reason, false)
        is ConnectionUiState.NoWifi -> ShellMode.Shell(null, state.detail, true)
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
}
