package com.alefinot.dashboardpp.trips

import com.alefinot.dashboardpp.api.Snapshot

/**
 * Shared fixtures for the trip tests: one snapshot builder with sensible driving
 * defaults, so each test only spells out the fields it is actually about.
 */
fun snap(
    up: Long,
    spd: Double = 0.0,
    odo: Double = 0.0,
    trpKm: Double = 0.0,
    trpL: Double = 0.0,
    max: Double = spd,
    volts: Double = 12.6,
    temp: Double = 88.0,
    lat: Double = 0.0,
    lon: Double = 0.0,
    fix: Int = 0,
    alt: Double = -1.0,
    hdop: Double = -1.0,
    sats: Int = 0,
): Snapshot = Snapshot(
    version = "1.4.6",
    up = up,
    spd = spd,
    src = 1,
    avg = spd,
    max = max,
    trpKm = trpKm,
    trpL = trpL,
    ikml = 0.0,
    akml = 0.0,
    odo = odo,
    fuelL = trpL,
    fuelPct = 50,
    fuelSt = 1,
    volts = volts,
    temp = temp,
    amb = 21,
    sats = sats,
    hdop = hdop,
    alt = alt,
    lat = lat,
    lon = lon,
    fix = fix,
    tmrSt = 0,
    tmrS = 0.0,
    fps = 28.0,
    heap = 120000L,
)

/** An in-memory [TripWriter]: the recorder's file behaviour without a filesystem. */
class FakeWriter : TripWriter {
    val files = LinkedHashMap<String, StringBuilder>()
    val events = mutableListOf<String>()
    var sealedPaths: MutableSet<String> = mutableSetOf()

    override fun create(path: String) {
        events.add("create:$path")
        files.getOrPut(path) { StringBuilder() }
    }

    override fun reopen(path: String) {
        events.add("reopen:$path")
        files.getOrPut(path) { StringBuilder() }
        sealedPaths.remove(path)
    }

    override fun append(path: String, line: String) {
        events.add("append:${path.substringAfterLast('/')}")
        files.getOrPut(path) { StringBuilder() }.append(line).append('\n')
    }

    override fun seal(path: String) {
        events.add("seal:$path")
        sealedPaths.add(path)
    }

    override fun readDataLines(path: String): List<String> =
        files[path]?.toString()?.lineSequence()?.filter { it.isNotBlank() }?.toList() ?: emptyList()

    override fun exists(path: String): Boolean = files.containsKey(path)

    fun lines(path: String): List<String> = readDataLines(path)

    fun pathAt(index: Int): String = files.keys.toList()[index]
}
