package com.alefinot.dashboardpp

import android.Manifest
import android.content.Intent
import android.content.pm.PackageManager
import android.net.Uri
import android.os.Build
import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.activity.result.contract.ActivityResultContracts
import androidx.core.content.ContextCompat
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.remember
import androidx.lifecycle.lifecycleScope
import com.alefinot.dashboardpp.trips.RecorderSettings
import com.alefinot.dashboardpp.ui.screens.DashboardRoot
import com.alefinot.dashboardpp.ui.theme.HudMotion
import com.alefinot.dashboardpp.ui.theme.HudTheme
import com.alefinot.dashboardpp.viewmodel.ConnectionViewModel
import com.alefinot.dashboardpp.webview.FileChooser
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.flow.first
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext

class MainActivity : ComponentActivity() {

    /**
     * POST_NOTIFICATIONS is asked for on Android 13+ because the foreground
     * service that keeps the trip log alive shows an ongoing notification. A
     * foreground service whose notification is blocked gets torn down on many
     * devices, which would silently punch holes into the recording. The grant is
     * not required to record — only to show why recording is running — so the
     * app behaves the same when the user refuses.
     */
    private val notificationPermission = registerForActivityResult(
        ActivityResultContracts.RequestPermission(),
    ) { /* granted or not: the recorder runs either way */ }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        requestNotificationPermissionIfNeeded()
        warmPreferences()
        setContent {
            val vm = remember { ConnectionViewModel(application) }
            DisposableEffect(vm) {
                onDispose { vm.dispose() }
            }
            HudTheme {
                DashboardRoot(this, vm)
            }
        }
    }

    /**
     * The app's switches live in DataStore, which never answers synchronously, and
     * the splash window is the only stretch of idle the app gets before its first
     * frame is on screen. Reading them there - off the main thread, mirrored onto
     * the theme on it - is what lets the first page entrance know the motion switch
     * as it composes, rather than starting to animate and stopping mid-fade. The
     * recording switch matters the same way to the service start in [ConnectedTabs],
     * whose cold path is a DataStore read on the boot critical path otherwise.
     */
    private fun warmPreferences() {
        lifecycleScope.launch(Dispatchers.Main) {
            val settings = RecorderSettings(application)
            withContext(Dispatchers.IO) { settings.warm() }
            HudMotion.setPrefEnabled(settings.motionEnabled.first())
        }
    }

    private fun requestNotificationPermissionIfNeeded() {
        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.TIRAMISU) return
        val granted = ContextCompat.checkSelfPermission(
            this, Manifest.permission.POST_NOTIFICATIONS,
        ) == PackageManager.PERMISSION_GRANTED
        if (!granted) notificationPermission.launch(Manifest.permission.POST_NOTIFICATIONS)
    }

    override fun onActivityResult(requestCode: Int, resultCode: Int, data: Intent?) {
        super.onActivityResult(requestCode, resultCode, data)
        if (requestCode == FileChooser.REQUEST_CODE) {
            val callback = FileChooser.pendingCallback
            FileChooser.pendingCallback = null
            // ACTION_OPEN_DOCUMENT reports a single pick in data.data and several
            // picks in clipData; both have to reach the page or the file input
            // never fires (issue #49). Cancelling must deliver null as well -
            // Chromium will not open another chooser while the old callback is
            // unresolved.
            val clip = data?.clipData
            val uris: Array<Uri>? = when {
                resultCode != RESULT_OK -> null
                clip != null && clip.itemCount > 0 ->
                    (0 until clip.itemCount).map { clip.getItemAt(it).uri }.toTypedArray()
                data?.data != null -> arrayOf(data.data!!)
                else -> null
            }
            callback?.onReceiveValue(uris)
        }
    }
}
