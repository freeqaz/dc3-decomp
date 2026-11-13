// Web pose pipeline throughput and per-stage cost (PLAN L-3), on the attract
// screen: the pose worker runs whatever the game state, and the game renders
// alongside it, so GPU contention is real.
//
//   node scripts/pose_bench/web_pose_fps.mjs --port 8451 --camera clip.y4m \
//       [--settle 25] [--window 20] [--json out.jsonl] [--label L] \
//       [--headed] [--chromium /usr/bin/chromium] "" "&pose_frames=off" "&pose_model=lite" ...
//
// Each trailing argument is appended to the page URL (one browser per
// argument, run in order -- interleave A/B arguments on a busy box).  Serve
// the build with `python3 native/web/server.py --port 8451` first.  The fake
// camera is Chrome's --use-file-for-fake-video-capture: a .y4m it loops at
// the file's own rate (scripts/web/make_pose_fixtures.sh, or
// `ffmpeg -i clip.mp4 -t 15 -pix_fmt yuv420p clip.y4m`).
//
// Prints frames/s over the window (frames that reached the page), and the
// per-stage p50/p95 the worker and page report (window.dc3PoseSummary().stages):
//   wait     worker blocked on the camera stream for the next frame
//   infer    PoseLandmarker.detectForVideo
//   geom     DC3 joint geometry + tracker + packing
//   draw     camera picture: drawImage + getImageData (P-4)
//   rgb      RGBA -> RGB888
//   maskget  segmentation mask -> Float32Array (a GPU readback on the GPU delegate)
//   mask     ordinal mask
//   busy     the worker's whole per-frame work (everything but wait)
//   xfer     worker postMessage -> page handler
//   main     page handler: engine push + preview overlay
// Instrument check: "&pose_slow=camera:10" adds 10 ms of busy work to the
// picture stage only; `draw`..`mask`/`busy` must grow by ~10 ms and nothing else.
import { createRequire } from 'node:module';
import path from 'node:path';
import fs from 'node:fs';
import { fileURLToPath } from 'node:url';

const here = path.dirname(fileURLToPath(import.meta.url));
const require = createRequire(path.join(here, '..', 'web', 'package.json'));
// scripts/web's playwright, else any playwright-core on NODE_PATH (the GPD).
let chromium;
try { ({ chromium } = require('playwright')); } catch (e) { ({ chromium } = createRequire(path.join(process.cwd(), 'x.js'))('playwright-core')); }

const argv = process.argv.slice(2);
const opt = { port: 8451, camera: null, settle: 25, window: 20, json: null, label: '', headed: false, chromium: null };
const extras = [];
for (let i = 0; i < argv.length; i++) {
    const a = argv[i];
    if (a === '--port') opt.port = +argv[++i];
    else if (a === '--camera') opt.camera = argv[++i];
    else if (a === '--settle') opt.settle = +argv[++i];
    else if (a === '--window') opt.window = +argv[++i];
    else if (a === '--json') opt.json = argv[++i];
    else if (a === '--label') opt.label = argv[++i];
    else if (a === '--headed') opt.headed = true;
    else if (a === '--chromium') opt.chromium = argv[++i];
    else extras.push(a);
}
if (!opt.camera) { console.error('--camera FILE.y4m is required'); process.exit(2); }
if (!extras.length) extras.push('');

const sleep = (s) => new Promise((r) => setTimeout(r, s * 1000));
for (const extra of extras) {
    const browser = await chromium.launch({
        headless: !opt.headed,
        ...(opt.chromium ? { executablePath: opt.chromium } : {}),
        args: ['--no-sandbox', '--enable-unsafe-webgpu', '--use-angle=vulkan', '--ozone-platform=x11',
            '--enable-features=Vulkan,VulkanFromANGLE,WebAssemblyJSPromiseIntegration',
            '--autoplay-policy=no-user-gesture-required', '--mute-audio',
            '--use-fake-device-for-media-stream', '--use-fake-ui-for-media-stream',
            `--use-file-for-fake-video-capture=${path.resolve(opt.camera)}`],
    });
    const page = await (await browser.newContext({ viewport: { width: 1280, height: 720 } })).newPage();
    const logs = [];
    page.on('console', (m) => { const t = m.text(); if (/dc3-pose|pose-worker/.test(t)) logs.push(t.slice(0, 200)); });
    await page.goto(`http://127.0.0.1:${opt.port}/?fast_boot=1${extra}`, { waitUntil: 'domcontentloaded' });
    await sleep(opt.settle);
    const a = await page.evaluate(() => window.dc3PoseSummary && window.dc3PoseSummary({ reset: true }));
    await sleep(opt.window);
    const b = await page.evaluate(() => window.dc3PoseSummary && window.dc3PoseSummary());
    const fps = a && b ? +((b.framesIn - a.framesIn) / opt.window).toFixed(2) : null;
    const st = (b && b.stages) || {};
    const line = `${opt.label} ${extra || '(default)'}\tframes/s=${fps}\tdelegate=${b && b.delegate}` +
        `\tmodel=${b && b.model}\tpersons=${b && b.numPoses}\tframes=${b && JSON.stringify(b.frames)}` +
        `\tperson-frames=${a && b ? b.framesWithPerson - a.framesWithPerson : 'n/a'}\n  ` +
        Object.entries(st).map(([k, v]) => `${k} ${v[0]}/${v[1]}`).join('  ') + '   (ms p50/p95)';
    console.log(line);
    for (const l of logs.filter((l) => /ready|delegate|failed|error/.test(l))) console.log('   ', l);
    if (opt.json) {
        fs.appendFileSync(opt.json, JSON.stringify({ label: opt.label, extra, fps, delegate: b && b.delegate,
            model: b && b.model, numPoses: b && b.numPoses, frames: b && b.frames, stages: st,
            framesWithPerson: a && b ? b.framesWithPerson - a.framesWithPerson : null }) + '\n');
    }
    await browser.close();
}
