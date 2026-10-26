package com.alefinot.dashboardpp.trips

import org.junit.Assert.assertEquals
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertNull
import org.junit.Test
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale

/**
 * The file name carries the trip's identity: the minute-second stamp is when the
 * drive began. The Trips list reads that stamp back out, because the file's
 * mtime is the end of the drive (or "still recording"), not its start.
 */
class TripStoreTest {

    @Test
    fun `a generated name reads back as the same instant`() {
        // Two patterns: the instant as a person writes it, and the stamp the
        // file name uses. The round trip must land on the same second.
        val readable = SimpleDateFormat("yyyy-MM-dd HH:mm:ss", Locale.US)
        val stamp = SimpleDateFormat("yyyyMMdd-HHmmss", Locale.US)
        val startMs = readable.parse("2026-10-26 13:39:46")!!.time
        val name = TripStore.uniqueName(stamp.format(Date(startMs))) { false }
        assertEquals("trip-20261026-133946-001.csv", name)
        assertEquals(startMs, TripStore.startMsOf(name)!!.toLong())
    }

    @Test
    fun `a real device file name is recognised`() {
        val ms = TripStore.startMsOf("trip-20261026-133946-001.csv")
        assertNotNull(ms)
        val back = SimpleDateFormat("yyyy-MM-dd HH:mm:ss", Locale.US).format(Date(ms!!))
        assertEquals("2026-10-26 13:39:46", back)
    }

    @Test
    fun `names that are not ours are rejected`() {
        assertNull(TripStore.startMsOf("trip-foreign-999.csv"))
        assertNull(TripStore.startMsOf("notes.csv"))
        assertNull(TripStore.startMsOf("trip-20261026-133946-01.csv"))
        assertNull(TripStore.startMsOf("trip-20261026-133946-001.csv.bak"))
        // Well-formed for the pattern, impossible as a date.
        assertNull(TripStore.startMsOf("trip-20261301-133946-001.csv"))
        assertNull(TripStore.startMsOf("trip-20261026-246061-001.csv"))
    }
}
