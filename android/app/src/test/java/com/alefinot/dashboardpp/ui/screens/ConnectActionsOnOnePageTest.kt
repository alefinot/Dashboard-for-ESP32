package com.alefinot.dashboardpp.ui.screens

import java.io.File
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

/**
 * The rule that "connect / disconnect is reachable from exactly one place" is a rule about
 * where buttons **are**, and nothing that runs at test time can see a button that was never
 * composed. So this guard reads the sources: it checks which screen files call a connection
 * action, and fails the day one grows back somewhere else.
 *
 * It exists because the app used to have four ways to connect - a hunting screen, a scan
 * screen, two covers offering FIND DEVICE / ENTER IP / WI-FI SETTINGS, and App Settings with
 * ENTER IP MANUALLY / RE-SCAN NETWORK - and each had its own idea of what the app was looking
 * for. The plan's answer ([plan §5], [plan §7]) is one page: the vehicle page's cards and its
 * ADD VEHICLE card. A cover may now only say which vehicle it is waiting for and offer to take
 * you there.
 *
 * The scan looks for **calls to the view model's connection actions**, not for words: a comment
 * may talk about connecting all it likes, and App Settings legitimately still prints the
 * connection's address. A call is the thing that can actually connect.
 */
class ConnectActionsOnOnePageTest {

    /** Everything the view model offers that starts, stops or chooses a connection. */
    private val actionCall =
        Regex("\\b(rescan|retryNoWifi|connectVehicle|disconnect|selectVehicle|submitManualIp)\\s*\\(")

    /**
     * The vehicle page, plus [ManualEntryPanel], which the vehicle page raises and nothing
     * else does - which [manualAddressPanelIsRaisedOnlyFromTheVehiclePage] is what keeps true.
     */
    private val allowed = listOf("ManualEntryPanel.kt", "VehicleScreen.kt")

    @Test
    fun connectionActionsAreCalledFromTwoScreenFilesOnly() {
        val found = screenFiles().filter { actionCall.containsMatchIn(codeOnly(it.readText())) }
            .map { it.name }.sorted()
        assertEquals(
            "a connection action may only be offered by the vehicle page (and the address panel " +
                "it raises); every other page has to point at the vehicle page instead",
            allowed,
            found,
        )
    }

    /** The covers' whole vocabulary: one action, and it is navigation, not connection. */
    @Test
    fun offlineCoverOffersOnlyTheWayToTheVehiclePage() {
        val src = moduleSource("OfflineCover.kt")
        val actions = Regex("HudButton\\(\\s*\"([^\"]+)\"").findAll(src)
            .map { it.groupValues[1] }.toList()
        assertEquals(
            "a cover must offer exactly one action, the one that opens the vehicle page; found: $actions",
            listOf("OPEN VEHICLE PAGE"),
            actions,
        )
        assertFalse("a cover must not reach for the view model", codeOnly(src).contains("ConnectionViewModel"))
        val body = codeOnly(src)
        for (gone in listOf("rescan", "retryNoWifi", "ENTER IP", "FIND DEVICE", "WI-FI", "onFind", "rememberedIp")) {
            assertFalse("OfflineCover grew connect furniture again ($gone)", body.contains(gone))
        }
    }

    /** App Settings keeps its switches, its storage figures and its address readout, and loses its device block. */
    @Test
    fun appSettingsHasNoConnectionActions() {
        val src = moduleSource("SettingsScreen.kt")
        val offenders = actionCall.findAll(codeOnly(src)).map { it.value }.toList()
        assertEquals(
            "App Settings must not run a connection - a device belongs to a vehicle, so those " +
                "actions live on the vehicle page only",
            emptyList<String>(),
            offenders,
        )
        assertFalse("App Settings may not raise the address panel", src.contains("onEnterIp"))
    }

    /** The address panel is raised from one place, and both covers get the way to the garage. */
    @Test
    fun manualAddressPanelIsRaisedOnlyFromTheVehiclePage() {
        val src = moduleSource("ConnectedTabs.kt")
        assertEquals(
            "exactly one page may raise the manual-address panel",
            1,
            Regex("onEnterIp = \\{ manualOpen = true \\}").findAll(src).count(),
        )
        assertEquals(
            "both covers must be handed the way to the vehicle page",
            2,
            Regex("onOpenVehicles = openGarage").findAll(src).count(),
        )
    }

    /**
     * Comments stripped out. Half the point of this redesign is written down in doc comments
     * that name the controls they removed - [OfflineCover]'s says in prose why there is no FIND
     * DEVICE button - and a guard that read those would fail on the explanation of its own rule.
     */
    private fun codeOnly(src: String): String = src
        .replace(Regex("/[*].*?[*]/", RegexOption.DOT_MATCHES_ALL), " ")
        .replace(Regex("//.*"), " ")

    private fun screenFiles(): List<File> {
        val kt = screensDir().listFiles { f: File -> f.isFile && f.name.endsWith(".kt") }
        assertTrue("no .kt files under ${screensDir()}", !kt.isNullOrEmpty())
        return kt!!.toList()
    }

    private fun screensDir() = File(moduleDir(), "src/main/java/com/alefinot/dashboardpp/ui/screens")

    private fun moduleSource(name: String): String {
        val f = File(screensDir(), name)
        assertTrue("cannot find $f", f.isFile)
        return f.readText()
    }

    /**
     * The app module's directory. Gradle runs a module's unit tests with that module as the
     * working directory, but the CLI and the IDE disagree about it, so each level upward is
     * tried with the plausible prefixes before giving up. Same reasoning as
     * `AppXmlWiringTest.moduleFile`: a guard test that fails because it could not find its own
     * input is worse than no guard test, because it teaches you to ignore it.
     */
    private fun moduleDir(): File {
        val marker = "src/main/java/com/alefinot/dashboardpp/ui/screens"
        var dir: File? = File(System.getProperty("user.dir"))
        while (dir != null) {
            for (cand in listOf(File(dir, "android/app"), File(dir, "app"), dir)) {
                if (File(cand, marker).isDirectory) return cand
            }
            dir = dir.parentFile
        }
        throw IllegalStateException(
            "cannot reach the app module walking up from " + System.getProperty("user.dir"),
        )
    }
}
