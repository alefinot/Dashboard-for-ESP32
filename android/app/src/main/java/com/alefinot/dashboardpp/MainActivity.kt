package com.alefinot.dashboardpp

import android.content.Intent
import android.net.Uri
import android.os.Bundle
import android.webkit.ValueCallback
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.remember
import com.alefinot.dashboardpp.ui.screens.DashboardRoot
import com.alefinot.dashboardpp.ui.theme.HudTheme
import com.alefinot.dashboardpp.viewmodel.ConnectionViewModel
import com.alefinot.dashboardpp.webview.FileChooser

class MainActivity : ComponentActivity() {
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
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
