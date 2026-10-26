package com.alefinot.dashboardpp.trips

import android.content.Context
import android.content.Intent
import androidx.core.content.FileProvider
import java.io.File

/**
 * Putting a trip where the share sheet can reach it (trip recording plan §6).
 * Exports live under cacheDir and are handed out through FileProvider with a
 * read-only content URI — external files never leave the app's own directory,
 * and the OS clears the cache when it needs the space.
 */
class TripExport(context: Context) {
    private val appContext = context.applicationContext
    private val authority = "${appContext.packageName}.fileprovider"

    private fun exportDir(): File {
        val d = File(appContext.cacheDir, "exports")
        if (!d.exists()) d.mkdirs()
        return d
    }

    /** Copy the CSV out of the trip folder; returns the exported file. */
    fun csv(trip: File): File {
        val out = File(exportDir(), trip.name)
        trip.inputStream().use { input -> out.outputStream().use { input.copyTo(it) } }
        return out
    }

    /** GPX built from the trip's fixed positions. */
    fun gpx(trip: File): File {
        val name = trip.name.removeSuffix(".csv")
        val out = File(exportDir(), "$name.gpx")
        GpxWriter.write(out, TripReader.samples(trip.absolutePath), name)
        return out
    }

    /** Drop old exports; the cache is not the archive, the CSV in trips/ is. */
    fun prune(maxBytes: Long = 32L * 1024 * 1024) {
        val dir = exportDir()
        val files = dir.listFiles()?.sortedBy { it.lastModified() } ?: return
        var total = files.sumOf { it.length() }
        for (f in files) {
            if (total <= maxBytes) break
            total -= f.length()
            f.delete()
        }
    }

    fun shareIntent(file: File, mimeType: String): Intent {
        val uri = FileProvider.getUriForFile(appContext, authority, file)
        return Intent.createChooser(
            Intent(Intent.ACTION_SEND).apply {
                type = mimeType
                putExtra(Intent.EXTRA_STREAM, uri)
                addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION)
            },
            "Share trip",
        )
    }
}
