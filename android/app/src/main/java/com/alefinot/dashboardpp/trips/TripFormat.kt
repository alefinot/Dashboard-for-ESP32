package com.alefinot.dashboardpp.trips

import com.alefinot.dashboardpp.api.Snapshot
import java.util.Locale

/**
 * One logged line of a trip CSV: the phone's receive time plus the ESP's own
 * sample. The ESP clock is often unset (`timeValid` false, no NTP on the config
 * portal), so the phone clock is the only wall time we can trust — and `up_s`
 * keeps every line tied to the device's own timeline, which is what the session
 * logic and the reboot detector work on.
 */
data class TripSample(val tsMs: Long, val snapshot: Snapshot)

/**
 * Who a trip file belongs to, as written in its first line. The id is the app's
 * own stable key (a rename does not move it), the MAC is what the dashboard
 * reported when the drive started, and the name is a copy of what the card said
 * at that moment - kept so a deleted vehicle still has a readable history, while
 * the current name is always looked up live from the id.
 *
 * A tag with no id does not exist: a file either says who it belongs to or it is
 * an untagged trip, which the Trips list reports as unknown rather than guessing.
 */
data class TripTag(val vehicleId: String, val eff: String, val name: String) {
    /** The part after `#vehicle=` - what the summary cache stores alongside the file. */
    fun raw(): String = "$vehicleId|$eff|$name"

    companion object {
        fun fromRaw(raw: String): TripTag? {
            val p = raw.split("|", limit = 3)
            if (p.size != 3 || p[0].isBlank()) return null
            return TripTag(p[0], p[1], p[2])
        }
    }
}

/**
 * CSV shape of a trip log (trip recording plan §4). Header first, then one line
 * per sample in a fixed 25-column order, oldest first. The file is append-only
 * and always valid on disk: a process kill costs only the samples that were
 * never polled, never the file.
 */
object TripFormat {
    val COLUMNS = listOf(
        "ts_ms", "up_s", "spd", "src", "avg", "max", "trp_km", "trp_l", "ikml",
        "akml", "odo", "fuel_l", "fuel_pct", "fuel_st", "volts", "temp", "amb",
        "sats", "hdop", "alt", "lat", "lon", "fix", "tmr_st", "tmr_s",
    )

    const val HEADER = "ts_ms,up_s,spd,src,avg,max,trp_km,trp_l,ikml,akml,odo," +
        "fuel_l,fuel_pct,fuel_st,volts,temp,amb,sats,hdop,alt,lat,lon,fix,tmr_st,tmr_s"

    /**
     * The vehicle tag line, written above the header. A `#` line is a comment: the
     * 25-column data rows are a fixed-format table that `parseLine` demands at its
     * exact width, so the only way to add anything without breaking every file
     * already on disk is a line that is not a data line [plan §6].
     *
     * The three fields never contain `|`, `;`, CR or LF: `sanitizeField` replaces
     * them. That is not paranoia about the vehicle name - the name is bounded where
     * it is typed - it is what lets the same raw string sit inside the summary
     * cache's `;`-separated row without a second escaping rule.
     */
    fun tagLine(vehicleId: String, eff: String, name: String): String {
        val id = sanitizeField(vehicleId)
        if (id.isEmpty()) return ""
        return TAG_PREFIX + id + "|" + sanitizeField(eff.ifBlank { UNKNOWN_EFF }) +
            "|" + sanitizeField(name)
    }

    /** The tag of a file, or null when the line is not one (or is malformed). */
    fun parseTag(line: String): TripTag? {
        if (!line.startsWith(TAG_PREFIX)) return null
        return TripTag.fromRaw(line.removePrefix(TAG_PREFIX))
    }

    /** Any line the CSV reader must skip rather than try to parse. */
    fun isComment(line: String): Boolean = line.startsWith("#")

    const val TAG_PREFIX = "#vehicle="

    /** What a tag writes for a dashboard that never reported its MAC. */
    const val UNKNOWN_EFF = "unknown"

    /** Field-safe form of a tag value. Never returns an empty string for non-blank input. */
    private fun sanitizeField(value: String): String = buildString(value.length) {
        for (c in value) {
            append(
                when (c) {
                    '|', ';', '\r', '\n', '\t' -> ' '
                    else -> c
                },
            )
        }
    }.trim()

    fun line(sample: TripSample): String {
        val s = sample.snapshot
        // Doubles use Double.toString: locale-independent ('.' always), so the
        // CSV stays parseable no matter what locale the phone is set to.
        return buildString(128) {
            append(sample.tsMs); append(',')
            append(s.up); append(',')
            append(s.spd); append(',')
            append(s.src); append(',')
            append(s.avg); append(',')
            append(s.max); append(',')
            append(s.trpKm); append(',')
            append(s.trpL); append(',')
            append(s.ikml); append(',')
            append(s.akml); append(',')
            append(s.odo); append(',')
            append(s.fuelL); append(',')
            append(s.fuelPct); append(',')
            append(s.fuelSt); append(',')
            append(s.volts); append(',')
            append(s.temp); append(',')
            append(s.amb); append(',')
            append(s.sats); append(',')
            append(s.hdop); append(',')
            append(s.alt); append(',')
            append(s.lat); append(',')
            append(s.lon); append(',')
            append(s.fix); append(',')
            append(s.tmrSt); append(',')
            append(s.tmrS)
        }
    }

    /**
     * Parse one data line. Null on a malformed line — including the truncated
     * last line a crash mid-write can leave behind, which must not cost the rest
     * of the trip.
     */
    fun parseLine(line: String): TripSample? {
        val p = line.split(',')
        if (p.size != COLUMNS.size) return null
        val up = p[1].toLongOrNull() ?: return null
        val ts = p[0].toLongOrNull() ?: return null
        val s = Snapshot(
            version = "",
            up = up,
            spd = p[2].toDoubleOrNull() ?: return null,
            src = p[3].toIntOrNull() ?: return null,
            avg = p[4].toDoubleOrNull() ?: return null,
            max = p[5].toDoubleOrNull() ?: return null,
            trpKm = p[6].toDoubleOrNull() ?: return null,
            trpL = p[7].toDoubleOrNull() ?: return null,
            ikml = p[8].toDoubleOrNull() ?: return null,
            akml = p[9].toDoubleOrNull() ?: return null,
            odo = p[10].toDoubleOrNull() ?: return null,
            fuelL = p[11].toDoubleOrNull() ?: return null,
            fuelPct = p[12].toIntOrNull() ?: return null,
            fuelSt = p[13].toIntOrNull() ?: return null,
            volts = p[14].toDoubleOrNull() ?: return null,
            temp = p[15].toDoubleOrNull() ?: return null,
            amb = p[16].toIntOrNull() ?: return null,
            sats = p[17].toIntOrNull() ?: return null,
            hdop = p[18].toDoubleOrNull() ?: return null,
            alt = p[19].toDoubleOrNull() ?: return null,
            lat = p[20].toDoubleOrNull() ?: return null,
            lon = p[21].toDoubleOrNull() ?: return null,
            fix = p[22].toIntOrNull() ?: return null,
            tmrSt = p[23].toIntOrNull() ?: return null,
            tmrS = p[24].toDoubleOrNull() ?: return null,
            fps = 0.0,
            heap = 0L,
        )
        return TripSample(ts, s)
    }

    fun isHeader(line: String): Boolean = line.startsWith("ts_ms,up_s")

    /**
     * True for anything a trip file reader should not hand to `parseLine`: blank,
     * the header, and the tag line (and any comment a user might add by hand).
     * One place, because the same three skips appear in every reader loop, and a
     * loop that forgets one does not crash - it counts a comment as a sample.
     */
    fun skippable(line: String): Boolean =
        line.isBlank() || isHeader(line) || isComment(line)

    /**
     * The lines a new trip file starts with: the vehicle's tag when there is one,
     * then the header. Kept as a pure list so the shape of a file's head - which
     * every reader of a trip file depends on, and which the Trips page and the
     * export both lean on - can be stated in a test without an Android [Context]
     * to open a file.
     */
    fun fileHead(tag: String): List<String> =
        if (tag.isBlank()) listOf(HEADER) else listOf(tag, HEADER)
}

/**
 * A trip's headline numbers, derived from its samples. One accumulator feeds
 * both the live "Recording…" row (the recorder keeps it in RAM) and the sealed
 * file read by the Trips list, so the two can never disagree about a formula.
 */
data class TripSummary(
    val startMs: Long,
    val endMs: Long,
    val samples: Int,
    val durationMs: Long,
    /** Distance from the ESP odometer delta — the primary figure (plan §4). */
    val distanceKm: Double,
    /** The ESP's own trip odometer at the end of the trip, for context. */
    val espTripKm: Double,
    /** Length of the logged GNSS track, as a sanity check on the odometer. */
    val gnssKm: Double,
    val maxSpeed: Double,
    val avgSpeed: Double,
    val fuelUsedL: Double,
    val economyKml: Double,
    val minVolts: Double,
    val maxVolts: Double,
    val minTemp: Double,
    val maxTemp: Double,
    val fixPct: Int,
    val gaps: Int,
    val gapSeconds: Long,
)

/** Streaming accumulator: feed samples in order, ask for [summary] at the end. */
class TripAccumulator(private val gapMs: Long = TripPolicy.GAP_MS) {
    private var count = 0
    private var startMs = 0L
    private var endMs = 0L
    private var firstOdo = Double.NaN
    private var lastOdo = Double.NaN
    private var firstTripFuel = Double.NaN
    private var lastTripFuel = Double.NaN
    private var lastTripKm = 0.0
    private var maxSpeed = 0.0
    private var speedSum = 0.0
    private var minVolts = Double.NaN
    private var maxVolts = Double.NaN
    private var minTemp = Double.NaN
    private var maxTemp = Double.NaN
    private var fixSamples = 0
    private var lastLat = Double.NaN
    private var lastLon = Double.NaN
    private var gnssM = 0.0
    private var prevTs = 0L
    private var gaps = 0
    private var gapMsTotal = 0L

    fun add(sample: TripSample) {
        val s = sample.snapshot
        count++
        if (count == 1) {
            startMs = sample.tsMs
            firstOdo = s.odo
            firstTripFuel = s.trpL
        }
        endMs = sample.tsMs

        if (prevTs != 0L) {
            val dt = sample.tsMs - prevTs
            if (dt > gapMs) {
                gaps++
                gapMsTotal += dt
            }
        }
        prevTs = sample.tsMs

        lastOdo = s.odo
        lastTripFuel = s.trpL
        lastTripKm = s.trpKm
        if (s.spd > maxSpeed) maxSpeed = s.spd
        speedSum += s.spd
        // 0 means "no reading" for coolant and battery, not a plausible 0 °C or
        // 0 V, so those samples must not drag the min down.
        if (s.volts > 0.0) {
            if (minVolts.isNaN() || s.volts < minVolts) minVolts = s.volts
            if (maxVolts.isNaN() || s.volts > maxVolts) maxVolts = s.volts
        }
        if (s.temp > 0.0) {
            if (minTemp.isNaN() || s.temp < minTemp) minTemp = s.temp
            if (maxTemp.isNaN() || s.temp > maxTemp) maxTemp = s.temp
        }
        if (s.hasPosition) {
            fixSamples++
            if (!lastLat.isNaN()) {
                gnssM += haversineM(lastLat, lastLon, s.lat, s.lon)
            }
            lastLat = s.lat
            lastLon = s.lon
        }
    }

    fun summary(): TripSummary? {
        if (count == 0) return null
        val odoDelta = if (!firstOdo.isNaN() && !lastOdo.isNaN() && lastOdo >= firstOdo) {
            lastOdo - firstOdo
        } else 0.0
        val fuelDelta =
            if (!firstTripFuel.isNaN() && !lastTripFuel.isNaN() && lastTripFuel >= firstTripFuel) {
                lastTripFuel - firstTripFuel
            } else 0.0
        return TripSummary(
            startMs = startMs,
            endMs = endMs,
            samples = count,
            durationMs = endMs - startMs,
            distanceKm = odoDelta,
            espTripKm = lastTripKm,
            gnssKm = gnssM / 1000.0,
            maxSpeed = maxSpeed,
            avgSpeed = speedSum / count,
            fuelUsedL = fuelDelta,
            economyKml = if (fuelDelta > 0.01 && odoDelta > 0.05) odoDelta / fuelDelta else 0.0,
            minVolts = if (minVolts.isNaN()) 0.0 else minVolts,
            maxVolts = if (maxVolts.isNaN()) 0.0 else maxVolts,
            minTemp = if (minTemp.isNaN()) 0.0 else minTemp,
            maxTemp = if (maxTemp.isNaN()) 0.0 else maxTemp,
            fixPct = if (count == 0) 0 else fixSamples * 100 / count,
            gaps = gaps,
            gapSeconds = gapMsTotal / 1000,
        )
    }

    companion object {
        /** Great-circle distance in metres, good enough for a track sanity check. */
        fun haversineM(lat1: Double, lon1: Double, lat2: Double, lon2: Double): Double {
            val r = 6371000.0
            val dLat = Math.toRadians(lat2 - lat1)
            val dLon = Math.toRadians(lon2 - lon1)
            val a = Math.sin(dLat / 2) * Math.sin(dLat / 2) +
                Math.cos(Math.toRadians(lat1)) * Math.cos(Math.toRadians(lat2)) *
                Math.sin(dLon / 2) * Math.sin(dLon / 2)
            return 2 * r * Math.atan2(Math.sqrt(a), Math.sqrt(1 - a))
        }
    }
}

/** Constants that decide when a trip is over. See trip recording plan §3. */
object TripPolicy {
    /** No successful snapshot for this long means the ESP went away. */
    const val SEAL_AFTER_MS = 120_000L

    /** A session that reappears inside this window continues its trip file. */
    const val REOPEN_WINDOW_MS = 24L * 3600_000L

    /** One power session is split after this long (parked with ignition on). */
    const val MAX_TRIP_MS = 12L * 3600_000L

    /**
     * Slack when matching a returning session's uptime to the phone's elapsed
     * time. Both clocks tick at roughly 1 s/s; this absorbs phone sleep, NTP
     * corrections on the ESP and a poll that lands late.
     */
    const val UP_SLACK_MS = 120_000L

    /** Sample gap that counts as a hole in the log rather than a missed poll. */
    const val GAP_MS = 10_000L

    /** After a trip seals, keep retrying this long before the service gives up. */
    const val IDLE_RETRY_MS = 15L * 60_000L
}

/** Compact text form of a summary for the on-disk index cache. */
object TripSummaryCodec {
    private const val V = "v1"

    fun encode(s: TripSummary): String = listOf(
        V, s.startMs, s.endMs, s.samples, s.durationMs,
        d(s.distanceKm), d(s.espTripKm), d(s.gnssKm), d(s.maxSpeed), d(s.avgSpeed),
        d(s.fuelUsedL), d(s.economyKml), d(s.minVolts), d(s.maxVolts),
        d(s.minTemp), d(s.maxTemp), s.fixPct, s.gaps, s.gapSeconds,
    ).joinToString(";")

    private fun d(v: Double) = String.format(Locale.US, "%.3f", v)

    fun decode(text: String): TripSummary? {
        val p = text.split(";")
        if (p.size != 19 || p[0] != V) return null
        return try {
            TripSummary(
                startMs = p[1].toLong(),
                endMs = p[2].toLong(),
                samples = p[3].toInt(),
                durationMs = p[4].toLong(),
                distanceKm = p[5].toDouble(),
                espTripKm = p[6].toDouble(),
                gnssKm = p[7].toDouble(),
                maxSpeed = p[8].toDouble(),
                avgSpeed = p[9].toDouble(),
                fuelUsedL = p[10].toDouble(),
                economyKml = p[11].toDouble(),
                minVolts = p[12].toDouble(),
                maxVolts = p[13].toDouble(),
                minTemp = p[14].toDouble(),
                maxTemp = p[15].toDouble(),
                fixPct = p[16].toInt(),
                gaps = p[17].toInt(),
                gapSeconds = p[18].toLong(),
            )
        } catch (e: Exception) {
            null
        }
    }
}
