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
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
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
import com.alefinot.dashboardpp.ui.theme.HudeSectionTitle
import com.alefinot.dashboardpp.ui.theme.hudGlass
import com.alefinot.dashboardpp.viewmodel.ConnectionViewModel
import androidx.compose.ui.text.input.ImeAction

/**
 * Typing an address by hand — the form the old `ManualEntryScreen` had, moved from a
 * screen into a panel over the shell.
 *
 * It is a panel for the same reason the page's confirm box is a `<div>` rather than
 * a window: the app behind it is the point. The user came from a page they were
 * reading, and cancelling returns them to it with the bar, the tabs and their place
 * in the pager exactly as they left them. The old route could only return them to
 * discovery.
 *
 * The furniture is `HudConfirmDialog`'s — scrim, glass card, 440dp cap, the page's
 * short entrance — with the form inside it. Tapping the scrim does nothing and Back
 * cancels, which is the pair of behaviours the page itself has for a box like this.
 */
@Composable
fun ManualEntryPanel(vm: ConnectionViewModel, onDismiss: () -> Unit) {
    BackHandler { onDismiss() }
    val remembered = vm.lastKnownIp.value
    var ip by remember { mutableStateOf(remembered ?: "") }
    val error by vm.manualError

    Box(
        Modifier
            .fillMaxSize()
            .background(HudColors.Scrim)
            // The tap is consumed so nothing behind the scrim can be hit by accident.
            .clickable(onClick = {}),
        contentAlignment = Alignment.Center,
    ) {
        HudEnter(
            Modifier.fillMaxWidth().widthIn(max = 440.dp).padding(horizontal = 24.dp),
            durationMs = HudMotion.SHORT_MS,
            riseDp = 8,
            scaleFrom = 0.98f,
        ) {
            Column(Modifier.hudGlass().padding(22.dp)) {
                Text("ENTER IP MANUALLY", color = HudColors.Cyan, style = HudeSectionTitle())
                Spacer(Modifier.height(14.dp))
                Text(
                    "The app could not find the dashboard. If you know its address, " +
                        "type it in — it remembers it for next time.",
                    color = HudColors.TextMuted,
                    style = HudeCfgHelp(),
                )
                Spacer(Modifier.height(14.dp))
                HudInput(
                    value = ip,
                    placeholder = "192.168.1.42",
                    onValueChange = { ip = it },
                    style = HudInputStyle.FIELD,
                    keyboardType = KeyboardType.Number,
                    imeAction = ImeAction.Done,
                    onDone = { if (ip.isNotBlank()) vm.submitManualIp(ip) },
                )
                if (remembered != null && remembered != ip) {
                    Spacer(Modifier.height(10.dp))
                    Text(
                        "Last device: $remembered",
                        color = HudColors.TextDim,
                        style = HudeCfgLive(),
                    )
                }
                if (error != null) {
                    Spacer(Modifier.height(12.dp))
                    Text(error!!, color = HudColors.Red, style = HudeCfgHelp())
                }
                Spacer(Modifier.height(18.dp))
                Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(10.dp)) {
                    Box(Modifier.weight(1f).heightIn(min = 48.dp)) {
                        HudButton(
                            "CONNECT",
                            style = HudButtonStyle.PRIMARY,
                            enabled = ip.isNotBlank(),
                        ) { vm.submitManualIp(ip) }
                    }
                    Box(Modifier.weight(1f).heightIn(min = 48.dp)) {
                        HudButton("CANCEL", style = HudButtonStyle.SECONDARY) { onDismiss() }
                    }
                }
            }
        }
    }
}
