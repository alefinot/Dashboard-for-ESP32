package com.alefinot.dashboardpp.ui.screens

import com.alefinot.dashboardpp.trips.TripsEnabled
import com.alefinot.dashboardpp.viewmodel.ConnectMode
import com.alefinot.dashboardpp.viewmodel.ConnectionCore
import com.alefinot.dashboardpp.viewmodel.ConnectionUiState
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

/**
 * The whole "no ESP connected" design turns on one decision that used to be drawn
 * in `DashboardRoot`'s `when`: which of the three things the root puts up for a
 * given connection state, and with what.
 *
 * It is a pure function now, and after the vehicle page became the home there is only
 * one thing it can put up: the shell. So what is left to test is what the shell says —
 * that the states which used to be dead-end screens hand over their reason as a line
 * instead of a screen, that a running ladder names itself in that line, and that the
 * recorder and the bar follow the same rule the screens do.
 */
class ShellRoutingTest {

    @Test
    fun `cold start is the vehicle page with nothing connected`() {
        // The garage has to be the first frame the app draws: no splash, no scan ring,
        // and no reason line yet, because nothing has been tried.
        val view = ShellRouting.view(ConnectionUiState.Idle(null))
        assertNull(view.conn)
        assertNull(view.reason)
        assertTrue(view.findIsRetryNetwork)
    }

    @Test
    fun `every state renders the same shell, and none of them gets a screen of its own`() {
        // There used to be a screen per state: a splash, a full-page scan ring, and
        // three dead ends. Each one took the trips and the settings away in order to
        // say one sentence. Now a state is a line on the page that was already there.
        for (state in listOf(
            ConnectionUiState.Idle(null),
            ConnectionUiState.Connecting(ConnectMode.SILENT),
            ConnectionUiState.Connecting(ConnectMode.SEARCH),
            ConnectionUiState.Connecting(ConnectMode.PROBE),
            ConnectionUiState.Failed("No Dashboard++ answered."),
        )) {
            assertNull("$state", ShellRouting.view(state).conn)
        }
    }

    @Test
    fun `a ladder that is running says which ladder is running`() {
        // With the scan ring gone, the line under the cover is the only place a wait can
        // be explained. A sweep of the whole network and a retry of two remembered
        // addresses are not the same wait, and the app must not say "searching" for both.
        val lines = ConnectMode.values().map {
            ShellRouting.view(ConnectionUiState.Connecting(it)).reason
        }
        for (line in lines) assertTrue(line.orEmpty().isNotBlank())
        assertEquals(lines.size, lines.distinct().size)
    }

    @Test
    fun `a connected vehicle is the shell, with a connection and no reason`() {
        val state = ConnectionUiState.Connected(
            "192.168.1.116", "1.4.6", "A4:CF:12:3B:9E:07", "v1",
        )
        val view = ShellRouting.view(state)
        assertEquals(state, view.conn)
        assertNull(view.reason)
        assertFalse(view.findIsRetryNetwork)
    }

    @Test
    fun `a ladder that ran out is the shell, without a connection, and it hands over the reason`() {
        // This is the state that used to be a dead-end screen: the user could only
        // re-scan or type an address, and the trips they had already recorded were
        // unreachable. Now it is the shell with nothing to show for Live and ESP.
        val view = ShellRouting.view(ConnectionUiState.Failed("No Dashboard++ answered."))
        assertNull(view.conn)
        assertTrue(view.reason.orEmpty().isNotBlank())
        // The ladder just ran on a network that exists, so FIND DEVICE re-runs it
        // rather than asking whether there is a network at all.
        assertFalse(view.findIsRetryNetwork)
    }

    @Test
    fun `a connection that drops mid-session keeps the shell and hands over the reason`() {
        // The reason line is what the Live cover says; it comes from the view model
        // and must not be rewritten on the way to the screen.
        val reason = "Lost connection to 192.168.1.116."
        val view = ShellRouting.view(ConnectionUiState.Failed(reason))
        assertNull(view.conn)
        assertEquals(reason, view.reason)
    }

    @Test
    fun `nothing being reached for is the shell too, and its FIND DEVICE retries the network`() {
        val detail = ConnectionCore.NO_VEHICLE_SELECTED
        val view = ShellRouting.view(ConnectionUiState.Idle(detail))
        assertEquals(detail, view.reason)
        // With no vehicle to reach, the only thing worth retrying is whether there is a
        // network and a card to reach at all.
        assertTrue(view.findIsRetryNetwork)
    }

    @Test
    fun `the recorder only runs with a device to record from`() {
        assertTrue(ShellRules.recorderWants("192.168.1.116", tripsAvailable = true, recordingEnabled = true))
        // The offline shell must never start the service, whatever the switches say.
        assertFalse(ShellRules.recorderWants(null, tripsAvailable = true, recordingEnabled = true))
        assertFalse(ShellRules.recorderWants(null, tripsAvailable = false, recordingEnabled = false))
        // And the two switches still mean what they meant.
        assertFalse(ShellRules.recorderWants("192.168.1.116", tripsAvailable = false, recordingEnabled = true))
        assertFalse(ShellRules.recorderWants("192.168.1.116", tripsAvailable = true, recordingEnabled = false))
    }

    @Test
    fun `the ESP slot dims when there is no device, and nothing else does`() {
        // Dimmed, not gone: the bar keeps five slots in the same order whatever
        // happens, so the swipe never passes through a hole and the cover inside the
        // page is reachable.
        assertTrue(ShellRules.glyphDimmed(AppTab.WEB, connected = false, tripsAvailable = true))
        assertFalse(ShellRules.glyphDimmed(AppTab.WEB, connected = true, tripsAvailable = true))
        // The trips pin already dims for firmware with no trip store - unchanged.
        assertTrue(ShellRules.glyphDimmed(AppTab.TRIPS, connected = true, tripsAvailable = false))
        assertFalse(ShellRules.glyphDimmed(AppTab.TRIPS, connected = false, tripsAvailable = true))
        assertFalse(ShellRules.glyphDimmed(AppTab.LIVE, connected = false, tripsAvailable = true))
        assertFalse(ShellRules.glyphDimmed(AppTab.SETTINGS, connected = false, tripsAvailable = true))
    }

    @Test
    fun `every page is reachable offline`() {
        // Trips and App Settings work in full; Live and ESP answer with a cover. None
        // of them leaves the swipe order, and none of them is switched off.
        for (tab in listOf(AppTab.TRIPS, AppTab.LIVE, AppTab.WEB, AppTab.SETTINGS)) {
            assertTrue(ShellRules.pageReachable(tab, connected = false, tripsAvailable = true))
            assertTrue(ShellRules.pageReachable(tab, connected = true, tripsAvailable = true))
        }
        // Firmware with no trip store still blocks the trips page, connected or not -
        // that rule predates the offline shell and does not change.
        assertFalse(ShellRules.pageReachable(AppTab.TRIPS, connected = true, tripsAvailable = false))
    }

    @Test
    fun `the shell only moves someone off a page the firmware cannot serve`() {
        // The trips page is the only one the shell ever leaves by itself, and it does
        // that for one reason: the firmware has no trip store. Without a device the
        // trip files belong to the phone, so a connection dropping while the user is
        // reading a trip must leave them on the page, not throw them at a cover.
        assertFalse(ShellRules.pageUnserved(tripsAvailable = true, connected = true))
        assertFalse(ShellRules.pageUnserved(tripsAvailable = true, connected = false))
        assertFalse(ShellRules.pageUnserved(tripsAvailable = false, connected = false))
        assertTrue(ShellRules.pageUnserved(tripsAvailable = false, connected = true))
    }

    @Test
    fun `the ESP page is warmed only while it is still unopened`() {
        // Warming is Chromium work spent on a page nobody is looking at, so it is
        // allowed exactly once per connection, only with a device to fetch it from,
        // and not at all if the user reached the slot on their own first.
        assertTrue(ShellRules.shouldWarm(connected = true, seen = false, warmed = false))
        assertFalse(ShellRules.shouldWarm(connected = false, seen = false, warmed = false))
        assertFalse(ShellRules.shouldWarm(connected = true, seen = true, warmed = false))
        assertFalse(ShellRules.shouldWarm(connected = true, seen = false, warmed = true))
    }

    @Test
    fun `local history does not need a device`() {
        // The fact the offline shell stands on: with nothing connected there is no
        // firmware version to check, and the trip files are the phone's own.
        assertTrue(TripsEnabled.forFirmware(null))
    }
}
