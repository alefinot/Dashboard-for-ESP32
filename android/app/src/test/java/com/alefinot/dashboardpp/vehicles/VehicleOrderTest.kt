package com.alefinot.dashboardpp.vehicles

import java.util.Random
import org.junit.Assert.assertEquals
import org.junit.Test

/**
 * Garage order and card ids (vehicle page plan §3) - the pure half of
 * [VehicleStore], testable without a Context or a DataStore.
 *
 * Both decisions outlive this task: the order is what a driver sees at a glance
 * through a windshield, and an id is what the trips of task 8 will be tagged with,
 * so a wrong answer here is history rewritten or a card the user cannot find. The
 * impure half (DataStore rows, one write per change) has nothing to argue about, so
 * it is not tested - it is written once, in [VehicleStore], in terms of these.
 */
class VehicleOrderTest {

    private fun v(
        id: String,
        lastConnectedAtMs: Long? = null,
        createdAtMs: Long = 0L,
    ) = Vehicle(id = id, name = id, lastConnectedAtMs = lastConnectedAtMs, createdAtMs = createdAtMs)

    private val old = v("old", lastConnectedAtMs = 1_600_000_000_000L, createdAtMs = 100L)
    private val recent = v("recent", lastConnectedAtMs = 1_700_000_000_000L, createdAtMs = 200L)
    private val never1 = v("never1", createdAtMs = 300L)
    private val never2 = v("never2", createdAtMs = 400L)

    @Test
    fun `the selected card comes first whatever it did`() {
        // The garage opens where the user last stood, even for a card that has
        // never connected and one that connected longest ago: it is the card they
        // pressed, and having it jump below the pack on every reconnect is how a
        // list of four becomes a hunt.
        val garage = listOf(old, never1, recent, never2)

        assertEquals(
            listOf("never2", "recent", "old", "never1"),
            VehicleStore.sorted(garage, "never2").map { it.id },
        )
    }

    @Test
    fun `a card that has connected outranks one that never has`() {
        // A never-connected card is a draft: a name typed five seconds ago, with no
        // unit proven to belong to it yet. It belongs at the bottom, not in the
        // middle of the cards that are actually dashboards.
        assertEquals(
            listOf("connected", "draft"),
            VehicleStore.sorted(
                listOf(v("draft", createdAtMs = 10L), v("connected", 1L, 999L)),
                null,
            ).map { it.id },
        )
    }

    @Test
    fun `among connected cards the most recent connect wins`() {
        // The unit most likely to be in front of the user right now. Reversed, the
        // garage would lead with the vehicle they have not driven in a year.
        val garage = listOf(v("van", 1_650_000_000_000L), v("car", 1_700_000_000_000L), v("bike", 1_600_000_000_000L))

        assertEquals(
            listOf("car", "van", "bike"),
            VehicleStore.sorted(garage, null).map { it.id },
        )
    }

    @Test
    fun `connected at the same moment sorts by the older card first`() {
        // A tie here is not rare: one connect can stamp several cards when the
        // reconnect probes a range of addresses. Without a second key the order
        // depends on the row order of the preferences map, so the garage would
        // reshuffle between two openings of the same app run.
        val garage = listOf(v("newer", 1_700_000_000_000L, createdAtMs = 2_000L), v("older", 1_700_000_000_000L, createdAtMs = 1_000L))

        assertEquals(
            listOf("older", "newer"),
            VehicleStore.sorted(garage.shuffled(Random(7)), null).map { it.id },
        )
    }

    @Test
    fun `never-connected cards sort by the older card first`() {
        // Same rule, same reason - and for drafts it is the only order there is.
        val garage = listOf(v("second", createdAtMs = 2_000L), v("first", createdAtMs = 1_000L))

        assertEquals(
            listOf("first", "second"),
            VehicleStore.sorted(garage, null).map { it.id },
        )
    }

    @Test
    fun `an empty garage sorts to an empty garage`() {
        assertEquals(emptyList<String>(), VehicleStore.sorted(emptyList(), "missing").map { it.id })
    }

    @Test
    fun `a pointer at a card that is gone reorders nothing`() {
        // The card was deleted out from under the pointer (task 3b's delete clears
        // it, but a row can also disappear through a corrupt file or an older app).
        // Nothing may rank first, and the rest must keep the order they had.
        val garage = listOf(old, never1, recent, never2)

        assertEquals(
            VehicleStore.sorted(garage, null).map { it.id },
            VehicleStore.sorted(garage, "deleted-card").map { it.id },
        )
        assertEquals(
            listOf("recent", "old", "never1", "never2"),
            VehicleStore.sorted(garage, null).map { it.id },
        )
    }

    @Test
    fun `sorting does not reorder the caller list`() {
        val garage = mutableListOf(recent, old)
        VehicleStore.sorted(garage, "old")
        assertEquals(listOf("recent", "old"), garage.map { it.id })
    }

    @Test
    fun `ids come from the counter and start at v1`() {
        // A fresh store has no counter, and the first card a user makes must be
        // "v1", not "v0" or a name built from the clock.
        assertEquals("v1", VehicleStore.nextIdName(0, emptySet()))
        assertEquals("v2", VehicleStore.nextIdName(1, setOf("v1")))
    }

    @Test
    fun `an id that is already taken is skipped`() {
        // A record restored by a future import, or a row written by hand, already
        // owns that name. Handing it out again would make the next card the user
        // creates overwrite a vehicle they did not delete.
        assertEquals("v4", VehicleStore.nextIdName(1, setOf("v2", "v3")))
        assertEquals("v3", VehicleStore.nextIdName(1, setOf("v2")))
        assertEquals("v2", VehicleStore.nextIdName(1, setOf("v1")))

        // The scan is a step forward, not a search for a gap: with v1..v4 taken and
        // the counter at 4, the answer is v5 even though v2's row may have vanished.
        assertEquals("v5", VehicleStore.nextIdName(4, setOf("v1", "v3", "v4")))
    }

    @Test
    fun `a freed id is not handed out again`() {
        // Deleting v3 frees its row, not its name. Card ids are the trip tags of
        // task 8, so a new vehicle called v3 would inherit the drives of the vehicle
        // that was deleted - the counter only ever moving forward is what prevents
        // it, and the taken set is only a second net over imported rows.
        assertEquals("v4", VehicleStore.nextIdName(3, setOf("v1", "v2")))
    }

    @Test
    fun `a missing or damaged counter restarts at v1 without colliding`() {
        // An empty store, and a counter written as a negative or zero by an older
        // build or a hand edit: never an id like "v0" or "v-9", which the reader of
        // a row would have no way to recognise.
        assertEquals("v1", VehicleStore.nextIdName(-5, emptySet()))
        assertEquals("v1", VehicleStore.nextIdName(-5, setOf("v-9")))
        assertEquals("v2", VehicleStore.nextIdName(-5, setOf("v1")))
    }
}
