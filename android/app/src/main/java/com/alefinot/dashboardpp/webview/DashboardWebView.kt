package com.alefinot.dashboardpp.webview

import android.annotation.SuppressLint
import android.app.Activity
import android.content.Context
import android.content.Intent
import android.net.Uri
import android.os.Handler
import android.os.Looper
import android.view.ViewGroup
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
 * Builds the WebView that hosts the ESP32's Web UI, without navigating it (the
 * holder owns navigation, so a retained view can be retried — see
 * [WebUiHolder]). Carries the load timeout (device asleep / powered off) and
 * the file picker used by the backup-import and OTA inputs on the ESP UI.
 * Pull-to-refresh is handled by the WebUI itself (floating arrow gesture); the
 * WebView must not intercept touches or it interrupts the arrow / double-reloads.
 *
 * [onLoadResult] reports each load attempt so the holder knows whether the page
 * is worth keeping or has to be re-requested next time the tab opens.
 */
@SuppressLint("SetJavaScriptEnabled")
private fun buildDashboardWebView(
    context: Context,
    activity: Activity,
    ip: String,
    vm: ConnectionViewModel,
    onLoadResult: (ok: Boolean) -> Unit,
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
        if (!loaded) {
            loaded = true
            onLoadResult(false)
            vm.onWebLost(ip)
        }
    }
    handler.postDelayed(timeoutRunnable, 8000)

    webView.webViewClient = object : WebViewClient() {
        override fun onPageFinished(view: WebView?, url: String?) {
            loaded = true
            handler.removeCallbacks(timeoutRunnable)
            onLoadResult(true)
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
                onLoadResult(false)
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

    return webView
}

/**
 * Holds the ESP Web UI's page for the lifetime of one connection, so switching
 * tabs does not tear it down and build it again.
 *
 * The old code made a fresh WebView on every trip to ESP SETTINGS and fetched
 * the page again: 53 KB of gzipped HTML (0.2-0.45 s on a healthy LAN, more over
 * a weak car link), plus parse, plus Chromium view setup. Measured on a bench
 * connection: 1.55 s between tapping the tab and the page being fully drawn
 * (0.4 s of empty surface, then 1.1 s of the page painting itself in), and the
 * page came back at the top with every panel the user had opened closed again.
 *
 * Here the view is built once and re-parented into the `AndroidView` slot, so
 * entering the tab is a paint instead of a load. That is also the only honest
 * kind of "cache" for this page: the ESP serves `/` with no cache headers at all
 * (`indexGetHandler` sends only Content-Encoding + Content-Length), so an HTTP
 * cache would either store nothing or — with LOAD_CACHE_ELSE_NETWORK — replay a
 * stale `/api/config` / `/api/live` body as if it were current telemetry. Not
 * re-requesting the page is both faster and strictly more correct than caching it.
 *
 * While hidden, the page's own polling is stopped. The WebUI runs
 * `setInterval(liveTick, 1000)`, which would keep hitting the ESP behind the
 * native tabs, and the app deliberately keeps one HTTP lane (plan §4.3). The
 * documented difference matters here: `WebView.onPause()` "does not pause
 * JavaScript" — `pauseTimers()` does, and it is a process-wide request, which is
 * fine because this is the only WebView in the app. Nothing else is paused:
 * `onPause()` costs about a second to undo and buys nothing once the timers are
 * stopped, because a page whose scripts are stopped has nothing left to repaint.
 *
 * The view is also kept in the hierarchy behind the native tabs rather than
 * removed: see the note in ConnectedTabs about why removal costs a second.
 */
class WebUiHolder(
    private val activity: Activity,
    private val ip: String,
    private val vm: ConnectionViewModel,
) {
    private var view: WebView? = null
    private var loadedOk = false
    private var timersPaused = false

    /** The page for this connection: created once, then handed out again. */
    fun obtain(context: Context): WebView {
        val v = view ?: buildDashboardWebView(context, activity, ip, vm) { ok -> loadedOk = ok }
            .also {
                view = it
                it.loadUrl("http://$ip/")
            }
        // Compose threw the old slot away but the View is still attached to it,
        // and a View can only have one parent — detach before handing it out.
        (v.parent as? ViewGroup)?.removeView(v)
        // A page that never loaded (dashboard asleep or switched off when the tab
        // was first opened) gets another chance: keeping the view must not mean
        // keeping the error page.
        if (!loadedOk) v.loadUrl("http://$ip/")
        return v
    }

    /** Tab left the screen: stop the page working behind the native tabs. */
    fun onHide() {
        val v = view ?: return
        if (timersPaused) return
        timersPaused = true
        v.pauseTimers()
    }

    /** Tab back on screen. The page's own 1 s tick refreshes it again. */
    fun onShow() {
        val v = view ?: return
        if (!timersPaused) return
        timersPaused = false
        v.resumeTimers()
    }

    /**
     * Input gate. While the page sits behind another tab, its view is still in
     * the hierarchy at full size, so a tap in the gaps of the tab above could
     * reach a page the user cannot see, and a stray drag would scroll it out
     * from under them. Consuming touches at the view level stops both: the
     * WebView never sees them.
     */
    fun setInteractive(on: Boolean) {
        val v = view ?: return
        v.isClickable = on
        v.isLongClickable = on
        v.setOnTouchListener(if (on) null else { _, _ -> true })
    }

    /** Connection over (or the shell disposed): let Chromium and the page go. */
    fun release() {
        val v = view ?: return
        view = null
        if (timersPaused) {
            timersPaused = false
            v.resumeTimers()
        }
        (v.parent as? ViewGroup)?.removeView(v)
        v.stopLoading()
        v.destroy()
    }
}
