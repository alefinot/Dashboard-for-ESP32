#!/usr/bin/env python3
"""Reproduce the Dashboard++ WebUI live-poll stall on the host.

The device runs the Arduino core's ``WebServer`` (arduino-esp32 3.3.x), which
documents "Supports only one simultaneous client": ``WebServer::handleClient()``
accepts at most one connection per call, only while it has no current client,
the web task calls it once per ~10 ms, ``NetworkServer`` listens with a backlog
of 4, and every response is sent with ``Connection: close`` so the browser must
open a fresh socket for every poll.

The WebUI today fires four simultaneous ``fetch()`` calls every 1000 ms
(``src/webui.html``). When a batch cannot drain inside the tick, the next
batch's connections find a full backlog and are refused - so some cells land on
alternate ticks. That is the "sometimes it updates every 2 seconds" symptom.

This script re-creates those server constraints on the host and measures the
*observed update interval* of two client patterns:

  parallel - what the page does now: 4 simultaneous GETs per tick
  lane     - what the page should do: one request in flight, 900 ms timeout

Run:  python scripts/verify_live_poll.py --serve-ms <ms> --ticks <n>
Exit: 0 when the lane pattern holds 1 Hz (and, at or above OVERFLOW_SERVE_MS,
      the parallel pattern is demonstrably degraded).
"""

import argparse
import http.client
import json
import re
import socket
import statistics
import sys
import threading
import time
from socketserver import TCPServer

# --- The live payload contract (shared with src/web.cpp GET /api/live) --------
# One place defines the combined snapshot: the firmware handler in web.cpp must
# emit exactly these keys, and the WebUI reads exactly these keys.
LIVE_KEYS = ["ambient", "odo", "fuel_raw", "fuel_ohm", "fuel_st", "volts", "temp"]
LIVE_JSON = ('{"ambient":1234,"odo":12345.67,"fuel_raw":2048,"fuel_ohm":123.4,'
             '"fuel_st":3,"volts":12.34,"temp":88.5}')
LIVE_BUDGET_BYTES = 176  # fixed char[] in the firmware handler (rule 14)

# Per-tick endpoints of the current page (src/webui.html fan-out) and the cell
# each one feeds. /api/live will feed all five cells at once.
PARALLEL_ENDPOINTS = ["/api/ambient", "/api/odo", "/api/fuel", "/api/sensors"]

# Above this per-request cost a 4-request batch outlives the 1000 ms tick
# (4 x 250 ms = 1000 ms) and the backlog starts to overflow. Measured on this
# harness at --ticks 10: 300 ms -> 10 misses, worst median interval 1242 ms;
# 400 ms -> 17 misses, worst median 2031 ms (the reported "every 2 seconds").
# Below it (serve-ms 0 is the control) both patterns stay clean.
OVERFLOW_SERVE_MS = 300


def routes():
    """Endpoint -> payload, matching the shapes in src/web.cpp."""
    return {
        "/api/ambient": '{"raw":1234}',
        "/api/odo": '{"km":12345.67}',
        "/api/fuel": '{"raw":2048,"liters":23.4,"pct":46,"ohm":123.4,"st":3}',
        "/api/sensors": '{"v":12.34,"t":88.5}',
        "/api/live": LIVE_JSON,
    }


class SingleClientServer(TCPServer):
    """Imitates arduino-esp32 WebServer: one client at a time, one accept per
    web-task iteration, listen backlog 4, always Connection: close."""

    allow_reuse_address = True
    request_queue_size = 4  # NetworkServer(port, max_clients = 4) -> listen(4)

    def __init__(self, port, serve_ms, loop_ms):
        super().__init__(("127.0.0.1", port), _Handler)
        self.serve_ms = serve_ms
        self.loop_ms = loop_ms
        self.busy = False
        self.served = 0
        self._stop = False
        self._thread = threading.Thread(target=self._loop, daemon=True)

    def start(self):
        self._thread.start()

    def _loop(self):
        # The web task: handleClient() once per loop_ms, and it only accepts
        # when there is no current client connected.
        while not self._stop:
            time.sleep(self.loop_ms / 1000.0)
            if self.busy:
                continue
            try:
                conn, _ = self.socket.accept()
            except OSError:
                continue
            self.busy = True
            threading.Thread(target=self._serve, args=(conn,), daemon=True).start()

    def _serve(self, conn):
        try:
            conn.settimeout(5.0)  # core's HTTP_MAX_DATA_WAIT is 5000 ms
            data = b""
            while b"\r\n\r\n" not in data:
                chunk = conn.recv(512)
                if not chunk:
                    break
                data += chunk
            time.sleep(self.serve_ms / 1000.0)
            first = data.split(b"\r\n", 1)[0].decode(errors="replace")
            path = first.split()[1] if len(first.split()) > 1 else ""
            body = routes().get(path)
            if body is None:
                conn.sendall(b"HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\n"
                             b"Connection: close\r\n\r\n")
            else:
                payload = body.encode()
                conn.sendall(b"HTTP/1.1 200 OK\r\nContent-Type: application/json\r\n"
                             b"Content-Length: " + str(len(payload)).encode() +
                             b"\r\nConnection: close\r\n\r\n" + payload)
                self.served += 1
            conn.close()
        except OSError:
            try:
                conn.close()
            except OSError:
                pass
        finally:
            self.busy = False

    def stop(self):
        self._stop = True
        self.server_close()


class _Handler:  # placeholder: requests are handled inline by SingleClientServer
    pass


def fetch(port, path, timeout=None):
    """One poll. Returns parsed JSON, or None if the request failed."""
    try:
        conn = http.client.HTTPConnection("127.0.0.1", port, timeout=timeout)
        conn.request("GET", path)
        resp = conn.getresponse()
        body = resp.read()
        conn.close()
        if resp.status != 200:
            return None
        return json.loads(body)
    except (OSError, ValueError, http.client.HTTPException):
        return None


class Recorder:
    """Per-cell success timestamps + failure count, in a clock-safe record."""

    def __init__(self):
        self.lock = threading.Lock()
        self.hits = {}     # cell -> [monotonic timestamps]
        self.failures = 0
        self.skips = 0

    def hit(self, cell, when):
        with self.lock:
            self.hits.setdefault(cell, []).append(when)

    def fail(self):
        with self.lock:
            self.failures += 1

    def skip(self):
        with self.lock:
            self.skips += 1

    def stats(self, tick_ms):
        """Worst cell wins: a page where one cell skips is a page that skipped."""
        worst_intervals, medians = [], []
        for cell, stamps in self.hits.items():
            stamps = sorted(stamps)
            intervals = [b - a for a, b in zip(stamps, stamps[1:])]
            if not intervals:
                continue
            medians.append(statistics.median(intervals))
            worst_intervals.append(intervals)
        misses = sum(1 for iv in worst_intervals for d in iv if d > tick_ms * 1.4 / 1000.0)
        median_ms = round(max(statistics.median(iv) for iv in worst_intervals) * 1000) if medians else -1
        return misses, median_ms


def run_parallel(port, ticks, tick_ms, rec):
    """What the page does today: four simultaneous fetches per tick, no timeout,
    no in-flight guard - a slow tick just stacks more sockets on the device."""
    for _ in range(ticks):
        started = time.monotonic()
        for path in PARALLEL_ENDPOINTS:
            def grab(p=path):
                if fetch(port, p) is None:
                    rec.fail()
                else:
                    rec.hit(p, time.monotonic())
            threading.Thread(target=grab, daemon=True).start()
        wait = tick_ms / 1000.0 - (time.monotonic() - started)
        if wait > 0:
            time.sleep(wait)
    time.sleep(4.0)  # let stragglers land, the browser waits too


def run_lane(port, ticks, tick_ms, rec, timeout_s=0.9):
    """What the fixed page does: one request in flight per tick, aborted at
    900 ms so a stalled poll can never hold the device's single client slot
    (the core holds one for HTTP_MAX_DATA_WAIT = 5 s) and can never stack a
    second socket on top of it at the next tick. One combined payload feeds
    every cell, so the five readings always change together."""
    in_flight = threading.Event()
    for _ in range(ticks):
        started = time.monotonic()
        if in_flight.is_set():
            # The lane is still busy: skip this tick rather than queue a second
            # connection. With the 900 ms abort < 1000 ms tick this does not
            # happen on a healthy link, and when it does the miss is counted.
            rec.skip()
            continue
        in_flight.set()

        def grab():
            try:
                if fetch(port, "/api/live", timeout=timeout_s) is None:
                    rec.fail()
                else:
                    rec.hit("/api/live", time.monotonic())
            finally:
                in_flight.clear()

        threading.Thread(target=grab, daemon=True).start()
        wait = tick_ms / 1000.0 - (time.monotonic() - started)
        if wait > 0:
            time.sleep(wait)
    time.sleep(2.0)


def check_live_shape():
    """Task 2 gate: the /api/live contract is seven numeric keys inside budget."""
    problems = []
    try:
        doc = json.loads(LIVE_JSON)
    except ValueError as exc:
        problems.append(f"live JSON does not parse: {exc}")
        doc = {}
    missing = [k for k in LIVE_KEYS if k not in doc]
    extra = [k for k in doc if k not in LIVE_KEYS]
    if missing:
        problems.append("missing keys: " + ",".join(missing))
    if extra:
        problems.append("unexpected keys: " + ",".join(extra))
    for k, v in doc.items():
        if not isinstance(v, (int, float)):
            problems.append(f"{k} is not numeric: {v!r}")
    if len(LIVE_JSON.encode()) > LIVE_BUDGET_BYTES:
        problems.append(f"payload {len(LIVE_JSON.encode())} B exceeds the "
                        f"{LIVE_BUDGET_BYTES} B firmware buffer")
    return problems


WEBUI = "src/webui.html"


def check_webui():
    """Task 4 gate: one 1 Hz live tick, one request in flight, no fan-out."""
    try:
        src = open(WEBUI, encoding="utf-8").read()
    except OSError as exc:
        return [f"cannot read {WEBUI}: {exc}"]
    checks = []

    def ok(name, cond):
        checks.append((name, cond))

    fanout = re.search(r"setInterval\([^)]*(pollAmbient|pollOdo|pollFuel|pollSensors)", src)
    ok("(a) no setInterval drives the four pollers", fanout is None)
    live_tick = re.search(r"setInterval\(\s*(?:\(\)\s*=>\s*)?liveTick\s*,\s*(\d+)", src)
    ok("(a) exactly one setInterval(liveTick, 1000)", live_tick is not None and
       live_tick.group(1) == "1000")
    gone = [n for n in ("pollAmbient", "pollOdo", "pollFuel", "pollSensors")
            if re.search(r"function\s+" + n + r"\b", src)]
    ok("(b) the four poll functions are gone", not gone)
    lane = re.search(r"function\s+laneFetch\b", src)
    abort = re.search(r"setTimeout\([^,]+,\s*(\d+)\)", src)
    ok("(c) laneFetch exists with an in-flight guard", lane is not None and
       re.search(r"laneInFlight|laneAbort", src) is not None)
    ok("(c) its timeout is below the 1000 ms tick", abort is not None and
       int(abort.group(1)) < 1000)
    ok("(d) /api/live is fetched", "'/api/live'" in src or '"/api/live"' in src)
    old = [p for p in ("/api/ambient", "/api/odo", "/api/fuel", "/api/sensors")
           if re.search(r"fetch\(\s*['\"]" + p + r"['\"]", src)]
    ok("(d) no periodic fetch of the four single-value endpoints", not old)
    return [name for name, cond in checks if not cond]


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--serve-ms", type=int, default=0,
                    help="per-request cost the fake server spends (stress: >= %d)"
                         % OVERFLOW_SERVE_MS)
    ap.add_argument("--loop-ms", type=int, default=10, help="web task loop delay")
    ap.add_argument("--tick-ms", type=int, default=1000, help="poll cadence")
    ap.add_argument("--ticks", type=int, default=12)
    ap.add_argument("--port", type=int, default=8137)
    ap.add_argument("--shape-only", action="store_true")
    ap.add_argument("--webui-only", action="store_true")
    args = ap.parse_args()

    if args.shape_only:
        problems = check_live_shape()
        for p in problems:
            print("FAIL shape:", p)
        print("PASS shape" if not problems else "FAIL shape: %d problem(s)" % len(problems))
        return 1 if problems else 0

    if args.webui_only:
        problems = check_webui()
        for p in problems:
            print("FAIL webui:", p)
        print("PASS webui" if not problems else "FAIL webui: %d problem(s)" % len(problems))
        return 1 if problems else 0

    server = SingleClientServer(args.port, args.serve_ms, args.loop_ms)
    server.start()
    try:
        results = {}
        for name, runner in (("parallel", run_parallel), ("lane", run_lane)):
            rec = Recorder()
            server.served = 0  # per-pattern count, not cumulative
            runner(args.port, args.ticks, args.tick_ms, rec)
            misses, median_ms = rec.stats(args.tick_ms)
            results[name] = (misses, median_ms, rec.failures)
            print("pattern=%s ticks=%d misses=%d interval_median=%dms failures=%d "
                  "skips=%d served=%d" % (name, args.ticks, misses, median_ms,
                                          rec.failures, rec.skips, server.served))
    finally:
        server.stop()

    failures = 0
    pm, pmed, pfails = results["parallel"]
    lm, lmed, lfails = results["lane"]
    if args.serve_ms >= OVERFLOW_SERVE_MS:
        if pm < 1:
            print("ASSERT FAIL: parallel pattern was expected to miss ticks at "
                  "serve-ms=%d (harness is not reproducing the bug)" % args.serve_ms)
            failures += 1
    elif args.serve_ms == 0:
        # Benign control: with a cheap device both patterns must be clean, so
        # the harness is not inventing the failure.
        if pm >= 1 or lm >= 1:
            print("ASSERT FAIL: a costless device still missed ticks "
                  "(parallel=%d lane=%d) - the harness is inventing the failure"
                  % (pm, lm))
            failures += 1
    if lm != 0:
        print("ASSERT FAIL: lane pattern missed %d tick(s)" % lm)
        failures += 1
    if not (0 <= lmed <= args.tick_ms + 150):
        print("ASSERT FAIL: lane interval median %dms is not within 150ms of %dms"
              % (lmed, args.tick_ms))
        failures += 1
    if lfails:
        print("ASSERT FAIL: lane pattern had %d failed request(s)" % lfails)
        failures += 1
    print("RESULT: %s" % ("PASS" if failures == 0 else "FAIL (%d)" % failures))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
