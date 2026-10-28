package com.alefinot.dashboardpp.ui.screens

import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.dp
import com.alefinot.dashboardpp.ui.components.HudIcons

/**
 * The modes of the shell: the vehicle page, the native live view, the ESP's own
 * configuration (its Web UI, labelled for what it configures), recorded trips, and
 * this app's own settings.
 *
 * The vehicle page is first-class now because it is where a connection is made. Before
 * it existed, every page assumed a device was already there and the app opened on the
 * live view; a dashboard that was not reachable left the whole app with one sentence and
 * no way out except an address box. So the bar has a slot for it, the pager has a page
 * for it, and the app opens there.
 */
enum class AppTab { LIVE, TRIPS, VEHICLES, WEB, SETTINGS }

/**
 * The bottom bar and the pager, as data.
 *
 * This used to be two lists — `BarSlots` for the row of icons, `BarPages` for the swipe
 * order — and they were allowed to differ, because the bar carried a Coming Soon slot
 * with no screen behind it and putting that in the pager would make the swipe pass
 * through a hole. Two lists plus an exception is how an empty centre survives a redesign:
 * the middle of the bar held a gauge, and the app's most important decision (which
 * vehicle am I connected to?) had no slot at all.
 *
 * So: one list, in one order, used for both. Index 2 is the middle of five, and the
 * middle of this bar has always been the thing the app is mostly in.
 */
object BarLayout {

    /** Left to right. Dragging a finger left walks right along the bar and the two agree. */
    val pages: List<AppTab> =
        listOf(AppTab.LIVE, AppTab.TRIPS, AppTab.VEHICLES, AppTab.WEB, AppTab.SETTINGS)

    /** The middle slot — the one drawn larger, and the one the glass sheet parks on. */
    val centre: AppTab = AppTab.VEHICLES

    /** The page the app opens on. It is the centre, but that is a choice, not a law. */
    val home: AppTab = AppTab.VEHICLES

    /** [home]'s index in [pages], which is the pager's `initialPage`. */
    val homePage: Int = pages.indexOf(home)

    /** The pager index for a bar slot. Every slot has one; there are no dead slots. */
    fun pageOf(tab: AppTab): Int = pages.indexOf(tab)

    /**
     * How a slot is drawn: the centre is larger and heavier, and the rest are tinted
     * rather than painted, so a dimmed slot (a trips list with no store behind it, an
     * ESP slot with no device in it) reads as unusable instead of merely quieter.
     */
    data class Glyph(
        val desc: String,
        val size: Dp,
        val box: Dp,
        val weight: Float,
        val colored: Boolean,
    )

    fun glyph(tab: AppTab): Glyph = when (tab) {
        AppTab.VEHICLES -> Glyph("Vehicle", 30.dp, 52.dp, 1.4f, true)
        AppTab.LIVE -> Glyph("Live", 22.dp, 48.dp, 1f, false)
        AppTab.TRIPS -> Glyph("Trips", 22.dp, 48.dp, 1f, false)
        AppTab.WEB -> Glyph("ESP settings", 22.dp, 48.dp, 1f, false)
        AppTab.SETTINGS -> Glyph("App settings", 22.dp, 48.dp, 1f, false)
    }

    fun icon(tab: AppTab): ImageVector = when (tab) {
        AppTab.LIVE -> HudIcons.Gauge
        AppTab.TRIPS -> HudIcons.Pin
        AppTab.VEHICLES -> HudIcons.Car
        AppTab.WEB -> HudIcons.Chip
        AppTab.SETTINGS -> HudIcons.Cog
    }
}
