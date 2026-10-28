package com.alefinot.dashboardpp.viewmodel

import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

/**
 * When a network event entitles the app to reach for the selected vehicle.
 *
 * `ConnectivityManager` calls back when the network says so, not when the user does: a
 * VPN interface registering, a hotspot starting, Wi-Fi flapping behind a dock Ethernet
 * handover. Every event acted on is a connect attempt, so these two answers are the
 * whole design of the watcher, and they are kept here as a pure rule because the
 * interesting part is the argument, not the callback.
 *
 * The scoping is what the garage changes: since a card is bound to a physical unit, an
 * unattended hunt can only ever mean "reach the unit on the selected card again". There
 * is no such thing as an unattended search for *some* dashboard — that is what the
 * sweep on the vehicle page is for, and it is something the user asks for.
 */
class ReconnectPolicyTest {

    @Test
    fun `a new network only reaches again from a state the app gave up on`() {
        // Connecting, or already connected: there is an attempt running or nothing to
        // reach for.
        assertFalse(
            ReconnectPolicy.shouldHunt(
                ConnectionUiState.Connecting(ConnectMode.SILENT),
                hasLocalLan = true, hasSelection = true,
            )
        )
        assertFalse(
            ReconnectPolicy.shouldHunt(
                ConnectionUiState.Connected("192.168.1.116", "1.4.6", "A4:CF:12:3B:9E:07", "v1"),
                hasLocalLan = true, hasSelection = true,
            )
        )

        // Given up on — yes, and this is the case that matters on a tablet: Wi-Fi back
        // on, hotspot started, dock Ethernet plugged in.
        assertTrue(
            ReconnectPolicy.shouldHunt(
                ConnectionUiState.Failed("No answer"), hasLocalLan = true, hasSelection = true,
            )
        )
        assertTrue(
            ReconnectPolicy.shouldHunt(
                ConnectionUiState.Idle("Nothing remembered"),
                hasLocalLan = true, hasSelection = true,
            )
        )

        // …but only with a real local LAN. Tailscale registers an interface on this
        // tablet every few minutes; that is a network event with no subnet that could
        // hold the dashboard, and acting on it burns two connect timeouts each time.
        assertFalse(
            ReconnectPolicy.shouldHunt(
                ConnectionUiState.Failed("No answer"), hasLocalLan = false, hasSelection = true,
            )
        )

        // …and only with a vehicle selected. That is the difference from the old boot
        // ladder, which hunted for any device on the theory that one might be wanted.
        // With the garage empty, or with the selection cleared, a network event must
        // stay quiet.
        assertFalse(
            ReconnectPolicy.shouldHunt(
                ConnectionUiState.Failed("No answer"), hasLocalLan = true, hasSelection = false,
            )
        )
        assertFalse(
            ReconnectPolicy.shouldHunt(
                ConnectionUiState.Idle(null), hasLocalLan = true, hasSelection = false,
            )
        )
    }

    @Test
    fun `only a connected session re-checks its unit when the network goes away`() {
        // A dropped default network under a connected session is worth one real
        // request: otherwise the app sits on a dead address until a Web UI load fails,
        // which on the shell means until the user opens the ESP slot.
        assertTrue(
            ReconnectPolicy.shouldRecheck(
                ConnectionUiState.Connected("192.168.1.116", "1.4.6", "A4:CF:12:3B:9E:07", "v1"),
            )
        )
        // Everything else is either already asking the user for something or is on its
        // way to the unit: re-verifying would be pointless or would fight the attempt
        // that is already running.
        assertFalse(ReconnectPolicy.shouldRecheck(ConnectionUiState.Failed("No answer")))
        assertFalse(ReconnectPolicy.shouldRecheck(ConnectionUiState.Idle(null)))
        assertFalse(
            ReconnectPolicy.shouldRecheck(ConnectionUiState.Connecting(ConnectMode.SEARCH))
        )
    }
}
