package com.alefinot.dashboardpp.viewmodel

import com.alefinot.dashboardpp.trips.RecorderSettings
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test
import java.io.File

/**
 * Narrowing the Trips list to one vehicle (plan §6), tested as the two pure rules behind
 * the control rather than through the control itself.
 *
 * The rule that matters most is the boring one - *when the filter exists at all*. A chip
 * row that appears on a tablet with one vehicle and disappears on one with two is the
 * difference between a tool and decoration, and it is exactly the kind of thing that
 * drifts back on without a test holding it. The rest is that filtering works on the id the
 * file carries, never on a name: names are labels, and they change.
 */
class TripsFilterTest {

    private fun row(
        path: String,
        vehicleId: String?,
        name: String? = vehicleId,
        eff: String? = "AA:BB:CC:DD:EE:FF",
    ): TripRow = TripRow(
        file = File(path),
        startMs = 1_730_000_000_000L,
        sizeBytes = 1024L,
        summary = null,
        vehicleId = vehicleId,
        vehicleName = name,
        eff = eff,
        inGarage = vehicleId != null,
    )

    private val twoCars = listOf(
        row("/x/trip_20240101090000.csv", "veh-1", "Caminone"),
        row("/x/trip_20240102090000.csv", "veh-2", "Furgoncino"),
        row("/x/trip_20240103090000.csv", null, null, null),
    )

    @Test
    fun allShowsEveryFile() {
        assertEquals(3, TripsViewModel.applyFilter(twoCars, RecorderSettings.FILTER_ALL).size)
    }

    @Test
    fun aVehicleIdShowsOnlyItsOwnDrives() {
        val kept = TripsViewModel.applyFilter(twoCars, "veh-2")
        assertEquals(listOf("trip_20240102090000.csv"), kept.map { it.file.name })
    }

    @Test
    fun unknownShowsOnlyFilesWithoutATag() {
        // A file from before tagging, or a drive taken with no card selected. Both are
        // "we do not know", and both stay findable rather than being shown as some
        // vehicle they were never claimed to be.
        val kept = TripsViewModel.applyFilter(twoCars, RecorderSettings.FILTER_UNKNOWN)
        assertEquals(listOf("trip_20240103090000.csv"), kept.map { it.file.name })
    }

    @Test
    fun oneVehicleWithNothingUntaggedGetsNoFilterAtAll() {
        val oneCar = listOf(row("/x/a.csv", "veh-1"), row("/x/b.csv", "veh-1"))
        val options = TripsViewModel.filterOptionsFor(oneCar, mapOf("veh-1" to "Caminone"))
        assertTrue(options.isEmpty())
    }

    @Test
    fun anEmptyTripsListGetsNoFilterEither() {
        assertTrue(TripsViewModel.filterOptionsFor(emptyList(), mapOf("veh-1" to "Caminone")).isEmpty())
    }

    @Test
    fun oneVehiclePlusUntaggedDrivesIsWorthAFilter() {
        val rows = listOf(row("/x/a.csv", "veh-1"), row("/x/b.csv", null, null, null))
        assertEquals(
            listOf(
                TripFilterOption(RecorderSettings.FILTER_ALL, "ALL"),
                TripFilterOption("veh-1", "Caminone"),
                TripFilterOption(RecorderSettings.FILTER_UNKNOWN, "UNKNOWN"),
            ),
            TripsViewModel.filterOptionsFor(rows, mapOf("veh-1" to "Caminone")),
        )
    }

    @Test
    fun twoVehiclesAndNoUntaggedDrivesStillGetNoUnknownChip() {
        val rows = listOf(row("/x/a.csv", "veh-1"), row("/x/b.csv", "veh-2"))
        assertEquals(
            listOf("ALL", "Caminone", "Furgoncino"),
            TripsViewModel.filterOptionsFor(rows, mapOf("veh-1" to "Caminone", "veh-2" to "Furgoncino"))
                .map { it.label },
        )
    }

    @Test
    fun chipsAreNamedFromTheGarageRatherThanFromTheFile() {
        // The cards were renamed after these drives happened. The chip carries the name
        // in use now - that is the name a user would look for - while the files keep
        // saying what the card was called at the time.
        val rows = listOf(row("/x/a.csv", "veh-1", "Old name"), row("/x/b.csv", "veh-2", "Other"))
        val labels = TripsViewModel.filterOptionsFor(
            rows,
            mapOf("veh-1" to "New name", "veh-2" to "Other"),
        ).map { it.label }
        assertEquals(listOf("ALL", "New name", "Other"), labels)
    }

    @Test
    fun aFileWhoseCardWasDeletedStillGetsAChip() {
        // Its drives are the only thing left of that vehicle, which is precisely when a
        // filter for it is worth having. It is labelled without inventing a name.
        val rows = listOf(row("/x/a.csv", "veh-9", "Ghost"), row("/x/b.csv", "veh-1"))
        val options = TripsViewModel.filterOptionsFor(rows, mapOf("veh-1" to "Caminone"))
        assertEquals(listOf("ALL", "(no card)", "Caminone"), options.map { it.label })
        assertEquals("veh-9", options[1].value)
    }

    @Test
    fun aRememberedFilterForAVehicleThatIsGoneYieldsNothingAndIsNotOffered() {
        // applyFilter is honest: an id with no drives yields no rows. The ViewModel then
        // falls back to ALL rather than leaving the user on an empty page whose label is
        // an id they have never seen.
        assertTrue(TripsViewModel.applyFilter(twoCars, "veh-deleted").isEmpty())
        val options = TripsViewModel.filterOptionsFor(
            twoCars,
            mapOf("veh-1" to "Caminone", "veh-2" to "Furgoncino"),
        )
        assertTrue(options.none { it.value == "veh-deleted" })
    }

    @Test
    fun aFilterThatHidesRowsStaysOfferedEvenWhenThereIsNothingElseToNarrow() {
        // The state reached on a real tablet, where this rule came from: `UNKNOWN` was
        // remembered from an earlier session, every file had since been recorded under a
        // card, and the page showed `0 of 3` with no chip row at all - no way back to ALL
        // anywhere on the screen. The rule that a control with one choice is decoration
        // must not outrank the rule that a filter which hides rows has to be escapable.
        val oneCar = listOf(row("/x/a.csv", "veh-1", "Caminone"), row("/x/b.csv", "veh-1", "Caminone"))
        val options = TripsViewModel.filterOptionsFor(oneCar, mapOf("veh-1" to "Caminone"), RecorderSettings.FILTER_UNKNOWN)
        assertEquals(listOf("ALL", "Caminone", "UNKNOWN"), options.map { it.label })
        // …and the empty state can name the chip that is on, instead of printing its code.
        assertEquals("UNKNOWN", options.first { it.value == RecorderSettings.FILTER_UNKNOWN }.label)
    }

    @Test
    fun aFilterThatHidesNothingStillLeavesTheRowOut() {
        // The boundary of that rule: a filter matching every row hides nothing, so the
        // list is not trapped and the decoration rule stands.
        val oneCar = listOf(row("/x/a.csv", "veh-1", "Caminone"))
        assertTrue(TripsViewModel.filterOptionsFor(oneCar, mapOf("veh-1" to "Caminone"), "veh-1").isEmpty())
    }
}
