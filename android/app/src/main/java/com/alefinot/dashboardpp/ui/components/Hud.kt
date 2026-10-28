package com.alefinot.dashboardpp.ui.components

import androidx.compose.animation.animateColorAsState
import androidx.compose.animation.core.animateDpAsState
import androidx.compose.animation.core.animateFloatAsState
import androidx.compose.animation.core.tween
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.interaction.MutableInteractionSource
import androidx.compose.foundation.interaction.collectIsPressedAsState
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.text.BasicTextField
import androidx.compose.foundation.text.KeyboardActions
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.foundation.layout.offset
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.draw.shadow
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.graphicsLayer
import androidx.compose.ui.focus.onFocusEvent
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.input.ImeAction
import androidx.compose.ui.text.input.KeyboardType
import androidx.compose.ui.graphics.SolidColor
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.alefinot.dashboardpp.ui.theme.HudBrushes
import com.alefinot.dashboardpp.ui.theme.HudColors
import com.alefinot.dashboardpp.ui.theme.HudMotion
import com.alefinot.dashboardpp.ui.theme.HudRadius
import com.alefinot.dashboardpp.ui.theme.HudeButtonLabel

/**
 * `button` from the Web UI, copied: 8px radius, a 135° cyan→blue gradient fill, a
 * cyan hairline, 9x14 padding, 12px/600 label.
 *
 * There is deliberately no ripple: the page's press feedback is `button:active` -
 * the fill brightens, the shadow deepens and the whole button scales to 0.98 - and
 * that is what this does on press, on the page's own 0.25s. Copying the look while
 * keeping Material's white ripple would put a gesture the page never makes back
 * into the same control.
 */
enum class HudButtonStyle { PRIMARY, SECONDARY, WARNING, DANGER }

@Composable
fun HudButton(
    label: String,
    modifier: Modifier = Modifier,
    style: HudButtonStyle = HudButtonStyle.PRIMARY,
    enabled: Boolean = true,
    onClick: () -> Unit,
) {
    val shape = HudRadius.CardShape
    val interaction = remember { MutableInteractionSource() }
    val pressed by interaction.collectIsPressedAsState()
    val scale by animateFloatAsState(
        if (pressed) 0.98f else 1f,
        tween(HudMotion.SHORT_MS, easing = HudMotion.Standard),
        label = "buttonScale",
    )
    // `button` and `button:hover` are the two gradient states; a touch screen has
    // no hover, so the pressed state takes hover's job.
    val brush: Brush? = when (style) {
        HudButtonStyle.PRIMARY ->
            if (pressed) HudBrushes.buttonActive() else HudBrushes.button()
        HudButtonStyle.WARNING -> HudBrushes.warning()
        HudButtonStyle.DANGER -> HudBrushes.danger()
        HudButtonStyle.SECONDARY -> null        // .btn-secondary is a flat fill
    }
    val border = when (style) {
        HudButtonStyle.PRIMARY -> HudColors.CyanDim              // rgba(0,229,255,.3)
        HudButtonStyle.SECONDARY -> Color(0x1AFFFFFF)            // rgba(255,255,255,.1)
        HudButtonStyle.WARNING -> Color(0x66FF9100)              // rgba(255,145,0,.4)
        HudButtonStyle.DANGER -> Color(0x66FF1744)               // rgba(255,23,68,.4)
    }
    val foreground = when (style) {
        HudButtonStyle.PRIMARY -> HudColors.White
        HudButtonStyle.SECONDARY -> HudColors.TextPrimary
        HudButtonStyle.WARNING -> Color(0xFFFFB74D)              // .btn-warning color
        HudButtonStyle.DANGER -> Color(0xFFFF8A80)               // .btn-danger color
    }
    Box(
        modifier
            .graphicsLayer {
                scaleX = scale
                scaleY = scale
            }
            // `box-shadow: 0 2px 8px rgba(0,0,0,.2)`, deepening on the active state.
            .shadow(
                if (pressed) 4.dp else 2.dp, shape, clip = false,
                ambientColor = HudColors.Shadow, spotColor = HudColors.Shadow,
            )
            .clip(shape)
            .then(
                if (brush != null) Modifier.background(brush) else Modifier.background(HudBrushes.secondary),
            )
            .border(1.dp, border, shape)
            .clickable(
                interactionSource = interaction,
                indication = null,
                enabled = enabled,
                onClick = onClick,
            )
            .padding(horizontal = 14.dp, vertical = 9.dp),
        contentAlignment = Alignment.Center,
    ) {
        Text(label.uppercase(), color = foreground, style = HudeButtonLabel())
    }
}

/**
 * `.adv-switch`, copied to the pixel: a 34x18 track, radius 9, `#2a2a2a` off /
 * rgba(0,255,255,.25) on, and a 14px knob inset 2px that slides 16px and turns
 * accent-cyan - on the switch's own 0.2s transition, in the CSS's own `ease`.
 *
 * The control is 34x18dp but the touch target around it is 44dp tall. On the page
 * the whole `<label>` wraps the switch, so its tappable area is larger than the
 * control as well; 34x18dp on its own would be a hard target to hit on a phone in
 * a moving car.
 */
@Composable
fun HudToggle(on: Boolean, modifier: Modifier = Modifier, onToggle: (Boolean) -> Unit) {
    val track by animateColorAsState(
        if (on) Color(0x4000FFFF) else Color(0xFF2A2A2A),
        tween(HudMotion.SWITCH_MS, easing = HudMotion.Ease),
        label = "switchTrack",
    )
    val knobColor by animateColorAsState(
        if (on) HudColors.Cyan else Color(0xFF666666),
        tween(HudMotion.SWITCH_MS, easing = HudMotion.Ease),
        label = "switchKnob",
    )
    val knobX by animateDpAsState(
        if (on) 18.dp else 2.dp,
        tween(HudMotion.SWITCH_MS, easing = HudMotion.Ease),
        label = "switchKnobX",
    )
    Box(modifier.size(width = 44.dp, height = 44.dp), contentAlignment = Alignment.Center) {
        Box(
            Modifier
                .size(width = 34.dp, height = 18.dp)
                .clip(CircleShape)
                .background(track)
                .clickable(
                    interactionSource = remember { MutableInteractionSource() },
                    indication = null,
                ) { onToggle(!on) },
        ) {
            Box(
                Modifier
                    .offset(x = knobX, y = 2.dp)
                    .size(14.dp)
                    .clip(CircleShape)
                    .background(knobColor),
            )
        }
    }
}

/**
 * The page's two field recipes, copied. `#searchBar`: 10px radius, glass fill,
 * 8x16 padding, 13px text, `transition: all .25s ease`, and on focus a cyan
 * border, `rgba(20,28,45,.9)` behind it and `0 0 16px rgba(0,229,255,.2)` around
 * it - which is why the focused search field here gets a *cyan* shadow: Compose
 * shadows take a colour, so the glow is the same shadow, in the page's colour.
 * `input[type=text|number]` / `select`: 6px radius, `--bg-input` fill, 6x10
 * padding, 12px text, `transition: all .2s ease`, `#182030` on focus.
 *
 * `#searchBar`'s 40px left padding is room for the `.search-icon` glyph; the app's
 * filter rows have no icon, so they keep the 16px side padding.
 */
enum class HudInputStyle { SEARCH, FIELD }

@Composable
fun HudInput(
    value: String,
    onValueChange: (String) -> Unit,
    modifier: Modifier = Modifier,
    style: HudInputStyle = HudInputStyle.FIELD,
    placeholder: String = "",
    keyboardType: KeyboardType = KeyboardType.Text,
    imeAction: ImeAction = ImeAction.Done,
    onDone: () -> Unit = {},
) {
    val search = style == HudInputStyle.SEARCH
    var focused by remember { mutableStateOf(false) }
    val fill by animateColorAsState(
        when {
            !focused -> if (search) HudColors.Panel else HudColors.Field
            search -> HudColors.PanelFocus
            else -> HudColors.FieldFocus
        },
        tween(if (search) HudMotion.SHORT_MS else HudMotion.SWITCH_MS, easing = HudMotion.Ease),
        label = "inputFill",
    )
    val edge by animateColorAsState(
        if (focused) HudColors.Cyan else HudColors.Hairline,
        tween(if (search) HudMotion.SHORT_MS else HudMotion.SWITCH_MS, easing = HudMotion.Ease),
        label = "inputEdge",
    )
    val shape = if (search) HudRadius.RowShape else HudRadius.FieldShape
    BasicTextField(
        value = value,
        onValueChange = onValueChange,
        singleLine = true,
        keyboardOptions = KeyboardOptions(keyboardType = keyboardType, imeAction = imeAction),
        keyboardActions = KeyboardActions(onDone = { onDone() }),
        textStyle = TextStyle(
            color = HudColors.TextPrimary,
            fontFamily = FontFamily.SansSerif,
            fontSize = if (search) 13.sp else 12.sp,
        ),
        cursorBrush = SolidColor(HudColors.Cyan),
        modifier = modifier.onFocusEvent { state -> focused = state.isFocused },
        decorationBox = { inner ->
            Box(
                Modifier.fillMaxWidth()
                    .shadow(
                        if (focused && search) 4.dp else if (search) 8.dp else 0.dp,
                        shape,
                        clip = false,
                        ambientColor = if (focused && search) HudColors.CyanDim else HudColors.Shadow,
                        spotColor = if (focused && search) HudColors.CyanDim else HudColors.Shadow,
                    )
                    .clip(shape)
                    .background(fill)
                    .border(1.dp, edge, shape)
                    .padding(
                        horizontal = if (search) 16.dp else 10.dp,
                        vertical = if (search) 8.dp else 6.dp,
                    ),
            ) {
                if (value.isEmpty() && placeholder.isNotEmpty()) {
                    Text(
                        placeholder,
                        color = HudColors.TextDim,
                        fontSize = if (search) 13.sp else 12.sp,
                    )
                }
                inner()
            }
        },
    )
}

/** The page's own spinner (`#pullRefresh`, `refreshSpin 1s linear`): a cyan ring. */
@Composable
fun ScanProgressRing(progress: Float, modifier: Modifier = Modifier) {
    CircularProgressIndicator(
        progress = { progress },
        modifier = modifier,
        color = HudColors.Cyan,
        trackColor = HudColors.Hairline,
        strokeWidth = 3.dp,
    )
}

/**
 * The ring a sweep shows while it is looking for a dashboard (`.scan-ring`).
 *
 * The sweep has two halves with different information: the subnet walk reports how far it
 * has got, and mDNS plus the 50 ms probes answer with no count at all. So the ring has two
 * forms — an arc that fills while there is a fraction to show, and a turning ring while
 * there is nothing to count. With animation switched off it holds still at a partial arc:
 * the state is "a search is running", and that does not need motion to be understood.
 */
@Composable
fun ScanRing(progress: Float, modifier: Modifier = Modifier) {
    when {
        progress > 0f -> ScanProgressRing(progress, modifier)
        !HudMotion.enabled -> CircularProgressIndicator(
            progress = { 0.35f },
            modifier = modifier,
            color = HudColors.Cyan,
            trackColor = HudColors.Hairline,
            strokeWidth = 3.dp,
        )
        else -> CircularProgressIndicator(
            modifier = modifier,
            color = HudColors.Cyan,
            trackColor = HudColors.Hairline,
            strokeWidth = 3.dp,
        )
    }
}
