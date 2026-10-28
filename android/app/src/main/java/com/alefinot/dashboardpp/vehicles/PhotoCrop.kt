package com.alefinot.dashboardpp.vehicles

/**
 * A square region expressed in the source image's own pixels - what
 * [VehiclePhotoStore.cropFrom] hands to a region decoder.
 */
data class CropRect(val x: Int, val y: Int, val size: Int)

/**
 * The maths behind the photo crop screen (vehicle page plan §5).
 *
 * The user sees a square box with the photo in it, and drags and pinches it. What the store
 * needs is a rectangle inside the JPEG file, because a region decoder can be handed that
 * rectangle and read *only* the part asked for - which is the difference between a 4000x3000
 * camera shot costing a few hundred kilobytes of RAM and costing forty-eight megabytes (see
 * [VehiclePhotoStore]'s header). So the mapping from "box, zoom, drag" to "pixels of the
 * original" lives here, where it can be tested without a screen, a camera or a phone.
 *
 * **Sign convention, stated once because it is the thing that silently inverts:** the offsets
 * move the *photo* as the user drags it, not the window. A positive X pushes the picture to
 * the right inside the box, which moves the crop window to the *left* in the source. Getting
 * this backwards produces a crop that runs opposite to the finger - a defect that is obvious
 * in the hand and invisible in a screenshot.
 *
 * The crop is square because a garage card's cover is square, and the fit at zoom 1 is a
 * *cover* fit (the short edge fills the box), so at the starting position the largest square
 * the photo can give is taken from its centre and nothing outside the photo is ever asked
 * for.
 */
object PhotoCrop {

    /**
     * The source rectangle for a [boxPx]-square viewport showing an image of [imgW]x[imgH]
     * at [zoom] (>= 1; smaller values are treated as 1) with the image displaced by
     * ([offsetX], [offsetY]) view pixels.
     *
     * `null` for measurements that describe no image at all - a zero or negative side, a
     * NaN or infinite gesture value. A Compose transform can produce those on the frame a
     * pointer goes up (a scale of exactly 0 has been seen on other projects' pinch
     * gestures), and the correct answer there is "no crop", not "crop the garbage".
     */
    fun rectFor(
        imgW: Int,
        imgH: Int,
        boxPx: Int,
        zoom: Float,
        offsetX: Float,
        offsetY: Float
    ): CropRect? {
        if (imgW <= 0 || imgH <= 0 || boxPx <= 0) return null
        if (!zoom.isFinite() || !offsetX.isFinite() || !offsetY.isFinite()) return null

        val z = if (zoom < 1f) 1f else zoom
        val dImgW = imgW.toDouble()
        val dImgH = imgH.toDouble()
        // Cover fit, then the user's zoom: how many source pixels fit in one box pixel.
        val scale = (boxPx.toDouble() / minOf(dImgW, dImgH)) * z
        // The window, in source pixels, seen through the box.
        val sideD = boxPx / scale
        // The image sits centred in the box and is then displaced by the drag, so the
        // window's centre moves the other way in source space.
        val cx = dImgW / 2.0 - offsetX / scale
        val cy = dImgH / 2.0 - offsetY / scale

        // Whole pixels, and never wider than the image: `sideD` is at most the short edge by
        // construction, but rounding up could push it past it.
        val size = sideD.toInt().coerceAtLeast(1).coerceAtMost(minOf(imgW, imgH))
        val x = (cx - sideD / 2.0).toInt().coerceIn(0, imgW - size)
        val y = (cy - sideD / 2.0).toInt().coerceIn(0, imgH - size)
        return CropRect(x, y, size)
    }

    /**
     * How far the photo may be dragged on each axis before the box would show area outside
     * it. The screen clamps the gesture to this; without the clamp the finger keeps moving
     * and the crop quietly stops, which reads as the app ignoring the user.
     *
     * Answers (0, 0) when the image exactly fills the box on an axis, which is the case at
     * zoom 1 on the image's short axis.
     */
    fun maxOffsetPx(imgW: Int, imgH: Int, boxPx: Int, zoom: Float): Pair<Float, Float> {
        if (imgW <= 0 || imgH <= 0 || boxPx <= 0) return 0f to 0f
        val z = if (!zoom.isFinite() || zoom < 1f) 1f else zoom
        val scale = (boxPx.toDouble() / minOf(imgW.toDouble(), imgH.toDouble())) * z
        val slackX = (imgW * scale - boxPx) / 2.0
        val slackY = (imgH * scale - boxPx) / 2.0
        return slackX.coerceAtLeast(0.0).toFloat() to slackY.coerceAtLeast(0.0).toFloat()
    }
    /**
     * The same rectangle, in the pixels of a *sub-sampled* decode of that image: [fullW] x
     * [fullH] is the file's real size (read from its header, no pixels allocated), [smallW]
     * x [smallH] is the size it was decoded to.
     *
     * [VehiclePhotoStore.cropFrom] needs this for the one case the region decoder cannot
     * serve - an image format it will not do a partial read of, where the fallback decodes
     * the whole picture at the stored size and must then cut the *same* square out of a
     * smaller bitmap. Scaling the rectangle by hand and getting it wrong by a factor of four
     * crops a quarter of a windscreen and calls it a photo, so it is a pure function with
     * its own tests.
     *
     * The answer stays square (the smaller of the two axis ratios decides, so the crop can
     * only ever be slightly tighter than asked) and lies inside the decoded copy.
     */
    fun scaleRectInto(rect: CropRect, fullW: Int, fullH: Int, smallW: Int, smallH: Int): CropRect? {
        if (rect.size <= 0 || fullW <= 0 || fullH <= 0 || smallW <= 0 || smallH <= 0) return null
        val sx = smallW.toDouble() / fullW
        val sy = smallH.toDouble() / fullH
        val size = (rect.size * minOf(sx, sy)).toInt()
            .coerceAtLeast(1)
            .coerceAtMost(minOf(smallW, smallH))
        val x = (rect.x * sx).toInt().coerceIn(0, smallW - size)
        val y = (rect.y * sy).toInt().coerceIn(0, smallH - size)
        return CropRect(x, y, size)
    }
}
