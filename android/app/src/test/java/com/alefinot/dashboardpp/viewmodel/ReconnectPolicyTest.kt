package com.alefinot.dashboardpp.viewmodel

import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

/**
 * When a network event is worth reacting to.
 *
 * Until now the app needed a finger for every one of them: a tablet that joins the
 * house Wi-Fi, or walks out of the car's hotspot, sat on RETRY NETWORK until it was
 * tapped. A `ConnectivityManager` callback makes that automatic, and a callback is not
 * a request - it arrives when the network says so, including for interfaces the user
 * never asked about (a VPN registering, Wi-Fi flapping). Each event acted on is a
 * 15 s discovery run, so the decision is the whole design, and it lives here where it
 * can be argued about instead of on a tablet.
 */
class ReconnectPolicyTest {

    @Test
    fun `a new network only restarts the hunt from a state the app gave up on`() {
        // Still booting, or already looking, or already found: a hunt is running or
        // there is nothing to look for.
        assertFalse(ReconnectPolicy.shouldHunt(ConnectionUiState.Booting, hasLocalLan = true))
        assertFalse(ReconnectPolicy.shouldHunt(ConnectionUiState.CheckingCache, hasLocalLan = true))
        assertFalse(ReconnectPolicy.shouldHunt(ConnectionUiState.CheckingSoftAp, hasLocalLan = true))
        assertFalse(ReconnectPolicy.shouldHunt(ConnectionUiState.Discovering, hasLocalLan = true))
        assertFalse(
            ReconnectPolicy.shouldHunt(
                ConnectionUiState.Connected("192.168.1.116", "1.3.3"), hasLocalLan = true,
            )
        )

        // The three states that end a hunt: yes, and this is the case that matters on
        // a tablet - Wi-Fi back on, hotspot started, dock Ethernet plugged in.
        assertTrue(ReconnectPolicy.shouldHunt(ConnectionUiState.NoWifi("none"), hasLocalLan = true))
        assertTrue(ReconnectPolicy.shouldHunt(ConnectionUiState.ManualEntryNeeded, hasLocalLan = true))
        assertTrue(
            ReconnectPolicy.shouldHunt(
                ConnectionUiState.ConnectionLost("lost"), hasLocalLan = true,
            )
        )

        // ...but only when the new network is a real local LAN. Tailscale registers an
        // interface on this tablet every few minutes; that is a network event with no
        // subnet to scan, and acting on it would replace the cover's message with one
        // about a network that cannot hold a Dashboard++.
        assertFalse(ReconnectPolicy.shouldHunt(ConnectionUiState.NoWifi("none"), hasLocalLan = false))
        assertFalse(ReconnectPolicy.shouldHunt(ConnectionUiState.ManualEntryNeeded, hasLocalLan = false))
        assertFalse(
            ReconnectPolicy.shouldHunt(
                ConnectionUiState.ConnectionLost("lost"), hasLocalLan = false,
            )
        )
    }

    @Test
    fun `only a connected session re-checks its device when the network goes away`() {
        // A dropped default network under a connected session is worth one verify: it
        // answers "is the device still reachable, or did we walk out of its range"
        // without waiting for a Web UI load to fail.
        assertTrue(
            ReconnectPolicy.shouldRecheck(ConnectionUiState.Connected("192.168.1.116", "1.3.3"))
        )
        // Everything else is already asking the user for something, or is on its way to
        // a device: re-verifying would either be pointless or fight the running hunt.
        assertFalse(ReconnectPolicy.shouldRecheck(ConnectionUiState.NoWifi("none")))
        assertFalse(ReconnectPolicy.shouldRecheck(ConnectionUiState.ManualEntryNeeded))
        assertFalse(ReconnectPolicy.shouldRecheck(ConnectionUiState.ConnectionLost("lost")))
        assertFalse(ReconnectPolicy.shouldRecheck(ConnectionUiState.Discovering))
        assertFalse(ReconnectPolicy.shouldRecheck(ConnectionUiState.Booting))
    }
}
