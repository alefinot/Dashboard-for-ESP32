package com.alefinot.dashboardpp.ui.screens

import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.unit.dp
import com.alefinot.dashboardpp.ui.components.HudButton
import com.alefinot.dashboardpp.ui.theme.HudColors
import com.alefinot.dashboardpp.ui.theme.HudeBody
import com.alefinot.dashboardpp.ui.theme.HudeHeading
import com.alefinot.dashboardpp.viewmodel.ConnectionViewModel

@Composable
fun ConnectionLostScreen(vm: ConnectionViewModel, reason: String) {
    Box(Modifier.fillMaxSize().background(HudColors.Bg)) {
        Column(
            Modifier.align(Alignment.Center).padding(32.dp),
            horizontalAlignment = Alignment.CenterHorizontally,
        ) {
            Text(
                "CONNECTION LOST",
                // the state color is the point here, so the h2 keeps its own
                // weight and size and drops the heading's gradient fill.
                color = HudColors.Red,
                style = HudeHeading(),
            )
            Spacer(Modifier.height(16.dp))
            Text(
                reason,
                color = HudColors.TextDim,
                style = HudeBody(),
            )
            Spacer(Modifier.height(32.dp))
            HudButton("SCAN AGAIN") { vm.rescan() }
            Spacer(Modifier.height(8.dp))
            HudButton("ENTER IP MANUALLY") { vm.showManualEntry() }
        }
    }
}
