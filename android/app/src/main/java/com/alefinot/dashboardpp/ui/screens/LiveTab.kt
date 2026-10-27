package com.alefinot.dashboardpp.ui.screens

import androidx.compose.animation.Crossfade
import androidx.compose.animation.animateColorAsState
import androidx.compose.animation.core.tween
import androidx.compose.foundation.Canvas
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableFloatStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.geometry.CornerRadius
import androidx.compose.ui.geometry.Rect
import androidx.compose.ui.geometry.RoundRect
import androidx.compose.ui.graphics.Path
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.unit.dp
import com.alefinot.dashboardpp.api.EspApi
import com.alefinot.dashboardpp.api.EspApiLane
import com.alefinot.dashboardpp.api.Snapshot
import com.alefinot.dashboardpp.trips.TripRuntime
import com.alefinot.dashboardpp.trips.TripRuntimeState
import com.alefinot.dashboardpp.ui.components.HudEnter
import com.alefinot.dashboardpp.ui.components.HudGauge
import com.alefinot.dashboardpp.ui.components.StatCell
import com.alefinot.dashboardpp.ui.theme.HudColors
import com.alefinot.dashboardpp.ui.theme.HudBrushes
import com.alefinot.dashboardpp.ui.theme.HudMotion
import com.alefinot.dashboardpp.ui.theme.HudRadius
import com.alefinot.dashboardpp.ui.theme.HudeHeading
import com.alefinot.dashboardpp.ui.theme.HudeSectionTitle
import com.alefinot.dashboardpp.ui.theme.HudeBody
import com.alefinot.dashboardpp.ui.theme.HudeCfgHelp
import com.alefinot.dashboardpp.ui.theme.HudeCfgNote
import com.alefinot.dashboardpp.ui.theme.hudCard
import kotlinx.coroutines.delay
import kotlinx.coroutines.isActive
import kotlin.math.PI
import kotlin.math.cos

/**
 * The breathing half of a live recording state. The page defines `pulseGlow`
 * (`src/webui.html:70` - `box-shadow: 0 0 12px rgba(0,229,255,.2)` at the ends of
 * the keyframe, `0 0 24px rgba(0,229,255,.5)` in the middle) and, on this reading
 * of the file, never attaches it to a selector: the WebUI has no recording
 * indicator to animate. The app has that state, so the chip borrows the page's own
 * keyframe instead of inventing a motion. The page sets no duration for it, because
 * nothing uses it; 2 s is the app's number, chosen so one breath spans two 1 Hz
 * samples - slow enough to read as a pulse and not as a blink.
 */
private const val CHIP_PULSE_MS = 2000L

/**
 * The breath is recomputed ~16 times a second instead of on every frame: a glow
 * that takes a second to reach its bright stop has nothing to show between those
 * steps, and a per-frame loop measured 48 repaints a second on the test tablet
 * (SM-T590) for as long as the recorder ran.
 */
private const val CHIP_PULSE_STEP_MS = 60L

/**
 * The native live view: the same data the WebUI renders, drawn natively so the
 * app can chart and log it (trip recording plan §5). It is read-only by design —
 * configuration stays in the Web UI, so there is only ever one writer of the
 * ESP's state and no second implementation of the config logic to drift.
 *
 * One poller only: when the recording service is running, its 1 Hz sample is
 * rendered from [TripRuntime] and this screen opens no connection at all. It
 * polls itself only while the service is not polling, and never while the Web UI
 * tab is on screen (that WebView's own live fetch becomes the single client).
 */
@Composable
fun LiveTab(ip: String, active: Boolean) {
    var state by remember { mutableStateOf(TripRuntime.state.value) }

    LaunchedEffect(active) {
        if (active) {
            TripRuntime.state.collect { state = it }
        }
    }

    LaunchedEffect(ip, active, state.polling) {
        if (!active || state.polling) return@LaunchedEffect
        while (true) {
            if (EspApiLane.webUiVisible) {
                delay(1000)
                continue
            }
            val snapshot = EspApi.snapshot(ip)
            if (snapshot != null) {
                TripRuntime.update {
                    it.copy(
                        ip = ip,
                        snapshot = snapshot,
                        live = null,
                        lastOkMs = System.currentTimeMillis(),
                        lastError = null,
                    )
                }
            } else {
                val live = EspApi.live(ip)
                TripRuntime.update {
                    it.copy(
                        ip = ip,
                        live = live ?: it.live,
                        lastOkMs = if (live != null) System.currentTimeMillis() else it.lastOkMs,
                        lastError = if (live == null) "no-answer" else null,
                    )
                }
            }
            delay(1000)
        }
    }

    Column(
        Modifier.fillMaxSize()
            // `html { background-color: #000000 }`, `body { background-color:
            // --bg-base }`: the page's ground is flat black, with no gradient and
            // no glow anywhere. Nothing is added here - the cards' translucent
            // fills do the modelling, exactly as they do on the page.
            .background(HudColors.Bg)
            .verticalScroll(rememberScrollState())
            .padding(16.dp),
    ) {
        HudEnter(modifier = Modifier.fillMaxWidth()) {
            Row(Modifier.fillMaxWidth(), verticalAlignment = Alignment.CenterVertically) {
                Text("LIVE", style = HudeHeading().copy(brush = HudBrushes.title))
                Spacer(Modifier.width(12.dp))
                StatusChip(state, active)
            }
        }
        Spacer(Modifier.height(16.dp))

        val snapshot = state.snapshot
        if (snapshot != null) {
            // `.card`, unmodified: rgba(22,30,46,.6), a 5% hairline, 8px corners,
            // no shadow. The page's cards carry no gradient wash of their own, so
            // the cyan on this screen comes from the gauge itself.
            // `pageFadeIn` per panel, offset by one stagger step: the readout
            // arrives just after the heading, the figures just after that, and the
            // whole screen has settled inside a single page entrance.
            HudEnter(
                modifier = Modifier.fillMaxWidth(),
                delayMs = HudMotion.STAGGER_MS,
            ) {
                Box(
                    Modifier.fillMaxWidth()
                        .hudCard()
                        .padding(horizontal = 14.dp, vertical = 12.dp),
                    contentAlignment = Alignment.Center,
                ) {
                    HudGauge(
                        value = snapshot.spd,
                        fullScale = speedScale(snapshot),
                        label = "SPEED",
                        unit = "km/h · ${sourceLabel(snapshot.src)}",
                        modifier = Modifier.fillMaxWidth().height(200.dp),
                        accent = if (snapshot.spd > 0.0) HudColors.Cyan else HudColors.CyanDim,
                    )
                }
            }
            Spacer(Modifier.height(16.dp))
        }

        HudEnter(modifier = Modifier.fillMaxWidth(), delayMs = 2 * HudMotion.STAGGER_MS) {
            Column(Modifier.fillMaxWidth()) {
                if (snapshot == null && state.live != null) {
                    // Firmware older than V1.4.6 answers /api/live but has no
                    // /api/snapshot: show what it does publish, and say what is missing.
                    FallbackNotice()
                    Spacer(Modifier.height(12.dp))
                    LiveReadingsCells(state)
                } else if (snapshot != null) {
                    SnapshotCells(snapshot)
                } else {
                    Text(
                        if (state.lastError == EspApi.FIRMWARE_UPDATE_REQUIRED) {
                            "No snapshot endpoint on this device."
                        } else {
                            "Waiting for the dashboard…"
                        },
                        color = HudColors.TextMuted,
                        style = HudeCfgHelp(),
                    )
                }
            }
        }
        Spacer(Modifier.height(24.dp))
    }
}

/** Gauge scale: quiet drive gets 60, highway gets 180, anything past that gets 240. */
private fun speedScale(s: Snapshot): Double = when {
    s.max > 200.0 || s.spd > 200.0 -> 240.0
    s.max > 120.0 || s.spd > 120.0 -> 180.0
    s.max > 60.0 || s.spd > 60.0 -> 120.0
    else -> 60.0
}

private fun sourceLabel(src: Int): String = when (src) {
    0 -> "HALL"
    1 -> "GPS"
    2 -> "FUSED"
    else -> "SRC$src"
}

private fun fuelStateLabel(st: Int): String = when (st) {
    0 -> "OFF"
    1 -> "OK"
    2 -> "OPEN"
    3 -> "SHORT"
    else -> "ST$st"
}

private fun fmt(value: Double, places: Int = 1): String =
    String.format(java.util.Locale.US, "%.${places}f", value)

/**
 * The run state as one of the page's pills (`.ip-banner` radius) carrying a
 * `.msg-success` / `.msg-error` tint: the accent at 8% behind the glass, a 30%
 * accent hairline, accent text.
 */
@Composable
private fun StatusChip(state: TripRuntimeState, active: Boolean) {
    val (label, target) = when {
        state.recording -> "RECORDING" to HudColors.Green
        state.lastError == EspApi.FIRMWARE_UPDATE_REQUIRED -> "NEEDS UPDATE" to HudColors.Amber
        state.pausedForWebUi -> "PAUSED · WEB UI" to HudColors.Amber
        // Nothing has come back yet on this connection - that is not the same
        // claim as "the dashboard stopped answering", which is what NO DATA means.
        state.lastOkMs == 0L -> "CONNECTING" to HudColors.TextDim
        state.polling && state.stale -> "NO DATA" to HudColors.Red
        state.polling -> "LOGGING IDLE" to HudColors.Cyan
        else -> "NOT RECORDING" to HudColors.TextDim
    }
    // The state changes without the user touching anything (the recorder starts,
    // the page takes the connection, data goes stale), so both halves of the pill
    // move: the colour over the page's 0.2s control easing, the word crossfading
    // into its place.
    val color by animateColorAsState(
        target,
        tween(HudMotion.SWITCH_MS, easing = HudMotion.Ease),
        label = "chipColor",
    )
    // `pulseGlow`, held by the pill while RECORDING. The fraction is a state object
    // read *inside* the draw lambda below: a step of the breath invalidates the
    // chip's own layer, not the Live screen behind it. When the state stops
    // recording the breath is left at its dim stop, and the pill settles back to the
    // plain tint the CSS gives a `.msg-*` pill.
    val glow = remember { mutableFloatStateOf(0f) }
    LaunchedEffect(state.recording, active) {
        // `active` is not decoration: the page is now composed whether or not the
        // user is looking at it, and a breath that runs off-screen invalidates that
        // chip's layer ~30 times a second for nobody. It restarts from the dim stop
        // when the page comes back, which is where a breath starting from rest
        // belongs anyway.
        if (!state.recording || !active) {
            glow.floatValue = 0f
            return@LaunchedEffect
        }
        val start = android.os.SystemClock.elapsedRealtime()
        while (isActive) {
            val p = (android.os.SystemClock.elapsedRealtime() - start) % CHIP_PULSE_MS
            // The keyframe sits at one stop for 0% and 100% and the other for 50%,
            // which is a cosine and not a triangle: it eases at both ends, so there
            // is no snap at the bottom of the breath.
            glow.floatValue = 0.5f - 0.5f * cos(2f * PI.toFloat() * p / CHIP_PULSE_MS)
            delay(CHIP_PULSE_STEP_MS)
        }
    }
    Box(
        Modifier
            // `.ip-banner` for the shape (--bg-card pill, hairline, 4x12 padding),
            // `.msg-success`/`.msg-error` for the state: that colour as a 30%
            // hairline and an 8% wash, and in the label. The padding sits on the
            // label rather than the box, so the layer that draws the breath spans
            // the whole pill and its ring lands on the hairline.
            .hudCard(
                shape = HudRadius.PillShape,
                fill = HudColors.Panel,
                borderColor = color.copy(alpha = 0.3f),
            )
            .background(color.copy(alpha = 0.08f), HudRadius.PillShape),
    ) {
        // `#ip-banner` is 13px with no weight of its own, and `#msg`'s variants put
        // the colour in the text: same here.
        Crossfade(label, animationSpec = tween(HudMotion.SWITCH_MS), label = "chipText") {
            Text(
                it,
                color = color,
                style = HudeBody(),
                modifier = Modifier.padding(horizontal = 12.dp, vertical = 4.dp),
            )
        }
        Canvas(Modifier.matchParentSize()) {
            val g = glow.floatValue
            if (g > 0.01f) {
                val h = size.height
                val path = Path()
                path.addRoundRect(
                    RoundRect(Rect(0f, 0f, size.width, h), CornerRadius(h / 2f, h / 2f)),
                )
                // The page's shadow is a blur outside the pill, and this layer has no
                // blur to give it (blurring what is behind needs API 31, the app
                // supports 26), so the halo is three strokes growing inward from the
                // hairline - a stroke is centred on the path, so the outer half of
                // each is cut off by the layer. Their alphas peak under the page's
                // bright stop (.5) so the stack never reads heavier than the single
                // shadow it stands in for, and all three ride the same cosine the
                // keyframe does.
                drawPath(
                    path,
                    color.copy(alpha = 0.05f + 0.07f * g),
                    style = Stroke(width = 7.dp.toPx()),
                )
                drawPath(
                    path,
                    color.copy(alpha = 0.07f + 0.11f * g),
                    style = Stroke(width = 3.5.dp.toPx()),
                )
                drawPath(
                    path,
                    color.copy(alpha = 0.18f + 0.30f * g),
                    style = Stroke(width = 1.5.dp.toPx()),
                )
            }
        }
    }
}

@Composable
private fun FallbackNotice() {
    Box(
        Modifier.fillMaxWidth()
            // `#msg` with `.msg-warning`: 8px corners, the status strip's own fill
            // under an amber wash at 8%, amber hairline at 30%, 12x6 padding. No
            // shadow - `#msg` declares none.
            .hudCard(
                fill = HudColors.Msg,
                borderColor = HudColors.Amber.copy(alpha = 0.3f),
            )
            .background(HudColors.Amber.copy(alpha = 0.08f), HudRadius.CardShape)
            .padding(horizontal = 12.dp, vertical = 8.dp),
    ) {
        Column {
            Text("LIMITED DATA", color = HudColors.Amber, style = HudeSectionTitle())
            Spacer(Modifier.height(4.dp))
            Text(
                "This device has no /api/snapshot endpoint, so speed, GNSS and trip " +
                    "figures are not published and this drive cannot be recorded. " +
                    "Update the firmware to V1.4.6 or newer.",
                color = HudColors.TextMuted,
                style = HudeCfgNote(),
            )
        }
    }
}

@Composable
private fun RowOf(first: @Composable () -> Unit, second: @Composable () -> Unit) {
    Row(Modifier.fillMaxWidth()) {
        Box(Modifier.weight(1f)) { first() }
        Spacer(Modifier.width(8.dp))
        Box(Modifier.weight(1f)) { second() }
    }
    Spacer(Modifier.height(8.dp))
}

@Composable
private fun SnapshotCells(s: Snapshot) {
    RowOf(
        { StatCell("TRIP", fmt(s.trpKm, 2), "km", modifier = Modifier.fillMaxWidth()) },
        { StatCell("TRIP FUEL", fmt(s.trpL, 2), "L", modifier = Modifier.fillMaxWidth()) },
    )
    RowOf(
        { StatCell("INST ECO", fmt(s.ikml, 1), "km/L", modifier = Modifier.fillMaxWidth()) },
        { StatCell("AVG ECO", fmt(s.akml, 1), "km/L", modifier = Modifier.fillMaxWidth()) },
    )
    RowOf(
        { StatCell("MAX", fmt(s.max, 1), "km/h", modifier = Modifier.fillMaxWidth()) },
        { StatCell("AVG", fmt(s.avg, 1), "km/h", modifier = Modifier.fillMaxWidth()) },
    )
    RowOf(
        { StatCell("ODO", fmt(s.odo, 1), "km", modifier = Modifier.fillMaxWidth()) },
        {
            StatCell(
                "FUEL", "${s.fuelPct}%",
                unit = "${fmt(s.fuelL, 2)} L",
                accent = if (s.fuelSt == 1) HudColors.Cyan else HudColors.Amber,
                modifier = Modifier.fillMaxWidth(),
            )
        },
    )
    RowOf(
        {
            StatCell(
                "FUEL INPUT", fuelStateLabel(s.fuelSt),
                accent = when (s.fuelSt) { 1 -> HudColors.Green; 0 -> HudColors.TextDim; else -> HudColors.Red },
                modifier = Modifier.fillMaxWidth(),
            )
        },
        {
            StatCell(
                "BATTERY", fmt(s.volts, 2), "V",
                accent = if (s.volts in 11.5..15.0) HudColors.Cyan else HudColors.Amber,
                modifier = Modifier.fillMaxWidth(),
            )
        },
    )
    RowOf(
        {
            StatCell(
                "COOLANT", fmt(s.temp, 1), "°C",
                accent = if (s.temp > 105.0) HudColors.Red else HudColors.Cyan,
                modifier = Modifier.fillMaxWidth(),
            )
        },
        // "amb" is the ambient LIGHT sensor (raw value, no unit) — the Web UI
        // shows it as a bare reading, so this does too. Not a temperature.
        { StatCell("LIGHT", s.amb.toString(), modifier = Modifier.fillMaxWidth()) },
    )
    RowOf(
        {
            StatCell(
                "SATS", s.sats.toString(),
                accent = if (s.fix == 1) HudColors.Green else HudColors.Amber,
                modifier = Modifier.fillMaxWidth(),
            )
        },
        {
            StatCell(
                "HDOP", if (s.hdop >= 0) fmt(s.hdop, 1) else "—",
                modifier = Modifier.fillMaxWidth(), dim = s.hdop < 0,
            )
        },
    )
    RowOf(
        {
            StatCell(
                "ALTITUDE", if (s.alt >= 0) fmt(s.alt, 0) else "—", "m",
                modifier = Modifier.fillMaxWidth(), dim = s.alt < 0,
            )
        },
        {
            StatCell(
                "TIMER", if (s.tmrSt == 0) "READY" else fmt(s.tmrS, 2) + " s",
                accent = when (s.tmrSt) { 1 -> HudColors.Amber; 2 -> HudColors.Green; else -> HudColors.TextDim },
                modifier = Modifier.fillMaxWidth(),
            )
        },
    )
    StatCell(
        "POSITION",
        if (s.hasPosition) "${fmt(s.lat, 5)}, ${fmt(s.lon, 5)}" else "NO FIX",
        accent = if (s.hasPosition) HudColors.Green else HudColors.Amber,
        modifier = Modifier.fillMaxWidth(),
    )
    Spacer(Modifier.height(8.dp))
    RowOf(
        { StatCell("DISPLAY", fmt(s.fps, 0), "fps", modifier = Modifier.fillMaxWidth()) },
        {
            StatCell(
                "FREE HEAP", (s.heap / 1024L).toString(), "kB",
                modifier = Modifier.fillMaxWidth(),
            )
        },
    )
}

@Composable
private fun LiveReadingsCells(state: TripRuntimeState) {
    val live = state.live ?: return
    RowOf(
        { StatCell("ODO", fmt(live.odo, 1), "km", modifier = Modifier.fillMaxWidth()) },
        {
            StatCell(
                "FUEL INPUT", fuelStateLabel(live.fuelSt),
                modifier = Modifier.fillMaxWidth(),
            )
        },
    )
    RowOf(
        { StatCell("RAW", live.fuelRaw.toString(), "ADC", modifier = Modifier.fillMaxWidth()) },
        { StatCell("OHMS", fmt(live.fuelOhm, 1), "Ω", modifier = Modifier.fillMaxWidth()) },
    )
    RowOf(
        { StatCell("BATTERY", fmt(live.volts, 2), "V", modifier = Modifier.fillMaxWidth()) },
        { StatCell("COOLANT", fmt(live.temp, 1), "°C", modifier = Modifier.fillMaxWidth()) },
    )
    RowOf(
        { StatCell("LIGHT", live.ambient.toString(), modifier = Modifier.fillMaxWidth()) },
        { StatCell("SPEED", "—", modifier = Modifier.fillMaxWidth(), dim = true) },
    )
}
