package com.alefinot.dashboardpp.viewmodel

import android.app.Application
import android.content.Intent
import androidx.lifecycle.AndroidViewModel
import com.alefinot.dashboardpp.trips.CachedSummary
import com.alefinot.dashboardpp.trips.RecorderSettings
import com.alefinot.dashboardpp.trips.TripExport
import com.alefinot.dashboardpp.trips.TripFormat
import com.alefinot.dashboardpp.trips.TripIndex
import com.alefinot.dashboardpp.trips.TripReader
import com.alefinot.dashboardpp.trips.TripStore
import com.alefinot.dashboardpp.trips.TripSummary
import com.alefinot.dashboardpp.trips.TripTag
import com.alefinot.dashboardpp.trips.TripTagReader
import com.alefinot.dashboardpp.vehicles.VehicleStore
import java.io.File
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.combine
import kotlinx.coroutines.flow.first
import kotlinx.coroutines.launch

/** One row in the Trips list. `summary` is null until it has been computed. */
data class TripRow(
    val file: File,
    val startMs: Long,
    val sizeBytes: Long,
    val summary: TripSummary?,
    /** Who the file says the drive belongs to, from its `#vehicle=` line. */
    val vehicleId: String? = null,
    /**
     * The name to show for that vehicle: the card's name today when the card still
     * exists, otherwise the name written in the file when the drive happened. A
     * rename therefore does not rewrite history, and deleting a vehicle does not
     * erase its drives [plan §6].
     */
    val vehicleName: String? = null,
    /** The eFuse MAC that dashboard reported, when the file recorded one. */
    val eff: String? = null,
    /** Is that id still a card in the garage? False for a file whose card was deleted. */
    val inGarage: Boolean = false,
)

/** One chip in the Trips filter row. */
data class TripFilterOption(val value: String, val label: String)

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
    private val settings = RecorderSettings(app)
    private val vehicles = VehicleStore(app)
    private val scope = CoroutineScope(SupervisorJob() + Dispatchers.Main)

    private val _rows = MutableStateFlow<List<TripRow>>(emptyList())
    val rows: StateFlow<List<TripRow>> = _rows

    private val _filter = MutableStateFlow(RecorderSettings.FILTER_ALL)
    val filter: StateFlow<String> = _filter

    private val _options = MutableStateFlow<List<TripFilterOption>>(emptyList())
    val filterOptions: StateFlow<List<TripFilterOption>> = _options

    /**
     * The rows the list actually draws: every row the index pass found, narrowed by
     * the filter. Kept in the model rather than the screen because the header's
     * count has to agree with the cards underneath it.
     */
    private val _visible = MutableStateFlow<List<TripRow>>(emptyList())
    val visibleRows: StateFlow<List<TripRow>> = _visible

    init {
        scope.launch {
            try {
                _filter.value = settings.tripFilter.first()
            } catch (e: Exception) {
                // An unreadable preference means no filter, which is the default.
            }
        }
        scope.launch {
            combine(_rows, _filter) { r, f -> applyFilter(r, f) }.collect { _visible.value = it }
        }
    }

    /** ALL, a vehicle id, or UNKNOWN. Remembered, so the list opens as you left it. */
    fun setFilter(value: String) {
        if (value == _filter.value) return
        _filter.value = value
        scope.launch {
            try {
                settings.setTripFilter(value)
            } catch (e: Exception) {
                // Worst case the filter is not remembered; the list still works.
            }
        }
    }

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
            // Live names, resolved by id: the tag in the file only carries the name
            // the card had at the time, so a rename shows up everywhere at once and
            // a deleted card falls back to that copy instead of disappearing.
            val names = try {
                vehicles.all().first().associate { it.id to it.name }
            } catch (e: Exception) {
                emptyMap()
            }
            val rows = files.map { f ->
                val hit = cached[f.absolutePath]
                val fresh = hit != null && TripIndex.fresh(f, hit)
                val tag = tagOf(f, hit, fresh)
                if (fresh) {
                    // A row the cache already agrees with keeps its summary.
                    index.reseat(f, hit!!, tag)
                }
                // The card is labelled with the drive's start from the file
                // name, not the mtime: a live trip's mtime moves every second,
                // and a sealed one marks the end of the drive, not its start.
                TripRow(
                    f,
                    TripStore.startMsOf(f.name) ?: f.lastModified(),
                    f.length(),
                    if (fresh) hit!!.summary else null,
                    vehicleId = tag?.vehicleId,
                    vehicleName = tag?.let { names[it.vehicleId] ?: it.name.ifBlank { null } },
                    eff = tag?.eff?.takeIf { it.isNotBlank() && it != TripFormat.UNKNOWN_EFF },
                    inGarage = tag?.vehicleId?.let { names.containsKey(it) } ?: false,
                )
            }
            _rows.value = rows
            _options.value = filterOptionsFor(rows, names)
            if (_filter.value != RecorderSettings.FILTER_ALL &&
                _filter.value != RecorderSettings.FILTER_UNKNOWN &&
                rows.none { it.vehicleId == _filter.value }
            ) {
                // A remembered filter can point at a vehicle that has no drives left,
                // or at a card that was deleted. Holding it would show an empty list
                // labelled with an id the user has never seen, so the filter goes back
                // to ALL - which is the one thing on screen they can act on.
                _filter.value = RecorderSettings.FILTER_ALL
            }
            _storageBytes.value = files.sumOf { it.length() }
            index.retain(files.map { it.absolutePath }.toSet())
            _busy.value = false
            // Any file whose summary is missing gets summarised in the
            // background and pushed back into the list, so the tab paints
            // instantly and fills in the numbers a moment later.
            rows.filter { it.summary == null }.forEach { row -> summarise(row) }
        }
    }

    /**
     * The tag for one file, from the cache row when that row knows it, otherwise
     * from line 1 - the only case in which a badge costs anything at all, and one
     * line, never a second walk over the samples.
     */
    private fun tagOf(file: File, hit: CachedSummary?, fresh: Boolean): TripTag? {
        if (hit != null && fresh && hit.tagKnown) return hit.tag
        return TripTagReader.read(file)
    }

    companion object {
        /** Which rows survive the filter. A vehicle id matches its own drives only. */
        fun applyFilter(rows: List<TripRow>, filter: String): List<TripRow> = when {
            filter == RecorderSettings.FILTER_ALL -> rows
            filter == RecorderSettings.FILTER_UNKNOWN -> rows.filter { it.vehicleId == null }
            else -> rows.filter { it.vehicleId == filter }
        }

        /**
         * The chips: ALL, one per vehicle that actually has drives, and UNKNOWN when
         * something has no tag (a file from before this app wrote one, or a drive with
         * no card selected). Empty when there is nothing to narrow: one vehicle and no
         * untagged files is already a filtered list, and a control with one choice is
         * decoration.
         *
         * Reached through the class rather than an instance because the rule worth
         * pinning down is "when does this control exist at all", and building a
         * ViewModel needs an Android context that has nothing to do with that rule.
         */
        internal fun filterOptionsFor(rows: List<TripRow>, names: Map<String, String>): List<TripFilterOption> {
            val ids = rows.mapNotNull { it.vehicleId }.distinct()
            val unknown = rows.any { it.vehicleId == null }
            if (ids.size + (if (unknown) 1 else 0) < 2) return emptyList()
            val out = ArrayList<TripFilterOption>()
            out.add(TripFilterOption(RecorderSettings.FILTER_ALL, "ALL"))
            // Vehicles in the order their drives first appear in the list, newest
            // first. Ids are never shown to the user, so a vehicle whose card is gone
            // still gets a chip - its drives are exactly what a filter for it finds.
            ids.forEach { id -> out.add(TripFilterOption(id, names[id] ?: NO_CARD_LABEL)) }
            if (unknown) out.add(TripFilterOption(RecorderSettings.FILTER_UNKNOWN, "UNKNOWN"))
            return out
        }

        /** A vehicle named in a file that no longer has a card in the garage. */
        private const val NO_CARD_LABEL = "(no card)"

        /** Exposed for the settings screen's "open the folder" affordance. */
        fun tripsDir(app: Application): File = TripStore(app).dir()
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
                    TripTagReader.read(row.file),
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
}
