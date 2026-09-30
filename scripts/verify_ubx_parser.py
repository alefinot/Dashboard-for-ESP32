#!/usr/bin/env python3
"""Host replica of the UBX frame parser in src/sensors.cpp (ubxParseByte).

Why this exists: the parser is a byte-at-a-time state machine that only runs on
the device, so a framing bug shows up as a mysterious CKFAIL counter in the log
and nothing else. This replica checks the state machine against hand-built
frames - including the two shapes that used to break it:

  * a zero-length payload, which used to be entered as a payload state with
    ubxNeed == 0, so CK_A/CK_B were consumed as payload and folded into the
    accumulators (issue #33);
  * a poisoned checksum, which must drop exactly one frame and still resync on
    the next 0xB5.

Run:  python scripts/verify_ubx_parser.py        (exit 0 = all checks pass)
"""

import sys

PAYLOAD_MAX = 92


def fletcher(cls, mid, payload):
    """The UBX Fletcher-8 checksum over class, id, length and payload."""
    ck_a = ck_b = 0
    for b in [cls, mid, len(payload) & 0xFF, (len(payload) >> 8) & 0xFF] + list(payload):
        ck_a = (ck_a + b) & 0xFF
        ck_b = (ck_b + ck_a) & 0xFF
    return ck_a, ck_b


def build(cls, mid, payload):
    payload = bytes(payload)
    ck_a, ck_b = fletcher(cls, mid, payload)
    return (b"\xb5\x62" + bytes([cls, mid, len(payload) & 0xFF,
                                (len(payload) >> 8) & 0xFF]) + payload +
            bytes([ck_a, ck_b]))


class Parser:
    """Mirror of ubxParseByte() / the counters it keeps.

    zero_len_to_ck=False reproduces the pre-#33 behaviour (always enter the
    payload state), True is the fixed transition.
    """

    def __init__(self, zero_len_to_ck=True):
        self.st = 0
        self.cls = self.mid = 0
        self.need = 0
        self.idx = 0
        self.ck_a = self.ck_b = 0
        self.pld = bytearray(PAYLOAD_MAX)
        self.sync_seen = 0
        self.ck_fail = 0
        self.oversize = 0
        self.accepted = []          # (cls, id, payload) for every accepted frame
        self.zero_len_to_ck = zero_len_to_ck

    def feed(self, b):
        st = self.st
        if st == 0:
            if b == 0xB5:
                self.sync_seen += 1
                self.st = 1
            return
        if st == 1:
            self.st = 2 if b == 0x62 else 0
            return
        if st == 2:
            self.cls = b
            self.st = 3
            return
        if st == 3:
            self.mid = b
            self.st = 4
            return
        if st == 4:
            self.need = b
            self.st = 5
            return
        if st == 5:
            self.need |= b << 8
            if self.need > PAYLOAD_MAX:
                self.oversize += 1
                self.st = 0
                return
            self.idx = 0
            # Seed both accumulators by stepping the algorithm through each
            # header byte individually (issue #6 - seeding ckB = ckA after one
            # combined sum made every valid frame fail).
            ck = self.cls
            self.ck_b = ck
            ck += self.mid
            self.ck_b += ck
            ck += self.need & 0xFF
            self.ck_b += ck
            ck += b
            self.ck_b += ck
            self.ck_a = ck
            self.ck_a &= 0xFF
            self.ck_b &= 0xFF
            if self.zero_len_to_ck and self.need == 0:
                self.st = 7          # fixed path: straight to CK_A
            else:
                self.st = 6
            return
        if st == 6:
            self.pld[self.idx] = b
            self.idx += 1
            self.ck_a = (self.ck_a + b) & 0xFF
            self.ck_b = (self.ck_b + self.ck_a) & 0xFF
            if self.idx >= self.need:
                self.st = 7
            return
        if st == 7:
            if b == self.ck_a:
                self.st = 8
            else:
                self.ck_fail += 1
                self.st = 0
            return
        if st == 8:
            if b == self.ck_b:
                self.accepted.append((self.cls, self.mid, bytes(self.pld[:self.need])))
            else:
                self.ck_fail += 1
            self.st = 0
            return


failures = []


def check(name, cond, detail=""):
    if cond:
        print(f"  ok   {name}")
    else:
        print(f"  FAIL {name} {detail}")
        failures.append(name)


print("UBX frame parser replica")

# --- 1. NAV-PVT (92 byte payload) still parses ------------------------------
pvt = bytes(range(92))
p = Parser()
p.feed_all = None
for b in build(0x01, 0x01, pvt):
    p.feed(b)
check("NAV-PVT (92 B) accepted", p.accepted == [(0x01, 0x01, pvt)], p.accepted)
check("NAV-PVT no checksum failures", p.ck_fail == 0, p.ck_fail)

# --- 2. ACK / NAK style short frame ----------------------------------------
ack = b"\x05\x00\x00\x00\x00\x00\x00\x00\x00\x00"
p = Parser()
for b in build(0x05, 0x01, ack):
    p.feed(b)
check("ACK-ACK (10 B) accepted", p.accepted == [(0x05, 0x01, ack)], p.accepted)

# --- 3. zero-length payload (issue #33) ------------------------------------
p = Parser(zero_len_to_ck=True)
for b in build(0x0A, 0x06, b""):
    p.feed(b)
check("zero-length frame accepted (fixed parser)",
      p.accepted == [(0x0A, 0x06, b"")], p.accepted)
check("zero-length frame raises no CKFAIL (fixed parser)", p.ck_fail == 0,
      p.ck_fail)

p_old = Parser(zero_len_to_ck=False)
for b in build(0x0A, 0x06, b""):
    p_old.feed(b)
check("zero-length frame IS the reported bug in the old path",
      p_old.accepted == [] and p_old.ck_fail == 1,
      f"accepted={p_old.accepted} ck_fail={p_old.ck_fail}")

# --- 4. poisoned checksum drops one frame, then resyncs --------------------
p = Parser()
bad = bytearray(build(0x01, 0x07, b"\x01\x02\x03\x04"))
bad[-1] ^= 0xFF
for b in bad:
    p.feed(b)
for b in build(0x01, 0x07, b"\x01\x02\x03\x04"):
    p.feed(b)
check("bad checksum counted once", p.ck_fail == 1, p.ck_fail)
check("parser resyncs after a bad frame",
      p.accepted == [(0x01, 0x07, b"\x01\x02\x03\x04")], p.accepted)

# --- 5. garbage before a frame, and a false sync ---------------------------
p = Parser()
stream = b"\x00\xff\xb5\x99 NMEA,\x00" + build(0x01, 0x02, b"\xaa\xbb")
for b in stream:
    p.feed(b)
check("frame recovered after garbage/false sync",
      p.accepted == [(0x01, 0x02, b"\xaa\xbb")], p.accepted)
check("no spurious CKFAIL from garbage", p.ck_fail == 0, p.ck_fail)

# --- 6. oversize length is refused without eating the stream ---------------
p = Parser()
over = b"\xb5\x62\x01\x00\xFF\xFF" + bytes(200)
for b in over:
    p.feed(b)
check("oversize frame refused", p.oversize == 1 and not p.accepted,
      f"oversize={p.oversize} accepted={p.accepted}")
for b in build(0x01, 0x03, b"\x11"):
    p.feed(b)
check("parser still works after an oversize header",
      p.accepted == [(0x01, 0x03, b"\x11")], p.accepted)

# --- 7. back-to-back frames, one zero-length in the middle ----------------
p = Parser()
for b in (build(0x01, 0x01, bytes(92)) + build(0x0A, 0x06, b"") +
          build(0x05, 0x01, bytes(10))):
    p.feed(b)
check("three back-to-back frames all accepted", len(p.accepted) == 3,
      len(p.accepted))
check("no cross-frame checksum damage", p.ck_fail == 0, p.ck_fail)

print()
print(f"FAILURES: {len(failures)}")
sys.exit(1 if failures else 0)
