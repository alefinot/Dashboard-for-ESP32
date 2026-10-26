package com.alefinot.dashboardpp.ui.theme

import androidx.compose.material3.MaterialTheme
import androidx.compose.runtime.Composable
import androidx.compose.ui.graphics.Color

/**
 * Material's colour scheme mapped from the Web UI's tokens. `surface` is the glass
 * colour at full opacity: a Material surface must be opaque (it sits under text
 * and ripple), while the app's own panels use the translucent `Panel` through
 * `Modifier.hudGlass`.
 */
private val HudeDarkColorScheme = androidx.compose.material3.darkColorScheme(
    primary = HudColors.Cyan,
    onPrimary = HudColors.Bg,
    secondary = HudColors.CyanDim,
    onSecondary = HudColors.TextPrimary,
    error = HudColors.Red,
    onError = HudColors.Bg,
    background = HudColors.Bg,
    onBackground = HudColors.TextPrimary,
    surface = Color(0xFF101622),
    onSurface = HudColors.TextPrimary,
    onSurfaceVariant = HudColors.TextMuted,
    outline = HudColors.TextMuted,
    outlineVariant = HudColors.Hairline,
)

/**
 * The page has no ripple anywhere: pressing a `button` scales it to 0.98 and its
 * fill brightens. The app copies that (see HudButton) instead of putting a ripple
 * into the same control, so there is no ripple theme to set - Material's default
 * indication is never reached, because the components pass `indication = null`.
 */
@Composable
fun HudTheme(content: @Composable () -> Unit) {
    MaterialTheme(
        colorScheme = HudeDarkColorScheme,
        typography = HudeTypography(),
        content = content,
    )
}
