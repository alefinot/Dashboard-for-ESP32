package com.alefinot.dashboardpp.trips

import java.io.File
import java.io.FileWriter
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale
import java.util.TimeZone

/**
 * GPX 1.1 export of a trip's route (trip recording plan §6). Only samples with a
 * valid position become track points; `ele` is written when the fix carried
 * altitude, and speed goes in `extensions` in m/s, the unit GPX consumers expect
 * there — the CSV remains the complete record, this is the shareable route.
 */
object GpxWriter {
    private val iso = SimpleDateFormat("yyyy-MM-dd'T'HH:mm:ss'Z'", Locale.US).apply {
        timeZone = TimeZone.getTimeZone("UTC")
    }

    fun write(target: File, samples: List<TripSample>, name: String) {
        target.parentFile?.mkdirs()
        FileWriter(target).use { it.write(gpx(samples, name)) }
    }

    fun gpx(samples: List<TripSample>, name: String): String {
        val pts = samples.filter { it.snapshot.hasPosition }
        return buildString(1024) {
            append("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n")
            append("<gpx version=\"1.1\" creator=\"Dashboard++\" ")
            append("xmlns=\"http://www.topografix.com/GPX/1/1\">\n")
            append("  <trk>\n")
            append("    <name>").append(escape(name)).append("</name>\n")
            append("    <trkseg>\n")
            pts.forEach { p ->
                append("      <trkpt lat=").append('"').append(p.snapshot.lat)
                append('"').append(" lon=").append('"').append(p.snapshot.lon)
                append('"').append(">\n")
                if (p.snapshot.alt >= 0.0) {
                    append("        <ele>").append(p.snapshot.alt).append("</ele>\n")
                }
                append("        <time>").append(iso.format(Date(p.tsMs))).append("</time>\n")
                if (p.snapshot.spd > 0.0) {
                    append("        <extensions><speed>")
                    append(String.format(Locale.US, "%.2f", p.snapshot.spd / 3.6))
                    append("</speed></extensions>\n")
                }
                append("      </trkpt>\n")
            }
            append("    </trkseg>\n")
            append("  </trk>\n")
            append("</gpx>\n")
        }
    }

    private fun escape(text: String): String = text
        .replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;")
        .replace("\"", "&quot;").replace("'", "&apos;")
}
