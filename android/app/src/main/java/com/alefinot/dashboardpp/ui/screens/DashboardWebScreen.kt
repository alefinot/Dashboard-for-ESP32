package com.alefinot.dashboardpp.ui.screens

import android.app.Activity
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.viewinterop.AndroidView
import com.alefinot.dashboardpp.ui.theme.HudColors
import com.alefinot.dashboardpp.viewmodel.ConnectionViewModel
import com.alefinot.dashboardpp.webview.createDashboardWebView

/**
 * The ESP's own configuration — the Web UI in a WebView, shown on the
 * "ESP SETTINGS" tab (plan §5).
 *
 * This used to carry a ⚙ that opened the *app's* settings as an overlay. That
 * is now a tab of its own, so the icon is gone: one way to reach a screen is
 * better than two, and a labelled tab is harder to miss than a corner glyph.
 */
@Composable
fun DashboardWebScreen(activity: Activity, vm: ConnectionViewModel, ip: String) {
    Box(Modifier.fillMaxSize().background(HudColors.Bg)) {
        AndroidView(
            factory = { ctx -> createDashboardWebView(ctx, activity, ip, vm) },
            modifier = Modifier.fillMaxSize(),
        )
    }
}
