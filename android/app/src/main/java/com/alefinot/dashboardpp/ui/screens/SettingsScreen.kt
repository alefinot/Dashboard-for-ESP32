package com.alefinot.dashboardpp.ui.screens

import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.remember
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.unit.dp
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import com.alefinot.dashboardpp.api.EspApi
import com.alefinot.dashboardpp.service.TripLogService
import com.alefinot.dashboardpp.trips.RecorderSettings
import androidx.compose.runtime.rememberCoroutineScope
import kotlinx.coroutines.launch
import com.alefinot.dashboardpp.trips.TripRuntime
import com.alefinot.dashboardpp.ui.components.HudButton
import com.alefinot.dashboardpp.ui.components.HudButtonStyle
import com.alefinot.dashboardpp.ui.components.HudConfirmDialog
import com.alefinot.dashboardpp.ui.components.HudToggle
import com.alefinot.dashboardpp.ui.components.hudBytes
import com.alefinot.dashboardpp.ui.theme.HudColors
import com.alefinot.dashboardpp.ui.theme.HudBrushes
import com.alefinot.dashboardpp.ui.theme.HudRadius
import com.alefinot.dashboardpp.ui.theme.HudeCfgHelp
import com.alefinot.dashboardpp.ui.theme.HudeCfgLive
import com.alefinot.dashboardpp.ui.theme.HudeCfgNote
import com.alefinot.dashboardpp.ui.theme.HudeFormLabel
import com.alefinot.dashboardpp.ui.theme.HudeHeading
import com.alefinot.dashboardpp.ui.theme.HudePerfLabel
import com.alefinot.dashboardpp.ui.theme.HudeSectionTitle
import com.alefinot.dashboardpp.ui.theme.HudePerfValue
import com.alefinot.dashboardpp.ui.theme.hudCard
import com.alefinot.dashboardpp.viewmodel.ConnectionViewModel
import com.alefinot.dashboardpp.viewmodel.TripsViewModel

/**
 * The "APP SETTINGS" tab — controls that apply to the Android app itself. The
 * ESP's own configuration (network, save / reboot / reset) stays in its Web UI
 * on the "ESP SETTINGS" tab and is not duplicated here.
 *
 * This is a tab, not an overlay: nothing to dismiss and no back handler to
 * install. It is also the settings page when there is no device, which is the point
 * of the nullable [ip]: the trip files, the motion switch and the storage figures
 * belong to the phone, so they stay readable and editable offline. Only the lines
 * that describe a device go quiet - the address and the firmware. The vehicle page is
 * where a device is forgotten now: a card is a vehicle, not an address, and deleting
 * one needs the name on the screen to confirm it.
 */
@Composable
fun SettingsScreen(
    vm: ConnectionViewModel,
    ip: String?,
    version: String?,
    trips: TripsViewModel,
    tripsAvailable: Boolean,
) {
    val context = LocalContext.current
    val recording by TripRuntime.state.collectAsStateWithLifecycle()
    var storageLine by remember { mutableStateOf("") }
    var showConfirmClear by remember { mutableStateOf(false) }
    val settings = remember { RecorderSettings(context) }
    val scope = rememberCoroutineScope()
    val recordingEnabled by settings.recordingEnabled
        .collectAsStateWithLifecycle(initialValue = true)
    val motionOn by settings.motionEnabled.collectAsStateWithLifecycle(initialValue = true)
    val appVersion = remember {
        try {
            val app = context.applicationContext
            app.packageManager.getPackageInfo(app.packageName, 0).versionName ?: "?"
        } catch (e: Exception) {
            "?"
        }
    }
    Box(
        Modifier.fillMaxSize()
            .background(HudColors.Bg),
    ) {
        Column(
            Modifier.fillMaxSize().verticalScroll(rememberScrollState()).padding(24.dp),
        ) {
            Text("APP SETTINGS", style = HudeHeading().copy(brush = HudBrushes.title))
            Spacer(Modifier.height(24.dp))
            Box(
                Modifier.fillMaxWidth().hudCard()
                    .padding(horizontal = 14.dp, vertical = 12.dp),
            ) {
                Column {
                    // A label/value pair inside a card is the page's Performance
                    // Monitor row: `.perf-label` on the left, `.perf-value` monospace
                    // on the right. Here they stack, because the values are long.
                    Text(
                        "DEVICE IP",
                        color = HudColors.TextMuted,
                        style = HudePerfLabel(),
                    )
                    Text(ip ?: "NOT CONNECTED", color = HudColors.TextPrimary, style = HudePerfValue())
                    Spacer(Modifier.height(12.dp))
                    Text(
                        "FIRMWARE",
                        color = HudColors.TextMuted,
                        style = HudePerfLabel(),
                    )
                    Text(
                        version ?: "—",
                        color = HudColors.TextPrimary,
                        style = HudePerfValue(),
                    )
                    Spacer(Modifier.height(12.dp))
                    Text(
                        "APP",
                        color = HudColors.TextMuted,
                        style = HudePerfLabel(),
                    )
                    Text(
                        appVersion,
                        color = HudColors.TextPrimary,
                        style = HudePerfValue(),
                    )
                    Spacer(Modifier.height(12.dp))
                    Text(
                        "Dashboard++ by @ale.finot",
                        color = HudColors.Cyan,
                        style = HudeFormLabel(),
                    )
                }
            }
            Spacer(Modifier.height(24.dp))

            // ── Trip recording ───────────────────────────────────────────────
            RecordingCard(
                available = tripsAvailable,
                recording = recording.recording,
                paused = recording.pausedForWebUi,
                needsUpdate = recording.lastError == EspApi.FIRMWARE_UPDATE_REQUIRED,
            )
            Spacer(Modifier.height(12.dp))
            Row(
                Modifier.fillMaxWidth().height(48.dp),
                verticalAlignment = Alignment.CenterVertically,
            ) {
                // `label` in the page's own colour: every form label in the settings
                // form is cyan, not muted (`label { color: var(--accent-cyan) }`).
                Text("AUTO-START ON CONNECT", color = HudColors.Cyan, style = HudeFormLabel())
                Spacer(Modifier.width(12.dp))
                HudToggle(on = recordingEnabled) { on ->
                    scope.launch { settings.setRecordingEnabled(on) }
                    // The switch is a preference first and a service second: offline there
                    // is nothing to sample, so turning it on saves the intent for the next
                    // connection instead of starting a recorder with no device behind it.
                    if (on && ip != null) TripLogService.start(context, ip) else TripLogService.stop(context)
                }
            }
            Spacer(Modifier.height(12.dp))
            Row(
                Modifier.fillMaxWidth().height(48.dp),
                verticalAlignment = Alignment.CenterVertically,
            ) {
                // Same label treatment as every other form label on this screen:
                // cyan, `label { color: var(--accent-cyan) }`.
                Text("MOTION", color = HudColors.Cyan, style = HudeFormLabel())
                Spacer(Modifier.width(12.dp))
                HudToggle(on = motionOn) { on -> scope.launch { settings.setMotionEnabled(on) } }
            }
            Text(
                "On, the app moves the way the dashboard does: panels arrive with the " +
                    "page's own fade, the tab marker travels to the tab you tap, the " +
                    "recorded trips come in one after another, and a live recording " +
                    "breathes. Off, everything is simply in place. A device that turns " +
                    "its own animation scale off stops the app too, without asking.",
                color = HudColors.TextDim,
                style = HudeCfgNote(),
            )
            Spacer(Modifier.height(12.dp))
            HudButton("STORAGE & FILES", style = HudButtonStyle.SECONDARY) {
                storageLine = hudBytes(trips.storageBytes.value) + " recorded · " +
                    trips.rows.value.size + " trip files"
            }
            Spacer(Modifier.height(8.dp))
            if (storageLine.isNotEmpty()) {
                Text(storageLine, color = HudColors.TextMuted, style = HudeCfgLive())
                Spacer(Modifier.height(8.dp))
            }
            // The page never asks by turning one button into two: `confirmDialog()`
            // puts a modal box over the screen, and the only ways out are its two
            // buttons or Escape. Same here - and deleting every recorded drive is
            // exactly the kind of action that should not be one careless tap deep.
            HudButton("DELETE ALL TRIPS", style = HudButtonStyle.DANGER) {
                showConfirmClear = true
            }
            Spacer(Modifier.height(4.dp))
            Text(
                "One CSV per ESP power session, written to the app's own storage " +
                    "(no SD-card permission). The recorder samples once a second and " +
                    "stands down while the Web UI is on screen. Open drives are never " +
                    "deleted. Share a trip as CSV or GPX from the Trips tab.",
                color = HudColors.TextDim,
                style = HudeCfgNote(),
            )
            Spacer(Modifier.height(24.dp))
            // No connect, no search, no address box, no forget: a device belongs to a
            // vehicle, so every one of those actions sits on the vehicle page's card and
            // nowhere else. What is left here are the app's own switches, which work
            // whether or not a dashboard is on the network.
        }

        if (showConfirmClear) {
            HudConfirmDialog(
                message = "Delete every recorded trip? All CSV files the app has " +
                    "written go away. A drive that is still being recorded is kept.",
                confirmLabel = "DELETE ALL",
                confirmStyle = HudButtonStyle.DANGER,
                onConfirm = {
                    trips.deleteAll()
                    storageLine = "All recorded trips removed."
                    showConfirmClear = false
                },
                onDismiss = { showConfirmClear = false },
            )
        }
    }
}

/**
 * Recording status, and the reason when it is not running. "Needs update" is for
 * firmware with no /api/snapshot endpoint: the app found the device on
 * /api/live, so the honest message is that the firmware is too old, not that the
 * device is missing.
 */
@Composable
private fun RecordingCard(
    available: Boolean,
    recording: Boolean,
    paused: Boolean,
    needsUpdate: Boolean,
) {
    val (status, color) = when {
        needsUpdate || !available -> "NEEDS FIRMWARE UPDATE" to HudColors.Amber
        recording -> "RECORDING NOW" to HudColors.Green
        paused -> "PAUSED · WEB UI OPEN" to HudColors.Amber
        else -> "NOT RECORDING" to HudColors.TextDim
    }
    // The page's status strip (`#msg` plus its `.msg-*` variants): 8px radius, a
    // hairline in the status colour at 30%, that colour at 8% as the fill over the
    // status strip's own `rgba(15,23,42,.6)`, and the status colour in the label.
    Box(
        Modifier.fillMaxWidth()
            .hudCard(
                fill = HudColors.Msg,
                borderColor = color.copy(alpha = 0.3f),
            )
            .background(color.copy(alpha = 0.08f), HudRadius.CardShape)
            .padding(12.dp),
    ) {
        Column {
            Text(status, color = color, style = HudeSectionTitle())
            Spacer(Modifier.height(6.dp))
            Text(
                when {
                    needsUpdate || !available ->
                        "This device does not publish /api/snapshot, so speed, GNSS " +
                            "and trip figures are unavailable and drives cannot be " +
                            "logged. Update the dashboard firmware to V1.4.6 or newer."
                    paused ->
                        "The Web UI on this phone is the dashboard's only HTTP client, " +
                            "so logging is paused while you configure. It resumes when " +
                            "you leave the tab; the drive stays one trip with a gap " +
                            "marked where it paused."
                    recording ->
                        "Sampling once per second. This Android version keeps polling " +
                            "with the screen off or the app backgrounded; battery " +
                            "optimization for this app is switched off so gaps do not " +
                            "appear at every screen timeout."
                    else ->
                        "Logging idle: connected, waiting for samples, or auto-start " +
                            "is off."
                },
                color = HudColors.TextMuted,
                style = HudeCfgHelp(),
            )
        }
    }
}
