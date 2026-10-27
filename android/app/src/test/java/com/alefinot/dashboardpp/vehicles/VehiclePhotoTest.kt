package com.alefinot.dashboardpp.vehicles

import java.io.File
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

/**
 * The pure half of [VehiclePhotoStore] (vehicle page plan §3) - the two decisions
 * worth arguing about, both of which a wrong answer makes expensive:
 *
 * - [VehiclePhotoStore.sampleSizeFor] is the memory story. This file exists so a
 *   garage of six cars is six small JPEGs rather than six 4000x3000 bitmaps, and the
 *   factor chosen here is what a decode allocates. Too small and the app dies in a
 *   thumbnail grid; too large and the card goes soft for no reason.
 * - [VehiclePhotoStore.photoFileIn] is the data-loss story. `Vehicle.photoFile` is
 *   text from a preferences row, and the store hands the name it returns to
 *   `File.delete()`. A name that escapes the vehicles dir is a delete outside it.
 *
 * The rest of the class is [android.graphics.BitmapFactory] and [java.io.File] calls
 * with nothing to assert off-device - the JVM's android.jar stubs throw - which is
 * why the build gate for this file is `assembleDebug`, not only the unit tests.
 */
class VehiclePhotoTest {

    private val dir = File(File(File.separator + "storage", "emulated"), "0/Android/data/com.alefinot.dashboardpp/files/vehicles")

    // ---- sampleSizeFor: the box, and the RAM it buys -------------------------

    @Test
    fun `a 1280 request on a phone-camera photo is 4, not 8 and not 2`() {
        // 4000/2 = 2000 is still outside a 1280 box, so 2 decodes 2000x1500 - 12 MB of
        // pixels the caller cannot show. 4000/4 = 1000 is inside it, and 8 would throw
        // away detail a 1280 card would have used (4000/8 = 500).
        assertEquals(4, VehiclePhotoStore.sampleSizeFor(4000, 3000, 1280))
    }

    @Test
    fun `the longest edge decides, not the width`() {
        // A rotated capture is the same pixels, so it must get the same factor...
        assertEquals(
            VehiclePhotoStore.sampleSizeFor(4000, 3000, 1280),
            VehiclePhotoStore.sampleSizeFor(3000, 4000, 1280),
        )
        // ...and a tall source whose *width* fits must still be sub-sampled. A rule
        // looking at the width alone would answer 2 for 2000x4000 and leave a 2000 px
        // edge - 8 MB of pixels - standing in a 1280 box.
        assertEquals(4, VehiclePhotoStore.sampleSizeFor(2000, 4000, 1280))
    }

    @Test
    fun `a source already inside the box is decoded whole`() {
        // Sub-sampling an 800x600 photo for a 1280 box would cost detail and buy
        // nothing: the factor is 1, and nothing scales up later either.
        assertEquals(1, VehiclePhotoStore.sampleSizeFor(800, 600, 1280))
        assertEquals(1, VehiclePhotoStore.sampleSizeFor(1280, 720, 1280))
        assertEquals(1, VehiclePhotoStore.sampleSizeFor(100, 100, 1280))
    }

    @Test
    fun `a box one pixel smaller doubles the factor`() {
        // The edge is inclusive: 1280 is inside a 1280 box and outside a 1279 one.
        // Pinned because the loop condition is a `>` and an off-by-one here decodes
        // twice the pixels for every card in the grid.
        assertEquals(1, VehiclePhotoStore.sampleSizeFor(1280, 720, 1280))
        assertEquals(2, VehiclePhotoStore.sampleSizeFor(1280, 720, 1279))
    }

    @Test
    fun `a card thumbnail request on the same photo is 16`() {
        // The arithmetic the report quotes: a 160 dp card at density 2.0 asks for a
        // 320 px box, so 4000x3000 decodes to 250x187 - about 183 KB - where the same
        // file asked at 1280 is 1000x750 (3 MB) and decoded whole would be 48 MB.
        // That is the difference between a garage of six scrolling and an app killed by
        // the system for its memory, and the 8 MB cache bound is what caps the total.
        assertEquals(16, VehiclePhotoStore.sampleSizeFor(4000, 3000, 320))
        assertEquals(250, 4000 / 16)
        assertEquals(187, 3000 / 16)
    }

    @Test
    fun `a 160 dp box at the densities this app ships to stays a thumbnail`() {
        // density 1.0 -> 160 px, 1.5 -> 240, 2.0 -> 320, 3.0 -> 480, 3.5 -> 560. Even
        // the biggest of those decodes the stored 1000x750 JPEG (or a whole capture)
        // to at most 500x375 - about 750 KB, not 48 MB - so a grid of six cards cannot
        // reach the cache bound on thumbnails alone.
        for (edge in listOf(160, 240, 320, 480, 560)) {
            val s = VehiclePhotoStore.sampleSizeFor(4000, 3000, edge)
            assertTrue("$edge px box -> $s", 4000 / s <= edge)
            assertTrue("$edge px box decodes only ${4000 / s} px", 4000 / s >= edge / 2)
        }
    }

    @Test
    fun `the factor always leaves the decoded image inside the box and no smaller than needed`() {
        // Both halves in one assertion, across a table of real shapes:
        // - `longest / s <= maxEdge` - the decode never overshoots the request;
        // - `longest / (s/2) > maxEdge` - halving the factor would overshoot, so the
        //   decode never undershoots it either (the "4, not 8" half).
        val shapes = listOf(
            Triple(4000, 3000, 1280), Triple(3000, 4000, 1280), Triple(5000, 5000, 1280),
            Triple(1920, 1080, 1280), Triple(4000, 3000, 320), Triple(4000, 3000, 640),
            Triple(1280, 720, 1280), Triple(800, 600, 1280), Triple(640, 480, 160),
        )
        for ((w, h, edge) in shapes) {
            val s = VehiclePhotoStore.sampleSizeFor(w, h, edge)
            val longest = maxOf(w, h)
            assertTrue("$s for $w x $h at $edge", longest / s <= edge)
            if (s > 1) assertTrue("factor $s undershot $w x $h at $edge", longest / (s / 2) > edge)
        }
    }

    @Test
    fun `an unusable request answers 1 rather than 0 or a negative factor`() {
        // inSampleSize <= 0 is not a thing a decoder understands, and a 0x0 source is
        // what a torn JPEG header reports. The pure function stays total; the callers
        // decide (read() rejects a non-positive box, decodeScaled() rejects 0x0).
        assertEquals(1, VehiclePhotoStore.sampleSizeFor(0, 0, 1280))
        assertEquals(1, VehiclePhotoStore.sampleSizeFor(-4000, 3000, 1280))
        assertEquals(1, VehiclePhotoStore.sampleSizeFor(4000, -3000, 1280))
        assertEquals(1, VehiclePhotoStore.sampleSizeFor(4000, 3000, 0))
        assertEquals(1, VehiclePhotoStore.sampleSizeFor(4000, 3000, -1280))
    }

    @Test
    fun `the doubling stops at a positive factor whatever the dimensions claim`() {
        // A corrupt header can report an absurd size. The loop must not run away into
        // a negative factor (an Int that overflows while doubling) - 2^30 is the
        // largest factor any positive Int dimension can ask for at a box of 1.
        val huge = VehiclePhotoStore.sampleSizeFor(Int.MAX_VALUE, Int.MAX_VALUE, 1)
        assertEquals(1 shl 30, huge)
        assertEquals(2, VehiclePhotoStore.sampleSizeFor(Int.MAX_VALUE, Int.MAX_VALUE, 1 shl 30))
    }

    // ---- photoFileIn: a stored name is never trusted as a path ---------------

    @Test
    fun `a bare file name resolves inside the vehicles dir`() {
        val f = VehiclePhotoStore.photoFileIn(dir, "v1.jpg")
        assertNotNull(f)
        assertEquals("v1.jpg", f!!.name)
        assertEquals(dir, f.parentFile)
    }

    @Test
    fun `a name that walks out of the vehicles dir is refused`() {
        // Vehicle.photoFile comes from a preferences row, and the resolved File is
        // what delete() is handed. Any of these would remove a file the vehicles dir
        // does not own - the app's own trip CSVs (`../trips/trip-….csv`), the
        // database, or on a device with a broader path, anything the app can reach.
        for (bad in listOf(
            "../etc/x",
            "../trips/trip-20240101-000000-001.csv",
            "a/../etc/x",
            "a/b",
            "vehicles/../../x.jpg",
            "..\\..\\windows\\x.jpg",
            "/sdcard/Download/photo.jpg",
            "C:\\Users\\someone\\photo.jpg",
        )) {
            assertNull("accepted \"$bad\"", VehiclePhotoStore.photoFileIn(dir, bad))
        }
    }

    @Test
    fun `an empty name or a bare dot is refused because it resolves to the directory itself`() {
        // File(vehiclesDir, "") and File(vehiclesDir, ".") are both *vehiclesDir*, so
        // either would hand delete() the whole directory. A name that is merely odd
        // (`my photo.jpg`) is not the guard's business - it still names one file
        // inside the dir, which is the only thing this check is for.
        assertNull(VehiclePhotoStore.photoFileIn(dir, ""))
        assertNull(VehiclePhotoStore.photoFileIn(dir, "."))
    }

    @Test
    fun `a null photoFile resolves to no photo`() {
        // Vehicle.photoFile is nullable; a null record field must resolve to "no
        // photo" the same way a refused name does, so the card shows its placeholder.
        assertNull(VehiclePhotoStore.photoFileIn(dir, null))
    }

    @Test
    fun `a name with a control character in it is refused`() {
        // A NUL inside a path string is where a Java File and the native layer stop
        // agreeing about which file is meant; junk like that does not come from
        // writeFrom, so refusing it costs nothing and closes the argument.
        assertNull(VehiclePhotoStore.photoFileIn(dir, "v1.jpg\u0000.png"))
        assertNull(VehiclePhotoStore.photoFileIn(dir, "v1.jpg\n../../x"))
    }
}
