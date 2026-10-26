package com.alefinot.dashboardpp.api

/**
 * One `/api/snapshot` reading: the whole vehicle + GNSS picture the ESP publishes
 * once per second. Field names match the JSON keys so the mapping below is
 * obvious next to the firmware's `snapshotGetHandler` in `src/web.cpp`.
 *
 * `hdop` and `alt` are -1 when the fix did not carry them (the firmware prints
 * -1 rather than inventing a value). `fix` is 1 only while the GNSS location is
 * valid, and `lat`/`lon` are only meaningful in that case.
 */
data class Snapshot(
    val version: String,
    /** ESP uptime in seconds — the session identity the trip recorder splits on. */
    val up: Long,
    val spd: Double,
    /** 0 = Hall, 1 = GPS, 2 = fused. */
    val src: Int,
    val avg: Double,
    val max: Double,
    val trpKm: Double,
    val trpL: Double,
    val ikml: Double,
    val akml: Double,
    val odo: Double,
    val fuelL: Double,
    val fuelPct: Int,
    /** Fuel input state: 0 disabled, 1 ok, 2 open circuit, 3 shorted. */
    val fuelSt: Int,
    val volts: Double,
    val temp: Double,
    val amb: Int,
    val sats: Int,
    val hdop: Double,
    val alt: Double,
    val lat: Double,
    val lon: Double,
    val fix: Int,
    /** Acceleration timer state: 0 ready, 1 running, 2 finished. */
    val tmrSt: Int,
    val tmrS: Double,
    val fps: Double,
    val heap: Long,
) {
    val hasFix: Boolean get() = fix == 1

    /** True when a position can be logged as a route point. */
    val hasPosition: Boolean
        get() = hasFix && lat in -90.0..90.0 && lon in -180.0..180.0 &&
            (lat != 0.0 || lon != 0.0)

    companion object {
        /**
         * Parse one snapshot response. Returns null when the body is not a flat
         * JSON object carrying `up` — that is what distinguishes our firmware's
         * `/api/snapshot` from a 404 page or another device's answer, so the
         * app's feature detection never has to compare version strings.
         */
        fun parse(body: String): Snapshot? {
            val obj = FlatJson.parse(body) ?: return null
            if (!obj.containsKey("up") || !obj.containsKey("spd")) return null
            val f = JsonFields(obj)
            return Snapshot(
                version = f.str("ver"),
                up = f.long("up"),
                spd = f.dbl("spd"),
                src = f.int("src"),
                avg = f.dbl("avg"),
                max = f.dbl("max"),
                trpKm = f.dbl("trp_km"),
                trpL = f.dbl("trp_l"),
                ikml = f.dbl("ikml"),
                akml = f.dbl("akml"),
                odo = f.dbl("odo"),
                fuelL = f.dbl("fuel_l"),
                fuelPct = f.int("fuel_pct"),
                fuelSt = f.int("fuel_st"),
                volts = f.dbl("volts"),
                temp = f.dbl("temp"),
                amb = f.int("amb"),
                sats = f.int("sats"),
                hdop = f.dbl("hdop", -1.0),
                alt = f.dbl("alt", -1.0),
                lat = f.dbl("lat"),
                lon = f.dbl("lon"),
                fix = f.int("fix"),
                tmrSt = f.int("tmr_st"),
                tmrS = f.dbl("tmr_s"),
                fps = f.dbl("fps"),
                heap = f.long("heap"),
            )
        }
    }
}

/**
 * The `/api/live` subset the app can still show on firmware older than
 * V1.4.6, which has no `/api/snapshot`. Deliberately a different type from
 * [Snapshot]: nothing downstream may pretend speed or GNSS data exists when the
 * device never sent it.
 */
data class LiveReadings(
    val ambient: Int,
    val odo: Double,
    val fuelRaw: Int,
    val fuelOhm: Double,
    val fuelSt: Int,
    val volts: Double,
    val temp: Double,
) {
    companion object {
        fun parse(body: String): LiveReadings? {
            val obj = FlatJson.parse(body) ?: return null
            if (!obj.containsKey("odo")) return null
            val f = JsonFields(obj)
            return LiveReadings(
                ambient = f.int("ambient"),
                odo = f.dbl("odo"),
                fuelRaw = f.int("fuel_raw"),
                fuelOhm = f.dbl("fuel_ohm"),
                fuelSt = f.int("fuel_st"),
                volts = f.dbl("volts"),
                temp = f.dbl("temp"),
            )
        }
    }
}
