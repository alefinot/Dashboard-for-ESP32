package com.alefinot.dashboardpp.ui.screens

import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.interaction.MutableInteractionSource
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.remember
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.unit.dp
import androidx.compose.ui.viewinterop.AndroidView
import com.alefinot.dashboardpp.ui.components.HudButton
import com.alefinot.dashboardpp.ui.components.HudEnter
import com.alefinot.dashboardpp.ui.theme.HudColors
import com.alefinot.dashboardpp.ui.theme.HudMotion
import com.alefinot.dashboardpp.ui.theme.HudeBody
import com.alefinot.dashboardpp.ui.theme.HudeCfgNote
import com.alefinot.dashboardpp.ui.theme.HudeSectionTitle
import com.alefinot.dashboardpp.ui.theme.HudeTable
import com.alefinot.dashboardpp.ui.theme.hudGlass
import com.alefinot.dashboardpp.webview.WebUiHolder
import com.alefinot.dashboardpp.webview.WebUiStage

/**
 * The ESP's own configuration — the Web UI in a WebView, shown on the
 * "ESP SETTINGS" tab (plan §5).
 *
 * The page comes from [WebUiHolder], which keeps it alive between tab switches,
 * so re-entering this tab shows the page as the user left it (no reload, and no
 * second set of traffic for the ESP).
 *
 * This used to carry a ⚙ that opened the *app's* settings as an overlay. That
 * is now a tab of its own, so the icon is gone: one way to reach a screen is
 * better than two, and a labelled tab is harder to miss than a corner glyph.
 */
@Composable
fun DashboardWebScreen(holder: WebUiHolder) {
    Box(Modifier.fillMaxSize().background(HudColors.Bg)) {
        AndroidView(
            factory = { ctx -> holder.obtain(ctx) },
            modifier = Modifier.fillMaxSize(),
        )
    }
}

/**
 * The sheet that stands in front of a page that is not on screen yet.
 *
 * The WebView is kept alive between tabs, so exactly one fetch per connection has
 * nothing drawn behind the tab: the first one. That used to be a black rectangle
 * for a second or two, and on a dashboard that is asleep, forever — which read as
 * a crash rather than as a fetch. This cover says what is happening, in the page's
 * own overlay (`#searchBar`/`.confirm-dialog` family: 12px corners, `--bg-card`
 * fill, hairline, `--card-shadow`), and its RETRY re-issues the fetch instead of
 * making the user leave the tab and come back.
 *
 * It arrives with `pageFadeIn` at the dialog's 0.25 s, the same entrance the page's
 * own overlays use, and it takes taps: the page underneath is not interactive yet.
 */
@Composable
fun WebUiCover(stage: WebUiStage, ip: String, onRetry: () -> Unit) {
    Box(
        Modifier
            .fillMaxSize()
            // The page's ground is flat black, so covering it with black at 94% is
            // indistinguishable from the page and still hides the blank or errored
            // document underneath.
            .background(HudColors.Bg.copy(alpha = 0.94f))
            // Swallow taps without drawing a ripple: the page is not reachable yet,
            // and a press that did nothing would look like the app being slow.
            .clickable(
                interactionSource = remember { MutableInteractionSource() },
                indication = null,
                onClick = {},
            ),
        contentAlignment = Alignment.Center,
    ) {
        HudEnter(durationMs = HudMotion.SHORT_MS, riseDp = 6) {
            Column(
                Modifier
                    .width(400.dp)
                    .hudGlass()
                    .padding(horizontal = 22.dp, vertical = 18.dp),
                horizontalAlignment = Alignment.CenterHorizontally,
            ) {
                when (stage) {
                    WebUiStage.LOADING -> {
                        CircularProgressIndicator(
                            Modifier.size(26.dp),
                            color = HudColors.Cyan,
                            trackColor = HudColors.CyanDim,
                            strokeWidth = 2.dp,
                        )
                        Spacer(Modifier.height(14.dp))
                        Text("Loading the dashboard…", style = HudeBody())
                        Spacer(Modifier.height(6.dp))
                        // `.cfg-live strong` - the endpoint in cyan monospace, so the
                        // address the fetch is going to is on the record.
                        Text("http://$ip/", color = HudColors.Cyan, style = HudeTable())
                        Spacer(Modifier.height(10.dp))
                        Text(
                            "The dashboard serves this page itself. It is fetched once " +
                                "per connection and kept open, so coming back to this tab " +
                                "is a repaint, not another fetch.",
                            color = HudColors.TextDim,
                            style = HudeCfgNote(),
                        )
                    }

                    WebUiStage.FAILED -> {
                        // The page puts a fault in a coloured lead line plus a note
                        // under it (`.cfg-warn` + `.cfg-note`), never in a fill.
                        Text(
                            "NO ANSWER FROM THE DASHBOARD",
                            color = HudColors.Amber,
                            style = HudeSectionTitle(),
                        )
                        Spacer(Modifier.height(8.dp))
                        Text("http://$ip/", color = HudColors.Cyan, style = HudeTable())
                        Spacer(Modifier.height(10.dp))
                        Text(
                            "The page did not load. The dashboard may be asleep, or its " +
                                "address may have changed since the app connected to it.",
                            color = HudColors.TextMuted,
                            style = HudeCfgNote(),
                        )
                        Spacer(Modifier.height(16.dp))
                        HudButton("RETRY", onClick = onRetry)
                        Spacer(Modifier.height(8.dp))
                        Text(
                            "Other tabs keep working while the page is out.",
                            color = HudColors.TextDim,
                            style = HudeCfgNote(),
                        )
                    }

                    WebUiStage.READY -> Unit
                }
            }
        }
    }
}
