package com.alefinot.dashboardpp.ui.screens

import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
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
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.platform.LocalContext
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import androidx.compose.ui.unit.dp
import com.alefinot.dashboardpp.api.EspApiLane
import com.alefinot.dashboardpp.service.TripLogService
import com.alefinot.dashboardpp.trips.RecorderSettings
import com.alefinot.dashboardpp.trips.TripsEnabled
import com.alefinot.dashboardpp.webview.WebUiHolder
import com.alefinot.dashboardpp.viewmodel.ConnectionUiState
import com.alefinot.dashboardpp.viewmodel.ConnectionViewModel
import com.alefinot.dashboardpp.viewmodel.TripRow
import com.alefinot.dashboardpp.viewmodel.TripsViewModel
import com.alefinot.dashboardpp.ui.theme.HudColors
import com.alefinot.dashboardpp.ui.theme.HudRadius
import com.alefinot.dashboardpp.ui.theme.hudGlass
import com.alefinot.dashboardpp.ui.theme.HudeFormLabel

/**
 * The four modes of a connected dashboard: native live view, the ESP's own
 * configuration (its Web UI, labelled for what it configures), recorded trips,
 * and this app's own settings.
 */
enum class AppTab { LIVE, TRIPS, WEB, SETTINGS }

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
    // One page per connection, kept alive across tab switches, so re-entering
    // ESP SETTINGS is a paint rather than the 1.55 s reload measured before
    // (see WebUiHolder). The WebView itself is built lazily: someone who never
    // opens the tab never costs the ESP a page fetch.
    val webUi = remember(conn.ip) { activity?.let { WebUiHolder(it, conn.ip, connection) } }
    var tab by remember { mutableStateOf(AppTab.LIVE) }
    // Has the page been opened at all this connection? After that it stays
    // composed behind the other tabs instead of being torn down, which is what
    // makes coming back to it instant.
    var webSeen by remember(conn.ip) { mutableStateOf(false) }
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

    // The Web UI page fetches /api/live itself; while it is on screen it is the
    // only HTTP client and the recorder stands down (plan §4.3). Since the page
    // now stays alive between tabs, its own 1 Hz tick must be stopped while it
    // is hidden - otherwise two pollers would hit the dashboard at once. Counted
    // at the socket layer with `ss`: with the page hidden the app opens no
    // connection to the ESP at all, while the page on screen opens about two per
    // second (it fetches more than one endpoint per tick).
    LaunchedEffect(tab) {
        val visible = tab == AppTab.WEB
        if (visible) webSeen = true
        EspApiLane.webUiVisible = visible
        if (visible) {
            webUi?.setInteractive(true)
            webUi?.onShow()
        } else {
            webUi?.setInteractive(false)
            webUi?.onHide()
        }
    }
    DisposableEffect(webUi) {
        onDispose {
            EspApiLane.webUiVisible = false
            webUi?.release()
        }
    }

    Column(Modifier.fillMaxSize().background(HudColors.Bg)) {
        Row(Modifier.padding(horizontal = 12.dp, vertical = 8.dp)) {
            TabButton("LIVE", tab == AppTab.LIVE) { tab = AppTab.LIVE }
            TabButton(
                label = if (tripsAvailable) "TRIPS" else "TRIPS ✕",
                selected = tab == AppTab.TRIPS,
                enabled = tripsAvailable,
            ) { tab = AppTab.TRIPS }
            // "ESP SETTINGS" says whose settings these are — the ESP's own Web
            // UI — which is the distinction the app needs, not "Web UI".
            TabButton("ESP SETTINGS", tab == AppTab.WEB) { tab = AppTab.WEB }
            TabButton("APP SETTINGS", tab == AppTab.SETTINGS) { tab = AppTab.SETTINGS }
        }
        // One hairline under the strip, the way the page separates its header from
        // the content (`header::after` is a 2px cyan fade; here a full hairline at
        // the token alpha, which is the same job at this width).
        Box(Modifier.fillMaxWidth().height(1.dp).background(HudColors.Hairline))

        Box(Modifier.fillMaxSize()) {
            // Composed once it has been opened, and kept that way: the native
            // tabs above are opaque, so this is invisible while they are up.
            // Letting Compose remove it is what costs the user time - the View
            // leaves the hierarchy, Chromium drops its rendering surface, and the
            // tab then opens as a black hole until a new surface and a first
            // frame arrive: 0.6-1.1 s measured, even with the page still loaded.
            // Covered instead of removed, its last frame stays warm and the tab
            // is simply there again - filmed at 10 fps, the frame that carries
            // the tab switch already shows the finished page.
            if (webSeen && webUi != null) {
                DashboardWebScreen(webUi)
            }
            when (tab) {
                AppTab.WEB -> Unit    // the page below is the content
                AppTab.LIVE -> LiveTab(ip = conn.ip, active = true)
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
    }

    openTrip?.let { row ->
        Box(Modifier.fillMaxSize()) {
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
        else -> HudColors.TextMuted
    }
    Box(
        Modifier
            .padding(end = 8.dp)
            // A tab is a control, so it takes the control radius (8dp). The current
            // one is a glass sheet with a cyan hairline, the page's
            // `.conn-pill.active`; the others are hairline-only outlines, its
            // `.conn-pill` at rest. Disabled (no trip store on this firmware) keeps
            // the outline and dims the label.
            .hudGlass(
                shape = HudRadius.CardShape,
                fill = if (selected) HudColors.Panel else Color.Transparent,
                borderColor = if (selected) HudColors.CyanDim else Color(0x1FFFFFFF),
                elevation = if (selected) 8.dp else 0.dp,
                highlight = if (selected) 0.05f else 0f,
            )
            .clickable(enabled = enabled) { onClick() }
            .padding(horizontal = 14.dp, vertical = 8.dp),
    ) {
        Text(label, color = accent, style = HudeFormLabel())
    }
}
