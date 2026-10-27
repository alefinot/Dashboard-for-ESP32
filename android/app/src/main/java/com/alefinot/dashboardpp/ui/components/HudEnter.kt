package com.alefinot.dashboardpp.ui.components

import androidx.compose.animation.core.animateFloatAsState
import androidx.compose.animation.core.tween
import androidx.compose.animation.fadeIn
import androidx.compose.animation.scaleIn
import androidx.compose.animation.slideIn
import androidx.compose.animation.AnimatedVisibility
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.staticCompositionLocalOf
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.foundation.layout.Box
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.graphicsLayer
import androidx.compose.ui.unit.IntOffset
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.ui.unit.dp
import com.alefinot.dashboardpp.ui.theme.HudMotion
import kotlinx.coroutines.delay

/**
 * Whether the page an entrance belongs to is on screen right now.
 *
 * Default true: a panel outside the pager (a dialog, a note, the first page at start
 * up) animates when it is composed, as it always has. `NativePage` in the shell
 * overrides it with the pager's own answer, so a page that is only sitting there
 * waiting to be swiped to does not spend its entrance before the user reaches it.
 *
 * Keyed to visibility rather than to composition because the pages of the shell are
 * kept composed between visits so that arriving costs a paint and not a rebuild - and
 * an entrance that runs on composition would be over and gone before the user got
 * there. It is keyed to the page *coming in* rather than to the pager settling, for
 * the reason app 21 measured: waiting for the settle shows a half-drawn page.
 */
val LocalEnterActive = staticCompositionLocalOf { true }

/**
 * The page's entrance, `pageFadeIn`: opacity 0 → 1 with an 8px rise, on
 * `cubic-bezier(.16, 1, .3, 1)`. Everything in the Web UI arrives this way - the
 * body (0.5s) and the modal dialog (0.25s) - so screens and panels in the app do
 * too, and the two surfaces read as one product's motion rather than two.
 *
 * It runs when the entrance is armed, once per visit. A list that re-composes items
 * while scrolling has to gate it (see TripsTab), or the same rows animate again on
 * every pass.
 *
 * The content is composed only while the entrance is up. Where the content has to
 * exist the whole time - a list with a scroll position to keep, a web page with a
 * frame to keep - [PageFade] does the same motion without taking the content away.
 */
@Composable
fun HudEnter(
    modifier: Modifier = Modifier,
    durationMs: Int = HudMotion.ENTRANCE_MS,
    delayMs: Int = 0,
    riseDp: Int = 8,
    scaleFrom: Float? = null,
    content: @Composable () -> Unit,
) {
    // Motion off: the content is there, with no transition at all. Branching rather
    // than passing a zero duration, because `AnimatedVisibility` given an `enter`
    // and no matching `exit` still runs the framework's default fade when it hides -
    // so a duration of 0 would leave a fade where the user asked for none.
    if (!HudMotion.enabled) {
        Box(modifier) { content() }
        return
    }
    val inView = LocalEnterActive.current
    var shown by remember { mutableStateOf(false) }
    LaunchedEffect(inView) {
        if (!inView) return@LaunchedEffect
        // Re-armed on every arrival, including the first: the entrance starts from
        // nothing, which is what a freshly composed page did before the pages started
        // being kept alive between visits.
        shown = false
        // A wall of panels arriving one after another (the Live screen: heading,
        // then the readout, then the figures) is one page entrance split into
        // slices, not a queue the user waits through - hence a 30 ms offset per
        // block, so nine panels settle inside one 0.5 s fade.
        if (delayMs > 0) delay(delayMs.toLong())
        shown = true
    }
    val spec = tween<IntOffset>(durationMs, easing = HudMotion.Standard)
    val rise = with(LocalDensity.current) { riseDp.dp.roundToPx() }
    AnimatedVisibility(
        visible = shown,
        enter = fadeIn(tween(durationMs, easing = HudMotion.Standard)) +
            slideIn(spec, initialOffset = { IntOffset(0, rise) }) +
            if (scaleFrom != null) {
                scaleIn(tween(durationMs, easing = HudMotion.Standard), initialScale = scaleFrom)
            } else {
                androidx.compose.animation.EnterTransition.None
            },
        modifier = modifier,
    ) { content() }
}

/**
 * The same `pageFadeIn` for content that stays composed while it fades: the trips
 * list, which would otherwise lose its scroll position on every visit, and the
 * retained web page, which cannot be torn down and rebuilt without losing its frame
 * (the black hole app 21 removed).
 *
 * [HudEnter] composes its content only once the entrance starts; here the content is
 * always composed and only its alpha and offset move. One motion, one curve, one
 * duration either way, and both are armed by the page coming into view
 * ([LocalEnterActive]).
 */
@Composable
fun PageFade(
    modifier: Modifier = Modifier,
    durationMs: Int = HudMotion.ENTRANCE_MS,
    delayMs: Int = 0,
    riseDp: Int = 8,
    content: @Composable () -> Unit,
) {
    // Motion off, as in [HudEnter]: the content is simply there.
    if (!HudMotion.enabled) {
        Box(modifier) { content() }
        return
    }
    val inView = LocalEnterActive.current
    var shown by remember { mutableStateOf(false) }
    LaunchedEffect(inView) {
        // Out of view is left alone: what the page showed on its way out is what it
        // shows again as it comes in, until the arrival re-arms the entrance.
        if (!inView) return@LaunchedEffect
        shown = false
        if (delayMs > 0) delay(delayMs.toLong())
        shown = true
    }
    val spec = tween<Float>(durationMs, easing = HudMotion.Standard)
    val fade by animateFloatAsState(if (shown) 1f else 0f, spec, label = "pageFade")
    val rise = with(LocalDensity.current) { riseDp.dp.toPx() }
    val shift by animateFloatAsState(if (shown) 0f else rise, spec, label = "pageRise")
    Box(
        modifier.graphicsLayer {
            alpha = fade
            translationY = shift
        },
    ) { content() }
}
