package com.alefinot.dashboardpp.viewmodel

sealed interface ConnectionUiState {
    object Booting : ConnectionUiState
    object CheckingCache : ConnectionUiState
    object CheckingSoftAp : ConnectionUiState
    object Discovering : ConnectionUiState
    data class Connected(val ip: String, val version: String?) : ConnectionUiState
    object ManualEntryNeeded : ConnectionUiState
    data class ConnectionLost(val reason: String) : ConnectionUiState
    data class NoWifi(val detail: String) : ConnectionUiState
}

/**
 * What a network event entitles the app to do.
 *
 * `ConnectivityManager` calls back when the network says so, not when the user does:
 * a VPN interface registering, a hotspot starting, Wi-Fi flapping behind a dock
 * Ethernet handover. Every event acted on is a discovery run, so these two answers
 * are the whole design of the watcher, and they are kept here as a pure rule because
 * the interesting part is the argument, not the callback.
 */
object ReconnectPolicy {

    /**
     * Should a network coming up start the hunt again?
     *
     * Only from a state the app has already given up on - `NoWifi`,
     * `ManualEntryNeeded`, `ConnectionLost` - because those are the three the covers
     * ask a finger for. And only when that network is a local LAN: a VPN interface is
     * a network event with no subnet to scan, and scanning it would replace the
     * cover's message with one that is true of a different network.
     */
    fun shouldHunt(state: ConnectionUiState, hasLocalLan: Boolean): Boolean = when (state) {
        is ConnectionUiState.NoWifi,
        is ConnectionUiState.ManualEntryNeeded,
        is ConnectionUiState.ConnectionLost,
        -> hasLocalLan

        else -> false
    }

    /**
     * Should a network going away re-check the device we are "connected" to?
     *
     * Only a `Connected` session can be invalidated by losing a network, and it is
     * worth one real request to find out: otherwise the app sits on a dead IP until a
     * Web UI load fails, which on the shell means until the user opens the ESP slot.
     */
    fun shouldRecheck(state: ConnectionUiState): Boolean =
        state is ConnectionUiState.Connected
}
