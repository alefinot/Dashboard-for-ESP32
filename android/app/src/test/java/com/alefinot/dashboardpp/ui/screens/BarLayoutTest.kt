package com.alefinot.dashboardpp.ui.screens

import androidx.compose.ui.unit.dp
import com.alefinot.dashboardpp.ui.components.HudIcons
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

/**
 * The bar and the pager, as data instead of as a row of composables.
 *
 * The order of the bar used to be written twice: once as `BarSlots` for the row of
 * icons and once as `BarPages` for the swipe order, with a comment explaining why they
 * differed (the Coming Soon slot advertised a screen that did not exist, so it had to
 * stay out of the pager). Two lists and a carve-out is how a hole in the middle of the
 * app becomes invisible: the centre of the bar was a gauge, and the page it sat on was
 * four slots away.
 *
 * One list now, and these tests are what keep it that way — that the centre slot is the
 * vehicle page, that the app opens on it, and that every slot is a page.
 */
class BarLayoutTest {

    @Test
    fun `the bar and the swipe order are one list, and nothing is missing from it`() {
        assertEquals(
            listOf(AppTab.LIVE, AppTab.TRIPS, AppTab.VEHICLES, AppTab.WEB, AppTab.SETTINGS),
            BarLayout.pages,
        )
        // Five slots, five pages. The old bar had a slot with no page behind it; a slot
        // you can tap that goes nowhere is a dead end wearing an icon.
        assertEquals(BarLayout.pages.size, AppTab.values().size)
        for (tab in AppTab.values()) {
            assertTrue("$tab", BarLayout.pages.contains(tab))
        }
    }

    @Test
    fun `the vehicle page is the centre slot and the page the app opens on`() {
        // Five slots means index 2 is the middle, and the middle of this bar has always
        // been the thing the app is mostly in. That argument now belongs to the garage:
        // it is where a connection is made, and every other page needs it.
        assertEquals(AppTab.VEHICLES, BarLayout.centre)
        assertEquals(2, BarLayout.pages.indexOf(BarLayout.centre))
        assertEquals(BarLayout.centre, BarLayout.home)
        assertEquals(2, BarLayout.homePage)
    }

    @Test
    fun `the vehicle glyph takes the centre treatment and nothing else does`() {
        val car = BarLayout.glyph(AppTab.VEHICLES)
        assertEquals(1.4f, car.weight, 0f)
        assertEquals(30.dp, car.size)
        assertEquals(52.dp, car.box)
        assertTrue(car.colored)
        for (tab in BarLayout.pages.filter { it != AppTab.VEHICLES }) {
            val g = BarLayout.glyph(tab)
            assertEquals("$tab weight", 1f, g.weight, 0f)
            assertEquals("$tab size", 22.dp, g.size)
            assertEquals("$tab box", 48.dp, g.box)
            // `colored` means "paint the artwork as drawn and lose alpha instead", which
            // was the gauge's privilege while it sat in the centre. Tinted like its
            // neighbours is what lets a dimmed ESP slot read as unusable next to it.
            assertFalse("$tab colored", g.colored)
        }
    }

    @Test
    fun `every slot keeps its name as the accessibility description`() {
        // The bar replaced words with icons, so the words have to survive somewhere a
        // screen reader can reach, and a touch target that reads as "unlabelled" is a
        // regression nobody would notice until it was reported.
        for (tab in AppTab.values()) {
            assertTrue("$tab", BarLayout.glyph(tab).desc.isNotBlank())
        }
    }

    @Test
    fun `the car glyph is drawn on the same grid as the others`() {
        // Same 24-unit viewport, same stroke language: a borrowed icon from another set
        // would be a second visual language in a bar that already has one.
        assertEquals(24f, HudIcons.Car.viewportWidth, 0f)
        assertEquals(24f, HudIcons.Car.viewportHeight, 0f)
    }
}
