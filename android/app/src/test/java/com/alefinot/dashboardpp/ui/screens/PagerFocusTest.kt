package com.alefinot.dashboardpp.ui.screens

import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

/**
 * One page is at rest, four pages wide (TRIPS, LIVE, WEB, SETTINGS), and the user is
 * on LIVE. The boundary this decides is what the user sees mid-swipe: a page that
 * stops counting too late paints and animates when it is already off screen, and one
 * that counts too late slides in half-drawn.
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
        // The drag has barely started: WEB (2) is being uncovered, so it may already
        // paint and animate. Waiting for the pager to commit is what showed a
        // half-drawn page.
        assertTrue(PagerFocus.inViewport(current = 1, offset = 0.05f, index = 2))
    }

    @Test
    fun `a swipe the other way brings the page behind it in`() {
        assertTrue(PagerFocus.inViewport(current = 1, offset = -0.05f, index = 0))
    }
}
