package com.alefinot.dashboardpp.ui.screens

import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.widthIn
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.unit.dp
import com.alefinot.dashboardpp.ui.components.HudButton
import com.alefinot.dashboardpp.ui.components.HudButtonStyle
import com.alefinot.dashboardpp.ui.components.HudEnter
import com.alefinot.dashboardpp.ui.theme.HudBrushes
import com.alefinot.dashboardpp.ui.theme.HudColors
import com.alefinot.dashboardpp.ui.theme.HudeBody
import com.alefinot.dashboardpp.ui.theme.HudeCfgLive
import com.alefinot.dashboardpp.ui.theme.HudeCfgNote
import com.alefinot.dashboardpp.ui.theme.HudeHeading
import com.alefinot.dashboardpp.ui.theme.hudGlass

/**
 * What a page says when the thing it shows needs a device that is not there.
 *
 * This is the whole of the offline treatment for Live and the ESP slot: the page
 * stays, the bar stays, the swipe order stays, and the page answers instead of
 * disappearing. It borrows the confirmation box's furniture (`hudGlass`, the same
 * width cap, the same scrim-free centred column) because that is the one panel in
 * this app that already knows how to sit over a page and be read.
 *
 * It is deliberately not a screen: no route, no back handler, no state of its own.
 * It is drawn by the page, from the reason the view model supplied, and it goes away
 * by itself the moment a connection comes back.
 */
@Composable
fun OfflineCover(
    title: String,
    reason: String?,
    note: String,
    rememberedIp: String?,
    modifier: Modifier = Modifier,
    findLabel: String = "FIND DEVICE",
    onFind: () -> Unit,
    onEnterIp: () -> Unit,
    onWifi: (() -> Unit)? = null,
) {
    // Opaque on purpose. The ESP slot is a hole in the app rather than a page, so on
    // that tab the cover has to carry the background the WebView would have painted;
    // leaving it translucent would put the bar's glow through the middle of the
    // message. (Measured: a frame with this area at zero luminance is the one frame
    // the design rules out.)
    Box(
        modifier
            .fillMaxSize()
            .background(HudColors.Bg),
        contentAlignment = Alignment.Center,
    ) {
        HudEnter(
            Modifier
                .fillMaxWidth()
                .widthIn(max = 460.dp)
                .padding(horizontal = 24.dp),
        ) {
            Column(Modifier.hudGlass().padding(22.dp)) {
                Text(title, style = HudeHeading().copy(brush = HudBrushes.title))
                Spacer(Modifier.height(12.dp))
                if (!reason.isNullOrBlank()) {
                    Text(reason, color = HudColors.TextMuted, style = HudeBody())
                    Spacer(Modifier.height(10.dp))
                }
                if (!rememberedIp.isNullOrBlank()) {
                    Text(
                        "Last device: $rememberedIp",
                        color = HudColors.Cyan,
                        style = HudeCfgLive(),
                    )
                    Spacer(Modifier.height(10.dp))
                }
                Text(note, color = HudColors.TextDim, style = HudeCfgNote())
                Spacer(Modifier.height(18.dp))
                HudButton(findLabel, style = HudButtonStyle.PRIMARY, onClick = onFind)
                Spacer(Modifier.height(8.dp))
                HudButton("ENTER IP", style = HudButtonStyle.SECONDARY, onClick = onEnterIp)
                if (onWifi != null) {
                    Spacer(Modifier.height(8.dp))
                    HudButton("WI-FI SETTINGS", style = HudButtonStyle.SECONDARY, onClick = onWifi)
                }
            }
        }
    }
}
