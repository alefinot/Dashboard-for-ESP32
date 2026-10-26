package com.alefinot.dashboardpp.ui.map

import org.osmdroid.util.GeoPoint
import kotlin.math.min

/**
 * What a map should show for a set of logged positions — decided without
 * asking osmdroid.
 *
 * The reason this layer exists is a real ANR found on the device the first
 * time a trip was opened. `MapView.zoomToBoundingBox` is not safe with the
 * inputs a Compose `AndroidView` hands it: the update block runs *before* the
 * view has been measured, so width and height are 0. osmdroid 6.1.20 puts that
 * negative viewport into `Math.log` when deriving the zoom
 * (`TileSystem.getLongitudeZoom` / `getLatitudeZoom`), gets NaN, then walks
 * past its own guards — `nextZoom == Double.MIN_VALUE` and
 * `nextZoom > pMaximumZoom` are both false for NaN, and `Math.max(NaN, min)`
 * is NaN — so it builds a `Projection` with an unusable map size and
 * `Projection.getCloserPixel` loops forever adding or subtracting it. The
 * dropbox ANR said exactly that: main thread Runnable in
 * `Projection.getCloserPixel`, called from `MapHolder.applyTrack`,
 * "Input dispatching timed out", 106% user CPU.
 *
 * The second degenerate input is a track with no extent: a stationary vehicle
 * with a live GNSS fix records the same coordinates every second, so the
 * bounding box is a point. That is handled here too instead of relying on
 * osmdroid's single-point special case.
 */
object MapFit {

    /**
     * Zoom used when the track has no extent to fit: a city block, which is
     * where a parked vehicle's single position is still readable.
     */
    const val STATIONARY_ZOOM = 15.0

    /** Below this the box has no extent — about 0.1 mm, far under GPS noise. */
    const val MIN_SPAN_DEG = 1e-9

    /**
     * Positions a map can project at all. A poisoned line in a CSV (out of
     * range, or not a number) must never reach the projection: the reader
     * keeps the row so the chart stays aligned, but a NaN coordinate would
     * poison the whole bounding box.
     */
    fun usable(points: List<GeoPoint>): List<GeoPoint> = points.filter {
        it.latitude.isFinite() && it.longitude.isFinite() &&
            it.latitude >= -90.0 && it.latitude <= 90.0 &&
            it.longitude >= -180.0 && it.longitude <= 180.0
    }

    /**
     * @param center midpoint of the track, always finite and in range.
     * @param zoom zoom to use when [useBoundingBox] is false.
     * @param useBoundingBox true only when `MapView.zoomToBoundingBox` is safe:
     * the view has been measured and the track has an extent on both axes.
     * When false the caller sets center and zoom directly, which keeps us out
     * of the path described above.
     */
    data class Plan(
        val center: GeoPoint,
        val zoom: Double,
        val useBoundingBox: Boolean,
    )

    /**
     * @param width measured width of the map view; 0 before layout.
     * @param height measured height of the map view; 0 before layout.
     * @param maxZoom the map view's own maximum, so the fallback never asks
     * for a zoom the tile source cannot draw.
     * @return null when there is nothing to show.
     */
    fun plan(points: List<GeoPoint>, width: Int, height: Int, maxZoom: Double): Plan? {
        if (points.isEmpty()) return null
        var minLat = points.first().latitude
        var maxLat = minLat
        var minLon = points.first().longitude
        var maxLon = minLon
        for (p in points) {
            if (p.latitude < minLat) minLat = p.latitude
            if (p.latitude > maxLat) maxLat = p.latitude
            if (p.longitude < minLon) minLon = p.longitude
            if (p.longitude > maxLon) maxLon = p.longitude
        }
        val center = GeoPoint((minLat + maxLat) / 2.0, (minLon + maxLon) / 2.0)
        val hasExtent = maxLat - minLat > MIN_SPAN_DEG && maxLon - minLon > MIN_SPAN_DEG
        val measured = width > 0 && height > 0
        return Plan(
            center = center,
            zoom = min(STATIONARY_ZOOM, maxZoom),
            useBoundingBox = hasExtent && measured,
        )
    }
}
