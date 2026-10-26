package com.alefinot.dashboardpp.ui.components

import androidx.compose.animation.core.tween
import androidx.compose.animation.fadeIn
import androidx.compose.animation.scaleIn
import androidx.compose.animation.slideIn
import androidx.compose.animation.AnimatedVisibility
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.foundation.layout.Box
import androidx.compose.ui.Modifier
import androidx.compose.ui.unit.IntOffset
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.ui.unit.dp
import com.alefinot.dashboardpp.ui.theme.HudMotion
import kotlinx.coroutines.delay

/**
 * The page's entrance, `pageFadeIn`: opacity 0 → 1 with an 8px rise, on
 * `cubic-bezier(.16, 1, .3, 1)`. Everything in the Web UI arrives this way - the
 * body (0.5s) and the modal dialog (0.25s) - so screens and panels in the app do
 * too, and the two surfaces read as one product's motion rather than two.
 *
 * It runs on composition, once. A list that re-composes items while scrolling has
 * to gate it (see TripsTab), or the same rows animate again on every pass.
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
    var shown by remember { mutableStateOf(false) }
    LaunchedEffect(Unit) {
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
