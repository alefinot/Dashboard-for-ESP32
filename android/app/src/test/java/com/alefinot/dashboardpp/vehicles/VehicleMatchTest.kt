package com.alefinot.dashboardpp.vehicles

import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Test

/**
 * Which garage card is the dashboard that just answered.
 *
 * The garage card is the user's, so getting this wrong is not a data bug - it renames
 * someone's car. Before the firmware reported its eFuse MAC the only evidence was the
 * address, and an address is a lease, not an identity: DHCP moves a dashboard off
 * 192.168.1.42, and a phone tethering hands that same address to whatever plugs in
 * next. So the rules below rank the evidence (MAC first, address only as a fallback,
 * and never as a licence to touch a card that already has an identity), and they are
 * here as a pure function so the argument happens in this file instead of inside a
 * composable on a tablet in a car.
 */
class VehicleMatchTest {

    private val macA = "A4:CF:12:3B:9E:07"
    private val macB = "B0:97:3A:1C:DD:2F"

    private fun card(
        id: String,
        mac: String? = null,
        ips: List<String> = emptyList(),
    ) = Vehicle(id = id, mac = mac, name = id, ips = ips)

    @Test
    fun `an identity is read case- and space-insensitively but must be six hex pairs`() {
        // What the firmware prints (task 1), and what it prints after a reboot:
        // identical, uppercase, colon-separated.
        assertEquals("A4:CF:12:3B:9E:07", VehicleMatch.normalizeMac("A4:CF:12:3B:9E:07"))
        // Lower-case arrives from a human typing the label off the back of the unit.
        assertEquals("A4:CF:12:3B:9E:07", VehicleMatch.normalizeMac("a4:cf:12:3b:9e:07"))
        // Padded by a JSON field or a paste buffer.
        assertEquals("A4:CF:12:3B:9E:07", VehicleMatch.normalizeMac("  A4:CF:12:3B:9E:07  "))
        // An *internal* space is repaired too, and that is a decision rather than
        // an accident: normalizeMac strips every whitespace character, so a value
        // mangled by a line-wrapped backup file or a hand-edited record still
        // resolves. Nothing else here pins that half of the leniency, and without
        // this line a later task could tighten it to trim() and silently un-bind
        // every card that happened to store such a value.
        assertEquals("A4:CF:12:3B:9E:07", VehicleMatch.normalizeMac("A4:CF:12:3B: 9E:07"))

        // Everything else means "no identity", not "an odd-looking one": an old
        // firmware that has no `mac` field at all (null / ""), a value the ESP
        // would never print, and half a MAC. Keeping the shape strict is what
        // makes the stored string canonical - the codec in the next task round-
        // trips exactly this text, so a lenient reader would let "A4CF123B9E07"
        // into a record and then never match it again.
        assertNull(VehicleMatch.normalizeMac(null))
        assertNull(VehicleMatch.normalizeMac(""))
        assertNull(VehicleMatch.normalizeMac("   "))
        assertNull(VehicleMatch.normalizeMac("A4CF123B9E07"))
        assertNull(VehicleMatch.normalizeMac("A4-CF-12-3B-9E-07"))
        assertNull(VehicleMatch.normalizeMac("A4:CF:12:3B:9E"))
        assertNull(VehicleMatch.normalizeMac("A4:CF:12:3B:9E:07:11"))
        assertNull(VehicleMatch.normalizeMac("A4:CF:12:3B:9E:0"))
        assertNull(VehicleMatch.normalizeMac("ZZ:CF:12:3B:9E:07"))
        assertNull(VehicleMatch.normalizeMac("mac"))
    }

    @Test
    fun `a reported identity outranks the card that happens to hold the address`() {
        // Rule 1. The user pressed the card that remembers the address; the unit
        // that answered is somebody else's. The MAC has to win or the wrong card
        // turns CONNECTED and the trip gets tagged to the wrong vehicle.
        val pressed = card("address-only", ips = listOf("192.168.1.42", "10.0.0.9"))
        val owner = card("bound-to-mac", mac = macA, ips = listOf("10.0.0.9"))

        val decision = VehicleMatch.resolve(listOf(pressed, owner), "192.168.1.42", macA)

        assertEquals(MatchDecision.Bound(owner), decision)
    }

    @Test
    fun `a card that stored its identity in lower case still owns that unit`() {
        // Rule 1 again, from the other direction: the codec round-trips the
        // record's `mac` text verbatim (it is not the store's job to sanitize),
        // so a value written by hand, by an older build, or typed by the user in
        // the editor reaches `claimedMac` in whatever case it was stored in. The
        // ESP always prints upper case, so if the comparison were case-sensitive
        // the card would look unbound, a second card would be offered the same
        // unit, and rule 4 would never see the collision.
        val stored = card("hatch", mac = "a4:cf:12:3b:9e:07", ips = listOf("10.0.0.9"))

        assertEquals(
            MatchDecision.Bound(stored),
            VehicleMatch.resolve(listOf(stored), "192.168.1.42", macA),
        )

        // The same card is the one rule 4 reports, for the same reason.
        assertEquals(
            stored,
            VehicleMatch.macTakenBy(listOf(stored, card("bike", mac = macB)), macA, null),
        )
    }

    @Test
    fun `old firmware with no identity matches the unbound card that holds that address`() {
        // Rule 2, the fallback that keeps firmware before task 1 usable: the
        // address is the only evidence there is, and the card has no identity to
        // betray.
        val garage = card("address-only", ips = listOf("192.168.1.42"))

        assertEquals(
            MatchDecision.Bound(garage),
            VehicleMatch.resolve(listOf(garage), "192.168.1.42", null),
        )
    }

    @Test
    fun `an unbound card may not borrow an address a MAC-bound card already claims`() {
        // Rule 2's negative half, and the reason the rule is worth testing: two
        // cards remember 192.168.1.42. One of them has an identity, so that
        // address is *its* lease - a unit answering there without a MAC (old
        // firmware, or a MAC the eFuse CRC rejected) is more likely that card's
        // dashboard running older code than a licence to move the spare card onto
        // it. Unknown is honest; Bound(spare) would silently mislabel.
        val claimed = card("bound-to-mac", mac = macA, ips = listOf("192.168.1.42"))
        val spare = card("address-only", ips = listOf("192.168.1.42"))

        assertEquals(
            MatchDecision.Unknown(ip = "192.168.1.42", mac = null),
            VehicleMatch.resolve(listOf(claimed, spare), "192.168.1.42", null),
        )
    }

    @Test
    fun `an address-probed card that answers with an unclaimed identity takes it`() {
        // Rule 3: the one-time upgrade. The card was made by address before the
        // firmware knew it had a MAC; from now on it is bound, name and photo
        // intact. The MAC in the decision is normalised, because that is what
        // gets written to the record.
        val pressed = card("address-only", ips = listOf("192.168.1.42"))

        assertEquals(
            MatchDecision.BindMac(pressed, macA),
            VehicleMatch.resolve(listOf(pressed), "192.168.1.42", "a4:cf:12:3b:9e:07"),
        )
    }

    @Test
    fun `an identity and an address nobody claims is a new unit, offered for adoption`() {
        // Rule 3's limit: a MAC nobody holds must never be glued to a card just
        // because that card exists. Task 4 shows "a dashboard nobody knows" and
        // asks the user to name it.
        val other = card("bound-to-mac", mac = macA, ips = listOf("10.0.0.9"))

        assertEquals(
            MatchDecision.Unknown(ip = "192.168.1.77", mac = macB),
            VehicleMatch.resolve(listOf(other), "192.168.1.77", macB),
        )

        // An empty garage answers the same way - the first dashboard the app ever
        // meets is adopted, not auto-created.
        assertEquals(
            MatchDecision.Unknown(ip = "192.168.1.77", mac = macB),
            VehicleMatch.resolve(emptyList(), "192.168.1.77", macB),
        )
    }

    @Test
    fun `the same identity can never be claimed by two cards`() {
        // Rule 4: the editor's refusal. It has to point at the card that already
        // has the MAC, and it has to allow the card that owns it to keep it while
        // being edited - otherwise saving an existing card would fail its own
        // uniqueness check.
        val owner = card("hatch", mac = macA)
        val claimer = card("bike", mac = macB)

        assertEquals(owner, VehicleMatch.macTakenBy(listOf(owner, claimer), macA, null))
        assertEquals(
            owner,
            VehicleMatch.macTakenBy(listOf(owner, claimer), "a4:cf:12:3b:9e:07", claimer.id),
        )
        // Editing the owner itself is not a collision with itself.
        assertNull(VehicleMatch.macTakenBy(listOf(owner, claimer), macA, owner.id))
        // Nothing to check for an absent or malformed MAC, and a free MAC is free.
        assertNull(VehicleMatch.macTakenBy(listOf(owner, claimer), null, null))
        assertNull(VehicleMatch.macTakenBy(listOf(owner, claimer), "not a mac", null))
        assertNull(VehicleMatch.macTakenBy(listOf(owner, claimer), "FF:FF:FF:FF:FF:FF", null))
    }

    @Test
    fun `a successful connect moves the answering address to the front and caps the list`() {
        // Rule 5. Newest first matters because the silent reconnect at launch
        // probes these addresses in order, and a stale address tried first costs
        // the two-second connect timeout before the good one is reached.
        val garage = card(
            "hatch",
            mac = macA,
            ips = listOf("192.168.1.50", "192.168.1.42", "10.0.0.9", "172.16.4.8"),
        )

        val updated = VehicleMatch.bindConnect(garage, "192.168.1.42", macA, 1_700_000_000_000L)

        assertEquals(listOf("192.168.1.42", "192.168.1.50", "10.0.0.9"), updated.ips)
        assertEquals(1_700_000_000_000L, updated.lastConnectedAtMs)
        // The identity survives a connect that reports it, and nothing else about
        // the user's card moves.
        assertEquals(macA, updated.mac)
        assertEquals(garage.name, updated.name)
        assertEquals(garage.id, updated.id)
    }

    @Test
    fun `reconnecting the address already at the front does not duplicate it`() {
        // The silent reconnect runs on every network event; a list of the same
        // address four times deep would push the real alternatives past the cap
        // and the launch probe would spend its budget on one dead host.
        val garage = card("hatch", ips = listOf("192.168.1.42", "192.168.1.50"))

        val updated = VehicleMatch.bindConnect(garage, "192.168.1.42", null, 1_700_000_000_000L)

        assertEquals(listOf("192.168.1.42", "192.168.1.50"), updated.ips)
    }

    @Test
    fun `a connect that reports no identity keeps the identity the card already had`() {
        // Old firmware, or a unit answering at a different address than the one
        // probed: dropping the MAC here would un-bind a card that had earned it.
        val garage = card("hatch", mac = macA, ips = listOf("192.168.1.50"))

        val updated = VehicleMatch.bindConnect(garage, "192.168.1.42", null, 1_700_000_000_000L)

        assertEquals(macA, updated.mac)
        assertEquals(listOf("192.168.1.42", "192.168.1.50"), updated.ips)
    }

    @Test
    fun `an all-zero identity is just another string, and still cannot be claimed twice`() {
        // Carried from task 1's review: ESP.getEfuseMac() discards the IDF return
        // code, so a unit whose MAC eFuse CRC is corrupt reports a plausible
        // looking value - commonly 00:00:00:00:00:00 - and two such units would
        // report the same one. The firmware fix is out of this plan's scope; the
        // app side has to be honest about it instead of pretending the value is
        // unique. It is not filtered (a real device could legally be all-zero on
        // some board revision, and silently treating it as "no MAC" would throw
        // away the only identity two such units can be told apart by *nothing*),
        // so what protects the garage is rule 4: the second unit is refused, and
        // resolve answers deterministically for the card that got there first.
        val allZero = "00:00:00:00:00:00"
        assertEquals(allZero, VehicleMatch.normalizeMac(allZero))

        val first = card("hatch", mac = allZero)
        val second = card("bike", mac = macB)
        assertEquals(first, VehicleMatch.macTakenBy(listOf(first, second), allZero, second.id))

        // Deterministic, not a coin flip: the store hands `resolve` its display
        // order, and the first claimant wins. Both units answering with the same
        // bogus MAC therefore land on one card every time, never alternating.
        assertEquals(
            MatchDecision.Bound(first),
            VehicleMatch.resolve(listOf(first, second), "192.168.1.42", allZero),
        )
    }

    @Test
    fun `a card holding a value that is not an identity counts as unbound, not as locked`() {
        // A record whose `mac` text is unusable (an empty field from a codec, a
        // hand-edited file) must not become a card that can never match anything:
        // it is still matchable by address, still upgradable, and it must not hold
        // the address hostage from a card that has no identity at all.
        val corrupt = card("half-written", mac = "", ips = listOf("192.168.1.42"))

        assertEquals(
            MatchDecision.Bound(corrupt),
            VehicleMatch.resolve(listOf(corrupt), "192.168.1.42", null),
        )
        // And the next connect repairs it.
        assertEquals(
            MatchDecision.BindMac(corrupt, macA),
            VehicleMatch.resolve(listOf(corrupt), "192.168.1.42", macA),
        )
        assertNull(VehicleMatch.macTakenBy(listOf(corrupt), macA, null))

        // A connect that reports nothing leaves nothing junk behind.
        val written = VehicleMatch.bindConnect(corrupt, "192.168.1.42", null, 1_700_000_000_000L)
        assertNull(written.mac)
    }
}
