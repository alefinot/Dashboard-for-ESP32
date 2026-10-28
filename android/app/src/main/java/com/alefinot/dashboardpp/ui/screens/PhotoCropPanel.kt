package com.alefinot.dashboardpp.ui.screens

import androidx.activity.compose.BackHandler
import androidx.compose.foundation.Image
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.gestures.detectTransformGestures
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.BoxWithConstraints
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.widthIn
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableFloatStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.asImageBitmap
import androidx.compose.ui.graphics.graphicsLayer
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.layout.ContentScale
import androidx.compose.ui.layout.onSizeChanged
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.ui.unit.IntSize
import androidx.compose.ui.unit.dp
import com.alefinot.dashboardpp.ui.components.HudButton
import com.alefinot.dashboardpp.ui.components.HudButtonStyle
import com.alefinot.dashboardpp.ui.components.HudEnter
import com.alefinot.dashboardpp.ui.theme.HudColors
import com.alefinot.dashboardpp.ui.theme.HudMotion
import com.alefinot.dashboardpp.ui.theme.HudRadius
import com.alefinot.dashboardpp.ui.theme.HudeCfgHelp
import com.alefinot.dashboardpp.ui.theme.HudeSectionTitle
import com.alefinot.dashboardpp.ui.theme.hudGlass
import com.alefinot.dashboardpp.vehicles.CropRect
import com.alefinot.dashboardpp.vehicles.PhotoCrop
import com.alefinot.dashboardpp.vehicles.VehiclePhotoStore
import java.io.File

/**
 * The step between "a picture arrived" and "this card has a photo" (vehicle page plan §5).
 *
 * It exists because of an aspect problem, not a decoration problem. A camera gives a
 * landscape 4:3 frame; a card's cover wants a square. Somebody has to decide which square,
 * and it should be the person who took the picture - an automatic centre crop cuts the number
 * plate off one car and the handlebars off another. So the user drags and pinches, and the
 * square they framed is what the store is asked for, in the original file's pixels
 * ([PhotoCrop.rectFor]) - which is how a crop off a 4000x3000 shot is decoded without this
 * app ever holding the whole photograph.
 *
 * **What is shown while framing is a preview, and only a preview.** It is decoded once at up
 * to [PREVIEW_EDGE] on the longest edge - about 3.7 MB of pixels, which is the biggest
 * transient this screen is allowed - and it is drawn at the aspect the full file has, so what
 * the user frames matches what they get while the pixels under a finger stay cheap. The crop
 * itself is decoded a second time, from the untouched capture file, at the size actually
 * kept. Anyone tempted to reuse the preview bitmap for the crop should not: it is already
 * sub-sampled once, so the card would come out soft at exactly the moment the user made the
 * picture sharp.
 *
 * **The square is sized by the leftover height, not by the panel's width.** That is the one
 * layout fact on this screen worth a paragraph, because getting it wrong is invisible on a
 * wide tablet and broken on a phone: a 1:1 box as wide as a 620 dp panel is 620 dp tall,
 * which on a 640 dp-tall phone leaves the panel's own title off the top of the screen. So the
 * words and the buttons take what they need, the middle gets the rest (`weight(1f)`), and the
 * square is the shorter side of what is left.
 *
 * A tap on the scrim does nothing, and Back cancels: the plan is explicit that CANCEL keeps
 * the previous photo, and the previous photo is whatever is already on disk. This panel writes
 * nothing until USE THIS PHOTO is pressed.
 */
@Composable
fun PhotoCropPanel(
    source: File,
    onUse: (CropRect) -> Unit,
    onCancel: () -> Unit,
) {
    // The measured side of the framed square, in pixels. Every number [PhotoCrop] works with
    // comes from here or from the file's header, which is why nothing on this screen guesses
    // at a size.
    var frame by remember { mutableStateOf(IntSize.Zero) }
    var full by remember(source) { mutableStateOf<Pair<Int, Int>?>(null) }
    var preview by remember(source) { mutableStateOf<android.graphics.Bitmap?>(null) }
    var unreadable by remember(source) { mutableStateOf(false) }
    var zoom by remember(source) { mutableFloatStateOf(1f) }
    var offX by remember(source) { mutableFloatStateOf(0f) }
    var offY by remember(source) { mutableFloatStateOf(0f) }

    BackHandler(enabled = true) { onCancel() }

    // Dimensions first, on their own: they come from the file's header without allocating a
    // single pixel, so the panel can say "this picture cannot be read" - and size the preview
    // decode - before anything expensive happens.
    LaunchedEffect(source) {
        val bounds = readBounds(source)
        if (bounds == null) unreadable = true else full = bounds
    }

    // The preview follows the frame, because the frame is what says how many pixels are worth
    // decoding for it. It is measured once per open, so this decodes at most twice: once with
    // nothing known, once with the real box (rule 14 - no decode is asked for blind).
    LaunchedEffect(source, frame.width) {
        val img = full ?: return@LaunchedEffect
        if (frame.width <= 0 || preview != null) return@LaunchedEffect
        preview = decodePreview(source, img.first, img.second, frame.width)
    }

    Box(
        Modifier
            .fillMaxSize()
            .background(HudColors.Scrim)
            .clickable(onClick = {}),
        contentAlignment = Alignment.Center,
    ) {
        HudEnter(
            Modifier
                .fillMaxSize()
                .widthIn(max = 620.dp)
                .padding(horizontal = 20.dp, vertical = 24.dp),
            durationMs = HudMotion.SHORT_MS,
            riseDp = 8,
            scaleFrom = 0.98f,
        ) {
            Column(Modifier.hudGlass().fillMaxSize().padding(20.dp)) {
                Text("FRAME THE PHOTO", color = HudColors.Cyan, style = HudeSectionTitle())
                Spacer(Modifier.height(10.dp))
                Text(
                    if (unreadable) {
                        "That picture could not be read. The card keeps the photo it had."
                    } else {
                        "Drag the photo to move it, spread to zoom in. The square is what the " +
                            "card will show; the picture is cropped to it, not stretched."
                    },
                    color = HudColors.TextMuted,
                    style = HudeCfgHelp(),
                )
                Spacer(Modifier.height(14.dp))

                // The leftover height, and the square inside it.
                Box(
                    Modifier.weight(1f).fillMaxWidth(),
                    contentAlignment = Alignment.Center,
                ) {
                    BoxWithConstraints(Modifier.fillMaxSize()) {
                        val side = minOf(maxWidth, maxHeight)
                        Box(
                            Modifier
                                .size(side)
                                .clip(HudRadius.CardShape)
                                .background(HudColors.Panel)
                                .border(1.dp, HudColors.Hairline, HudRadius.CardShape)
                                .onSizeChanged { frame = it }
                                .pointerInput(source, full, frame.width) {
                                    if (frame.width <= 0) return@pointerInput
                                    detectTransformGestures { _, pan, zoomFactor, _ ->
                                        val img = full ?: return@detectTransformGestures
                                        val next = (zoom * zoomFactor).coerceIn(1f, MAX_ZOOM)
                                        zoom = next
                                        // Clamped to the amount of picture that actually hangs
                                        // outside the frame. Past that the finger keeps moving
                                        // while the crop stops, which reads as the app ignoring
                                        // the gesture rather than as the photo running out.
                                        val (maxX, maxY) = PhotoCrop.maxOffsetPx(
                                            img.first, img.second, frame.width, next,
                                        )
                                        offX = (offX + pan.x).coerceIn(-maxX, maxX)
                                        offY = (offY + pan.y).coerceIn(-maxY, maxY)
                                    }
                                },
                            contentAlignment = Alignment.Center,
                        ) {
                            preview?.let { bmp ->
                                val img = full ?: return@let
                                val density = LocalDensity.current
                                // Cover fit at zoom 1, then the user's zoom. The displayed box
                                // keeps the file's aspect by construction, which is what makes
                                // FillBounds safe here instead of a distortion machine.
                                val cover = frame.width.toFloat() / minOf(img.first, img.second)
                                val w = (img.first * cover * zoom).toInt().coerceAtLeast(1)
                                val h = (img.second * cover * zoom).toInt().coerceAtLeast(1)
                                with(density) {
                                    Image(
                                        bitmap = bmp.asImageBitmap(),
                                        contentDescription = "the photo being framed for this card",
                                        contentScale = ContentScale.FillBounds,
                                        modifier = Modifier
                                            .size(w.toDp(), h.toDp())
                                            .graphicsLayer {
                                                translationX = offX
                                                translationY = offY
                                            },
                                    )
                                }
                            }
                            if (preview == null && !unreadable) {
                                Text("reading the photo…", color = HudColors.TextDim, style = HudeCfgHelp())
                            }
                        }
                    }
                }

                Spacer(Modifier.height(16.dp))
                Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(10.dp)) {
                    Box(Modifier.weight(1f).heightIn(min = 48.dp)) {
                        HudButton(
                            "USE THIS PHOTO",
                            style = HudButtonStyle.PRIMARY,
                            enabled = !unreadable,
                        ) {
                            val img = full ?: return@HudButton
                            val rect = PhotoCrop.rectFor(
                                img.first, img.second, frame.width, zoom, offX, offY,
                            )
                            if (rect != null) onUse(rect)
                        }
                    }
                    Box(Modifier.weight(1f).heightIn(min = 48.dp)) {
                        HudButton("CANCEL", style = HudButtonStyle.SECONDARY) { onCancel() }
                    }
                }
                Spacer(Modifier.height(10.dp))
                Text(
                    "Cancelling leaves the photo the card already has.",
                    color = HudColors.TextDim,
                    style = HudeCfgHelp(),
                )
            }
        }
    }
}

/**
 * Header-only read of the file: dimensions without pixels. `null` when the file is torn,
 * truncated, or not an image at all - which the panel reports instead of showing an empty box.
 */
private suspend fun readBounds(f: File): Pair<Int, Int>? =
    kotlinx.coroutines.withContext(kotlinx.coroutines.Dispatchers.IO) {
        if (!f.isFile) return@withContext null
        val o = android.graphics.BitmapFactory.Options().apply { inJustDecodeBounds = true }
        android.graphics.BitmapFactory.decodeFile(f.absolutePath, o)
        if (o.outWidth <= 0 || o.outHeight <= 0) null else o.outWidth to o.outHeight
    }

/**
 * The framing preview: as large as the frame it is shown in (twice over, for a finger that
 * will zoom later), never larger than [PREVIEW_EDGE], and sub-sampled by the store's own rule
 * so the decoder is never asked to allocate a frame the screen cannot use.
 *
 * Twice the frame rather than once because a pinch that starts before the pixels arrive would
 * otherwise be stretching a bitmap smaller than the box, and softness during the gesture reads
 * as a broken crop.
 */
private suspend fun decodePreview(
    f: File,
    imgW: Int,
    imgH: Int,
    framePx: Int,
): android.graphics.Bitmap? = kotlinx.coroutines.withContext(kotlinx.coroutines.Dispatchers.IO) {
    val edge = (framePx * 2).coerceAtMost(PREVIEW_EDGE).coerceAtLeast(256)
    val o = android.graphics.BitmapFactory.Options().apply {
        inSampleSize = VehiclePhotoStore.sampleSizeFor(imgW, imgH, edge)
    }
    android.graphics.BitmapFactory.decodeFile(f.absolutePath, o)
}

/** Longest edge of the framing preview: about 3.7 MB, the transient this screen is allowed. */
private const val PREVIEW_EDGE = 1280

/** Pinch ceiling. Past this the preview is a blur and no crop is worth having. */
private const val MAX_ZOOM = 8f
