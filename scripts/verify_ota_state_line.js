#!/usr/bin/env node
/*
 * verify_ota_state_line.js — what the Firmware Update card's live state line
 * decides, checked against the source that actually ships.
 *
 * The line under the Firmware Update card is a chain of branches over the reply
 * of GET /api/ota/check (`state`, `screen`, `quiet`, `quiet_silent_ms`,
 * `percent`, `bytes`, `error`, `status`, `hold_to_restart_ms`, …). Getting a
 * branch order wrong is how a stall looks like progress, or a refusal looks like
 * a reboot, so the chain is worth a check that outlives the session that wrote it.
 *
 * The functions are sliced out of src/webui.html at run time — this file holds no
 * copy of the logic, so it cannot drift into testing something the device never
 * served. Run:  node scripts/verify_ota_state_line.js
 *
 * Exit code 0 = every case matched, non-zero = at least one mismatch.
 */
'use strict';

const fs = require('fs');
const path = require('path');

const HTML = path.join(__dirname, '..', 'src', 'webui.html');

// Optional argument: check a different copy of the same page instead of the
// source - the usual one being the page a device is actually serving, which is
// the only way to catch a firmware image built before the UI was regenerated
// (src/webui_html_gz.h is generated, rule 15). curl is used rather than a Node
// http client so this script stays dependency-free like the other verifiers:
//   node scripts/verify_ota_state_line.js http://192.168.1.116/
let src;
if (process.argv[2]) {
    const { execFileSync } = require('child_process');
    src = execFileSync('curl', ['-s', '--compressed', process.argv[2]], { encoding: 'utf8', maxBuffer: 32 * 1024 * 1024 });
    console.log('reading the page from ' + process.argv[2] + ' (' + src.length + ' bytes)');
} else {
    src = fs.readFileSync(HTML, 'utf8');
}

// Slice the state-line block out of the page script: from its own banner comment
// to the next function after it. Both markers are part of the shipped file, so a
// rename that breaks the slice fails loudly instead of testing nothing.
const START = '// --- the live update-state line';
const END = 'function otaFileChosen(';
const iStart = src.indexOf(START);
const iEnd = src.indexOf(END, iStart);
if (iStart < 0 || iEnd < 0) {
    console.error('SLICE FAILED: markers not found in src/webui.html');
    console.error('  start marker present: ' + (iStart >= 0) + ', end marker present: ' + (iEnd >= 0));
    process.exit(2);
}
const block = src.slice(iStart, iEnd);
console.log('sliced ' + block.length + ' bytes of the state-line block from ' +
            (process.argv[2] || 'src/webui.html'));

// A page with exactly two elements in it, plus the pull button the render
// re-enables. offsetParent is what the real code uses to tell "the card is
// collapsed or the page is hidden" from "visible", so the stub can drive it.
function makeHarness(opts) {
    opts = opts || {};
    const els = {
        'ota-state-line': { className: '', innerText: '', offsetParent: opts.visible === false ? null : {} },
        'ota-state-hint': { className: '', innerText: '', offsetParent: opts.visible === false ? null : {} },
        'otaPullBtn': { disabled: true, textContent: 'Checking...' }
    };
    const timers = [];
    const doc = {
        hidden: !!opts.hidden,
        getElementById: (id) => (Object.prototype.hasOwnProperty.call(els, id) ? els[id] : null)
    };
    const sandboxFn = new Function(
        'document', 'setTimeout', 'clearTimeout',
        block + '\n; return { otaStateRender: otaStateRender, otaStateSay: otaStateSay,' +
        ' setPush: function (p) { _otaPush = p; }, setLast: function (d) { _otaLastCheck = d; } };'
    );
    const api = sandboxFn(doc, (fn, ms) => { timers.push(ms); return timers.length; }, () => {});
    return { api: api, els: els, timers: timers };
}

const cases = [
    {
        name: 'idle unit, nothing running',
        d: { state: 'idle', screen: 'off', quiet: false, status: 'idle', enabled: true,
             current_version: '1.4.6', build_version: '1.4.6' },
        want: { cls: 'is-idle', text: 'idle - nothing is installing',
                hintHas: ['running 1.4.6', 'build 1.4.6', 'Check Updates is available'], active: false, delay: 5000 }
    },
    {
        name: 'idle unit, no manifest URL configured',
        d: { state: 'idle', screen: 'off', quiet: false, status: 'idle', enabled: false,
             current_version: '1.4.6', build_version: '1.4.6' },
        want: { cls: 'is-idle', hintHas: ['no manifest URL'], active: false, delay: 5000 }
    },
    {
        name: 'install in flight (quiet window open, bytes known)',
        d: { state: 'receiving', screen: 'updating', quiet: true, quiet_silent_ms: 120,
             percent: 40, bytes: 700000, bytes_total: 1750000, status: 'idle' },
        want: { cls: 'is-busy', text: 'installing: receiving 40% (684 KB of 1.67 MB)',
                hintHas: ['update screen', 'endpoints are paused'], active: true, delay: 1000 }
    },
    {
        name: 'stalled install (no bytes for 40 s) must warn, not look like progress',
        d: { state: 'receiving', screen: 'updating', quiet: true, quiet_silent_ms: 40000,
             percent: 92, bytes: 1610000, bytes_total: 1750000, status: 'idle' },
        want: { cls: 'is-warn', text: 'installing: receiving 92% (1.54 MB of 1.67 MB)',
                hintHas: ['no bytes for 40 s', 'sender may have gone away', '120 s'], active: true, delay: 1000 }
    },
    {
        name: 'refusal: reason sentence, and the running firmware is untouched',
        d: { state: 'error', screen: 'failed', quiet: false, error: 'sig_invalid',
             last_target_version: '1.4.7', status: 'signature does not match', percent: 100 },
        want: { cls: 'is-bad', text: 'last attempt refused: sig_invalid (v1.4.7)',
                hintHas: ['still installed', 'did not reboot', 'panel on the unit'], active: false, delay: 5000 }
    },
    {
        name: 'a frame on the panel without an install behind it (done/failed frames)',
        d: { state: 'idle', screen: 'done', quiet: false, hold_to_restart_ms: 3000, status: 'idle' },
        want: { cls: 'is-busy', text: 'the unit is showing its done frame',
                hintHas: ['hold the trip button 3 s', 'cannot reset the trip'], active: true, delay: 1000 }
    },
    {
        name: 'cloud pull waiting out the 60 s re-check floor',
        d: { state: 'idle', screen: 'off', quiet: false, status: 'waiting 48s before retry' },
        want: { cls: 'is-busy', text: 'pull: waiting 48s before retry',
                hintHas: ['manifest check reaches GitHub over TLS'], active: true, delay: 1000 }
    },
    {
        name: 'cloud pull answered up-to-date (and re-enables the button)',
        d: { state: 'idle', screen: 'off', quiet: false, status: 'up-to-date (v1.4.6)' },
        want: { cls: 'is-good', text: 'pull: up-to-date (v1.4.6)', active: false, delay: 5000,
                pullBtn: { disabled: false, textContent: 'Check Updates' } }
    },
    {
        name: 'cloud pull refused (no manifest URL) as a status sentence',
        d: { state: 'idle', screen: 'off', quiet: false, status: 'error: no OTA URL configured' },
        want: { cls: 'is-bad', text: 'pull: error: no OTA URL configured',
                hintHas: ['nothing was flashed'], active: false, delay: 5000 }
    },
    {
        name: 'this page is pushing, unit unreachable mid-transfer (must NOT read as lost)',
        push: { sent: 500000, total: 1750000, ver: '1.4.7' },
        d: null,
        want: { cls: 'is-busy', text: 'sending 488 KB of 1.67 MB (28%) - the unit cannot answer while the image streams, which is normal',
                hintHas: ['writes and hashes the image as it arrives', 'checks a signature if one came with the push'], active: true, delay: 1000 }
    },
    {
        name: 'this page is pushing and the unit did answer',
        push: { sent: 1200000, total: 1750000, ver: '1.4.7' },
        d: { state: 'writing', bytes: 1100000, bytes_total: 1750000, quiet: true, quiet_silent_ms: 200, screen: 'updating' },
        want: { cls: 'is-busy', text: 'sending 1.14 MB of 1.67 MB (68%) - the unit has 1.05 MB, state writing',
                active: true, delay: 1000 }
    },
    {
        name: 'idle card collapsed -> poll slows down',
        visible: false,
        d: { state: 'idle', screen: 'off', quiet: false, status: 'idle' },
        want: { active: false, delay: 8000 }
    },
    {
        name: 'hidden page with an install live -> keeps polling fast',
        hidden: true,
        d: { state: 'writing', screen: 'updating', quiet: true, quiet_silent_ms: 300, bytes_total: 1750000, bytes: 900000 },
        want: { active: true, delay: 1000 }
    }
];

let fails = 0;
for (const c of cases) {
    const h = makeHarness(c);
    if (c.push) h.api.setPush(c.push);
    if (c.d) h.api.setLast(c.d);
    const active = h.api.otaStateRender(c.d);
    const line = h.els['ota-state-line'];
    const hint = h.els['ota-state-hint'];
    const delay = h.timers[h.timers.length - 1];
    const problems = [];
    if (c.want.cls && line.className !== 'cfg-state ' + c.want.cls)
        problems.push('class: got "' + line.className + '" want "cfg-state ' + c.want.cls + '"');
    if (c.want.text && line.innerText !== c.want.text)
        problems.push('text:\n      got  "' + line.innerText + '"\n      want "' + c.want.text + '"');
    (c.want.hintHas || []).forEach(needle => {
        if (hint.innerText.indexOf(needle) < 0)
            problems.push('hint is missing "' + needle + '" (got: "' + hint.innerText + '")');
    });
    if (c.want.active !== undefined && active !== c.want.active)
        problems.push('active flag: got ' + active + ' want ' + c.want.active);
    if (c.want.delay && delay !== c.want.delay)
        problems.push('next poll: got ' + delay + ' ms want ' + c.want.delay + ' ms');
    if (c.want.pullBtn) {
        if (h.els['otaPullBtn'].disabled !== c.want.pullBtn.disabled)
            problems.push('pull button disabled: got ' + h.els['otaPullBtn'].disabled);
        if (h.els['otaPullBtn'].textContent !== c.want.pullBtn.textContent)
            problems.push('pull button label: got "' + h.els['otaPullBtn'].textContent + '"');
    }
    if (problems.length) {
        fails++;
        console.log('FAIL  ' + c.name);
        problems.forEach(p => console.log('      ' + p));
    } else {
        console.log('ok    ' + c.name +
                    '   [' + line.className.replace('cfg-state ', '') + ', next poll ' + delay + ' ms]');
    }
}

console.log(fails ? '\n' + fails + ' case(s) failed' : '\nall ' + cases.length + ' cases match');
process.exit(fails ? 1 : 0);
