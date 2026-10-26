package com.alefinot.dashboardpp.ui.screens

import androidx.compose.animation.AnimatedContent
import androidx.compose.animation.animateColorAsState
import androidx.compose.animation.core.animateDpAsState
import androidx.compose.animation.core.tween
import androidx.compose.animation.fadeIn
import androidx.compose.animation.fadeOut
import androidx.compose.animation.togetherWith
import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.offset
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateMapOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Rect
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.layout.boundsInRoot
import androidx.compose.ui.layout.onGloballyPositioned
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.platform.LocalDensity
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import androidx.compose.ui.unit.dp
import com.alefinot.dashboardpp.api.EspApiLane
import com.alefinot.dashboardpp.service.TripLogService
import com.alefinot.dashboardpp.trips.RecorderSettings
import com.alefinot.dashboardpp.trips.TripsEnabled
import com.alefinot.dashboardpp.webview.WebUiHolder
import com.alefinot.dashboardpp.webview.WebUiStage
import com.alefinot.dashboardpp.viewmodel.ConnectionUiState
import com.alefinot.dashboardpp.viewmodel.ConnectionViewModel
import com.alefinot.dashboardpp.viewmodel.TripRow
import com.alefinot.dashboardpp.viewmodel.TripsViewModel
import com.alefinot.dashboardpp.ui.theme.HudColors
import com.alefinot.dashboardpp.ui.theme.HudMotion
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
    // The motion switch, collected once here (this composable owns the tab host, the
    // pill and both lists) and mirrored into the theme, so the pill, the entrances
    // and the list stagger all read one flag and change together.
    val motionEnabled by settings.motionEnabled
        .collectAsStateWithLifecycle(initialValue = true)
    LaunchedEffect(motionEnabled) { HudMotion.setPrefEnabled(motionEnabled) }

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
        // The strip: four `.conn-pill`s, the current one a glass sheet with a cyan
        // hairline, the others hairline outlines. The sheet is not four fills that
        // swap in place - it is one sheet that slides from the old tab to the new
        // one, because every state change on the page is transitioned (0.2-0.3 s) and
        // a tab that changes is a state change.
        TabStrip(
            selected = tab,
            onSelect = {
                // Flipped here as well as in the LaunchedEffect above: that effect
                // runs a composition later, which left the WEB slot empty (black) for
                // one frame on the first visit - the same flash the cover exists to
                // stop, so the slot has to be filled in the frame the tab opens.
                if (it == AppTab.WEB) webSeen = true
                tab = it
            },
            tripsAvailable = tripsAvailable,
        )
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
                // Exactly one fetch per connection has nothing drawn behind the tab:
                // the first one. It gets the page's overlay treatment instead of a
                // black rectangle, and a RETRY that re-issues the fetch.
                val stage by webUi.stage.collectAsStateWithLifecycle()
                if (tab == AppTab.WEB && stage != WebUiStage.READY) {
                    WebUiCover(stage, conn.ip) { webUi.retry() }
                }
            }
            // The native screens and the empty slot that stands in for the page fade
            // between one another (`pageFadeIn` - the page fades a new view in rather
            // than swapping it). Only the natives are inside this: the retained page
            // stays the sibling underneath, because taking it out of the hierarchy at
            // all is what costs the 1.5 s (note above).
            AnimatedContent(
                targetState = tab,
                modifier = Modifier.fillMaxSize(),
                transitionSpec = {
                    fadeIn(tween(HudMotion.CONTROL_MS, easing = HudMotion.Standard)) togetherWith
                        fadeOut(tween(HudMotion.REVEAL_MS))
                },
                label = "tabContent",
            ) { t ->
                when (t) {
                    // The page below is the content; a full-size empty box keeps every
                    // tab the same size, so nothing shrinks while it fades.
                    AppTab.WEB -> Box(Modifier.fillMaxSize())
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
    }

    openTrip?.let { row ->
        Box(Modifier.fillMaxSize()) {
            TripDetailScreen(row = row, vm = trips, onDismiss = { openTrip = null })
        }
    }
}

/**
 * The four tabs, with the selected-tab sheet drawn as one moving pill underneath
 * the labels instead of four fills that swap. Positions are measured from the
 * labels themselves, so the sheet fits the word it sits under (they are different
 * widths) at any font scale.
 */
@Composable
private fun TabStrip(
    selected: AppTab,
    tripsAvailable: Boolean,
    onSelect: (AppTab) -> Unit,
) {
    val density = LocalDensity.current
    val bounds = remember { mutableStateMapOf<AppTab, Rect>() }
    var stripTopLeft by remember { mutableStateOf(Offset.Zero) }
    val target = bounds[selected]
    var measured by remember { mutableStateOf(false) }
    LaunchedEffect(target) { if (target != null) measured = true }
    // The first measurement lands without a slide: a sheet that arrived from the
    // corner of the screen on first layout is a trick, not a transition.
    // Motion off, or the very first measurement: either way the pill is simply put
    // where it belongs, with no travel to watch.
    val ms = if (measured && HudMotion.enabled) HudMotion.TAB_MS else 0
    val x = if (target != null) target.left - stripTopLeft.x else 0f
    val y = if (target != null) target.top - stripTopLeft.y else 0f
    val pillLeft by animateDpAsState(with(density) { x.toDp() }, tween(ms, easing = HudMotion.Standard), label = "pillX")
    val pillTop by animateDpAsState(with(density) { y.toDp() }, tween(ms, easing = HudMotion.Standard), label = "pillY")
    val pillW by animateDpAsState(with(density) { (target?.width ?: 0f).toDp() }, tween(ms, easing = HudMotion.Standard), label = "pillW")
    val pillH by animateDpAsState(with(density) { (target?.height ?: 0f).toDp() }, tween(ms, easing = HudMotion.Standard), label = "pillH")

    Box(
        Modifier
            .fillMaxWidth()
            .onGloballyPositioned { stripTopLeft = it.boundsInRoot().topLeft },
    ) {
        if (target != null && target.width > 0f) {
            Box(
                Modifier
                    .offset(pillLeft, pillTop)
                    .size(pillW, pillH)
                    // `.conn-pill.active`: the glass sheet with the cyan hairline.
                    .hudGlass(
                        shape = HudRadius.CardShape,
                        borderColor = HudColors.CyanDim,
                        elevation = 8.dp,
                        highlight = 0.05f,
                    ),
            )
        }
        Row(Modifier.padding(horizontal = 12.dp, vertical = 8.dp)) {
            TabButton(
                label = "LIVE",
                selected = selected == AppTab.LIVE,
                onBounds = { bounds[AppTab.LIVE] = it },
                onClick = { onSelect(AppTab.LIVE) },
            )
            TabButton(
                label = if (tripsAvailable) "TRIPS" else "TRIPS ✕",
                selected = selected == AppTab.TRIPS,
                enabled = tripsAvailable,
                onBounds = { bounds[AppTab.TRIPS] = it },
                onClick = { onSelect(AppTab.TRIPS) },
            )
            // "ESP SETTINGS" says whose settings these are — the ESP's own Web
            // UI — which is the distinction the app needs, not "Web UI".
            TabButton(
                label = "ESP SETTINGS",
                selected = selected == AppTab.WEB,
                onBounds = { bounds[AppTab.WEB] = it },
                onClick = { onSelect(AppTab.WEB) },
            )
            TabButton(
                label = "APP SETTINGS",
                selected = selected == AppTab.SETTINGS,
                onBounds = { bounds[AppTab.SETTINGS] = it },
                onClick = { onSelect(AppTab.SETTINGS) },
            )
        }
    }
}

@Composable
private fun TabButton(
    label: String,
    selected: Boolean,
    enabled: Boolean = true,
    onBounds: (Rect) -> Unit = {},
    onClick: () -> Unit,
) {
    val accent by animateColorAsState(
        when {
            !enabled -> HudColors.TextDim
            selected -> HudColors.Cyan
            else -> HudColors.TextMuted
        },
        tween(HudMotion.CONTROL_MS, easing = HudMotion.Ease),
        label = "tabLabel",
    )
    Box(
        Modifier
            .padding(end = 8.dp)
            // Measured inside its own trailing gap, so the sheet that slides under
            // this label is the size of the pill and not of the gap too.
            .onGloballyPositioned { c -> onBounds(c.boundsInRoot()) }
            // A tab is a control, so it takes the control radius (8dp). The current
            // one is the sliding sheet above - `hudGlass` is not applied here, else
            // it would paint over it. The rest are hairline-only outlines, the page's
            // `.conn-pill` at rest. Disabled (no trip store on this firmware) keeps
            // the outline and dims the label.
            .hudGlass(
                shape = HudRadius.CardShape,
                fill = Color.Transparent,
                borderColor = if (selected) Color.Transparent else Color(0x1FFFFFFF),
                elevation = 0.dp,
                highlight = 0f,
            )
            .clickable(enabled = enabled) { onClick() }
            .padding(horizontal = 14.dp, vertical = 8.dp),
    ) {
        Text(label, color = accent, style = HudeFormLabel())
    }
}
