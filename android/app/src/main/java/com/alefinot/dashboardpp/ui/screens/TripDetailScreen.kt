package com.alefinot.dashboardpp.ui.screens

import androidx.activity.compose.BackHandler
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
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.unit.dp
import com.alefinot.dashboardpp.trips.TripSample
import com.alefinot.dashboardpp.trips.TripSummary
import com.alefinot.dashboardpp.ui.components.HudButton
import com.alefinot.dashboardpp.ui.components.Sparkline
import com.alefinot.dashboardpp.ui.components.bucketMax
import com.alefinot.dashboardpp.ui.components.hudDuration
import com.alefinot.dashboardpp.ui.components.hudNum
import com.alefinot.dashboardpp.ui.components.StatCell
import com.alefinot.dashboardpp.ui.map.RouteMap
import com.alefinot.dashboardpp.ui.theme.HudColors
import com.alefinot.dashboardpp.ui.theme.HudeDisplaySmall
import com.alefinot.dashboardpp.ui.theme.HudeLabel
import com.alefinot.dashboardpp.ui.theme.HudeSmall
import com.alefinot.dashboardpp.ui.theme.HudMonoText
import com.alefinot.dashboardpp.viewmodel.TripDetail
import com.alefinot.dashboardpp.viewmodel.TripRow
import com.alefinot.dashboardpp.viewmodel.TripsViewModel
import org.osmdroid.util.GeoPoint
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale

/**
 * One recorded drive: headline figures, the route on a map, and the series
 * charts (trip recording plan §6). Charts are drawn by hand from the CSV — a
 * charting library would be another dependency and a worse fit for a fixed HUD
 * look — and every series is bucketed to a bounded point count so a 12 h trip
 * (43k samples) costs the same paint as a 5 minute one.
 */
@Composable
fun TripDetailScreen(row: TripRow, vm: TripsViewModel, onDismiss: () -> Unit) {
    val context = LocalContext.current
    val detail = remember(row.file.absolutePath) { vm.detail(row) }
    var confirmDelete by remember { mutableStateOf(false) }

    BackHandler { onDismiss() }

    if (detail == null) {
        Column(Modifier.fillMaxSize().background(HudColors.Bg).padding(16.dp)) {
            Text("TRIP UNREADABLE", color = HudColors.Red, style = HudeDisplaySmall)
            Spacer(Modifier.height(12.dp))
            Text(
                "The file could not be parsed. It may have been removed or " +
                    "corrupted; it is still on disk unless you delete it.",
                color = HudColors.TextDim,
                style = HudeSmall,
            )
            Spacer(Modifier.height(16.dp))
            HudButton("DELETE FILE") { vm.delete(row); onDismiss() }
            Spacer(Modifier.height(8.dp))
            HudButton("CLOSE", onClick = onDismiss)
        }
        return
    }

    val s = detail.summary
    val samples = detail.samples

    Column(
        Modifier.fillMaxSize().background(HudColors.Bg)
            .verticalScroll(rememberScrollState()).padding(16.dp),
    ) {
        Row(Modifier.fillMaxWidth(), verticalAlignment = Alignment.CenterVertically) {
            Text(
                SimpleDateFormat("yyyy-MM-dd HH:mm", Locale.US).format(Date(s.startMs)),
                color = HudColors.Cyan,
                style = HudeDisplaySmall,
            )
        }
        Spacer(Modifier.height(4.dp))
        Text(
            buildString {
                append(hudDuration(s.durationMs))
                append(" · ")
                append(String.format(Locale.US, "%.2f", s.distanceKm))
                append(" km · ")
                append(samples.size)
                append(" samples")
                if (s.gaps > 0) {
                    append(" · ")
                    append(s.gaps)
                    append(" gaps (")
                    append(s.gapSeconds)
                    append(" s lost)")
                }
            },
            color = HudColors.TextDim,
            style = HudeSmall,
        )
        Spacer(Modifier.height(16.dp))

        val points = remember(samples) {
            samples.filter { it.snapshot.hasPosition }
                .map { GeoPoint(it.snapshot.lat, it.snapshot.lon) }
        }
        if (points.size >= 2) {
            Box(Modifier.fillMaxWidth().height(260.dp).border(1.dp, HudColors.CyanDim)) {
                RouteMap(points, Modifier.fillMaxSize())
            }
        } else {
            Box(Modifier.fillMaxWidth().height(140.dp).border(1.dp, HudColors.CyanDim).background(HudColors.Panel).padding(12.dp)) {
                Text(
                    if (points.isEmpty()) {
                        "No positions in this trip — the GNSS never had a fix, so " +
                            "the route cannot be drawn."
                    } else {
                        "Only one position: the fix arrived once. Nothing to draw a route from."
                    },
                    color = HudColors.TextDim,
                    style = HudeSmall,
                )
            }
        }
        Spacer(Modifier.height(16.dp))

        ChartRow(
            title = "SPEED",
            unit = "km/h",
            values = bucketMax(samples.map { it.snapshot.spd }, CHART_POINTS),
            accent = HudColors.Cyan,
        )
        val altitude = samples.filter { it.snapshot.alt >= 0.0 }
        if (altitude.size >= 2) {
            ChartRow(
                title = "ALTITUDE",
                unit = "m",
                values = bucketMax(altitude.map { it.snapshot.alt }, CHART_POINTS),
                accent = HudColors.Amber,
            )
        }
        val eco = samples.filter { it.snapshot.ikml > 0.0 }
        if (eco.size >= 2) {
            ChartRow(
                title = "INSTANT ECONOMY",
                unit = "km/L",
                values = bucketMax(eco.map { it.snapshot.ikml }, CHART_POINTS),
                accent = HudColors.Green,
            )
        }
        Spacer(Modifier.height(8.dp))

        SummaryGrid(s)

        Spacer(Modifier.height(16.dp))
        HudButton("SHARE CSV") { context.startActivity(vm.shareCsv(row)) }
        Spacer(Modifier.height(8.dp))
        HudButton("SHARE GPX") { context.startActivity(vm.shareGpx(row)) }
        Spacer(Modifier.height(8.dp))
        if (!confirmDelete) {
            HudButton("DELETE THIS TRIP") { confirmDelete = true }
        } else {
            Row(Modifier.fillMaxWidth()) {
                Box(Modifier.weight(1f)) {
                    HudButton("CONFIRM DELETE") { vm.delete(row); onDismiss() }
                }
                Spacer(Modifier.width(8.dp))
                Box(Modifier.weight(1f)) {
                    HudButton("KEEP", onClick = { confirmDelete = false })
                }
            }
        }
        Spacer(Modifier.height(8.dp))
        HudButton("CLOSE", onClick = onDismiss)
        Spacer(Modifier.height(24.dp))
    }
}

/**
 * Point budget for the hand-drawn charts. A bucketed 600-point polyline already
 * exceeds the pixel width of the widest phone screen, so anything past this only
 * costs paint time; the bucketing keeps peaks rather than averaging them away.
 */
private const val CHART_POINTS = 600

@Composable
private fun ChartRow(title: String, unit: String, values: List<Double>, accent: Color) {
    Column(Modifier.fillMaxWidth().padding(vertical = 6.dp)) {
        Row(Modifier.fillMaxWidth(), verticalAlignment = Alignment.CenterVertically) {
            Text(title, color = HudColors.TextDim, style = HudeSmall)
            Spacer(Modifier.width(8.dp))
            Text(
                "peak ${hudNum(values.maxOrNull(), if (unit == "m") 0 else 1)} $unit",
                color = accent,
                style = HudMonoText,
            )
        }
        Spacer(Modifier.height(4.dp))
        Sparkline(values, Modifier.fillMaxWidth().height(72.dp), line = accent, fill = accent)
    }
}

@Composable
private fun GridRow(first: @Composable () -> Unit, second: @Composable () -> Unit) {
    Row(Modifier.fillMaxWidth()) {
        Box(Modifier.weight(1f)) { first() }
        Spacer(Modifier.width(8.dp))
        Box(Modifier.weight(1f)) { second() }
    }
    Spacer(Modifier.height(8.dp))
}

@Composable
private fun SummaryGrid(s: TripSummary) {
    Text("SUMMARY", color = HudColors.Cyan, style = HudeLabel)
    Spacer(Modifier.height(8.dp))
    GridRow(
        { StatCell("DISTANCE", hudNum(s.distanceKm, 2), "km", modifier = Modifier.fillMaxWidth()) },
        { StatCell("GNSS TRACK", hudNum(s.gnssKm, 2), "km", modifier = Modifier.fillMaxWidth(), dim = s.gnssKm <= 0.0) },
    )
    GridRow(
        { StatCell("ESP TRIP", hudNum(s.espTripKm, 2), "km", modifier = Modifier.fillMaxWidth()) },
        { StatCell("AVG SPEED", hudNum(s.avgSpeed, 1), "km/h", modifier = Modifier.fillMaxWidth()) },
    )
    GridRow(
        { StatCell("MAX SPEED", hudNum(s.maxSpeed, 1), "km/h", modifier = Modifier.fillMaxWidth()) },
        { StatCell("FUEL USED", hudNum(s.fuelUsedL, 2), "L", modifier = Modifier.fillMaxWidth()) },
    )
    GridRow(
        { StatCell("ECONOMY", hudNum(s.economyKml, 2), "km/L", modifier = Modifier.fillMaxWidth()) },
        {
            StatCell(
                "GNSS FIX", "${s.fixPct}%",
                accent = if (s.fixPct > 60) HudColors.Green else HudColors.Amber,
                modifier = Modifier.fillMaxWidth(),
            )
        },
    )
    GridRow(
        {
            StatCell(
                "BATTERY", "${hudNum(s.minVolts, 1)}–${hudNum(s.maxVolts, 1)}", "V",
                modifier = Modifier.fillMaxWidth(),
            )
        },
        {
            StatCell(
                "COOLANT", "${hudNum(s.minTemp, 0)}–${hudNum(s.maxTemp, 0)}", "°C",
                modifier = Modifier.fillMaxWidth(), dim = s.maxTemp <= 0.0,
            )
        },
    )
    GridRow(
        {
            StatCell(
                "LOG GAPS", s.gaps.toString(),
                unit = if (s.gapSeconds > 0) "${s.gapSeconds} s lost" else "",
                accent = if (s.gaps == 0) HudColors.Green else HudColors.Amber,
                modifier = Modifier.fillMaxWidth(),
            )
        },
        { StatCell("SAMPLES", s.samples.toString(), modifier = Modifier.fillMaxWidth()) },
    )
    if (s.gaps > 0) {
        Text(
            "Gaps are stretches longer than 10 s with no sample — the app was " +
                "closed, the Wi-Fi dropped, or the Web UI was open on the phone " +
                "and the logger stood down.",
            color = HudColors.TextDim,
            style = HudeSmall,
        )
        Spacer(Modifier.height(8.dp))
    }
}
