#!/usr/bin/env node
// Screenshot the page's own states (W-8), no gameplay: the player page while
// loading and once the first screen is up, its error card (no WebGPU), and the
// ?dev harness. Fails (exit 1) if the player page shows any dev-harness element
// or the dev page hides one -- the check that the default page stays
// player-facing.
//
//   node scripts/web/page-states.mjs --port 8434 [--out DIR]
import { chromium } from 'playwright';
import { mkdirSync } from 'fs';
import { parseArgs, waitForServer } from './lib/core.mjs';

const opts = parseArgs({ port: { type: 'number', default: 8420 }, out: { type: 'string', default: '/tmp/dc3-web/page-states' },
    'cpu-throttle': { type: 'number', default: 1 } });
mkdirSync(opts.out, { recursive: true });
await waitForServer(opts.port);
const browser = await chromium.launch({
    headless: !process.env.DISPLAY,
    args: ['--no-sandbox', '--enable-unsafe-webgpu', '--use-angle=vulkan', '--ozone-platform=x11',
        '--enable-features=Vulkan,VulkanFromANGLE,WebAssemblyJSPromiseIntegration', '--mute-audio'],
});
const DEV_IDS = ['status', 'controls', 'toolbar', 'console'];
const visible = (page) => page.evaluate((ids) => Object.fromEntries(ids.concat(['loading']).map((id) => {
    const el = document.getElementById(id);
    if (!el) return [id, false];
    const cs = getComputedStyle(el);
    return [id, cs.display !== 'none' && cs.visibility !== 'hidden' && +cs.opacity > 0];
})), DEV_IDS);
let failures = 0;
const check = (cond, msg) => { console.log(`${cond ? 'ok  ' : 'FAIL'} ${msg}`); if (!cond) failures++; };

async function open(query, { noGpu = false } = {}) {
    const ctx = await browser.newContext({ viewport: { width: 1280, height: 720 } });
    const page = await ctx.newPage();
    if (noGpu) await page.addInitScript(() => Object.defineProperty(Navigator.prototype, 'gpu', { get: () => undefined }));
    // --cpu-throttle N (CDP): a slower machine, or a remote deploy whose boot
    // keeps the main thread busy right after the first screen.
    if (opts['cpu-throttle'] > 1) {
        const cdp = await ctx.newCDPSession(page);
        await cdp.send('Emulation.setCPUThrottlingRate', { rate: opts['cpu-throttle'] });
    }
    const logs = [], typed = [];
    page.on('console', (m) => { logs.push(m.text()); typed.push({ type: m.type(), text: m.text() }); });
    await page.goto(`http://127.0.0.1:${opts.port}/${query}`, { waitUntil: 'domcontentloaded' });
    return { ctx, page, logs, typed };
}
const until = async (pred, ms) => { const d = Date.now() + ms; while (Date.now() < d) { if (await pred()) return true; await new Promise((r) => setTimeout(r, 250)); } return false; };

try {
    // Player page: loading card, then the game once a screen is entered.
    {
        const { ctx, page, logs, typed } = await open('?pose=off');
        await new Promise((r) => setTimeout(r, 1500));
        let v = await visible(page);
        await page.screenshot({ path: `${opts.out}/player-loading.png` });
        check(DEV_IDS.every((id) => !v[id]), `player page hides the dev harness while loading ${JSON.stringify(v)}`);
        const up = await until(() => logs.some((l) => l.includes("will enter '")), 120000);
        check(up, 'player page reached a screen');
        // The page's decision (the `hidden` class) is immediate; the 0.4 s CSS
        // fade that follows can lag well past a fixed 1 s wait when the boot
        // keeps the main thread busy (np-dogfood3 read it mid-fade against the
        // deployed build), so wait for the fade with a deadline instead.
        const decided = await until(() => page.evaluate(() => document.getElementById('loading').classList.contains('hidden')), 3000);
        const tFade = Date.now();
        const faded = await until(async () => !(await visible(page)).loading, 10000);
        check(decided && faded, `loading card hidden once a screen is entered (class set: ${decided}, faded out ${faded ? 'after ' + ((Date.now() - tFade) / 1000).toFixed(1) + ' s' : 'NOT within 10 s'})`);
        v = await visible(page);
        check(DEV_IDS.every((id) => !v[id]), 'player page still hides the dev harness');
        const nLines = await page.evaluate(() => document.getElementById('console').childElementCount);
        check(nLines === 0, `player page keeps no console lines (${nLines})`);
        // Engine stderr is mostly trace (W-13): it must not all land as console.error.
        const trace = typed.filter((e) => /^DC3 Web: UIScreen /.test(e.text));
        check(trace.length > 0 && trace.every((e) => e.type === 'log'),
              `engine stderr trace reaches devtools as console.log (${trace.length} 'DC3 Web: UIScreen' lines, types ${[...new Set(trace.map((e) => e.type))].join(',') || '-'})`);
        const errs = typed.filter((e) => e.type === 'error' && !/^Failed to load resource/.test(e.text));
        check(errs.length < 20, `few console.error lines on the boot to the first screen (${errs.length}${errs.length ? ': ' + errs.slice(0, 3).map((e) => JSON.stringify(e.text.slice(0, 60))).join(' ') : ''})`);
        const levels = await page.evaluate(() => window.dc3StderrLevel ? [
            'FAIL: Assertion failed', 'AsyncFile: failed to open \'x\'', 'Aborted(RuntimeError: unreachable)',
            'timer error is large: 74.5', '[POSTPROC WARNING] x', "DC3 Web: UIScreen 'a' entering panel 'b'..."].map(window.dc3StderrLevel) : null);
        check(JSON.stringify(levels) === JSON.stringify(['error', 'error', 'error', 'warn', 'warn', 'log']),
              `stderr levels: failures stay errors, warnings warn, trace logs (${JSON.stringify(levels)})`);
        await page.screenshot({ path: `${opts.out}/player-running.png` });
        await ctx.close();
    }
    // Pose panel (W-13): no camera in this browser, so it shows its status and
    // button. It must sit in the bottom helpbar band clear of the hand menus'
    // corner items, and its text must stay inside its box.
    for (const [w, h] of [[1280, 720], [800, 450]]) {
        const ctx = await browser.newContext({ viewport: { width: w, height: h } });
        const page = await ctx.newPage();
        await page.goto(`http://127.0.0.1:${opts.port}/?x=1`, { waitUntil: 'domcontentloaded' });
        await until(() => page.evaluate(() => !!document.getElementById('pose-status') &&
            !/requesting|idle/.test((window.dc3PoseStats || {}).state || 'idle')), 20000);
        const measure = () => page.evaluate(() => {
            const r = (id) => document.getElementById(id).getBoundingClientRect();
            const c = r('dc3-canvas'), p = r('pose-panel'), st = document.getElementById('pose-status');
            const s = st.getBoundingClientRect();
            return { text: st.textContent, top: (p.top - c.top) / c.height, left: (p.left - c.left) / c.width,
                     right: (p.right - c.left) / c.width, bottom: (p.bottom - c.top) / c.height,
                     overflow: st.scrollWidth - st.clientWidth, sRight: s.right - p.right };
        });
        // A status with a long unbreakable word: the panel prints the raw error
        // name or message (np-dogfood3 saw "(NotSupportedError)" run into the
        // border); this one is longer than any box width.
        // ... and with the camera preview showing (a tracked player's layout).
        for (const forced of [null, 'Camera error (NotSupportedError:getUserMediaIsNotAvailableOnThisOrigin) — using a stand-in player', 'Tracking you ✓']) {
            if (forced) await page.evaluate((t) => { document.getElementById('pose-status').textContent = t; }, forced);
            if (forced === 'Tracking you ✓') await page.evaluate(() => { document.getElementById('pose-view').style.display = 'block'; });
            await new Promise((r) => setTimeout(r, 200));
            const m = await measure();
            const where = `${w}x${h} "${m.text.slice(0, 40)}": top ${m.top.toFixed(3)} left ${m.left.toFixed(3)} right ${m.right.toFixed(3)} bottom ${m.bottom.toFixed(3)}`;
            // (The synthetic long word may wrap to a third line on a small
            // window; only its containment is checked.)
            if (!forced || !/getUserMedia/.test(forced))
                check(m.top >= 0.86 && m.bottom <= 1.0 && m.left >= 0.25 && m.right <= 0.75, `pose panel in the bottom band, clear of the corner helpbar items (${where})`);
            check(m.overflow <= 1 && m.sRight <= 1, `pose status text stays in its box (${where}, overflow ${m.overflow}px)`);
        }
        await page.screenshot({ path: `${opts.out}/pose-panel-${w}x${h}.png` });
        await ctx.close();
    }
    // Error card: no WebGPU.
    {
        const { ctx, page } = await open('?pose=off', { noGpu: true });
        await new Promise((r) => setTimeout(r, 1500));
        const v = await visible(page);
        const text = await page.evaluate(() => (document.getElementById('loading') || {}).innerText || '');
        check(v.loading && /WebGPU not available/.test(text), `error card shown without WebGPU: "${text.split('\n')[0]}"`);
        await page.screenshot({ path: `${opts.out}/player-no-webgpu.png` });
        await ctx.close();
    }
    // Developer harness.
    {
        const { ctx, page, logs } = await open('?dev&pose=off');
        await until(() => logs.some((l) => l.includes("will enter '")), 120000);
        await new Promise((r) => setTimeout(r, 1000));
        const v = await visible(page);
        check(DEV_IDS.every((id) => v[id]) && !v.loading, `?dev shows the harness ${JSON.stringify(v)}`);
        const nLines = await page.evaluate(() => document.getElementById('console').childElementCount);
        check(nLines > 0 && nLines <= 500, `?dev console has lines, capped at 500 (${nLines})`);
        await page.screenshot({ path: `${opts.out}/dev.png` });
        await ctx.close();
    }
    // Pose files never come from a CDN on their own (W-13). Needs a camera:
    // the model is resolved after getUserMedia, so this browser has Chromium's
    // synthetic one. `full` is vendored by build.sh and must load locally;
    // `heavy` is not, and must fail on the page instead of fetching
    // storage.googleapis.com (the old page tracked on it, source "mixed").
    const camBrowser = await chromium.launch({
        headless: !process.env.DISPLAY,
        args: ['--no-sandbox', '--enable-unsafe-webgpu', '--use-angle=vulkan', '--ozone-platform=x11',
            '--enable-features=Vulkan,VulkanFromANGLE,WebAssemblyJSPromiseIntegration', '--mute-audio',
            '--use-fake-device-for-media-stream', '--use-fake-ui-for-media-stream'],
    });
    try {
        for (const [model, want] of [['full', 'local'], ['heavy', 'missing']]) {
            const page = await (await camBrowser.newContext({ viewport: { width: 1280, height: 720 } })).newPage();
            const foreign = [];
            page.on('request', (r) => { if (!/^(https?:\/\/127\.0\.0\.1[:/]|data:|blob:)/.test(r.url())) foreign.push(r.url()); });
            await page.goto(`http://127.0.0.1:${opts.port}/?fast_boot=1&pose_model=${model}`, { waitUntil: 'domcontentloaded' });
            const settled = () => page.evaluate(() => { const p = window.dc3PoseSummary && window.dc3PoseSummary(); return p && p.source ? p : null; });
            await until(settled, 30000);
            const p = (await settled()) || {};
            check(p.source === want && foreign.length === 0,
                `pose_model=${model}: source ${p.source} (want ${want}), state ${p.state}, ${foreign.length} cross-origin requests${foreign.length ? ' e.g. ' + foreign[0] : ''}`);
            await page.context().close();
        }
    } finally {
        await camBrowser.close();
    }
} finally {
    await browser.close();
}
console.log(`${failures ? 'FAIL' : 'PASS'} page states (${opts.out})`);
process.exit(failures ? 1 : 0);
