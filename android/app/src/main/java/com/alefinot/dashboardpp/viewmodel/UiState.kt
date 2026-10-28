package com.alefinot.dashboardpp.viewmodel

sealed interface ConnectionUiState {
    /**
     * Nothing is being reached for, and nothing is connected. [reason] is what the
     * vehicle page shows: why there is nothing (no card selected, the card has never
     * connected), or null when the app simply has not tried yet — which is the state a
     * cold start draws, so the garage is on screen before any network request runs.
     */
    data class Idle(val reason: String?) : ConnectionUiState

    /**
     * An attempt is running. [mode] says how far it is going, which is what tells the
     * page whether to show a scan ring: a [ConnectMode.SILENT] reconnect must not put
     * any screen in front of the garage, while a search the user asked for shows its
     * progress inside the card.
     */
    data class Connecting(val mode: ConnectMode) : ConnectionUiState

    /**
     * A unit is connected. [mac] is its eFuse identity from the verify step (null on
     * firmware that predates it), and [vehicleId] is the card it belongs to — the two
     * together are what the trip log tags, so a drive records which vehicle it was in
     * rather than which address answered.
     */
    data class Connected(
        val ip: String,
        val version: String?,
        val mac: String? = null,
        val vehicleId: String,
    ) : ConnectionUiState

    /**
     * An attempt finished and nothing verified. [reason] is the line the page shows —
     * it absorbs what used to be three separate screens (`NoWifi`,
     * `ManualEntryNeeded`, `ConnectionLost`), all of which are now a line under the
     * card that was being reached for, with the rest of the app still on screen.
     */
    data class Failed(val reason: String) : ConnectionUiState
}

/**
 * What a network event entitles the app to do.
 *
 * `ConnectivityManager` calls back when the network says so, not when the user does: a
 * VPN interface registering, a hotspot starting, Wi-Fi flapping behind a dock Ethernet
 * handover. Every event acted on is a connect attempt, so these two answers are the
 * whole design of the watcher, and they are kept here as a pure rule because the
 * interesting part is the argument, not the callback.
 *
 * The scoping is what the garage changes. Since a card is bound to a physical unit, an
 * unattended hunt can only ever mean "reach the unit on the selected card again" —
 * there is no such thing as an unattended search for *some* dashboard, which is what
 * the old boot ladder did, and why a tablet that joined the house Wi-Fi could end up
 * connected to a dashboard in someone else's car.
 */
object ReconnectPolicy {

    /**
     * Should a network coming up reach again for the selected vehicle?
     *
     * Only from a state the app has already given up on ([Idle], [Failed]) — those are
     * the two the covers ask a finger for. Only when that network is a local LAN: a VPN
     * interface is a network event with no subnet that could hold the dashboard. And
     * only with a vehicle selected: with the garage empty there is nothing to reach, and
     * the watcher has to stay quiet rather than fall back to scanning.
     */
    fun shouldHunt(
        state: ConnectionUiState,
        hasLocalLan: Boolean,
        hasSelection: Boolean,
    ): Boolean = when (state) {
        is ConnectionUiState.Idle, is ConnectionUiState.Failed -> hasLocalLan && hasSelection
        is ConnectionUiState.Connecting, is ConnectionUiState.Connected -> false
    }

    /**
     * Should a network going away re-check the unit we are "connected" to?
     *
     * Only a [Connected] session can be invalidated by losing a network, and it is
     * worth one real request to find out: otherwise the app sits on a dead address until
     * a Web UI load fails, which on the shell means until the user opens the ESP slot.
     */
    fun shouldRecheck(state: ConnectionUiState): Boolean =
        state is ConnectionUiState.Connected
}
