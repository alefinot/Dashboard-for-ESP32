package com.alefinot.dashboardpp.ui.components

import java.util.Locale

/**
 * Shared number and duration formatting for the native screens. Fixed to
 * Locale.US: these strings appear in trip tables and must not change shape with
 * the phone's locale (a comma decimal would also stop matching the CSV, which
 * is deliberately written with dots).
 */
fun hudDuration(ms: Long): String {
    val total = ms / 1000
    return String.format(Locale.US, "%d:%02d:%02d", total / 3600, (total / 60) % 60, total % 60)
}

fun hudBytes(bytes: Long): String = when {
    bytes >= 1_048_576 -> String.format(Locale.US, "%.1f MB", bytes / 1048576.0)
    bytes >= 1024 -> String.format(Locale.US, "%.0f kB", bytes / 1024.0)
    else -> "$bytes B"
}

fun hudNum(value: Double?, places: Int = 1): String =
    if (value == null) "—" else String.format(Locale.US, "%.${places}f", value)

/**
 * Cap a series to at most [maxPoints] while keeping its shape. Each bucket
 * collapses to its maximum, so a spike in a 43k-sample trip still shows instead
 * of being averaged away — a speed chart that hides peaks would be a lie.
 */
fun bucketMax(values: List<Double>, maxPoints: Int): List<Double> {
    if (values.size <= maxPoints || maxPoints < 2) return values
    val out = ArrayList<Double>(maxPoints)
    val bucket = (values.size + maxPoints - 1) / maxPoints
    var i = 0
    while (i < values.size) {
        var best = values[i]
        val end = minOf(i + bucket, values.size)
        for (j in i until end) if (values[j] > best) best = values[j]
        out.add(best)
        i = end
    }
    return out
}

fun bucketMax(values: DoubleArray, maxPoints: Int): DoubleArray {
    if (values.size <= maxPoints || maxPoints < 2) return values
    val out = DoubleArray(maxPoints)
    val bucket = (values.size + maxPoints - 1) / maxPoints
    var outIndex = 0
    var i = 0
    while (i < values.size && outIndex < maxPoints) {
        var best = values[i]
        val end = minOf(i + bucket, values.size)
        for (j in i until end) if (values[j] > best) best = values[j]
        out[outIndex++] = best
        i = end
    }
    return if (outIndex == maxPoints) out else out.copyOf(outIndex)
}
