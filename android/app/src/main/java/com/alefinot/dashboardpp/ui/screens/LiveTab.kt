package com.alefinot.dashboardpp.ui.screens

import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.layout.Arrangement
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
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.unit.dp
import com.alefinot.dashboardpp.api.EspApi
import com.alefinot.dashboardpp.api.EspApiLane
import com.alefinot.dashboardpp.api.Snapshot
import com.alefinot.dashboardpp.trips.TripRuntime
import com.alefinot.dashboardpp.trips.TripRuntimeState
import com.alefinot.dashboardpp.ui.components.HudGauge
import com.alefinot.dashboardpp.ui.components.StatCell
import com.alefinot.dashboardpp.ui.theme.HudColors
import com.alefinot.dashboardpp.ui.theme.HudeDisplaySmall
import com.alefinot.dashboardpp.ui.theme.HudeSmall
import kotlinx.coroutines.delay

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
        Modifier.fillMaxSize().background(HudColors.Bg)
            .verticalScroll(rememberScrollState())
            .padding(16.dp),
    ) {
        Row(Modifier.fillMaxWidth(), verticalAlignment = Alignment.CenterVertically) {
            Text("LIVE", color = HudColors.Cyan, style = HudeDisplaySmall)
            Spacer(Modifier.width(12.dp))
            StatusChip(state)
        }
        Spacer(Modifier.height(16.dp))

        val snapshot = state.snapshot
        if (snapshot != null) {
            HudGauge(
                value = snapshot.spd,
                fullScale = speedScale(snapshot),
                label = "SPEED",
                unit = "km/h · ${sourceLabel(snapshot.src)}",
                modifier = Modifier.fillMaxWidth().height(190.dp),
                accent = if (snapshot.spd > 0.0) HudColors.Cyan else HudColors.CyanDim,
            )
            Spacer(Modifier.height(16.dp))
        }

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
                color = HudColors.TextDim,
                style = HudeSmall,
            )
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

@Composable
private fun StatusChip(state: TripRuntimeState) {
    val (label, color) = when {
        state.recording -> "RECORDING" to HudColors.Green
        state.lastError == EspApi.FIRMWARE_UPDATE_REQUIRED -> "NEEDS UPDATE" to HudColors.Amber
        state.pausedForWebUi -> "PAUSED · WEB UI" to HudColors.Amber
        state.polling && state.stale -> "NO DATA" to HudColors.Red
        state.polling -> "LOGGING IDLE" to HudColors.Cyan
        else -> "NOT RECORDING" to HudColors.TextDim
    }
    Box(
        Modifier.border(1.dp, color).background(HudColors.Panel)
            .padding(horizontal = 8.dp, vertical = 3.dp),
    ) {
        Text(label, color = color, style = HudeSmall)
    }
}

@Composable
private fun FallbackNotice() {
    Box(
        Modifier.fillMaxWidth().border(1.dp, HudColors.Amber).background(HudColors.Panel)
            .padding(10.dp),
    ) {
        Column {
            Text("LIMITED DATA", color = HudColors.Amber, style = HudeSmall)
            Spacer(Modifier.height(4.dp))
            Text(
                "This device has no /api/snapshot endpoint, so speed, GNSS and trip " +
                    "figures are not published and this drive cannot be recorded. " +
                    "Update the firmware to V1.4.6 or newer.",
                color = HudColors.TextDim,
                style = HudeSmall,
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
