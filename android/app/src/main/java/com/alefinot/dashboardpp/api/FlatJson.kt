package com.alefinot.dashboardpp.api

/**
 * Minimal reader for the flat JSON objects the ESP answers with on the read-only
 * endpoints (`{"a":1,"b":"x"}`).
 *
 * It exists instead of org.json because the trip code is unit-tested on the JVM:
 * the org.json classes in the mockable android.jar are stubs that throw, so a
 * parser built on them could not be tested. The ESP payloads we read are flat
 * objects of numbers and short strings, which is exactly the grammar handled
 * here — anything nested or malformed returns null and the caller treats the
 * response as unusable.
 */
object FlatJson {

    /** Values come back as raw strings; the caller converts and defaults them. */
    fun parse(text: String): Map<String, String>? {
        val body = text.trim()
        if (body.length < 2 || body.first() != '{' || body.last() != '}') return null
        val out = LinkedHashMap<String, String>()
        var i = 1
        val end = body.length - 1
        while (i < end) {
            // key
            if (body[i] != '"') return null
            val keyEnd = body.indexOf('"', i + 1)
            if (keyEnd < 0 || keyEnd >= end) return null
            val key = body.substring(i + 1, keyEnd)
            i = keyEnd + 1
            while (i < end && body[i] == ' ') i++
            if (i >= end || body[i] != ':') return null
            i++
            while (i < end && body[i] == ' ') i++
            if (i >= end) return null
            val value: String
            if (body[i] == '"') {
                val valueEnd = body.indexOf('"', i + 1)
                if (valueEnd < 0 || valueEnd > end) return null
                value = body.substring(i + 1, valueEnd)
                i = valueEnd + 1
            } else {
                // number / true / false / null — terminated by , or the closing }
                var j = i
                while (j < end && body[j] != ',') j++
                // A nested object or array would introduce a second '{'/'[' here;
                // we do not consume those, so the endpoint must be flat.
                if (body.substring(i, j).any { it == '{' || it == '[' }) return null
                value = body.substring(i, j)
                i = j
            }
            out[key] = value.trim()
            while (i < end && (body[i] == ' ' || body[i] == ',')) i++
        }
        return out
    }
}

/** Typed getters over a parsed object, with defaults for absent keys. */
class JsonFields(private val map: Map<String, String>) {
    fun str(key: String, fallback: String = ""): String = map[key] ?: fallback

    fun dbl(key: String, fallback: Double = 0.0): Double =
        map[key]?.toDoubleOrNull() ?: fallback

    /**
     * Ints arrive as plain integers, but reading them through toDouble keeps the
     * parser honest about a value the firmware might one day print with a
     * fraction (e.g. "1.0") instead of failing the whole sample.
     */
    fun int(key: String, fallback: Int = 0): Int =
        map[key]?.toDoubleOrNull()?.toInt() ?: fallback

    fun long(key: String, fallback: Long = 0L): Long =
        map[key]?.toLongOrNull() ?: map[key]?.toDoubleOrNull()?.toLong() ?: fallback

    fun has(key: String): Boolean = map.containsKey(key)
}
