#include "dashboard.h"

Preferences preferences;
WebServer server(80);

const char *index_html = R"rawliteral(
<!DOCTYPE html><html lang="en">
<head>
<meta charset="UTF-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Dashboard++ Config</title>
<style>
body {font-family:'Segoe UI',Tahoma,Geneva,Verdana,sans-serif; background:#121212; color:#f0f0f0; padding:20px; margin:0; max-width: 900px; margin: auto;}
.header {text-align:center; margin-bottom: 30px;}
h2 {color:#00bcd4; margin:0 0 5px 0; font-size: 28px;}
p.subtitle {color:#888; font-size: 14px; margin-top:0;}
details {background:#0a0a0a; border-radius:10px; margin-bottom:15px; overflow:hidden;}
summary {background:#1a1a1a; padding:12px 20px; font-weight:bold; font-size:14px; color:#00bcd4; cursor:pointer; user-select:none;}
summary:hover {background:#222;}
.details-content {padding: 20px; display:grid; grid-template-columns:repeat(auto-fit,minmax(280px,1fr)); gap:15px;}
.card {display: flex; flex-direction: column; justify-content: center; background:#252525; padding:15px; border-radius:8px;}
label {font-size:13px; color:#aaa; font-weight:600;}
input {width:100%; padding:10px; margin-top:6px; background:#2c2c2c; color:#fff; border:1px solid #444; border-radius:6px; box-sizing:border-box; font-size:14px; transition: border 0.3s;}
input:focus {border-color: #00bcd4; outline: none;}
input[type="checkbox"] {width:22px; height:22px; margin-top:0; accent-color:#00bcd4; cursor:pointer;}
.checkbox-card {flex-direction: row; align-items: center; justify-content: space-between;}
.checkbox-card label {margin-bottom: 0;}

.xy-group h4 { margin: 0 0 12px 0; color: #fff; font-size: 14px; font-weight: bold; border-bottom: 1px solid #333; padding-bottom: 6px;}
.xy-row { display: flex; align-items: center; gap: 10px; margin-bottom: 10px; }
.xy-row:last-child { margin-bottom: 0; }
.xy-row label { width: 15px; color: #00bcd4; font-weight: bold; text-align: center; font-size: 15px;}
.xy-row input[type="range"] { flex-grow: 1; accent-color: #00bcd4; padding: 0; cursor: pointer; height: 6px; }
.xy-row input[type="number"] { width: 75px; padding: 6px; margin: 0; text-align: center; font-weight: bold;}

button {background:linear-gradient(135deg, #00bcd4, #007c91); color:#fff; padding:15px; border:none; border-radius:8px; cursor:pointer; width:100%; font-size:18px; font-weight:bold; transition:0.3s;}
button:hover {background:linear-gradient(135deg, #0097a7, #005b6b); box-shadow:0 4px 10px rgba(0,188,212,0.4);}

/* Contenitore per i pulsanti */
.btn-group { display: flex; gap: 15px; margin-top: 15px; }
.btn-group button { flex: 1; margin-top: 0; }

.btn-secondary {background:linear-gradient(135deg, #455a64, #263238); font-size: 16px;}
.btn-secondary:hover {background:linear-gradient(135deg, #546e7a, #37474f); box-shadow:0 4px 10px rgba(69,90,100,0.4);}

.btn-reset {background:linear-gradient(135deg, #e53935, #b71c1c); margin-top: 15px;}
.btn-reset:hover {background:linear-gradient(135deg, #ef5350, #c62828); box-shadow:0 4px 10px rgba(229,57,53,0.4);}

#msg {text-align:center; font-weight:bold; margin-bottom:15px; min-height:20px;}
.msg-success {color: #4caf50;} .msg-error {color: #f44336;}
#serial-out { background:#0d0d0d; color:#00ff88; font-family:'Consolas','Courier New',monospace; font-size:10px; padding:8px; height:200px; overflow-y:auto; white-space:pre; }
</style>
</head>
<body>
<div class="header">
    <h2>Dashboard++ Setup</h2>
    <p class="subtitle">Device Configuration & UI Layout Manager</p>
</div>
<div id="msg"></div>
<div id="form-container">Loading settings...</div>

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

<button class="btn-reset" onclick="factoryReset()">⚠️ Factory Reset</button>

<script>
const configMap = [
    {
        title: "⚙️ System & General",
        items: [
            { id: "ENABLE_DEMO_MODE", label: "Enable Sensor Demo Mode" },
            { id: "TARGET_FPS", label: "Target Refresh Rate (FPS)" },
            { id: "ENABLE_POWER_SENSE", label: "Enable Power Sensing" },
            { type: "button", label: "🕒 Sync Device Time", action: "syncTime" },
            { id: "SHOW_FPS_COUNTER_DEFAULT", label: "Show FPS Counter on Boot" },
            { id: "ENABLE_DYNAMIC_CPU", label: "Enable Dynamic CPU Frequency" },
            { 
                id: "MANUAL_CPU_FREQ", 
                label: "Manual CPU Frequency", 
                type: "select",
                options: [
                    { value: 80, label: "80 MHz" },
                    { value: 160, label: "160 MHz" },
                    { value: 240, label: "240 MHz" }
                ]
            }
        ]
    },
    {
        title: "🎨 Display & Colors",
        items: [
            { id: "BOOT_TIME_MS", label: "Boot Loading Duration (ms)", type: "number" },
            { id: "SHUTDOWN_TIME_MS", label: "Shutdown Loading Duration (ms)", type: "number" },
            { id: "DISPLAY_ROTATION", label: "Screen Rotation (0-3)" },
            { id: "DISPLAY_WIDTH", label: "Display Width (px)" },
            { id: "DISPLAY_HEIGHT", label: "Display Height (px)" },
            { id: "SPI_BUS_SPEED", label: "SPI Bus Frequency (Hz)" },
            { id: "BACKLIGHT_BRIGHTNESS", label: "Backlight Brightness", type: "range", min: 0, max: 100, step: 1 },
            { id: "NIGHT_MODE_START_HOUR", label: "Night Mode Start Time (Hour)" },
            { id: "NIGHT_MODE_END_HOUR", label: "Night Mode End Time (Hour)" },
            { id: "TEMP_BAR_MIN", label: "Temp Bar Minimum (°C)" },
            { id: "TEMP_BAR_MAX", label: "Temp Bar Maximum (°C)" },
            { id: "TEMP_WARN_RED", label: "Engine Temp Red Zone (°C)" },
            { id: "TEMP_WARN_YEL", label: "Engine Temp Yellow Zone (°C)" },
            { id: "FUEL_WARN_YEL", label: "Fuel Low Yellow Zone (%)" },
            { id: "FUEL_WARN_RED", label: "Fuel Low Red Zone (%)" },
            { id: "COLOR_TEMP_NORM", label: "Engine Temp Normal Color", type: "color" },
            { id: "COLOR_TEMP_WARN", label: "Engine Temp Warning Color", type: "color" },
            { id: "COLOR_TEMP_CRIT", label: "Engine Temp Critical Color", type: "color" },
            { id: "COLOR_FUEL_NORM", label: "Fuel Level Normal Color", type: "color" },
            { id: "COLOR_FUEL_WARN", label: "Fuel Level Warning Color", type: "color" },
            { id: "COLOR_FUEL_CRIT", label: "Fuel Level Critical Color", type: "color" }
        ]
    },
    {
        title: "🏍️ Sensors & Vehicle Tuning",
        items: [
            { type: "touch-table", label: "Fuel Level Mapping Table", pointCount: "FUEL_TOUCH_POINTS", arrayId: "touchTable" },
            { id: "WHEEL_CIRCUMFERENCE_MM", label: "Wheel Circumference (mm)" },
            { id: "MIN_SPEED_THRESHOLD", label: "Minimum Speed Threshold (km/h)" },
            { id: "MAX_SPEED_DELTA_KMH", label: "Max Deviation Hall vs GPS (km/h)" },
            { id: "FUEL_FILTER_ALPHA", label: "Fuel Sensor Smoothing Alpha" },
            { id: "NTC_R_BALANCE", label: "Temperature NTC Balance Resistor (Ohms)" },
            { id: "NTC_BETA", label: "Temperature NTC Beta Value" },
            { id: "MIN_SATELLITES", label: "Minimum GPS Satellites Required" },
            { id: "OPTIMAL_SATELLITES", label: "Optimal GPS Satellites Count" },
        ]
    },
    {
        title: "⏱️ Acceleration Timer",
        items: [
            { id: "ACCEL_START_SPEED", label: "Timer Start Speed (km/h)" },
            { id: "ACCEL_TARGET_SPEED", label: "Timer Target Speed (km/h)" },
            { id: "ACCEL_BADGE_LINE1", label: "Badge Top Text (e.g., '0-50')" },
            { id: "ACCEL_BADGE_LINE2", label: "Badge Bottom Text (e.g., 'km/h')" }
        ]
    },
    {
        title: "✍️ Custom Signatures",
        items: [
            { id: "SPLASH_SIGNATURE", label: "Boot Screen Signature" },
            { id: "REBOOT_SIGNATURE", label: "Reboot Screen Signature" },
            { id: "DASHBOARD_SIGNATURE", label: "Main Dashboard Watermark" }
        ]
    },
    {
        title: "📐 UI Layout (Main Positions)",
        items: [
            { type: 'xy', idX: "BIG_CENTER_X", idY: "BIG_CENTER_Y", label: "Viewport Center" },
            { id: "SHOW_ELEMENT_BOUNDS", label: "Show UI Element Bounds (Debug)" },
            { id: "ENABLE_CIRCLE_TEST", label: "Show Test Circles" }
        ]
    },
    {
        title: "📏 UI Elements Offsets (X / Y)",
        items: [
            { type: 'xy', idX: "OFFSET_BIG_TIME_X", idY: "OFFSET_BIG_TIME_Y", label: "Clock Offset" },
            { type: 'xy', idX: "OFFSET_BIG_DATE_X", idY: "OFFSET_BIG_DATE_Y", label: "Date Offset" },
            { type: 'xy', idX: "OFFSET_BIG_SIGNATURE_X", idY: "OFFSET_BIG_SIGNATURE_Y", label: "Signature Offset" },
            { type: 'xy', idX: "OFFSET_BIG_SPEED_NUM_X", idY: "OFFSET_BIG_SPEED_NUM_Y", label: "Main Speed Number Offset" },
            { type: 'xy', idX: "OFFSET_BIG_SPEED_UNIT_X", idY: "OFFSET_BIG_SPEED_UNIT_Y", label: "Speed Unit (KM/H) Offset" },
            { type: 'xy', idX: "OFFSET_BIG_ODO_X", idY: "OFFSET_BIG_ODO_Y", label: "Odometer Offset" },
            { type: 'xy', idX: "OFFSET_BIG_SAT_X", idY: "OFFSET_BIG_SAT_Y", label: "Satellites Offset" },
            { type: 'xy', idX: "OFFSET_BIG_TMR_X", idY: "OFFSET_BIG_TMR_Y", label: "Acceleration Timer Offset" },
            { type: 'xy', idX: "OFFSET_BIG_BAT_X", idY: "OFFSET_BIG_BAT_Y", label: "Battery Offset" },
            { type: 'xy', idX: "SIDEBAR_LEFT_X", idY: "SIDEBAR_LEFT_Y", label: "Left Sidebar (Temp) Pos" },
            { type: 'xy', idX: "SIDEBAR_RIGHT_X", idY: "SIDEBAR_RIGHT_Y", label: "Right Sidebar (Fuel) Pos" },
            { type: 'xy', idX: "OFFSET_HALL_ICON_X", idY: "OFFSET_HALL_ICON_Y", label: "Hall Sensor Icon Offset" },
            { type: 'xy', idX: "OFFSET_GPS_ICON_X", idY: "OFFSET_GPS_ICON_Y", label: "GPS Sensor Icon Offset" },
            { type: 'xy', idX: "OFFSET_WHEEL_ICON_X", idY: "OFFSET_WHEEL_ICON_Y", label: "Wheel Sensor Icon Offset" },
            { type: 'xy', idX: "OFFSET_WIFI_ICON_X", idY: "OFFSET_WIFI_ICON_Y", label: "WiFi Icon Offset" },
            { type: 'xy', idX: "OFFSET_INST_KML_X", idY: "OFFSET_INST_KML_Y", label: "Instant KM/L Offset" },
            { type: 'xy', idX: "OFFSET_AVG_KML_X", idY: "OFFSET_AVG_KML_Y", label: "Average KM/L Offset" },
            { type: 'xy', idX: "OFFSET_BIG_FPS_X", idY: "OFFSET_BIG_FPS_Y", label: "FPS Counter Offset" }
        ]
    }
];

fetch('/api/config').then(r=>r.json()).then(d=>{
    let html = '';
    let processedKeys = new Set();
    
    const maxW = d.DISPLAY_WIDTH || 480;
    const maxH = d.DISPLAY_HEIGHT || 320;

    configMap.forEach(group => {
        let groupHtml = `<div class="details-content">`;
        let hasItems = false;

        group.items.forEach(item => {
            let iType = item.type || 'standard';
            
            if (iType === 'select') {
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
                        <select id="${item.id}" style="width:100%; padding:10px; margin-top:6px; background:#2c2c2c; color:#fff; border:1px solid #444; border-radius:6px; box-sizing:border-box; font-size:14px; transition: border 0.3s;">
                            ${opts}
                        </select>
                    </div>`;
                }
            } else if (iType === 'color') {
                if(d.hasOwnProperty(item.id)) {
                    hasItems = true;
                    processedKeys.add(item.id);
                    groupHtml += `
                    <div class="form-group">
                        <label>${item.label}</label>
                        <input type="color" id="${item.id}" value="${d[item.id]}">
                    </div>`;
                }
            } else if (iType === 'button') {
                hasItems = true;
                groupHtml += `
                <div class="card" style="grid-column: 1 / -1; padding: 8px;">
                    <button class="btn-secondary" onclick="${item.action}()" style="margin:0; font-size:14px; padding:10px;">${item.label}</button>
                </div>`;
            } else if (iType === 'range') {
                if(d.hasOwnProperty(item.id)) {
                    hasItems = true;
                    processedKeys.add(item.id);
                    let val = d[item.id];
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
            } else if (iType === 'touch-table') {
                hasItems = true;
                let count = d[item.pointCount] || 8;
                processedKeys.add(item.pointCount);
                processedKeys.add(item.arrayId);
                let arr = d[item.arrayId] || [];
                let tableHtml = `
                <div class="card" style="grid-column:1/-1; padding:16px;">
                    <h4 style="margin:0 0 8px 0;color:#fff;font-size:14px;">${item.label}</h4>
                    <label style="font-size:13px;color:#aaa;">Number of points (tank capacity + 1):</label>
                    <div style="display:flex; align-items:center; gap:8px; margin-bottom:10px;">
                        <input type="number" id="${item.pointCount}" value="${count}" min="2" max="20" style="width:80px;">
                        <button onclick="save();setTimeout(function(){location.reload()},500)" style="padding:6px 12px; font-size:12px; background:#455a64; color:#fff; border:none; border-radius:4px; cursor:pointer;">↻ Refresh</button>
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
            } else if (iType === 'xy') {
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
                    
                    if(isBool) {
                        groupHtml += `<div class="card checkbox-card"><label>${item.label}</label><input type="${type}" id="${item.id}" ${checked}></div>`;
                    } else {
                        groupHtml += `<div class="card"><label>${item.label}</label><input type="${type}" id="${item.id}" value="${val}" step="any"></div>`;
                    }
                }
            }
        });
        
        groupHtml += `</div>`;
        if(hasItems) html += `<details><summary>${group.title}</summary>${groupHtml}</details>`;
    });

    let fallbackHtml = `<div class="details-content">`;
    let hasFallback = false;
    let hiddenKeys = new Set(["ENABLE_SLEEP_AFTER_REBOOT"]);
    for(let k in d){
        if(!processedKeys.has(k) && !hiddenKeys.has(k)) {
            hasFallback = true;
            let val = d[k]; let type = 'text'; let checked = ''; let isBool = (typeof val === 'boolean');
            if(isBool){ type = 'checkbox'; checked = val ? 'checked' : ''; val = 'true'; }
            else if(typeof val === 'number') type = 'number';
            
            if(isBool){
                fallbackHtml += `<div class="card checkbox-card"><label>${k} (Unmapped)</label><input type="${type}" id="${k}" ${checked}></div>`;
            } else {
                fallbackHtml += `<div class="card"><label>${k} (Unmapped)</label><input type="${type}" id="${k}" value="${val}" step="any"></div>`;
            }
        }
    }
    fallbackHtml += `</div>`;
    if(hasFallback) html += `<details><summary>🔧 Additional System Settings</summary>${fallbackHtml}</details>`;

    html = `<details id="serial-panel">
    <summary>📟 Serial Monitor</summary>
    <div id="serial-out">Waiting for data...</div>
    </details>` + html;
    document.getElementById('form-container').innerHTML = html;

    document.getElementById('serial-panel').addEventListener('toggle', function() {
      if (this.open) {
        serialPollTimer = setInterval(pollSerial, 200);
        pollSerial();
      } else {
        clearInterval(serialPollTimer);
        serialPollTimer = null;
      }
    });

    document.getElementById('serial-out').addEventListener('wheel', function() {
      let el = this;
      clearTimeout(el._scrollTimer);
      el._scrollTimer = setTimeout(function() {
        serialAutoScroll = (el.scrollTop + el.clientHeight >= el.scrollHeight - 30);
      }, 200);
    });

    setTimeout(syncTime, 1000);
});

let autosaveTimer = null;
document.getElementById('form-container').addEventListener('input', function(e) {
    if(e.target.type === 'file') return;
    clearTimeout(autosaveTimer);
    autosaveTimer = setTimeout(save, 2000);
});

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
            if(!confirm("Are you sure you want to restore these settings? This will overwrite the current configuration and reboot the device.")) {
                event.target.value = "";
                return;
            }
            sendConfigToDevice(jsonConfig, "Restoring configuration...", "Backup restored successfully! Device is rebooting...");
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
    
    el.innerHTML += html;
    if (serialAutoScroll) el.scrollTop = el.scrollHeight;
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
</script>
</body>
</html>
)rawliteral";

void webServerTask(void *pvParameters) {
  WiFi.mode(WIFI_AP);
  WiFi.setTxPower(WIFI_POWER_MINUS_1dBm);
  WiFi.softAP("Dashboard_Config", "12345678");
  logPrintf("AP: Dashboard_Config\n");
  logPrintf("IP: %s\n", WiFi.softAPIP().toString().c_str());

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
    server.send(200, "application/json", "{\"status\":\"ok\"}");
    forceFullRedraw = true;
    analogWrite(BL_DISPLAY, map(BACKLIGHT_BRIGHTNESS, 0, 100, 0, 255));
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

  server.begin();

  unsigned long lastClientTime = millis();

  for (;;) {
    server.handleClient();

    if (WiFi.softAPgetStationNum() > 0) {
      lastClientTime = millis();
    } else if (millis() - lastClientTime > 60000) {
      logPrintf("WiFi timeout, disabled\n");
      WiFi.mode(WIFI_OFF);
      vTaskDelete(NULL);
    }

    vTaskDelay(pdMS_TO_TICKS(10));
  }
}
