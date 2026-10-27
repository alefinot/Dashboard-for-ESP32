package com.alefinot.dashboardpp.vehicles

import android.content.Context
import androidx.datastore.core.DataStore
import androidx.datastore.preferences.core.Preferences
import androidx.datastore.preferences.core.edit
import androidx.datastore.preferences.core.emptyPreferences
import androidx.datastore.preferences.core.intPreferencesKey
import androidx.datastore.preferences.core.stringPreferencesKey
import androidx.datastore.preferences.preferencesDataStore
import kotlinx.coroutines.flow.Flow
import kotlinx.coroutines.flow.catch
import kotlinx.coroutines.flow.first
import kotlinx.coroutines.flow.map

// One DataStore instance per name, per process - what the preferencesDataStore
// delegate guarantees, and why the garage's three keys live in one file: the pager,
// the reconnect path and the editor all read the same rows, and a second instance
// would be a second mutation lock on one file (the pattern trips/Stores.kt sets).
private val Context.vehicleStore: DataStore<Preferences> by preferencesDataStore("vehicle_store")

/**
 * The garage on disk (vehicle page plan §3): one JSON record per `vehicle:<id>`
 * row, the id the cards are keyed by, and which card the user last stood on.
 *
 * The split this class is built around: [sorted] and [nextIdName] are pure
 * functions in the companion because they are the decisions worth arguing about -
 * which card a new vehicle will be called, and what order the garage shows - while
 * everything that touches DataStore is a thin, boring read or one [edit]. The same
 * split `TripStore.uniqueName` makes.
 *
 * Nothing here decides identity: the record's `mac` is stored and returned
 * verbatim, and [VehicleMatch] is the only place that text is interpreted. Nor does
 * anything here sort except through [sorted].
 */
class VehicleStore(context: Context) {
    private val store = context.applicationContext.vehicleStore

    /**
     * The garage in display order, live: one row per card, corrupt rows skipped.
     *
     * The selected id is read from the same preferences snapshot as the rows, so a
     * frame never shows a card sorted to the front that the pointer row has already
     * moved off.
     */
    fun all(): Flow<List<Vehicle>> =
        store.data
            .catch { emit(emptyPreferences()) }
            .map { prefs -> rows(prefs, prefs[KEY_LAST]) }

    /** One card by id, or null if it is gone or its row cannot be read. */
    suspend fun get(id: String): Vehicle? {
        val text = snapshot()[stringPreferencesKey(rowKey(id))] ?: return null
        return VehicleCodec.decode(text)
    }

    /**
     * Insert or replace, in one transaction. Renaming a card is a [put] of the same
     * id with a different `name` - there is no rename, because the id is what the
     * trips of task 8 will tag themselves with and it must never change.
     *
     * `id` must come from [nextId]: two ids are reserved by this store's own keys
     * (`last` and `nextseq`) and a row with one of those names would overwrite the
     * pointer or the counter instead of creating a card.
     */
    suspend fun put(v: Vehicle) {
        store.edit { it[stringPreferencesKey(rowKey(v.id))] = VehicleCodec.encode(v) }
    }

    /**
     * Remove one card.
     *
     * The `vehicle:last` pointer is cleared in the same transaction when the deleted
     * card was the selected one, so the pager never wakes up on an id that no longer
     * exists ([sorted] tolerates a stale pointer, but the UI would have nothing to
     * highlight).
     *
     * The card's JPEG is not touched here. It is `vehicles/<id>.jpg` and belongs to
     * `VehiclePhotoStore.delete(id)` (task 3b), which will be called from this
     * method; until that lands, deleting a card leaves its photo file on disk.
     */
    suspend fun delete(id: String) {
        store.edit { prefs ->
            prefs.remove(stringPreferencesKey(rowKey(id)))
            if (prefs[KEY_LAST] == id) prefs.remove(KEY_LAST)
        }
    }

    /** Which card the garage opens on, or null if none was ever picked. */
    suspend fun selectedId(): String? = snapshot()[KEY_LAST]

    /**
     * Point the garage at [id]. Not validated against the rows on purpose: the
     * caller (task 4) chooses from [all], and checking here would mean reading
     * inside the write lock. A pointer to a card that no longer exists is harmless -
     * [sorted] puts nothing first, and [delete] clears it when it can.
     */
    suspend fun select(id: String) {
        store.edit { it[KEY_LAST] = id }
    }

    /**
     * The next free card id - `v1`, `v2`, ... - reserved in the same transaction
     * that reads it, so two callers asking at once cannot be handed the same one.
     *
     * The counter is separate from the highest existing id on purpose: [delete]
     * frees a row but never its name, because a trip already tagged with `v3` must
     * not find a different vehicle when `v3` is handed out again.
     */
    suspend fun nextId(): String {
        var assigned = ""
        store.edit { prefs ->
            val name = nextIdName(prefs[KEY_SEQ] ?: 0, rowIds(prefs))
            // nextIdName's shape is "v" plus digits, so this round trip is exact.
            prefs[KEY_SEQ] = name.substring(1).toInt()
            assigned = name
        }
        return assigned
    }

    /** One snapshot of the whole store, for the reads that need more than one key. */
    private suspend fun snapshot(): Preferences =
        try {
            store.data.first()
        } catch (e: Exception) {
            // A failed read answers as "no rows", which is what [all] answers too;
            // the alternative is throwing at the pager for a torn preference file.
            emptyPreferences()
        }

    /** Rows decoded and ordered; [selectedId] comes from the same snapshot. */
    private fun rows(prefs: Preferences, selectedId: String?): List<Vehicle> {
        val out = ArrayList<Vehicle>()
        for ((key, value) in prefs.asMap()) {
            if (value !is String) continue
            if (!isRow(key.name)) continue
            // One torn row costs one card, not the garage.
            VehicleCodec.decode(value)?.let { out.add(it) }
        }
        return sorted(out, selectedId)
    }

    /** The ids that already have a row, for [nextIdName]'s skip. */
    private fun rowIds(prefs: Preferences): Set<String> =
        prefs.asMap().keys
            .filter { isRow(it.name) }
            .map { it.name.removePrefix(ROW_PREFIX) }
            .toSet()

    /** True for `vehicle:<id>` only - the two bookkeeping keys share the prefix. */
    private fun isRow(name: String): Boolean =
        name.startsWith(ROW_PREFIX) && name != KEY_LAST.name && name != KEY_SEQ.name

    companion object {
        private const val ROW_PREFIX = "vehicle:"
        private val KEY_LAST = stringPreferencesKey("vehicle:last")
        private val KEY_SEQ = intPreferencesKey("vehicle:nextseq")

        private fun rowKey(id: String) = "$ROW_PREFIX$id"

        /**
         * Garage order: the selected card first, then by most recent connect
         * (newest first), never-connected cards last, and ties broken by the older
         * card first.
         *
         * Selected first because the garage opens where the user was. Connected
         * before never-connected because a card that has answered is a dashboard the
         * user owns and a card that has not is a draft. Newest first because that is
         * the unit most likely to be in front of them now. Never-connected cards
         * have no `lastConnectedAtMs`, so they sort on the minimum of that column -
         * and among themselves the older card is the one they made first and would
         * look for first.
         *
         * A `selectedId` that matches no card (the card was deleted while the
         * pointer still named it) changes nothing: no row is ranked first, so the
         * rest keep their order.
         */
        fun sorted(records: List<Vehicle>, selectedId: String?): List<Vehicle> =
            records.sortedWith(
                compareBy<Vehicle> { if (it.id == selectedId) 0 else 1 }
                    .thenByDescending { it.lastConnectedAtMs ?: Long.MIN_VALUE }
                    .thenBy { it.createdAtMs },
            )

        /**
         * The id to hand out given the last counter value [seq] and the ids that
         * already have a row, [taken].
         *
         * The skip is the point: a record restored by a future import, or written
         * into the file by hand, must not be overwritten by the next card the user
         * makes. The scan only ever moves forward, so a name that a [delete] freed
         * is not handed out again - card ids double as trip tags (task 8), and a new
         * vehicle wearing a dead card's id would inherit its drives.
         */
        fun nextIdName(seq: Int, taken: Set<String>): String {
            // A missing or damaged counter restarts at v1 rather than producing
            // "v0" or "v-3", which would be an id no reader of this store expects.
            var n = if (seq < 1) 1 else seq + 1
            while ("v$n" in taken) n++
            return "v$n"
        }
    }
}
