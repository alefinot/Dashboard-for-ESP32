package com.alefinot.dashboardpp.ui.screens

import androidx.compose.animation.core.LinearEasing
import androidx.compose.animation.core.animateFloatAsState
import androidx.compose.animation.core.tween
import androidx.compose.foundation.background
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
import androidx.compose.foundation.layout.offset
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.itemsIndexed
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.alpha
import androidx.compose.ui.unit.IntOffset
import androidx.compose.ui.unit.dp
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import com.alefinot.dashboardpp.trips.TripRuntime
import com.alefinot.dashboardpp.ui.components.hudBytes
import com.alefinot.dashboardpp.ui.components.LocalEnterActive
import com.alefinot.dashboardpp.ui.components.hudDuration
import com.alefinot.dashboardpp.ui.components.hudNum
import com.alefinot.dashboardpp.trips.TripRuntimeState
import com.alefinot.dashboardpp.ui.theme.HudColors
import com.alefinot.dashboardpp.ui.theme.HudBrushes
import com.alefinot.dashboardpp.ui.theme.HudMotion
import com.alefinot.dashboardpp.ui.theme.HudRadius
import com.alefinot.dashboardpp.ui.theme.hudCard
import com.alefinot.dashboardpp.ui.theme.HudeHeading
import com.alefinot.dashboardpp.ui.theme.HudeBody
import com.alefinot.dashboardpp.ui.theme.HudeCfgHelp
import com.alefinot.dashboardpp.ui.theme.HudeCfgLive
import com.alefinot.dashboardpp.ui.theme.HudeCfgNote
import com.alefinot.dashboardpp.ui.theme.HudePerfValue
import com.alefinot.dashboardpp.ui.theme.HudeSectionTitle
import com.alefinot.dashboardpp.ui.theme.HudeTable
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

    Column(
        Modifier.fillMaxSize()
            .background(HudColors.Bg)
            .padding(16.dp),
    ) {
        Row(Modifier.fillMaxWidth(), verticalAlignment = Alignment.CenterVertically) {
            // The page's `h2`: 14px, 700, letter-spacing 2, and its gradient text.
            Text("TRIPS", style = HudeHeading().copy(brush = HudBrushes.title))
            Spacer(Modifier.width(12.dp))
            Text(
                "${rows.size} · ${formatBytes(vm.storageBytes())}",
                color = HudColors.TextMuted,
                style = HudeCfgLive(),
            )
        }
        Spacer(Modifier.height(4.dp))
        Text(
            "One file per ESP power session · CSV in app storage",
            color = HudColors.TextDim,
            style = HudeCfgNote(),
        )
        Spacer(Modifier.height(12.dp))

        if (message != null) {
            Text(message.orEmpty(), color = HudColors.Amber, style = HudeCfgHelp())
            Spacer(Modifier.height(8.dp))
        }

        if (runtime.recording) {
            LiveTripRow(runtime)
            Spacer(Modifier.height(10.dp))
        }

        if (rows.isEmpty() && !runtime.recording) {
            Spacer(Modifier.height(24.dp))
            Text("No trips recorded yet.", color = HudColors.TextPrimary, style = HudeBody())
            Spacer(Modifier.height(8.dp))
            Text(
                "Drives are logged automatically while the app is open and the " +
                    "dashboard answers /api/snapshot.",
                color = HudColors.TextDim,
                style = HudeCfgNote(),
            )
        }

        // The list of drives, with the page's own staggered reveal.
        TripList(rows, onOpen)
    }
}

/**
 * The list of drives, revealed the way the page fades its body in (`pageFadeIn` on
 * the body) rather than row by row: a LazyColumn throws rows away and re-creates them
 * as they scroll, so a row-local entrance would replay on every scroll back - the
 * cost the design brief rules out. One ramp drives the whole list instead, each row
 * taking its slice of it by position, so a row scrolling back into view finds the ramp
 * already at 1 and costs nothing.
 *
 * The ramp belongs to a visit, not to composition. The page is kept composed between
 * visits so that arriving is a paint rather than a rebuild, so the ramp is re-armed
 * when the page comes into view ([LocalEnterActive]) - and a scroll never re-arms it,
 * because it never leaves the view.
 *
 * With motion off the list starts revealed: the ramp would otherwise animate from
 * nothing, which is motion the user asked not to have.
 */
@Composable
private fun TripList(rows: List<TripRow>, onOpen: (TripRow) -> Unit) {
    val inView = LocalEnterActive.current
    var visits by remember { mutableIntStateOf(0) }
    LaunchedEffect(inView) {
        if (inView) visits++
    }
    // The visit counter keys the ramp: a new visit starts it back at nothing, which is
    // what a freshly composed page used to do by itself. Starting at zero visits, an
    // off-screen page sits unrevealed and animates nothing until the user gets here.
    var revealed by remember(visits) { mutableStateOf(visits > 0 && !HudMotion.enabled) }
    LaunchedEffect(visits) {
        if (visits > 0 && rows.isNotEmpty()) revealed = true
    }
    val staggered = minOf(rows.size, 8)
    val rampMs = (HudMotion.ENTRANCE_MS + HudMotion.STAGGER_MS * maxOf(staggered - 1, 0)).toFloat()
    val ramp by animateFloatAsState(
        if (revealed) 1f else 0f,
        tween(rampMs.toInt(), easing = LinearEasing),
        label = "tripsReveal",
    )
    LazyColumn(Modifier.fillMaxSize(), verticalArrangement = Arrangement.spacedBy(12.dp)) {
        itemsIndexed(rows, key = { _, row -> row.file.absolutePath }) { i, row ->
            val start = (minOf(i, staggered - 1) * HudMotion.STAGGER_MS)
            TripCard(
                row,
                onOpen,
                reveal = ((ramp * rampMs - start) / HudMotion.ENTRANCE_MS).coerceIn(0f, 1f),
            )
        }
    }
}

/** The open trip, straight from the recorder's running totals. */
@Composable
private fun LiveTripRow(state: TripRuntimeState) {
    val snap = state.snapshot
    val summary = state.summary
    // No page rule for a live recording row, so it borrows the page's status
    // language: `#msg` plus `.msg-success` -- emerald hairline at 30%, emerald at
    // 8% as the wash, 8px radius.
    Box(
        Modifier.fillMaxWidth()
            // `#msg` + `.msg-success`: 8px corners, the status strip's own fill
            // under an emerald wash at 8%, emerald hairline at 30%. No shadow.
            .hudCard(
                fill = HudColors.Msg,
                borderColor = HudColors.Green.copy(alpha = 0.3f),
            )
            .background(HudColors.Green.copy(alpha = 0.08f), HudRadius.CardShape)
            .padding(horizontal = 14.dp, vertical = 12.dp),
    ) {
        Column {
            Row(Modifier.fillMaxWidth(), verticalAlignment = Alignment.CenterVertically) {
                // `#msg`'s own 13px, at the weight the page gives a line it wants
                // read first (`.cfg-warn`, `.card h4`).
                Text("RECORDING", color = HudColors.Green, style = HudeSectionTitle(0f))
                Spacer(Modifier.width(10.dp))
                Text(
                    summary?.let { formatDuration(it.durationMs) } ?: "0:00:00",
                    color = HudColors.TextPrimary,
                    style = HudePerfValue(),
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
                color = HudColors.TextMuted,
                style = HudeCfgHelp(),
            )
        }
    }
}

@Composable
private fun TripCard(row: TripRow, onOpen: (TripRow) -> Unit, reveal: Float = 1f) {
    Box(
        Modifier.fillMaxWidth()
            // The list's shared ramp, in alpha and in an 8px rise -- `pageFadeIn`
            // lifts by exactly that much.
            .alpha(reveal)
            .offset { IntOffset(0, ((1f - reveal) * 8f).dp.roundToPx()) }
            .hudCard()
            .clickable { onOpen(row) }
            .padding(horizontal = 14.dp, vertical = 12.dp),
    ) {
        Column {
            Text(
                // A trip row is tabular data, so it takes the page's `table` rule:
                // 12px monospace, 0.5px tracking.
                SimpleDateFormat("yyyy-MM-dd HH:mm", Locale.US).format(Date(row.startMs)),
                color = HudColors.TextPrimary,
                style = HudeTable(),
            )
            Spacer(Modifier.height(6.dp))
            val s = row.summary
            if (s == null) {
                Text("…", color = HudColors.TextMuted, style = HudeCfgHelp())
            } else {
                Text(
                    "${fmtOrDash(s.distanceKm, 2)} km · ${formatDuration(s.durationMs)}",
                    color = HudColors.Cyan,
                    style = HudeTable(),
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
                    color = HudColors.TextMuted,
                    style = HudeCfgHelp(),
                )
            }
        }
    }
}

private fun formatBytes(bytes: Long): String = hudBytes(bytes)

private fun formatDuration(ms: Long): String = hudDuration(ms)

private fun fmtOrDash(value: Double?, places: Int): String = hudNum(value, places)
