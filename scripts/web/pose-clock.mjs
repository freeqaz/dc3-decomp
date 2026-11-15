#!/usr/bin/env node
// The pose worker must survive any camera clock (W-35).
//
//   node scripts/web/pose-clock.mjs --port P --frames DIR [--delegate CPU|GPU] [--n 200]
//   node scripts/web/pose-clock.mjs --port P --frames DIR --sabotage
//
// Drives the shipped native/web/pose/pose_worker.js (served by
// native/web/server.py on port P) with frames whose CAPTURE timestamps are
// chosen here, the way pumpStream hands them over, and requires a pose message
// for every frame and no worker error.  DIR holds PNG frames with a person in
// them (scripts/web/make_pose_fixtures.sh OUT -> OUT/frames).
//
// Why: VideoFrame.timestamp on Chromium is the time since boot.  tasks-vision
// returns result timestamps as 32-bit milliseconds, so on a machine up more
// than 24.86 days the first frame with a person in it broke the MediaPipe graph
// for good -- "Packet timestamp mismatch ... free_memory" on every later frame,
// thousands of console errors, nobody ever tracked (seen on a Mac, 2026-11-14).
//
// --sabotage serves the worker with the clock it had before the fix (capture
// time passed straight to MediaPipe) and requires the long-uptime scenarios to
// FAIL and the short-uptime ones to pass: the proof that this check can fail.
//
// Exit: 0 pass · 1 a scenario failed (or, with --sabotage, the old clock did
// not fail where it must) · 2 usage.
import { readFileSync, readdirSync } from 'fs';
import { join } from 'path';
import { chromium } from 'playwright';

const argv = process.argv.slice(2);
const arg = (k, d) => { const i = argv.indexOf('--' + k); return i >= 0 ? argv[i + 1] : d; };
const flag = (k) => argv.includes('--' + k);
const port = +arg('port', 0), framesDir = arg('frames'), n = +arg('n', 200);
const delegate = arg('delegate', 'CPU'), only = arg('only', null), sabotage = flag('sabotage');
if (!port || !framesDir) { console.error('usage: pose-clock.mjs --port P --frames DIR [--delegate CPU|GPU] [--n 200] [--sabotage]'); process.exit(2); }
const files = readdirSync(framesDir).filter((f) => f.endsWith('.png')).sort().slice(0, 15);
if (!files.length) { console.error(`no PNG frames in ${framesDir}`); process.exit(2); }
const origin = `http://127.0.0.1:${port}`;

// The clock before the fix, put back by --sabotage.
const NEW_CLOCK = 'function mediapipeMs(captureMs) {';
const OLD_CLOCK = NEW_CLOCK + `
    if (captureMs <= lastTsMs) captureMs = lastTsMs + 1;
    lastTsMs = captureMs; mpClockMs = captureMs; return captureMs;`;

// scenario: frame k -> { us: capture timestamp in microseconds, blank: nobody in view }
// `long`: the capture clock is past 2^31 ms at some frame with a person in it.
const DAY = 86400e6, STEP = 33333;
const MAC = 4166704915260;                 // the Mac's clock in the report, 48.2 days
const scenarios = [
    ['just booted, person',              false, (k) => ({ us: 1e6 + k * STEP, blank: false })],
    ['just booted, nobody',              false, (k) => ({ us: 1e6 + k * STEP, blank: true })],
    ['24 days up, person',               false, (k) => ({ us: 24 * DAY + k * STEP, blank: false })],
    ['25 days up, person',               true,  (k) => ({ us: 25 * DAY + k * STEP, blank: false })],
    ['48 days up, person',               true,  (k) => ({ us: MAC + k * STEP, blank: false })],
    ['48 days up, nobody',               false, (k) => ({ us: MAC + k * STEP, blank: true })],
    ['48 days up, nobody then a person', true,  (k) => ({ us: MAC + k * STEP, blank: k < 40 })],
    ['48 days up, person comes and goes', true, (k) => ({ us: MAC + k * STEP, blank: Math.floor(k / 20) % 2 === 1 })],
    ['passes 2^31 ms mid-run, person',   true,  (k) => ({ us: 2147483647000 - 100 * STEP + k * STEP, blank: false })],
    ['400 days up, person',              true,  (k) => ({ us: 400 * DAY + k * STEP, blank: false })],
    ['timestamps repeat',                true,  (k) => ({ us: MAC + Math.floor(k / 2) * STEP, blank: false })],
    ['timestamps 1 us apart',            true,  (k) => ({ us: MAC + k, blank: false })],
    ['clock jumps back',                 true,  (k) => ({ us: MAC + (k % 50) * STEP, blank: false })],
    ['clock jumps a day ahead',          false, (k) => ({ us: 1e6 + k * STEP + (k >= 60 ? DAY : 0), blank: false })],
];

const gpuArgs = delegate === 'GPU'
    ? ['--enable-unsafe-webgpu', '--use-angle=vulkan', '--enable-features=Vulkan,VulkanFromANGLE'] : ['--disable-gpu'];
const browser = await chromium.launch({ headless: true, args: ['--no-sandbox', '--ozone-platform=x11', '--disable-extensions', ...gpuArgs] });
let bad = 0, ran = 0, workerPatched = 0;
for (const [name, long, fn] of scenarios) {
    if (only && !name.includes(only)) continue;
    const page = await browser.newPage();
    await page.route(`${origin}/__clock.html*`, (route) => route.fulfill({ contentType: 'text/html',
        headers: { 'Cross-Origin-Opener-Policy': 'same-origin', 'Cross-Origin-Embedder-Policy': 'require-corp' },
        body: '<!doctype html><title>pose clock</title>' }));
    await page.route(`${origin}/__f/**`, (route) => route.fulfill({ contentType: 'image/png',
        headers: { 'Cross-Origin-Resource-Policy': 'same-origin' },
        body: readFileSync(join(framesDir, new URL(route.request().url()).pathname.split('/').pop())) }));
    if (sabotage) await page.route(/\/pose\/pose_worker\.js(\?.*)?$/, async (route) => {
        const res = await route.fetch();
        const src = await res.text();
        if (src.split(NEW_CLOCK).length !== 2) { console.error('--sabotage: mediapipeMs not found in pose_worker.js'); process.exit(2); }
        workerPatched++;
        await route.fulfill({ response: res, body: src.replace(NEW_CLOCK, OLD_CLOCK) });
    });
    await page.goto(`${origin}/__clock.html?${new URLSearchParams({ pose: 'off', pose_delegate: delegate, pose_persons: '2' })}`);
    const plan = Array.from({ length: n }, (_, k) => fn(k));
    const out = await page.evaluate(async ({ plan, files }) => {
        window.__dc3PoseNoAutoInit = true;
        const mod = await import('/pose/pose_input.js');
        const { worker, ready } = await mod.createWorker(await mod.resolveSources());
        const info = await ready;
        const blobs = [];
        for (const f of files) blobs.push(await (await fetch('/__f/' + f)).blob());
        let ok = 0, errors = 0, first = null, firstAt = -1, seen = 0, people = 0, lastTs = null, notUp = 0, maxTs = 0;
        for (let k = 0; k < plan.length; k++) {
            const p = plan[k];
            let bmp;
            if (p.blank) {
                const c = new OffscreenCanvas(640, 360), g = c.getContext('2d');
                g.fillStyle = '#333'; g.fillRect(0, 0, 640, 360);
                bmp = c.transferToImageBitmap();
            } else {
                people++;
                bmp = await createImageBitmap(blobs[k % blobs.length], { colorSpaceConversion: 'none', premultiplyAlpha: 'none' });
            }
            const captureSec = p.us / 1e6;      // pumpStream's arithmetic
            const msg = await new Promise((res) => {
                const h = (ev) => { if (ev.data.type === 'pose' || ev.data.type === 'error') { worker.removeEventListener('message', h); res(ev.data); } };
                worker.addEventListener('message', h);
                worker.postMessage({ type: 'frame', image: bmp, tsMs: captureSec * 1000, captureSec }, [bmp]);
            });
            if (msg.type === 'error') { errors++; if (first === null) { first = String(msg.message).replace(/\s+/g, ' ').slice(0, 200); firstAt = k; } }
            else {
                ok++;
                if (msg.count > 0) seen++;
                if (lastTs !== null && !(msg.tsMs > lastTs)) notUp++;
                lastTs = msg.tsMs; maxTs = Math.max(maxTs, msg.tsMs);
            }
        }
        worker.terminate();
        return { delegate: info.delegate, ok, errors, first, firstAt, seen, people, notUp, maxTs };
    }, { plan, files });
    await page.close();
    ran++;
    // A frame is "seen" when the model finds the person; most are, not all.
    const tracked = out.people === 0 ? out.seen === 0 : out.seen >= out.people * 0.6;
    const clean = out.errors === 0 && out.ok === n && out.notUp === 0 && tracked;
    const pass = sabotage ? (long ? out.errors > 0 : clean) : clean;
    if (!pass) bad++;
    console.log(`${pass ? 'ok  ' : 'FAIL'} [${out.delegate}] ${name}: ${out.ok}/${n} poses, ${out.seen}/${out.people} person frames tracked, ` +
        `${out.errors} errors` + (out.errors ? ` (first at frame ${out.firstAt}: ${out.first})` : '') +
        (out.notUp ? `, ${out.notUp} timestamps not increasing` : '') +
        (sabotage ? (long ? '  [old clock: must fail]' : '  [old clock: must pass]') : `, clock ended at ${Math.round(out.maxTs)} ms`));
}
await browser.close();
if (sabotage && !workerPatched) { console.error('--sabotage: the worker script was never intercepted; nothing was tested'); process.exit(1); }
if (!ran) { console.error('no scenario ran'); process.exit(2); }
console.log(bad ? `FAILED: ${bad} of ${ran} scenarios` : (sabotage ? `sabotage control ok: the old clock fails every long-uptime scenario (${ran} scenarios)` : `ok: ${ran} scenarios`));
process.exit(bad ? 1 : 0);
