package com.alefinot.dashboardpp.viewmodel

import com.alefinot.dashboardpp.vehicles.Vehicle
import com.alefinot.dashboardpp.vehicles.VehicleType
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

/**
 * The connection ladder, decided one step at a time, with no network and no clock.
 *
 * The app used to hunt for *whatever Dashboard++ is out there* and take the first one
 * that answered. That is the design the garage replaces: since a card is bound to a
 * physical unit by its eFuse MAC, "connect" now means "reach *this* vehicle's
 * dashboard", and the answer to "which card is the unit that answered?" has to be
 * settled before anything is written down. Those two questions are the whole of this
 * file, and they are pure functions because getting them wrong renames someone's car:
 * it tags a drive to the wrong vehicle, marks CONNECTED on a card nobody pressed, and
 * binds two different dashboards into one record.
 *
 * [ConnectionCore.decide] answers one step. The view model is the loop, so every test
 * here says what the *next* step is, given what has already been tried and what the
 * last probe answered — including the two cases the old code could not express: a card
 * with no unit bound (rule 3), and a unit that answers without an identity at all
 * (firmware predating the `mac` field).
 */
class ConnectionCoreTest {

    private val now = 1_700_000_000_000L

    /** The line the shell shows when there is nothing to reach. */
    private val noSelection = ConnectionCore.NO_VEHICLE_SELECTED

    private fun card(
        id: String,
        mac: String? = null,
        ips: List<String> = emptyList(),
        name: String = id.uppercase(),
    ) = Vehicle(
        id = id,
        mac = mac,
        name = name,
        type = VehicleType.CAR,
        ips = ips,
        createdAtMs = now,
    )

    private fun decide(
        cards: List<Vehicle>,
        selectedId: String?,
        tried: List<String> = emptyList(),
        scanDone: Boolean = false,
        outcome: ProbeOutcome? = null,
        mode: ConnectMode = ConnectMode.SEARCH,
    ) = ConnectionCore.decide(cards, selectedId, tried, scanDone, outcome, mode, now)

    private fun ok(ip: String, mac: String?, version: String? = "1.4.6") =
        ProbeOutcome.Ok(ip, version, mac)

    // ---------------------------------------------------------------- selection

    @Test
    fun `with no vehicle selected nothing is dialed`() {
        // This is the state the garage opens in on a fresh install: there is no card
        // to reach, so an unattended attempt must sit still rather than fall back to
        // hunting the network for anything that answers — which is the exact behaviour
        // the refactor exists to remove.
        for (mode in listOf(ConnectMode.SILENT, ConnectMode.PROBE)) {
            val r = decide(listOf(card("a", ips = listOf("192.168.1.11"))), null, mode = mode)
            assertTrue(r.next is NextAction.Idle)
            assertEquals(noSelection, (r.next as NextAction.Idle).reason)
            assertNull(r.persist)
            assertNull(r.adoption)
        }
    }

    @Test
    fun `a search with no card selected still sweeps`() {
        // SEARCH FOR DASHBOARD on the ADD VEHICLE card: there is no card yet, so the
        // sweep is the only door, and what it finds comes back as an offer to name.
        val r = decide(emptyList(), null, mode = ConnectMode.SEARCH)
        assertEquals(NextAction.ScanThenProbe(null), r.next)
    }

    @Test
    fun `a selection that no longer exists is idle, not a crash`() {
        // A card deleted from under an in-flight attempt. The view model re-reads the
        // list every step, so the core has to answer this rather than assume its
        // caller kept the card alive.
        val r = decide(emptyList(), "gone")
        assertTrue(r.next is NextAction.Idle)
    }

    @Test
    fun `a card that has never connected is idle on the unattended paths`() {
        // Distinct from "no selection": here the user pressed CONNECT on a card they
        // made in the editor, and the honest answer at launch is that the app has no
        // address for it yet — not "no vehicle selected".
        for (mode in listOf(ConnectMode.SILENT, ConnectMode.PROBE)) {
            val r = decide(listOf(card("a")), "a", mode = mode)
            assertTrue("$mode", r.next is NextAction.Idle)
            assertNotEqualsLine(r, noSelection)
        }
    }

    private fun assertNotEqualsLine(r: CoreResult, line: String) {
        val reason = (r.next as NextAction.Idle).reason
        assertNotNull(reason)
        assertTrue(reason != line)
    }

    // ------------------------------------------------------- the probe ladder

    @Test
    fun `remembered addresses are probed newest first`() {
        val cards = listOf(card("a", ips = listOf("192.168.1.50", "192.168.1.11", "10.0.0.5")))
        val first = decide(cards, "a")
        assertEquals(NextAction.Probe("a", "192.168.1.50"), first.next)

        val second = decide(cards, "a", tried = listOf("192.168.1.50"),
            outcome = ProbeOutcome.Fail("192.168.1.50"))
        assertEquals(NextAction.Probe("a", "192.168.1.11"), second.next)

        val third = decide(cards, "a", tried = listOf("192.168.1.50", "192.168.1.11"),
            outcome = ProbeOutcome.Fail("192.168.1.11"))
        assertEquals(NextAction.Probe("a", "10.0.0.5"), third.next)
    }

    @Test
    fun `a probe never redials an address that already failed`() {
        // Each connect timeout is two seconds; re-trying the same dead address inside
        // one attempt would only delay the address that might work.
        val cards = listOf(card("a", ips = listOf("192.168.1.50", "192.168.1.11")))
        val r = decide(cards, "a", tried = listOf("192.168.1.50"),
            outcome = ProbeOutcome.Fail("192.168.1.50"))
        assertEquals("192.168.1.11", (r.next as NextAction.Probe).address)
    }

    @Test
    fun `silent mode stops at the remembered addresses`() {
        // Launch reconnect must not put a 15 s subnet sweep in front of the first
        // frame: the garage is usable while it runs, and a device that is off is not
        // going to be found by scanning the whole LAN for it.
        val cards = listOf(card("a", name = "Workshop Van", ips = listOf("192.168.1.50")))
        val r = decide(cards, "a", tried = listOf("192.168.1.50"),
            outcome = ProbeOutcome.Fail("192.168.1.50"), mode = ConnectMode.SILENT)
        val failed = r.next as NextAction.Failed
        assertTrue(failed.reason.contains("Workshop Van"))
        assertTrue(failed.reason.isNotBlank())
    }

    @Test
    fun `an exhausted silent ladder still says which vehicle it failed to reach`() {
        // The line has to name the vehicle, because on the vehicle page it sits under
        // that card: "no Dashboard++ answered" on its own is a sentence about a network
        // the user is not asking about.
        val cards = listOf(card("a", name = "Workshop Van", ips = listOf("192.168.1.50")))
        val r = decide(cards, "a", tried = listOf("192.168.1.50"),
            outcome = ProbeOutcome.Fail("192.168.1.50"), mode = ConnectMode.SILENT)
        assertTrue((r.next as NextAction.Failed).reason.contains("Workshop Van"))
    }

    @Test
    fun `search mode sweeps the network once the addresses are spent`() {
        val cards = listOf(card("a", ips = listOf("192.168.1.50")))
        val r = decide(cards, "a", tried = listOf("192.168.1.50"),
            outcome = ProbeOutcome.Fail("192.168.1.50"), mode = ConnectMode.SEARCH)
        assertEquals(NextAction.ScanThenProbe("a"), r.next)
    }

    @Test
    fun `a search that already swept gives up instead of sweeping again`() {
        // Without this the view model would re-run mDNS and the subnet scanner every
        // time the sweep came back empty, which is a loop the user watches.
        val cards = listOf(card("a", ips = listOf("192.168.1.50")))
        val r = decide(cards, "a", tried = listOf("192.168.1.50"), scanDone = true,
            outcome = ProbeOutcome.Fail("192.168.1.9"), mode = ConnectMode.SEARCH)
        assertTrue(r.next is NextAction.Failed)
    }

    @Test
    fun `probe mode never sweeps the network`() {
        // RECONNECT on a card means "the addresses I remember, again" — the user is
        // asking for a retry, not for a hunt that may land on a different unit.
        val cards = listOf(card("a", ips = listOf("192.168.1.50")))
        val r = decide(cards, "a", tried = listOf("192.168.1.50"),
            outcome = ProbeOutcome.Fail("192.168.1.50"), mode = ConnectMode.PROBE)
        assertTrue(r.next is NextAction.Failed)
    }

    @Test
    fun `an exhausted ladder is a failure, not an idle wait`() {
        // The difference matters on screen: Idle means "there is nothing to reach",
        // which the vehicle page answers by inviting the user in; Failed means "it was
        // reached for and nobody answered", which is what earns a RECONNECT button.
        val cards = listOf(card("a", ips = listOf("192.168.1.50", "10.0.0.5")))
        for (mode in listOf(ConnectMode.SILENT, ConnectMode.PROBE)) {
            val r = decide(cards, "a", tried = listOf("192.168.1.50", "10.0.0.5"),
                outcome = ProbeOutcome.Fail("10.0.0.5"), mode = mode)
            assertTrue("$mode", r.next is NextAction.Failed)
        }
    }

    // ---------------------------------------------------------------- identity

    @Test
    fun `the unit that answers is bound to the card that was probed`() {
        // Rule 3: a card made in the editor has a name but no identity. The first
        // connect that reaches it adopts the MAC the unit reports, name and photo
        // intact.
        val cards = listOf(card("a", name = "Workshop Van", ips = listOf("192.168.1.50")))
        val r = decide(cards, "a", outcome = ok("192.168.1.50", "AA:BB:CC:DD:EE:FF"))
        assertEquals(NextAction.Connect("a", "192.168.1.50", "1.4.6", "AA:BB:CC:DD:EE:FF"), r.next)
        assertNotNull(r.persist)
        assertEquals("AA:BB:CC:DD:EE:FF", r.persist!!.mac)
        assertEquals("Workshop Van", r.persist!!.name)
        assertEquals(now, r.persist!!.lastConnectedAtMs)
    }

    @Test
    fun `a reported MAC is stored in one canonical form`() {
        // A unit spelling its identity in lower case, or a value read from a backup
        // file, has to end up in the record the way the ESP prints it, or the next
        // compare misses and the unit looks like a stranger.
        val cards = listOf(card("a", ips = listOf("192.168.1.50")))
        val r = decide(cards, "a", outcome = ok("192.168.1.50", "aa:bb:cc:dd:ee:ff"))
        assertEquals("AA:BB:CC:DD:EE:FF", r.persist!!.mac)
    }

    @Test
    fun `a connect moves the answering address to the front of the list`() {
        // The launch ladder probes in list order, so the address that just worked has
        // to be first or every cold start pays a timeout for a stale lease.
        val cards = listOf(card("a", mac = "AA:BB:CC:DD:EE:FF", ips = listOf("10.0.0.5", "192.168.1.50")))
        val r = decide(cards, "a", tried = listOf("10.0.0.5"),
            outcome = ok("192.168.1.50", "AA:BB:CC:DD:EE:FF"))
        val persisted = r.persist!!
        assertEquals(NextAction.Connect("a", "192.168.1.50", "1.4.6", "AA:BB:CC:DD:EE:FF"), r.next)
        assertEquals(listOf("192.168.1.50", "10.0.0.5"), persisted.ips)
        assertEquals("1.4.6", persisted.firmwareVersion)
    }

    @Test
    fun `a unit reporting no MAC keeps the identity its card already earned`() {
        // Firmware predating the mac field, or a body the parser could not read. The
        // card was bound by MAC earlier; a connect that proves nothing about identity
        // must not un-bind it.
        val cards = listOf(card("a", mac = "AA:BB:CC:DD:EE:FF", ips = listOf("192.168.1.50")))
        val r = decide(cards, "a", outcome = ok("192.168.1.50", null))
        assertEquals(NextAction.Connect("a", "192.168.1.50", "1.4.6", null), r.next)
        assertEquals("AA:BB:CC:DD:EE:FF", r.persist!!.mac)
    }

    @Test
    fun `a bound card reconnects to its own address when the unit reports nothing`() {
        // The case the MAC rule could otherwise break: a card already bound to a unit
        // that is still running old firmware answers at its own remembered address
        // with no identity at all. Address alone is enough to connect when exactly one
        // card remembers that address — refusing here would leave a card that can
        // never reconnect after a firmware rollback.
        val cards = listOf(card("a", mac = "AA:BB:CC:DD:EE:FF", ips = listOf("192.168.1.50")))
        val r = decide(cards, "a", outcome = ok("192.168.1.50", null))
        assertTrue(r.next is NextAction.Connect)
        assertEquals("a", (r.next as NextAction.Connect).vehicleId)
        assertNull(r.adoption)
    }

    @Test
    fun `when the answering unit belongs to another card that card becomes the connection`() {
        // The MAC outranks the address, so the unit at the selected card's remembered
        // address belongs to another vehicle. The selection moves to the card that own
        // the unit (plan §4) rather than marking CONNECTED on the card the user
        // pressed — and the move has to be said out loud, because the page now shows a
        // different vehicle than the one that was tapped.
        val cards = listOf(
            card("other", mac = "AA:BB:CC:DD:EE:FF", ips = listOf("192.168.1.11")),
            card("a", ips = listOf("192.168.1.11")),
        )
        val r = decide(cards, "a", outcome = ok("192.168.1.11", "AA:BB:CC:DD:EE:FF"))
        assertEquals(NextAction.Connect("other", "192.168.1.11", "1.4.6", "AA:BB:CC:DD:EE:FF"), r.next)
        assertEquals("other", r.persist!!.id)
        assertEquals(listOf("192.168.1.11"), r.persist!!.ips)
        assertNull(r.adoption)
        assertNotNull(r.notice)
        assertTrue(r.notice!!.contains("OTHER"))
        assertTrue(r.notice!!.contains("A"))
    }

    @Test
    fun `an address two cards remember is not connected on address evidence alone`() {
        // With no identity in the answer and two cards holding the same address, the
        // app cannot say which vehicle this is — and a wrong guess writes that
        // vehicle's name into the trip log.
        val cards = listOf(
            card("a", ips = listOf("192.168.1.11")),
            card("b", ips = listOf("192.168.1.11")),
        )
        val r = decide(cards, "a", outcome = ok("192.168.1.11", null))
        assertTrue(r.next is NextAction.Failed)
        assertNull(r.persist)
    }

    @Test
    fun `a unit nobody owns is offered for adoption, not adopted`() {
        // An unclaimed MAC must never be written into the card the user happened to
        // have selected — that is how two cards end up claiming one dashboard. The
        // offer is data for the page to show, and the user decides.
        val cards = listOf(card("a", mac = "AA:BB:CC:DD:EE:FF", ips = listOf("192.168.1.11")))
        val r = decide(cards, "a", outcome = ok("192.168.1.99", "11:22:33:44:55:66"))
        assertTrue(r.next is NextAction.Failed)
        assertNull(r.persist)
        assertEquals(
            UnclaimedUnit("192.168.1.99", "1.4.6", "11:22:33:44:55:66"),
            r.adoption,
        )
    }

    @Test
    fun `an unclaimed unit found at launch leaves the app idle rather than failed`() {
        // The silent ladder runs unattended at startup. Finding an unlinked dashboard
        // there is information for the garage to show, not an error to apologise for.
        val cards = listOf(card("a", mac = "AA:BB:CC:DD:EE:FF", ips = listOf("192.168.1.11")))
        val r = decide(cards, "a", mode = ConnectMode.SILENT,
            outcome = ok("192.168.1.11", "11:22:33:44:55:66"))
        assertTrue(r.next is NextAction.Idle)
        assertEquals(
            UnclaimedUnit("192.168.1.11", "1.4.6", "11:22:33:44:55:66"),
            r.adoption,
        )
    }

    @Test
    fun `a unit that reports no identity at all is not offered for adoption`() {
        // There is nothing to adopt without a MAC: no identity to carry into a card.
        // Offering one would let the user bind a card to an address that may belong to
        // the phone's own hotspot.
        val cards = listOf(card("a", mac = "AA:BB:CC:DD:EE:FF", ips = listOf("192.168.1.11")))
        val r = decide(cards, "a", outcome = ok("192.168.1.99", null))
        assertNull(r.adoption)
        assertTrue(r.next is NextAction.Failed)
    }

    @Test
    fun `the unclaimed offer is cleared when nothing is unclaimed`() {
        // The view model publishes whatever the core hands it, so a step that finds
        // nothing to offer has to say so explicitly rather than leave the previous
        // offer on screen.
        val cards = listOf(card("a", ips = listOf("192.168.1.50")))
        val r = decide(cards, "a", outcome = ok("192.168.1.50", "AA:BB:CC:DD:EE:FF"))
        assertNull(r.adoption)
    }

    @Test
    fun `the next probe always belongs to the card that is selected`() {
        // The view model aborts an attempt when the selection moves, and it can only
        // notice that a step is stale if every step says who it is for.
        val cards = listOf(card("a", ips = listOf("192.168.1.50")), card("b", ips = listOf("10.0.0.2")))
        assertEquals("a", (decide(cards, "a").next as NextAction.Probe).vehicleId)
        assertEquals("b", (decide(cards, "b").next as NextAction.Probe).vehicleId)
        assertEquals("10.0.0.2", (decide(cards, "b").next as NextAction.Probe).address)
    }

    @Test
    fun `a card that has never connected is swept for only when the user asked`() {
        // A card made in the editor has no address, so the sweep is the only way it
        // can ever get one — but a sweep is a 15 s scan that may land on a unit the
        // user never touched, so it runs from the pressed CONNECT and never from
        // launch or the network watcher.
        val cards = listOf(card("a"))
        val search = decide(cards, "a", mode = ConnectMode.SEARCH)
        assertEquals(NextAction.ScanThenProbe("a"), search.next)
        // A second sweep is a loop the user watches.
        val spent = decide(cards, "a", scanDone = true, mode = ConnectMode.SEARCH)
        assertTrue(spent.next is NextAction.Failed)
        assertTrue((spent.next as NextAction.Failed).reason.isNotBlank())
    }

    @Test
    fun `a sweep that finds an unclaimed unit hands over the identity to name`() {
        // The NAME IT offer needs the MAC and the firmware version, or the line the
        // page shows is empty and the editor starts blank.
        val cards = listOf(card("a"))
        val r = decide(cards, "a", scanDone = true, mode = ConnectMode.SEARCH,
            outcome = ok("192.168.1.99", "11:22:33:44:55:66"))
        assertEquals(
            UnclaimedUnit("192.168.1.99", "1.4.6", "11:22:33:44:55:66"),
            r.adoption,
        )
        assertTrue(r.next is NextAction.Failed)
        assertNull(r.persist)
    }
}
