package com.alefinot.dashboardpp.ui.screens

import androidx.activity.compose.BackHandler
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.PickVisualMediaRequest
import androidx.activity.result.contract.ActivityResultContracts
import android.graphics.Bitmap
import androidx.compose.foundation.Image
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.widthIn
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.foundation.layout.size
import androidx.compose.material3.Icon
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.asImageBitmap
import androidx.compose.ui.layout.ContentScale
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.ui.text.input.ImeAction
import androidx.compose.ui.text.input.KeyboardType
import androidx.compose.ui.unit.dp
import com.alefinot.dashboardpp.ui.components.HudButton
import com.alefinot.dashboardpp.ui.components.HudButtonStyle
import com.alefinot.dashboardpp.ui.components.HudConfirmDialog
import com.alefinot.dashboardpp.ui.components.HudEnter
import com.alefinot.dashboardpp.ui.components.HudIcons
import com.alefinot.dashboardpp.ui.components.HudInput
import com.alefinot.dashboardpp.ui.components.HudInputStyle
import com.alefinot.dashboardpp.ui.theme.HudColors
import com.alefinot.dashboardpp.ui.theme.HudMotion
import com.alefinot.dashboardpp.ui.theme.HudRadius
import com.alefinot.dashboardpp.viewmodel.ConnectionViewModel
import com.alefinot.dashboardpp.ui.theme.HudeCfgHelp
import com.alefinot.dashboardpp.ui.theme.HudeCfgLive
import com.alefinot.dashboardpp.ui.theme.HudeFormLabel
import com.alefinot.dashboardpp.ui.theme.HudeSectionTitle
import com.alefinot.dashboardpp.ui.theme.hudGlass
import com.alefinot.dashboardpp.vehicles.Vehicle
import com.alefinot.dashboardpp.vehicles.VehiclePhotoStore
import com.alefinot.dashboardpp.vehicles.VehicleType
import java.io.File

/**
 * One card's details, editable — a panel over the garage, not a route.
 *
 * It is a panel for the same reason [ManualEntryPanel] is one, and for the same reason the
 * page's delete confirmation is a box inside the card rather than a window: the page behind
 * is the point, and closing this returns the user to their garage with the bar, the pager
 * and their place in it exactly as they left it. A route could only ever drop them back on
 * some other page.
 *
 * The form edits **the user's half** of the record — name, type, plate, maker, model, year,
 * notes. What the app measured is shown and not editable, because it is not the user's to
 * type: the eFuse identity is what binds the card to the physical dashboard behind them, and
 * the remembered addresses are what a return trip is dialled from. Showing them is the
 * honest part: a card that has no identity says so, and that explains why it cannot be
 * recognised automatically.
 *
 * The name field's placeholder is not a decorative example. Leaving the name blank is
 * allowed on purpose (the save fills in the identity tail rather than losing the edit on the
 * back gesture), so the placeholder tells the user what the card will be called if they do —
 * [NAME IT] on an unclaimed unit lands here with the name empty, where that matters most.
 *
 * Deleting asks first, through the app's own confirm dialog, because the consequence is not
 * the card: the drives this vehicle took outlast it by design.
 *
 * The photo well at the top of the form is the third place a picture is asked for - after the
 * garage and before anything else a new card is worth. Both routes end in the same crop step
 * ([PhotoCropPanel]) rather than writing the picture straight in, because the card's cover is
 * a square and a photograph is not, and an automatic crop cuts the plate off one car and the
 * handlebars off another. Nothing lands on disk until USE THIS PHOTO: a gallery pick and a
 * camera shot both sit in a scratch capture the editor throws away when the user backs out of
 * the crop, so closing the editor mid-way cannot leave half a photo on a card.
 */
@Composable
fun VehicleEditorScreen(
    vm: ConnectionViewModel,
    draft: Vehicle,
    isNew: Boolean,
    refusal: String?,
    onSave: (Vehicle) -> Unit,
    onDismiss: () -> Unit,
    onDelete: () -> Unit,
) {
    // Cancelling the confirmation is a separate Back step from closing the form: the first
    // press should un-arm the destructive one, not throw away the edits beside it.
    var confirming by remember(draft.id) { mutableStateOf(false) }
    BackHandler(enabled = confirming) { confirming = false }
    BackHandler(enabled = !confirming) { onDismiss() }

    var name by remember(draft.id) { mutableStateOf(draft.name) }
    var type by remember(draft.id) { mutableStateOf(draft.type) }
    var plate by remember(draft.id) { mutableStateOf(draft.plate) }
    var maker by remember(draft.id) { mutableStateOf(draft.maker) }
    var model by remember(draft.id) { mutableStateOf(draft.model) }
    var year by remember(draft.id) { mutableStateOf(draft.year) }
    var notes by remember(draft.id) { mutableStateOf(draft.notes) }

    // Photo state, all of it keyed to this card. [cropFrom] is a staged scratch capture the
    // user is framing; [pendingCapture] is the file handed to the camera, kept between the
    // intent and its result because the result callback gets a boolean, not the file.
    // [hasPhoto] and [well] describe what the card already has - the well shows the *stored*
    // picture, and follows the VM's photo version, so a crop that lands repaints it without
    // this screen holding onto the bitmap.
    var cropFrom by remember(draft.id) { mutableStateOf<File?>(null) }
    var pendingCapture by remember(draft.id) { mutableStateOf<VehiclePhotoStore.Capture?>(null) }
    var hasPhoto by remember(draft.id) { mutableStateOf(draft.photoFile != null) }
    var well by remember(draft.id) { mutableStateOf<Bitmap?>(null) }
    var photoNote by remember(draft.id) { mutableStateOf<String?>(null) }

    val wellEdgePx = with(LocalDensity.current) { WELL_EDGE.dp.toPx().toInt() }
    LaunchedEffect(draft.id, vm.photoVersion.intValue) {
        // Asked for at twice the box: the well is drawn from a bitmap that is at least as
        // large as it is, and the store caps the decode at the edge the photo was kept at
        // anyway (rule 14 - a page of cards asks for card-sized pixels, and so does this).
        vm.photo(draft.id, wellEdgePx * 2) { well = it }
    }

    val pickFromGallery = rememberLauncherForActivityResult(
        ActivityResultContracts.PickVisualMedia(),
    ) { uri ->
        if (uri != null) {
            photoNote = null
            vm.stagePickedPhoto(uri, draft.id) { staged ->
                if (staged == null) {
                    photoNote = "That picture could not be read. The card keeps the photo it had."
                } else {
                    cropFrom = staged
                }
            }
        }
    }

    val shootPhoto = rememberLauncherForActivityResult(
        ActivityResultContracts.TakePicture(),
    ) { taken ->
        val capture = pendingCapture
        pendingCapture = null
        when {
            taken && capture != null -> {
                photoNote = null
                cropFrom = capture.file
            }
            // A cancelled shot is not an error, but the scratch file may hold the start of a
            // write, so it goes rather than being mistaken for a capture on the next attempt.
            capture != null -> {
                vm.cancelPhoto(draft.id)
                photoNote = null
            }
            else -> photoNote = "The camera left nothing usable to frame."
        }
    }

    /**
     * Ask the VM for a scratch capture, then point the camera at it. The order matters and is
     * the plan's: a device with nothing to offer answers null, and the editor says the camera
     * is not available rather than launching an intent whose photograph would land nowhere.
     */
    fun askForCamera() {
        vm.cameraCapture(draft.id) { capture ->
            if (capture == null) {
                photoNote = "No camera is available here. Choose a photo instead."
            } else {
                photoNote = null
                pendingCapture = capture
                shootPhoto.launch(capture.uri)
            }
        }
    }

    val identityTail = draft.mac?.takeLast(6)?.uppercase() ?: draft.id.uppercase()

    Box(
        Modifier
            .fillMaxSize()
            .background(HudColors.Scrim)
            // Consumes the tap so nothing behind the scrim can be hit by accident.
            .clickable(onClick = {}),
        contentAlignment = Alignment.Center,
    ) {
        HudEnter(
            Modifier.fillMaxWidth().widthIn(max = 520.dp).padding(horizontal = 24.dp),
            durationMs = HudMotion.SHORT_MS,
            riseDp = 8,
            scaleFrom = 0.98f,
        ) {
            Column(
                Modifier
                    .hudGlass()
                    .verticalScroll(rememberScrollState())
                    .padding(22.dp),
            ) {
                Text(
                    if (isNew) "NAME THIS DASHBOARD" else "EDIT VEHICLE",
                    color = HudColors.Cyan,
                    style = HudeSectionTitle(),
                )
                Spacer(Modifier.height(12.dp))
                Text(
                    if (isNew) {
                        "A dashboard answered that no card carries. Give it a name and the app " +
                            "keeps a card for it — the address book, the photo and the trips " +
                            "all hang off that card."
                    } else {
                        "What this card says about the vehicle, and what the app measured. " +
                            "Only the first half is edited here."
                    },
                    color = HudColors.TextMuted,
                    style = HudeCfgHelp(),
                )
                Spacer(Modifier.height(16.dp))

                PhotoWell(
                    hasPhoto = hasPhoto,
                    preview = well,
                    onGallery = {
                        pickFromGallery.launch(
                            PickVisualMediaRequest(
                                ActivityResultContracts.PickVisualMedia.ImageOnly,
                            ),
                        )
                    },
                    onCamera = { askForCamera() },
                    onRemove = {
                        hasPhoto = false
                        well = null
                        photoNote = null
                        vm.removePhoto(draft.id)
                    },
                )
                Spacer(Modifier.height(16.dp))

                // The evidence half, read-only. A blank identity is stated rather than left
                // out: it is the reason this card cannot be recognised when it answers, and
                // hiding that would leave the user guessing why the other card exists.
                Field(
                    "IDENTITY (FROM THE DASHBOARD)",
                    draft.mac ?: "not reported by this dashboard",
                )
                Spacer(Modifier.height(10.dp))
                Field(
                    "ADDRESSES THE APP DIALS",
                    if (draft.ips.isEmpty()) "none yet" else draft.ips.joinToString(", "),
                )
                Spacer(Modifier.height(16.dp))

                Field(
                    "NAME",
                    name,
                    onValueChange = { name = it },
                    placeholder = "UNTITLED $identityTail",
                )
                Spacer(Modifier.height(12.dp))

                // A row of three rather than a spinner: the app has no dropdown in its
                // furniture, and on a tablet all three choices are visible at once and one
                // tap away, which is what a dropdown was for in the first place.
                Text("TYPE", color = HudColors.TextMuted, style = HudeFormLabel())
                Spacer(Modifier.height(6.dp))
                Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                    for (option in VehicleType.values()) {
                        Box(Modifier.weight(1f).heightIn(min = 44.dp)) {
                            HudButton(
                                option.name,
                                style = if (option == type) HudButtonStyle.PRIMARY else HudButtonStyle.SECONDARY,
                            ) { type = option }
                        }
                    }
                }
                Spacer(Modifier.height(14.dp))

                Field("PLATE", plate, onValueChange = { plate = it }, placeholder = "plate, if the vehicle has one")
                Spacer(Modifier.height(12.dp))
                Field("MAKER", maker, onValueChange = { maker = it }, placeholder = "Toyota")
                Spacer(Modifier.height(12.dp))
                Field("MODEL", model, onValueChange = { model = it }, placeholder = "Corolla")
                Spacer(Modifier.height(12.dp))
                Field(
                    "YEAR", year,
                    onValueChange = { year = it.filter { c -> c.isDigit() }.take(4) },
                    placeholder = "1998",
                    keyboardType = KeyboardType.Number,
                )
                Spacer(Modifier.height(12.dp))
                Field("NOTES", notes, onValueChange = { notes = it }, placeholder = "anything the card should remember")

                if (refusal != null) {
                    Spacer(Modifier.height(14.dp))
                    Text(refusal, color = HudColors.Red, style = HudeCfgHelp())
                }
                photoNote?.let {
                    Spacer(Modifier.height(10.dp))
                    Text(it, color = HudColors.Amber, style = HudeCfgHelp())
                }

                Spacer(Modifier.height(18.dp))
                Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(10.dp)) {
                    Box(Modifier.weight(1f).heightIn(min = 48.dp)) {
                        HudButton("SAVE", style = HudButtonStyle.PRIMARY) {
                            onSave(
                                draft.copy(
                                    name = name, type = type, plate = plate, maker = maker,
                                    model = model, year = year, notes = notes,
                                ),
                            )
                        }
                    }
                    Box(Modifier.weight(1f).heightIn(min = 48.dp)) {
                        HudButton("CANCEL", style = HudButtonStyle.SECONDARY) { onDismiss() }
                    }
                }

                if (!isNew) {
                    Spacer(Modifier.height(14.dp))
                    Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(10.dp)) {
                        Box(Modifier.weight(1f).heightIn(min = 44.dp)) {
                            HudButton("DELETE VEHICLE", style = HudButtonStyle.DANGER) { confirming = true }
                        }
                    }
                }
            }
        }
    }

    // Both overlays sit above the form rather than inside it: the form is scrolled, and a
    // confirmation or a crop that scrolled with it would be off the screen by the time the
    // user reached the bottom of the fields.

    if (confirming) {
        // The plan's sentence, in the app's own dialog - and no card id in it. The drives are
        // found by name now; naming a row key in a warning would be a detail nobody can use.
        HudConfirmDialog(
            message = "The card goes, its photo goes with it, and the addresses it remembered " +
                "stop being dialled. The drives this vehicle took stay in the trip log.",
            onConfirm = onDelete,
            onDismiss = { confirming = false },
            confirmLabel = "FORGET ${draft.name.ifBlank { "THE CARD" }}",
            cancelLabel = "KEEP IT",
            confirmStyle = HudButtonStyle.DANGER,
        )
    }

    cropFrom?.let { staged ->
        PhotoCropPanel(
            source = staged,
            onUse = { rect ->
                vm.applyPhotoCrop(staged, draft.id, rect) { landed ->
                    // Both answers close the crop step; only the failure leaves a note. A
                    // null here means the picture never became a file, so the card still has
                    // the photo it had - and saying so is better than pretending it worked.
                    cropFrom = null
                    if (landed == null) {
                        photoNote = "The photo could not be cut down to a file. The card keeps " +
                            "the picture it had."
                    } else {
                        hasPhoto = true
                        photoNote = null
                    }
                }
            },
            onCancel = {
                cropFrom = null
                vm.cancelPhoto(draft.id)
            },
        )
    }
}

/**
 * The photo well: the card's picture at the top of the form, and the three things that can
 * be done to it. A card with no photo shows the bar's car glyph rather than a blank frame,
 * the same honesty the garage cover keeps - an empty square that shows nothing looks broken,
 * and an empty square that shows the glyph says "vehicle, no photo yet".
 *
 * REMOVE only appears when the record claims a photo, because "remove" for a card that has
 * none is a button that does nothing.
 */
@Composable
private fun PhotoWell(
    hasPhoto: Boolean,
    preview: Bitmap?,
    onGallery: () -> Unit,
    onCamera: () -> Unit,
    onRemove: () -> Unit,
) {
    Text("PHOTO", color = HudColors.TextMuted, style = HudeFormLabel())
    Spacer(Modifier.height(6.dp))
    Box(
        Modifier
            .size(WELL_EDGE.dp)
            .clip(HudRadius.CardShape)
            .background(HudColors.Panel)
            .border(1.dp, HudColors.Hairline, HudRadius.CardShape),
        contentAlignment = Alignment.Center,
    ) {
        preview?.let {
            Image(
                bitmap = it.asImageBitmap(),
                contentDescription = "this card's photo",
                contentScale = ContentScale.Crop,
                modifier = Modifier.size(WELL_EDGE.dp),
            )
        }
        if (preview == null) {
            Icon(
                HudIcons.Car,
                contentDescription = null,
                tint = HudColors.TextDim,
                modifier = Modifier.size(46.dp),
            )
        }
    }
    Spacer(Modifier.height(10.dp))
    Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(8.dp)) {
        Box(Modifier.weight(1f).heightIn(min = 44.dp)) {
            HudButton("CHOOSE PHOTO", style = HudButtonStyle.SECONDARY, onClick = onGallery)
        }
        Box(Modifier.weight(1f).heightIn(min = 44.dp)) {
            HudButton("TAKE PHOTO", style = HudButtonStyle.SECONDARY, onClick = onCamera)
        }
    }
    if (hasPhoto) {
        Spacer(Modifier.height(8.dp))
        Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(8.dp)) {
            Box(Modifier.weight(1f).heightIn(min = 40.dp)) {
                HudButton("REMOVE PHOTO", style = HudButtonStyle.DANGER, onClick = onRemove)
            }
        }
    }
}

/** Edge of the editor's photo well, in dp. Twice the card cover, and still one hand's width. */
private const val WELL_EDGE = 164

/**
 * A label over a value. With [onValueChange] it is an editable field; without it the value
 * is text the user reads but does not type — the same furniture either way, so the read-only
 * rows above the form do not look like a different kind of control.
 */
@Composable
private fun Field(
    label: String,
    value: String,
    onValueChange: ((String) -> Unit)? = null,
    placeholder: String = "",
    keyboardType: KeyboardType = KeyboardType.Text,
) {
    Column(Modifier.fillMaxWidth()) {
        Text(label, color = HudColors.TextMuted, style = HudeFormLabel())
        Spacer(Modifier.height(6.dp))
        if (onValueChange == null) {
            Text(value, color = HudColors.TextPrimary, style = HudeCfgLive())
        } else {
            HudInput(
                value = value,
                onValueChange = onValueChange,
                modifier = Modifier.fillMaxWidth(),
                style = HudInputStyle.FIELD,
                placeholder = placeholder,
                keyboardType = keyboardType,
                imeAction = ImeAction.Next,
            )
        }
    }
}
