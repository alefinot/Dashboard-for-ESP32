package com.alefinot.dashboardpp.viewmodel

import android.app.Application
import android.content.Intent
import androidx.lifecycle.AndroidViewModel
import com.alefinot.dashboardpp.trips.TripExport
import com.alefinot.dashboardpp.trips.TripIndex
import com.alefinot.dashboardpp.trips.TripReader
import com.alefinot.dashboardpp.trips.TripStore
import com.alefinot.dashboardpp.trips.TripSummary
import java.io.File
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.first
import kotlinx.coroutines.launch

/** One row in the Trips list. `summary` is null until it has been computed. */
data class TripRow(
    val file: File,
    val startMs: Long,
    val sizeBytes: Long,
    val summary: TripSummary?,
)

/** Everything the detail screen draws. */
data class TripDetail(
    val file: File,
    val summary: TripSummary,
    val samples: List<com.alefinot.dashboardpp.trips.TripSample>,
)

/**
 * The Trips tab's model: list (from the summary cache, not by re-reading every
 * CSV), delete, share, and detail loading. All file work runs on the IO
 * dispatcher — a 12 h trip is ~43k lines, and parsing that on the main thread
 * would jank the tab.
 */
class TripsViewModel(app: Application) : AndroidViewModel(app) {
    private val store = TripStore(app)
    private val index = TripIndex(app)
    private val export = TripExport(app)
    private val scope = CoroutineScope(SupervisorJob() + Dispatchers.Main)

    private val _rows = MutableStateFlow<List<TripRow>>(emptyList())
    val rows: StateFlow<List<TripRow>> = _rows

    private val _busy = MutableStateFlow(false)
    val busy: StateFlow<Boolean> = _busy

    private val _message = MutableStateFlow<String?>(null)
    val message: StateFlow<String?> = _message

    private val _storageBytes = MutableStateFlow(0L)

    /**
     * Bytes of recorded CSV, as counted by the last index pass. Deliberately a flow
     * rather than a function that walks the folder: the Trips header shows it, and a
     * directory walk in a composition runs on the UI thread on every recomposition of
     * that header - which, now that the page stays composed, would include every
     * second the recorder reports while the user is looking at another page.
     */
    val storageBytes: StateFlow<Long> = _storageBytes

    fun refresh() {
        scope.launch(Dispatchers.IO) {
            _busy.value = true
            val files = store.trips()
            val cached = try {
                index.cached().first()
            } catch (e: Exception) {
                emptyMap()
            }
            val rows = files.map { f ->
                val hit = cached[f.absolutePath]
                val summary = if (hit != null && TripIndex.fresh(f, hit)) hit.summary else null
                // The card is labelled with the drive's start from the file
                // name, not the mtime: a live trip's mtime moves every second,
                // and a sealed one marks the end of the drive, not its start.
                TripRow(f, TripStore.startMsOf(f.name) ?: f.lastModified(), f.length(), summary)
            }
            _rows.value = rows
            _storageBytes.value = files.sumOf { it.length() }
            index.retain(files.map { it.absolutePath }.toSet())
            _busy.value = false
            // Any file whose summary is missing gets summarised in the
            // background and pushed back into the list, so the tab paints
            // instantly and fills in the numbers a moment later.
            rows.filter { it.summary == null }.forEach { row -> summarise(row) }
        }
    }

    private fun summarise(row: TripRow) {
        scope.launch(Dispatchers.IO) {
            val summary = TripReader.summarize(row.file.absolutePath) ?: return@launch
            try {
                index.put(
                    row.file.absolutePath,
                    row.file.lastModified(),
                    row.file.length(),
                    summary,
                )
            } catch (e: Exception) {
                // The cache is an optimisation; failing it costs only the re-read.
            }
            _rows.value = _rows.value.map {
                if (it.file == row.file) it.copy(summary = summary) else it
            }
        }
    }

    fun detail(row: TripRow): TripDetail? {
        val summary = row.summary ?: TripReader.summarize(row.file.absolutePath) ?: return null
        return TripDetail(row.file, summary, TripReader.samples(row.file.absolutePath))
    }

    fun delete(row: TripRow) {
        scope.launch(Dispatchers.IO) {
            val deleting = row.file.absolutePath == openTripPath()
            if (deleting) {
                _message.value = "CAN'T DELETE THE TRIP BEING RECORDED"
                return@launch
            }
            row.file.delete()
            index.retain(store.trips().map { it.absolutePath }.toSet())
            _message.value = null
            refresh()
        }
    }

    fun deleteAll() {
        scope.launch(Dispatchers.IO) {
            val open = openTripPath()
            var blocked = false
            store.trips().forEach { f ->
                if (f.absolutePath == open) blocked = true else f.delete()
            }
            try {
                index.retain(emptySet())
            } catch (e: Exception) {
                // ignore
            }
            _message.value = if (blocked) "KEPT THE TRIP BEING RECORDED" else null
            refresh()
        }
    }

    /** The file the recorder is writing, if any — it must never be deleted. */
    private fun openTripPath(): String? =
        com.alefinot.dashboardpp.trips.TripRuntime.state.value.takeIf { it.recording }?.tripPath

    fun shareCsv(row: TripRow): Intent {
        export.prune()
        return export.shareIntent(export.csv(row.file), "text/csv")
    }

    fun shareGpx(row: TripRow): Intent {
        export.prune()
        return export.shareIntent(export.gpx(row.file), "application/gpx+xml")
    }

    fun consumeMessage() {
        _message.value = null
    }


    companion object {
        /** Exposed for the settings screen's "open the folder" affordance. */
        fun tripsDir(app: Application): File = TripStore(app).dir()
    }
}
