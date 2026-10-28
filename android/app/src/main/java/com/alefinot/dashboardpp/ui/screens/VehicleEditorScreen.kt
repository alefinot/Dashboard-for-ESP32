package com.alefinot.dashboardpp.ui.screens

import androidx.activity.compose.BackHandler
import androidx.compose.foundation.background
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
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.text.input.ImeAction
import androidx.compose.ui.text.input.KeyboardType
import androidx.compose.ui.unit.dp
import com.alefinot.dashboardpp.ui.components.HudButton
import com.alefinot.dashboardpp.ui.components.HudButtonStyle
import com.alefinot.dashboardpp.ui.components.HudEnter
import com.alefinot.dashboardpp.ui.components.HudInput
import com.alefinot.dashboardpp.ui.components.HudInputStyle
import com.alefinot.dashboardpp.ui.theme.HudColors
import com.alefinot.dashboardpp.ui.theme.HudMotion
import com.alefinot.dashboardpp.ui.theme.HudeCfgHelp
import com.alefinot.dashboardpp.ui.theme.HudeCfgLive
import com.alefinot.dashboardpp.ui.theme.HudeFormLabel
import com.alefinot.dashboardpp.ui.theme.HudeSectionTitle
import com.alefinot.dashboardpp.ui.theme.hudGlass
import com.alefinot.dashboardpp.vehicles.Vehicle
import com.alefinot.dashboardpp.vehicles.VehicleType

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
 * Deleting asks first, in the same panel, because the consequence is not the card: the drives
 * this vehicle took outlast it by design.
 */
@Composable
fun VehicleEditorScreen(
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
                    if (!confirming) {
                        Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(10.dp)) {
                            Box(Modifier.weight(1f).heightIn(min = 44.dp)) {
                                HudButton("DELETE VEHICLE", style = HudButtonStyle.DANGER) { confirming = true }
                            }
                        }
                    } else {
                        // The sentence the plan asks for, in the panel rather than in a
                        // dialog: a card is a record, and the drives it took are files that
                        // carry its id and stay behind on purpose.
                        Text(
                            "The card goes, its photo goes with it, and the addresses it " +
                                "remembered stop being dialled. The drives this vehicle took " +
                                "stay in the trip log, tagged ${draft.id}.",
                            color = HudColors.TextMuted,
                            style = HudeCfgHelp(),
                        )
                        Spacer(Modifier.height(12.dp))
                        Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(10.dp)) {
                            Box(Modifier.weight(1f).heightIn(min = 48.dp)) {
                                HudButton("FORGET ${draft.name.ifBlank { "THE CARD" }}", style = HudButtonStyle.DANGER) {
                                    onDelete()
                                }
                            }
                            Box(Modifier.weight(1f).heightIn(min = 48.dp)) {
                                HudButton("KEEP IT", style = HudButtonStyle.SECONDARY) { confirming = false }
                            }
                        }
                    }
                }
            }
        }
    }
}

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
