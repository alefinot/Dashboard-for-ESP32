package com.alefinot.dashboardpp.ui.screens

import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.ui.text.input.KeyboardType
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.unit.dp
import com.alefinot.dashboardpp.ui.components.HudButton
import com.alefinot.dashboardpp.ui.components.HudInput
import com.alefinot.dashboardpp.ui.components.HudInputStyle
import com.alefinot.dashboardpp.ui.theme.HudColors
import com.alefinot.dashboardpp.ui.theme.HudeHeading
import com.alefinot.dashboardpp.ui.theme.HudeCfgNote
import com.alefinot.dashboardpp.ui.theme.HudeSectionTitle
import com.alefinot.dashboardpp.viewmodel.ConnectionViewModel

@Composable
fun ManualEntryScreen(vm: ConnectionViewModel) {
    var ip by remember { mutableStateOf(vm.lastKnownIp.value ?: "") }
    Box(Modifier.fillMaxSize().background(HudColors.Bg)) {
        Column(
            Modifier.align(Alignment.Center).padding(32.dp),
        ) {
            Text(
                "ENTER DEVICE IP",
                // the state color is the point here, so the h2 keeps its own
                // weight and size and drops the heading's gradient fill.
                color = HudColors.Cyan,
                style = HudeHeading(),
            )
            Spacer(Modifier.height(16.dp))
            Text(
                "Phone and dashboard must share a local network: the same\n"
                    + "Wi-Fi, the phone's hotspot, or USB tethering.\n"
                    + "Check the IP in your network's device list,\n"
                    + "or scan again when it's powered and awake.",
                color = HudColors.TextDim,
                style = HudeCfgNote(),
            )
            Spacer(Modifier.height(16.dp))
            // `HudInput` is the page's field recipe: 6px corners, `--bg-input` fill,
            // 6x10 padding, 12px text, `#182030` and a cyan edge once it has focus.
            HudInput(
                value = ip,
                onValueChange = { ip = it },
                modifier = Modifier.fillMaxWidth(),
                style = HudInputStyle.FIELD,
                placeholder = "192.168.1.10",
                keyboardType = KeyboardType.Number,
                onDone = { vm.submitManualIp(ip) },
            )
            if (vm.manualError.value != null) {
                Spacer(Modifier.height(12.dp))
                Text(
                    // `.cfg-warn`: the page's "you must not miss this" line, in
                    // `--accent-rose` at weight 700.
                    vm.manualError.value ?: "",
                    color = HudColors.Red,
                    style = HudeSectionTitle(0f),
                )
            }
            Spacer(Modifier.height(24.dp))
            HudButton("CONNECT") { vm.submitManualIp(ip) }
            Spacer(Modifier.height(8.dp))
            HudButton("BACK TO SCAN") { vm.rescan() }
        }
    }
}
