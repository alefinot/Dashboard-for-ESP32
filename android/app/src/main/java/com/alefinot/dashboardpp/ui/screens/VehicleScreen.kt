package com.alefinot.dashboardpp.ui.screens

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
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.layout.widthIn
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.Icon
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableLongStateOf
import androidx.compose.runtime.produceState
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.asImageBitmap
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.layout.ContentScale
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import com.alefinot.dashboardpp.vehicles.Vehicle
import com.alefinot.dashboardpp.ui.components.HudButton
import com.alefinot.dashboardpp.ui.components.HudButtonStyle
import com.alefinot.dashboardpp.ui.components.HudIcons
import com.alefinot.dashboardpp.ui.components.ScanRing
import com.alefinot.dashboardpp.ui.theme.HudBrushes
import com.alefinot.dashboardpp.ui.theme.HudColors
import com.alefinot.dashboardpp.ui.theme.HudRadius
import com.alefinot.dashboardpp.ui.theme.HudeCfgNote
import com.alefinot.dashboardpp.ui.theme.HudeHeading
import com.alefinot.dashboardpp.ui.theme.HudePerfLabel
import com.alefinot.dashboardpp.ui.theme.HudePerfValue
import com.alefinot.dashboardpp.ui.theme.HudeSectionTitle
import com.alefinot.dashboardpp.ui.theme.hudCard
import com.alefinot.dashboardpp.viewmodel.ConnectMode
import com.alefinot.dashboardpp.viewmodel.ConnectionUiState
import com.alefinot.dashboardpp.viewmodel.ConnectionViewModel
import kotlinx.coroutines.delay

/**
 * The vehicle page — the garage, and the app's home.
 *
 * A card here is a **vehicle**, not an address: a thing that exists in the world and that
 * a phone can be near, which is why it can be picked while nothing is answering, and why
 * "which car am I in?" is a question this page can answer with Wi-Fi off. That
 * distinction is the reason the hunt was rewritten (see [ConnectionCore] and
 * [ConnectionViewModel]): an address is not a vehicle, and a list of addresses is not a
 * garage.
 *
 * What this page owns:
 *  - the list, one card per paired vehicle — the selected one first, then the ones that
 *    have connected most recently ([com.alefinot.dashboardpp.vehicles.VehicleStore.sorted]);
 *  - the selection: tapping a card makes that vehicle the one the app reaches for, and a
 *    vehicle that has never been seen on a network is still selectable;
 *  - the honest state of each card: connected, saved, being reached for, or never seen.
 *
 * A card with nothing to show says so with a placeholder instead of a stock picture. When
 * a card is selected and the app is trying to reach it, or has failed to, the card carries
 * the [ConnectionUiState]'s own words **verbatim** — the same sentence the view model
 * gives the covers, because a paraphrased failure is how an app ends up saying
 * "searching" about a network that already answered.
 *
 * Forgetting a vehicle is not on this page yet; it needs the name on the card to confirm
 * it, which is the next thing the page grows.
 */
@Composable
fun VehicleScreen(
    connection: ConnectionViewModel,
    onEnterIp: () -> Unit,
    onEdit: (String) -> Unit,
    onNameUnclaimed: () -> Unit,
) {
    // Collected once per view model: `vehicleCards()` hands back a cold flow, and calling
    // it in place would rebuild and restart the collection on every recomposition.
    val cards = remember(connection) { connection.vehicleCards() }
    val vehicles by cards.collectAsStateWithLifecycle(initialValue = emptyList())
    val selectedId = connection.selectedVehicleId.value
    val state = connection.uiState.value
    val notice by connection.notice
    val unclaimed by connection.unclaimed

    // The clock the derived status lines ("LAST SEEN YESTERDAY") are measured against.
    // Read once per composition and refreshed once a minute while this page is on screen:
    // a card should not have to be revisited to stop saying "today" after midnight, and a
    // per-frame clock would rebuild every card sixty times a second for a sentence that
    // changes once a day.
    var nowMs by remember { mutableLongStateOf(System.currentTimeMillis()) }
    LaunchedEffect(Unit) {
        while (true) {
            delay(60_000)
            nowMs = System.currentTimeMillis()
        }
    }

    Box(
        Modifier
            .fillMaxSize()
            .background(HudColors.Bg),
    ) {
        Column(
            Modifier
                .fillMaxSize()
                .verticalScroll(rememberScrollState())
                .padding(24.dp),
        ) {
            Text("VEHICLES", style = HudeHeading().copy(brush = HudBrushes.title))
            Spacer(Modifier.height(6.dp))
            Text(
                "A card is a vehicle, not an address. Pick one and the app reaches for " +
                    "that dashboard; a vehicle that has never been seen on this network " +
                    "is still a vehicle.",
                color = HudColors.TextDim,
                style = HudeCfgNote(),
            )
            Spacer(Modifier.height(18.dp))

            if (vehicles.isEmpty()) {
                // One line. The card below already explains what its two buttons do, so an
                // empty garage only has to say that nothing has been met yet - a second
                // paragraph about what a search is would be the page talking to itself.
                Text(
                    "Nothing has been met yet: this app has not reached a dashboard on any " +
                        "network it knows.",
                    color = HudColors.TextMuted,
                    style = HudeCfgNote(),
                )
                Spacer(Modifier.height(14.dp))
            } else {
                for (v in vehicles) {
                    VehicleCard(
                        vehicle = v,
                        selected = v.id == selectedId,
                        // Only the selected card can be mid-attempt: the others are
                        // records, not candidates, and saying "connecting" about a card
                        // the app is not reaching for would be a lie. The connected card
                        // is matched separately so it can always offer its own way out.
                        state = if (v.id == selectedId) state else null,
                        connected = connectedCard(v, state),
                        nowMs = nowMs,
                        ringProgress = connection.scanProgress.value,
                        photo = cardPhoto(connection, v.id),
                        onClick = { connection.selectVehicle(v.id) },
                        onConnect = { connection.connectVehicle(v.id, ConnectMode.SEARCH) },
                        onRetrySaved = { connection.connectVehicle(v.id, ConnectMode.PROBE) },
                        onDisconnect = { connection.disconnect() },
                        onEdit = { onEdit(v.id) },
                    )
                    Spacer(Modifier.height(10.dp))
                }
            }

            // Two sentences that are not this page's to write, and that no card can
            // represent: a unit answered that no card claims, or the unit that answered
            // belongs to a different card than the one selected. Both are published by the
            // view model and would otherwise be lost, and either one means a card on this
            // list is about to be described wrongly.
            notice?.let {
                NoticeCard("THE APP AND THE CARD DISAGREE", it, HudColors.Amber)
                Spacer(Modifier.height(10.dp))
            }
            unclaimed?.let {
                NoticeCard(
                    "A DASHBOARD ANSWERED THAT NO CARD CLAIMS",
                    "${it.ip} (firmware ${it.version ?: "unknown"}) is not on this list yet. " +
                        "Name it and it becomes a card of its own.",
                    HudColors.Cyan,
                    actionLabel = "NAME IT",
                    onAction = onNameUnclaimed,
                )
                Spacer(Modifier.height(10.dp))
            }

            AddVehicleCard(
                onSearch = { connection.rescan() },
                onEnterIp = onEnterIp,
            )
        }
    }
}

/**
 * A card's photo, decoded at the size the cover is drawn and no larger, and recomposed when
 * [ConnectionViewModel.photoVersion] says the picture on disk changed under us.
 *
 * Twice the cover's long edge rather than once: the cover is 76 dp, which is 113 px on this
 * tablet's density but 228 px on a modern phone, and a bitmap decoded at the smaller number
 * goes soft the moment the app runs on hardware denser than the one the layout was sketched
 * on. [VehiclePhotoStore.read] takes the request as a ceiling, not a promise - it never
 * decodes larger than the JPEG it kept (1280 px at most, usually much less), so asking for
 * twice the box costs nothing on the cards that have no photo to speak of.
 *
 * The one sharp edge is a slow read answering late: the request runs on the VM's scope, so a
 * stale answer can land after a fresh one and paint the previous picture for a moment. It is
 * self-correcting (the next recomposition keyed on [ConnectionViewModel.photoVersion] asks
 * again) and it is a card cover, not a diagnosis, so the race is left as it is rather than
 * bought with a request serial number on every card on the page.
 */
@Composable
private fun cardPhoto(connection: ConnectionViewModel, vehicleId: String): Bitmap? {
    val edgePx = with(LocalDensity.current) { 76.dp.toPx().toInt() * 2 }
    val version = connection.photoVersion.intValue
    val photo by produceState<Bitmap?>(initialValue = null, vehicleId, version) {
        connection.photo(vehicleId, edgePx) { value = it }
    }
    return photo
}

/**
 * The cover panel: photo, scan ring, or car glyph - one of the three, in that order.
 *
 * [ContentScale.Crop] on a landscape cover is the deliberate half-truth: a stored photo is a
 * square (the editor frames it that way), and filling the panel means showing its middle
 * band. That is the band the user framed with their fingers, and the alternative -
 * letterboxing a square into 76x54 - would make the garage look like a wall of stamps.
 *
 * The photo wins over the scan ring, which is a change from the placeholder-only cover: a
 * card being hunted for still says so in its status line and in the footer's phase line, and
 * the ring in the cover was never the only notice. Repeating it over a user's own photograph
 * would be the app asking to be looked at instead of looking at the car.
 */
@Composable
private fun CardCover(
    photo: Bitmap?,
    selected: Boolean,
    scanning: Boolean,
    ringProgress: Float,
) {
    Box(
        Modifier
            .size(width = 76.dp, height = 54.dp)
            .clip(HudRadius.CardShape)
            .background(HudColors.Panel)
            .border(1.dp, HudColors.Hairline, HudRadius.CardShape),
        contentAlignment = Alignment.Center,
    ) {
        photo?.let {
            Image(
                bitmap = it.asImageBitmap(),
                contentDescription = "photo of this vehicle",
                contentScale = ContentScale.Crop,
                modifier = Modifier.size(width = 76.dp, height = 54.dp),
            )
        }
        if (photo == null) {
            if (scanning) {
                ScanRing(ringProgress, Modifier.size(28.dp))
            } else {
                Icon(
                    HudIcons.Car,
                    contentDescription = null,
                    tint = if (selected) HudColors.CyanDim else HudColors.TextDim,
                    modifier = Modifier.size(30.dp),
                )
            }
        }
    }
}

/**
 * One vehicle card, drawn with the page's `.card` skin like the trip rows: same fill,
 * same hairline, same corners, so a list of vehicles and a list of drives read as the
 * same kind of thing on the same page.
 *
 * The selection is an edge, not a badge: the selected card takes the page's accent
 * hairline and a faint top highlight, because it is a standing fact about the app rather
 * than a status that comes and goes. The cover shows the vehicle's photo when the card has
 * one, and until then it is a placeholder panel drawn with the bar's own car glyph so the
 * shape means "vehicle" in both places — and it is drawn dim on purpose. An empty frame
 * that shows the glyph is honest about being empty; one that shows a stock car is not.
 * A square photo is cropped to this landscape cover rather than letterboxed, which puts the
 * middle of the picture where the vehicle is (see [CardCover]). While the app is hunting
 * for **this** card's unit the cover becomes the sweep's ring instead: that is where a
 * user is looking when they press SEARCH, and the full-screen ring that used to answer the
 * question is gone.
 *
 * What a card offers depends on what the card *is*, not on what the page is doing: the
 * connected card always offers DISCONNECT (even if the selection has moved on, because
 * that is the card with a session to end), and any other selected card offers CONNECT —
 * the whole ladder — plus, when it has an address on file, the short two-address try.
 * Every card offers EDIT, including a card nobody is standing on: a card is a record about
 * a vehicle, and records are edited whether or not they are the one being reached for. A
 * card that is neither selected nor connected offers *only* that, because a wall of
 * connection buttons for cars nobody is looking at is not a garage.
 */
@Composable
private fun VehicleCard(
    vehicle: Vehicle,
    selected: Boolean,
    state: ConnectionUiState?,
    connected: Boolean,
    nowMs: Long,
    ringProgress: Float,
    photo: Bitmap?,
    onClick: () -> Unit,
    onConnect: () -> Unit,
    onRetrySaved: () -> Unit,
    onDisconnect: () -> Unit,
    onEdit: () -> Unit,
) {
    val status = cardStatusLine(vehicle, state, nowMs)
    Column(
        Modifier
            .fillMaxWidth()
            .hudCard(
                borderColor = if (selected) HudColors.Cyan else HudColors.CardHairline,
                highlight = if (selected) 0.05f else 0f,
            )
            .clickable(onClickLabel = "Select ${vehicle.name}") { onClick() }
            .padding(12.dp),
    ) {
        Row(verticalAlignment = Alignment.CenterVertically) {
            CardCover(photo, selected, showScanRing(state), ringProgress)
            Spacer(Modifier.width(12.dp))
            Column(Modifier.fillMaxWidth()) {
                Text(
                    vehicle.name.ifBlank { "UNNAMED VEHICLE" },
                    color = if (selected) HudColors.Cyan else HudColors.TextPrimary,
                    style = HudeHeading().copy(fontSize = 17.sp),
                )
                // What the user called this vehicle, in their own words: the plate is the
                // thing they look for in a car park, and the spec line is the difference
                // between two cards with similar names. A blank field draws no line at all
                // (see [plateLine]) - a card with empty gaps reads like a form.
                plateLine(vehicle)?.let {
                    Spacer(Modifier.height(4.dp))
                    Text(it, color = HudColors.TextDim, style = HudePerfLabel())
                }
                specLine(vehicle).takeIf { it.isNotEmpty() }?.let {
                    Spacer(Modifier.height(4.dp))
                    Text(it, color = HudColors.TextMuted, style = HudeCfgNote())
                }
                Spacer(Modifier.height(4.dp))
                Text(
                    status.text,
                    color = statusColor(status.tone),
                    style = HudePerfLabel(),
                    maxLines = 3,
                    overflow = TextOverflow.Ellipsis,
                )
                // The address is reference, not headline: it is what the app dialed last,
                // and it changes when the car moves. A card that leads with an address is a
                // card that is still thinking of itself as a bookmark.
                if (!connected) {
                    vehicle.ips.firstOrNull()?.let {
                        Spacer(Modifier.height(4.dp))
                        Text(it, color = HudColors.TextDim, style = HudePerfValue())
                    }
                }
            }
        }

        Spacer(Modifier.height(10.dp))
        Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
            if (connected) {
                HudButton(
                    "DISCONNECT",
                    modifier = Modifier.widthIn(min = 120.dp),
                    style = HudButtonStyle.SECONDARY,
                ) { onDisconnect() }
            } else if (selected) {
                HudButton(
                    "CONNECT",
                    modifier = Modifier.widthIn(min = 108.dp),
                    style = HudButtonStyle.PRIMARY,
                ) { onConnect() }
                if (vehicle.ips.isNotEmpty()) {
                    HudButton(
                        "RETRY AT ${vehicle.ips.first()}",
                        modifier = Modifier.weight(1f),
                        style = HudButtonStyle.SECONDARY,
                    ) { onRetrySaved() }
                }
            }
            HudButton(
                "EDIT",
                modifier = Modifier.widthIn(min = 72.dp),
                style = HudButtonStyle.SECONDARY,
            ) { onEdit() }
        }
    }
}

/**
 * A fact the connection published that no card can carry: either the unit that answered
 * belongs to another card, or it belongs to none. Both are drawn here because the page has
 * to contradict its own cards out loud rather than let a wrong card stand.
 */
@Composable
private fun NoticeCard(
    title: String,
    text: String,
    accent: Color,
    actionLabel: String? = null,
    onAction: (() -> Unit)? = null,
) {
    Column(
        Modifier
            .fillMaxWidth()
            .hudCard(borderColor = accent.copy(alpha = 0.35f))
            .padding(14.dp),
    ) {
        Text(title, color = accent, style = HudeSectionTitle())
        Spacer(Modifier.height(8.dp))
        Text(text, color = HudColors.TextMuted, style = HudeCfgNote())
        if (actionLabel != null && onAction != null) {
            // The offer is only worth making if it can be accepted where it is made: a
            // notice the user has to go hunting for a control on to act on is a notice they
            // will dismiss instead.
            Spacer(Modifier.height(12.dp))
            Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                Box(Modifier.heightIn(min = 44.dp)) {
                    HudButton(actionLabel, style = HudButtonStyle.PRIMARY) { onAction() }
                }
            }
        }
    }
}

/**
 * The last thing in the garage, and the only way a new vehicle gets in: [plan §5] puts both
 * entry points on this page because it is the page a user is already standing on. SEARCH
 * runs the whole ladder (walk, mDNS, probe, and adopting a unit no card claims); ENTER IP
 * MANUALLY raises the same panel the covers raise, the one path for a device no discovery
 * can see.
 */
@Composable
private fun AddVehicleCard(onSearch: () -> Unit, onEnterIp: () -> Unit) {
    Column(
        Modifier
            .fillMaxWidth()
            .hudCard()
            .padding(14.dp),
    ) {
        Text("ADD VEHICLE", color = HudColors.TextDim, style = HudeSectionTitle())
        Spacer(Modifier.height(10.dp))
        Row(horizontalArrangement = Arrangement.spacedBy(12.dp)) {
            HudButton(
                "SEARCH FOR DASHBOARD++",
                modifier = Modifier.weight(1f),
                style = HudButtonStyle.PRIMARY,
            ) { onSearch() }
            HudButton(
                "ENTER IP MANUALLY",
                modifier = Modifier.weight(1f),
                style = HudButtonStyle.SECONDARY,
            ) { onEnterIp() }
        }
        Spacer(Modifier.height(10.dp))
        Text(
            "A dashboard that answers becomes a card. Name it, and the app remembers the " +
                "vehicle rather than the address - which is why coming back to a car you " +
                "drive every day takes a couple of seconds and a first hunt does not.",
            color = HudColors.TextDim,
            style = HudeCfgNote(),
        )
    }
}
