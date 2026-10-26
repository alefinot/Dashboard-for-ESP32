package com.alefinot.dashboardpp.ui.components

import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import com.alefinot.dashboardpp.ui.theme.HudColors
import com.alefinot.dashboardpp.ui.theme.HudeLabel

@Composable
fun HudButton(label: String, modifier: Modifier = Modifier, onClick: () -> Unit) {
    Box(
        modifier = modifier
            .border(1.dp, HudColors.CyanDim)
            .background(HudColors.Panel)
            .padding(12.dp)
            .clickable(onClick = onClick),
        contentAlignment = Alignment.Center,
    ) {
        Text(
            label.uppercase(),
            color = HudColors.Cyan,
            style = HudeLabel,
        )
    }
}

@Composable
fun HudToggle(on: Boolean, modifier: Modifier = Modifier, onToggle: (Boolean) -> Unit) {
    val accent = if (on) HudColors.Cyan else HudColors.TextDim
    Box(
        modifier = modifier
            .border(1.dp, accent)
            .background(HudColors.Panel)
            .clickable { onToggle(!on) }
            .padding(horizontal = 12.dp, vertical = 8.dp),
        contentAlignment = Alignment.Center,
    ) {
        Text(if (on) "ON" else "OFF", color = accent, style = HudeLabel)
    }
}

@Composable
fun HudPanel(modifier: Modifier = Modifier, content: @Composable () -> Unit) {
    Box(
        modifier = modifier
            .border(1.dp, HudColors.CyanDim)
            .background(HudColors.Panel)
            .padding(16.dp),
    ) {
        content()
    }
}

@Composable
fun ScanProgressRing(progress: Float, modifier: Modifier = Modifier) {
    CircularProgressIndicator(
        progress = { progress },
        modifier = modifier,
        color = HudColors.Cyan,
        strokeWidth = 3.dp,
    )
}
