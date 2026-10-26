package com.alefinot.dashboardpp.ui.components

import androidx.compose.foundation.Canvas
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.Path
import androidx.compose.ui.graphics.PathEffect
import androidx.compose.ui.graphics.StrokeCap
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.alefinot.dashboardpp.ui.theme.HudColors
import com.alefinot.dashboardpp.ui.theme.HudMonoText
import com.alefinot.dashboardpp.ui.theme.HudeLabel
import com.alefinot.dashboardpp.ui.theme.HudeSmall
import kotlin.math.max
import kotlin.math.min

/**
 * One labelled value in the Live card wall. Same visual language as the ESP's
 * own WebUI cells (label above, mono value), so the native screen reads as the
 * same product rather than a second app.
 */
@Composable
fun StatCell(
    label: String,
    value: String,
    unit: String = "",
    accent: Color = HudColors.Cyan,
    dim: Boolean = false,
    modifier: Modifier = Modifier,
) {
    val fg = if (dim) HudColors.TextDim else HudColors.TextPrimary
    Box(
        modifier
            .border(1.dp, HudColors.CyanDim)
            .background(HudColors.Panel)
            .padding(horizontal = 10.dp, vertical = 8.dp),
    ) {
        Column {
            Text(label, color = HudColors.TextDim, style = HudeSmall)
            Text(
                buildString {
                    append(value)
                    if (unit.isNotEmpty()) {
                        append(' ')
                        append(unit)
                    }
                },
                color = fg,
                style = HudMonoText,
            )
        }
    }
}

/**
 * Sparkline over an indexed series. The caller decides how to decimate (plan
 * §6: cap the polyline near 2000 points), so this stays a pure painter.
 */
@Composable
fun Sparkline(
    values: List<Double>,
    modifier: Modifier = Modifier,
    line: Color = HudColors.Cyan,
    fill: Color? = null,
    strokeWidth: Float = 2f,
    zeroBaseline: Boolean = true,
) {
    Canvas(modifier.fillMaxWidth()) {
        if (values.size < 2) return@Canvas
        val lo = values.min()
        val hi = values.max()
        val span = if (hi - lo < 1e-6) 1.0 else hi - lo
        val w = size.width
        val h = size.height
        val stepX = w / (values.size - 1)
        fun yFor(v: Double): Float =
            (h - ((v - lo) / span) * h * 0.92f - h * 0.04f).toFloat()

        if (zeroBaseline) {
            drawLine(
                color = HudColors.CyanDim.copy(alpha = 0.35f),
                start = Offset(0f, h - 1f),
                end = Offset(w, h - 1f),
                strokeWidth = 1f,
            )
        }
        val path = Path()
        values.forEachIndexed { i, v ->
            val x = i * stepX
            val y = yFor(v)
            if (i == 0) path.moveTo(x, y) else path.lineTo(x, y)
        }
        if (fill != null) {
            val area = Path().apply {
                moveTo(0f, h)
                lineTo(0f, yFor(values.first()))
                values.forEachIndexed { i, v -> if (i > 0) lineTo(i * stepX, yFor(v)) }
                lineTo(w, h)
                close()
            }
            drawPath(
                area,
                brush = Brush.verticalGradient(listOf(fill.copy(alpha = 0.35f), fill.copy(alpha = 0.02f))),
            )
        }
        drawPath(path, color = line, style = Stroke(width = strokeWidth, cap = StrokeCap.Round))
    }
}

/**
 * Radial speedometer, drawn the way the ESP's own gauge reads: a 270° sweep with
 * the number in the middle. The arc is vector-drawn, so it stays crisp at any
 * screen size and costs no bitmaps.
 */
@Composable
fun HudGauge(
    value: Double,
    fullScale: Double,
    label: String,
    unit: String,
    modifier: Modifier = Modifier,
    accent: Color = HudColors.Cyan,
) {
    val clamped = max(0.0, min(value, fullScale))
    val fraction = (clamped / fullScale).toFloat().coerceIn(0f, 1f)
    Box(modifier) {
        Canvas(Modifier.fillMaxSize()) {
            val stroke = size.minDimension * 0.09f
            val radius = size.minDimension / 2f - stroke
            val center = Offset(size.width / 2f, size.height / 2f)
            val start = 135f
            val sweep = 270f
            drawArc(
                color = HudColors.CyanDim.copy(alpha = 0.45f),
                startAngle = start,
                sweepAngle = sweep,
                useCenter = false,
                style = Stroke(width = stroke, cap = StrokeCap.Round),
                size = androidx.compose.ui.geometry.Size(radius * 2f, radius * 2f),
                topLeft = Offset(center.x - radius, center.y - radius),
            )
            drawArc(
                color = accent,
                startAngle = start,
                sweepAngle = sweep * fraction,
                useCenter = false,
                style = Stroke(width = stroke, cap = StrokeCap.Round),
                size = androidx.compose.ui.geometry.Size(radius * 2f, radius * 2f),
                topLeft = Offset(center.x - radius, center.y - radius),
            )
            // Tick at the sweep ends, so an empty gauge still reads as a gauge.
            val tick = PathEffect.dashPathEffect(floatArrayOf(stroke * 0.4f, stroke))
            drawArc(
                color = HudColors.TextDim.copy(alpha = 0.5f),
                startAngle = start,
                sweepAngle = sweep,
                useCenter = false,
                style = Stroke(width = 1.5f, cap = StrokeCap.Butt, pathEffect = tick),
                size = androidx.compose.ui.geometry.Size(radius * 1.62f, radius * 1.62f),
                topLeft = Offset(center.x - radius * 0.81f, center.y - radius * 0.81f),
            )
        }
        Column(
            Modifier.fillMaxSize().padding(12.dp),
            horizontalAlignment = androidx.compose.ui.Alignment.CenterHorizontally,
            verticalArrangement = androidx.compose.foundation.layout.Arrangement.Center,
        ) {
            Text(
                label,
                color = HudColors.TextDim,
                style = HudeLabel,
            )
            Text(
                value.toInt().toString(),
                color = accent,
                style = androidx.compose.ui.text.TextStyle(
                    fontFamily = com.alefinot.dashboardpp.ui.theme.HudeDisplayFamily,
                    fontSize = 44.sp,
                    fontWeight = FontWeight.W700,
                ),
            )
            Text(unit, color = HudColors.TextDim, style = HudeSmall)
        }
    }
}
