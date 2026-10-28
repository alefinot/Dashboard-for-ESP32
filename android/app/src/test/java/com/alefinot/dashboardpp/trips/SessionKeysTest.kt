package com.alefinot.dashboardpp.trips

import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

/**
 * Which preference keys a deleted vehicle takes with it.
 *
 * The recorder keeps one row per *address* (`session:<ip>`) holding the open trip's
 * continuity: last accepted uptime, when the last sample landed, which file the drive is in.
 * Those rows belong to the card that was dialled, so forgetting a vehicle has to forget its
 * addresses' rows too - otherwise the next card that reaches that address inherits a
 * stranger's open trip.
 *
 * Only the key selection is testable here: the DataStore itself needs an Android `Context`,
 * and this project has no instrumented test source set. So the store asks this function
 * which keys to drop and removes exactly that list, which keeps the dangerous part - the
 * match - on the JVM side.
 */
class SessionKeysTest {

    @Test
    fun `an address names exactly one row`() {
        val keys = RecorderSettings.sessionKeysToRemove(
            existing = setOf("session:192.168.1.9", "session:192.168.1.30"),
            ips = listOf("192.168.1.9"),
        )
        assertEquals(listOf("session:192.168.1.9"), keys)
    }

    @Test
    fun `a shorter address never takes the longer one that starts with it`() {
        // The match is whole-key equality, not a prefix. "192.168.1.9" is a text prefix of
        // "192.168.1.99", and a prefix match here would silently delete another vehicle's
        // open trip - the kind of bug that only shows up as someone else's lost drive.
        val keys = RecorderSettings.sessionKeysToRemove(
            existing = setOf("session:192.168.1.9", "session:192.168.1.99", "session:192.168.1.109"),
            ips = listOf("192.168.1.9"),
        )
        assertEquals(listOf("session:192.168.1.9"), keys)
    }

    @Test
    fun `nothing else in the settings store is ever touched`() {
        // A card's addresses must not become an excuse to reset the recording switch, the
        // motion switch or the battery hint - those belong to the app, not to a vehicle.
        val existing = setOf(
            "recording_enabled", "motion_enabled", "notification_enabled",
            "battery_prompt_shown", "session:192.168.1.9",
        )
        val keys = RecorderSettings.sessionKeysToRemove(existing, listOf("192.168.1.9"))
        assertEquals(setOf("session:192.168.1.9"), keys.toSet())
    }

    @Test
    fun `an address the recorder never wrote about is simply absent`() {
        val keys = RecorderSettings.sessionKeysToRemove(setOf("session:192.168.1.9"), listOf("10.0.0.5"))
        assertTrue(keys.isEmpty())
    }

    @Test
    fun `the same address named twice still removes one row`() {
        // A card's `ips` list can hold an address twice (a SoftAP address seen on two
        // networks), and a duplicate in the removal list would ask the transaction to drop
        // the same key twice.
        val keys = RecorderSettings.sessionKeysToRemove(
            existing = setOf("session:192.168.4.1", "session:192.168.1.9"),
            ips = listOf("192.168.4.1", "192.168.4.1", "192.168.1.9"),
        )
        assertEquals(2, keys.size)
        assertTrue("session:192.168.4.1" in keys)
        assertTrue("session:192.168.1.9" in keys)
    }

    @Test
    fun `no addresses asks for no removals at all`() {
        // A card saved by hand with no remembered addresses is deleted without disturbing a
        // single row - and the store uses this to skip the write transaction entirely.
        assertTrue(RecorderSettings.sessionKeysToRemove(setOf("session:192.168.1.9"), emptyList()).isEmpty())
    }
}
