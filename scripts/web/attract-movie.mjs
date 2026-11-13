#!/usr/bin/env node
// The attract screen's intro movie is on the canvas (W-11).
//
// The <video> element can play real frames while the canvas stays black: the
// movie is decoded into a texture and drawn by MoviePanel::Draw, and until
// np-web3 the engine wiped that draw every frame (WgpuRnd resumed the MSAA
// frame pass after the overlay flush, whose resolve overwrote the frame
// target). So this checks the PIXELS, not the video element:
//
//   - the attract_movie_panel's video is playing (currentTime advances);
//   - two screenshots 2 s apart, decoded in the page, each have a mean luma
//     well above what the helpbar overlay alone gives (the wiped frame is 0) and differ from each other
//     (the movie moves, the overlay alone would not).
//
//   node scripts/web/attract-movie.mjs --port 8448 [--out DIR]
//
// Exit 0 pass, 1 fail, 2 setup (no screen, no movie element).
import { chromium } from 'playwright';
import { mkdirSync, readFileSync } from 'fs';
import { parseArgs, waitForServer } from './lib/core.mjs';

const opts = parseArgs({ port: { type: 'number', default: 8420 }, out: { type: 'string', default: '/tmp/dc3-web/attract-movie' } });
mkdirSync(opts.out, { recursive: true });
await waitForServer(opts.port);
const browser = await chromium.launch({
    headless: !process.env.DISPLAY,
    args: ['--no-sandbox', '--enable-unsafe-webgpu', '--use-angle=vulkan', '--ozone-platform=x11',
        '--enable-features=Vulkan,VulkanFromANGLE,WebAssemblyJSPromiseIntegration',
        '--autoplay-policy=no-user-gesture-required', '--mute-audio'],
});
let failures = 0;
const check = (cond, msg) => { console.log(`${cond ? 'ok  ' : 'FAIL'} ${msg}`); if (!cond) failures++; };
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

// Luma statistics of a PNG, decoded by the page (no image library in Node).
async function stats(page, file) {
    const b64 = readFileSync(file).toString('base64');
    return page.evaluate(async (b64) => {
        const bytes = Uint8Array.from(atob(b64), (c) => c.charCodeAt(0));
        const bmp = await createImageBitmap(new Blob([bytes], { type: 'image/png' }));
        const c = new OffscreenCanvas(bmp.width, bmp.height);
        const g = c.getContext('2d');
        g.drawImage(bmp, 0, 0);
        const d = g.getImageData(0, 0, bmp.width, bmp.height).data;
        let sum = 0, lit = 0;
        const n = d.length / 4;
        const luma = new Uint8Array(n);
        for (let i = 0; i < n; i++) {
            const y = 0.299 * d[i * 4] + 0.587 * d[i * 4 + 1] + 0.114 * d[i * 4 + 2];
            luma[i] = y; sum += y; if (y > 6) lit++;
        }
        return { mean: sum / n, lit: lit / n, luma: Array.from(luma.filter((_, i) => i % 97 === 0)) };
    }, b64);
}

let setupError = '';
try {
    const page = await (await browser.newContext({ viewport: { width: 1280, height: 720 } })).newPage();
    const logs = [];
    page.on('console', (m) => logs.push(m.text()));
    // No fast_boot: the attract screen must play its movie, not auto-advance.
    await page.goto(`http://127.0.0.1:${opts.port}/?pose=off`, { waitUntil: 'domcontentloaded' });
    const d = Date.now() + 120000;
    while (Date.now() < d && !logs.some((l) => l.includes("will enter 'attract_screen'"))) await sleep(250);
    if (!logs.some((l) => l.includes("will enter 'attract_screen'"))) throw new Error('never reached attract_screen');
    await sleep(6000);
    const video = () => page.evaluate(() => {
        const M = (window.Module && Module._webMovies) || {};
        const v = Object.values(M).find((m) => /intro\.webm/.test(m.video.currentSrc));
        return v ? { t: v.video.currentTime, paused: v.video.paused, ready: v.ready } : null;
    });
    const v1 = await video();
    if (!v1) throw new Error('no intro.webm movie element');
    await page.screenshot({ path: `${opts.out}/attract-1.png` });
    await sleep(2000);
    const v2 = await video();
    await page.screenshot({ path: `${opts.out}/attract-2.png` });
    check(v2 && !v2.paused && v2.t > v1.t + 1, `intro video plays (${v1.t.toFixed(2)} s -> ${v2 && v2.t.toFixed(2)} s)`);
    const s1 = await stats(page, `${opts.out}/attract-1.png`);
    const s2 = await stats(page, `${opts.out}/attract-2.png`);
    for (const [i, s] of [[1, s1], [2, s2]]) {
        // Measured on the movie: the Harmonix logo on black reads mean luma 22,
        // a dark dance scene 13; the wiped frame is 0, and the helpbar overlay
        // alone would be ~2.
        check(s.mean > 8, `canvas is lit at sample ${i}: mean luma ${s.mean.toFixed(1)} (${(100 * s.lit).toFixed(1)}% of pixels above 6)`);
    }
    let moved = 0;
    for (let i = 0; i < s1.luma.length; i++) if (Math.abs(s1.luma[i] - s2.luma[i]) > 16) moved++;
    check(moved / s1.luma.length > 0.05, `the picture moves between samples (${(100 * moved / s1.luma.length).toFixed(1)}% of sampled pixels changed)`);
} catch (e) {
    setupError = String(e.message || e);
    console.log(`SETUP ${setupError}`);
}
await browser.close();
console.log(setupError ? 'RESULT setup failure' : failures ? `RESULT FAILED (${failures})` : 'RESULT PASS');
process.exit(setupError ? 2 : failures ? 1 : 0);
