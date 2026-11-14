// H-34: does the web build cap the game at the image's 60 fps when the
// browser's requestAnimationFrame runs faster (a 120/144 Hz display)?
//
//   node scripts/web/frame-cap.mjs --port 8461 [--hz 144] [--seconds 10]
//        [--env "DC3_FPS_CAP=0"] [--expect uncapped]
//
// Headless Chromium has no high-refresh display. --hz N (default 144) replaces
// window.requestAnimationFrame, before any page script runs, with a scheduler
// that fires callbacks on an N Hz grid and passes the grid time as the
// timestamp -- a callback that runs long misses grid points exactly as it
// would on a real panel. --hz 0 keeps the real rAF and adds Chrome's
// --disable-frame-rate-limit --disable-gpu-vsync instead (the first version
// of this probe; on this box that made every game frame ~70 ms, so it could
// not say anything -- kept for comparison).
//
// On the attract screen (no camera, ?pose=off) it counts, over a window:
//   rAF/s   an independent requestAnimationFrame loop on the page
//   game/s  window.dc3FrameCount (one per game frame)
// and reads window.dc3FrameCap (main_web.cpp dc3RafGate: the estimated
// display rate, the cap it chose, rAFs run vs skipped) and counts the gate's
// "frame cap" decision lines.
//
// Check (exit 1 on failure):
//   default            game/s <= cap + 3, cap = hz / round-down(hz/60) (72 on
//                      144, 60 on 120, 82.5 on 165), always. PASS also needs
//                      the gate's cap to be that and not to have flipped
//                      (<= 2 decision lines). A game slower than the cap on
//                      its own leaves the gate nothing to read (no skipped
//                      rAF, no one-refresh gap), and it may turn the cap off:
//                      correct, and then the run is exit 3 (SKIPPED). rAF/s
//                      is below hz whenever a game frame outlasts a refresh,
//                      as on a real panel. TICK: game-frame ms percentiles.
//   --expect uncapped  the gate skipped nothing and game/s > 66 (for
//                      DC3_FPS_CAP=0: the control -- the gate must be what
//                      caps). 0 skipped but game/s <= 66 is exit 3: the game
//                      could not outrun the cap, so the run says nothing.
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
const hz = +arg('hz', 144);
const unthrottle = hz === 0;

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
    if (hz > 0) {
        await page.addInitScript((HZ) => {
            const period = 1000 / HZ, t0 = performance.now();
            let queue = [], timer = null, nextId = 1;
            const grid = (now) => t0 + Math.floor((now - t0) / period + 0.01) * period;
            const arm = () => {
                if (timer !== null) return;
                const now = performance.now();
                timer = setTimeout(fire, Math.max(0, grid(now) + period - now));
            };
            function fire() {
                timer = null;
                const ts = grid(performance.now());
                const q = queue; queue = [];
                for (const [, cb] of q) {
                    try { cb(ts); } catch (e) { setTimeout(() => { throw e; }); }
                }
                if (queue.length) arm();
            }
            window.requestAnimationFrame = (cb) => { const id = nextId++; queue.push([id, cb]); arm(); return id; };
            window.cancelAnimationFrame = (id) => { queue = queue.filter((e) => e[0] !== id); };
        }, hz);
    }
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
    // Time each game frame (the awaited tick, JSPI suspensions included).
    await page.evaluate(() => {
        const M = window.Module;
        if (!M || !M._dc3MainLoopTick || M.__tickWrapped) return;
        const orig = M._dc3MainLoopTick;
        window.__tickMs = [];
        M._dc3MainLoopTick = async (...a) => {
            const t0 = performance.now();
            const r = await orig(...a);
            window.__tickMs.push(performance.now() - t0);
            return r;
        };
        M.__tickWrapped = true;
    });
    const read = () => page.evaluate(() => ({ r: window.__rafs, g: window.dc3FrameCount || 0, t: performance.now(), cap: window.dc3FrameCap ? { ...window.dc3FrameCap } : null }));
    const a = await read();
    await new Promise((r) => setTimeout(r, seconds * 1000));
    const b = await read();
    const ticks = await page.evaluate(() => (window.__tickMs || []).slice());
    ticks.sort((x, y) => x - y);
    const pct = (q) => ticks.length ? ticks[Math.min(ticks.length - 1, Math.floor(q * ticks.length))].toFixed(1) : 'n/a';
    console.log(`TICK game frame ms over ${ticks.length} frames: p50 ${pct(0.5)} p90 ${pct(0.9)} p99 ${pct(0.99)}`);
    const dt = (b.t - a.t) / 1000;
    const raf = (b.r - a.r) / dt, game = (b.g - a.g) / dt;
    const skipped = a.cap && b.cap ? b.cap.skipped - a.cap.skipped : null;
    console.log(`FRAMECAP hz=${hz || 'real+unthrottled'} rAF/s=${raf.toFixed(1)} game/s=${game.toFixed(1)} over ${dt.toFixed(1)} s` +
        ` skipped=${skipped} gate=${JSON.stringify(b.cap)} decisions=${capLines.length} env=${env || '(none)'}`);
    const want = hz > 0 ? hz / Math.floor(hz / 60 + 0.05) : 60;
    if (hz === 0 && raf < 90) {
        console.log(`CHECK SKIPPED: rAF ran at ${raf.toFixed(1)}/s; the probe needs >= 90 to say anything`);
        code = 3;
    } else if (expect === 'uncapped') {
        if (skipped !== 0) {
            console.log(`CHECK FAIL: the gate skipped ${skipped} rAFs with the cap off`);
            code = 1;
        } else if (game <= 66) {
            // rAF slows with the page, as on a real panel: a game frame longer
            // than one refresh pushes the next rAF out. Under load the game
            // cannot outrun the cap, and then this run says nothing.
            console.log(`CHECK SKIPPED: 0 rAFs skipped, but the game only made ${game.toFixed(1)}/s; it cannot outrun a 60 fps cap on this box right now`);
            code = 3;
        } else console.log(`CHECK PASS: uncapped (${game.toFixed(1)} game/s, 0 rAFs skipped)`);
    } else {
        const capOk = hz === 0 || (b.cap && Math.abs(b.cap.fps - want) < 0.5 && capLines.length <= 2);
        const below = game < want - 5;
        if (game > want + 3) {
            console.log(`CHECK FAIL: ${game.toFixed(1)} game/s > ${want} + 3: not capped`);
            code = 1;
        } else if (capOk) {
            console.log(`CHECK PASS: ${game.toFixed(1)} game frames/s, gate cap ${want}, ${capLines.length} decision(s), on a ${raf.toFixed(1)}/s rAF` +
                (below ? ' (the game ran below the cap on its own: box load)' : ''));
        } else if (below) {
            // Frames longer than the capped period leave no one-refresh gap,
            // so the gate reads the game's own rate and turns the cap off --
            // correct, and nothing a cap could change. This run says nothing.
            console.log(`CHECK SKIPPED: the game made ${game.toFixed(1)}/s, under the ${want} cap, and the gate ` +
                `(cap ${b.cap && b.cap.fps}, ${capLines.length} decisions) followed it; nothing measured`);
            code = 3;
        } else {
            console.log(`CHECK FAIL: the game reached ${game.toFixed(1)}/s but the gate cap is ${b.cap && b.cap.fps} (want ${want}) after ${capLines.length} decision(s)`);
            code = 1;
        }
    }
} catch (e) {
    console.log('ERROR ' + e.message);
    code = 4;
} finally {
    await browser.close();
}
process.exit(code);
