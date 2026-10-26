package com.alefinot.dashboardpp.ui.theme

import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.animation.core.CubicBezierEasing
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.draw.shadow
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.Shape
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.dp

/**
 * Shape and motion taken from the ESP Web UI's CSS, so the native screens and the
 * WebView page are built from one set of numbers instead of two that happen to
 * look alike. Each entry names the rule it came from.
 *
 * There is no elevation scale: the page has exactly two shadows
 * (`--card-shadow` 0 8px 32px on a card, `box-shadow: 0 2px 8px` on a button), and
 * they are the `elevation` arguments of `hudGlass` and `HudButton`. The touch
 * target sizes are Android's, not the page's - see `HudToggle`.
 */
object HudRadius {
    /** `.card`, `.perf-card`, `#msg`, `button`: border-radius 8px. */
    val Card = 8.dp
    /** `.confirm-dialog`, `details`: 12px. */
    val Overlay = 12.dp
    /** `#searchBar`, `.acc-collapser`: 10px. */
    val Row = 10.dp
    /** `input[type=text|number]`, `select`: 6px. */
    val Field = 6.dp
    /** `.ip-banner`: pill. */
    val Pill = 30.dp
    /** `.adv-switch` track: 18px tall, radius 9px. */
    val Toggle = 9.dp

    val CardShape = RoundedCornerShape(Card)
    val OverlayShape = RoundedCornerShape(Overlay)
    val RowShape = RoundedCornerShape(Row)
    val FieldShape = RoundedCornerShape(Field)
    val PillShape = RoundedCornerShape(Pill)
}

/**
 * The Web UI's motion vocabulary: one easing curve everywhere
 * (`cubic-bezier(.16, 1, .3, 1)` - a strong ease-out that arrives fast and settles
 * slowly), a 0.5 s page entrance, 0.25-0.3 s for controls, 0.8 s for a bar that
 * changes width. Compose honours the system "animator duration scale", so turning
 * animations off in Developer Options turns all of this off with no code here.
 */
object HudMotion {
    /** `cubic-bezier(0.16, 1, 0.3, 1)` - used by every transition in the page. */
    val Standard = CubicBezierEasing(0.16f, 1f, 0.3f, 1f)

    /** CSS `ease`, which is what the page uses for its plain property transitions. */
    val Ease = CubicBezierEasing(0.25f, 0.1f, 0.25f, 1f)

    /** `animation: pageFadeIn 0.5s` (body, `.panel`, `#sel-panel`). */
    const val ENTRANCE_MS = 500

    /** `.confirm-dialog`, `button:hover/active`: 0.25 s. */
    const val SHORT_MS = 250

    /** border/colour transitions on inputs, selects, `.form-row button`: 0.2-0.3 s. */
    const val CONTROL_MS = 300

    /** `.adv-switch` and `.reset-btn`: 0.2 s. */
    const val SWITCH_MS = 200

    /** `.perf-bar-fill`: `transition: width 0.8s ease`. */
    const val BAR_MS = 800

    /** `#sel-panel`: `animation: selFadeIn 0.15s ease-out`. */
    const val REVEAL_MS = 150

    /**
     * Stagger between list rows arriving for the first time. The page has no
     * staggered list (its panels appear whole), so this is the app's own number,
     * chosen small enough that a nine-row wall settles inside one page entrance.
     */
    const val STAGGER_MS = 30
}

/**
 * The page's gradients, written the way the CSS writes them (135deg runs from the
 * top-left corner to the bottom-right, which is what Compose's default linear
 * gradient does across the same box).
 */
object HudBrushes {
    /** `button` - the default action, `linear-gradient(135deg, rgba(0,229,255,.15), rgba(41,121,255,.25))`. */
    fun button(): Brush = Brush.linearGradient(
        listOf(Color(0x2600E5FF), Color(0x402979FF)),
    )

    /** `button:hover` - the brightened state, and the one a touch press uses. */
    fun buttonActive(): Brush = Brush.linearGradient(
        listOf(Color(0x4D00E5FF), Color(0x732979FF)),
    )

    /** `h2` - the heading's clip: `linear-gradient(135deg, #ffffff 30%, #00e5ff)`. */
    fun titleGradient(): Brush = Brush.linearGradient(
        0f to Color(0xFFFFFFFF),
        0.3f to Color(0xFFFFFFFF),
        1f to HudColors.Cyan,
    )

    /** `.btn-secondary` - a flat fill, `rgba(30,41,59,.7)`, not a gradient. */
    val secondary = Color(0xB31E293B)

    /** `.btn-warning`. */
    fun warning(): Brush = Brush.linearGradient(
        listOf(Color(0x33FF9100), Color(0x4DD84315)),
    )

    /** `.btn-danger`. */
    fun danger(): Brush = Brush.linearGradient(
        listOf(Color(0x33FF1744), Color(0x4CC62828)),
    )

    /**
     * `h2`: `background: linear-gradient(135deg, #ffffff 30%, #00e5ff)` with
     * `background-clip: text`. Applied as a text brush, Compose resolves the
     * offsets against the text layout's own bounds - the same box the CSS paints
     * the gradient into. White holds to 30% the way the CSS stop does.
     */
    val title: Brush by lazy {
        Brush.linearGradient(
            0f to HudColors.White,
            0.3f to HudColors.White,
            1f to HudColors.Cyan,
        )
    }
}

/**
 * The page's frosted **overlay** family - `#searchBar`, `.confirm-dialog`,
 * `details` - as Compose draws it: `--bg-card` over black, a 1px `--border-color`
 * hairline, `--card-shadow` (`0 8px 32px rgba(0,0,0,.4)`) and its second half,
 * `inset 0 1px 0 rgba(255,255,255,.05)`, as a light down the inside top edge.
 *
 * `.card` is a different recipe - different fill, fainter hairline, no shadow at
 * all - and lives in `hudCard`. Naming both, instead of one "glass" modifier used
 * everywhere, is what keeps the page's two families distinguishable in the app;
 * the first pass here conflated them and made every card float.
 *
 * `wash` is the colour layer between the fill and the edge treatments - the page
 * stacks `background-color` and `background-image` in that order too.
 *
 * The CSS's `backdrop-filter: blur(12px)` needs `RenderEffect`, API 31+, and the
 * pixels behind the sheet, so it is not drawn here; the translucency, hairline and
 * shadow are what the page contributes over its flat black body, and they are what
 * matches on every Android version.
 */
@Composable
fun Modifier.hudGlass(
    shape: Shape = HudRadius.OverlayShape,
    fill: Color = HudColors.Panel,
    borderColor: Color = HudColors.Hairline,
    elevation: Dp = 12.dp,
    highlight: Float = 0.05f,
    wash: Brush? = null,
): Modifier = this
    .shadow(elevation, shape, clip = false, ambientColor = HudColors.Shadow, spotColor = HudColors.Shadow)
    .clip(shape)
    .background(fill)
    .then(if (wash != null) Modifier.background(wash) else Modifier)
    .then(
        if (highlight > 0f) {
            Modifier.background(
                Brush.verticalGradient(
                    0f to Color.White.copy(alpha = highlight),
                    0.35f to Color.Transparent,
                ),
            )
        } else {
            Modifier
        },
    )
    .border(1.dp, borderColor, shape)

/**
 * `.card`: `rgba(22,30,46,.6)`, a 1px hairline at `rgba(255,255,255,.05)`, 8px
 * corners, 12x14 of padding. No shadow and no inset highlight - the page puts
 * `--card-shadow` on overlays and menus, never on a card, so `elevation 0` here is
 * a reading of the rule, not an omission: a wall of cards each dropping a 32px
 * shadow is not what the page looks like.
 *
 * `.card:hover` brightens the fill to `rgba(26,36,56,.8)` and the hairline to
 * `.12`. There is no hover on a touch screen, so the caller decides what the
 * brightened state means (a selected row, a pressed row) and animates it on the
 * page's own 0.2s, so a change never snaps.
 */
@Composable
fun Modifier.hudCard(
    shape: Shape = HudRadius.CardShape,
    fill: Color = HudColors.CardFill,
    borderColor: Color = HudColors.CardHairline,
    wash: Brush? = null,
    highlight: Float = 0f,
): Modifier = this
    .clip(shape)
    .background(fill)
    .then(if (wash != null) Modifier.background(wash) else Modifier)
    .then(
        if (highlight > 0f) {
            Modifier.background(
                Brush.verticalGradient(
                    0f to Color.White.copy(alpha = highlight),
                    0.35f to Color.Transparent,
                ),
            )
        } else {
            Modifier
        },
    )
    .border(1.dp, borderColor, shape)
