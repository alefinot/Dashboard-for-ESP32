package com.alefinot.dashboardpp.trips

import android.content.Context
import java.io.BufferedWriter
import java.io.File
import java.io.FileOutputStream
import java.io.OutputStreamWriter
import java.text.ParseException
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale

/**
 * Where trip files live (trip recording plan §4). App-private external storage:
 * visible to the user through a file manager and via the share sheet, removed
 * cleanly with the app, and needs no storage permission.
 */
class TripStore(context: Context) {
    private val appContext = context.applicationContext

    fun dir(): File {
        val base = appContext.getExternalFilesDir(null) ?: appContext.filesDir
        val d = File(base, "trips")
        if (!d.exists()) d.mkdirs()
        return d
    }

    /** Newest first, as the Trips list wants them. */
    fun trips(): List<File> =
        dir().listFiles { f -> f.isFile && f.name.startsWith("trip-") && f.name.endsWith(".csv") }
            // Ordered by the drive's start (the name), not by mtime: a live trip
            // is being appended to, so its mtime keeps moving to the top of the
            // list on every refresh.
            ?.sortedByDescending { startMsOf(it.name) ?: it.lastModified() }
            ?: emptyList()

    fun deleteAll(): Int {
        val files = trips()
        files.forEach { it.delete() }
        return files.size
    }

    /** `trip-YYYYMMDD-HHMMSS-<seq>.csv`, the seq breaking ties within a second. */
    fun newName(startMs: Long): String {
        val stamp = SimpleDateFormat("yyyyMMdd-HHmmss", Locale.US).format(Date(startMs))
        val dir = dir()
        return File(dir, uniqueName(stamp) { File(dir, it).exists() }).absolutePath
    }

    companion object {
        /**
         * Pure half of [newName]: the first `trip-<stamp>-NNN.csv` that does not
         * collide. Two drives started in the same second (a seal and an immediate
         * re-open, or a clock jump) must never overwrite each other's file.
         */
        fun uniqueName(stamp: String, exists: (String) -> Boolean): String {
            var seq = 1
            while (true) {
                // Locale.US: a locale that renders digits differently must not
                // change the file names we compare against.
                val candidate = "trip-$stamp-" + String.format(Locale.US, "%03d.csv", seq)
                if (!exists(candidate)) return candidate
                seq++
            }
        }

        /**
         * The trip start encoded in a file name, or null when the name is not
         * ours (a file copied in by hand, or the fixtures the tests use).
         *
         * This is what the Trips list labels a card with. The file's mtime is
         * not a trip's start: while a trip is live its mtime is "the recorder
         * last wrote", and once sealed it is "the session ended" — which is how
         * a drive that began at 13:39 got labelled 13:57 on the device. The
         * stamp in the name is the one label that never moves.
         */
        fun startMsOf(name: String): Long? {
            val m = Regex("^trip-(\\d{8})-(\\d{6})-\\d{3}\\.csv$").find(name) ?: return null
            val format = SimpleDateFormat("yyyyMMdd-HHmmss", Locale.US)
            format.isLenient = false
            return try {
                format.parse("${m.groupValues[1]}-${m.groupValues[2]}")?.time
            } catch (e: ParseException) {
                null // impossible dates (month 13, second 61) are not ours
            }
        }
    }
}

/**
 * File access for the recorder, behind an interface so the session state machine
 * ([TripRecorder]) is a plain JVM class the unit tests can drive with a fake.
 */
interface TripWriter {
    /**
     * Create the file, writing [tag] (a `#vehicle=` line, or "" for none) above the
     * header. The tag goes first because a reader that only wants to know who the
     * drive belongs to should not have to read past line 1.
     */
    fun create(path: String, tag: String)

    /** Continue an existing file — no second header. */
    fun reopen(path: String)

    fun append(path: String, line: String)

    /** Flush and close. The file stays valid; sealing is not a format event. */
    fun seal(path: String)

    /** Existing data lines, used to seed the running summary on a re-open. */
    fun readDataLines(path: String): List<String>

    fun exists(path: String): Boolean
}

/** The real implementation: one append-mode writer per open trip file. */
class CsvTripWriter(private val store: TripStore) : TripWriter {
    private val open = HashMap<String, BufferedWriter>()

    override fun create(path: String, tag: String) {
        closeOf(path)
        val f = File(path)
        f.parentFile?.mkdirs()
        val w = BufferedWriter(OutputStreamWriter(FileOutputStream(f), Charsets.UTF_8))
        TripFormat.fileHead(tag).forEach { line ->
            w.write(line)
            w.newLine()
        }
        w.flush()
        open[path] = w
    }

    override fun reopen(path: String) {
        if (open.containsKey(path)) return
        val f = File(path)
        if (!f.exists()) {
            // A file that vanished mid-drive is recreated without a tag: the trip
            // already had an owner written once, and inventing one here from the
            // recorder's current binding would be a guess about a file that may
            // hold samples from before that vehicle existed.
            create(path, "")
            return
        }
        val w = BufferedWriter(OutputStreamWriter(FileOutputStream(f, true), Charsets.UTF_8))
        open[path] = w
    }

    override fun append(path: String, line: String) {
        val w = open[path] ?: return
        w.write(line)
        w.newLine()
        // Flushing every sample is what keeps the file honest after a kill: at
        // one line per second the cost is irrelevant, the value is the trip.
        w.flush()
    }

    override fun seal(path: String) = closeOf(path)

    private fun closeOf(path: String) {
        open.remove(path)?.run {
            try {
                flush()
                close()
            } catch (e: Exception) {
                // A dead volume or a revoked storage permission must not take the
                // service down; the samples already flushed are still on disk.
            }
        }
    }

    override fun readDataLines(path: String): List<String> {
        val f = File(path)
        if (!f.exists()) return emptyList()
        return try {
            val out = ArrayList<String>()
            f.forEachLine { line ->
                if (!TripFormat.skippable(line)) out.add(line)
            }
            out
        } catch (e: Exception) {
            // A torn read (device storage revoked mid-trip) costs the seeding,
            // never the append that follows.
            emptyList()
        }
    }

    override fun exists(path: String): Boolean = File(path).exists()

    fun closeAll() = open.keys.toList().forEach { closeOf(it) }
}

/**
 * The owner of a trip file, read from line 1 without parsing the file.
 *
 * The Trips list wants a name badge on every card; the summary cache carries the
 * tag whenever it has a row for the file, and this is the fallback for files the
 * cache does not know yet. It reads one line, which is what makes the badge cost
 * no second walk over the samples.
 */
object TripTagReader {
    /** Line 1 of a trip file: its tag, or null when it has none (or cannot be read). */
    fun read(file: File): TripTag? {
        if (!file.exists()) return null
        return try {
            file.bufferedReader(Charsets.UTF_8).use { r -> TripFormat.parseTag(r.readLine() ?: "") }
        } catch (e: Exception) {
            null
        }
    }
}
