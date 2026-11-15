#!/usr/bin/env node
// Does the web build run on a WebGPU adapter restricted like an iPhone's?
// (PLAN W-29, docs/native/WEB.md "Phones: iPhone and iPad".)
//
// Boots the page with ?gpu_profile=<profile> (native/web/gpu_profile.js masks
// the adapter's optional features and limits), goes to song select, starts a
// song and plays it for --play seconds, then checks:
//
//   device       requestDevice was not refused, and the device the engine got
//                has no feature outside the profile (mobile: no
//                texture-compression-bc, no timestamp-query)
//   textures     no BC-format texture was created (the engine's software DXT
//                path decoded them), and the engine said so ("software DXT")
//   boot         main_screen reached, song started (game_screen + the song's
//                load completed), no page error, no WebGPU validation error
//                ("uncaptured error"), no device loss
//   picture      the canvas is not black on the main menu and in the song
//
// and prints what an iPhone would care about: GPU texture memory (from the
// createTexture descriptors), wasm heap, time to the menu and to the song, and
// game frames/s during the song.
//
//   node scripts/web/mobile-gpu.mjs --port 8471 [--profile mobile|bare|record]
//        [--song-index 3] [--play 20] [--out DIR] [--extra '&k=v']
//
// --profile record is the negative control: nothing is masked, the rig's
// adapter has texture-compression-bc, the engine uploads BC textures, and the
// device/texture checks must FAIL (exit 1). If they pass there, the check
// cannot see BC and proves nothing.
//
// Exit 0 = all checks passed; 1 = a check failed; 2 = the run itself broke.
// GPU: run outside the sandbox, under the GPU lock.
import { createRequire } from 'node:module';
import { mkdirSync, writeFileSync } from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { createCapture, navigateTo, selectSong, pressKey, waitForServer } from './lib/core.mjs';

const here = path.dirname(fileURLToPath(import.meta.url));
const require = createRequire(path.join(here, 'package.json'));
const { chromium } = require('playwright');

const argv = process.argv.slice(2);
const arg = (k, d) => { const i = argv.indexOf('--' + k); return i >= 0 ? argv[i + 1] : d; };
const port = +arg('port', 8471);
const profile = arg('profile', 'mobile');
const songIndex = +arg('song-index', 3);
const playSec = +arg('play', 20);
const extra = arg('extra', '');
const out = arg('out', `/tmp/dc3-web/mobile-gpu-${profile}`);
mkdirSync(out, { recursive: true });
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

let failures = 0;
const check = (cond, msg) => { console.log(`${cond ? 'ok  ' : 'FAIL'} ${msg}`); if (!cond) failures++; };

// Mean luma of the canvas (0..255) from a screenshot of it.
async function canvasLuma(page, name) {
    const buf = await page.locator('#dc3-canvas').screenshot({ path: path.join(out, name + '.png'), timeout: 10000 });
    return await page.evaluate(async (b64) => {
        const img = new Image();
        img.src = 'data:image/png;base64,' + b64;
        await img.decode();
        const c = document.createElement('canvas');
        c.width = 160; c.height = 90;
        const g = c.getContext('2d');
        g.drawImage(img, 0, 0, 160, 90);
        const d = g.getImageData(0, 0, 160, 90).data;
        let s = 0;
        for (let i = 0; i < d.length; i += 4) s += 0.299 * d[i] + 0.587 * d[i + 1] + 0.114 * d[i + 2];
        return +(s / (d.length / 4)).toFixed(1);
    }, buf.toString('base64'));
}

let browser;
try {
    await waitForServer(port);
    browser = await chromium.launch({
        headless: !process.env.DISPLAY,
        args: ['--no-sandbox', '--enable-unsafe-webgpu', '--use-angle=vulkan', '--ozone-platform=x11',
            '--enable-features=Vulkan,VulkanFromANGLE,WebAssemblyJSPromiseIntegration', '--mute-audio',
            '--autoplay-policy=no-user-gesture-required', '--disable-extensions',
            '--disable-background-networking'],
    });
    const ctx = await browser.newContext({ viewport: { width: 1280, height: 720 } });
    const page = await ctx.newPage();
    await page.addInitScript(() => {
        const grab = (res) => {
            try { const ex = (res.instance || res).exports || {}; for (const k in ex) if (ex[k] instanceof WebAssembly.Memory && !window.__dc3Mem) window.__dc3Mem = ex[k]; } catch (e) {}
            return res;
        };
        const is = WebAssembly.instantiateStreaming;
        if (is) WebAssembly.instantiateStreaming = (...a) => is.apply(WebAssembly, a).then(grab);
        const ii = WebAssembly.instantiate;
        WebAssembly.instantiate = (...a) => ii.apply(WebAssembly, a).then(grab);
    });
    const cap = createCapture(page, { verbose: false });
    let heapPeakMB = 0;
    const heapSampler = setInterval(async () => {
        try {
            const mb = await page.evaluate(() => (window.__dc3Mem ? window.__dc3Mem.buffer.byteLength / 1048576 : 0));
            if (mb > heapPeakMB) heapPeakMB = mb;
        } catch (e) { /* page busy */ }
    }, 1000);

    const t0 = Date.now();
    const url = `http://127.0.0.1:${port}/?fast_boot=1&pose=off&gpu_profile=${encodeURIComponent(profile)}${extra}`;
    console.log(`[mobile-gpu] ${url}`);
    await page.goto(url, { waitUntil: 'domcontentloaded', timeout: 30000 });

    // Boot is wall-clock bound (asset downloads, and the software DXT decode
    // this run exercises): wait up to 240 s for the first screen, then let
    // navigateTo (45 s for the same line) walk the menus.
    if (!await cap.waitForLog('attract_screen', 240000)) throw new Error('engine never booted (no attract_screen in 240 s)');
    await navigateTo(page, cap, 'main_screen');
    const menuSec = (Date.now() - t0) / 1000;
    await sleep(2000);
    const menuLuma = await canvasLuma(page, 'main_menu');

    await pressKey(page, 'Enter');                 // main -> choose_mode
    await cap.waitForLog("will enter 'choose_mode_screen'", 30000);
    await sleep(1500);
    await pressKey(page, 'Enter');                 // -> song select
    const atSelect = await cap.waitForLog("will enter 'song_select_screen'", 30000);
    if (!atSelect) throw new Error('never reached song_select_screen');
    await sleep(1500);
    const tSong = Date.now();
    await selectSong(page, cap, { scrolls: songIndex });
    // The difficulty / confirm screens after the song: Enter until it loads.
    let loaded = false;
    const deadline = Date.now() + 240000;           // wall-clock runaway guard
    while (Date.now() < deadline) {
        if (cap.logs.some((l) => l.text.includes('DONE (state 4)'))) { loaded = true; break; }
        await pressKey(page, 'Enter');
        await sleep(2000);
    }
    const songSec = (Date.now() - tSong) / 1000;
    const gameScreen = cap.logs.some((l) => l.text.includes("will enter 'game_screen'"));

    // Play: game frames/s from the engine's frame counter.
    let fps = null, songLuma = null;
    if (loaded) {
        await sleep(5000);
        const f0 = await page.evaluate(() => window.dc3FrameCount || 0);
        const s0 = Date.now();
        await sleep(playSec * 1000);
        const f1 = await page.evaluate(() => window.dc3FrameCount || 0);
        fps = +((f1 - f0) / ((Date.now() - s0) / 1000)).toFixed(1);
        songLuma = await canvasLuma(page, 'in_song');
    }
    clearInterval(heapSampler);

    const P = await page.evaluate(() => window.dc3GpuProfile || null);
    writeFileSync(path.join(out, 'console.jsonl'), cap.logs.map((e) => JSON.stringify(e)).join('\n') + '\n');
    writeFileSync(path.join(out, 'gpu_profile.json'), JSON.stringify(P, null, 2));

    const text = (re) => cap.logs.filter((l) => re.test(l.text)).map((l) => l.text);
    const validation = text(/uncaptured error|device lost|device request failed|adapter request failed/i);
    const softwareDxt = text(/software DXT/).length > 0;
    const allowedMobile = ['depth-clip-control', 'depth32float-stencil8', 'texture-compression-etc2',
        'texture-compression-astc', 'indirect-first-instance', 'shader-f16',
        'rg11b10ufloat-renderable', 'bgra8unorm-storage', 'float32-filterable'];
    const devFeatures = (P && P.deviceFeatures) || [];
    const outside = devFeatures.filter((f) => profile === 'bare' ? true : !allowedMobile.includes(f));
    const bcFormats = P ? Object.entries(P.texByFormat).filter(([f, v]) => /^bc/.test(f) && v.count > 0) : [];
    const MB = (b) => +(b / 1048576).toFixed(1);

    console.log('\n=== mobile-gpu: ' + profile + ' ===');
    console.log(`adapter features: ${(P && P.adapterFeatures || []).join(' ')}`);
    console.log(`requested:        ${JSON.stringify(P && P.requestedFeatures)} limits ${JSON.stringify(P && P.requestedLimits)}`);
    console.log(`device features:  ${devFeatures.join(' ') || '(none)'}`);
    console.log(`canvas format:    ${P && P.canvasFormat}`);
    console.log(`textures:         ${P ? P.texCount : '?'} live, ${P ? MB(P.texBytes) : '?'} MB live, ${P ? MB(P.texBytesPeak) : '?'} MB peak`);
    if (P) for (const [f, v] of Object.entries(P.texByFormat).sort((a, b) => b[1].bytes - a[1].bytes))
        console.log(`  ${f.padEnd(24)} ${String(v.count).padStart(5)} live  ${MB(v.bytes)} MB`);
    console.log(`wasm heap peak:   ${heapPeakMB.toFixed(1)} MB (WebAssembly.Memory byteLength)`);
    console.log(`time:             main menu ${menuSec.toFixed(1)} s, song select -> song loaded ${songSec.toFixed(1)} s`);
    console.log(`in song:          ${fps} game frames/s over ${playSec} s; canvas luma menu ${menuLuma}, song ${songLuma}`);
    console.log('');

    check(P && P.profile === profile, `the page ran the ${profile} profile (window.dc3GpuProfile)`);
    check(P && P.refused.length === 0, `requestDevice was not refused${P && P.refused.length ? ': ' + P.refused.join('; ') : ''}`);
    check(devFeatures.length >= 0 && outside.length === 0, `device has no feature outside the profile${outside.length ? ': ' + outside.join(',') : ''}`);
    check(bcFormats.length === 0, `no BC texture created${bcFormats.length ? ': ' + bcFormats.map(([f, v]) => f + ' x' + v.count).join(', ') : ''}`);
    check(softwareDxt, 'the engine reports software DXT decoding');
    check(cap.errors.length === 0, `no page error${cap.errors.length ? ': ' + cap.errors.slice(0, 3).join(' | ') : ''}`);
    check(validation.length === 0, `no WebGPU validation error / device loss${validation.length ? ': ' + validation.slice(0, 3).join(' | ') : ''}`);
    check(menuLuma > 8, `main menu is drawn (canvas luma ${menuLuma} > 8)`);
    check(gameScreen && loaded, 'a song was started and finished loading (game_screen, DONE (state 4))');
    check(songLuma !== null && songLuma > 8, `the song is drawn (canvas luma ${songLuma} > 8)`);
    writeFileSync(path.join(out, 'summary.json'), JSON.stringify({
        profile, failures, menuSec, songSec, fps, heapPeakMB, menuLuma, songLuma,
        texBytesPeakMB: P && MB(P.texBytesPeak), texBytesLiveMB: P && MB(P.texBytes),
        texByFormat: P && P.texByFormat, deviceFeatures: devFeatures, adapterFeatures: P && P.adapterFeatures,
        validation: validation.slice(0, 20), errors: cap.errors.slice(0, 20),
    }, null, 2));
    console.log(`\n${failures ? 'FAIL' : 'PASS'} (${failures} failed) -- ${out}`);
    await browser.close();
    process.exit(failures ? 1 : 0);
} catch (e) {
    console.error('mobile-gpu: run broke: ' + (e && e.stack || e));
    if (browser) await browser.close().catch(() => {});
    process.exit(2);
}
