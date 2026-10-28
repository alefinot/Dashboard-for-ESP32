package com.alefinot.dashboardpp.vehicles

import org.junit.Assert.assertTrue
import org.junit.Test
import java.io.File

/**
 * The parts of this app that are configured in XML, which the compiler cannot see.
 *
 * Two of those pieces of wiring decide whether the editor's photo buttons work, and both fail
 * in the same unhelpful way - by making the app say something that is not true:
 *
 * - **The FileProvider roots.** A camera capture is handed to another app as a `content://`
 *   URI over one of the roots in `res/xml/files_path.xml`. If the directory
 *   [VehiclePhotoStore.captureFile] writes into is not listed, `getUriForFile` throws
 *   `IllegalArgumentException`, the store answers "no capture to offer", and the editor tells
 *   the user there is no camera on a tablet that has one. Nothing compiles differently, no lint
 *   check notices; on a real tablet the only symptom is that sentence. (This file exists
 *   because that exact mistake was made in this branch: `captures/` was declared in the store's
 *   code and in its docs, and never added to the XML. It took a tablet to notice.)
 * - **Package visibility.** From Android 11 an app that does not declare the `IMAGE_CAPTURE`
 *   action in `<queries>` cannot resolve the camera activity, so `TAKE PHOTO` can only ever
 *   report that there is no camera - on API 28 the same code works fine, which is how this one
 *   survives review on a development tablet.
 *
 * So these tests reach for the source **text** rather than the running objects. A JVM unit test
 * has no Android `Context`, so it cannot ask the store for a capture file or the provider for a
 * URI, and the assertion that actually matters is that the names which have to agree at runtime
 * appear in the two files that mention them. Text tests on purpose: brittle in the way that
 * matters (rename the directory, or drop the block, and this fails), cheap in the way that
 * matters (no emulator, no Robolectric, no tablet).
 */
class AppXmlWiringTest {

    /** The scratch capture directory has to be a published root, or a capture has no URI. */
    @Test
    fun `the scratch capture directory is a declared provider root`() {
        val xml = moduleFile("src/main/res/xml/files_path.xml")
        assertTrue(
            "files_path.xml must publish the scratch capture root, or a camera capture has no " +
                "URI to hand over: " + xml.lines().joinToString(" | "),
            Regex("path\\s*=\\s*\"captures/\"").containsMatchIn(xml),
        )
    }

    /** The trip share came first, and must not be dropped while adding another root. */
    @Test
    fun `the trip export root is still declared`() {
        val xml = moduleFile("src/main/res/xml/files_path.xml")
        assertTrue(
            "files_path.xml lost the exports root the share sheet depends on",
            Regex("path\\s*=\\s*\"exports/\"").containsMatchIn(xml),
        )
    }

    /**
     * The other half of the first agreement: the name the store builds its scratch path from.
     * If this constant and the XML drift apart the failure is silent at runtime, so it is loud
     * here instead.
     */
    @Test
    fun `the store names its capture directory the way the provider declares it`() {
        val src = moduleFile("src/main/java/com/alefinot/dashboardpp/vehicles/VehiclePhotoStore.kt")
        val m = Regex("CAPTURE_DIR\\s*=\\s*\"([^\"]+)\"").find(src)
        if (m == null) {
            // Assert.fail returns void, which cannot be the right-hand side of an elvis; the
            // throw is what keeps `m` a MatchResult below.
            throw AssertionError(
                "VehiclePhotoStore no longer declares CAPTURE_DIR - update this test and " +
                    "files_path.xml together",
            )
        }
        val xml = moduleFile("src/main/res/xml/files_path.xml")
        assertTrue(
            "the store writes captures to \"${m.groupValues[1]}\" while files_path.xml publishes " +
                "something else - getUriForFile will throw and the editor will claim there is no camera",
            xml.contains("path=\"${m.groupValues[1]}/\""),
        )
    }

    /**
     * Without this block the camera route is dead on Android 11+ while working on the API 28
     * tablet it was developed against, and the only evidence in the field is a button that
     * insists there is no camera.
     */
    @Test
    fun `the manifest declares visibility for the camera capture intent`() {
        val manifest = moduleFile("src/main/AndroidManifest.xml")
        val queries = Regex("<queries>(.*?)</queries>", RegexOption.DOT_MATCHES_ALL).find(manifest)
        if (queries == null) {
            throw AssertionError(
                "AndroidManifest.xml has no <queries> block: on API 30+ the editor cannot resolve " +
                    "an IMAGE_CAPTURE intent, so TAKE PHOTO can only report that no camera exists",
            )
        }
        assertTrue(
            "the <queries> block no longer covers IMAGE_CAPTURE - TAKE PHOTO will not launch a " +
                "camera on Android 11+",
            queries.value.contains("android.media.action.IMAGE_CAPTURE"),
        )
    }

    /**
     * Module-relative read. Gradle starts unit tests in the module directory (`android/app`) but
     * prefixes differ across configurations and IDEs, so each level from here upward is tried
     * with the plausible module prefixes before giving up. A guard test that fails because it
     * could not find its own input is worse than no guard test: it teaches you to ignore it.
     */
    private fun moduleFile(rel: String): String {
        val prefixes = listOf("", "app/", "android/app/")
        var dir: File? = File(System.getProperty("user.dir"))
        while (dir != null) {
            for (p in prefixes) {
                val f = File(dir, p + rel)
                if (f.isFile) return f.readText()
            }
            dir = dir.parentFile
        }
        throw IllegalStateException(
            "cannot find $rel (or app/$rel, or android/app/$rel) walking up from " +
                System.getProperty("user.dir"),
        )
    }
}
