// H-34: does the web build cap the game at the image's 60 fps when the
// browser's requestAnimationFrame runs faster (a 120/144 Hz display)?
//
//   node scripts/web/frame-cap.mjs --port 8461 [--seconds 10] [--env "DC3_FPS_CAP=0"]
//        [--no-unthrottle] [--expect uncapped]
//
// Headless Chromium has no high-refresh display to emulate, but
// --disable-frame-rate-limit (+ --disable-gpu-vsync) makes rAF fire as fast
// as the page can take it, which is the same question with a bigger number.
// On the attract screen (no camera, ?pose=off) it counts, over a window:
//   rAF/s   an independent requestAnimationFrame loop on the page
//   game/s  window.dc3FrameCount (one per game frame)
// and reads window.dc3FrameCap (main_web.cpp dc3RafGate: the estimated
// display rate, the cap it chose, rAFs run vs skipped).
//
// Check (exit 1 on failure):
//   default            rAF/s >= 90 (else the probe did not get an unthrottled
//                      rAF: exit 3, nothing measured) and game/s <= 66
//   --expect uncapped  game/s >= 0.8 * rAF/s (for DC3_FPS_CAP=0: the
//                      sabotage/control run -- the gate must be what caps)
import { createRequire } from 'node:module';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const here = path.dirname(fileURLToPath(import.meta.url));
const require = createRequire(path.join(here, 'package.json'));
const { chromium } = require('playwright');

const argv = process.argv.slice(2);
const arg = (k, d) => { const i = argv.indexOf('--' + k); return i >= 0 ? argv[i + 1] : d; };
const port = +arg('port', 8461);
const seconds = +arg('seconds', 10);
const env = arg('env', '');
const expect = arg('expect', 'capped');
const unthrottle = !argv.includes('--no-unthrottle');

const browser = await chromium.launch({
    headless: !process.env.DISPLAY,
    args: [
        '--no-sandbox', '--enable-unsafe-webgpu', '--use-angle=vulkan',
        '--enable-features=Vulkan,VulkanFromANGLE,WebAssemblyJSPromiseIntegration',
        '--ozone-platform=x11', '--disable-extensions', '--mute-audio',
        '--autoplay-policy=no-user-gesture-required',
        ...(unthrottle ? ['--disable-frame-rate-limit', '--disable-gpu-vsync'] : []),
    ],
});
let code = 0;
try {
    const page = await (await browser.newContext({ viewport: { width: 1280, height: 720 } })).newPage();
    let screen = '';
    const capLines = [];
    page.on('console', (m) => {
        const t = m.text();
        const s = /will enter '([^']+)'/.exec(t);
        if (s) screen = s[1];
        if (/DC3 Web: frame cap/.test(t)) { capLines.push(t); console.log(t); }
    });
    await page.addInitScript(() => {
        window.__rafs = 0;
        const loop = () => { window.__rafs++; requestAnimationFrame(loop); };
        requestAnimationFrame(loop);
    });
    const q = `?pose=off&env=${encodeURIComponent('DC3_AUDIO_GAIN=0' + (env ? ';' + env : ''))}`;
    await page.goto(`http://127.0.0.1:${port}/${q}`, { waitUntil: 'domcontentloaded', timeout: 30000 });
    const deadline = Date.now() + 120000;
    while (screen !== 'attract_screen' && Date.now() < deadline) await new Promise((r) => setTimeout(r, 250));
    if (screen !== 'attract_screen') throw new Error('never reached attract_screen');
    await new Promise((r) => setTimeout(r, 3000)); // the gate's first decision + the movie start
    const read = () => page.evaluate(() => ({ r: window.__rafs, g: window.dc3FrameCount || 0, t: performance.now(), cap: window.dc3FrameCap || null }));
    const a = await read();
    await new Promise((r) => setTimeout(r, seconds * 1000));
    const b = await read();
    const dt = (b.t - a.t) / 1000;
    const raf = (b.r - a.r) / dt, game = (b.g - a.g) / dt;
    console.log(`FRAMECAP rAF/s=${raf.toFixed(1)} game/s=${game.toFixed(1)} over ${dt.toFixed(1)} s` +
        ` gate=${JSON.stringify(b.cap)} env=${env || '(none)'} unthrottled=${unthrottle}`);
    if (expect === 'uncapped') {
        if (game < 0.8 * raf) { console.log('CHECK FAIL: expected the game to run on every rAF'); code = 1; }
        else console.log('CHECK PASS: uncapped (game frames on every rAF)');
    } else if (unthrottle && raf < 90) {
        console.log(`CHECK SKIPPED: rAF ran at ${raf.toFixed(1)}/s; the probe needs >= 90 to say anything`);
        code = 3;
    } else if (game > 66) {
        console.log(`CHECK FAIL: ${game.toFixed(1)} game frames/s on a ${raf.toFixed(1)}/s rAF: no cap`);
        code = 1;
    } else {
        console.log(`CHECK PASS: capped at ${game.toFixed(1)} game frames/s on a ${raf.toFixed(1)}/s rAF`);
    }
} catch (e) {
    console.log('ERROR ' + e.message);
    code = 4;
} finally {
    await browser.close();
}
process.exit(code);
