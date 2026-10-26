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
import androidx.compose.ui.Modifier
import androidx.compose.ui.unit.IntOffset
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.ui.unit.dp
import com.alefinot.dashboardpp.ui.theme.HudMotion

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
    riseDp: Int = 8,
    scaleFrom: Float? = null,
    content: @Composable () -> Unit,
) {
    var shown by remember { mutableStateOf(false) }
    LaunchedEffect(Unit) { shown = true }
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
