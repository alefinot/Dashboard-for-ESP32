package com.alefinot.dashboardpp.vehicles

import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

/**
 * The two rules the editor enforces, kept out of the screen so they can be tested at the
 * boundary instead of discovered by a user.
 *
 * Both exist because a garage card has to be *identifiable*. A card with no name cannot be
 * told apart in the trip log or on the bar, and a card that carries another card's MAC is
 * the exact bug this whole refactor was written to remove - two cards describing one
 * physical dashboard, and a trip tagged with the wrong vehicle. So the editor never stores
 * a blank name, and never stores a duplicate identity.
 */
class VehicleEditorRulesTest {

    // ── names ────────────────────────────────────────────────────────────────
    @Test
    fun `a name left blank still gives the card something to be called`() {
        // The app is allowed to name a car for the user, but only with something they can
        // recognise as a placeholder and change: the tail of the dashboard's own eFuse MAC
        // is the one string that identifies this unit and no other.
        val v = Vehicle(id = "v3", mac = "A4:CF:12:3B:9E:07")
        assertEquals("UNTITLED 3B9E07", displayName("   ", v, emptySet()))
        // If something already carries the same placeholder, the safe answer is the
        // numbered one, not the duplicate - and in practice this cannot happen, because
        // two cards cannot share the MAC the tail came from ([macConflict] refuses that).
        assertEquals("UNTITLED 3B9E07 2", displayName("", v, setOf("UNTITLED 3B9E07")))
    }

    @Test
    fun `a card with no MAC is named after its own id rather than invented nothing`() {
        // Old firmware does not report a MAC, and a card saved by hand may never get one.
        // The id is the only stable handle left, and it is also the tag the trip files use,
        // so the placeholder at least agrees with what the export says.
        assertEquals("UNTITLED V3", displayName("", Vehicle(id = "v3"), emptySet()))
    }

    @Test
    fun `what the user typed is what the card is called, minus the whitespace`() {
        assertEquals("Corolla", displayName("  Corolla ", Vehicle(id = "v1"), emptySet()))
        assertEquals("Toyota Corolla 1998", displayName("Toyota Corolla 1998", Vehicle(id = "v1"), emptySet()))
    }

    @Test
    fun `a name already used by another card is numbered rather than duplicated`() {
        // Two cards called "Corolla" cannot be told apart in the trips filter, on the bar
        // tooltip, or in the delete confirmation, and the confirmation is where a mistake
        // becomes permanent.
        val taken = setOf("Corolla", "Corolla 2")
        assertEquals("Corolla 3", displayName("Corolla", Vehicle(id = "v9"), taken))
    }

    @Test
    fun `a card renaming itself to its own name is not a collision with itself`() {
        val v = Vehicle(id = "v1", name = "Corolla")
        assertEquals("Corolla", displayName("Corolla", v, setOf("Corolla")))
    }

    // ── identity ─────────────────────────────────────────────────────────────
    @Test
    fun `a MAC that belongs to another card is refused, and the refusal names that card`() {
        val other = Vehicle(id = "v2", name = "Van", mac = "5E:AB:12:3B:9E:07")
        val editing = Vehicle(id = "v1", name = "Corolla", mac = "5E:AB:12:3B:9E:07")
        val clash = macConflict(editing, listOf(other, editing))
        assertEquals("v2", clash?.id)
        // "This identity is already on another card" is only useful if it says which one:
        // the user needs the other card to get rid of it.
        val why = refusalMessage(clash!!)
        assertTrue(why, why.contains("Van"))
        assertTrue(why, why.contains(other.mac!!))
    }

    @Test
    fun `no identity means nothing to clash`() {
        // A hand-entered card has no MAC at all (firmware that predates /api/perf's mac
        // field). Two of them are a nuisance, not a contradiction: neither claims to be a
        // physical unit, so there is no false statement to refuse.
        assertNull(macConflict(Vehicle(id = "v1", mac = null), listOf(Vehicle(id = "v2", mac = null))))
        assertNull(macConflict(Vehicle(id = "v1", mac = "A4:CF:00:00:00:01"), listOf(Vehicle(id = "v2", mac = "A4:CF:00:00:00:02"))))
    }

    @Test
    fun `the same card being saved again is not a conflict with itself`() {
        val v = Vehicle(id = "v1", name = "Corolla", mac = "A4:CF:00:00:00:01")
        assertNull(macConflict(v, listOf(v)))
        assertNull(macConflict(v, listOf(v, Vehicle(id = "v2", name = "Van"))))
    }

    @Test
    fun `MAC comparison ignores case and separators so a retyped identity still matches`() {
        val other = Vehicle(id = "v2", name = "Van", mac = "a4:cf:00:00:00:01")
        val editing = Vehicle(id = "v1", mac = "A4CF00000001")
        assertEquals("v2", macConflict(editing, listOf(other))?.id)
    }

    // ── what a save writes down ──────────────────────────────────────────────
    @Test
    fun `a save keeps the identity and evidence the app gathered and takes the user's words`() {
        // The editor edits the user's half of the record (name, plate, type, maker, model,
        // year, notes). The app's half - MAC, remembered addresses, when it last answered,
        // firmware version, creation time - is not the editor's to change, and a save that
        // dropped it would silently unbind the card from the unit it was adopted from.
        val stored = Vehicle(
            id = "v1", mac = "A4:CF:00:00:00:01", name = "Old", photoFile = "v1.jpg",
            ips = listOf("192.168.1.9"), lastConnectedAtMs = 123L, firmwareVersion = "1.4.6",
            createdAtMs = 45L,
        )
        val typed = stored.copy(
            name = " Corolla ", plate = " B 1234 CD ", maker = " Toyota ",
            type = VehicleType.MOTORCYCLE, notes = "  track days  ",
            // A form never carries the photo: the pipeline that wrote v1.jpg owns
            // that file name, and a save that lost it would leave a JPEG on disk
            // that nothing points at.
            photoFile = null,
        )
        val saved = mergeTypedFields(stored, typed)
        assertEquals("Corolla", saved.name)
        assertEquals("B 1234 CD", saved.plate)
        assertEquals(VehicleType.MOTORCYCLE, saved.type)
        assertEquals("track days", saved.notes)
        assertEquals("Toyota", saved.maker)
        assertEquals(stored.mac, saved.mac)
        assertEquals(stored.ips, saved.ips)
        assertEquals(stored.lastConnectedAtMs, saved.lastConnectedAtMs)
        assertEquals(stored.firmwareVersion, saved.firmwareVersion)
        assertEquals(stored.photoFile, saved.photoFile)
        assertEquals(stored.createdAtMs, saved.createdAtMs)
    }
}
