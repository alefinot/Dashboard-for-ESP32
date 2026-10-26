package com.alefinot.dashboardpp.trips

import org.junit.Assert.assertEquals
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

/**
 * The CSV contract (trip recording plan §4). These tests are the reason the
 * column order can never drift silently: the header, the writer and the reader
 * are asserted against one another, and the file is what users keep and open in
 * a spreadsheet long after this app changes.
 */
class TripFormatTest {

    @Test
    fun headerListsEveryColumn() {
        assertEquals(25, TripFormat.COLUMNS.size)
        assertEquals(TripFormat.COLUMNS.joinToString(","), TripFormat.HEADER)
    }

    @Test
    fun lineRoundTripsEveryField() {
        val s = snap(
            up = 4211, spd = 63.5, odo = 12480.25, trpKm = 18.4, trpL = 1.35,
            max = 91.2, volts = 13.92, temp = 94.5, lat = -6.914532, lon = 107.641239,
            fix = 1, alt = 743.0, hdop = 1.2, sats = 9,
        )
        val sample = TripSample(1_730_000_000_123L, s)
        val back = TripFormat.parseLine(TripFormat.line(sample))
        assertNotNull(back)
        assertEquals(sample.tsMs, back!!.tsMs)
        val b = back.snapshot
        assertEquals(4211L, b.up)
        assertEquals(63.5, b.spd, 1e-9)
        assertEquals(12480.25, b.odo, 1e-9)
        assertEquals(18.4, b.trpKm, 1e-9)
        assertEquals(1.35, b.trpL, 1e-9)
        assertEquals(91.2, b.max, 1e-9)
        assertEquals(13.92, b.volts, 1e-9)
        assertEquals(94.5, b.temp, 1e-9)
        assertEquals(-6.914532, b.lat, 1e-9)
        assertEquals(107.641239, b.lon, 1e-9)
        assertEquals(1, b.fix)
        assertEquals(743.0, b.alt, 1e-9)
        assertEquals(1.2, b.hdop, 1e-9)
        assertEquals(9, b.sats)
    }

    @Test
    fun decimalsAlwaysUseDotRegardlessOfLocale() {
        java.util.Locale.setDefault(java.util.Locale("de"))
        val line = TripFormat.line(TripSample(1L, snap(up = 1, spd = 12.75, odo = 5.5)))
        assertTrue(line, line.contains("12.75"))
        assertTrue(line, line.contains("5.5"))
        java.util.Locale.setDefault(java.util.Locale.US)
    }

    @Test
    fun truncatedLastLineIsRejectedNotFatal() {
        // A process kill mid-write can leave a half line; readers must skip it
        // instead of losing the whole trip.
        assertNull(TripFormat.parseLine("1730000000123,42,63.5,1,63.5,91"))
        assertNull(TripFormat.parseLine("not,a,trip,line"))
        assertNull(TripFormat.parseLine(""))
    }

    @Test
    fun headerIsRecognised() {
        assertTrue(TripFormat.isHeader(TripFormat.HEADER))
        assertTrue(!TripFormat.isHeader("1730000000123,42,0.0"))
    }

    @Test
    fun summaryCodecRoundTrips() {
        val acc = TripAccumulator()
        var odo = 100.0
        for (i in 0 until 20) {
            odo += 0.05
            acc.add(
                TripSample(
                    1_000_000L + i * 1000L,
                    snap(up = 10L + i, spd = 40.0 + i, odo = odo, trpKm = i * 0.05,
                        trpL = i * 0.004, volts = 13.1, temp = 90.0,
                        lat = -6.9 + i * 0.001, lon = 107.6 + i * 0.001, fix = 1, alt = 100.0),
                ),
            )
        }
        val summary = acc.summary()!!
        val back = TripSummaryCodec.decode(TripSummaryCodec.encode(summary))
        assertNotNull(back)
        assertEquals(summary.startMs, back!!.startMs)
        assertEquals(summary.samples, back.samples)
        assertEquals(summary.distanceKm, back.distanceKm, 1e-3)
        assertEquals(summary.maxSpeed, back.maxSpeed, 1e-3)
        assertEquals(summary.fuelUsedL, back.fuelUsedL, 1e-3)
        assertEquals(summary.fixPct, back.fixPct)
        assertEquals(summary.gaps, back.gaps)
    }

    @Test
    fun summaryCodecRejectsGarbage() {
        assertNull(TripSummaryCodec.decode("nonsense"))
        assertNull(TripSummaryCodec.decode(""))
    }

    @Test
    fun accumulatorMarksGapsAndKeepsOdometerDistance() {
        val acc = TripAccumulator()
        // 0–9 s at 1 Hz, then a 40 s hole (Web UI open, or the app was closed),
        // then 5 more samples.
        var t = 0L
        for (i in 0 until 10) {
            acc.add(TripSample(t, snap(up = i.toLong(), spd = 50.0, odo = 10.0 + i * 0.014)))
            t += 1000L
        }
        t += 39_000L
        for (i in 0 until 5) {
            acc.add(TripSample(t, snap(up = (10 + i).toLong(), spd = 60.0, odo = 10.14 + i * 0.017)))
            t += 1000L
        }
        val s = acc.summary()!!
        assertEquals(15, s.samples)
        assertEquals(1, s.gaps)
        assertEquals(40L, s.gapSeconds)
        // Distance comes from the ESP odometer delta, never from the clock.
        assertEquals(0.208, s.distanceKm, 1e-9)
        assertEquals(60.0, s.maxSpeed, 1e-9)
        assertTrue(s.avgSpeed in 50.0..60.0)
    }

    @Test
    fun zeroVoltsAndZeroTempDoNotDragTheMinimumDown() {
        val acc = TripAccumulator()
        acc.add(TripSample(0L, snap(up = 0, volts = 0.0, temp = 0.0)))
        acc.add(TripSample(1000L, snap(up = 1, volts = 12.4, temp = 85.0)))
        val s = acc.summary()!!
        assertEquals(12.4, s.minVolts, 1e-9)
        assertEquals(85.0, s.minTemp, 1e-9)
    }

    @Test
    fun haversineMatchesKnownDistance() {
        // Bandung → Jakarta, roughly 120 km as the crow flies.
        val m = TripAccumulator.haversineM(-6.9147, 107.6098, -6.2088, 106.8456)
        assertTrue(m.toString(), m in 100_000.0..120_000.0)
    }
}
