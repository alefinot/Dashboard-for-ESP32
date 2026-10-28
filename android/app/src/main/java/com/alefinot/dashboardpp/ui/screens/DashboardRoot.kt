package com.alefinot.dashboardpp.ui.screens

import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.navigationBarsPadding
import androidx.compose.foundation.layout.statusBarsPadding
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.remember
import androidx.compose.ui.Modifier
import com.alefinot.dashboardpp.ui.theme.HudColors
import com.alefinot.dashboardpp.viewmodel.ConnectionViewModel
import com.alefinot.dashboardpp.viewmodel.TripsViewModel

/**
 * The app's one screen, in every state.
 *
 * This used to be a `when` over the connection state, with six branches and three
 * screens of its own: a splash, a full-page scan ring, and the dead ends. Each of them
 * covered the whole window to say one sentence, and what they hid was the part of the app
 * that never needed a dashboard — the trips that are already recorded, and the settings
 * that belong to the phone.
 *
 * There is one screen now, and it opens on the vehicle page: the place where a
 * connection is made, unmade and named. [ShellRouting] decides what the shell *says* for
 * a state; this file only decides that it is always the same shell.
 */
@Composable
fun DashboardRoot(activity: android.app.Activity, vm: ConnectionViewModel) {
    // One TripsViewModel for the whole session: it owns the file index and the
    // summary cache, and outlives tab switches (trip recording plan §5).
    val trips = remember { TripsViewModel(activity.application) }
    LaunchedEffect(Unit) { vm.boot() }
    val s = vm.uiState.value
    // Keep content inside the safe area: with targetSdk 35 edge-to-edge is
    // enforced, so without these insets the HUD chrome draws behind the
    // status bar / nav bar.
    Box(
        Modifier
            .fillMaxSize()
            .background(HudColors.Bg)
            .statusBarsPadding()
            .navigationBarsPadding(),
    ) {
        val view = ShellRouting.view(s)
        AppShell(
            conn = view.conn,
            reason = view.reason,
            connection = vm,
            trips = trips,
        )
    }
}
