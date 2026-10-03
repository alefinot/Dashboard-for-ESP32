#!/usr/bin/env python3
"""Weather widget size - config and WebUI wiring (host-side).

The card used to be 480x28 in stone: the size was a literal inside
drawWeatherWidget(), so the WebUI could move the widget but never resize it.
WEATHER_WIDTH / WEATHER_HEIGHT make it a setting.

This checks only what can be checked without a panel - the declarative parts:

  1. Both parameters are real CFG_TABLE rows: a band, an integer kind, a unique
     NVS key inside the 15-char NVS limit. A parameter that is not in the table
     is not clamped on the write paths, is missing from /api/config, and does
     not survive a reboot.
  2. Their shipped defaults are still the old literals, 480 x 28. An existing
     unit has neither key in NVS, so pref.getInt(key, p.di) hands back the
     compiled default - and the picture must not move on firmware update.
  3. The WebUI offers both controls in the UI Layout group, next to the Position
     row used to place the widget, and in no other group. The System & General
     weather card stays the "what it shows" card.
  4. The renderer references both parameters. This is the one renderer check kept
     here: an orphan parameter that saves to NVS and is never read is the
     characteristic failure of this feature, and it is invisible from the UI.

What this does NOT check: the drawn geometry - centre anchoring, vertical
placement at other heights, the dirty rect after a shrink, which stats get
dropped at narrow widths. That is pixel output, it is only observable on the
panel, and a host-side replica of the layout arithmetic would be a second copy
that drifts from the C++ (the same reason the factory-defaults JSON blob was
deleted). Verify it on-device in demo mode at the minimum, default and maximum
size.

Run from the repository root:  python scripts/verify_weather_size.py
"""
import re
import sys

from cfg_table import all_rows, constants, numeric_rows

WEBUI = "src/webui.html"
UI = "src/ui.cpp"

# The size drawWeatherWidget() hardcoded before this was a setting.
OLD_W, OLD_H = 480, 28

# name -> (band lo, band hi, shipped default)
SPEC = {"WEATHER_WIDTH": (160, 480, OLD_W),
        "WEATHER_HEIGHT": (20, 120, OLD_H)}

errors = []
rows = {r["name"]: r for r in all_rows()}
consts = constants()
band_rows = {name: (kind, lo, hi, dflt)
             for name, kind, lo, hi, dflt in numeric_rows(consts=consts)}

nvs_keys = [r["nvs"] for r in rows.values() if r["nvs"]]
for name, (lo_exp, hi_exp, dflt_exp) in SPEC.items():
    row = rows.get(name)
    if row is None:
        errors.append("%s: no CFG_TABLE row - not clamped, not on /api/config, "
                      "not restored from NVS" % name)
        continue
    if row["kind"] != "CK_INT":
        errors.append("%s: kind %s, expected CK_INT" % (name, row["kind"]))
    if not row["nvs"]:
        errors.append("%s: no NVS key - the value cannot be stored" % name)
    elif len(row["nvs"]) > 15:
        errors.append("%s: NVS key %r is %d chars (NVS keys cap at 15)"
                      % (name, row["nvs"], len(row["nvs"])))
    elif nvs_keys.count(row["nvs"]) != 1:
        errors.append("%s: NVS key %r is used by more than one parameter - they "
                      "would share one NVS slot" % (name, row["nvs"]))
    if name not in band_rows:
        errors.append("%s: not read as a banded numeric row" % name)
        continue
    _kind, lo, hi, dflt = band_rows[name]
    if (lo, hi, dflt) != (float(lo_exp), float(hi_exp), float(dflt_exp)):
        errors.append("%s: band/default is [%g..%g] default %g, expected "
                      "[%g..%g] default %g - the shipped default is what an "
                      "existing unit gets on update, so it has to stay %d"
                      % (name, lo, hi, dflt, lo_exp, hi_exp, dflt_exp,
                         dflt_exp))

# 3: which configMap group each control belongs to. Groups are delimited by
# their `title: "...", icon:` header, so a control is attributed to the group
# whose slice contains it.
webui = open(WEBUI, encoding="utf-8").read()
titles = [(m.group(1), m.start()) for m in
          re.finditer(r'title:\s*"([^"]+)",\s*icon:', webui)]
if not titles:
    errors.append("%s: configMap groups not found" % WEBUI)
groups = {}
for i, (title, start) in enumerate(titles):
    end = (titles[i + 1][1] if i + 1 < len(titles)
           else webui.index("document.addEventListener"))
    groups[title] = webui[start:end]

for name in SPEC:
    field = re.search(r'\{\s*(?:type:\s*"[^"]+",\s*)?id:\s*"%s"[^\n]*' % name, webui)
    where = [t for t, body in groups.items()
             if re.search(r'id:\s*"%s"' % name, body)]
    if where != ["UI Layout"]:
        errors.append(
            "%s: WebUI control in %s, expected UI Layout only"
            % (name, ", ".join(where) if where else "no group"))
    elif field and "unit" not in field.group(0):
        errors.append("%s: control shows a bare number next to Position - give "
                      "it a px unit label" % name)

# 4: the renderer has to consume the settings.
ui = open(UI, encoding="utf-8").read()
for name in SPEC:
    if name not in ui:
        errors.append("%s is never read in %s - it would save to NVS and "
                      "change nothing on screen" % (name, UI))

if errors:
    print("FAIL: weather widget size")
    for e in errors:
        print("  - " + e)
    sys.exit(1)

print("CFG_TABLE rows: %s" % "; ".join(
    "%s [%g..%g] default %g (nvs %s)"
    % (n, band_rows[n][1], band_rows[n][2], band_rows[n][3], rows[n]["nvs"])
    for n in SPEC if n in rows))
print("Shipped defaults still the pre-setting %d x %d - no visual change on "
      "update" % (OLD_W, OLD_H))
print("WebUI controls: UI Layout only")
print("Renderer reads both parameters (geometry itself is a panel check)")
print("OK: weather widget size is wired end to end")
