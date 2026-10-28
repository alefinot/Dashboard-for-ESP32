package com.alefinot.dashboardpp.viewmodel

import com.alefinot.dashboardpp.vehicles.MatchDecision
import com.alefinot.dashboardpp.vehicles.Vehicle
import com.alefinot.dashboardpp.vehicles.VehicleMatch

/** Why the ladder is running, which decides how far it is allowed to go. */
enum class ConnectMode {
    /** Unattended, at launch: the selected vehicle's remembered addresses, nothing else. */
    SILENT,

    /** The user pressed CONNECT (or SEARCH FOR DASHBOARD): everything, including the sweep. */
    SEARCH,

    /** A retry of the remembered addresses — the same reach as [SILENT], asked for. */
    PROBE,
}

/** What one probe of one address answered. Mirrors `VerifyResult` one-to-one. */
sealed interface ProbeOutcome {
    /** A Dashboard++ signed its body at [ip]. [mac] is null on firmware that predates it. */
    data class Ok(val ip: String, val version: String?, val mac: String?) : ProbeOutcome

    /** Nothing answered, or what answered was not a Dashboard++. */
    data class Fail(val ip: String) : ProbeOutcome
}

/** What to do next. One step, decided from what has already been tried. */
sealed interface NextAction {
    /** Nothing to reach, and nothing being reached for: the vehicle page shows [reason]. */
    data class Idle(val reason: String) : NextAction

    /** Probe [address] for the vehicle [vehicleId], and come back with the answer. */
    data class Probe(val vehicleId: String, val address: String) : NextAction

    /** mDNS plus the subnet sweep. [vehicleId] is null when no card exists yet. */
    data class ScanThenProbe(val vehicleId: String?) : NextAction

    /** The unit that answered belongs to [vehicleId]: show it as connected. */
    data class Connect(
        val vehicleId: String,
        val ip: String,
        val version: String?,
        val mac: String?,
    ) : NextAction

    /** The ladder is spent and nothing verified: [reason] is what the page says. */
    data class Failed(val reason: String) : NextAction
}

/** A dashboard that answered and is claimed by no card — an offer for the page, not a write. */
data class UnclaimedUnit(val ip: String, val version: String?, val mac: String)

/** One decision: what to do, what it is worth writing down, and what the page must say. */
data class CoreResult(
    val next: NextAction,
    /** The card to save, when the step learned something about a unit. */
    val persist: Vehicle? = null,
    /** Set when an unclaimed unit answered; the page offers to name it. */
    val adoption: UnclaimedUnit? = null,
    /** Set when the connection belongs to a card other than the one selected. */
    val notice: String? = null,
)

/**
 * The connection ladder, as a pure step function.
 *
 * The app used to hunt for *whatever Dashboard++ is out there* and take the first one
 * that answered; the garage replaced that question with "reach **this** vehicle's
 * dashboard", which splits the old single ladder into three decisions that only work
 * together: how far to go (mode), which card an answer belongs to (identity), and what
 * to write down (rule 5). Drawing them as one `when` in a composable is how the old
 * code ended up unable to tell "the unit at this address belongs to another card" from
 * "nothing answered" — the two look identical from inside a verify call.
 *
 * So [decide] answers exactly one step, and the view model is the loop. Everything here
 * is pure: no network, no clock, no store. That is what makes the expensive cases
 * testable — a card bound to a unit that reports no identity, an address two cards
 * remember, a unit nobody owns — instead of leaving them to be discovered on a tablet
 * where the wrong answer writes someone's car name into a trip log.
 *
 * The identity half deliberately **does not** reuse [VehicleMatch.resolve] for a
 * no-MAC answer. `resolve` takes the first unbound card holding the address, which is
 * the right tie-break for a matcher; for a connect it is not safe enough, because the
 * card this picks is the card the next drive gets attributed to. Where the answer
 * carries no identity, [decide] therefore insists on exactly one claimant and fails
 * otherwise.
 */
object ConnectionCore {

    /** The line the vehicle page shows when there is no card to reach. */
    const val NO_VEHICLE_SELECTED = "No vehicle selected"

    private const val NO_ADDRESS = "That vehicle has no remembered address yet."
    private const val NO_ANSWER = "No Dashboard++ answered on this network."
    private const val UNCLAIMED = "A dashboard answered that no vehicle card knows."
    private const val AMBIGUOUS =
        "More than one vehicle card remembers that address, and the dashboard did not " +
            "report an identity, so the app cannot tell which vehicle it is."

    /**
     * [tried] is every address already probed in this attempt (including the one
     * [outcome] just reported), [scanDone] says whether the sweep has already run, and
     * [nowMs] is supplied rather than read so the record a step produces is testable.
     */
    fun decide(
        vehicles: List<Vehicle>,
        selectedId: String?,
        tried: List<String>,
        scanDone: Boolean,
        outcome: ProbeOutcome?,
        mode: ConnectMode,
        nowMs: Long,
    ): CoreResult {
        val answer = outcome as? ProbeOutcome.Ok
        if (answer != null) return resolveAnswer(vehicles, selectedId, answer, mode, nowMs)
        return nextStep(vehicles, selectedId, tried, scanDone, mode)
    }

    // ------------------------------------------------------------------ the ladder

    private fun nextStep(
        vehicles: List<Vehicle>,
        selectedId: String?,
        tried: List<String>,
        scanDone: Boolean,
        mode: ConnectMode,
    ): CoreResult {
        val card = selectedId?.let { id -> vehicles.firstOrNull { it.id == id } }
        // A selection that is gone (deleted from under the attempt) is the same
        // nothing-to-reach answer as never having one; there is no card to probe.
        if (selectedId != null && card == null) return CoreResult(NextAction.Idle(NO_VEHICLE_SELECTED))

        val untried = card?.ips?.firstOrNull { it !in tried }
        if (untried != null) return CoreResult(NextAction.Probe(card!!.id, untried))

        return when (mode) {
            // Unattended paths stop at the card's own addresses. A subnet sweep costs
            // ~15 s, a multicast lock, and can land on a dashboard the user never
            // touched — none of which belongs at launch or on a network callback.
            ConnectMode.SILENT, ConnectMode.PROBE -> when {
                card == null -> CoreResult(NextAction.Idle(NO_VEHICLE_SELECTED))
                card.ips.isEmpty() -> CoreResult(NextAction.Idle(NO_ADDRESS))
                // Everything remembered was dialed and nobody answered. That is a
                // failure, not a wait: the vehicle page shows it under that card.
                else -> CoreResult(
                    NextAction.Failed(
                        "No Dashboard++ answered for ${card.name.ifEmpty { card.id }} " +
                            "at its remembered addresses.",
                    ),
                )
            }

            ConnectMode.SEARCH ->
                // The sweep is the only door left: either the card has never connected,
                // or none of its remembered addresses is live any more.
                if (scanDone) CoreResult(NextAction.Failed(NO_ANSWER))
                else CoreResult(NextAction.ScanThenProbe(selectedId))
        }
    }

    // ------------------------------------------------------------------ the identity

    private fun resolveAnswer(
        vehicles: List<Vehicle>,
        selectedId: String?,
        answer: ProbeOutcome.Ok,
        mode: ConnectMode,
        nowMs: Long,
    ): CoreResult {
        val mac = VehicleMatch.normalizeMac(answer.mac)

        // No identity in the answer: the address is the whole claim, so it has to be
        // unambiguous on its own. See the class note for why this does not go through
        // VehicleMatch.resolve's first-claimant tie-break.
        if (mac == null) {
            val owners = vehicles.filter { it.ips.contains(answer.ip) }
            return when (owners.size) {
                1 -> toConnect(owners.first(), answer, selectedId, vehicles, nowMs)
                0 -> CoreResult(NextAction.Failed(UNCLAIMED))
                else -> CoreResult(NextAction.Failed(AMBIGUOUS))
            }
        }

        return when (val decision = VehicleMatch.resolve(vehicles, answer.ip, mac)) {
            // Rule 1 — a card claims this MAC. Rule 2 is unreachable here (it only
            // fires when no MAC was reported).
            is MatchDecision.Bound -> toConnect(decision.vehicle, answer, selectedId, vehicles, nowMs)

            // Rule 3 — an unbound card remembers this address and the unit that
            // answered reports a MAC nobody owns: bind them, name and photo intact.
            is MatchDecision.BindMac -> toConnect(decision.vehicle, answer, selectedId, vehicles, nowMs)

            // Nobody owns this unit. It is never written into the card that happened to
            // be selected — that is how two cards end up claiming one dashboard.
            is MatchDecision.Unknown -> CoreResult(
                next = if (mode == ConnectMode.SILENT) {
                    // Found unattended: the vehicle page shows the offer when it is
                    // opened; there is no ladder to have failed.
                    NextAction.Idle(UNCLAIMED)
                } else {
                    NextAction.Failed(UNCLAIMED)
                },
                adoption = UnclaimedUnit(answer.ip, answer.version, mac),
            )
        }
    }

    private fun toConnect(
        card: Vehicle,
        answer: ProbeOutcome.Ok,
        selectedId: String?,
        vehicles: List<Vehicle>,
        nowMs: Long,
    ): CoreResult {
        // Rule 5 — the answering address goes to the front of the list (the next cold
        // start probes in that order), the identity is adopted when one was reported
        // and kept when it was not, and the firmware version rides along because the
        // verify step already fetched it and the trips page needs it for the trip store.
        val bound = VehicleMatch.bindConnect(card, answer.ip, answer.mac, nowMs)
        val version = answer.version ?: bound.firmwareVersion
        return CoreResult(
            next = NextAction.Connect(
                vehicleId = card.id,
                ip = answer.ip,
                version = answer.version,
                mac = VehicleMatch.normalizeMac(answer.mac),
            ),
            persist = bound.copy(firmwareVersion = version),
            notice = noticeFor(card, selectedId, vehicles),
        )
    }

    /**
     * One line for the page when the connection belongs to a card other than the one
     * the user had selected (plan §4: "the selection moves to that card and the page
     * says so in one line"). It is not a failure and it is not silent: the page is
     * about to show a different vehicle than the card that was pressed.
     */
    private fun noticeFor(card: Vehicle, selectedId: String?, others: List<Vehicle>): String? {
        if (selectedId == null || selectedId == card.id) return null
        val pressed = others.firstOrNull { it.id == selectedId }
        val pressedName = pressed?.name?.ifEmpty { pressed.id } ?: selectedId
        return "The dashboard that answered belongs to ${card.name.ifEmpty { card.id}}, " +
            "not $pressedName."
    }
}
