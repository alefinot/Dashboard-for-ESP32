package com.alefinot.dashboardpp.trips

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test
import java.io.File

/**
 * The shareable artefacts (trip recording plan §6): GPX for map apps, and the
 * file names a user sees when they open the folder or share a drive.
 */
class TripExportTest {

    private val base = 1_730_000_000_000L

    private fun route(): List<TripSample> = listOf(
        // No fix yet — must not become a track point, or the route would start
        // with a line from (0,0) in the ocean.
        TripSample(base, snap(up = 10, spd = 0.0, lat = 0.0, lon = 0.0, fix = 0)),
        TripSample(
            base + 1000L,
            snap(up = 11, spd = 36.0, lat = -6.914000, lon = 107.610000, fix = 1, alt = 743.0),
        ),
        TripSample(
            base + 2000L,
            snap(up = 12, spd = 54.0, lat = -6.920000, lon = 107.615000, fix = 1, alt = 700.0),
        ),
        // Fix lost again, with a stale coordinate that has to be ignored.
        TripSample(base + 3000L, snap(up = 13, spd = 10.0, lat = 0.0, lon = 0.0, fix = 0)),
    )

    @Test
    fun gpxHoldsOnlyThePointsThatHadAFix() {
        val xml = GpxWriter.gpx(route(), "2026-10-26 drive")
        assertEquals(2, Regex("<trkpt ").findAll(xml).count())
        assertEquals(2, Regex("</trkpt>").findAll(xml).count())
        assertTrue(xml, xml.contains("lat=\"-6.914\"") || xml.contains("lat=\"-6.91400\""))
        assertFalse("a no-fix sample must never reach the route", xml.contains("lat=\"0.0\""))
    }

    @Test
    fun gpxIsBalancedXmlWithTheElementsConsumersRead() {
        val xml = GpxWriter.gpx(route(), "Trip & <test>")
        assertTrue(xml, xml.startsWith("<?xml"))
        assertTrue(xml, xml.contains("<gpx version=\"1.1\""))
        assertTrue(xml, xml.contains("<trkseg>"))
        assertTrue(xml, xml.contains("</trkseg>"))
        assertTrue(xml, xml.contains("<name>Trip &amp; &lt;test&gt;</name>"))
        assertTrue(xml, xml.contains("<ele>743.0</ele>"))
        // speed in extensions is m/s, which is what GPX consumers expect
        assertTrue(xml, xml.contains("<speed>10.00</speed>"))
        assertTrue(xml, xml.contains("<time>"))
        assertEquals(Regex("<trkpt").findAll(xml).count(), Regex("</trkpt>").findAll(xml).count())
    }

    @Test
    fun gpxOfARoutelessTripIsStillValid() {
        val xml = GpxWriter.gpx(listOf(TripSample(base, snap(up = 1))), "no gps")
        assertTrue(xml, xml.contains("<trkseg>"))
        assertTrue(xml, xml.contains("</trkseg>"))
        assertEquals(0, Regex("<trkpt ").findAll(xml).count())
    }

    @Test
    fun writeProducesAReadableFile() {
        // A plain java.io temp file rather than a JUnit TemporaryFolder rule, so
        // the test needs no extra dependency.
        val tmp = File.createTempFile("trip", ".gpx")
        try {
            GpxWriter.write(tmp, route(), "drive")
            assertTrue(tmp.length() > 100)
            assertTrue(tmp.readText().contains("<gpx"))
        } finally {
            tmp.delete()
        }
    }

    @Test
    fun twoDrivesInTheSameSecondNeverShareAName() {
        val used = mutableSetOf<String>()
        val a = TripStore.uniqueName("20261026-141530") { used.add(it); false }
        val b = TripStore.uniqueName("20261026-141530") { name -> name == a }
        val c = TripStore.uniqueName("20261026-141530") { name -> name == a || name == b }
        assertEquals("trip-20261026-141530-001.csv", a)
        assertEquals("trip-20261026-141530-002.csv", b)
        assertEquals("trip-20261026-141530-003.csv", c)
    }

    @Test
    fun firmwareVersionGateIsNumericNotLexicographic() {
        assertTrue(TripsEnabled.forFirmware("1.4.6"))
        assertTrue(TripsEnabled.forFirmware("1.4.7"))
        assertTrue(TripsEnabled.forFirmware("1.5.0"))
        assertTrue(TripsEnabled.forFirmware("V1.4.6"))
        assertTrue(TripsEnabled.forFirmware("2.0.0"))
        assertFalse(TripsEnabled.forFirmware("1.4.5"))
        assertFalse(TripsEnabled.forFirmware("1.3.12"))
        assertFalse(TripsEnabled.forFirmware("0.9.9"))
        // "1.10.0" is newer than "1.4.6"; a string compare would say otherwise.
        assertTrue(TripsEnabled.forFirmware("1.10.0"))
        // Unknown shapes are left to the runtime 404 probe rather than guessing.
        assertTrue(TripsEnabled.forFirmware(null))
        assertTrue(TripsEnabled.forFirmware("nightly"))
    }
}
