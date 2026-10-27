package com.alefinot.dashboardpp.webview

import android.annotation.SuppressLint
import android.app.Activity
import android.content.Context
import android.content.Intent
import android.net.Uri
import android.os.Handler
import android.os.Looper
import android.view.View
import android.view.ViewGroup
import android.webkit.ValueCallback
import android.webkit.WebChromeClient
import android.webkit.WebResourceError
import android.webkit.WebResourceRequest
import android.webkit.WebView
import android.webkit.WebViewClient
import com.alefinot.dashboardpp.viewmodel.ConnectionViewModel
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.flow.first
import kotlinx.coroutines.withTimeoutOrNull

/**
 * Where the retained page is at. The tab uses it to cover the one thing it cannot
 * show yet - a page that is still being fetched, or one that did not answer -
 * instead of leaving a black rectangle with no explanation.
 */
enum class WebUiStage { LOADING, READY, FAILED }

/** How long a warmed page may take to answer before it is paused anyway. */
private const val WEB_WARM_SETTLE_MS = 8_000L

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
    // Should the page be painting right now? Kept as a field because the View can be
    // created at any moment (the first time the tab is within reach of the pager), and
    // a newly built View defaults to VISIBLE and running.
    private var drawn = true

    private val _stage = MutableStateFlow(WebUiStage.LOADING)

    /**
     * The page's fetch state. The View is kept alive between tabs, so only the very
     * first fetch of a connection has nothing drawn behind the tab: that is the one
     * the shell covers (see `WebUiCover`), and the RETRY it offers replaces the old
     * "open the tab again and hope" path.
     */
    val stage: StateFlow<WebUiStage> = _stage.asStateFlow()

    /** The live View, or null until the tab has been opened for this connection. */
    fun peek(): WebView? = view

    /** Fetch the page again - what the cover's RETRY button calls. */
    fun retry() {
        val v = view ?: return
        loadedOk = false
        _stage.value = WebUiStage.LOADING
        v.loadUrl("http://$ip/")
    }

    /**
     * Build the page and start its fetch before the tab is ever opened, so the first
     * visit is a paint rather than a load. This fetch — 53 KB of gzipped HTML, 0.2-0.45 s
     * on a healthy bench LAN, longer over a weak car link — is the only work left that
     * can still be running underneath the frame that carries the tab switch: the View
     * itself has been kept alive between visits since app 21.
     *
     * It goes through [obtain] rather than a second loading path, so there is one place
     * that decides what a page is and when it may be asked for. Nothing paints: the draw
     * gate is already off while the slot is out of the viewport, and a View with no
     * parent has no surface to paint into in any case.
     */
    fun warm(context: Context) {
        if (view != null) return
        obtain(context)
    }

    /**
     * Stop a warmed page working, once it has an answer worth stopping on.
     *
     * Pausing the timers the instant the fetch starts would pause the fetch with it:
     * `pauseTimers` stops Chromium's layout and timer updates, not only the page's own
     * scripts. So the page is left alone until its stage settles and paused right after.
     * Without this the warmed page would run its `setInterval(liveTick, 1000)` behind the
     * native tabs while the user is parked on Live, and the app deliberately keeps one
     * HTTP lane to the ESP (plan §4.3). The `drawn` check is the race the user can cause:
     * if they open the slot during the wait, the page is on screen and gets to keep its
     * timers, because `onShow` is the tab's own business.
     */
    suspend fun quiesceWhenSettled() {
        withTimeoutOrNull(WEB_WARM_SETTLE_MS) { stage.first { it != WebUiStage.LOADING } }
        if (!drawn) onHide()
    }

    /** The page for this connection: created once, then handed out again. */
    fun obtain(context: Context): WebView {
        val v = view ?: buildDashboardWebView(context, activity, ip, vm) { ok ->
            loadedOk = ok
            _stage.value = if (ok) WebUiStage.READY else WebUiStage.FAILED
        }
            .also {
                view = it
                _stage.value = WebUiStage.LOADING
                it.loadUrl("http://$ip/")
            }
        // Compose threw the old slot away but the View is still attached to it,
        // and a View can only have one parent — detach before handing it out.
        (v.parent as? ViewGroup)?.removeView(v)
        // A page that never loaded (dashboard asleep or switched off when the tab
        // was first opened) gets another chance: keeping the view must not mean
        // keeping the error page.
        if (!loadedOk) v.loadUrl("http://$ip/")
        // A View built while the tab is out of the viewport has to start out hidden:
        // its default is VISIBLE, and one painting page under the native tabs is
        // enough to cost every animation above it.
        applyDrawn(v)
        return v
    }

    /**
     * Paint gate: the page is either inside the pager's viewport or it is not, and
     * either way it stays attached, loaded and holding its surface (that is what
     * makes coming back a repaint). What stops when it is out of the viewport is the
     * drawing - INVISIBLE so the View keeps its size and layout, plus onPause so
     * Chromium stops producing frames rather than only hiding them.
     *
     * Without this the retained page costs the whole app its frame budget: filmed on
     * a SM-T590, an idle screen with the page retained behind the tabs rendered 18
     * frames a second at 73 ms each, the same screen with the page never opened 29
     * at 17 ms. Every animation - the sheet between the tabs, the gauges, the lists -
     * was paying for a page nobody was looking at.
     *
     * The input gate that used to sit next to this one (a touch listener that ate
     * every event) is gone with the design it served: an invisible View takes no
     * touches on Android, and the gestures that do belong to this page are handed to
     * it by the pager's ESP slot, which only feeds a View that is the settled page,
     * loaded, and under the finger - see GestureMirror.
     */
    fun setDrawn(on: Boolean) {
        if (drawn == on) return
        drawn = on
        view?.let { applyDrawn(it) }
    }

    private fun applyDrawn(v: WebView) {
        v.visibility = if (drawn) View.VISIBLE else View.INVISIBLE
        if (drawn) v.onResume() else v.onPause()
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
