package com.alefinot.dashboardpp.ui.screens

import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.Icon
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.remember
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import com.alefinot.dashboardpp.vehicles.Vehicle
import com.alefinot.dashboardpp.ui.components.HudButton
import com.alefinot.dashboardpp.ui.components.HudButtonStyle
import com.alefinot.dashboardpp.ui.components.HudIcons
import com.alefinot.dashboardpp.ui.theme.HudBrushes
import com.alefinot.dashboardpp.ui.theme.HudColors
import com.alefinot.dashboardpp.ui.theme.HudRadius
import com.alefinot.dashboardpp.ui.theme.HudeCfgNote
import com.alefinot.dashboardpp.ui.theme.HudeHeading
import com.alefinot.dashboardpp.ui.theme.HudePerfLabel
import com.alefinot.dashboardpp.ui.theme.HudePerfValue
import com.alefinot.dashboardpp.ui.theme.HudeSectionTitle
import com.alefinot.dashboardpp.ui.theme.hudCard
import com.alefinot.dashboardpp.viewmodel.ConnectionUiState
import com.alefinot.dashboardpp.viewmodel.ConnectionViewModel

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
) {
    // Collected once per view model: `vehicleCards()` hands back a cold flow, and calling
    // it in place would rebuild and restart the collection on every recomposition.
    val cards = remember(connection) { connection.vehicleCards() }
    val vehicles by cards.collectAsStateWithLifecycle(initialValue = emptyList())
    val selectedId = connection.selectedVehicleId.value
    val state = connection.uiState.value

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
                // Nothing paired. The page still has to say what the button below does,
                // and where to go for a device the user already has the address of — the
                // two questions a first run asks, answered before being asked.
                Box(
                    Modifier
                        .fillMaxWidth()
                        .hudCard(borderColor = HudColors.Amber.copy(alpha = 0.3f))
                        .padding(14.dp),
                ) {
                    Column {
                        Text("NOTHING PAIRED YET", color = HudColors.Amber, style = HudeSectionTitle())
                        Spacer(Modifier.height(8.dp))
                        Text(
                            "FIND DASHBOARD++ looks for a Dashboard++ on this network and " +
                                "pairs with the one it finds. If you know the address " +
                                "already, connect by hand instead — the app still keeps a " +
                                "card for it, because the card is what a trip belongs to.",
                            color = HudColors.TextMuted,
                            style = HudeCfgNote(),
                        )
                    }
                }
                Spacer(Modifier.height(18.dp))
            } else {
                for (v in vehicles) {
                    VehicleCard(
                        vehicle = v,
                        selected = v.id == selectedId,
                        // Only the selected card can be mid-attempt: the others are
                        // records, not candidates, and saying "connecting" about a card
                        // the app is not reaching for would be a lie.
                        state = if (v.id == selectedId) state else null,
                        onClick = { connection.selectVehicle(v.id) },
                    )
                    Spacer(Modifier.height(10.dp))
                }
            }

            Row(Modifier.fillMaxWidth()) {
                HudButton("FIND DASHBOARD++", style = HudButtonStyle.PRIMARY) { connection.rescan() }
                Spacer(Modifier.width(12.dp))
                HudButton("CONNECT BY HAND (IP / HOSTNAME)", style = HudButtonStyle.SECONDARY) {
                    onEnterIp()
                }
            }
            Spacer(Modifier.height(10.dp))
            Text(
                "FIND DASHBOARD++ looks on this network only. A vehicle the app has " +
                    "paired before is tried at its own remembered addresses first, which " +
                    "is why coming back to a car you drive every day takes a couple of " +
                    "seconds and a first hunt does not.",
                color = HudColors.TextDim,
                style = HudeCfgNote(),
            )
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
 * than a status that comes and goes. The cover is a placeholder panel until a photo of
 * the vehicle exists, drawn with the bar's own car glyph so the shape means "vehicle" in
 * both places — and it is drawn dim on purpose. An empty frame that shows the glyph is
 * honest about being empty; one that shows a stock car is not.
 */
@Composable
private fun VehicleCard(
    vehicle: Vehicle,
    selected: Boolean,
    state: ConnectionUiState?,
    onClick: () -> Unit,
) {
    val (status, statusColor) = cardStatus(vehicle, state)
    Row(
        Modifier
            .fillMaxWidth()
            .hudCard(
                borderColor = if (selected) HudColors.Cyan else HudColors.CardHairline,
                highlight = if (selected) 0.05f else 0f,
            )
            .clickable(onClickLabel = "Select ${vehicle.name}") { onClick() }
            .padding(12.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Box(
            Modifier
                .size(width = 76.dp, height = 54.dp)
                .background(HudColors.Panel, HudRadius.CardShape)
                .border(1.dp, HudColors.Hairline, HudRadius.CardShape),
            contentAlignment = Alignment.Center,
        ) {
            Icon(
                HudIcons.Car,
                contentDescription = null,
                tint = if (selected) HudColors.CyanDim else HudColors.TextDim,
                modifier = Modifier.size(30.dp),
            )
        }
        Spacer(Modifier.width(12.dp))
        Column(Modifier.fillMaxWidth()) {
            Text(
                vehicle.name.ifBlank { "UNNAMED VEHICLE" },
                color = if (selected) HudColors.Cyan else HudColors.TextPrimary,
                style = HudeHeading().copy(fontSize = 17.sp),
            )
            Spacer(Modifier.height(4.dp))
            Text(status, color = statusColor, style = HudePerfLabel())
            // The address is reference, not headline: it is what the app dialed last, and
            // it changes when the car moves. A card that leads with an address is a card
            // that is still thinking of itself as a bookmark.
            vehicle.ips.firstOrNull()?.let {
                Spacer(Modifier.height(4.dp))
                Text(it, color = HudColors.TextDim, style = HudePerfValue())
            }
        }
    }
}

/**
 * What a card says about itself. The caller passes a [ConnectionUiState] only for the
 * card the app is actually reaching for, so everything here is about one vehicle.
 *
 * The words a hunting or failing state gives are passed through **verbatim**. A card that
 * rewrites "ESP32 at 192.168.1.50 answered /api/perf, but that unit is not the vehicle
 * you selected" as "CONNECTION LOST" has stopped reporting and started reassuring, which
 * is the exact failure the whole refactor exists to remove.
 */
private fun cardStatus(
    vehicle: Vehicle,
    state: ConnectionUiState?,
): Pair<String, Color> = when {
    state is ConnectionUiState.Connected ->
        "CONNECTED · ${state.ip}" to HudColors.Green

    state is ConnectionUiState.Connecting ->
        state.mode.stageLine().uppercase() to HudColors.Amber

    state is ConnectionUiState.Failed -> state.reason to HudColors.Red

    // Idle with a reason is the case where the page has something specific to say and
    // nothing running — the selection was cleared, or a session just ended.
    state is ConnectionUiState.Idle && state.reason != null -> state.reason to HudColors.Amber

    vehicle.ips.isNotEmpty() -> "SAVED · NOT HERE RIGHT NOW" to HudColors.TextMuted

    else -> "NEVER SEEN ON A NETWORK YET" to HudColors.TextDim
}
