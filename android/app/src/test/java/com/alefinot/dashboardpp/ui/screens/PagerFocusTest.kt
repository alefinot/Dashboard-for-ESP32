package com.alefinot.dashboardpp.ui.screens

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

/**
 * One page is at rest in a row of five (LIVE, TRIPS, VEHICLES, WEB, SETTINGS), and the
 * user may be on any of them. The boundary this decides is what the user sees mid-swipe:
 * a page that stops counting too late paints and animates when it is already off screen,
 * and one that starts counting too late slides in half-drawn.
 *
 * The two tests at the end are about the row itself rather than the rule: the bar and the
 * pager used to be separate lists with a slot that existed in one and not the other, so
 * "which index is this slot" was a question with two answers.
 */
class PagerFocusTest {

    @Test
    fun `the settled page is in view`() {
        assertTrue(PagerFocus.inViewport(current = 1, offset = 0f, index = 1))
    }

    @Test
    fun `a page a full page away is not in view`() {
        // At rest, only the settled page counts: what is a whole page to either side
        // is off screen and must not paint, poll or animate.
        assertFalse(PagerFocus.inViewport(current = 1, offset = 0f, index = 2))
        assertFalse(PagerFocus.inViewport(current = 1, offset = 0f, index = 0))
        assertFalse(PagerFocus.inViewport(current = 1, offset = 0f, index = 3))
    }

    @Test
    fun `the first sliver of a swipe brings the next page in`() {
        // The drag has barely started: the page to the right is being uncovered, so it
        // may already paint and animate. Waiting for the pager to commit is what showed a
        // half-drawn page.
        assertTrue(PagerFocus.inViewport(current = 1, offset = 0.05f, index = 2))
    }

    @Test
    fun `a swipe the other way brings the page behind it in`() {
        assertTrue(PagerFocus.inViewport(current = 1, offset = -0.05f, index = 0))
    }

    @Test
    fun `every slot of the bar is one pager index away from its neighbour`() {
        // The bar and the swipe order are the same list now, so a slot cannot be drawn
        // in one place and paged to from another. Coming Soon used to sit in the bar with
        // no page behind it, which is how the middle of the app ended up empty.
        assertEquals(AppTab.values().size, BarLayout.pages.size)
        for ((i, tab) in BarLayout.pages.withIndex()) {
            assertEquals("$tab sits at $i", i, BarLayout.pageOf(tab))
        }
    }

    @Test
    fun `from the vehicle page a swipe uncovers its neighbours and nothing past them`() {
        // The app opens on the centre slot, so this is the swipe everyone makes first.
        // The two pages beside the garage come in at the first sliver, and the ends of
        // the bar stay out until the pager gets closer to them.
        val home = BarLayout.homePage
        assertEquals(2, home)
        assertTrue(PagerFocus.inViewport(current = home, offset = 0f, index = home))
        assertTrue(PagerFocus.inViewport(current = home, offset = 0.05f, index = home + 1))
        assertTrue(PagerFocus.inViewport(current = home, offset = -0.05f, index = home - 1))
        assertFalse(PagerFocus.inViewport(current = home, offset = 0f, index = home + 1))
        assertFalse(PagerFocus.inViewport(current = home, offset = 0f, index = home - 1))
        assertFalse(PagerFocus.inViewport(current = home, offset = 0f, index = 0))
        assertFalse(PagerFocus.inViewport(current = home, offset = 0f, index = 4))
    }
}
