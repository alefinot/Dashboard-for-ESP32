package com.alefinot.dashboardpp.trips

import android.content.Context
import androidx.datastore.core.DataStore
import androidx.datastore.preferences.core.Preferences
import androidx.datastore.preferences.core.booleanPreferencesKey
import androidx.datastore.preferences.core.edit
import androidx.datastore.preferences.core.emptyPreferences
import androidx.datastore.preferences.core.stringPreferencesKey
import androidx.datastore.preferences.preferencesDataStore
import java.io.File
import kotlinx.coroutines.flow.Flow
import kotlinx.coroutines.flow.catch
import kotlinx.coroutines.flow.first
import kotlinx.coroutines.flow.map

// One DataStore instance per name, per process — that is what the delegate
// guarantees, and both the UI and the service read these.
private val Context.tripIndexStore: DataStore<Preferences> by preferencesDataStore("trip_index")
private val Context.tripSettingsStore: DataStore<Preferences> by preferencesDataStore("trip_settings")

/**
 * A summary remembered next to the file it describes, together with who the file
 * says the drive belongs to.
 *
 * [tag] is null in two different cases, and they are not the same: a row written
 * by an app that did not know about tags (`tagKnown` false — the file has to be
 * opened once to find out) and a file that genuinely has no tag (`tagKnown` true,
 * [tag] null).
 */
data class CachedSummary(
    val mtime: Long,
    val size: Long,
    val summary: TripSummary,
    val tag: TripTag?,
    val tagKnown: Boolean = true,
)

/**
 * Cache of per-trip summaries (trip recording plan §4): opening the Trips tab
 * must not re-parse every CSV, so a sealed trip is summarised once and reused
 * until its mtime or size changes. Trips still being written are never cached.
 */
class TripIndex(context: Context) {
    private val store = context.applicationContext.tripIndexStore

    private fun key(path: String) = "idx:$path"

    suspend fun put(path: String, mtime: Long, size: Long, summary: TripSummary, tag: TripTag? = null) {
        // The tag comes before the summary code, not after it: that code is itself a
        // `;`-joined string, so it has to stay the last field for `limit = 4` to keep
        // it whole. A row written before tags existed has three fields and decodes
        // with tagKnown false.
        store.edit {
            it[stringPreferencesKey(key(path))] =
                "$mtime;$size;${tag?.raw() ?: NO_TAG};${TripSummaryCodec.encode(summary)}"
        }
    }

    fun cached(): Flow<Map<String, CachedSummary>> =
        store.data
            .catch { emit(emptyPreferences()) }
            .map { prefs ->
                val out = HashMap<String, CachedSummary>()
                for ((k, v) in prefs.asMap()) {
                    if (k.name.startsWith("idx:") && v is String) {
                        val parts = v.split(";", limit = 4)
                        val legacy = parts.size == 3
                        if (parts.size != 3 && parts.size != 4) continue
                        val summary =
                            TripSummaryCodec.decode(if (legacy) parts[2] else parts[3]) ?: continue
                        out[k.name.removePrefix("idx:")] = CachedSummary(
                            mtime = parts[0].toLongOrNull() ?: continue,
                            size = parts[1].toLongOrNull() ?: continue,
                            summary = summary,
                            tag = if (legacy) null else TripTag.fromRaw(parts[2]),
                            tagKnown = !legacy && parts[2] != NO_TAG,
                        )
                    }
                }
                out
            }

    /** Drop cache rows for files that no longer exist. */
    suspend fun retain(livePaths: Set<String>) {
        store.edit { prefs ->
            val stale = prefs.asMap().keys
                .filter { it.name.startsWith("idx:") && it.name.removePrefix("idx:") !in livePaths }
            stale.forEach { prefs.remove(it) }
        }
    }

    /**
     * Re-store a row that is still current but was written before tags existed, so
     * the tag found in the file is remembered and the next list pass reads nothing.
     * A no-op for rows that already know their tag - which is every row this app
     * writes - so the common path stays a read-only pass.
     */
    suspend fun reseat(file: File, cached: CachedSummary, tag: TripTag?) {
        if (cached.tagKnown) return
        put(file.absolutePath, cached.mtime, cached.size, cached.summary, tag)
    }

    companion object {
        /** The marker for "this file was opened and has no tag line". */
        private const val NO_TAG = "-"

        /** True when a cached row still matches the file on disk. */
        fun fresh(file: File, cached: CachedSummary): Boolean =
            file.length() == cached.size && file.lastModified() == cached.mtime
    }
}

/**
 * Recording settings (trip recording plan §5): an explicit user switch, kept in
 * DataStore with the app's other preferences. Off is off — the service is not
 * started, so no polling and no wake lock.
 */
class RecorderSettings(context: Context) {
    private val store = context.applicationContext.tripSettingsStore

    val recordingEnabled: Flow<Boolean> =
        store.data.catch { emit(emptyPreferences()) }
            .map { it[KEY_RECORDING] ?: true }

    /**
     * The app's motion switch (`motion_enabled`, default on). It lives in this
     * store because this DataStore is where the app keeps its preferences - the
     * notification flag next to it is not a recording setting either.
     */
    val motionEnabled: Flow<Boolean> =
        store.data.catch { emit(emptyPreferences()) }
            .map { it[KEY_MOTION] ?: true }

    val notificationEnabled: Flow<Boolean> =
        store.data.catch { emit(emptyPreferences()) }
            .map { it[KEY_NOTIFICATION] ?: true }

    /**
     * Which vehicles the Trips list is narrowed to: `ALL`, `UNKNOWN`, or a vehicle
     * id. A deleted vehicle's id stays a legal value — the list then shows that
     * vehicle's drives, which is the whole point of tagging trips by id [plan §6].
     */
    val tripFilter: Flow<String> =
        store.data.catch { emit(emptyPreferences()) }
            .map { it[KEY_TRIP_FILTER] ?: FILTER_ALL }

    suspend fun setTripFilter(value: String) {
        store.edit { it[KEY_TRIP_FILTER] = value }
    }

    /** One-time battery-optimisation hint, so it never nags twice. */
    val batteryPromptShown: Flow<Boolean> =
        store.data.catch { emit(emptyPreferences()) }
            .map { it[KEY_BATTERY_PROMPT] ?: false }

    /**
     * Read the three switches once, so the store is in RAM before anything needs
     * an answer. DataStore never answers synchronously, and the boot window (the
     * splash) is the only idle stretch the app gets before its first frame: warmed
     * there, the shell's first entrance already knows the motion switch instead of
     * animating and then stopping mid-fade.
     */
    suspend fun warm() {
        recordingEnabled.first()
        motionEnabled.first()
        notificationEnabled.first()
        tripFilter.first()
    }

    /** The persisted session per device, so a trip survives an app restart. */
    suspend fun sessionFor(ip: String): SessionRecord? {
        val text = try {
            store.data.first()[stringPreferencesKey(sessionKey(ip))]
        } catch (e: Exception) {
            null
        } ?: return null
        return SessionCodec.decode(ip, text)
    }

    suspend fun saveSession(record: SessionRecord) {
        store.edit { it[stringPreferencesKey(sessionKey(record.ip))] = SessionCodec.encode(record) }
    }

    /**
     * Drop the recorder's rows for a vehicle that is being forgotten.
     *
     * These rows are per *address*, and they hold an open trip's continuity - last accepted
     * uptime, last sample, which file the drive is in. They are not trips and are never
     * listed as ones, so deleting them loses no drive: the sealed file stays on disk and the
     * trips list reads the file names. What is being cleaned is the claim that this address
     * belongs to a vehicle that no longer has a card - leaving it would hand the next card
     * that dials that address a stranger's half-finished trip.
     *
     * Returns the number of rows actually removed, which is what lets the caller skip the
     * write when there is nothing to do.
     */
    suspend fun clearSessions(ips: List<String>): Int {
        if (ips.isEmpty()) return 0
        var removed = 0
        store.edit { tx ->
            // Keys in this store are typed objects; the pure rule works on their names, so
            // the names are read out here and turned back into keys of the right type (each
            // session row holds a String) for the removal.
            val names = sessionKeysToRemove(tx.asMap().keys.map { it.name }.toSet(), ips)
            removed = names.size
            for (n in names) tx.remove(stringPreferencesKey(n))
        }
        return removed
    }

    suspend fun setRecordingEnabled(enabled: Boolean) {
        store.edit { it[KEY_RECORDING] = enabled }
    }

    suspend fun setMotionEnabled(enabled: Boolean) {
        store.edit { it[KEY_MOTION] = enabled }
    }

    suspend fun setNotificationEnabled(enabled: Boolean) {
        store.edit { it[KEY_NOTIFICATION] = enabled }
    }

    suspend fun setBatteryPromptShown() {
        store.edit { it[KEY_BATTERY_PROMPT] = true }
    }

    companion object {
        private val KEY_RECORDING = booleanPreferencesKey("recording_enabled")
        private val KEY_MOTION = booleanPreferencesKey("motion_enabled")
        private val KEY_NOTIFICATION = booleanPreferencesKey("notification_enabled")
        private val KEY_BATTERY_PROMPT = booleanPreferencesKey("battery_prompt_shown")
        private val KEY_TRIP_FILTER = stringPreferencesKey("trip_filter")

        /** The filter value meaning "no filter". */
        const val FILTER_ALL = "ALL"

        /** The filter value meaning "files with no vehicle tag". */
        const val FILTER_UNKNOWN = "UNKNOWN"

        /** The key holding the recorder's row for one address. */
        fun sessionKey(ip: String) = "session:$ip"

        /**
         * Of [existing], the session rows that belong to [ips].
         *
         * Whole-key equality on purpose: "192.168.1.9" is a text prefix of "192.168.1.99",
         * and a prefix match here would delete another vehicle's open trip. Kept as a pure
         * function (and tested in `SessionKeysTest`) because the store itself cannot be.
         */
        fun sessionKeysToRemove(existing: Set<String>, ips: List<String>): List<String> {
            if (ips.isEmpty()) return emptyList()
            val wanted = ips.map { sessionKey(it) }.toSet()
            return existing.filter { it in wanted }
        }
    }
}
