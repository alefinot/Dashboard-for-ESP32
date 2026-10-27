package com.alefinot.dashboardpp.ui.screens

/**
 * Which pages of the shell's pager are in play right now.
 *
 * The shell asks this question in three places, and the answer has to be the same in
 * all three or the page behaves inconsistently:
 *
 * - the ESP page may paint only while it is in view (app 21: keeping it composed but
 *   stopped is what removed the idle jank without bringing back the black hole),
 * - its gesture stream is mirrored onto the web view only while it is in view,
 * - and a page's entrance runs as it comes in, which is the same event.
 *
 * "In view" is deliberately generous: a page counts from the moment the drag starts to
 * uncover it, not when the pager commits to it. Measured in app 21, waiting for the
 * commit shows a half-drawn page sliding in. The threshold is a hair under a whole
 * page so that the page the pager is *leaving* still counts (it is on screen for the
 * whole of the drag) while the one a full page away does not.
 */
object PagerFocus {

    /**
     * @param current the pager's settled page index
     * @param offset the fraction the pages have travelled since (positive towards
     * higher indices)
     * @param index the page being asked about
     */
    fun inViewport(current: Int, offset: Float, index: Int): Boolean =
        // How far this page is from where the pager is, in pages. Zero at rest on it,
        // and it drops below one page the moment a drag starts uncovering it - which
        // is the whole point of the generous threshold: the page being left (the one
        // at distance ~1 going the other way) stays in view for the whole drag.
        kotlin.math.abs(current + offset - index) < 0.999f
}
