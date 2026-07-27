#include "dashboard.h"
#include <ArduinoOTA.h>
#include <Update.h>
#include <ESPmDNS.h>

Preferences preferences;
WebServer server(80);

static bool otaUpdateSuccess = false;

const char *index_html = R"rawliteral(
<!DOCTYPE html><html lang="en">
<head>
<meta charset="UTF-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Dashboard++ Config</title>
<style>
@keyframes fadeInUp { from { opacity:0; transform:translateY(12px); } to { opacity:1; transform:translateY(0); } }
@keyframes fadeIn { from { opacity:0; } to { opacity:1; } }
@keyframes pulse { 0%,100%{transform:scale(1)} 50%{transform:scale(1.05)} }
body {font-family:'Segoe UI',Tahoma,Geneva,Verdana,sans-serif; background:#121212; color:#f0f0f0; padding:12px; margin:0 auto; max-width:900px; animation:fadeIn .4s ease;}
.header {text-align:center; margin-bottom:12px; animation:fadeInUp .5s ease;}
h2 {color:#00bcd4; margin:0 0 4px 0; font-size:20px;}
p.subtitle {color:#888; font-size:13px; margin-top:0;}
details {background:#0a0a0a; border-radius:6px; margin-bottom:10px; animation:fadeInUp .4s ease both;}
details > summary { border-radius:6px; }
details[open] > summary { border-radius:6px 6px 0 0; }
.details-content { border-radius:0 0 6px 6px; }
details:nth-of-type(1) { animation-delay:.05s; }
details:nth-of-type(2) { animation-delay:.1s; }
details:nth-of-type(3) { animation-delay:.15s; }
details:nth-of-type(4) { animation-delay:.2s; }
details:nth-of-type(5) { animation-delay:.25s; }
details:nth-of-type(6) { animation-delay:.3s; }
details:nth-of-type(7) { animation-delay:.35s; }
details:nth-of-type(8) { animation-delay:.4s; }
details:nth-of-type(9) { animation-delay:.45s; }
details:nth-of-type(10) { animation-delay:.5s; }
summary {background:#1a1a1a; padding:8px 12px; font-weight:bold; font-size:12px; color:#00bcd4; cursor:pointer; user-select:none; transition:background .3s;}
summary:hover {background:#222;}
summary::-webkit-details-marker {display:none;}
summary::marker {content:'';}
summary::before { content:'▶'; display:inline-flex; align-items:center; justify-content:center; width:12px; height:12px; font-size:8px; margin-right:5px; transition:transform .3s ease; }
details[open] > summary::before { transform:rotate(90deg); }
.details-content {padding:8px; display:grid; grid-template-columns:repeat(auto-fit,minmax(240px,1fr)); gap:6px;}
.card { display:flex; flex-direction:column; justify-content:center; background:#252525; padding:8px 10px; border-radius:5px; }
label {font-size:11px; color:#aaa; font-weight:600;}
input {width:100%; padding:5px 7px; margin-top:4px; background:#2c2c2c; color:#fff; border:1px solid #444; border-radius:4px; box-sizing:border-box; font-size:12px;}
input:focus {border-color:#00bcd4; outline:none; background:#333;}
input[type="checkbox"] {width:18px; height:18px; margin-top:0; accent-color:#00bcd4; cursor:pointer;}
.checkbox-card {flex-direction:row; align-items:center; justify-content:space-between;}
.checkbox-card label {margin-bottom:0;}

.xy-group h4 { margin:0 0 6px 0; color:#fff; font-size:12px; font-weight:bold; border-bottom:1px solid #333; padding-bottom:3px; }
.xy-row { display:flex; align-items:center; gap:6px; margin-bottom:4px; }
.xy-row:last-child { margin-bottom:0; }
.xy-row label { width:12px; color:#00bcd4; font-weight:bold; text-align:center; font-size:12px; }
.xy-row input[type="range"] { flex-grow:1; accent-color:#00bcd4; padding:0; cursor:pointer; height:4px; }
.xy-row input[type="number"] { width:60px; padding:3px 5px; margin:0; text-align:center; font-weight:bold;}

button { background:linear-gradient(135deg,#00bcd4,#007c91); color:#fff; padding:10px; border:none; border-radius:5px; cursor:pointer; width:100%; font-size:14px; font-weight:bold; transition:transform .2s, box-shadow .3s, background .3s; position:relative; overflow:hidden; }
button:hover { background:linear-gradient(135deg,#0097a7,#005b6b); box-shadow:0 4px 14px rgba(0,188,212,.35); transform:translateY(-1px); }
button:active { transform:translateY(0) scale(.98); }

.perf-grid {display:grid; grid-template-columns:1fr 1fr; gap:6px;}
.perf-card {background:#1e1e1e; padding:8px; border-radius:5px; transition:transform .2s ease, box-shadow .2s ease;}
.perf-card:hover { transform:translateY(-1px); box-shadow:0 4px 14px rgba(0,0,0,.25); }
.perf-card h4 {margin:0 0 4px 0; color:#00bcd4; font-size:11px; font-weight:bold;}
.perf-row {display:flex; justify-content:space-between; align-items:center; padding:0; font-size:11px; line-height:1.5;}
.perf-label {color:#888;}
.perf-value {color:#fff; font-weight:bold; font-family:'Consolas','Courier New',monospace;}
.perf-bar {height:4px; background:#333; border-radius:2px; margin-top:3px; overflow:hidden;}
.perf-bar-fill {height:100%; border-radius:2px; transition:width .8s ease;}

.btn-group { display:flex; gap:8px; margin-top:8px; animation:fadeInUp .4s ease both; }
.btn-group button { flex:1; margin-top:0; }
.btn-group:nth-of-type(1) { animation-delay:.6s; }
.btn-group:nth-of-type(2) { animation-delay:.65s; }

.btn-secondary {background:linear-gradient(135deg,#455a64,#263238); font-size:13px;}
.btn-secondary:hover {background:linear-gradient(135deg,#546e7a,#37474f); box-shadow:0 4px 14px rgba(69,90,100,.35);}

.btn-reset {background:linear-gradient(135deg,#e53935,#b71c1c); margin-top:8px; animation:fadeInUp .4s ease .7s both;}
.btn-reset:hover {background:linear-gradient(135deg,#ef5350,#c62828); box-shadow:0 4px 14px rgba(229,57,53,.35);}



#msg {text-align:center; font-weight:bold; margin-bottom:10px; min-height:16px; animation:fadeIn .3s ease;}
.msg-success {color:#4caf50;} .msg-error {color:#f44336;}
#serial-out { background:#0d0d0d; color:#00ff88; font-family:'Consolas','Courier New',monospace; font-size:10px; padding:5px; height:140px; overflow-y:auto; white-space:pre; }
#searchBar { width:100%; padding:8px 12px; margin-bottom:10px; background:#1e1e1e; color:#fff; border:2px solid #333; border-radius:6px; box-sizing:border-box; font-size:13px; animation:fadeInUp .5s ease .1s both; }
#searchBar:focus { border-color:#00bcd4; outline:none; background:#252525; }
#searchBar::placeholder {color:#666;}
#scrollTopBtn { position:fixed; bottom:16px; right:16px; width:36px; height:36px; border-radius:50%; background:#00bcd4; color:#fff; border:none; font-size:16px; cursor:pointer; box-shadow:0 3px 10px rgba(0,0,0,.4); display:none; z-index:999; align-items:center; justify-content:center; transition:opacity .3s, transform .3s, box-shadow .3s; animation:fadeInUp .35s ease; }
#scrollTopBtn:hover { transform:scale(1.1); background:#0097a7; box-shadow:0 4px 14px rgba(0,188,212,.35); }
#scrollTopBtn:active { transform:scale(.95); }
.btn-group button { animation:fadeIn .35s ease both; }
.btn-group:nth-of-type(1) button { animation-delay:.6s; }
.btn-group:nth-of-type(1) button:nth-child(2) { animation-delay:.63s; }
.btn-group:nth-of-type(1) button:nth-child(3) { animation-delay:.66s; }
.btn-group:nth-of-type(2) button { animation-delay:.7s; }
.btn-group:nth-of-type(2) button:nth-child(2) { animation-delay:.73s; }
</style>
</head>
<body>
<div class="header">
    <h2>Dashboard++ Setup</h2>
    <p class="subtitle">Device Configuration & UI Layout Manager</p>
    <p style="margin:-10px 0 14px 0; font-size:11px; color:#666;"><span id="ip-info">AP: 192.168.4.1</span></p>
</div>
<div id="msg"></div>
<input type="text" id="searchBar" placeholder="🔍 Search settings..." oninput="filterConfig()" autocomplete="off">
<div id="form-container"><div id="loading-text" style="text-align:center; padding:40px; color:#888; animation: pulse 1.5s ease infinite;">Loading settings...</div></div>

<details id="perf-panel">
    <summary>📊 Performance Monitor</summary>
    <div class="details-content" id="perf-content" style="display:block;">
        <div class="perf-grid" id="perf-grid"></div>
    </div>
</details>

<div class="btn-group">
    <button onclick="save()" class="btn-primary">💾 Save Settings</button>
    <button onclick="reboot()" class="btn-secondary" style="background-color:#d32f2f;">🔄 Reboot ESP32</button>
    <button onclick="deepSleep()" class="btn-secondary" style="background-color:#7b1fa2;">💤 Deep Sleep</button>
</div>

<div class="btn-group">
    <button class="btn-secondary" onclick="exportBackup()">📥 Backup Settings</button>
    <button class="btn-secondary" onclick="document.getElementById('importFile').click()">📤 Import Settings</button>
    <input type="file" id="importFile" accept=".json" style="display:none" onchange="importBackup(event)">
</div>

<button id="otaBtn" onclick="doOta()" class="btn-reset" style="margin-top:6px;">📤 Select Firmware</button>

<button class="btn-reset" onclick="factoryReset()">⚠️ Factory Reset</button>

<button id="scrollTopBtn" onclick="window.scrollTo({top:0,behavior:'smooth'})">▲</button>

<script>
const configMap = [
    {
        title: "⚙️ System & General",
        items: [
            { type: "card_header", label: "⚙️ General" },
            { id: "TARGET_FPS", label: "Target Refresh Rate (FPS)" },
            { id: "SHOW_FPS_COUNTER_DEFAULT", label: "Show FPS Counter" },
            { id: "ENABLE_DEMO_MODE", label: "Enable Sensor Demo Mode" },
            { id: "ENABLE_POWER_SENSE", label: "Enable Power Sensing", onconfirm: "confirmPowerSense" },
            { id: "SHOW_ELEMENT_BOUNDS", label: "Show UI Element Bounds (Debug)" },
            { type: "card_header", label: "⚡ CPU" },
            { 
                id: "MANUAL_CPU_FREQ", 
                label: "Manual CPU Frequency", 
                type: "select",
                options: [
                    { value: 80, label: "80 MHz" },
                    { value: 160, label: "160 MHz" },
                    { value: 240, label: "240 MHz" }
                ]
            },
            { id: "ENABLE_DYNAMIC_CPU", label: "Enable Dynamic CPU Frequency" },
            { id: "ENABLE_CPU_THROTTLE", label: "Enable Thermal Throttling" },
            { id: "CPU_THROTTLE_TEMP_WARN", label: "Throttle to 160 MHz At (°C)" },
            { id: "CPU_THROTTLE_TEMP_CRIT", label: "Throttle to 80 MHz At (°C)" },
            { type: "card_header", label: "✍️ Custom Signatures" },
            { id: "SPLASH_SIGNATURE", label: "Boot Screen Signature" },
            { id: "REBOOT_SIGNATURE", label: "Reboot Screen Signature" },
            { id: "DASHBOARD_SIGNATURE", label: "Main Dashboard Watermark" }
        ]
    },
    {
        title: "📡 WiFi",
        items: [
            { type: "card_header", label: "📶 Network Settings" },
            { id: "WIFI_TX_POWER_DBM", label: "TX Power (dBm)", type: "range", min: -1, max: 20, step: 1 },
            { type: "heading", label: "📡 Primary Network" },
            { id: "WIFI_SSID", label: "Network Name (SSID)" },
            { id: "WIFI_PASSWORD", label: "Password" },
            { type: "heading", label: "🔄 Fallback Network 1" },
            { id: "WIFI_SSID_1", label: "Network Name (SSID)" },
            { id: "WIFI_PASSWORD_1", label: "Password" },
            { type: "heading", label: "🔄 Fallback Network 2" },
            { id: "WIFI_SSID_2", label: "Network Name (SSID)" },
            { id: "WIFI_PASSWORD_2", label: "Password" },
            { type: "heading", label: "🔄 Fallback Network 3" },
            { id: "WIFI_SSID_3", label: "Network Name (SSID)" },
            { id: "WIFI_PASSWORD_3", label: "Password" },
            { type: "heading", label: "🔄 Fallback Network 4" },
            { id: "WIFI_SSID_4", label: "Network Name (SSID)" },
            { id: "WIFI_PASSWORD_4", label: "Password" }
        ]
    },
    {
        title: "🎨 Display & Colors",
        items: [
            { type: "subtitle", label: "🖥️ Display" },
            { id: "SHUTDOWN_TIME_MS", label: "Shutdown Loading Duration (ms)", type: "number" },
            { id: "DISPLAY_ROTATION", label: "Screen Rotation (0-3)" },
            { id: "DISPLAY_WIDTH", label: "Display Width (px)" },
            { id: "DISPLAY_HEIGHT", label: "Display Height (px)" },
            { id: "SPI_BUS_SPEED", label: "SPI Bus Frequency (Hz)" },
            { id: "DISPLAY_INVERT_COLORS", label: "Invert Display Colors" },
            { id: "ENABLE_ANTIALIASING", label: "Enable Anti-Aliased Rendering" },
            { id: "AA_SHARPNESS", label: "AA Sharpness", type: "range", min: 0.2, max: 1.0, step: 0.1 },
            { type: "subtitle", label: "💡 Brightness Control" },
            { id: "BACKLIGHT_BRIGHTNESS", label: "Backlight Brightness", type: "range", min: 0, max: 100, step: 1 },
            { id: "FADE_DURATION_MS", label: "Fade Duration (ms)", type: "range", min: 100, max: 5000, step: 100 },
            { id: "ENABLE_NIGHT_MODE", label: "Enable Night Mode" },
            { id: "NIGHT_MODE_START_HOUR", label: "Start Time (Hour)" },
            { id: "NIGHT_MODE_END_HOUR", label: "End Time (Hour)" },
            { type: "subtitle", label: "📊 Thresholds" },
            { id: "TEMP_BAR_MIN", label: "Temp Bar Minimum (°C)" },
            { id: "TEMP_BAR_MAX", label: "Temp Bar Maximum (°C)" },
            { id: "TEMP_WARN_RED", label: "Engine Temp Red Zone (°C)" },
            { id: "TEMP_WARN_YEL", label: "Engine Temp Yellow Zone (°C)" },
            { id: "FUEL_WARN_YEL", label: "Fuel Low Yellow Zone (%)" },
            { id: "FUEL_WARN_RED", label: "Fuel Low Red Zone (%)" },
            { type: "card_header", label: "🎨 Colors" },
            { type: "inline-colors", label: "🌡️ Engine Temp", colors: [
                { id: "COLOR_TEMP_NORM", label: "Normal" },
                { id: "COLOR_TEMP_WARN", label: "Warning" },
                { id: "COLOR_TEMP_CRIT", label: "Critical" }
            ] },
            { type: "inline-colors", label: "⛽ Fuel Level", colors: [
                { id: "COLOR_FUEL_NORM", label: "Normal" },
                { id: "COLOR_FUEL_WARN", label: "Warning" },
                { id: "COLOR_FUEL_CRIT", label: "Critical" }
            ] },
            { id: "GHOST_COLOR_STR", label: "Ghost Digit Color", type: "color" }
        ]
    },
    {
        title: "🏍️ Sensors & Vehicle Tuning",
        items: [
            { type: "touch-table", label: "Fuel Level Mapping Table", pointCount: "FUEL_TOUCH_POINTS", arrayId: "touchTable" },
            { type: "subtitle", label: "🌡️ Temperature Sensor" },
            { id: "NTC_R_BALANCE", label: "NTC Balance Resistor (Ohms)" },
            { id: "NTC_BETA", label: "NTC Beta Value" },
            { type: "card_header", label: "⏱️ Acceleration Timer" },
            { id: "ACCEL_START_SPEED", label: "Timer Start Speed (km/h)" },
            { id: "ACCEL_TARGET_SPEED", label: "Timer Target Speed (km/h)" },
            { id: "ACCEL_BADGE_LINE1", label: "Badge Top Text (e.g., '0-50')" },
            { id: "ACCEL_BADGE_LINE2", label: "Badge Bottom Text (e.g., 'km/h')" },
            { type: "subtitle", label: "📦 Miscellaneous" },
            { id: "WHEEL_CIRCUMFERENCE_MM", label: "Wheel Circumference (mm)" },
            { id: "MIN_SPEED_THRESHOLD", label: "Minimum Speed Threshold (km/h)" },
            { id: "FUEL_FILTER_ALPHA", label: "Fuel Sensor Smoothing Alpha" },
            { type: "subtitle", label: "⏱️ Polling Rate" },
            { id: "REFRESH_SPEED_MS", label: "Speed (ms)", type: "number" },
            { id: "REFRESH_SAT_MS", label: "Satellites (ms)", type: "number" },
            { id: "REFRESH_TMR_MS", label: "Accel Timer (ms)", type: "number" },
            { id: "REFRESH_BAT_MS", label: "Battery Voltage (ms)", type: "number" },
            { type: "card_header", label: "🔢 Digit Count Configuration" },
            { id: "SPEED_DIGITS", label: "Speed Digits" },
            { id: "SAT_DIGITS", label: "Satellites Digits" },
            { id: "TMR_INT_DIGITS", label: "Timer Integer Digits" },
            { id: "TMR_DEC_DIGITS", label: "Timer Decimal Digits" },
            { id: "BAT_INT_DIGITS", label: "Battery Integer Digits" },
            { id: "BAT_DEC_DIGITS", label: "Battery Decimal Digits" },
            { id: "INST_INT_DIGITS", label: "Instant KM/L Integer Digits" },
            { id: "INST_DEC_DIGITS", label: "Instant KM/L Decimal Digits" },
            { id: "AVG_INT_DIGITS", label: "Average KM/L Integer Digits" },
            { id: "AVG_DEC_DIGITS", label: "Average KM/L Decimal Digits" },
            { id: "FUEL_INT_DIGITS", label: "Fuel Integer Digits" },
            { id: "FUEL_DEC_DIGITS", label: "Fuel Decimal Digits" },
            { id: "ODO_INT_DIGITS", label: "Odometer Integer Digits" },
            { id: "ODO_DEC_DIGITS", label: "Odometer Decimal Digits" }
        ]
    },
    {
        title: "🛰️ GNSS",
        items: [
            { type: "card_header", label: "🛰️ GNSS Settings" },
            { id: "MIN_SATELLITES", label: "Minimum Satellites Required" },
            { id: "OPTIMAL_SATELLITES", label: "Optimal Satellites Count" },
            { id: "MAX_SPEED_DELTA_KMH", label: "Max Deviation Hall vs GPS (km/h)" },
        ]
    },
    {
        title: "📐 UI Layout",
        items: [
            { type: 'xy', idX: "OFFSET_BIG_TMR_X", idY: "OFFSET_BIG_TMR_Y", label: "Acceleration Timer Offset" },
            { type: 'xy', idX: "OFFSET_AVG_KML_X", idY: "OFFSET_AVG_KML_Y", label: "Average KM/L Offset" },
            { type: 'xy', idX: "OFFSET_BIG_BAT_X", idY: "OFFSET_BIG_BAT_Y", label: "Battery Offset" },
            { type: 'xy', idX: "OFFSET_BIG_TIME_X", idY: "OFFSET_BIG_TIME_Y", label: "Clock Offset" },
            { type: 'xy', idX: "OFFSET_BIG_DATE_X", idY: "OFFSET_BIG_DATE_Y", label: "Date Offset" },
            { type: 'xy', idX: "OFFSET_BIG_FPS_X", idY: "OFFSET_BIG_FPS_Y", label: "FPS Counter Offset" },
            { type: 'xy', idX: "OFFSET_FUEL_LTRS_X", idY: "OFFSET_FUEL_LTRS_Y", label: "Fuel Liters Offset" },
            { type: 'xy', idX: "OFFSET_HALL_ICON_X", idY: "OFFSET_HALL_ICON_Y", label: "Speed Source Icon Offset" },
            { type: 'xy', idX: "OFFSET_INST_KML_X", idY: "OFFSET_INST_KML_Y", label: "Instant KM/L Offset" },
            { type: 'xy', idX: "SIDEBAR_LEFT_X", idY: "SIDEBAR_LEFT_Y", label: "Left Sidebar (Temp) Pos" },
            { type: 'xy', idX: "OFFSET_BIG_SPEED_NUM_X", idY: "OFFSET_BIG_SPEED_NUM_Y", label: "Main Speed Number Offset" },
            { type: 'xy', idX: "OFFSET_BIG_ODO_X", idY: "OFFSET_BIG_ODO_Y", label: "Odometer Offset" },
            { type: 'xy', idX: "SIDEBAR_RIGHT_X", idY: "SIDEBAR_RIGHT_Y", label: "Right Sidebar (Fuel) Pos" },
            { type: "card_header", label: "Sidebar Dimensions" },
            { id: "SIDEBAR_BAR_WIDTH", label: "Bar Width (px)" },
            { id: "SIDEBAR_BAR_HEIGHT", label: "Bar Height (px)" },
            { type: 'xy', idX: "OFFSET_BIG_SAT_X", idY: "OFFSET_BIG_SAT_Y", label: "Satellites Offset" },
            { type: 'xy', idX: "OFFSET_BIG_SIGNATURE_X", idY: "OFFSET_BIG_SIGNATURE_Y", label: "Signature Offset" },
            { type: 'xy', idX: "OFFSET_BIG_SPEED_UNIT_X", idY: "OFFSET_BIG_SPEED_UNIT_Y", label: "Speed Unit (KM/H) Offset" },
            { type: 'xy', idX: "BIG_CENTER_X", idY: "BIG_CENTER_Y", label: "Viewport Center" },
            { type: 'xy', idX: "OFFSET_WIFI_ICON_X", idY: "OFFSET_WIFI_ICON_Y", label: "WiFi Icon Offset" }
        ]
    },
];

fetch('/api/config').then(r=>r.json()).then(d=>{
    let html = '';
    let processedKeys = new Set();
    
    const maxW = d.DISPLAY_WIDTH || 480;
    const maxH = d.DISPLAY_HEIGHT || 320;

    configMap.forEach(group => {
        let groupHtml = `<div class="details-content">`;
        let hasItems = false;
        let inSubCard = false;
        let inCardSubCard = false;

        function closeSubCard() {
            if (inCardSubCard) {
                groupHtml += `</div></div>`;
                inCardSubCard = false;
            } else if (inSubCard) {
                groupHtml += `</div></div>`;
                inSubCard = false;
            }
        }

        group.items.forEach(item => {
            let iType = item.type || 'standard';
            
            if (iType === 'select') {
                if (inCardSubCard) {
                    if(d.hasOwnProperty(item.id)) {
                        hasItems = true;
                        processedKeys.add(item.id);
                        let val = d[item.id];
                        let opts = '';
                        item.options.forEach(o => {
                            let sel = (o.value == val) ? 'selected' : '';
                            opts += `<option value="${o.value}" ${sel}>${o.label}</option>`;
                        });
                        groupHtml += `
                        <div class="card">
                            <label>${item.label}</label>
                            <select id="${item.id}" style="width:100%; padding:6px 8px; margin-top:4px; background:#2c2c2c; color:#fff; border:1px solid #444; border-radius:4px; box-sizing:border-box; font-size:12px;">
                                ${opts}
                            </select>
                        </div>`;
                    }
                } else {
                    closeSubCard();
                    if(d.hasOwnProperty(item.id)) {
                        hasItems = true;
                        processedKeys.add(item.id);
                        let val = d[item.id];
                        let opts = '';
                        item.options.forEach(o => {
                            let sel = (o.value == val) ? 'selected' : '';
                            opts += `<option value="${o.value}" ${sel}>${o.label}</option>`;
                        });
                        groupHtml += `
                        <div class="card">
                            <label>${item.label}</label>
                            <select id="${item.id}" style="width:100%; padding:6px 8px; margin-top:4px; background:#2c2c2c; color:#fff; border:1px solid #444; border-radius:4px; box-sizing:border-box; font-size:12px;">
                                ${opts}
                            </select>
                        </div>`;
                    }
                }
            } else if (iType === 'inline-colors') {
                hasItems = true;
                let rowHtml = `<div style="display:flex;gap:8px;">`;
                item.colors.forEach(c => {
                    processedKeys.add(c.id);
                    rowHtml += `<div style="display:flex;flex-direction:column;align-items:center;gap:2px;flex:1;"><label style="font-size:10px;color:#aaa;text-align:center;">${c.label}</label><input type="color" id="${c.id}" value="${d[c.id]}" style="width:36px;height:36px;border:none;background:none;cursor:pointer;padding:0;"></div>`;
                });
                rowHtml += `</div>`;
                groupHtml += rowHtml;
            } else if (iType === 'color') {
                if(d.hasOwnProperty(item.id)) {
                    hasItems = true;
                    processedKeys.add(item.id);
                    if (inCardSubCard) {
                        groupHtml += `
                        <div class="card">
                            <label>${item.label}</label>
                            <input type="color" id="${item.id}" value="${d[item.id]}" style="width:32px;height:32px;border:none;background:none;cursor:pointer;padding:0;">
                        </div>`;
                    } else if (inSubCard) {
                        groupHtml += `<div style="display:flex;justify-content:space-between;align-items:center;padding:3px 0;"><label style="font-size:12px;color:#ccc;">${item.label}</label><input type="color" id="${item.id}" value="${d[item.id]}" style="width:35px;height:26px;border:none;background:none;cursor:pointer;padding:0;"></div>`;
                    } else {
                        groupHtml += `
                    <div class="form-group">
                        <label>${item.label}</label>
                        <input type="color" id="${item.id}" value="${d[item.id]}">
                    </div>`;
                    }
                }
            } else if (iType === 'button') {
                closeSubCard();
                hasItems = true;
                groupHtml += `
                <div class="card" style="grid-column: 1 / -1; padding: 6px;">
                    <button class="btn-secondary" onclick="${item.action}()" style="margin:0; font-size:12px; padding:8px;">${item.label}</button>
                </div>`;
            } else if (iType === 'range') {
                if(d.hasOwnProperty(item.id)) {
                    hasItems = true;
                    processedKeys.add(item.id);
                    let val = d[item.id];
                    if (inCardSubCard) {
                        groupHtml += `
                        <div class="card">
                            <label>${item.label}</label>
                            <input type="range" id="${item.id}_slider" min="${item.min}" max="${item.max}" step="${item.step}" value="${val}" oninput="document.getElementById('${item.id}').value = this.value" style="width:100%;accent-color:#00bcd4;padding:0;cursor:pointer;height:6px;">
                            <input type="number" id="${item.id}" value="${val}" min="${item.min}" max="${item.max}" step="${item.step}" oninput="document.getElementById('${item.id}_slider').value = this.value" style="width:100%;padding:6px;background:#2c2c2c;color:#fff;border:1px solid #444;border-radius:4px;text-align:center;margin-top:6px;">
                        </div>`;
                    } else if (inSubCard) {
                        groupHtml += `<div style="display:flex;justify-content:space-between;align-items:center;padding:4px 0;"><label style="font-size:13px;color:#ccc;">${item.label}</label><input type="range" id="${item.id}_slider" min="${item.min}" max="${item.max}" step="${item.step}" value="${val}" oninput="document.getElementById('${item.id}').value = this.value" style="flex-grow:1;accent-color:#00bcd4;padding:0;cursor:pointer;height:6px;"><input type="number" id="${item.id}" value="${val}" min="${item.min}" max="${item.max}" step="${item.step}" oninput="document.getElementById('${item.id}_slider').value = this.value" style="width:75px;padding:6px;background:#2c2c2c;color:#fff;border:1px solid #444;border-radius:4px;text-align:center;margin-left:8px;"></div>`;
                    } else {
                        groupHtml += `
                    <div class="card xy-group">
                        <h4>${item.label}</h4>
                        <div class="xy-row">
                            <label>%</label>
                            <input type="range" id="${item.id}_slider" min="${item.min}" max="${item.max}" step="${item.step}" value="${val}" oninput="document.getElementById('${item.id}').value = this.value">
                            <input type="number" id="${item.id}" value="${val}" min="${item.min}" max="${item.max}" step="${item.step}" oninput="document.getElementById('${item.id}_slider').value = this.value">
                        </div>
                    </div>`;
                    }
                }
            } else if (iType === 'touch-table') {
                closeSubCard();
                hasItems = true;
                let count = d[item.pointCount] || 8;
                processedKeys.add(item.pointCount);
                processedKeys.add(item.arrayId);
                let arr = d[item.arrayId] || [];
                let tableHtml = `
                <div class="card" style="grid-column:1/-1; padding:8px 10px;">
                    <h4 style="margin:0 0 4px 0;color:#fff;font-size:12px;">${item.label}</h4>
                    <label style="font-size:11px;color:#aaa;">Number of points (tank capacity + 1):</label>
                    <div style="display:flex; align-items:center; gap:5px; margin-bottom:6px;">
                        <input type="number" id="${item.pointCount}" value="${count}" min="2" max="20" style="width:65px;">
                        <button onclick="save();setTimeout(function(){location.reload()},500)" style="padding:4px 8px; font-size:10px; background:#455a64; color:#fff; border:none; border-radius:3px; cursor:pointer;">↻ Refresh</button>
                    </div>
                    <div style="display:grid; grid-template-columns:60px 1fr 60px 1fr; gap:4px 8px; align-items:center;">
                        <div style="font-weight:bold;color:#00bcd4;font-size:12px;">#</div>
                        <div style="font-weight:bold;color:#00bcd4;font-size:12px;">ADC</div>
                        <div style="font-weight:bold;color:#00bcd4;font-size:12px;">#</div>
                        <div style="font-weight:bold;color:#00bcd4;font-size:12px;">ADC</div>`;
                for (let i = 0; i < count; i++) {
                    let val = (arr[i] !== undefined) ? arr[i] : '';
                    if (i % 2 === 0 && i > 0) tableHtml += `</div><div style="display:grid; grid-template-columns:60px 1fr 60px 1fr; gap:4px 8px; align-items:center;">`;
                    tableHtml += `<div style="color:#aaa;font-size:13px;">${i}</div>`;
                    tableHtml += `<div><input type="number" id="TT_${i}" value="${val}" style="width:100%;"></div>`;
                }
                tableHtml += `</div></div>`;
                groupHtml += tableHtml;
            } else if (iType === 'heading') {
                hasItems = true;
                groupHtml += `<h4 style="margin:6px 0 2px 0;color:#00bcd4;font-size:11px;text-transform:uppercase;letter-spacing:0.5px;">${item.label}</h4>`;
            } else if (iType === 'subtitle') {
                hasItems = true;
                closeSubCard();
                groupHtml += `<div class="card" style="grid-column:1/-1; padding:8px 10px;"><h4 style="margin:0 0 4px 0;color:#fff;font-size:12px;">${item.label}</h4><div style="display:flex;flex-direction:column;gap:4px;">`;
                inSubCard = true;
            } else if (iType === 'card_header') {
                hasItems = true;
                closeSubCard();
                groupHtml += `<div class="card" style="grid-column:1/-1; padding:8px 10px;"><h4 style="margin:0 0 4px 0;color:#fff;font-size:12px;">${item.label}</h4><div style="display:flex;flex-direction:column;gap:4px;">`;
                inCardSubCard = true;
            } else if (iType === 'xy') {
                closeSubCard();
                if(d.hasOwnProperty(item.idX) && d.hasOwnProperty(item.idY)) {
                    hasItems = true;
                    processedKeys.add(item.idX);
                    processedKeys.add(item.idY);
                    
                    let valX = d[item.idX];
                    let valY = d[item.idY];
                    
                    let minX, maxX, minY, maxY;
                    if (item.idX.includes("OFFSET")) {
                        minX = -Math.floor(maxW/2); maxX = Math.floor(maxW/2);
                        minY = -Math.floor(maxH/2); maxY = Math.floor(maxH/2);
                    } else {
                        minX = 0; maxX = maxW;
                        minY = 0; maxY = maxH;
                    }
                    
                    groupHtml += `
                    <div class="card xy-group">
                        <h4>${item.label}</h4>
                        <div class="xy-row">
                            <label>X</label>
                            <input type="range" id="${item.idX}_slider" min="${minX}" max="${maxX}" value="${valX}" oninput="document.getElementById('${item.idX}').value = this.value">
                            <input type="number" id="${item.idX}" value="${valX}" step="1" oninput="document.getElementById('${item.idX}_slider').value = this.value">
                        </div>
                        <div class="xy-row">
                            <label>Y</label>
                            <input type="range" id="${item.idY}_slider" min="${minY}" max="${maxY}" value="${valY}" oninput="document.getElementById('${item.idY}').value = this.value">
                            <input type="number" id="${item.idY}" value="${valY}" step="1" oninput="document.getElementById('${item.idY}_slider').value = this.value">
                        </div>
                    </div>`;
                }
            } else {
                if(d.hasOwnProperty(item.id)) {
                    hasItems = true;
                    processedKeys.add(item.id);
                    let val = d[item.id]; 
                    let type = 'text'; 
                    let checked = ''; 
                    let isBool = (typeof val === 'boolean');
                    
                    if(isBool) { type = 'checkbox'; checked = val ? 'checked' : ''; val = 'true'; }
                    else if(typeof val === 'number') type = 'number';
                    
                    if (inCardSubCard) {
                        if(isBool) {
                            groupHtml += `<div class="card checkbox-card"><label>${item.label}</label><input type="${type}" id="${item.id}" ${checked}></div>`;
                        } else {
                            groupHtml += `<div class="card"><label>${item.label}</label><input type="${type}" id="${item.id}" value="${val}" step="any"></div>`;
                        }
                    } else if (inSubCard) {
                        if(isBool) {
                            groupHtml += `<div class="card checkbox-card"><label>${item.label}</label><input type="${type}" id="${item.id}" ${checked}></div>`;
                        } else {
                            groupHtml += `<div class="card"><label>${item.label}</label><input type="${type}" id="${item.id}" value="${val}" step="any"></div>`;
                        }
                    }
                }
            }
        });
        
        closeSubCard();
        groupHtml += `</div>`;
        if(hasItems) html += `<details><summary>${group.title}</summary>${groupHtml}</details>`;
    });

    document.getElementById('form-container').innerHTML = html;

    initDetailsAnim();

    document.querySelectorAll('#form-container details').forEach(el => {
      el.addEventListener('toggle', function accordionHandler() {
        if(this.open) {
          document.querySelectorAll('#form-container details').forEach(other => {
            if(other !== this && other.open) {
              closeDetails(other);
            }
          });
        }
      });
    });

    function updateCpuMode() {
        let cb = document.getElementById('ENABLE_DYNAMIC_CPU');
        let sel = document.getElementById('MANUAL_CPU_FREQ');
        if (cb && sel) sel.disabled = cb.checked;
    }
    document.getElementById('ENABLE_DYNAMIC_CPU')?.addEventListener('change', updateCpuMode);
    updateCpuMode();

    document.getElementById('ENABLE_POWER_SENSE')?.addEventListener('change', function() {
        if (this.checked && !confirm("Enabling power sensing will start monitoring vehicle power draw. Continue?")) {
            this.checked = false;
        }
    });

    // Automatically sync time from browser clock on WebUI connection
    syncTime();

    // Fetch LAN IP and update status
    fetch('/api/perf').then(r=>r.json()).then(perf => {
        let ipEl = document.getElementById('ip-info');
        let parts = ['<a href="http://192.168.4.1" target="_blank">AP: 192.168.4.1</a>'];
        if (perf.lan_ip && perf.lan_ip.length > 0) {
            parts.unshift('LAN: <a href="http://' + perf.lan_ip + '" target="_blank">' + perf.lan_ip + '</a>');
        } else {
            let wifiStatus = (perf.wifi_clients > 0) ? '(WiFi connected, no LAN IP)' : '(not connected to WiFi)';
            parts.push('<span style="color:#888;">' + wifiStatus + '</span>');
        }
        ipEl.innerHTML = parts.join(' &nbsp;|&nbsp; ');
    }).catch(() => {});
});

window.addEventListener('scroll', function() {
    document.getElementById('scrollTopBtn').style.display = window.scrollY > 400 ? 'flex' : 'none';
});

let autosaveTimer = null;
document.getElementById('form-container').addEventListener('input', function(e) {
    if(e.target.type === 'file') return;
    clearTimeout(autosaveTimer);
    autosaveTimer = setTimeout(save, 2000);
});

function filterConfig(){
    const q = document.getElementById('searchBar').value.toLowerCase().trim();
    document.querySelectorAll('#form-container details').forEach(d => {
        if(d.id === 'perf-panel') return;
        const cards = d.querySelectorAll('.details-content > .card, .details-content > .form-group, .details-content > .xy-group');
        let visible = 0;
        cards.forEach(c => {
            const match = !q || c.textContent.toLowerCase().includes(q);
            c.style.display = match ? '' : 'none';
            if(match) visible++;
        });
        d.style.display = (!q || visible > 0 || d.querySelector('summary').textContent.toLowerCase().includes(q)) ? '' : 'none';
        if(q) d.open = true;
    });
}

function save(){
    let out = {};
    document.querySelectorAll('input, select').forEach(el => {
        if(el.id.endsWith('_slider') || el.type === 'file') return;
        
        if(el.id.startsWith('TT_')) {
            let idx = parseInt(el.id.substring(3));
            if (!out['touchTable']) out['touchTable'] = [];
            out['touchTable'][idx] = parseFloat(el.value) || 0;
        } else if(el.type === 'checkbox') out[el.id] = el.checked;
        else if(el.type === 'number' || el.tagName.toLowerCase() === 'select') {
            let val = parseFloat(el.value);
            out[el.id] = isNaN(val) ? el.value : val;
        }
        else if (el.id.match(/^WIFI_PASSWORD(_\d+)?$/)) {
            if (el.value === '' && el.hasAttribute('data-orig')) {
                out[el.id] = el.getAttribute('data-orig');
            } else {
                out[el.id] = el.value;
            }
        }
        else out[el.id] = el.value;
    });
    
    sendConfigToDevice(out, "Saving settings to device...", "Configuration saved successfully! Display updated.");
}

function reboot() {
    if(!confirm("Are you sure you want to reboot the device?")) return;
    let msgBox = document.getElementById('msg');
    msgBox.className = '';
    msgBox.innerText = "Rebooting device...";
    
    fetch('/api/reboot', {method:'POST'})
    .then(() => {
        msgBox.className = 'msg-success';
        msgBox.innerText = "Reboot command sent. Please refresh shortly.";
        setTimeout(() => location.reload(), 4000);
    })
    .catch(() => {
        msgBox.className = 'msg-error';
        msgBox.innerText = "Connection lost during reboot. Please refresh shortly.";
        setTimeout(() => location.reload(), 4000);
    });
}

function deepSleep() {
    if(!confirm("Put device into deep sleep?")) return;
    let msgBox = document.getElementById('msg');
    msgBox.className = '';
    msgBox.innerText = 'Device going to sleep...';
    fetch('/api/sleep', {method:'POST'}).then(() => {
        msgBox.className = 'msg-success';
        msgBox.innerText = 'Device is now sleeping.';
    }).catch(() => {
        msgBox.className = 'msg-error';
        msgBox.innerText = 'Connection lost (device is sleeping).';
    });
}

function exportBackup() {
    fetch('/api/config').then(r=>r.json()).then(d=>{
        const blob = new Blob([JSON.stringify(d, null, 2)], {type: "application/json"});
        const url = URL.createObjectURL(blob);
        const a = document.createElement('a');
        a.href = url;
        a.download = "dashboard_backup.json";
        document.body.appendChild(a);
        a.click();
        document.body.removeChild(a);
        URL.revokeObjectURL(url);
    }).catch(err => {
        alert("Failed to fetch configuration for backup.");
    });
}

function syncTime() {
    let msgBox = document.getElementById('msg');
    let epoch = Math.floor(Date.now() / 1000);
    fetch('/api/time', {
        method: 'POST',
        headers: {'Content-Type': 'application/json'},
        body: JSON.stringify({timestamp: epoch})
    })
    .then(r => r.json())
    .then(d => {
        msgBox.className = 'msg-success';
        msgBox.innerText = "Time synced successfully!";
    })
    .catch(() => {
        msgBox.className = 'msg-error';
        msgBox.innerText = "Failed to sync time.";
    });
}

function importBackup(event) {
    const file = event.target.files[0];
    if(!file) return;
    
    const reader = new FileReader();
    reader.onload = function(e) {
        try {
            const jsonConfig = JSON.parse(e.target.result);
            if(!confirm("Are you sure you want to restore these settings? This will overwrite the current configuration. (A separate reboot is not required.)")) {
                event.target.value = "";
                return;
            }
            sendConfigToDevice(jsonConfig, "Restoring configuration...", "Backup restored successfully!");
        } catch(err) {
            alert("Error: Invalid JSON file!");
        }
        event.target.value = "";
    };
    reader.readAsText(file);
}

function sendConfigToDevice(jsonPayload, startMsg, successMsg) {
    let msgBox = document.getElementById('msg');
    msgBox.className = '';
    msgBox.innerText = startMsg;
    
    fetch('/api/config', {method:'POST', body:JSON.stringify(jsonPayload)})
    .then(r => r.json())
    .then(r => { 
        msgBox.className = 'msg-success';
        msgBox.innerText = successMsg; 
    })
    .catch(() => {
        msgBox.className = 'msg-error';
        msgBox.innerText = "Error communicating with the device.";
    });
}

// ─── Smooth details open/close animation ─────────────────────────────
function initDetailsAnim() {
    document.querySelectorAll('details').forEach(function(el) {
        if (el._animReady) return;
        el._animReady = true;
        var content = el.querySelector(':scope > .details-content, :scope > div');
        if (!content) return;
        el._c = content;
        el.querySelector('summary').addEventListener('click', function(e) {
            e.preventDefault();
            if (el.open) {
                if (el.id === 'perf-panel') {
                    clearInterval(perfTimer); perfTimer = null;
                    clearInterval(serialPollTimer); serialPollTimer = null;
                }
                animateSlideUp(el);
            } else {
                if (el.id === 'perf-panel') buildPerfPanel();
                animateSlideDown(el);
            }
        });
    });
}

function animateSlideDown(el) {
    el.setAttribute('open', '');
    var c = el._c;
    c.style.maxHeight = '0';
    c.style.overflow = 'hidden';
    void c.offsetHeight;
    var sh = c.scrollHeight;
    c.style.transition = 'max-height 0.35s ease, opacity 0.3s ease';
    c.style.opacity = '1';
    c.style.maxHeight = sh + 'px';
    var tid;
    var onEnd = function(e) {
        if (e.propertyName !== 'max-height') return;
        clearTimeout(tid);
        c.removeEventListener('transitionend', onEnd);
        c.style.transition = '';
        c.style.overflow = '';
        c.style.maxHeight = '';
        c.style.opacity = '';
    };
    c.addEventListener('transitionend', onEnd);
    tid = setTimeout(function() {
        c.removeEventListener('transitionend', onEnd);
        c.style.transition = '';
        c.style.overflow = '';
        c.style.maxHeight = '';
        c.style.opacity = '';
    }, 450);
}

function animateSlideUp(el) {
    var c = el._c;
    var sh = c.scrollHeight;
    c.style.transition = 'max-height 0.35s ease, opacity 0.25s ease';
    c.style.overflow = 'hidden';
    c.style.maxHeight = sh + 'px';
    c.style.opacity = '1';
    void c.offsetHeight;
    c.style.maxHeight = '0';
    c.style.opacity = '0';
    var tid;
    var onEnd = function(e) {
        if (e.propertyName !== 'max-height') return;
        clearTimeout(tid);
        c.removeEventListener('transitionend', onEnd);
        c.style.transition = '';
        c.style.overflow = '';
        c.style.maxHeight = '';
        c.style.opacity = '';
        el.removeAttribute('open');
    };
    c.addEventListener('transitionend', onEnd);
    tid = setTimeout(function() {
        c.removeEventListener('transitionend', onEnd);
        c.style.transition = '';
        c.style.overflow = '';
        c.style.maxHeight = '';
        c.style.opacity = '';
        el.removeAttribute('open');
    }, 450);
}

function closeDetails(el) {
    if (!el || !el._c) { el.removeAttribute('open'); return; }
    animateSlideUp(el);
}

let serialPollTimer = null;
let serialAutoScroll = true;

let serialHasData = false;

function pollSerial() {
  fetch('/api/serial').then(r => r.text()).then(data => {
    let el = document.getElementById('serial-out');
    
    if (!data || data.length === 0) {
      if (!serialHasData) el.innerHTML = 'Waiting for data...';
      return;
    }
    
    serialHasData = true;
    
    // Escape HTML and convert newlines to <br>
    let html = data
      .replace(/&/g, '&amp;')
      .replace(/</g, '&lt;')
      .replace(/>/g, '&gt;')
      .replace(/\n/g, '<br>');
    
    let atBottom = el.scrollTop + el.clientHeight >= el.scrollHeight - 30;

    el.insertAdjacentHTML('beforeend', html);

    if (atBottom) {
        el.scrollTop = el.scrollHeight;
    }
  }).catch(() => {});
}

function factoryReset() {
    if(!confirm("Are you sure you want to reset to factory settings? All configurations and odometer data will be lost. This cannot be undone!")) return;
    
    let msgBox = document.getElementById('msg');
    msgBox.className = '';
    msgBox.innerText = "Restoring factory settings...";
    
    fetch('/api/reset', {method:'POST'})
    .then(r => r.json())
    .then(r => { 
        msgBox.className = 'msg-success';
        msgBox.innerText = "Factory reset successful! Device is rebooting..."; 
        setTimeout(() => location.reload(), 5000); 
    })
    .catch(() => {
        msgBox.className = 'msg-error';
        msgBox.innerText = "Connection lost during reboot (Normal behavior). Please refresh shortly.";
    });
}

var _otaFile = null;

function doOta() {
    const btn = document.getElementById('otaBtn');
    if (!_otaFile) {
        var inp = document.createElement('input');
        inp.type = 'file'; inp.accept = '.bin';
        inp.onchange = function() {
            if (!inp.files || !inp.files[0]) return;
            _otaFile = inp.files[0];
            btn.textContent = '📤 Upload ' + _otaFile.name;
            btn.style.background = '';
        };
        inp.click();
        return;
    }
    if (!confirm('Upload ' + _otaFile.name + ' and reboot the device?')) return;
    btn.disabled = true;
    btn.textContent = '⏳ Uploading...';
    btn.style.background = 'linear-gradient(135deg,#777,#555)';
    const fd = new FormData();
    fd.append('firmware', _otaFile);
    fetch('/api/ota', {method:'POST', body:fd})
    .then(r => r.json()).then(d => {
        if (d.status === 'ok') {
            btn.textContent = '✅ Done';
            btn.style.background = 'linear-gradient(135deg,#4caf50,#2e7d32)';
            setTimeout(() => location.reload(), 10000);
        } else {
            btn.disabled = false;
            btn.textContent = '🔄 Retry';
            btn.style.background = '';
        }
    }).catch(() => {
        btn.textContent = '⏳ Rebooting...';
        setTimeout(() => location.reload(), 30000);
    });
}

// ─── Performance Monitor ─────────────────────────────────────────────
let perfTimer = null;

function buildPerfPanel() {
    let grid = document.getElementById('perf-grid');
    if (!grid || grid.children.length > 0) return;
    grid.innerHTML = `
        <div class="perf-card" style="grid-column:span 1;">
            <h4>🖥️ Display</h4>
            <div class="perf-row"><span class="perf-label">Current FPS</span><span class="perf-value" id="p-fps-cur">--</span></div>
            <div class="perf-row"><span class="perf-label">Average FPS</span><span class="perf-value" id="p-fps-avg">--</span></div>
            <div class="perf-row"><span class="perf-label">Target FPS</span><span class="perf-value" id="p-fps-tgt">--</span></div>
            <div class="perf-row"><span class="perf-label">Refresh</span><span class="perf-value" id="p-refresh">--</span></div>
            <div class="perf-row"><span class="perf-label">Resolution</span><span class="perf-value" id="p-resolution">--</span></div>
        </div>
        <div class="perf-card" style="grid-column:span 1;">
            <h4>🔧 System</h4>
            <div class="perf-row"><span class="perf-label">Uptime</span><span class="perf-value" id="p-uptime">--</span></div>
            <div class="perf-row"><span class="perf-label">WiFi Clients</span><span class="perf-value" id="p-wifi">--</span></div>
            <div class="perf-row"><span class="perf-label">SPI Bus</span><span class="perf-value" id="p-spi">--</span></div>
        </div>
        <div class="perf-card" style="grid-column:1/-1;">
            <h4>⚡ CPU</h4>
            <div class="perf-row"><span class="perf-label">Frequency</span><span class="perf-value" id="p-cpu-freq">--</span></div>
            <div class="perf-row"><span class="perf-label">Mode</span><span class="perf-value" id="p-cpu-mode">--</span></div>
            <div class="perf-row"><span class="perf-label">Temperature</span><span class="perf-value" id="p-cpu-temp">--</span></div>
            <div style="margin-top:8px;">
                <div style="display:flex;justify-content:space-between;font-size:13px;color:#aaa;margin-bottom:4px;">
                    <span>Usage</span>
                    <span><strong style="color:#fff" id="cpu-pct">--</strong>%</span>
                </div>
                <div style="height:16px;background:#333;border-radius:4px;overflow:hidden;">
                    <div id="cpu-used-bar" style="height:100%;width:0;background:#4caf50;border-radius:4px;transition:width 0.8s ease;"></div>
                </div>
            </div>
        </div>
        <div class="perf-card" style="grid-column:1/-1;">
            <h4>💾 Memory & Storage</h4>
            <h5 style="margin:0 0 6px 0;color:#aaa;font-size:11px;">RAM</h5>
            <div style="display:flex;justify-content:space-between;font-size:12px;color:#aaa;margin-bottom:3px;">
                <span>Used: <strong style="color:#fff" id="mem-used">--</strong> KB</span>
                <span>Free: <strong style="color:#fff" id="mem-free">--</strong> KB</span>
                <span>Total: <strong style="color:#fff" id="mem-total">--</strong> KB</span>
                <span><strong style="color:#fff" id="mem-pct">--</strong>%</span>
            </div>
            <div style="height:18px;background:#333;border-radius:4px;overflow:hidden;margin-bottom:8px;">
                <div id="mem-used-bar" style="height:100%;width:0;background:#4caf50;border-radius:4px;transition:width 0.8s ease;"></div>
            </div>
            <div style="display:flex;gap:12px;font-size:12px;margin-bottom:8px;">
                <span style="color:#888;">Min Free (peak): <strong style="color:#fff" id="mem-minfree">--</strong></span>
                <span id="mem-psram-row" style="display:none;color:#888;">PSRAM Used: <strong style="color:#fff" id="mem-psram">--</strong></span>
            </div>
            <h5 style="margin:0 0 6px 0;color:#aaa;font-size:11px;">Flash Storage</h5>
            <div style="display:flex;justify-content:space-between;font-size:12px;color:#aaa;margin-bottom:3px;">
                <span>Used: <strong style="color:#fff" id="fl-used">--</strong> KB</span>
                <span>Free: <strong style="color:#fff" id="fl-free">--</strong> KB</span>
                <span>Total: <strong style="color:#fff" id="fl-total">--</strong> KB</span>
                <span><strong style="color:#fff" id="fl-pct">--</strong>%</span>
            </div>
            <div style="height:18px;background:#333;border-radius:4px;overflow:hidden;">
                <div id="fl-used-bar" style="height:100%;width:0;background:#2196f3;border-radius:4px;transition:width 0.8s ease;"></div>
            </div>
        </div>
        <div class="perf-card" style="grid-column:1/-1;">
            <h4>📟 Serial Monitor</h4>
            <div id="serial-out" style="background:#0d0d0d;color:#00ff88;font-family:'Consolas','Courier New',monospace;font-size:10px;padding:5px;height:140px;overflow-y:auto;white-space:pre;">Waiting for data...</div>
        </div>`;
    document.getElementById('serial-out').addEventListener('scroll', function() {
        serialAutoScroll = (this.scrollTop + this.clientHeight >= this.scrollHeight - 30);
    });
}

function updatePerf() {
    fetch('/api/perf').then(r=>r.json()).then(d => {
        function set(id, v) { let el = document.getElementById(id); if(el) el.textContent = v; }
        
        set('p-cpu-freq', d.cpu_freq + ' MHz');
        set('p-cpu-mode', d.cpu_dynamic ? 'Dynamic' : 'Fixed ' + d.cpu_freq + ' MHz');
        set('p-cpu-temp', d.cpu_temp.toFixed(1) + ' °C');
        let cPct = Math.min(d.cpu_usage, 100);
        let cBar = document.getElementById('cpu-used-bar');
        if (cBar) {
            cBar.style.width = cPct.toFixed(0) + '%';
            cBar.style.background = cPct < 50 ? '#4caf50' : cPct < 80 ? '#ff9800' : '#f44336';
        }
        set('cpu-pct', cPct.toFixed(1));
        
        // ── RAM bar ──
        let rUsed = d.heap_size - d.free_heap;
        let rPct = d.heap_size > 0 ? (rUsed / d.heap_size * 100) : 0;
        let rBar = document.getElementById('mem-used-bar');
        if (rBar) { rBar.style.width = rPct.toFixed(0)+'%'; rBar.style.background = rPct<70?'#4caf50':rPct<85?'#ff9800':'#f44336'; }
        set('mem-used', (rUsed/1024).toFixed(0));
        set('mem-total', (d.heap_size/1024).toFixed(0));
        set('mem-pct', rPct.toFixed(1));
        set('mem-free', (d.free_heap/1024).toFixed(0));
        set('mem-minfree', (d.min_free_heap/1024).toFixed(0) + ' KB');
        let psramRow = document.getElementById('mem-psram-row');
        if (d.psram_size > 0 && psramRow) {
            psramRow.style.display = 'inline';
            let pu = d.psram_size - d.psram_free;
            set('mem-psram', (pu/1024).toFixed(0) + ' / ' + (d.psram_size/1024).toFixed(0) + ' KB');
        }
        
        // ── Flash bar ──
        let flUsed = d.flash_total - d.flash_free;
        let flPct = d.flash_total > 0 ? (flUsed / d.flash_total * 100) : 0;
        let flBar = document.getElementById('fl-used-bar');
        if (flBar) { flBar.style.width = flPct.toFixed(0)+'%'; }
        set('fl-used', (flUsed/1024).toFixed(0));
        set('fl-total', (d.flash_total/1024).toFixed(0));
        set('fl-pct', flPct.toFixed(1));
        set('fl-free', (d.flash_free/1024).toFixed(0));
        
        set('p-fps-cur', d.fps_current.toFixed(1));
        set('p-fps-avg', d.fps_average.toFixed(1));
        set('p-fps-tgt', d.fps_target);
        set('p-refresh', d.refresh_ms + ' ms');
        set('p-resolution', d.resolution);
        
        let s = d.uptime_s;
        set('p-uptime', Math.floor(s/3600)+'h '+Math.floor((s%3600)/60)+'m '+(s%60)+'s');
        set('p-wifi', d.wifi_clients);
        set('p-spi', (d.spi_speed/1e6).toFixed(0)+' MHz');
    }).catch(() => {});
}

document.getElementById('perf-panel').addEventListener('toggle', function() {
    if (this.open) {
        buildPerfPanel();
        updatePerf();
        perfTimer = setInterval(updatePerf, 1000);
        serialPollTimer = setInterval(pollSerial, 200);
        pollSerial();
    } else {
        clearInterval(perfTimer); perfTimer = null;
        clearInterval(serialPollTimer); serialPollTimer = null;
    }
});
</script>
</body>
</html>
)rawliteral";

void webServerTask(void *pvParameters) {
  WiFi.mode(WIFI_AP_STA);
  int txPower = WIFI_TX_POWER_DBM;
  if (txPower < -1) txPower = -1;
  if (txPower > 20) txPower = 20;
  WiFi.setTxPower((wifi_power_t)txPower);
  WiFi.softAP("Dashboard_Config", "12345678");
  logPrintf("AP: Dashboard_Config\n");
  logPrintf("AP IP: %s\n", WiFi.softAPIP().toString().c_str());

  delay(100);

  struct WifiNetwork { const char *ssid; const char *pass; };
  const int MAX_WIFI_NETS = 5;
  WifiNetwork wifiNets[MAX_WIFI_NETS];
  int wifiNetCount = 0;

  wifiNets[wifiNetCount++] = {WIFI_SSID.c_str(), WIFI_PASSWORD.c_str()};
  if (WIFI_SSID_1.length() > 0) wifiNets[wifiNetCount++] = {WIFI_SSID_1.c_str(), WIFI_PASSWORD_1.c_str()};
  if (WIFI_SSID_2.length() > 0) wifiNets[wifiNetCount++] = {WIFI_SSID_2.c_str(), WIFI_PASSWORD_2.c_str()};
  if (WIFI_SSID_3.length() > 0) wifiNets[wifiNetCount++] = {WIFI_SSID_3.c_str(), WIFI_PASSWORD_3.c_str()};
  if (WIFI_SSID_4.length() > 0) wifiNets[wifiNetCount++] = {WIFI_SSID_4.c_str(), WIFI_PASSWORD_4.c_str()};

  bool connected = false;
  WiFi.setHostname("dashboard-pp");

  for (int i = 0; i < wifiNetCount && !connected; i++) {
    if (strlen(wifiNets[i].ssid) == 0) continue;

    logPrintf("Trying WiFi[%d]: %s\n", i, wifiNets[i].ssid);
    WiFi.begin(wifiNets[i].ssid, wifiNets[i].pass);

    unsigned long startAttempt = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - startAttempt < 5000) {
      delay(100);
    }

    if (WiFi.status() == WL_CONNECTED) {
      connected = true;
      logPrintf("STA connected: %s\n", WiFi.localIP().toString().c_str());
      logPrintf("Gateway: %s\n", WiFi.gatewayIP().toString().c_str());
    } else {
      logPrintf("WiFi[%d] failed, trying next...\n", i);
      WiFi.disconnect(false);
      delay(200);
    }
  }

  if (connected) {
    if (MDNS.begin("dashboard-pp")) {
      MDNS.addService("http", "tcp", 80);
      logPrintf("mDNS: http://dashboard-pp.local\n");
    }
  } else {
    logPrintf("All WiFi networks failed, using AP only\n");
  }

  ArduinoOTA.onStart([]() {
    logPrintf("OTA started\n");
    otaUpdateInProgress = true;
    showUpdatingScreen();
  });
  ArduinoOTA.onEnd([]() {
    logPrintf("OTA finished\n");
    otaUpdateSuccess = true;
    otaProgressTarget = 258;
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    updateOTAProgress(progress, total);
    logPrintf("OTA: %u%%\n", (progress * 100) / total);
  });
  ArduinoOTA.onError([](ota_error_t error) {
    logPrintf("OTA error: %d\n", error);
    otaUpdateInProgress = false;
    forceFullRedraw = true;
  });
  ArduinoOTA.begin();
  logPrintf("ArduinoOTA ready\n");

  server.on("/", HTTP_GET, []() { server.send(200, "text/html", index_html); });

  server.on("/api/config", HTTP_GET, []() {
    JsonDocument doc;
    processConfig(1, &doc);
    String out;
    serializeJson(doc, out);
    server.send(200, "application/json", out);
  });

  server.on("/api/config", HTTP_POST, []() {
    if (!server.hasArg("plain")) {
      server.send(400);
      return;
    }
    JsonDocument doc;
    deserializeJson(doc, server.arg("plain"));

    processConfig(2, &doc);
    recalculateDerivedParams();
    display.applyBusConfig();
    // Apply CPU frequency immediately
    {
      uint32_t freq = ENABLE_DYNAMIC_CPU ? 240 : MANUAL_CPU_FREQ;
      setCpuFrequencyMhz(freq);
      logPrintf("CPU: %dMHz (config change)\n", freq);
    }
    server.send(200, "application/json", "{\"status\":\"ok\"}");
    forceFullRedraw = true;
    pendingInvertDisplay = true;
    pendingBacklightValue = BACKLIGHT_BRIGHTNESS;
  });

  server.on("/api/time", HTTP_POST, []() {
    if (!server.hasArg("plain")) {
      server.send(400);
      return;
    }
    JsonDocument doc;
    deserializeJson(doc, server.arg("plain"));
    if (doc["timestamp"].is<long>()) {
      long epoch = doc["timestamp"];
      struct timeval tv;
      tv.tv_sec = epoch;
      tv.tv_usec = 0;
      settimeofday(&tv, NULL);
      logPrintf("RTC sync: %ld\n", epoch);
    }
    server.send(200, "application/json", "{\"status\":\"ok\"}");
  });

  server.on("/api/reboot", HTTP_POST, []() {
    server.send(200, "application/json", "{\"status\":\"ok\"}");
    pendingReboot = true;
  });

  server.on("/api/sleep", HTTP_POST, []() {
    server.send(200, "application/json", "{\"status\":\"ok\"}");
    pendingSleep = true;
  });

  server.on("/api/reset", HTTP_POST, []() {
    Preferences pref;

    pref.begin("cfg", false);
    pref.clear();
    pref.end();

    pref.begin("dashboard", false);
    pref.clear();
    pref.end();

    server.send(200, "application/json", "{\"status\":\"ok\"}");
    logPrintf("Factory reset, rebooting\n");

    pendingReboot = true;
  });

  server.on("/api/ota", HTTP_POST, []() {
    if (otaUpdateSuccess) {
      server.send(200, "application/json", "{\"status\":\"ok\",\"msg\":\"Update OK\"}");
      delay(100);
      ESP.restart();
    } else {
      server.send(500, "application/json", "{\"status\":\"error\",\"msg\":\"Update failed\"}");
      otaUpdateInProgress = false;
      forceFullRedraw = true;
    }
  }, []() {
    HTTPUpload &upload = server.upload();
    if (upload.status == UPLOAD_FILE_START) {
      otaUpdateSuccess = false;
      otaUpdateInProgress = true;
      showUpdatingScreen();
      logPrintf("OTA web: start %s\n", upload.filename.c_str());
      if (!Update.begin(UPDATE_SIZE_UNKNOWN)) {
        Update.printError(Serial);
      }
    } else if (upload.status == UPLOAD_FILE_WRITE) {
      if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
        Update.printError(Serial);
      }
      if (upload.totalSize > 0) {
        updateOTAProgress(Update.progress(), upload.totalSize);
      }
    } else if (upload.status == UPLOAD_FILE_END) {
      if (Update.end(true)) {
        logPrintf("OTA web: success %u bytes\n", upload.totalSize);
        otaUpdateSuccess = true;
      } else {
        Update.printError(Serial);
      }
    }
  });

  server.on("/api/serial", HTTP_GET, []() {
    // Read all available data and advance the tail atomically
    int tail = logTail;
    int head = logHead;
    int len = (head >= tail) ? (head - tail) : (LOG_BUF_SIZE - tail + head);
    
    String out;
    if (len > 0) {
      out.reserve(len + 1);
      if (head >= tail) {
        for (int i = tail; i < head; i++)
          out += logBuf[i];
      } else {
        for (int i = tail; i < LOG_BUF_SIZE; i++)
          out += logBuf[i];
        for (int i = 0; i < head; i++)
          out += logBuf[i];
      }
      logTail = head;
    }
    server.send(200, "text/plain", out);
  });

  server.on("/api/perf", HTTP_GET, []() {
    JsonDocument doc;

    doc["cpu_freq"] = getCpuFrequencyMhz();
    doc["cpu_temp"] = temperatureRead();
    doc["cpu_dynamic"] = ENABLE_DYNAMIC_CPU;
    doc["cpu_usage"] = cpuUsagePct;
    doc["uptime_s"] = millis() / 1000;
    doc["free_heap"] = ESP.getFreeHeap();
    doc["min_free_heap"] = ESP.getMinFreeHeap();
    doc["heap_size"] = ESP.getHeapSize();
    doc["psram_size"] = ESP.getPsramSize();
    doc["psram_free"] = ESP.getFreePsram();
    doc["flash_total"] = ESP.getFlashChipSize();
    doc["flash_free"] = ESP.getFreeSketchSpace();
    doc["fps_current"] = currentMeasuredFps;
    doc["fps_average"] = currentAverageFps;
    doc["fps_target"] = TARGET_FPS;
    doc["refresh_ms"] = (unsigned long)DISPLAY_REFRESH_MS;
    doc["spi_speed"] = SPI_BUS_SPEED;
    doc["wifi_clients"] = WiFi.softAPgetStationNum();
    String lanIp = "";
    if (WiFi.status() == WL_CONNECTED) {
      lanIp = WiFi.localIP().toString();
      // Only set if it's a valid LAN IP (not the AP address)
      if (lanIp.length() > 0 && !lanIp.equals("192.168.4.1")) {
        doc["lan_ip"] = lanIp;
      } else {
        doc["lan_ip"] = "";
      }
    }
    doc["resolution"] = String(DISPLAY_WIDTH) + "x" + String(DISPLAY_HEIGHT);

    String out;
    serializeJson(doc, out);
    server.send(200, "application/json", out);
  });

  server.begin();

  unsigned long lastClientTime = millis();

  for (;;) {
    server.handleClient();
    ArduinoOTA.handle();

    if (WiFi.softAPgetStationNum() > 0) {
      lastClientTime = millis();
    }

    if (WiFi.status() == WL_CONNECTED) {
      lastClientTime = millis();
    }

    // Allow 10 minutes before powering off (for testing)
    if (millis() - lastClientTime > 600000) {
      logPrintf("WiFi timeout, disabled\n");
      WiFi.mode(WIFI_OFF);
      vTaskDelete(NULL);
    }

    vTaskDelay(pdMS_TO_TICKS(10));
  }
}
