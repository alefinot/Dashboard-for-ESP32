package com.alefinot.dashboardpp.trips

import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

/**
 * Who a recorded drive belongs to (plan §6).
 *
 * The recorder is deliberately free of the garage: it is handed samples, and a snapshot
 * carries no identity of its own (`/api/snapshot` has no mac or efuse field - only the
 * verify step reports the unit's eFuse MAC). So identity arrives as one pre-rendered tag
 * line, and the interesting property is *when* that line is allowed to land: a file's tag
 * is written once, when the file is created, and never rewritten afterwards. Rename a
 * vehicle and last year's drives keep saying what the card was called at the time - which
 * is what a record of a drive should hold.
 */
class RecorderTagTest {

    private var now = 1_730_000_000_000L
    private val writer = FakeWriter()
    private val recorder = TripRecorder(writer, { startMs -> "trip-$startMs.csv" }, { now })

    private fun drive(up: Long, spd: Double = 20.0, ip: String = "10.0.0.5") {
        recorder.onSnapshot(ip, snap(up = up, spd = spd, odo = 10.0 + up * 0.01))
        now += 1000L
    }

    @Test
    fun aBoundRecorderWritesWhoseDriveItIs() {
        recorder.bindVehicle("veh-7", "5E:FF:00:11:22:33", "Caminone")
        drive(up = 100)

        assertEquals(
            "#vehicle=veh-7|5E:FF:00:11:22:33|Caminone",
            writer.tags[writer.pathAt(0)],
        )
        // The tag is not the only thing in the file: the samples are still there.
        assertEquals(1, writer.lines(writer.pathAt(0)).size)
    }

    @Test
    fun anUnboundRecorderStillRecordsAndWritesNoOwner() {
        // No card selected, or a firmware old enough to report no MAC: an untagged file
        // is honest, and the drive is not lost because of it.
        assertEquals(TripEvent.OPENED, recorder.onSnapshot("10.0.0.5", snap(up = 100, spd = 12.0)))
        assertEquals("", writer.tags[writer.pathAt(0)])
        // Line 1 is the header, not a half-written tag: a reader that looks for
        // `#vehicle=` on a file like this must find nothing and move on.
        assertTrue(writer.files[writer.pathAt(0)]!!.startsWith(TripFormat.HEADER))
    }

    @Test
    fun bindingAfterAFileIsOpenLeavesThatFileAlone() {
        drive(up = 100)
        recorder.bindVehicle("veh-9", "AA:BB:CC:DD", "Renamed Midway")
        drive(up = 101)

        // One drive, one file, and the tag it started with - the file being written is
        // not retroactively re-owned.
        assertEquals(1, writer.files.size)
        assertEquals("", writer.tags[writer.pathAt(0)])
        assertEquals(2, writer.lines(writer.pathAt(0)).size)
    }

    @Test
    fun theNextFileCarriesTheBindingOfTheMomentItOpens() {
        recorder.bindVehicle("veh-1", "AA:BB:CC:00:00:01", "One")
        drive(up = 500)
        // The dashboard restarts: `up` goes backwards, which seals the drive and opens
        // a new file. A different power session may be a different car - the card may
        // have been changed while the vehicle was parked - so the new file takes the
        // binding as it stands now, not the one the previous drive had.
        recorder.bindVehicle("veh-2", "AA:BB:CC:00:00:02", "Two")
        drive(up = 5)

        assertEquals(2, writer.files.size)
        assertEquals("#vehicle=veh-1|AA:BB:CC:00:00:01|One", writer.tags[writer.pathAt(0)])
        assertEquals("#vehicle=veh-2|AA:BB:CC:00:00:02|Two", writer.tags[writer.pathAt(1)])
    }

    @Test
    fun aDriveOnAnotherDeviceIsNotSilentlyReOwned() {
        // A different ip seals the drive first (two dashboards can never be one trip),
        // so the new unit's file is the first place the new tag may appear.
        recorder.bindVehicle("veh-1", "AA:BB:CC:00:00:01", "One")
        drive(up = 100)
        recorder.bindVehicle("veh-2", "AA:BB:CC:00:00:02", "Two")
        // The files are named for the phone time the drive started, so the second unit
        // has to start a minute later to be a second file at all.
        now += 60_000L
        drive(up = 100, ip = "192.168.4.1")

        assertEquals(2, writer.files.size)
        assertEquals("#vehicle=veh-1|AA:BB:CC:00:00:01|One", writer.tags[writer.pathAt(0)])
        assertEquals("#vehicle=veh-2|AA:BB:CC:00:00:02|Two", writer.tags[writer.pathAt(1)])
    }
}
