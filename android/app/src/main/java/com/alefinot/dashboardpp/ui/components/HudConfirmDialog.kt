package com.alefinot.dashboardpp.ui.components

import androidx.activity.compose.BackHandler
import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
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
import com.alefinot.dashboardpp.ui.theme.HudColors
import com.alefinot.dashboardpp.ui.theme.HudMotion
import com.alefinot.dashboardpp.ui.theme.HudeBody
import com.alefinot.dashboardpp.ui.theme.hudGlass

/**
 * `#confirm-overlay` + `.confirm-dialog`, copied - the page's own yes/no box,
 * which the app used to fake with a button that turns into a second button.
 *
 * Everything about it comes from the page, including what it does *not* do:
 * - the scrim is `rgba(4,7,13,.75)` (the CSS also blurs what is behind it by 4px,
 *   which is the one part a pre-API-31 Android app cannot do; over a dark page the
 *   translucency carries most of it),
 * - the card is 12px glass, `width: calc(100% - 48px)` up to 440px, 22px padding,
 *   message at 13px/500 with a 1.55 line-height and 16px below it,
 * - the two actions are one grid row of equal width, 10px apart, Confirm first and
 *   Cancel as `.btn-secondary` - exactly the markup's order
 *   (`confirm-yes`, then `confirm-no`),
 * - it enters with `pageFadeIn` at 0.25s and does not animate out,
 * - and tapping the scrim does nothing: `confirmDialog()` binds only the two
 *   buttons and Escape, so the only ways out are Confirm, Cancel, and (here) the
 *   Back button. For a destructive action, that is the part worth copying.
 *
 * It is an overlay in this composition rather than a window-level `Dialog` for the
 * same reason it is a `<div>` in the page: it is part of the screen it belongs to,
 * and it has to be able to use that screen's exact scrim colour. Laying it over
 * the content as the last child also means nothing behind it receives a pointer
 * event, which is what makes it modal.
 */
@Composable
fun HudConfirmDialog(
    message: String,
    onConfirm: () -> Unit,
    onDismiss: () -> Unit,
    confirmLabel: String = "CONFIRM",
    cancelLabel: String = "CANCEL",
    confirmStyle: HudButtonStyle = HudButtonStyle.PRIMARY,
) {
    // Escape cancels, per `confirmDialog()`'s keydown handler.
    BackHandler { onDismiss() }
    Box(
        Modifier
            .fillMaxSize()
            .background(HudColors.Scrim)
            // No dismiss on tap-outside; the tap is consumed so a control behind
            // the scrim cannot be hit by accident.
            .clickable(onClick = {}),
        contentAlignment = Alignment.Center,
    ) {
        HudEnter(
            modifier = Modifier.fillMaxWidth().widthIn(max = 440.dp).padding(horizontal = 24.dp),
            durationMs = HudMotion.SHORT_MS,
            riseDp = 8,
            scaleFrom = 0.98f,
        ) {
            Column(Modifier.hudGlass().padding(22.dp)) {
                Text(message, color = HudColors.TextPrimary, style = HudeBody())
                Spacer(Modifier.height(16.dp))
                Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(10.dp)) {
                    Box(Modifier.weight(1f)) {
                        HudButton(confirmLabel, style = confirmStyle) {
                            onConfirm()
                        }
                    }
                    Box(Modifier.weight(1f)) {
                        HudButton(cancelLabel, style = HudButtonStyle.SECONDARY, onClick = onDismiss)
                    }
                }
            }
        }
    }
}
