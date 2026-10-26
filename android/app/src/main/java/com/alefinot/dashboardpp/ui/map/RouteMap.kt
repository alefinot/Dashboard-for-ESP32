package com.alefinot.dashboardpp.ui.map

import android.content.Context
import android.graphics.Color as AndroidColor
import android.graphics.drawable.GradientDrawable
import android.view.ViewTreeObserver
import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.remember
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.toArgb
import androidx.compose.ui.viewinterop.AndroidView
import com.alefinot.dashboardpp.ui.theme.HudColors
import org.osmdroid.config.Configuration
import org.osmdroid.tileprovider.tilesource.TileSourceFactory
import org.osmdroid.util.BoundingBox
import org.osmdroid.util.GeoPoint
import org.osmdroid.views.MapView
import org.osmdroid.views.overlay.Marker
import org.osmdroid.views.overlay.Polyline
import java.io.File

/**
 * osmdroid needs one global setup before any MapView exists: where tiles are
 * cached, and a User-Agent that identifies us. The tile policy at
 * openstreetmap.org/robots requires a real contact in the UA header — anonymous
 * or generic agents get throttled or blocked — so the app's own repository is
 * named there. The cache goes under the app's external files dir rather than
 * osmdroid's default so it is removed cleanly on uninstall.
 */
object OsmdroidSetup {
    @Volatile
    private var done = false

    const val USER_AGENT =
        "Dashboard++forESP32/1.1.0 (Android; https://github.com/alefinot/Dashboard-for-ESP32)"

    fun ensure(context: Context) {
        if (done) return
        synchronized(this) {
            if (done) return
            val base = context.applicationContext.getExternalFilesDir(null)
                ?: context.applicationContext.filesDir
            val cache = File(base, "osmtiles")
            if (!cache.exists()) cache.mkdirs()
            Configuration.getInstance().apply {
                // Base path first: its getter falls back to asking a Context
                // that osmdroid only knows after Configuration.load(), which we
                // never call — so leaving it unset logs a caught NPE on every
                // MapView construction.
                setOsmdroidBasePath(base)
                // Explicit setters: the getter for the tile cache is overloaded
                // (with and without a Context), so the synthetic property syntax
                // would not resolve.
                setOsmdroidTileCache(cache)
                setUserAgentValue(USER_AGENT)
            }
            done = true
        }
    }
}

/**
 * The logged track drawn over OSM raster tiles.
 *
 * Degradation is deliberate (plan §6): if tiles cannot be fetched — no data
 * connection, tile host blocked, cache empty — osmdroid paints its empty canvas
 * and the vector track, start and end markers still render. The route is our
 * recorded data, not the tile server's, so an offline phone still sees the shape
 * of the drive; only the basemap is missing.
 */
@Composable
fun RouteMap(points: List<GeoPoint>, modifier: Modifier = Modifier) {
    val context = androidx.compose.ui.platform.LocalContext.current
    OsmdroidSetup.ensure(context)
    val holder = remember { MapHolder() }
    AndroidView(
        modifier = modifier,
        factory = { ctx -> holder.build(ctx) },
        update = { map -> holder.applyTrack(map, points) },
    )
    DisposableEffect(Unit) {
        onDispose { holder.release() }
    }
}

private class MapHolder {
    private var map: MapView? = null
    private var layoutListener: ViewTreeObserver.OnGlobalLayoutListener? = null

    /**
     * Framing is applied once per point set and viewport size. Re-fitting on
     * every recomposition would yank the viewport back while the user is
     * panning around the route.
     */
    private var fitted: Long? = null

    /** Filtered track, kept so framing can run after the first layout. */
    private var track: List<GeoPoint> = emptyList()

    fun build(context: Context): MapView {
        val view = MapView(context)
        view.setTileSource(TileSourceFactory.MAPNIK)
        view.setMultiTouchControls(true)
        // Data connection on: tiles are a bonus, never a dependency (see above).
        view.setUseDataConnection(true)
        view.setTilesScaledToDpi(true)
        // OSM's own maximum for the standard layer is z19; below z4 the whole
        // world is useless for a drive, so stop the pinch there.
        view.setMaxZoomLevel(19.0)
        view.setMinZoomLevel(4.0)
        map = view
        return view
    }

    /**
     * The track is drawn as soon as the view exists; framing waits for a real
     * size, because a `AndroidView` update block runs before layout and framing
     * an unmeasured map is not merely wrong — see [MapFit].
     */
    fun applyTrack(view: MapView, points: List<GeoPoint>) {
        track = MapFit.usable(points)
        drawTrack(view)
        if (view.width > 0 && view.height > 0) fit(view) else awaitSize(view)
    }

    private fun awaitSize(view: MapView) {
        if (layoutListener != null) return
        val observer = view.viewTreeObserver
        val listener = object : ViewTreeObserver.OnGlobalLayoutListener {
            override fun onGlobalLayout() {
                if (view.width > 0 && view.height > 0) {
                    detachListener(view)
                    fit(view)
                }
            }
        }
        layoutListener = listener
        if (observer.isAlive) observer.addOnGlobalLayoutListener(listener)
    }

    private fun detachListener(view: MapView) {
        layoutListener?.let { l ->
            val observer = view.viewTreeObserver
            if (observer.isAlive) observer.removeOnGlobalLayoutListener(l)
        }
        layoutListener = null
    }

    private fun drawTrack(view: MapView) {
        val manager = view.getOverlayManager()
        manager.clear()
        if (track.isEmpty()) return

        val density = view.resources.displayMetrics.density
        val line = Polyline()
        line.getPoints().addAll(track)
        line.setColor(HudColors.Cyan.toArgb())
        line.setWidth(5f * density)
        line.getPaint().setStrokeCap(android.graphics.Paint.Cap.ROUND)
        line.getPaint().setStrokeJoin(android.graphics.Paint.Join.ROUND)
        manager.add(line)

        if (track.size >= 2) {
            manager.add(endMarker(view, track.first(), HudColors.Green.toArgb(), "Trip start"))
            manager.add(endMarker(view, track.last(), HudColors.Amber.toArgb(), "Trip end"))
        } else {
            manager.add(endMarker(view, track.first(), HudColors.Green.toArgb(), "Position"))
        }
    }

    private fun fit(view: MapView) {
        val plan = MapFit.plan(track, view.width, view.height, view.maxZoomLevel) ?: return
        val key = fitKey(view)
        if (key == fitted) return
        fitted = key
        if (plan.useBoundingBox) {
            val density = view.resources.displayMetrics.density
            // strokeSize is the pixel padding kept around the track so the start
            // and end markers are never flush with the map edge.
            view.zoomToBoundingBox(
                BoundingBox.fromGeoPoints(track),
                true,
                (28 * density).toInt(),
            )
        } else {
            // A single position, or a map that has no size to fit into: point
            // the camera at it and leave the zoom decision to us.
            view.controller.setZoom(plan.zoom)
            view.controller.setCenter(plan.center)
        }
        view.invalidate()
    }

    /** Identity of "this track in this viewport", used to fit exactly once. */
    private fun fitKey(view: MapView): Long {
        var h = (view.width.toLong() shl 20) xor view.height.toLong()
        h = h * 31 + track.size
        if (track.isNotEmpty()) {
            h = h * 31 + track.first().latitude.hashCode()
            h = h * 31 + track.last().longitude.hashCode()
        }
        return h
    }

    private fun endMarker(view: MapView, point: GeoPoint, color: Int, title: String): Marker {
        val marker = Marker(view, view.context)
        marker.setPosition(point)
        marker.setTitle(title)
        marker.setAnchor(Marker.ANCHOR_CENTER, Marker.ANCHOR_CENTER)
        marker.setIcon(dot(view.resources.displayMetrics.density, color))
        return marker
    }

    /**
     * Marker drawn in code rather than loaded from a drawable resource: a filled
     * disc with a dark ring, which is both legible on light and dark basemaps and
     * consistent with the HUD palette.
     */
    private fun dot(density: Float, color: Int): GradientDrawable {
        val size = (18f * density).toInt()
        return GradientDrawable().apply {
            shape = GradientDrawable.OVAL
            setColor(color)
            setStroke((2f * density).toInt(), AndroidColor.BLACK)
            setSize(size, size)
        }
    }

    fun release() {
        map?.run {
            detachListener(this)
            onPause()
            onDetach()
        }
        map = null
        track = emptyList()
        fitted = null
    }
}
