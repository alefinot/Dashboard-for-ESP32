package com.alefinot.dashboardpp.ui.screens

import com.alefinot.dashboardpp.vehicles.Vehicle
import com.alefinot.dashboardpp.vehicles.VehicleType
import com.alefinot.dashboardpp.viewmodel.ConnectMode
import com.alefinot.dashboardpp.viewmodel.ConnectionUiState
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

/**
 * What a garage card is allowed to claim about a vehicle.
 *
 * A card is the only place the app writes a sentence about a car, and a sentence like
 * "LAST SEEN 2 DAYS AGO" next to "ESP32 at 192.168.1.50 answered /api/perf, but that unit
 * is not the vehicle you selected" is not two equal pieces of text: the second is a
 * measurement, the first is a guess about time. So the wording is built here, in plain
 * functions, where the boundary cases are testable rather than visible once a year on a
 * screen.
 *
 * The dates are counted in **UTC day boundaries**. That is a deliberate choice, not an
 * oversight: a car driven at 23:50 at home and looked at from a hotel in another zone the
 * next morning is still "yesterday", and a card that changed its story because the phone
 * moved zones would be a card reporting its own clock instead of the car.
 */
class VehicleCardLinesTest {

    private val day = 24L * 60 * 60 * 1000
    // 2024-10-26 13:40 UTC: mid-afternoon, so "30 hours ago" is yesterday rather
    // than two days ago by the luck of where the timestamp happened to fall.
    private val now = 1_729_950_000_000L

    // ── the spec line ────────────────────────────────────────────────────────
    @Test
    fun `the spec line names the kind, then the maker and model, then the year`() {
        val v = Vehicle(
            id = "v1", type = VehicleType.CAR, maker = "Toyota", model = "Corolla", year = "1998",
        )
        assertEquals("CAR · TOYOTA COROLLA · 1998", specLine(v))
    }

    @Test
    fun `a half-known vehicle does not leave dangling separators or blank gaps`() {
        // Every combination a user can realistically leave behind, and none of them may
        // read like a form that was half filled in.
        assertEquals("CAR", specLine(Vehicle(id = "v1")))
        assertEquals("CAR · TOYOTA", specLine(Vehicle(id = "v1", maker = "Toyota")))
        assertEquals(
            "CAR · TOYOTA COROLLA",
            specLine(Vehicle(id = "v1", maker = "Toyota", model = "Corolla")),
        )
        assertEquals("CAR · 1998", specLine(Vehicle(id = "v1", year = "1998")))
        assertEquals(
            "MOTORCYCLE · 1972",
            specLine(Vehicle(id = "v1", type = VehicleType.MOTORCYCLE, year = "1972")),
        )
    }

    @Test
    fun `whitespace typed into the editor does not reach the card`() {
        assertEquals("CAR · TOYOTA", specLine(Vehicle(id = "v1", maker = "  Toyota  ")))
        assertEquals("CAR", specLine(Vehicle(id = "v1", maker = "   ", model = "", year = " ")))
    }

    // ── the plate line ───────────────────────────────────────────────────────
    @Test
    fun `a plate that is not there is no line at all`() {
        assertNull(plateLine(Vehicle(id = "v1")))
        assertNull(plateLine(Vehicle(id = "v1", plate = "   ")))
        assertEquals("B 1234 CD", plateLine(Vehicle(id = "v1", plate = " b 1234 CD ")))
    }

    // ── how long ago the app last reached this vehicle ───────────────────────
    @Test
    fun `a vehicle the app has never connected to says so and does not invent a time`() {
        assertEquals("NEVER CONNECTED", lastSeenLine(null, now))
    }

    @Test
    fun `the recent end of the scale is the part a driver actually reads`() {
        assertEquals("LAST SEEN JUST NOW", lastSeenLine(now - 4_000, now))
        assertEquals("LAST SEEN JUST NOW", lastSeenLine(now - 59_000, now))
        // Same UTC day: a drive this morning is still "today" at lunchtime.
        assertEquals("LAST SEEN TODAY", lastSeenLine(now - 3 * 60 * 60 * 1000, now))
    }

    @Test
    fun `day boundaries are UTC and the day before is named, not counted`() {
        // 18:33 UTC minus 30 hours = the day before. Counting that as "1 days ago" is
        // both wrong English and less useful than the word.
        assertEquals("LAST SEEN YESTERDAY", lastSeenLine(now - 30 * 60 * 60 * 1000, now))
        assertEquals("LAST SEEN 2 DAYS AGO", lastSeenLine(now - 2 * day - 60_000, now))
        assertEquals("LAST SEEN 6 DAYS AGO", lastSeenLine(now - 6 * day, now))
        assertEquals("LAST SEEN 20 DAYS AGO", lastSeenLine(now - 20 * day, now))
    }

    @Test
    fun `the long end of the scale counts months and then years, singular or plural`() {
        // A card that has been sitting since last season reading "410 DAYS AGO" is asking
        // the user to do arithmetic.
        assertEquals("LAST SEEN 1 MONTH AGO", lastSeenLine(now - 35 * day, now))
        assertEquals("LAST SEEN 2 MONTHS AGO", lastSeenLine(now - 70 * day, now))
        assertEquals("LAST SEEN 1 YEAR AGO", lastSeenLine(now - 380 * day, now))
        assertEquals("LAST SEEN 3 YEARS AGO", lastSeenLine(now - 3 * 365 * day, now))
    }

    // ── what wins when several sentences are true ────────────────────────────
    @Test
    fun `a measurement always beats the derived guess about time`() {
        val v = Vehicle(id = "v1", ips = listOf("192.168.1.50"), lastConnectedAtMs = now - 2 * day)
        val reason = "ESP32 at 192.168.1.50 answered /api/perf, but that unit is not the " +
            "vehicle you selected."
        val failed = cardStatusLine(v, ConnectionUiState.Failed(reason), now)
        // Verbatim, not paraphrased: this is the exact sentence the connection code
        // produced, and the card has no business smoothing it over.
        assertEquals(reason, failed.text)
        assertEquals(CardTone.BAD, failed.tone)
    }

    @Test
    fun `a running ladder, a live session and an idle reason each own their line`() {
        val v = Vehicle(id = "v1", ips = listOf("192.168.1.50"), lastConnectedAtMs = now - day)
        val connecting = cardStatusLine(v, ConnectionUiState.Connecting(ConnectMode.SEARCH), now)
        assertEquals(ConnectMode.SEARCH.stageLine().uppercase(), connecting.text)
        assertEquals(CardTone.LIVE, connecting.tone)

        val connected = cardStatusLine(
            v,
            ConnectionUiState.Connected(ip = "192.168.1.50", version = "1.4.6", mac = null, vehicleId = "v1"),
            now,
        )
        assertEquals("CONNECTED · 192.168.1.50", connected.text)
        assertEquals(CardTone.GOOD, connected.tone)

        val idle = cardStatusLine(v, ConnectionUiState.Idle("No vehicle selected yet."), now)
        assertEquals("No vehicle selected yet.", idle.text)
        assertEquals(CardTone.WARN, idle.tone)
    }

    @Test
    fun `with nothing running the card falls back to when it last answered`() {
        assertEquals(
            "LAST SEEN YESTERDAY",
            cardStatusLine(Vehicle(id = "v1", lastConnectedAtMs = now - 26 * 60 * 60 * 1000), null, now).text,
        )
        val never = cardStatusLine(Vehicle(id = "v1"), null, now)
        assertEquals("NEVER CONNECTED", never.text)
        assertEquals(CardTone.DIM, never.tone)
    }

    @Test
    fun `only the card that owns a session may offer to end it`() {
        val a = Vehicle(id = "a", name = "Corolla", ips = listOf("192.168.1.50"))
        val b = Vehicle(id = "b", name = "Van", ips = listOf("192.168.1.77"))
        val state = ConnectionUiState.Connected(
            ip = "192.168.1.50", version = "1.4.6", mac = null, vehicleId = "a",
        )
        assertTrue(connectedCard(a, state))
        // The selection has moved to the van; the Corolla still owns the session, so it
        // keeps its DISCONNECT and the van must not offer one.
        assertFalse(connectedCard(b, state))
        assertFalse(connectedCard(a, ConnectionUiState.Idle(null)))
    }

    @Test
    fun `a silent reconnect never animates a card, a hunt the user asked for does`() {
        // The garage is the home screen: a cold start is the most-seen animation in the app,
        // and a ring that spins there would put motion in front of a page the user did not
        // ask to search. PROBE is two addresses and finishes in about a second - a ring that
        // fast is a flicker, not progress.
        assertFalse(showScanRing(ConnectionUiState.Connecting(ConnectMode.SILENT)))
        assertFalse(showScanRing(ConnectionUiState.Connecting(ConnectMode.PROBE)))
        assertTrue(showScanRing(ConnectionUiState.Connecting(ConnectMode.SEARCH)))
        assertFalse(showScanRing(ConnectionUiState.Connected("1.2.3.4", "1.4.6", null, "a")))
        assertFalse(showScanRing(null))
    }

    @Test
    fun `every tone maps to a colour, and no two tones share one`() {
        // The status line carries its meaning in colour as much as in words, and a status
        // that is the same colour as another status is information the page stops
        // delivering. A colour-blind user or a sunlit screen is where this shows up.
        val colors = CardTone.values().map { statusColor(it) }
        assertEquals(CardTone.values().size, colors.distinct().size)
    }
}
