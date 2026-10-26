package com.alefinot.dashboardpp.ui.theme

import androidx.compose.ui.graphics.Color

/**
 * The palette of the ESP's own Web UI, copied from the CSS custom properties in
 * `src/webui.html` (`--bg-base`, `--bg-card`, `--border-color`, `--accent-*`,
 * `--text-*`). The app and the page are meant to read as one product, so the
 * values come from the page rather than from a matching guess.
 *
 * Keep this in step with `webui.html`: when a token changes there, it changes
 * here in the same commit (the line numbers below are where each was read).
 */
object HudColors {
    /** `--bg-base` (#000) - the page body, and now the app's background too. */
    val Bg = Color(0xFF000000)
    val BgAlt = Color(0xFF000000)

    /**
     * `--bg-card` rgba(16,22,34,.75) - the frosted sheet the page puts on its
     * overlays: `#searchBar`, `.confirm-dialog`, `.ip-banner`. It is not the fill
     * of `.card` (see `CardFill` below): the page has two different fills, and
     * only the overlays carry `backdrop-filter: blur(12px)` plus `--card-shadow`.
     * The blur sits on a flat black body, so what is actually visible is the
     * translucency, the hairline and the shadow; `Modifier.hudGlass` reproduces
     * those three, which is what matches - and it works below API 31, where a
     * real blur would not.
     */
    val Panel = Color(0xBF101622)
    /** `.card` rgba(22,30,46,.6) - the surface the page builds most of itself from. */
    val CardFill = Color(0x99161E2E)
    /** `.card:hover` rgba(26,36,56,.8). */
    val CardFillHover = Color(0xCC1A2438)
    /** `.acc-collapser` #131c28 - the option list, opaque by the page's own decision. */
    val Sheet = Color(0xFF131C28)
    /** `details summary` rgba(18,25,38,.6) - a panel's header band. */
    val PanelHead = Color(0x99121926)
    /** `#searchBar:focus` rgba(20,28,45,.9) - a search field while it has focus. */
    val PanelFocus = Color(0xE6141C2D)
    /** `--bg-card-hover` rgba(23,31,48,.85) - what a card looks like under a pointer. */
    val CardHover = Color(0xD9171F30)
    /** `#msg:not(:empty)` rgba(15,23,42,.6) - the status strip's fill. */
    val Msg = Color(0x990F172A)
    /** `.perf-card` rgba(18,25,38,.8) - a tile inside a card. */
    val CardInner = Color(0xCC121926)
    /** `--bg-input` #121824 - the fill of `input[type=text|number]` and `select`. */
    val Field = Color(0xFF121824)
    /** `input:focus` background #182030 - the same field while it has focus. */
    val FieldFocus = Color(0xFF182030)
    /** `#confirm-overlay` rgba(4,7,13,.75) - behind a modal dialog. */
    val Scrim = Color(0xBF04070D)

    /** `--border-color` rgba(255,255,255,.08) - the hairline on overlays and fields. */
    val Hairline = Color(0x14FFFFFF)
    /** `.card` border rgba(255,255,255,.05) - a card's hairline is fainter than that. */
    val CardHairline = Color(0x0DFFFFFF)
    /** `.card:hover` border rgba(255,255,255,.12). */
    val CardHairlineHover = Color(0x1FFFFFFF)
    /** `details[open]` border - a panel that is active or focused. */
    val HairlineActive = Color(0x4000E5FF)
    /** The 1px inset top highlight that makes a flat fill read as a glass sheet. */
    val InsetTop = Color(0x0FFFFFFF)
    /** `--card-shadow` rgba(0,0,0,.4) - Compose takes an alpha, not a blur radius. */
    val Shadow = Color(0x66000000)

    /** `--accent-cyan` */
    val Cyan = Color(0xFF00E5FF)
    /** rgba(0,229,255,.3) - the quiet cyan of borders, tracks and unselected chrome. */
    val CyanDim = Color(0x4D00E5FF)
    /** `--accent-blue` */
    val Blue = Color(0xFF2979FF)
    /** `--accent-emerald` / `--accent-amber` / `--accent-rose` */
    val Green = Color(0xFF00E676)
    val Amber = Color(0xFFFF9100)
    val Red = Color(0xFFFF1744)
    /** `rgba(255,23,68,.08)` - a warning strip's fill. */
    val RedDim = Color(0x14FF1744)

    /** `--text-main` / `--text-muted` / `--text-dim` */
    val TextPrimary = Color(0xFFF1F5F9)
    val TextMuted = Color(0xFF94A3B8)
    val TextDim = Color(0xFF64748B)
    val White = Color(0xFFFFFFFF)
}
