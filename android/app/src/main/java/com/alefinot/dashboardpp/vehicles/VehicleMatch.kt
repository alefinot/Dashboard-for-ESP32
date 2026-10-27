package com.alefinot.dashboardpp.vehicles

/** The app's answer to "which card is the dashboard that just answered?" */
sealed interface MatchDecision {
    /** A card already claims this unit — that card is it. */
    data class Bound(val vehicle: Vehicle) : MatchDecision

    /** A card was probed by address and answered with a MAC nobody claims: adopt it. */
    data class BindMac(val vehicle: Vehicle, val mac: String) : MatchDecision

    /** Nobody claims it (or nothing answers a claim): the page offers adoption. */
    data class Unknown(val ip: String, val mac: String?) : MatchDecision
}

/**
 * The identity rules, as pure functions.
 *
 * They live here rather than in a composable because the whole design is the
 * argument about which evidence ranks above which - and because a wrong answer
 * renames someone's car: it tags a drive to the wrong vehicle, moves CONNECTED to a
 * card the user never pressed, and quietly binds two different units together.
 *
 * The ranking is MAC first, address second. An address is a lease, not an identity:
 * DHCP moves a dashboard, and a phone's hotspot hands the same address to the next
 * thing that plugs in. Since the firmware reports its base eFuse MAC on `/api/perf`,
 * a card that has one no longer needs to guess.
 *
 * Everything MAC-shaped goes through [normalizeMac] first, so a card stored as
 * `a4:cf:12:3b:9e:07` (typed by hand, or written by an older build) still matches
 * the `A4:CF:12:3B:9E:07` the ESP prints. Addresses are compared as exact strings:
 * they are already what the app dialed.
 */
object VehicleMatch {

    /** How many addresses a card remembers - see [bindConnect]. */
    private const val MAX_IPS = 3

    /** Six hex pairs, colon-separated: exactly what `perfGetHandler` prints. */
    private val MAC_SHAPE = Regex("^([0-9A-F]{2}:){5}[0-9A-F]{2}$")

    /**
     * The MAC a card actually claims, or `null` if it has no usable identity.
     *
     * Going through [normalizeMac] here rather than trusting the stored text is
     * what keeps these rules total: a record holding `""` or a half-written value
     * is a card with *no* identity - still matchable by address, still upgradable
     * by rule 3 - instead of one that can never match anything and blocks the
     * address it remembers.
     */
    private fun claimedMac(v: Vehicle): String? = normalizeMac(v.mac)

    /** Whether a card is bound to a physical unit. */
    private fun isBound(v: Vehicle): Boolean = claimedMac(v) != null

    /**
     * Read a reported identity into the one form the app stores and compares.
     *
     * Case- and space-insensitive on the way in, canonical on the way out:
     * uppercase, colon-separated, no padding. Anything that is not six hex pairs
     * is `null` - an old firmware that has no `mac` field, a malformed body, a
     * half-typed editor value. Being strict about the shape is what makes the
     * stored text trustworthy: a lenient reader would let `A4CF123B9E07` into a
     * record and then never match it against the ESP's own spelling again.
     *
     * An all-zero MAC is *not* rejected. A unit whose MAC eFuse CRC is corrupt
     * does report `00:00:00:00:00:00`, and it is still the best (and only) string
     * that unit will ever give us; [macTakenBy] is what stops a second such unit
     * from claiming a card already holding it.
     */
    fun normalizeMac(raw: String?): String? {
        val text = raw?.filter { !it.isWhitespace() }?.uppercase() ?: return null
        return if (MAC_SHAPE.matches(text)) text else null
    }

    /**
     * Which card, if any, is the unit that answered at [ip] with [mac]?
     *
     * 1. A reported MAC matches the card that claims it - that wins over any address.
     * 2. `mac == null` (old firmware) matches only an unbound card whose `ips` holds
     *    the answering address, and only when no MAC-bound card claims that address.
     * 3. A card probed by address that answers with a MAC nobody claims gets it bound
     *    ([MatchDecision.BindMac]) - a one-time upgrade, name and photo intact. A MAC
     *    nobody claims and no address explains is [MatchDecision.Unknown]: the page
     *    offers adoption rather than borrowing a card that exists.
     *
     * Ties are resolved by list order, first claimant wins; [MatchDecision.Unknown]
     * is preferred over guessing when the evidence contradicts itself.
     */
    fun resolve(vehicles: List<Vehicle>, ip: String, mac: String?): MatchDecision {
        val reported = normalizeMac(mac)

        if (reported != null) {
            // Rule 1 - the identity is the whole answer, whatever the address says.
            val claimant = vehicles.firstOrNull { claimedMac(it) == reported }
            if (claimant != null) return MatchDecision.Bound(claimant)

            // Rule 3 - nobody owns this unit yet, but one card was dialed at this
            // address and got a reply. Bind them; the card's user already knows
            // which vehicle this is.
            val probed = vehicles.firstOrNull { !isBound(it) && it.ips.contains(ip) }
            if (probed != null) return MatchDecision.BindMac(probed, reported)

            return MatchDecision.Unknown(ip, reported)
        }

        // Rule 2 - old firmware: the address is the only evidence, and it is weak
        // evidence. A card that has an identity and remembers this address is the
        // likely owner of the lease, and it is not answering (a unit that could
        // report a MAC would have done so), so an unbound card must not step into
        // its place.
        if (vehicles.any { isBound(it) && it.ips.contains(ip) }) {
            return MatchDecision.Unknown(ip, null)
        }
        val unbound = vehicles.firstOrNull { !isBound(it) && it.ips.contains(ip) }
        if (unbound == null) return MatchDecision.Unknown(ip, null)
        return MatchDecision.Bound(unbound)
    }

    /**
     * Rule 4: the card that already claims [mac], or `null` if the editor may take
     * it. [exceptId] is the card being saved, which is allowed to keep its own
     * identity - without that, editing an existing vehicle would fail its own
     * uniqueness check.
     *
     * This is the only thing standing between a garage and two cards bound to the
     * same unit (or to the same bogus all-zero MAC a CRC-corrupt eFuse reports), so
     * it is checked on save, not only in [resolve].
     */
    fun macTakenBy(vehicles: List<Vehicle>, mac: String?, exceptId: String?): Vehicle? {
        val wanted = normalizeMac(mac) ?: return null
        return vehicles.firstOrNull { it.id != exceptId && claimedMac(it) == wanted }
    }

    /**
     * Rule 5: what a successful connect does to the record.
     *
     * The answering address goes to the **front** of `ips`, capped at 3 newest first:
     * the launch reconnect probes in that order, so old addresses are kept only as
     * long as they might still be reachable. A repeat connect of the address already
     * at the front changes nothing about the list - the network watcher fires often,
     * and four copies of one host would push the real alternatives out of the cap.
     *
     * [mac] is adopted when the unit reports one, and the card's existing identity
     * is kept when it does not: a connect that proves nothing about identity must not
     * un-bind a card that had earned it. A stored value that is not a MAC is dropped
     * in the same pass, which leaves the card honestly unbound instead of locked.
     */
    fun bindConnect(v: Vehicle, ip: String, mac: String?, nowMs: Long): Vehicle = v.copy(
        mac = normalizeMac(mac) ?: claimedMac(v),
        ips = (listOf(ip) + v.ips.filter { it != ip }).take(MAX_IPS),
        lastConnectedAtMs = nowMs,
    )
}
