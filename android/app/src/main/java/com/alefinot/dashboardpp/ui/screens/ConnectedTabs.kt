package com.alefinot.dashboardpp.ui.screens

import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.padding
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import androidx.compose.ui.unit.dp
import com.alefinot.dashboardpp.api.EspApiLane
import com.alefinot.dashboardpp.service.TripLogService
import com.alefinot.dashboardpp.trips.RecorderSettings
import com.alefinot.dashboardpp.trips.TripsEnabled
import com.alefinot.dashboardpp.viewmodel.ConnectionUiState
import com.alefinot.dashboardpp.viewmodel.ConnectionViewModel
import com.alefinot.dashboardpp.viewmodel.TripRow
import com.alefinot.dashboardpp.viewmodel.TripsViewModel
import com.alefinot.dashboardpp.ui.theme.HudColors
import com.alefinot.dashboardpp.ui.theme.HudeLabel

/**
 * The four modes of a connected dashboard: native live view, the ESP's own
 * configuration (its Web UI, labelled for what it configures), recorded trips,
 * and this app's own settings.
 */
enum class AppTab { LIVE, WEB, TRIPS, SETTINGS }

/**
 * The connected shell. Live, Trips and Settings are native (chartable,
 * loggable); the ESP's configuration stays a WebView because it already
 * implements configuration, and duplicating it would mean a second
 * implementation to keep in sync (plan §5).
 *
 * Settings is a tab, not an overlay behind a gear: with four modes a hidden
 * entry point costs more than the row it adds, and a tab cannot be missed.
 *
 * The recording service is started here for the lifetime of the connection —
 * not the lifetime of a screen — so moving between tabs, or turning the screen
 * off in the car, keeps the log running.
 */
@Composable
fun ConnectedTabs(
    conn: ConnectionUiState.Connected,
    connection: ConnectionViewModel,
    trips: TripsViewModel,
) {
    val context = LocalContext.current
    val activity = context as? android.app.Activity
    var tab by remember { mutableStateOf(AppTab.LIVE) }
    var openTrip by remember { mutableStateOf<TripRow?>(null) }

    val tripsAvailable = TripsEnabled.forFirmware(conn.version)

    // The user's recording switch, collected here so starting and stopping the
    // service follows it immediately (the service checks it again on its side).
    val settings = remember { RecorderSettings(context) }
    val recordingEnabled by settings.recordingEnabled
        .collectAsStateWithLifecycle(initialValue = true)

    LaunchedEffect(conn.ip, tripsAvailable, recordingEnabled) {
        if (tripsAvailable && recordingEnabled) {
            TripLogService.start(context, conn.ip)
        } else {
            TripLogService.stop(context)
            if (tab == AppTab.TRIPS) tab = AppTab.LIVE
        }
    }

    // The Web UI fetches /api/live itself; while it is on screen it is the only
    // HTTP client and the recorder stands down (plan §4.3).
    LaunchedEffect(tab) {
        EspApiLane.webUiVisible = tab == AppTab.WEB
    }
    DisposableEffect(Unit) {
        onDispose { EspApiLane.webUiVisible = false }
    }

    Column(Modifier.fillMaxSize().background(HudColors.Bg)) {
        Row(Modifier.padding(horizontal = 12.dp, vertical = 8.dp)) {
            TabButton("LIVE", tab == AppTab.LIVE) { tab = AppTab.LIVE }
            // "ESP SETTINGS" says whose settings these are — the ESP's own Web
            // UI — which is the distinction the app needs, not "Web UI".
            TabButton("ESP SETTINGS", tab == AppTab.WEB) { tab = AppTab.WEB }
            TabButton(
                label = if (tripsAvailable) "TRIPS" else "TRIPS ✕",
                selected = tab == AppTab.TRIPS,
                enabled = tripsAvailable,
            ) { tab = AppTab.TRIPS }
            TabButton("APP SETTINGS", tab == AppTab.SETTINGS) { tab = AppTab.SETTINGS }
        }

        when (tab) {
            AppTab.LIVE -> LiveTab(ip = conn.ip, active = true)
            AppTab.WEB -> {
                if (activity != null) {
                    DashboardWebScreen(activity = activity, vm = connection, ip = conn.ip)
                }
            }
            AppTab.TRIPS -> TripsTab(trips) { row -> openTrip = row }
            AppTab.SETTINGS -> SettingsScreen(
                vm = connection,
                ip = conn.ip,
                version = conn.version,
                trips = trips,
                tripsAvailable = tripsAvailable,
            )
        }
    }

    openTrip?.let { row ->
        Box(Modifier.fillMaxSize().background(HudColors.Bg)) {
            TripDetailScreen(row = row, vm = trips, onDismiss = { openTrip = null })
        }
    }
}

@Composable
private fun TabButton(
    label: String,
    selected: Boolean,
    enabled: Boolean = true,
    onClick: () -> Unit,
) {
    val accent = when {
        !enabled -> HudColors.TextDim
        selected -> HudColors.Cyan
        else -> HudColors.CyanDim
    }
    Box(
        Modifier
            .padding(end = 8.dp)
            .border(1.dp, accent)
            .background(if (selected) HudColors.Panel else HudColors.Bg)
            .clickable(enabled = enabled) { onClick() }
            .padding(horizontal = 14.dp, vertical = 8.dp),
    ) {
        Text(label, color = accent, style = HudeLabel)
    }
}
