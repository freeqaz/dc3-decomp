#!/usr/bin/env node
/**
 * DC3 Web — lightweight load/network profiler (rb3 netperf-suite's little sibling).
 *
 * Boots the web build, records every network request (bytes on the wire via CDP
 * encodedDataLength) and every screen entry (`will enter '<name>'` console log),
 * then prints a per-phase table: wall time, request count, wire bytes.
 *
 * Owns the browser launch (unlike lib/core.mjs's launchBrowser) so the CDP
 * Network session and console capture are attached BEFORE first navigation —
 * nothing is missed, and throttling applies from byte zero.
 *
 * Usage:
 *   node scripts/web/netperf-lite.mjs [--port 8420] [--target song_select_screen]
 *        [--throttle low|normal]   # low = 50 Mbit / 30 ms RTT, normal = 200/15
 *        [--debug]                 # profile the debug build (?debug=true)
 *        [--json out.json]
 */

import { writeFileSync } from 'fs';
import { chromium } from 'playwright';
import { parseArgs, waitForServer, createCapture, navigateTo, cleanup } from './lib/core.mjs';

const opts = parseArgs({
    port:     { type: 'number', default: 8420 },
    target:   { type: 'string', default: 'main_screen' },
    throttle: { type: 'string' },
    debug:    { type: 'flag' },
    json:     { type: 'string' },
    verbose:  { type: 'flag' },
});

const PROFILES = {
    low:    { downloadThroughput: 50e6 / 8,  uploadThroughput: 10e6 / 8, latency: 30 },
    normal: { downloadThroughput: 200e6 / 8, uploadThroughput: 50e6 / 8, latency: 15 },
};

let browser;
try {
    await waitForServer(opts.port);
    browser = await chromium.launch({
        headless: !process.env.DISPLAY,
        args: [
            '--no-sandbox', '--enable-unsafe-webgpu', '--use-angle=vulkan',
            '--enable-features=Vulkan,VulkanFromANGLE,WebAssemblyJSPromiseIntegration',
            '--ozone-platform=x11', '--disable-extensions',
            '--disable-background-networking', '--disable-default-apps',
            '--disable-sync', '--mute-audio',
            '--autoplay-policy=no-user-gesture-required',
        ],
    });
    const context = await browser.newContext({ viewport: { width: 1280, height: 720 } });
    const page = await context.newPage();

    const cdp = await context.newCDPSession(page);
    await cdp.send('Network.enable');
    if (opts.throttle) {
        const p = PROFILES[opts.throttle];
        if (!p) throw new Error(`unknown throttle profile: ${opts.throttle}`);
        await cdp.send('Network.emulateNetworkConditions', { offline: false, ...p });
    }

    const t0 = Date.now();
    const requests = [];           // { url, bytes, t }  (t = loadingFinished time)
    const reqUrls = new Map();     // requestId -> url
    cdp.on('Network.responseReceived', (e) => reqUrls.set(e.requestId, e.response.url));
    cdp.on('Network.loadingFinished', (e) => {
        const url = reqUrls.get(e.requestId) || '?';
        requests.push({ url, bytes: e.encodedDataLength, t: Date.now() - t0 });
    });

    const cap = createCapture(page, { verbose: opts.verbose });
    const screens = [];            // { name, t }
    page.on('console', (msg) => {
        const m = msg.text().match(/will enter '([^']+)'/);
        if (m) screens.push({ name: m[1], t: Date.now() - t0 });
    });

    const url = `http://127.0.0.1:${opts.port}/?fast_boot=1${opts.debug ? '&debug=true' : ''}`;
    await page.goto(url, { waitUntil: 'domcontentloaded', timeout: 30000 });

    await navigateTo(page, cap, opts.target);
    // Let trailing fetches drain
    await page.waitForTimeout(4000);

    // ---- report: aggregate requests into inter-screen phases -------------
    const phases = [];
    let prev = { name: 'boot', t: 0 };
    for (const s of [...screens, { name: '(end)', t: Date.now() - t0 }]) {
        const rs = requests.filter((r) => r.t > prev.t && r.t <= s.t);
        phases.push({
            phase: `${prev.name} -> ${s.name}`,
            wallMs: s.t - prev.t,
            reqs: rs.length,
            mb: rs.reduce((a, r) => a + r.bytes, 0) / 1e6,
        });
        prev = s;
    }
    const totalMb = requests.reduce((a, r) => a + r.bytes, 0) / 1e6;
    console.log(`\n== netperf-lite  throttle=${opts.throttle || 'none'}  build=${opts.debug ? 'debug' : 'release'}  total ${requests.length} req  ${totalMb.toFixed(1)} MB wire ==`);
    for (const p of phases) {
        console.log(`  ${p.phase.padEnd(38)} wall=${(p.wallMs / 1000).toFixed(1).padStart(6)}s  req=${String(p.reqs).padStart(4)}  ${p.mb.toFixed(1).padStart(7)} MB`);
    }
    // Top-10 heaviest requests
    const top = [...requests].sort((a, b) => b.bytes - a.bytes).slice(0, 10);
    console.log('  -- heaviest requests --');
    for (const r of top) {
        console.log(`  ${(r.bytes / 1e6).toFixed(2).padStart(7)} MB  t=${(r.t / 1000).toFixed(1).padStart(5)}s  ${r.url.replace(`http://127.0.0.1:${opts.port}`, '')}`);
    }
    if (opts.json) {
        writeFileSync(opts.json, JSON.stringify({ throttle: opts.throttle || null, phases, requests, screens }, null, 1));
        console.log(`  wrote ${opts.json}`);
    }
    process.exit(0);
} catch (err) {
    console.error('FAIL:', err.message);
    process.exit(1);
} finally {
    await cleanup(browser);
}
