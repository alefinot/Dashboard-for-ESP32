package com.alefinot.dashboardpp.ui.map

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test
import org.osmdroid.util.GeoPoint

/**
 * Framing is pure arithmetic, so the degenerate cases that cost the app an ANR
 * on the device are testable here: an unmeasured view (width and height 0, which
 * is what Compose hands the AndroidView update block) and a track with no
 * extent. Both must resolve to a center plus a fixed zoom and must never be
 * handed to MapView.zoomToBoundingBox.
 */
class MapFitTest {

    private val munich = listOf(
        GeoPoint(48.137097, 11.574778),
        GeoPoint(48.137280, 11.574910),
        GeoPoint(48.137462, 11.575030),
    )

    @Test
    fun `nothing to plan for an empty track`() {
        assertNull(MapFit.plan(emptyList(), 1200, 800, 19.0))
    }

    @Test
    fun `an unmeasured view is never framed with a bounding box`() {
        // This is the ANR path: 0 width means osmdroid computes its zoom with
        // Math.log of a negative ratio, gets NaN and loops in getCloserPixel.
        val plan = MapFit.plan(munich, 0, 0, 19.0)!!
        assertFalse(plan.useBoundingBox)
        assertTrue(plan.zoom.isFinite())
        assertTrue(plan.zoom <= 19.0)
    }

    @Test
    fun `a stationary track has no extent to fit`() {
        // Demo mode, or a car at a traffic light with a live fix: every sample
        // carries the same coordinates.
        val still = List(120) { GeoPoint(48.136857, 11.576125) }
        val plan = MapFit.plan(still, 1200, 800, 19.0)!!
        assertFalse(plan.useBoundingBox)
        assertEquals(48.136857, plan.center.latitude, 1e-9)
        assertEquals(11.576125, plan.center.longitude, 1e-9)
    }

    @Test
    fun `a line that has no width in one axis cannot be fitted either`() {
        val straight = listOf(GeoPoint(48.137, 11.575), GeoPoint(48.142, 11.575))
        assertFalse(MapFit.plan(straight, 1200, 800, 19.0)!!.useBoundingBox)
    }

    @Test
    fun `a real track in a measured view is fitted normally`() {
        val plan = MapFit.plan(munich, 1200, 800, 19.0)!!
        assertTrue(plan.useBoundingBox)
        assertEquals(48.1372795, plan.center.latitude, 1e-6)
        assertEquals(11.574904, plan.center.longitude, 1e-6)
    }

    @Test
    fun `the fallback zoom respects the tile source maximum`() {
        val still = listOf(GeoPoint(48.136857, 11.576125))
        assertEquals(12.0, MapFit.plan(still, 1200, 800, 12.0)!!.zoom, 1e-9)
        assertEquals(
            MapFit.STATIONARY_ZOOM,
            MapFit.plan(still, 1200, 800, 19.0)!!.zoom,
            1e-9,
        )
    }

    @Test
    fun `poisoned coordinates are dropped before projection`() {
        val bad = listOf(
            munich.first(),
            GeoPoint(Double.NaN, 11.57),
            GeoPoint(91.0, 11.57),
            GeoPoint(48.13, Double.POSITIVE_INFINITY),
            GeoPoint(48.13, -181.0),
            munich.last(),
        )
        val kept = MapFit.usable(bad)
        assertEquals(2, kept.size)
        assertEquals(48.137097, kept.first().latitude, 1e-9)
        assertEquals(48.137462, kept.last().latitude, 1e-9)
    }

    @Test
    fun `poisoned coordinates alone leave nothing to show`() {
        val kept = MapFit.usable(listOf(GeoPoint(Double.NaN, Double.NaN)))
        assertNull(MapFit.plan(kept, 1200, 800, 19.0))
    }
}
