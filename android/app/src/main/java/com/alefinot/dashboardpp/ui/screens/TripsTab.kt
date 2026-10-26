package com.alefinot.dashboardpp.ui.screens

import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
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
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.unit.dp
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import com.alefinot.dashboardpp.trips.TripRuntime
import com.alefinot.dashboardpp.ui.components.hudBytes
import com.alefinot.dashboardpp.ui.components.hudDuration
import com.alefinot.dashboardpp.ui.components.hudNum
import com.alefinot.dashboardpp.trips.TripRuntimeState
import com.alefinot.dashboardpp.ui.theme.HudColors
import com.alefinot.dashboardpp.ui.theme.HudeDisplaySmall
import com.alefinot.dashboardpp.ui.theme.HudeLabel
import com.alefinot.dashboardpp.ui.theme.HudeSmall
import com.alefinot.dashboardpp.ui.theme.HudMonoText
import com.alefinot.dashboardpp.viewmodel.TripRow
import com.alefinot.dashboardpp.viewmodel.TripsViewModel
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale

/**
 * Recorded drives (trip recording plan §5): newest first, one card per power
 * session. The trip being written today sits at the top as a live row — never
 * deletable, and it shows the running numbers the recorder holds in RAM rather
 * than re-reading the file it is appending to.
 */
@Composable
fun TripsTab(vm: TripsViewModel, onOpen: (TripRow) -> Unit) {
    val rows by vm.rows.collectAsStateWithLifecycle()
    val runtime by TripRuntime.state.collectAsStateWithLifecycle()
    val message by vm.message.collectAsStateWithLifecycle()
    var refreshKey by remember { mutableStateOf(0) }

    LaunchedEffect(refreshKey, runtime.recording) {
        if (!runtime.recording) vm.refresh() else {
            // While recording, the sealed trips still need their summaries; the
            // open file is shown from RAM instead.
            vm.refresh()
        }
    }

    Column(Modifier.fillMaxSize().background(HudColors.Bg).padding(16.dp)) {
        Row(Modifier.fillMaxWidth(), verticalAlignment = Alignment.CenterVertically) {
            Text("TRIPS", color = HudColors.Cyan, style = HudeDisplaySmall)
            Spacer(Modifier.width(12.dp))
            Text(
                "${rows.size} · ${formatBytes(vm.storageBytes())}",
                color = HudColors.TextDim,
                style = HudeSmall,
            )
        }
        Spacer(Modifier.height(4.dp))
        Text(
            "One file per ESP power session · CSV in app storage",
            color = HudColors.TextDim,
            style = HudeSmall,
        )
        Spacer(Modifier.height(12.dp))

        if (message != null) {
            Text(message.orEmpty(), color = HudColors.Amber, style = HudeSmall)
            Spacer(Modifier.height(8.dp))
        }

        if (runtime.recording) {
            LiveTripRow(runtime)
            Spacer(Modifier.height(10.dp))
        }

        if (rows.isEmpty() && !runtime.recording) {
            Spacer(Modifier.height(24.dp))
            Text("No trips recorded yet.", color = HudColors.TextDim, style = HudeLabel)
            Spacer(Modifier.height(8.dp))
            Text(
                "Drives are logged automatically while the app is open and the " +
                    "dashboard answers /api/snapshot.",
                color = HudColors.TextDim,
                style = HudeSmall,
            )
        }

        LazyColumn(Modifier.fillMaxSize(), verticalArrangement = Arrangement.spacedBy(8.dp)) {
            items(rows, key = { it.file.absolutePath }) { row ->
                TripCard(row, onOpen)
            }
        }
    }
}

/** The open trip, straight from the recorder's running totals. */
@Composable
private fun LiveTripRow(state: TripRuntimeState) {
    val snap = state.snapshot
    val summary = state.summary
    Box(
        Modifier.fillMaxWidth().border(1.dp, HudColors.Green).background(HudColors.Panel)
            .padding(12.dp),
    ) {
        Column {
            Row(Modifier.fillMaxWidth(), verticalAlignment = Alignment.CenterVertically) {
                Text("RECORDING", color = HudColors.Green, style = HudeLabel)
                Spacer(Modifier.width(10.dp))
                Text(
                    summary?.let { formatDuration(it.durationMs) } ?: "0:00:00",
                    color = HudColors.TextPrimary,
                    style = HudMonoText,
                )
            }
            Spacer(Modifier.height(6.dp))
            Text(
                buildString {
                    append(fmtOrDash(summary?.distanceKm, 2))
                    append(" km · ")
                    append(snap?.let { it.spd.toInt().toString() } ?: "—")
                    append(" km/h · ")
                    append(fmtOrDash(summary?.fuelUsedL, 2))
                    append(" L")
                },
                color = HudColors.TextDim,
                style = HudeSmall,
            )
        }
    }
}

@Composable
private fun TripCard(row: TripRow, onOpen: (TripRow) -> Unit) {
    Box(
        Modifier.fillMaxWidth().border(1.dp, HudColors.CyanDim).background(HudColors.Panel)
            .clickable { onOpen(row) }.padding(12.dp),
    ) {
        Column {
            Text(
                SimpleDateFormat("yyyy-MM-dd HH:mm", Locale.US).format(Date(row.startMs)),
                color = HudColors.TextPrimary,
                style = HudeLabel,
            )
            Spacer(Modifier.height(6.dp))
            val s = row.summary
            if (s == null) {
                Text("…", color = HudColors.TextDim, style = HudeSmall)
            } else {
                Text(
                    "${fmtOrDash(s.distanceKm, 2)} km · ${formatDuration(s.durationMs)}",
                    color = HudColors.Cyan,
                    style = HudMonoText,
                )
                Spacer(Modifier.height(4.dp))
                Text(
                    buildString {
                        append("max ").append(fmtOrDash(s.maxSpeed, 0)).append(" km/h")
                        append(" · ").append(fmtOrDash(s.fuelUsedL, 2)).append(" L")
                        append(" · ").append(s.fixPct).append("% fix")
                        if (s.gaps > 0) {
                            append(" · ").append(s.gaps).append(" gaps")
                        }
                    },
                    color = HudColors.TextDim,
                    style = HudeSmall,
                )
            }
        }
    }
}

private fun formatBytes(bytes: Long): String = hudBytes(bytes)

private fun formatDuration(ms: Long): String = hudDuration(ms)

private fun fmtOrDash(value: Double?, places: Int): String = hudNum(value, places)
