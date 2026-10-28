package com.alefinot.dashboardpp.ui.screens

import androidx.compose.ui.graphics.Color
import com.alefinot.dashboardpp.vehicles.Vehicle
import com.alefinot.dashboardpp.viewmodel.ConnectMode
import com.alefinot.dashboardpp.viewmodel.ConnectionUiState
import com.alefinot.dashboardpp.ui.theme.HudColors
import kotlin.math.roundToInt

/**
 * The words on a garage card, built away from the screen so the boundary cases can be
 * tested (see `VehicleCardLinesTest`).
 *
 * A card carries three lines, and they are not the same kind of statement:
 *
 * - the **spec** line and the **plate** line are the user's own description of the
 *   vehicle, half-filled on purpose most of the time;
 * - the **status** line is the app's evidence about a physical unit, and it is the only
 *   line on the card that can be wrong. So it never paraphrases: while a ladder is
 *   running or has just failed, the status line is the connection code's own sentence,
 *   word for word, and only when nothing is running does the card fall back to a derived
 *   "LAST SEEN …" about time.
 *
 * Timestamps are counted in **UTC day boundaries** ([lastSeenLine]). A vehicle driven at
 * 23:50 at home and looked at from another timezone the next morning is still "yesterday";
 * a card that recomputed that from the phone's current zone would be reporting its own
 * clock rather than the car.
 */

/** How much weight the status line deserves, decided apart from the colour it is drawn in. */
enum class CardTone {
    /** A unit is connected and reporting. */
    GOOD,

    /** A ladder the user asked for is running. */
    LIVE,

    /** Nothing is connected, and the reason is the user's to act on. */
    WARN,

    /** Something was tried and did not work. */
    BAD,

    /** A fact about the past: saved, seen once, not here now. */
    MUTED,

    /** The app has never reached this vehicle at all. */
    DIM,
}

/**
 * The colour for a tone. Kept next to [CardTone] rather than inside the composable so the
 * "no two tones look the same" property is testable: a status that shares another's colour
 * stops being information the moment the sun comes up, and a status that is only told apart
 * by colour is already lost to a colour-blind driver. The palette is the page's own — green
 * for a session, cyan for a ladder the user started, amber for something to act on, red for
 * something that failed, and the two text greys for facts about the past.
 */
fun statusColor(tone: CardTone): Color = when (tone) {
    CardTone.GOOD -> HudColors.Green
    CardTone.LIVE -> HudColors.Cyan
    CardTone.WARN -> HudColors.Amber
    CardTone.BAD -> HudColors.Red
    CardTone.MUTED -> HudColors.TextMuted
    CardTone.DIM -> HudColors.TextDim
}

/** One status line: what the card says about whether this vehicle is reachable. */
data class CardStatusLine(val text: String, val tone: CardTone)

/** Trim a field for display; blank becomes absent, so nothing dangles. */
private fun fieldOf(value: String): String? = value.trim().takeIf { it.isNotEmpty() }

/**
 * `TYPE · MAKER MODEL · YEAR`, in the app's uppercase HUD voice, with whatever the user
 * left out simply missing.
 *
 * The blank-collapsing is the whole point: a card that reads "CAR ·  ·" or "CAR ·  1998"
 * tells the user the form is half-filled instead of telling them about the car. The type
 * is always there (it has a default in the record), so the line is never empty for a
 * saved vehicle.
 */
fun specLine(v: Vehicle): String {
    // The whole line is uppercase because the rest of the HUD is; the user types
    // "Toyota Corolla" and the card answers in the page's voice.
    val makerModel = listOfNotNull(fieldOf(v.maker), fieldOf(v.model))
        .joinToString(" ")
    val what = listOfNotNull(
        v.type.name,
        makerModel.takeIf { it.isNotEmpty() }?.uppercase(),
        fieldOf(v.year),
    )
    return what.joinToString(" · ")
}

/** The plate, cleaned, or `null` when there is none — the caller draws no line at all. */
fun plateLine(v: Vehicle): String? = fieldOf(v.plate)?.uppercase()

/**
 * "How long ago did this vehicle last answer?" in the units a person reads.
 *
 * Day counts are calendar days in UTC (not 24-hour windows), then the scale switches to
 * months and years: a card that has sat since last season showing "410 DAYS AGO" asks the
 * user to do arithmetic, and one showing "37 MONTHS AGO" is worse. The exact thresholds
 * are arbitrary in a way the boundaries are not — anywhere between three weeks and two
 * months, and between nine and fourteen months, reads the same to a human.
 */
fun lastSeenLine(lastConnectedAtMs: Long?, nowMs: Long): String {
    val then = lastConnectedAtMs ?: return "NEVER CONNECTED"
    if (nowMs - then < MINUTE) return "LAST SEEN JUST NOW"
    val days = daysBetweenUtc(then, nowMs)
    if (days == 0L) return "LAST SEEN TODAY"
    if (days == 1L) return "LAST SEEN YESTERDAY"
    if (days <= 21L) return "LAST SEEN $days DAYS AGO"
    val years = (days / 365.25).roundToInt()
    if (days >= 320L && years >= 1) return "LAST SEEN ${counted(years, "YEAR")}"
    val months = (days / 30.44).roundToInt().coerceAtLeast(1)
    return "LAST SEEN ${counted(months, "MONTH")}"
}

/** One or two, in English: "1 MONTH AGO", "2 MONTHS AGO". */
private fun counted(n: Int, unit: String) =
    if (n == 1) "$n $unit AGO" else "$n ${unit}S AGO"

/** Whole calendar days between two instants, counted in UTC. */
private fun daysBetweenUtc(fromMs: Long, toMs: Long): Long =
    toMs.floorDiv(DAY_MS) - fromMs.floorDiv(DAY_MS)

private const val MINUTE = 60_000L
private const val DAY_MS = 24L * 60 * 60 * 1000

/**
 * The status line for one card, given the page-wide connection state.
 *
 * **Only the selected card can be mid-ladder** — the app holds one device at a time, so a
 * [ConnectionUiState] that is about a live attempt belongs to the selected vehicle's card
 * and to no other. That is why the state is passed in whole and the caller decides which
 * card gets it (see [VehicleCardLinesTest]'s precedence cases):
 *
 * 1. a live or failed attempt says exactly what it said, and it wins over any guess about
 *    the past — the user is watching something happen *now*;
 * 2. a live session says which address is reporting;
 * 3. otherwise the card falls back to [lastSeenLine], which is a derived statement about
 *    time and must never cover a measurement.
 */
fun cardStatusLine(v: Vehicle, state: ConnectionUiState?, nowMs: Long): CardStatusLine = when (state) {
    is ConnectionUiState.Connected ->
        CardStatusLine("CONNECTED · ${state.ip}", CardTone.GOOD)

    is ConnectionUiState.Connecting ->
        CardStatusLine(state.mode.stageLine().uppercase(), CardTone.LIVE)

    is ConnectionUiState.Failed -> CardStatusLine(state.reason, CardTone.BAD)

    is ConnectionUiState.Idle ->
        state.reason?.takeIf { it.isNotBlank() }
            ?.let { CardStatusLine(it, CardTone.WARN) }
            ?: fallback(v, nowMs)

    null -> fallback(v, nowMs)
}

private fun fallback(v: Vehicle, nowMs: Long): CardStatusLine =
    if (v.lastConnectedAtMs == null) CardStatusLine("NEVER CONNECTED", CardTone.DIM)
    else CardStatusLine(lastSeenLine(v.lastConnectedAtMs, nowMs), CardTone.MUTED)

/**
 * Whether a live session belongs to this card.
 *
 * The session says which card it is on ([ConnectionUiState.Connected.vehicleId]), so this
 * is a lookup and not a guess. It matters for the actions: the card that has a session is
 * the one that must keep offering DISCONNECT even after the user taps a different card, and
 * no other card may offer it — a second disconnect button would end a session that another
 * card owns, which is exactly the kind of thing an app does when cards are really
 * bookmarks.
 */
fun connectedCard(v: Vehicle, state: ConnectionUiState?): Boolean =
    state is ConnectionUiState.Connected && state.vehicleId == v.id

/**
 * Whether the card should show a search ring: true only for a ladder the user asked for
 * with their hand. A [ConnectMode.SILENT] reconnect must never put motion in front of the
 * garage (the page is the home screen, and a cold start is the most common thing a user
 * ever watches), and [ConnectMode.PROBE] is a two-address attempt that finishes in about a
 * second — a ring that fast is a flicker, not progress.
 */
fun showScanRing(state: ConnectionUiState?): Boolean =
    state is ConnectionUiState.Connecting && state.mode == ConnectMode.SEARCH
