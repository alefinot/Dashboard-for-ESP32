package com.alefinot.dashboardpp.vehicles

/** What kind of vehicle a card describes - the garage's only categorisation. */
enum class VehicleType { CAR, MOTORCYCLE, OTHER }

/**
 * One garage card: a vehicle the user named, plus what the app knows about the
 * dashboard unit that belongs to it.
 *
 * Two halves live in one record on purpose. `name` … `notes` are the user's words
 * and are never written by a connect; `mac`, `ips`, `lastConnectedAtMs` and
 * `firmwareVersion` are the app's evidence about a physical unit. Keeping them
 * together is what lets a card mean *this dashboard* rather than *whatever answered
 * at this address* - the identity rules in [VehicleMatch] only ever touch the second
 * half, and rule 3 upgrades a card made by address without disturbing the first.
 *
 * `id` is app-generated and stable across renames because the trips (task 8) tag
 * themselves with it: renaming a vehicle must not rewrite recorded history.
 *
 * `mac` is `null` until the unit is bound. Old firmware never reports one, so an
 * unbound card is a normal state, not a broken one - it is matched by address
 * instead, under the narrower rule [VehicleMatch.resolve] describes.
 *
 * `year` is a `String`, not an `Int`: the user types it, and "c. 1998", "1998/99"
 * and "" are all answers a number field would throw away.
 *
 * `ips` is most-recent-first and holds at most 3 entries (see
 * [VehicleMatch.bindConnect]); the launch reconnect probes them in that order, so a
 * stale address first costs a connect timeout before the live one is tried.
 */
data class Vehicle(
    val id: String,
    val mac: String? = null,
    val name: String = "",
    val photoFile: String? = null,
    val type: VehicleType = VehicleType.CAR,
    val plate: String = "",
    val maker: String = "",
    val model: String = "",
    val year: String = "",
    val notes: String = "",
    val ips: List<String> = emptyList(),
    val lastConnectedAtMs: Long? = null,
    val firmwareVersion: String? = null,
    val createdAtMs: Long = 0L,
)
