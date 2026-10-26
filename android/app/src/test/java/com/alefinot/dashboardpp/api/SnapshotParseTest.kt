package com.alefinot.dashboardpp.api

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

/**
 * The two halves of the firmware↔app contract on one line: whatever
 * `snapshotGetHandler` in `src/web.cpp` prints has to parse here, and a device
 * without the endpoint (404 page, or another vendor's server) must be rejected
 * rather than half-believed.
 */
class SnapshotParseTest {

    /** Verbatim shape of the firmware's response, key order included. */
    private val withFix = """{"ver":"1.4.6","up":12345,"spd":63.5,"src":1,"avg":48.2,""" +
        """"max":91.2,"trp_km":18.40,"trp_l":1.35,"ikml":14.2,"akml":13.6,""" +
        """"odo":12480.25,"fuel_l":41.20,"fuel_pct":62,"fuel_st":1,"volts":13.92,""" +
        """"temp":94.5,"amb":21,"sats":9,"hdop":1.20,"alt":743,"lat":-6.914532,""" +
        """"lon":107.641239,"fix":1,"tmr_st":0,"tmr_s":0.00,"fps":28.4,"heap":118432}"""

    @Test
    fun snapshotCarriesEveryFieldTheFirmwarePrints() {
        val s = Snapshot.parse(withFix)
        assertNotNull(s)
        s!!
        assertEquals("1.4.6", s.version)
        assertEquals(12345L, s.up)
        assertEquals(63.5, s.spd, 1e-9)
        assertEquals(1, s.src)
        assertEquals(48.2, s.avg, 1e-9)
        assertEquals(91.2, s.max, 1e-9)
        assertEquals(18.40, s.trpKm, 1e-9)
        assertEquals(1.35, s.trpL, 1e-9)
        assertEquals(14.2, s.ikml, 1e-9)
        assertEquals(13.6, s.akml, 1e-9)
        assertEquals(12480.25, s.odo, 1e-9)
        assertEquals(41.20, s.fuelL, 1e-9)
        assertEquals(62, s.fuelPct)
        assertEquals(1, s.fuelSt)
        assertEquals(13.92, s.volts, 1e-9)
        assertEquals(94.5, s.temp, 1e-9)
        assertEquals(21, s.amb)
        assertEquals(9, s.sats)
        assertEquals(1.20, s.hdop, 1e-9)
        assertEquals(743.0, s.alt, 1e-9)
        assertEquals(-6.914532, s.lat, 1e-9)
        assertEquals(107.641239, s.lon, 1e-9)
        assertEquals(1, s.fix)
        assertEquals(0, s.tmrSt)
        assertEquals(0.0, s.tmrS, 1e-9)
        assertEquals(28.4, s.fps, 1e-9)
        assertEquals(118432L, s.heap)
        assertTrue(s.hasPosition)
    }

    @Test
    fun missingHdopAndAltitudeArriveAsMinusOneNotZero() {
        // The firmware prints -1 when the fix carried no hdop/altitude; the app
        // must show "no reading", never a plausible 0.
        val body = withFix.replace("\"hdop\":1.20", "\"hdop\":-1")
            .replace("\"alt\":743", "\"alt\":-1")
        val s = Snapshot.parse(body)!!
        assertEquals(-1.0, s.hdop, 1e-9)
        assertEquals(-1.0, s.alt, 1e-9)
    }

    @Test
    fun noFixMeansNoPosition() {
        val body = """{"ver":"1.4.6","up":42,"spd":0.0,"src":0,"avg":0.0,"max":0.0,""" +
            """"trp_km":0.00,"trp_l":0.00,"ikml":0.0,"akml":0.0,"odo":0.00,"fuel_l":0.00,""" +
            """"fuel_pct":0,"fuel_st":0,"volts":12.10,"temp":0.0,"amb":25,"sats":0,"hdop":-1,""" +
            """"alt":-1,"lat":0.000000,"lon":0.000000,"fix":0,"tmr_st":0,"tmr_s":0.00,""" +
            """"fps":30.0,"heap":90000}"""
        val s = Snapshot.parse(body)!!
        assertFalse(s.hasFix)
        assertFalse("a 0,0 coordinate must never be logged as a route point", s.hasPosition)
    }

    @Test
    fun a404PageIsNotASnapshot() {
        assertNull(Snapshot.parse("<h1>404</h1>"))
        assertNull(Snapshot.parse(""))
        assertNull(Snapshot.parse("Not found"))
        // Another device on the LAN answering with valid-but-foreign JSON.
        assertNull(Snapshot.parse("""{"status":"ok","value":5}"""))
    }

    @Test
    fun flatJsonRejectsNestedOrMalformedBodies() {
        assertEquals(mapOf("a" to "1", "b" to "x"), FlatJson.parse("""{"a":1,"b":"x"}"""))
        assertNull(FlatJson.parse("""{"a":{"b":1}}"""))
        assertNull(FlatJson.parse("""{"a":[1,2]}"""))
        assertNull(FlatJson.parse("""not json"""))
    }

    @Test
    fun absentKeysFallBackInsteadOfThrowing() {
        val f = JsonFields(FlatJson.parse("""{"up":7}""")!!)
        assertEquals(7L, f.long("up"))
        assertEquals(0.0, f.dbl("spd"), 1e-9)
        assertEquals("", f.str("ver"))
        assertFalse(f.has("spd"))
    }
}
