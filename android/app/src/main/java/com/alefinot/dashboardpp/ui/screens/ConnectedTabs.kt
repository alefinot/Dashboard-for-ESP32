package com.alefinot.dashboardpp.ui.screens

import androidx.compose.animation.animateColorAsState
import androidx.compose.animation.core.animateFloatAsState
import androidx.compose.animation.core.tween
import androidx.compose.foundation.Image
import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.WindowInsets
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.navigationBars
import androidx.compose.foundation.layout.offset
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.windowInsetsPadding
import androidx.compose.foundation.pager.HorizontalPager
import androidx.compose.foundation.pager.PagerState
import androidx.compose.foundation.pager.rememberPagerState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.CompositionLocalProvider
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.derivedStateOf
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateMapOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.setValue
import androidx.compose.runtime.withFrameNanos
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.alpha
import androidx.compose.ui.draw.clip
import androidx.compose.ui.draw.drawWithContent
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Rect
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.ColorFilter
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.layout.boundsInRoot
import androidx.compose.ui.layout.onGloballyPositioned
import androidx.compose.ui.layout.positionInRoot
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.ui.platform.LocalViewConfiguration
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.dp
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import com.alefinot.dashboardpp.api.EspApiLane
import com.alefinot.dashboardpp.service.TripLogService
import com.alefinot.dashboardpp.trips.RecorderSettings
import com.alefinot.dashboardpp.trips.TripsEnabled
import com.alefinot.dashboardpp.ui.components.HudEnter
import com.alefinot.dashboardpp.ui.components.HudIcons
import com.alefinot.dashboardpp.ui.components.LocalEnterActive
import com.alefinot.dashboardpp.ui.components.PageFade
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
import kotlinx.coroutines.delay
import kotlinx.coroutines.launch

/**
 * The four modes of a connected dashboard: native live view, the ESP's own
 * configuration (its Web UI, labelled for what it configures), recorded trips,
 * and this app's own settings.
 *
 * They live in a bottom bar now, not in a strip under the title, and each one is
 * an icon rather than a word: Live carries the app's own mark, Trips a map pin,
 * ESP settings the chip, App settings the cog. Live sits in the middle of the bar
 * because it is the mode the app is mostly in; on its left are Trips and a Coming
 * Soon placeholder, on its right the two settings. The names are not lost - each
 * icon keeps its name as the accessibility description, so TalkBack still reads a
 * label and the touch target is a labelled control.
 *
 * The recording service is started here for the lifetime of the connection -
 * not the lifetime of a screen - so moving between tabs, or turning the screen
 * off in the car, keeps the log running.
 */
enum class AppTab { LIVE, TRIPS, WEB, SETTINGS }

/** The five slots of the bottom bar, left to right, with Live in the middle. */
private enum class BarSlot { SOON, TRIPS, LIVE, WEB, SETTINGS }

private val BarSlots = listOf(BarSlot.SOON, BarSlot.TRIPS, BarSlot.LIVE, BarSlot.WEB, BarSlot.SETTINGS)

/**
 * The swipe order. Left to right, so dragging a finger left walks right along the
 * bar and the two agree about what is next to what.
 *
 * SOON is not in it. The bar advertises a screen that does not exist; putting it
 * in the pager would make the swipe pass through a hole.
 */
private val BarPages = listOf(AppTab.TRIPS, AppTab.LIVE, AppTab.WEB, AppTab.SETTINGS)

/** Live's index in [BarPages] and in the bar, which are the same order. */
private const val LIVE_PAGE = 1
private const val TRIPS_PAGE = 0

/**
 * The app's only screen once it is up: the five-slot bar and the pages it swipes
 * between. There is no other route to a settings screen, no overlay, no back handler,
 * and no second version of the bar; the trip detail is a pushed route, the only one.
 *
 * It is the shell with a device *and* the shell without one. With no device, `conn` is
 * null: the bar keeps all five slots, Trips and App Settings work in full, and Live
 * and the ESP slot answer with a cover that quotes the reason the view model gave.
 * A lost connection is not an exit from the app - it is a page with nothing to show
 * right now. See [ShellRouting] for how a state becomes a mode and [ShellRules] for
 * what the bar and the recorder do about it.
 *
 * The ESP page is kept per connection rather than per visit, so re-entering it is a
 * paint rather than the 1.55 s reload measured before (see [WebUiHolder]). The WebView
 * itself is built lazily: someone who never opens the slot never costs the ESP a page
 * fetch, and with no device at all there is nothing to host.
 */
@Composable
fun AppShell(
    conn: ConnectionUiState.Connected?,
    reason: String?,
    findIsRetryNetwork: Boolean,
    connection: ConnectionViewModel,
    trips: TripsViewModel,
) {
    val context = LocalContext.current
    val activity = context as? android.app.Activity
    val connected = conn != null
    val ip = conn?.ip
    val webUi = remember(ip) {
        if (ip == null || activity == null) null else WebUiHolder(activity, ip, connection)
    }
    // Has the page been opened at all this connection? After that it stays
    // composed behind the other pages instead of being torn down, which is what
    // makes coming back to it instant.
    var webSeen by remember(ip) { mutableStateOf(false) }
    var openTrip by remember { mutableStateOf<TripRow?>(null) }
    // The Coming Soon slot is not a destination: tapping it leaves the page alone
    // and answers in place, so the bar needs somewhere to say so without a dialog.
    var soonNote by remember { mutableStateOf(false) }
    // Typing an address by hand is a panel over the shell, raised from the covers and
    // from App Settings, and it closes by itself the moment a device turns up.
    var manualOpen by remember { mutableStateOf(false) }
    LaunchedEffect(connected) { if (connected) manualOpen = false }

    // Firmware with no snapshot endpoint has no trip store; with no device there is no
    // version to check, and the trip files are the phone's own, so the trips pages
    // stay pages (`forFirmware(null)` is true).
    val tripsAvailable = TripsEnabled.forFirmware(conn?.version)

    // The user's recording switch, collected here so starting and stopping the
    // service follows it immediately (the service checks it again on its side).
    val settings = remember { RecorderSettings(context) }
    val recordingEnabled by settings.recordingEnabled
        .collectAsStateWithLifecycle(initialValue = true)
    // The motion switch, collected once here (this composable owns the bar, the pill
    // and both lists) and mirrored into the theme, so the pill, the entrances and the
    // list stagger all read one flag and change together.
    val motionEnabled by settings.motionEnabled
        .collectAsStateWithLifecycle(initialValue = true)
    LaunchedEffect(motionEnabled) { HudMotion.setPrefEnabled(motionEnabled) }

    val pager = rememberPagerState(initialPage = LIVE_PAGE, pageCount = { BarPages.size })
    val scope = rememberCoroutineScope()
    // The settled page. The bar's selection, which screen is `active`, and whether
    // the Web UI may poll all follow this one value.
    val page = BarPages.getOrElse(pager.currentPage) { AppTab.LIVE }

    // Recording needs a device to sample, firmware with the snapshot endpoint, and the
    // user's switch - all three, and the first of them is why the service never runs
    // in the offline shell (see ShellRules.recorderWants).
    LaunchedEffect(ip, tripsAvailable, recordingEnabled) {
        val target = ip?.takeIf { ShellRules.recorderWants(ip, tripsAvailable, recordingEnabled) }
        if (target != null) {
            TripLogService.start(context, target)
        } else {
            TripLogService.stop(context)
            // A page the firmware cannot serve is snapped out of, not animated to: the
            // user did not ask to go anywhere. With no device the trips page is the
            // phone's own, so the offline shell leaves the user on it.
            if (ShellRules.pageUnserved(tripsAvailable, connected = ip != null)) {
                val now = BarPages.getOrElse(pager.currentPage) { AppTab.LIVE }
                if (now == AppTab.TRIPS) pager.scrollToPage(LIVE_PAGE)
            }
        }
    }

    // The Web UI page fetches /api/live itself; while it is on screen it is the only
    // HTTP client and the recorder stands down (plan §4.3). Since the page now stays
    // alive between tabs, its own 1 Hz tick must be stopped while it is hidden -
    // otherwise two pollers would hit the dashboard at once. Counted at the socket
    // layer with `ss`: with the page hidden the app opens no connection to the ESP at
    // all, while the page on screen opens about two per second (it fetches more than
    // one endpoint per tick).
    // Two different questions, and collapsing them is what made the app sluggish.
    // "Is the page inside the pager's viewport" decides whether it may paint and take
    // touches; a swipe uncovers it long before the pager commits, so waiting for the
    // settled page shows a half-drawn page sliding in. "Is it the settled page"
    // decides who owns the HTTP lane and whether the page's own 1 s tick runs - that
    // one has to wait for the settle, or two clients would poll the dashboard at once.
    // The viewport answer is derived: recomputed as the pager moves, but it only
    // changes the moment it flips, so a swipe does not recompose this screen.
    val webInView by remember(webUi) {
        derivedStateOf {
            PagerFocus.inViewport(
                pager.currentPage,
                pager.currentPageOffsetFraction,
                BarPages.indexOf(AppTab.WEB),
            )
        }
    }
    LaunchedEffect(webInView) {
        if (webInView) webSeen = true
        webUi?.setDrawn(webInView)
    }
    LaunchedEffect(page) {
        val visible = page == AppTab.WEB
        EspApiLane.webUiVisible = visible
        if (visible) webUi?.onShow() else webUi?.onHide()
    }
    // Warming the ESP slot. The View has been kept alive between visits since app 21, so
    // the fetch of the page is the one thing that can still be running underneath the
    // frame that carries the tab switch. It happens while the app is idle: one frame of
    // quiet first so the shell paints before Chromium is spent on a page nobody is
    // looking at, then one second without the pager moving. The keys are the cancelling:
    // a swipe restarts the wait (and the shell re-keys the holder when a connection
    // drops), and once the user has opened the slot there is nothing left to warm.
    var warmed by remember(ip) { mutableStateOf(false) }
    LaunchedEffect(ip, webSeen, page, pager.isScrollInProgress) {
        val holder = webUi ?: return@LaunchedEffect
        if (!ShellRules.shouldWarm(connected = ip != null, seen = webSeen, warmed = warmed)) {
            return@LaunchedEffect
        }
        withFrameNanos { }
        delay(1_000)
        if (webSeen) return@LaunchedEffect   // the user reached the slot on their own
        holder.warm(context)
        warmed = true
    }
    // A warmed page is fetched but not seen, so its own 1 Hz tick has to be stopped once
    // the fetch lands: parked on Live, the app must still hold exactly one HTTP client
    // (plan §4.3), and pausing any earlier would pause the fetch itself.
    LaunchedEffect(warmed, webSeen, ip) {
        if (warmed && !webSeen) webUi?.quiesceWhenSettled()
    }
    DisposableEffect(webUi) {
        onDispose {
            EspApiLane.webUiVisible = false
            webUi?.release()
        }
    }

    // The bar: the selected slot's glass sheet is measured from the icons, and it
    // follows the pager rather than a separate selection - which is what makes it
    // slide during a swipe instead of only after one.
    // The sheet's composable reads this map and the pager; the screen body does not,
    // so a swipe recomposes the sheet and nothing else.
    val bounds = remember { mutableStateMapOf<AppTab, Rect>() }
    // One mirror for the screen: a gesture's state has to survive between the events
    // that make it up.
    val touchSlop = LocalViewConfiguration.current.touchSlop
    val mirror = remember(webUi) { GestureMirror({ webUi?.peek() }, touchSlop) }

    Box(Modifier.fillMaxSize().background(HudColors.Bg)) {
        Column(Modifier.fillMaxSize()) {
            Box(Modifier.weight(1f).fillMaxWidth()) {
                // Composed once it has been opened, and kept that way: the native
                // pages above are opaque, so this is invisible while they are up.
                // Letting Compose remove it is what costs the user time - the View
                // leaves the hierarchy, Chromium drops its rendering surface, and the
                // tab then opens as a black hole until a new surface and a first frame
                // arrive: 0.6-1.1 s measured, even with the page still loaded. Covered
                // instead of removed, its last frame stays warm and the tab is simply
                // there again - filmed at 10 fps, the frame that carries the tab switch
                // already shows the finished page.
                if (webSeen && webUi != null) {
                    DashboardWebScreen(webUi)
                }
                // The pages slide over that retained view. The ESP page is a hole in
                // them on purpose: once the page is up, the view underneath is the
                // content, and the swipe is what uncovers it.
                // A pager claims every gesture that starts inside it, so a page kept
                // underneath would never be touched - the ESP slot mirrors its gesture
                // stream onto that View (see [GestureMirror]), which gives the web page
                // its taps and scrolls back without giving up the swipe.
                // Pages beyond the visible one are composed as well, so arriving is a
                // paint and not a build: a cold page used to be composed while the
                // pager was already moving, and the Trips page costs ~650 ms of UI
                // thread on this tablet (measured - see the plan's baseline). That is
                // the one long frame in a swipe.
                //
                // It is only affordable because everything a page *does* is keyed to
                // the viewport flag computed below rather than to its composition: the
                // HTTP lane, the web view's paint, the record chip's breath, the trips
                // list's reveal and the page's own drawing all wait for the page to be
                // on screen, and the trip list re-reads itself on arrival instead of on
                // composition.
                //
                // And the building is timed so it does not land on the user: the pages
                // are composed a beat after the shell has drawn and animated in, one
                // page per frame. Pre-composing them all in the shell's first
                // composition measured 4.4 s to the bar against 3.1 s without it, and
                // that is the first thing the driver ever sees.
                var extraPages by remember { mutableStateOf(0) }
                LaunchedEffect(Unit) {
                    delay(600)
                    while (extraPages < BarPages.size - 2) {
                        withFrameNanos { }
                        extraPages++
                    }
                }
                HorizontalPager(
                    state = pager,
                    modifier = Modifier.fillMaxSize(),
                    beyondViewportPageCount = extraPages,
                ) { p ->
                    // Is this page on screen right now? Its own derived flag, read
                    // rather than computed in the body: the pager hands over a new
                    // offset every frame of a drag, and only the flip is worth
                    // reacting to. This is the same rule that says whether the web
                    // view may paint, so a page paints, is mirrored and animates in on
                    // one and the same event (see PagerFocus).
                    val inView by remember(pager, p) {
                        derivedStateOf {
                            PagerFocus.inViewport(
                                pager.currentPage,
                                pager.currentPageOffsetFraction,
                                p,
                            )
                        }
                    }
                    when (BarPages.getOrElse(p) { AppTab.LIVE }) {
                        AppTab.TRIPS -> NativePage(inView) { TripsTab(trips) { row -> openTrip = row } }
                        AppTab.LIVE -> NativePage(inView) {
                            if (conn == null) {
                                // The page stays, the live view does not: this is what
                                // "limited features, not a limited app" looks like on the
                                // page that needs a device most.
                                OfflineCover(
                                    title = "NO DASHBOARD ON THE NETWORK",
                                    reason = reason,
                                    note = "Trips and App Settings work without a device. " +
                                        "Speed, GNSS and the dashboard's own configuration " +
                                        "need one - connect, and this page is the live view " +
                                        "again.",
                                    rememberedIp = connection.lastKnownIp.value,
                                    findLabel = if (findIsRetryNetwork) "RETRY NETWORK" else "FIND DEVICE",
                                    onFind = {
                                        if (findIsRetryNetwork) connection.retryNoWifi() else connection.rescan()
                                    },
                                    onEnterIp = { manualOpen = true },
                                    onWifi = { openWifiSettings(context) },
                                )
                            } else {
                                // Off-screen pages are not the live view: their polling has
                                // to stay down or two clients would hit /api/snapshot at once.
                                LiveTab(ip = conn.ip, active = page == AppTab.LIVE)
                            }
                        }
                        AppTab.WEB -> Box(
                            Modifier
                                .fillMaxSize()
                                .webUiGestureMirror(mirror) {
                                    // Only the settled ESP page mirrors, and only when
                                    // there is a page to touch: while the cover is up it
                                    // is the cover that takes the taps.
                                    page == AppTab.WEB &&
                                        webUi?.stage?.value == WebUiStage.READY
                                },
                        ) {
                            CompositionLocalProvider(LocalEnterActive provides inView) {
                                if (webUi != null) {
                                    val stage by webUi.stage.collectAsStateWithLifecycle()
                                    if (stage != WebUiStage.READY) {
                                        // Exactly one fetch per connection has nothing drawn
                                        // behind the page: the first one. It gets the page's
                                        // overlay treatment instead of a black rectangle, and
                                        // a RETRY that re-issues the fetch.
                                        WebUiCover(stage, conn?.ip ?: "") { webUi.retry() }
                                    }
                                } else {
                                    // No holder, because no device: the slot has no view to
                                    // show, so it paints its own page and says what it is
                                    // waiting for. Opaque, for the reason in [OfflineCover].
                                    OfflineCover(
                                        title = "NO DEVICE CONNECTED",
                                        reason = reason,
                                        note = "The dashboard's own configuration lives on the " +
                                            "device. This slot becomes its Web UI as soon as a " +
                                            "device is connected.",
                                        rememberedIp = connection.lastKnownIp.value,
                                        findLabel = if (findIsRetryNetwork) "RETRY NETWORK" else "FIND DEVICE",
                                        onFind = {
                                            if (findIsRetryNetwork) connection.retryNoWifi() else connection.rescan()
                                        },
                                        onEnterIp = { manualOpen = true },
                                        onWifi = { openWifiSettings(context) },
                                    )
                                }
                            }
                        }
                        AppTab.SETTINGS -> NativePage(inView) {
                            // A block fade rather than per-card stagger: this page is a
                            // stack of settings cards, and the page it mirrors fades its
                            // body in as one block too.
                            PageFade(Modifier.fillMaxSize()) {
                                SettingsScreen(
                                    vm = connection,
                                    ip = conn?.ip,
                                    version = conn?.version,
                                    trips = trips,
                                    tripsAvailable = tripsAvailable,
                                    onEnterIp = { manualOpen = true },
                                )
                            }
                        }
                    }
                }
            }
            // One hairline above the bar, the way the page separates its header from
            // the content (`header::after` is a 2px cyan fade). It used to sit under
            // the strip at the top; the bar moved the separation to the bottom.
            Box(Modifier.fillMaxWidth().height(1.dp).background(HudColors.Hairline))
            BottomBar(
                selected = page,
                pager = pager,
                bounds = bounds,
                tripsAvailable = tripsAvailable,
                connected = connected,
                onBounds = { slot, r -> bounds[slot] = r },
                onSoon = { soonNote = true },
                onSelect = { slot ->
                    val index = BarPages.indexOf(slot)
                    if (index >= 0) {
                        scope.launch {
                            // The bar animates the same move a swipe would make, so a
                            // tap and a drag look the same and the sheet has one source
                            // of motion. With motion off it is a snap.
                            if (HudMotion.enabled) pager.animateScrollToPage(index) else pager.scrollToPage(index)
                        }
                    }
                },
            )
        }

        if (soonNote) {
            // Answered in place, over the content, so the bar does not have to grow
            // to say it. Disappears on its own.
            LaunchedEffect(soonNote) {
                delay(2600)
                soonNote = false
            }
            Box(Modifier.fillMaxSize(), contentAlignment = Alignment.BottomCenter) {
                HudEnter(delayMs = 0) {
                    Box(
                        Modifier
                            .padding(bottom = 84.dp)
                            .hudGlass(
                                shape = HudRadius.CardShape,
                                fill = HudColors.Msg,
                                borderColor = HudColors.CyanDim,
                                elevation = 6.dp,
                                highlight = 0.04f,
                            )
                            .padding(horizontal = 14.dp, vertical = 9.dp),
                    ) {
                        Text("COMING SOON - not built yet", color = HudColors.Cyan, style = HudeFormLabel())
                    }
                }
            }
        }

        if (manualOpen) {
            // Over the shell, not instead of it: cancelling returns the user to the page
            // they were on, and a successful entry recomposes the same shell with a
            // device in it.
            ManualEntryPanel(vm = connection) { manualOpen = false }
        }
    }

    openTrip?.let { row ->
        Box(Modifier.fillMaxSize()) {
            TripDetailScreen(row = row, vm = trips, onDismiss = { openTrip = null })
        }
    }
}

/**
 * The system's Wi-Fi panel, for the one case the app cannot fix itself: there is no
 * network to scan. A device with no such panel simply does not open anything - the
 * cover's RETRY NETWORK still does the useful half.
 */
private fun openWifiSettings(context: android.content.Context) {
    try {
        context.startActivity(
            android.content.Intent(android.provider.Settings.ACTION_WIFI_SETTINGS)
                .addFlags(android.content.Intent.FLAG_ACTIVITY_NEW_TASK),
        )
    } catch (e: android.content.ActivityNotFoundException) {
        // Nothing to open here; the cover's other two buttons still work.
    }
}

/**
 * A native page, opaque on purpose: it slides over the retained Web view, and a
 * transparent page would let that view show through mid-swipe.
 *
 * It also hands its panels the pager's answer about visibility, so their entrance
 * [HudEnter] runs when the page comes in rather than when it happens to be composed.
 */
@Composable
private fun NativePage(active: Boolean, content: @Composable () -> Unit) {
    Box(
        Modifier
            .fillMaxSize()
            .background(HudColors.Bg)
            // A page that is not on screen does not paint. With every page kept
            // composed, the frame pass would otherwise replay four pages of draw
            // commands for every frame the shell draws: the RECORDING breath on the
            // Live page measured 13 ms a frame with nothing kept composed and 17-26 ms
            // with the pages composed and still painting. The gate rides the same
            // viewport flag as everything else, and that flag flips at the first pixel
            // of movement (see PagerFocus), so a page never misses a frame it could be
            // seen in.
            .drawWithContent { if (active) drawContent() },
    ) {
        CompositionLocalProvider(LocalEnterActive provides active) { content() }
    }
}

/**
 * The glass sheet under the selected slot, and the only thing that reads the pager
 * while it moves.
 *
 * Its own composable on purpose. How far a pager has travelled changes every frame of
 * a swipe and every frame of a tap's animation; read out in the body of
 * [AppShell], that one value recomposes the whole screen - both native pages, the
 * gauges, the lists, the bar - sixty times a second, and on this tablet that was
 * enough to drop most of the frames it was drawing. Read here, it recomposes a Box.
 */
@Composable
private fun PillSheet(pager: PagerState, bounds: Map<AppTab, Rect>, barOrigin: Offset) {
    val density = LocalDensity.current
    val settled = pager.currentPage.coerceIn(0, BarPages.size - 1)
    val rest = bounds[BarPages[settled]]
    // A fraction of the way between two pages: integer at rest, and sweeping from
    // one integer to the next while the user drags or the bar animates a tap.
    val frac = (pager.currentPage + pager.currentPageOffsetFraction)
        .coerceIn(0f, (BarPages.size - 1).toFloat())
    val from = BarPages[frac.toInt()]
    val to = BarPages[(frac.toInt() + 1).coerceAtMost(BarPages.size - 1)]
    val f = frac - frac.toInt()
    val a = bounds[from]
    val b = bounds[to]
    // Motion on, the sheet is interpolated where the swipe actually is, so it travels
    // with the pages and stops where they stop. Motion off, it is simply under the
    // settled slot - and the page change itself is a snap, not a scroll.
    val pill = if (HudMotion.enabled && a != null && b != null && f > 0.001f) {
        Rect(
            a.left + (b.left - a.left) * f,
            a.top + (b.top - a.top) * f,
            a.right + (b.right - a.right) * f,
            a.bottom + (b.bottom - a.bottom) * f,
        )
    } else {
        rest ?: a
    } ?: return
    if (pill.width <= 0f) return
    with(density) {
        Box(
            Modifier
                .offset((pill.left - barOrigin.x).toDp(), (pill.top - barOrigin.y).toDp())
                .size(pill.width.toDp(), pill.height.toDp())
                // `.conn-pill.active`: the glass sheet with the cyan hairline.
                .hudGlass(
                    shape = HudRadius.CardShape,
                    borderColor = HudColors.CyanDim,
                    elevation = 8.dp,
                    highlight = 0.05f,
                ),
        )
    }
}

/**
 * The bottom bar: five slots, Live in the middle and larger, the current one under
 * a glass sheet that slides between them (`.conn-pill.active`). The sheet is not
 * five fills that swap in place - it is one sheet, because every state change on the
 * page is transitioned (0.2-0.3 s) and a tab that changes is a state change.
 */
@Composable
private fun BottomBar(
    selected: AppTab,
    pager: PagerState,
    bounds: Map<AppTab, Rect>,
    tripsAvailable: Boolean,
    connected: Boolean,
    onBounds: (AppTab, Rect) -> Unit,
    onSoon: () -> Unit,
    onSelect: (AppTab) -> Unit,
) {
    // Where the bar itself starts on screen. The slot bounds are measured in root
    // coordinates (that is what onGloballyPositioned reports), and Modifier.offset is
    // relative to this Box, so the two have to be reconciled - without that, the sheet
    // is drawn a bar-height away from where it was measured and lands off screen.
    var barOrigin by remember { mutableStateOf(Offset.Zero) }
    Box(
        Modifier
            .fillMaxWidth()
            .onGloballyPositioned { barOrigin = it.positionInRoot() }
            // The bar is the app's bottom edge, so it has to clear the system
            // navigation bar rather than sit under it. On a window that already fits
            // the system bars these insets read as zero.
            .windowInsetsPadding(WindowInsets.navigationBars),
    ) {
        PillSheet(pager, bounds, barOrigin)
        Row(Modifier.fillMaxWidth().padding(horizontal = 6.dp, vertical = 8.dp)) {
            for (slot in BarSlots) {
                val weight = if (slot == BarSlot.LIVE) 1.4f else 1f
                when (slot) {
                    BarSlot.SOON -> Box(Modifier.weight(weight), contentAlignment = Alignment.Center) {
                        BarGlyph(
                            icon = HudIcons.Clock,
                            desc = "Coming soon",
                            selected = false,
                            dim = true,
                            size = 22.dp,
                            box = 48.dp,
                            onClick = onSoon,
                        )
                    }
                    BarSlot.TRIPS -> Box(Modifier.weight(weight), contentAlignment = Alignment.Center) {
                        BarGlyph(
                            icon = HudIcons.Pin,
                            desc = "Trips",
                            selected = selected == AppTab.TRIPS,
                            // A firmware without a trip store keeps the pin and dims it
                            // (the strip used to write "TRIPS x" for the same fact).
                            dim = ShellRules.glyphDimmed(AppTab.TRIPS, connected, tripsAvailable),
                            size = 22.dp,
                            box = 48.dp,
                            onBounds = { onBounds(AppTab.TRIPS, it) },
                            enabled = tripsAvailable,
                            onClick = { onSelect(AppTab.TRIPS) },
                        )
                    }
                    BarSlot.LIVE -> Box(Modifier.weight(weight), contentAlignment = Alignment.Center) {
                        BarGlyph(
                            // The app's own mark, colours and all: this is the mode the
                            // app is mostly in, and the mark is how it is known.
                            icon = HudIcons.Gauge,
                            desc = "Live",
                            selected = selected == AppTab.LIVE,
                            colored = true,
                            size = 30.dp,
                            box = 52.dp,
                            onBounds = { onBounds(AppTab.LIVE, it) },
                            onClick = { onSelect(AppTab.LIVE) },
                        )
                    }
                    BarSlot.WEB -> Box(Modifier.weight(weight), contentAlignment = Alignment.Center) {
                        BarGlyph(
                            icon = HudIcons.Chip,
                            desc = "ESP settings",
                            selected = selected == AppTab.WEB,
                            // Dimmed rather than gone: the slot is a page even when there
                            // is no device to put in it (ShellRules.glyphDimmed).
                            dim = ShellRules.glyphDimmed(AppTab.WEB, connected, tripsAvailable),
                            size = 22.dp,
                            box = 48.dp,
                            onBounds = { onBounds(AppTab.WEB, it) },
                            onClick = { onSelect(AppTab.WEB) },
                        )
                    }
                    BarSlot.SETTINGS -> Box(Modifier.weight(weight), contentAlignment = Alignment.Center) {
                        BarGlyph(
                            icon = HudIcons.Cog,
                            desc = "App settings",
                            selected = selected == AppTab.SETTINGS,
                            size = 22.dp,
                            box = 48.dp,
                            onBounds = { onBounds(AppTab.SETTINGS, it) },
                            onClick = { onSelect(AppTab.SETTINGS) },
                        )
                    }
                }
            }
        }
    }
}

/**
 * One icon in the bar. Selected ones take the page's accent cyan and sit under the
 * travelling sheet; the rest are `--text-muted`, and dimmed ones `--text-dim`.
 *
 * The gauge is the exception: it is the launcher mark, so it is never re-tinted. It
 * loses alpha instead, which keeps the mark recognisable while it recedes.
 */
@Composable
private fun BarGlyph(
    icon: ImageVector,
    desc: String,
    selected: Boolean,
    dim: Boolean = false,
    colored: Boolean = false,
    size: Dp,
    box: Dp,
    enabled: Boolean = true,
    onBounds: (Rect) -> Unit = {},
    onClick: () -> Unit,
) {
    val ms = if (HudMotion.enabled) HudMotion.CONTROL_MS else 0
    val tint by animateColorAsState(
        when {
            dim -> HudColors.TextDim
            selected -> HudColors.Cyan
            else -> HudColors.TextMuted
        },
        tween(ms, easing = HudMotion.Ease),
        label = "glyphTint",
    )
    val fade by animateFloatAsState(
        targetValue = if (selected) 1f else 0.62f,
        tween(ms, easing = HudMotion.Ease),
        label = "glyphFade",
    )
    Box(
        Modifier
            .size(box)
            .clip(HudRadius.CardShape)
            // Measured as the icon's own box, so the sheet that slides under it is
            // that box and not the slot's whole weight.
            .onGloballyPositioned { c -> onBounds(c.boundsInRoot()) }
            .clickable(enabled = enabled) { onClick() },
        contentAlignment = Alignment.Center,
    ) {
        if (colored) {
            Image(icon, desc, modifier = Modifier.size(size).alpha(fade))
        } else {
            Image(icon, desc, colorFilter = ColorFilter.tint(tint), modifier = Modifier.size(size).alpha(fade))
        }
    }
}
