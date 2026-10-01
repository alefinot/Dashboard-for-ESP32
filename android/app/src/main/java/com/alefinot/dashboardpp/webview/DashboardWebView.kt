package com.alefinot.dashboardpp.webview

import android.annotation.SuppressLint
import android.app.Activity
import android.content.Context
import android.content.Intent
import android.net.Uri
import android.os.Handler
import android.os.Looper
import android.webkit.ValueCallback
import android.webkit.WebChromeClient
import android.webkit.WebResourceError
import android.webkit.WebResourceRequest
import android.webkit.WebView
import android.webkit.WebViewClient
import com.alefinot.dashboardpp.viewmodel.ConnectionViewModel

object FileChooser {
    const val REQUEST_CODE = 42
    var pendingCallback: ValueCallback<Array<Uri>>? = null
}

/**
 * Build the document picker the page asked for. The type used to be pinned to
 * "application/json", so the OTA input (`accept=".bin"`) opened a chooser with
 * nothing selectable in it - the reported "cannot select the firmware file"
 * (issue #49). The page's own accept list drives the filter; when it carries no
 * usable MIME type (an extension WebView cannot map, e.g. .bin) the picker stays
 * unrestricted rather than showing an empty list.
 */
internal fun fileChooserIntent(params: WebChromeClient.FileChooserParams?): Intent {
    val accept = params?.acceptTypes?.filter { it.contains('/') }?.toTypedArray() ?: emptyArray()
    return Intent(Intent.ACTION_OPEN_DOCUMENT).apply {
        addCategory(Intent.CATEGORY_OPENABLE)
        addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION)
        if (params?.mode == WebChromeClient.FileChooserParams.MODE_OPEN_MULTIPLE) {
            putExtra(Intent.EXTRA_ALLOW_MULTIPLE, true)
        }
        type = "*/*"
        if (accept.isNotEmpty()) putExtra(Intent.EXTRA_MIME_TYPES, accept)
    }
}

/**
 * Creates the WebView that hosts the ESP32's Web UI, with a load timeout
 * (device asleep / powered off) and the file picker used by the
 * backup-import input on the ESP UI. Pull-to-refresh is handled by the
 * WebUI itself (floating arrow gesture); the WebView must not intercept
 * touches or it interrupts the arrow / double-reloads.
 */
@SuppressLint("SetJavaScriptEnabled")
fun createDashboardWebView(
    context: Context,
    activity: Activity,
    ip: String,
    vm: ConnectionViewModel,
): WebView {
    val webView = WebView(context)
    val settings = webView.settings
    settings.javaScriptEnabled = true
    settings.domStorageEnabled = true
    // Honour the page's own <meta name="viewport">. WebView's default is off -
    // unlike a real browser - so the Web UI was laid out at the default 980 CSS
    // px and scaled to fit, which also mis-anchors body-level overlays such as
    // the dropdown list (issue #50).
    settings.useWideViewPort = true
    settings.loadWithOverviewMode = false
    webView.setBackgroundColor(android.graphics.Color.parseColor("#05080D"))

    val handler = Handler(Looper.getMainLooper())
    var loaded = false
    val timeoutRunnable = Runnable {
        if (!loaded) vm.onWebLost(ip)
    }
    handler.postDelayed(timeoutRunnable, 8000)

    webView.webViewClient = object : WebViewClient() {
        override fun onPageFinished(view: WebView?, url: String?) {
            loaded = true
            handler.removeCallbacks(timeoutRunnable)
        }

        override fun onReceivedError(
            view: WebView?,
            request: WebResourceRequest?,
            error: WebResourceError?
        ) {
            val host = request?.url?.host ?: return
            if (host == ip) {
                loaded = true
                handler.removeCallbacks(timeoutRunnable)
                vm.onWebLost(ip)
            }
        }
    }

    webView.setWebChromeClient(object : WebChromeClient() {
        override fun onShowFileChooser(
            view: WebView?,
            filePathCallback: ValueCallback<Array<Uri>>?,
            fileChooserParams: FileChooserParams?
        ): Boolean {
            // A chooser whose callback was never resolved makes Chromium refuse to
            // open the next one, so one abandoned attempt used to kill every file
            // input on the page - OTA upload and backup import alike (issue #49).
            FileChooser.pendingCallback?.onReceiveValue(null)
            FileChooser.pendingCallback = filePathCallback
            return try {
                activity.startActivityForResult(
                    fileChooserIntent(fileChooserParams),
                    FileChooser.REQUEST_CODE
                )
                true
            } catch (e: Exception) {
                // Nothing to open the picker with: hand the page an empty result
                // now instead of leaving the callback hanging.
                FileChooser.pendingCallback = null
                filePathCallback?.onReceiveValue(null)
                false
            }
        }
    })

    // Pull-to-refresh lives in the WebUI itself (floating arrow + reload);
    // keep the WebView's touch handling untouched so the arrow gesture
    // isn't interrupted or double-reloaded.

    webView.loadUrl("http://$ip/")
    return webView
}
