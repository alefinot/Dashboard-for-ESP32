package com.alefinot.dashboardpp.vehicles

/**
 * The rules the vehicle editor enforces, written away from the screen (tested in
 * `VehicleEditorRulesTest`).
 *
 * A garage card is only useful if it is identifiable, and the two ways a save can break
 * that are both easy to do by accident: pressing SAVE with the name still blank, and giving
 * a second card the identity of a unit that already has one. The second one is the reason
 * the refactor exists — two cards describing one physical dashboard, and a drive tagged to
 * the wrong vehicle — so it is refused rather than warned about.
 */

/** MACs are compared as bare hex: a retyped or re-cased identity is the same identity. */
private fun macKey(mac: String?): String? =
    mac?.uppercase()?.filter { it.isDigit() || it in 'A'..'F' }?.takeIf { it.isNotEmpty() }

/** The last three bytes of a MAC, as six hex digits — "A4:CF:12:3B:9E:07" gives "3B9E07". */
private fun macTail(mac: String?): String? =
    macKey(mac)?.takeIf { it.length >= 6 }?.takeLast(6)

/**
 * The name to store.
 *
 * A blank name is filled in rather than rejected: the user is often mid-edit, and a card
 * that cannot be saved is a card whose other fields are lost on the back gesture. The
 * placeholder is built from the dashboard's own eFuse MAC tail, so it is a string the user
 * can match against the unit in front of them — and it is obviously a placeholder, because
 * it says UNTITLED. With no MAC (old firmware, or a hand-entered address) the card's id is
 * used, which is at least the same handle the trip files carry.
 *
 * [takenNames] are the names of the *other* cards. A collision is numbered up rather than
 * refused, because two cards named "Corolla" cannot be told apart in the trips filter or
 * in the delete confirmation, and the confirmation is where a mistake stops being fixable.
 */
fun displayName(name: String, v: Vehicle, takenNames: Set<String>): String {
    val typed = name.trim()
    val base = typed.ifBlank {
        "UNTITLED ${macTail(v.mac) ?: v.id.uppercase()}"
    }
    // A name is only taken if this card did not already carry it: saving a card
    // without renaming it must not rename it to "Corolla 2" by itself.
    if (base !in takenNames || base == v.name.trim()) return base
    var n = 2
    while ("$base $n" in takenNames) n++
    return "$base $n"
}

/**
 * The other card that already claims this card's identity, or null if there is none.
 *
 * A card with no MAC cannot conflict: it makes no claim about a physical unit, so two of
 * them are a nuisance rather than a contradiction, and refusing them would block the
 * hand-entered path that exists precisely because some units cannot be identified.
 */
fun macConflict(v: Vehicle, others: List<Vehicle>): Vehicle? {
    val key = macKey(v.mac) ?: return null
    return others.firstOrNull { it.id != v.id && macKey(it.mac) == key }
}

/**
 * Why a save was refused, in words the user can act on: the other card is named, because
 * getting rid of *that* card is the way to resolve the clash, and its identity is quoted so
 * the two cards can be compared rather than guessed at.
 */
fun refusalMessage(clash: Vehicle): String {
    val what = clash.name.trim().ifBlank { "the card ${clash.id}" }
    return "That dashboard is already on a card called $what " +
        "(identity ${clash.mac ?: "unknown"}). Remove it there first."
}

/**
 * Copy the user's fields onto the stored record and nothing else.
 *
 * The editor edits half of a [Vehicle]: name, type, plate, maker, model, year, notes. The
 * other half — `mac`, `ips`, `lastConnectedAtMs`, `firmwareVersion`, `createdAtMs`,
 * `photoFile` — is evidence and files the app owns, written by a connect and by the photo
 * pipeline. A save that rebuilt the record from the form would drop that evidence and
 * silently unbind the card from the unit it was adopted from, which is a worse bug than the
 * typo the user was fixing.
 */
fun mergeTypedFields(stored: Vehicle, typed: Vehicle): Vehicle = stored.copy(
    name = typed.name.trim(),
    type = typed.type,
    plate = typed.plate.trim(),
    maker = typed.maker.trim(),
    model = typed.model.trim(),
    year = typed.year.trim(),
    notes = typed.notes.trim(),
)
