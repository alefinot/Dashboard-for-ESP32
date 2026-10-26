package com.alefinot.dashboardpp.ui.screens

import androidx.compose.foundation.background
import androidx.compose.foundation.border
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
import com.alefinot.dashboardpp.ui.components.HudToggle
import com.alefinot.dashboardpp.ui.components.hudBytes
import com.alefinot.dashboardpp.ui.theme.HudColors
import com.alefinot.dashboardpp.ui.theme.HudMonoText
import com.alefinot.dashboardpp.ui.theme.HudeDisplaySmall
import com.alefinot.dashboardpp.ui.theme.HudeLabel
import com.alefinot.dashboardpp.ui.theme.HudeSmall
import com.alefinot.dashboardpp.viewmodel.ConnectionViewModel
import com.alefinot.dashboardpp.viewmodel.TripsViewModel

/**
 * The "APP SETTINGS" tab — controls that apply to the Android app itself. The
 * ESP's own configuration (network, save / reboot / reset) stays in its Web UI
 * on the "ESP SETTINGS" tab and is not duplicated here.
 *
 * This is a tab, not an overlay: nothing to dismiss and no back handler to
 * install. The two connection actions at the bottom end the connection, and the
 * shell reacts to that by leaving the tabs — which is how you get out.
 */
@Composable
fun SettingsScreen(
    vm: ConnectionViewModel,
    ip: String,
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
    val appVersion = remember {
        try {
            val app = context.applicationContext
            app.packageManager.getPackageInfo(app.packageName, 0).versionName ?: "?"
        } catch (e: Exception) {
            "?"
        }
    }
    Box(Modifier.fillMaxSize().background(HudColors.Bg)) {
        Column(
            Modifier.fillMaxSize().verticalScroll(rememberScrollState()).padding(24.dp),
        ) {
            Text(
                "APP SETTINGS",
                color = HudColors.Cyan,
                style = HudeDisplaySmall,
            )
            Spacer(Modifier.height(24.dp))
            Box(
                Modifier
                    .fillMaxWidth()
                    .border(1.dp, HudColors.CyanDim)
                    .background(HudColors.Panel)
                    .padding(16.dp),
            ) {
                Column {
                    Text(
                        "DEVICE IP",
                        color = HudColors.TextDim,
                        style = HudeSmall,
                    )
                    Text(ip, color = HudColors.TextPrimary, style = HudMonoText)
                    Spacer(Modifier.height(12.dp))
                    Text(
                        "FIRMWARE",
                        color = HudColors.TextDim,
                        style = HudeSmall,
                    )
                    Text(
                        version ?: "UNKNOWN",
                        color = HudColors.TextPrimary,
                        style = HudMonoText,
                    )
                    Spacer(Modifier.height(12.dp))
                    Text(
                        "APP",
                        color = HudColors.TextDim,
                        style = HudeSmall,
                    )
                    Text(
                        appVersion,
                        color = HudColors.TextPrimary,
                        style = HudMonoText,
                    )
                    Spacer(Modifier.height(12.dp))
                    Text(
                        "Dashboard++ by @ale.finot",
                        color = HudColors.Cyan,
                        style = HudeSmall,
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
            Row(Modifier.fillMaxWidth(), verticalAlignment = Alignment.CenterVertically) {
                Text("AUTO-START ON CONNECT", color = HudColors.TextDim, style = HudeSmall)
                Spacer(Modifier.width(12.dp))
                HudToggle(on = recordingEnabled) { on ->
                    scope.launch { settings.setRecordingEnabled(on) }
                    if (on) TripLogService.start(context, ip) else TripLogService.stop(context)
                }
            }
            Spacer(Modifier.height(12.dp))
            HudButton("STORAGE & FILES") {
                storageLine = hudBytes(trips.storageBytes()) + " recorded · " +
                    trips.rows.value.size + " trip files"
            }
            Spacer(Modifier.height(8.dp))
            if (storageLine.isNotEmpty()) {
                Text(storageLine, color = HudColors.TextDim, style = HudeSmall)
                Spacer(Modifier.height(8.dp))
            }
            if (!showConfirmClear) {
                HudButton("DELETE ALL TRIPS") { showConfirmClear = true }
            } else {
                Row(Modifier.fillMaxWidth()) {
                    Box(Modifier.weight(1f)) {
                        HudButton("CONFIRM DELETE ALL") {
                            trips.deleteAll()
                            storageLine = "All recorded trips removed."
                            showConfirmClear = false
                        }
                    }
                    Spacer(Modifier.width(8.dp))
                    Box(Modifier.weight(1f)) {
                        HudButton("KEEP", onClick = { showConfirmClear = false })
                    }
                }
            }
            Spacer(Modifier.height(4.dp))
            Text(
                "One CSV per ESP power session, written to the app's own storage " +
                    "(no SD-card permission). The recorder samples once a second and " +
                    "stands down while the Web UI is on screen. Open drives are never " +
                    "deleted. Share a trip as CSV or GPX from the Trips tab.",
                color = HudColors.TextDim,
                style = HudeSmall,
            )
            Spacer(Modifier.height(24.dp))

            // Both of these end the connection, and the shell follows the view
            // model: as soon as it stops reporting a connection the tab row is
            // gone and discovery takes over. Settings no longer has to close
            // itself, and the old CLOSE button has nothing left to close.
            HudButton("RE-SCAN NETWORK") { vm.rescan() }
            Spacer(Modifier.height(8.dp))
            HudButton("FORGET THIS DEVICE") { vm.forgetDevice(ip) }
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
    Box(
        Modifier.fillMaxWidth().border(1.dp, color).background(HudColors.Panel).padding(12.dp),
    ) {
        Column {
            Text(status, color = color, style = HudeLabel)
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
                color = HudColors.TextDim,
                style = HudeSmall,
            )
        }
    }
}
