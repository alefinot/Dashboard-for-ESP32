package com.alefinot.dashboardpp.ui.screens

import android.view.MotionEvent
import android.view.View
import androidx.compose.ui.ExperimentalComposeUiApi
import androidx.compose.ui.Modifier
import androidx.compose.ui.input.pointer.motionEventSpy
import kotlin.math.abs

/**
 * Hands a pager page's gestures back to the Web view that is *behind* the pager.
 *
 * The ESP page is not drawn by the pager: the live View is kept under the pages so it
 * is never disposed and never rebuilt (that is what makes coming back to the tab a
 * repaint instead of a fetch, see `WebUiHolder`). The cost nobody noticed until it was
 * on a real tablet: a pager claims every gesture that starts inside it, and Compose
 * stops dispatching at the claim - so the page underneath never saw a touch. The tab
 * looked alive and was deaf.
 *
 * Two fixes were tried on the device and rejected:
 *  - putting the View into the pager's own page slot: taps work there, but moving the
 *    View re-parents it, Chromium drops its surface, and the reveal opens on a black
 *    frame (filmed: one frame at mean 0.7 against the page's 11.8) - the exact fault
 *    1.5 fixed;
 *  - switching the pager off on that page (`userScrollEnabled = false`): the page is
 *    still deaf, because the claim happens regardless, and the swipe away is lost with it.
 *
 * So the gestures are mirrored. `motionEventSpy` sees the real MotionEvent stream
 * before Compose routes it, and the same stream - shifted into the View's own
 * coordinate space - is handed to the View. Compose routing is untouched, so the pager
 * keeps its swipe; the web page gets taps, scrolls and pinch-zoom. A horizontal drag
 * past touch slop is the pager's, and the page is then sent an ACTION_CANCEL, which is
 * exactly what an Android ViewGroup does when it gives a gesture up to its parent.
 */
internal class GestureMirror(private val target: () -> View?, private val slopPx: Float) {
    private enum class Mode { Idle, Forward, GivenToPager }

    private var mode = Mode.Idle
    private var transX = 0f
    private var transY = 0f
    private var startX = 0f
    private var startY = 0f

    fun on(e: MotionEvent) {
        val v = target() ?: return
        when (e.actionMasked) {
            MotionEvent.ACTION_DOWN -> {
                val at = IntArray(2).also { v.getLocationOnScreen(it) }
                // The spy is asked about events Compose will route anywhere in this
                // node's area, and possibly about ones beside it - so do the hit test
                // the View would have done: a press outside this View is not its event.
                if (e.rawX < at[0] || e.rawX >= at[0] + v.width ||
                    e.rawY < at[1] || e.rawY >= at[1] + v.height
                ) {
                    mode = Mode.Idle
                    return
                }
                // One translation for the whole gesture: neither the ComposeView nor
                // this View moves underneath a finger (the pages slide, the View under
                // them does not), and raw coordinates plus the View's position on the
                // screen put both in the same frame.
                transX = (e.rawX - e.x) - at[0]
                transY = (e.rawY - e.y) - at[1]
                startX = e.x
                startY = e.y
                mode = Mode.Forward
                send(v, e)
            }

            MotionEvent.ACTION_MOVE -> {
                if (mode != Mode.Forward) return
                val dx = e.x - startX
                val dy = e.y - startY
                // A one-finger horizontal pull is the pager's page change; a vertical
                // one, or a second finger (pinch), is the web page's.
                if (e.pointerCount == 1 && abs(dx) > slopPx && abs(dx) > abs(dy)) {
                    mode = Mode.GivenToPager
                    send(v, e, MotionEvent.ACTION_CANCEL)
                    return
                }
                send(v, e)
            }

            MotionEvent.ACTION_POINTER_DOWN, MotionEvent.ACTION_POINTER_UP ->
                if (mode == Mode.Forward) send(v, e)

            MotionEvent.ACTION_UP ->
                if (mode == Mode.Forward) {
                    mode = Mode.Idle
                    send(v, e)
                }

            MotionEvent.ACTION_CANCEL ->
                if (mode != Mode.Idle) {
                    mode = Mode.Idle
                    send(v, e, MotionEvent.ACTION_CANCEL)
                }
        }
    }

    private fun send(v: View, e: MotionEvent, asAction: Int = -1) {
        val copy = MotionEvent.obtain(e)
        if (asAction >= 0) copy.action = asAction
        copy.offsetLocation(transX, transY)
        v.dispatchTouchEvent(copy)
        copy.recycle()
    }
}

/**
 * Mirror this node's touches onto the Web page behind the pager - while [active] says
 * this page is the settled one *and* is actually on screen. The spy is attached to a
 * page that exists off screen too, and without that gate a swipe over the Live tab
 * would be served to a page the user cannot see; the same goes for the cover that
 * stands in front of a page that has not loaded yet, which takes the taps itself.
 */
@OptIn(ExperimentalComposeUiApi::class)
internal fun Modifier.webUiGestureMirror(mirror: GestureMirror, active: () -> Boolean): Modifier =
    motionEventSpy { ev -> if (active()) mirror.on(ev) }
