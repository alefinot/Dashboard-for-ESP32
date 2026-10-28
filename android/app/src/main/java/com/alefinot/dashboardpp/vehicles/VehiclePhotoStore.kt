package com.alefinot.dashboardpp.vehicles

import android.content.Context
import android.graphics.Bitmap
import android.graphics.BitmapFactory
import android.graphics.BitmapRegionDecoder
import android.graphics.Rect
import android.net.Uri
import android.util.LruCache
import androidx.core.content.FileProvider
import java.io.File
import java.io.FileInputStream
import java.io.FileOutputStream
import kotlin.math.max
import kotlin.math.roundToInt

/**
 * One JPEG per garage card (vehicle page plan §3): `<vehicles dir>/<id>.jpg`.
 *
 * This class exists because of a memory problem, not a feature problem. A phone
 * camera photo is 4000x3000 and more; a card thumbnail is a few centimetres wide.
 * If the app kept the capture as it came off the camera and decoded it whole, a
 * garage of six cars would be six full-size bitmaps in flight - tens of megabytes,
 * on the phones this app ships to, for pictures seen through a 160 dp box. Three
 * rules keep that bounded:
 *
 * 1. **Only a downscaled copy is ever kept.** [writeFrom] decodes with
 *    [sampleSizeFor], trims the result to [MAX_STORED_EDGE] on the longest edge and
 *    re-encodes at [JPEG_QUALITY]; a 4000x3000 capture lands as roughly 1000x750,
 *    a few hundred KB on disk. The capture file itself is deleted once the copy has
 *    landed, so there is no full-size second copy to find later.
 * 2. **A read decodes the box it was asked for, never bigger.** The bounds-only pass
 *    plus `inSampleSize` means a 160 dp thumbnail (about a 320 px box at density
 *    2.0) decodes that same 4000x3000 file to 250x187 - about 183 KB, not 48 MB.
 *    [shrinkTo] is the net under that: a decoder is allowed to ignore
 *    `inSampleSize`, and a thumbnail request must still not hand back a full-size
 *    bitmap.
 * 3. **At most [CACHE_BYTES] of decoded photos are held**, evicted least-recently-used
 *    first and dropped when a photo is rewritten or deleted. Like [com.alefinot.dashboardpp.trips.TripIndex],
 *    a cache entry is keyed by the file's mtime, so replacing a photo cannot leave a
 *    card showing the old one.
 *
 * **Every public method blocks on I/O.** They are synchronous by contract, not by
 * accident: the caller owns the threading (task 5 calls them from
 * `Dispatchers.IO`), exactly as [TripStore] states of its own file methods. There is
 * no coroutine machinery in here - the [LruCache] itself is internally synchronised,
 * and two threads racing to decode one photo cost one extra decode, never a wrong
 * image.
 *
 * Failure answers `null`, never an exception: a photo is the least important thing
 * on the screen, and the correct response to a photo the app cannot produce is the
 * placeholder the card already knows how to draw. Nothing here recycles a [Bitmap]
 * - pixel data has lived on the Java heap since API 26, so there is nothing left to
 * recycle and a `recycle()` call would only be a way to crash a later frame.
 */
class VehiclePhotoStore(context: Context) {
    private val appContext = context.applicationContext

    /** Decoded photos, [CACHE_BYTES] of them, largest-cost entries evicted first. */
    private val cache = object : LruCache<String, Bitmap>(CACHE_BYTES) {
        override fun sizeOf(key: String, value: Bitmap): Int = value.allocationByteCount
    }

    /**
     * Where the JPEGs live: app-private external storage, removed with the app and
     * visible to a file manager, with the same fallback chain [TripStore.dir] uses
     * for the trip CSVs (a device without the volume mounted falls back to internal
     * storage rather than failing every photo).
     */
    fun dir(): File {
        val base = appContext.getExternalFilesDir(null) ?: appContext.filesDir
        val d = File(base, DIR_NAME)
        if (!d.exists()) d.mkdirs()
        return d
    }

    /**
     * The file a record's `photoFile` names, or `null` when that text cannot name
     * one. Answers `null` for a name that would point outside [dir] - see
     * [photoFileIn] for why a stored string is never trusted as a path.
     *
     * This resolves; it does not check that the file exists. Task 5 needs the path
     * of the photo it is about to replace, and a resolution that also stat'ed would
     * turn a name check into an I/O surprise.
     */
    fun fileFor(photoFile: String?): File? = photoFileIn(dir(), photoFile)

    /**
     * Replace a card's photo with the contents of [source], downscaled, and return
     * the file it landed at - or `null`, leaving the previous photo exactly as it
     * was, if anything in the chain failed.
     *
     * The order of operations is the whole point of this method, so it is written
     * straight down and the failure paths all answer `null` before the last step:
     *
     * The decode is at the stored size ([MAX_STORED_EDGE], via [sampleSizeFor] then
     * [shrinkTo]); the sequence that actually changes a photo (temporary name, rename,
     * cache eviction, letting go of [source]) lives in [commit], which [cropFrom] uses
     * too - the order is what makes a failed write cost the user nothing, and two copies
     * of it would only be able to drift apart.
     */
    fun writeFrom(source: File, vehicleId: String): File? =
        commit(decodeScaled(source, MAX_STORED_EDGE), vehicleId, source)

    /**
     * Cut the square [rect] out of [source] and make it this card's photo, or answer
     * `null` and leave the previous photo exactly as it was.
     *
     * [rect] is in the source file's own full-resolution pixels - [PhotoCrop.rectFor]
     * produces it from what the user did on screen - and the decode reads **only that
     * square**: a region decoder is what lets a card cover come off a 4000x3000 camera
     * shot without this app ever holding the whole picture (the memory rules in the
     * header again). The square's side is the long edge [sampleSizeFor] decides from, so
     * a 3000 px crop for a [MAX_STORED_EDGE] store decodes at factor 4, and [shrinkTo]
     * takes care of what a power-of-two factor leaves over.
     *
     * The fallback matters because a region decoder is not universal: on the API levels
     * this app supports it does a partial read of a JPEG, while a screenshot picked from a
     * photos app is often a PNG. There the whole file is decoded at the stored size and
     * the same square is cut out of *that*, the rectangle first scaled into the smaller
     * space by [PhotoCrop.scaleRectInto]. It costs more memory than the region path and
     * stays bounded by [MAX_STORED_EDGE], which is the trade worth making so a card can be
     * given a screenshot on every device this app ships to, instead of the app refusing
     * half the pictures a user can choose.
     *
     * [source] goes once the crop has landed, as in [writeFrom]: the capture is scratch,
     * and the crop screen has no further use for it.
     */
    fun cropFrom(source: File, vehicleId: String, rect: CropRect): File? {
        val pixels = cropByRegion(source, rect, MAX_STORED_EDGE)
            ?: cropByWholeFile(source, rect, MAX_STORED_EDGE)
        return commit(pixels, vehicleId, source)
    }

    /**
     * Land [pixels] as this card's photo. The order is the whole point of this method, so
     * it is written straight down:
     *
     * 1. write the JPEG to `<id>.jpg.tmp` and close the stream, flush included;
     * 2. only then `renameTo` `<id>.jpg` - atomic within one directory, so a process
     *    killed mid-encode leaves the *previous* photo intact and a stray `.tmp` nobody
     *    reads;
     * 3. drop this card's cache entries, so the card that asked for a new photo does not
     *    keep drawing the old one;
     * 4. delete [source] - removed only once the copy has landed, which is why a failed
     *    write costs the user their photo not at all.
     *
     * A null [pixels] (nothing decoded), a failed encode, or a failed rename - full disk,
     * read-only volume, revoked storage permission - deletes the `.tmp` and answers `null`:
     * the record still points at the photo it already had, which is still a photo. A decode
     * that cannot allocate answers null from BitmapFactory itself; an [OutOfMemoryError]
     * from a scale step is an Error and is not swallowed here, because at that point the
     * app is already out of memory.
     *
     * [source] may be the card's own current photo - re-cropping what is already stored -
     * in which case it is not deleted, because it *is* the answer. Without that check the
     * last step would remove the file the second step just committed.
     */
    private fun commit(pixels: Bitmap?, vehicleId: String, source: File): File? {
        val target = fileForId(vehicleId) ?: return null
        if (pixels == null) return null
        val tmp = File(target.parentFile, target.name + TMP_SUFFIX)
        val sameFile = source.absolutePath == target.absolutePath
        var landed = false
        try {
            if (!writeJpeg(pixels, tmp)) return null
            landed = tmp.renameTo(target)
        } catch (e: Exception) {
            // IOException from a full disk or a read-only volume, SecurityException from a
            // revoked storage permission: a photo is not worth a crash in the capture path,
            // and the previous photo is still on disk.
        } finally {
            if (!landed) tmp.delete()
        }
        if (!landed) return null
        evict(vehicleId)
        if (!sameFile) source.delete()
        return target
    }

    /**
     * The region decode: the square [rect] on its own, sub-sampled so it lands at or below
     * [maxEdge]. `null` for anything the decoder will not do, which is not an error the
     * user sees - the caller's fallback takes it from there.
     */
    private fun cropByRegion(f: File, rect: CropRect, maxEdge: Int): Bitmap? {
        if (!f.isFile || rect.size <= 0) return null
        return try {
            FileInputStream(f).use { input ->
                // The one-argument newInstance is API 31+; the two-argument form is what a
                // minSdk 26 device has, and it is deprecated rather than removed.
                @Suppress("DEPRECATION")
                val dec = BitmapRegionDecoder.newInstance(input, false) ?: return null
                try {
                    val opts = BitmapFactory.Options().apply {
                        // The deliberate opposite of [sampleSizeFor]'s "at or below the box"
                        // rule, because [shrinkTo] finishes this job and a region decode is not
                        // the same bargain as a whole-frame one: landing just **above** maxEdge
                        // costs a little memory for a moment, while landing below it throws away
                        // detail the file actually has and there is no way back except making
                        // the user frame the photo again. Measured on a tablet: a 4000x3000 shot
                        // cropped to its 3000 px square and asked for at 1280 came back **750**
                        // under the other rule - a third of the resolution, and the card cover
                        // went soft at exactly the moment the user made the picture sharp.
                        inSampleSize = sampleForCrop(rect.size, maxEdge)
                    }
                    val box = Rect(rect.x, rect.y, rect.x + rect.size, rect.y + rect.size)
                    shrinkTo(dec.decodeRegion(box, opts) ?: return null, maxEdge)
                } finally {
                    // close() on a region decoder is API 33+; recycle() is what 26 has, and
                    // it frees the native decoder instead of leaving it to the collector.
                    @Suppress("DEPRECATION")
                    dec.recycle()
                }
            }
        } catch (e: Exception) {
            // IllegalArgumentException (a rectangle the file cannot cover), IOException, or
            // a decoder that will not do a partial read of this image format.
            null
        }
    }

    /**
     * Fallback crop: decode the whole file at [maxEdge], then cut the same square out of
     * that smaller bitmap - which is why the rectangle has to be rescaled first, and why
     * [PhotoCrop.scaleRectInto] carries its own tests instead of a multiplication here.
     *
     * It lands coarser than [cropByRegion] whenever the user zoomed in, and that is the
     * bargain rather than an oversight: scaling the whole frame so that a 500 px window of a
     * 4000 px file still fills 1280 px would decode the entire photograph at full size, about
     * 36 MB of pixels for a 12 MP shot. Capping the whole-frame decode at [maxEdge] bounds the
     * spike to a few MB on the one path that only runs when a partial read is impossible -
     * a truncated file, or an image format the region decoder will not step through.
     *
     * Returns the decoded bitmap itself when the crop turns out to be all of it, rather
     * than allocating a second identical one (rule 14).
     */
    private fun cropByWholeFile(f: File, rect: CropRect, maxEdge: Int): Bitmap? {
        val bounds = BitmapFactory.Options().apply { inJustDecodeBounds = true }
        BitmapFactory.decodeFile(f.absolutePath, bounds)
        if (bounds.outWidth <= 0 || bounds.outHeight <= 0) return null // torn, truncated, or not an image
        val whole = decodeScaled(f, maxEdge) ?: return null
        val small = PhotoCrop.scaleRectInto(rect, bounds.outWidth, bounds.outHeight, whole.width, whole.height)
            ?: return null
        if (whole.width == whole.height && small.size == whole.width && small.x == 0 && small.y == 0) {
            return whole
        }
        return try {
            Bitmap.createBitmap(whole, small.x, small.y, small.size, small.size)
        } catch (e: Exception) {
            null // defensive: scaleRectInto already keeps the rectangle inside the bitmap
        }
    }

    /**
     * The scratch file a camera capture or a picked image is written into:
     * `<cacheDir>/captures/<id>.jpg`, or `null` for an id that could not name a file.
     *
     * Scratch, never the photo. The camera contract needs a `content://` URI another app
     * can open for writing, so this file has to sit under a root the app's FileProvider
     * publishes (`captures/` in res/xml/files_path.xml), while the JPEG a card keeps lives
     * in app-private external storage, which is never handed to another app at all. The
     * capture is cropped into the vehicles dir by [cropFrom] and disappears there; nothing
     * in this class treats a capture as a photo, and the decoded-photo cache never sees one.
     *
     * One capture per card at a time is deliberate: the editor has one photo well, and two
     * crops in flight for the same card would need a naming scheme nobody asked for.
     */
    fun captureFile(vehicleId: String): File? {
        val name = nameOf(vehicleId)
        if (!isPhotoName(name)) return null
        val d = File(appContext.cacheDir, CAPTURE_DIR)
        if (!d.isDirectory && !d.mkdirs()) return null
        return File(d, name)
    }

    /**
     * A camera capture in one piece: the scratch file, and the `content://` URI the camera
     * intent is handed for it. Or `null` when there is no capture to offer, which the editor
     * reports as the camera not being available rather than launching an intent that would
     * take a photograph nobody can find.
     *
     * The two come back together because they belong together: the crop screen needs the
     * *file* (it decodes from it), the intent needs the *URI*, and a caller that asked for
     * them in two separate calls would be able to get a URI for one capture and a file from
     * the next - which is a photo written where nothing is looking.
     */
    fun captureFor(vehicleId: String): Capture? {
        val f = captureFile(vehicleId) ?: return null
        val uri = try {
            FileProvider.getUriForFile(appContext, appContext.packageName + '.' + PROVIDER_SUFFIX, f)
        } catch (e: IllegalArgumentException) {
            // The file is outside every root files_path.xml declares - a packaging mistake
            // rather than a runtime one, and still not worth crashing the editor over.
            return null
        }
        return Capture(f, uri)
    }

    /** A scratch capture and the URI that stands for it to another app. */
    data class Capture(val file: File, val uri: Uri)

    /**
     * Copy what a photo picker chose into the card's capture file, and answer the file, or
     * `null` when the copy could not be made.
     *
     * The picker hands back a `content://` URI owned by somebody else - the system media
     * provider, a gallery, a cloud photos app. Its stream may not be seekable, which a
     * region decoder wants; the permission grant can lapse while the crop screen is open;
     * and the source can stop existing when the app that offered it goes away. Copying it
     * once into a file this app owns makes every later step - the crop, the decode, a retry
     * after the tablet rotates - work on something that cannot move under it.
     *
     * A stream that dies partway leaves a truncated file, and a truncated file decodes to
     * something *wrong* rather than to nothing, so the partial copy is deleted here and the
     * answer is `null`. A real photo is far smaller than what the cache dir holds, so the
     * copy needs no quota of its own; [cropFrom] removes the capture once it has landed,
     * and [clearCapture] covers the user deciding against the photo.
     */
    fun stageFrom(source: Uri, vehicleId: String): File? {
        val target = captureFile(vehicleId) ?: return null
        return try {
            val input = appContext.contentResolver.openInputStream(source) ?: return null
            input.use { ins -> FileOutputStream(target).use { out -> ins.copyTo(out, COPY_BUFFER) } }
            if (target.isFile && target.length() > 0) target else { target.delete(); null }
        } catch (e: Exception) {
            target.delete()
            null
        }
    }

    /**
     * Drop a card's scratch capture. The crop screen calls this when the user cancels, so a
     * photo they decided against does not sit in the cache until Android clears it.
     */
    fun clearCapture(vehicleId: String) {
        captureFile(vehicleId)?.delete()
    }

    /**
     * The card's photo, decoded no larger than a [maxEdge] box, or `null` when there
     * is no usable photo (missing file, torn file, unreadable volume).
     *
     * [vehicleId] rather than a path: `writeFrom` names the file `<id>.jpg`, so the
     * id the card already has is enough to find it, and it keeps every read inside
     * the store's own directory.
     *
     * Served from the cache when the same file is asked for again at the same size;
     * a file that has gone is evicted on the way out, so a photo deleted by a file
     * manager or by [VehicleStore.delete] stops showing immediately.
     */
    fun read(vehicleId: String, maxEdge: Int): Bitmap? {
        if (maxEdge <= 0) return null // an invisible box must not decode the full file
        val f = fileForId(vehicleId) ?: return null
        if (!f.isFile) {
            evict(vehicleId)
            return null
        }
        val key = cacheKey(f.name, f.lastModified(), maxEdge)
        cache.get(key)?.let { return it }
        val bmp = decodeScaled(f, maxEdge) ?: return null
        cache.put(key, bmp)
        return bmp
    }

    /**
     * Remove a card's decoded photos from the cache without touching the file - for
     * a caller that changed what a photo means (a crop, a replace) and must not keep
     * serving the old bytes.
     */
    fun evict(vehicleId: String) {
        val prefix = nameOf(vehicleId) + ":"
        for (key in cache.snapshot().keys) {
            if (key.startsWith(prefix)) cache.remove(key)
        }
    }

    /**
     * Delete a card's photo: the file and every cache entry for it. Idempotent - a
     * card with no photo is not an error, and a card whose row is being deleted
     * ([VehicleStore.delete]) may not have one at all.
     *
     * The record's `photoFile` is left alone on purpose: the pointer is a name, not
     * a claim that the bytes exist, and [read] answering `null` is what makes the
     * card draw its placeholder. An action that removes a photo *and keeps the card*
     * (task 5) persists `photoFile = null` in the record as well, so the editor does
     * not offer to open a photo that is gone.
     */
    fun delete(vehicleId: String) {
        fileForId(vehicleId)?.delete()
        evict(vehicleId)
    }

    /** `<id>.jpg` resolved through the same guard as a stored `photoFile`. */
    private fun fileForId(vehicleId: String): File? = photoFileIn(dir(), nameOf(vehicleId))

    /**
     * Bounds-only pass, then the real decode at the factor [sampleSizeFor] picks -
     * the least power-of-two sub-sampling that still lands inside [maxEdge] - then
     * [shrinkTo] for the remainder.
     *
     * Two decodes rather than one: the second needs the dimensions, and the only way
     * to know them before allocating the pixels is to read the header alone.
     */
    private fun decodeScaled(f: File, maxEdge: Int): Bitmap? {
        val bounds = BitmapFactory.Options().apply { inJustDecodeBounds = true }
        BitmapFactory.decodeFile(f.absolutePath, bounds)
        if (bounds.outWidth <= 0 || bounds.outHeight <= 0) return null // torn, truncated, or not an image

        val options = BitmapFactory.Options().apply {
            inSampleSize = sampleSizeFor(bounds.outWidth, bounds.outHeight, maxEdge)
        }
        val decoded = BitmapFactory.decodeFile(f.absolutePath, options) ?: return null
        return shrinkTo(decoded, maxEdge)
    }

    /**
     * Final exact scale to the box: `inSampleSize` is a power of two, so it lands on
     * or below the target but never exactly on it - and a decoder may ignore it
     * altogether. Never scales up: an image smaller than the box is shown as it is.
     */
    private fun shrinkTo(bmp: Bitmap, maxEdge: Int): Bitmap {
        val edge = maxOf(bmp.width, bmp.height)
        if (edge <= maxEdge) return bmp
        val scale = maxEdge.toFloat() / edge
        val w = max(1, (bmp.width * scale).roundToInt())
        val h = max(1, (bmp.height * scale).roundToInt())
        return Bitmap.createScaledBitmap(bmp, w, h, true)
    }

    /**
     * Encode to [tmp] and close the stream. The close is not decoration: the rename
     * in [writeFrom] is what makes a photo "exist", so every byte must be on its way
     * to disk before the file gets the name a card will read.
     */
    private fun writeJpeg(bmp: Bitmap, tmp: File): Boolean {
        FileOutputStream(tmp).use { out ->
            if (!bmp.compress(Bitmap.CompressFormat.JPEG, JPEG_QUALITY, out)) return false
            out.flush()
        }
        return true
    }

    private fun nameOf(vehicleId: String) = "$vehicleId$PHOTO_SUFFIX"

    companion object {
        /** Longest edge kept on disk - plan §3 (a card's preview, not a print). */
        const val MAX_STORED_EDGE = 1280

        /** JPEG quality of the stored copy: 85 is visually indistinguishable here. */
        const val JPEG_QUALITY = 85

        private const val DIR_NAME = "vehicles"
        private const val PHOTO_SUFFIX = ".jpg"
        private const val TMP_SUFFIX = ".tmp"

        /** Scratch for camera captures and picked images, under the app's cache dir. */
        private const val CAPTURE_DIR = "captures"

        /** Authority suffix: the FileProvider in AndroidManifest.xml, shared with
         *  [com.alefinot.dashboardpp.trips.TripExport], spells the same name. */
        private const val PROVIDER_SUFFIX = "fileprovider"

        /** Copy buffer for [stageFrom]: one 16 KiB buffer per staged image (rule 14). */
        private const val COPY_BUFFER = 16 * 1024

        /** The decoded-photo budget for one store, in bytes (rule 14). */
        private const val CACHE_BYTES = 8 * 1024 * 1024

        /**
         * The sub-sampling factor for a [width]x[height] source that must decode to no
         * more than a [maxEdge] box: the smallest power of two bringing the longest edge
         * to or below the box - so half that factor would leave it above the box - and
         * `1` when the source is already small enough or the request is unusable.
         *
         * Both halves of that rule cost something, in opposite directions:
         * - too small a factor (2 where 4 was available) decodes 2000x1500 for a 1280
         *   request - 12 MB of pixels the caller will never show;
         * - too large a factor (8 where 4 was enough) decodes 500x375 and the card is
         *   soft, with no way back except re-reading the file.
         *
         * `4000x3000 @ 1280 -> 4`: 4000/2 = 2000 is still outside the box, 4000/4 = 1000
         * is inside it. The **longest** edge decides, so a portrait 3000x4000 gets the
         * same answer and a 2000x4000 gets 4 where a width-only rule would say 2.
         *
         * A 0x0, negative or zero-box request has no sensible factor and answers `1`,
         * leaving the caller's own guards in charge (`read` refuses a non-positive box,
         * `decodeScaled` refuses dimensions it cannot read) - the pure function's job is
         * to be total, not to guess on the caller's behalf.
         *
         * The doubling cannot run away: the loop stops as soon as `longest / s <=
         * maxEdge`, and with `maxEdge` at least 1 that happens by `s = 2^30` for any
         * positive [Int] dimension, so the factor stays a positive [Int].
         */
        fun sampleSizeFor(width: Int, height: Int, maxEdge: Int): Int {
            if (width <= 0 || height <= 0 || maxEdge <= 0) return 1
            val longest = if (width > height) width else height
            var sample = 1
            while (longest / sample > maxEdge) sample *= 2
            return sample
        }

        /**
         * Sub-sampling factor for a **crop** whose result is then resized exactly: the largest
         * power of two that still leaves [size] at or above [maxEdge] after decoding, or `1`
         * when the crop is already that small or the request is unusable.
         *
         * The mirror of [sampleSizeFor], and the reason both exist is that the two callers
         * spend different money. A whole-frame read has nobody after it, so it must land inside
         * the box: over the box and the caller is holding 2000x1500 pixels it will never draw.
         * A crop is followed by [shrinkTo], which resizes whatever came out to the edge it was
         * asked for - so landing under the box buys nothing at all, it only spends resolution
         * the file had. `3000 @ 1280 -> 2` (decodes 1500, shrinks to 1280), never 4 (decodes
         * 750 and the card is soft forever).
         *
         * `size == maxEdge` answers `1`: halving would put the decode under the edge, and a
         * zero or negative request has no factor to choose, so the caller's own guards stay in
         * charge. The doubling stops as soon as `size / (s*2) < maxEdge`, so it cannot run past
         * the range of a positive [Int].
         */
        fun sampleForCrop(size: Int, maxEdge: Int): Int {
            if (size <= 0 || maxEdge <= 0) return 1
            var sample = 1
            while (size / (sample * 2) >= maxEdge) sample *= 2
            return sample
        }

        /**
         * Pure half of [fileFor]: [photoFile] inside [dir] as a file name, or `null`
         * when it cannot be one.
         *
         * The guard is the reason this is a function and not `File(dir, photoFile)`.
         * `photoFile` is text read back out of a preferences row, so a torn row, a
         * hand-edited file, or a record from a future build can put anything in it.
         * Handed straight to `File`, that text decides where [delete] writes - and a
         * `../` would take it outside the vehicles dir, up to the app's own files and
         * beyond. Only a bare file name is accepted: no separator (either spelling,
         * so the check means the same thing off-device), no `..`, no control
         * character (a NUL inside a path string is truncated at the native layer), and
         * not a name that denotes the directory itself - `""` and `"."` both resolve to
         * [dir], which would put a directory in front of a file delete.
         *
         * `internal` rather than private so the guard is testable without a
         * [Context] - it is the half that a wrong answer makes a data-loss bug.
         */
        internal fun photoFileIn(dir: File, photoFile: String?): File? {
            val name = photoFile ?: return null
            if (!isPhotoName(name)) return null
            return File(dir, name)
        }

        private fun isPhotoName(name: String): Boolean =
            name.isNotEmpty() &&
                name != "." &&
                !name.contains('/') &&
                !name.contains('\\') &&
                !name.contains("..") &&
                !name.any { it.isISOControl() }

        /** Cache key: a photo plus what it was decoded for. A new file, or a new box, is a new entry. */
        private fun cacheKey(name: String, mtime: Long, maxEdge: Int): String =
            "$name:$mtime:$maxEdge"
    }
}
