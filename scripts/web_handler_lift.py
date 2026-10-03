#!/usr/bin/env python3
"""One-shot: lift the 25 anonymous endpoint lambdas in src/web.cpp into named
static handler functions (same pattern as the existing wifi*Handler ones).

A lambda closure type gets its own std::function _M_invoke instantiation; a plain
function pointer shares one. That is most of the per-handler overhead the map
shows (_Function_handler<...EUlvE#..._M_invoke>, 150-650 bytes each)."""
import re

PATH = "src/web.cpp"
NAME = {
    ("/", "HTTP_GET"): "indexGetHandler",
    ("/debug", "HTTP_GET"): "debugGetHandler",
    ("/api/config", "HTTP_GET"): "configGetHandler",
    ("/api/config", "HTTP_POST"): "configPostHandler",
    ("/api/time", "HTTP_POST"): "timePostHandler",
    ("/api/odo", "HTTP_GET"): "odoGetHandler",
    ("/api/fuel", "HTTP_GET"): "fuelGetHandler",
    ("/api/odo", "HTTP_POST"): "odoPostHandler",
    ("/api/trip/reset", "HTTP_POST"): "tripResetPostHandler",
    ("/api/reboot", "HTTP_POST"): "rebootPostHandler",
    ("/api/sleep", "HTTP_POST"): "sleepPostHandler",
    ("/api/reset", "HTTP_POST"): "resetPostHandler",
    ("/api/ambient", "HTTP_GET"): "ambientGetHandler",
    ("/api/sensors", "HTTP_GET"): "sensorsGetHandler",
    ("/api/ambient/cal-dark", "HTTP_POST"): "ambientCalDarkPostHandler",
    ("/api/ambient/cal-bright", "HTTP_POST"): "ambientCalBrightPostHandler",
    ("/api/ota", "HTTP_POST"): ["otaPostHandler", "otaUploadHandler"],
    ("/api/ota/pull", "HTTP_POST"): "otaPullPostHandler",
    ("/api/ota/check", "HTTP_GET"): "otaCheckGetHandler",
    ("/api/boot", "HTTP_GET"): "bootGetHandler",
    ("/api/serial", "HTTP_GET"): "serialGetHandler",
    ("/api/perf", "HTTP_GET"): "perfGetHandler",
    ("/api/health", "HTTP_GET"): "healthGetHandler",
}

for k, v in NAME.items():
    NAME[k] = [v] if isinstance(v, str) else v

lines = open(PATH, encoding="utf-8", newline="").read().split("\r\n")
rx = re.compile(r'^  server\.on\("([^"]+)", (HTTP_GET|HTTP_POST), \[\]\(\) \{$')
cont = re.compile(r'^  \}, \[\]\(\) \{$')

out, funcs = [], []
i = 0
converted = 0
while i < len(lines):
    m = rx.match(lines[i])
    if not m:
        out.append(lines[i])
        i += 1
        continue
    path, method = m.group(1), m.group(2)
    names = NAME[(path, method)]
    # WebServer::on takes a second callback for multipart uploads; /api/ota uses
    # it, so the registration carries two lambda bodies and both get lifted.
    which = 0
    funcs.append("static void %s() {" % names[0])
    depth = lines[i].count("{") - lines[i].count("}")
    j = i + 1
    while True:
        if depth == 1 and cont.match(lines[j]):
            assert which + 1 < len(names), lines[j]
            funcs.append("}")
            funcs.append("")
            funcs.append("static void %s() {" % names[which + 1])
            which += 1
            j += 1
            continue
        if depth + lines[j].count("{") - lines[j].count("}") == 0:
            assert lines[j].startswith("  });"), (j, lines[j])
            break
        body_line = lines[j]
        funcs.append(body_line[2:] if body_line.startswith("  ") else body_line)
        depth += body_line.count("{") - body_line.count("}")
        j += 1
    funcs.append("}")
    funcs.append("")
    out.append('  server.on("%s", %s, %s);'
               % (path, method, ", ".join(names)))
    i = j + 1
    converted += 1

assert converted == 23, converted
src = "\r\n".join(out)
anchor = "\r\n// ----------------------------------------------------------------------------\r\n// Web server task"
if anchor not in src:
    anchor = "\r\nvoid webServerTask(void *pvParameters) {"
assert anchor in src, "anchor not found"
block = ("\r\n// Endpoint handlers. Named functions rather than inline lambdas: each\r\n"
         "// closure type gets its own std::function trampoline in flash, a plain\r\n"
         "// function pointer shares one, and the bodies stay readable outside the\r\n"
         "// 7 KB registration block. Same behaviour, same responses.\r\n\r\n")
src = src.replace(anchor, "\r\n" + block + "\r\n".join(funcs) + anchor, 1)
open(PATH, "w", encoding="utf-8", newline="").write(src)
print("converted %d handlers" % converted)
