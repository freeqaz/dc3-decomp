// Web pose pipeline throughput and per-stage cost (PLAN L-3, W-17..W-22), on
// the attract screen: the pose worker runs whatever the game state, and the
// game renders alongside it, so GPU and CPU contention are real.
//
//   node scripts/pose_bench/web_pose_fps.mjs --port 8451 --camera clip.y4m \
//       [--settle 25] [--window 20] [--repeat N] [--json out.jsonl] [--label L] \
//       [--no-game] [--throttle R] [--cpu-quota P] [--check-fps F] \
//       [--headed] [--chromium /usr/bin/chromium] "" "&pose_frames=off" "&pose_model=lite" ...
//
// Each trailing argument is appended to the page URL (one browser per
// argument; --repeat N runs the whole list N times, interleaved, which is what
// a busy box needs).  Serve the build with `python3 native/web/server.py
// --port 8451` first.  The fake camera is Chrome's
// --use-file-for-fake-video-capture: a .y4m it loops at the file's own rate
// (`ffmpeg -i clip.mp4 -t 20 -vf fps=30,format=yuv420p -f yuv4mpegpipe clip.y4m`;
// measured: it delivers 29.99 frames/s to a MediaStreamTrackProcessor).
// Every run adds pose_stats=1 (the per-frame pipeline timing).
//
//   --no-game     the pose pipeline alone: a blank page at the server root
//                 loads pose_input.js exactly as index.html does, with no game
//                 (nothing consumes the frames, so the pipeline/delivered rows
//                 stay empty and the rate is the worker's own)
//   --throttle R  Chromium CPU throttling (DevTools Emulation.setCPUThrottlingRate,
//                 R = 2, 4, ...) from before the page loads.  It slows the
//                 page's MAIN THREAD only -- the game, the pose message
//                 handler -- not the pose worker: Chromium refuses it on a
//                 worker target ("Operation is only supported for pages, not
//                 workers", measured 2026-11-14).  A stand-in for a slow main
//                 thread, not a measurement of a slower machine
//   --cpu-quota P the whole browser (every process and thread: game, pose
//                 workers, GPU process) in a cgroup with CPUQuota=P%
//                 (systemd-run --user --scope; 400 = four cores' worth): the
//                 stand-in for a machine with fewer cores.  Per-core speed and
//                 the GPU are unchanged, so it is still a stand-in
//   --check-fps F the regression check: exit 1 if any run's delivered rate is
//                 below F frames/s -- with the game, pose frames a game frame
//                 took (PoseInput_Web.cpp consumption); with --no-game, frames
//                 out of the worker.  Exit 2 if a run produced no pose frames.
//
// Prints per run: camera frames/s at the worker (processed + skipped), pose
// frames/s out of the worker, delivered frames/s (taken by a game frame),
// skipped/overwritten counts, the game's own frame rate, box load, and the
// p50/p95 of every stage:
//   worker stages (dc3PoseSummary().stages)
//     wait     worker blocked on the camera stream for the next frame
//     scale    frame -> model input size (pose_input=<h>)
//     infer    PoseLandmarker.detectForVideo
//     probe    the second-person look, when it runs in this worker
//     geom     DC3 joint geometry + tracker + packing
//     draw     camera picture: drawImage + getImageData (P-4; 0 = reused the scale)
//     rgb      RGBA -> RGB888
//     maskget  segmentation mask -> Float32Array (a GPU readback on the GPU delegate)
//     mask     ordinal mask
//     busy     the worker's whole per-frame work (everything but wait)
//     xfer     worker postMessage -> page handler
//     main     page handler: engine push + picture push + preview overlay
//   pipeline (dc3PoseSummary().pipeline; per frame a game frame took)
//     age      camera frame waited in the stream beyond the freshest frame seen
//     worker   frame reached the worker -> result posted
//     xfer     posted -> page handler
//     page     page handler -> engine push done
//     toGame   push -> end of the game frame whose Poll latched it
//     read2game  frame reached the worker -> taken by the game
//     e2e      age + read2game: camera frame -> game, less the camera's own
//              exposure/driver latency (not visible to the page)
// Instrument check: "&pose_slow=camera:10" adds 10 ms of busy work to the
// picture stage only; `draw`..`mask`/`busy` must grow by ~10 ms and nothing else;
// "&pose_slow=infer:40" is the sabotage for --check-fps.
import { createRequire } from 'node:module';
import path from 'node:path';
import fs from 'node:fs';
import os from 'node:os';
import { fileURLToPath } from 'node:url';

const here = path.dirname(fileURLToPath(import.meta.url));
const require = createRequire(path.join(here, '..', 'web', 'package.json'));
// scripts/web's playwright, else any playwright-core on NODE_PATH (the GPD).
let chromium;
try { ({ chromium } = require('playwright')); } catch (e) { ({ chromium } = createRequire(path.join(process.cwd(), 'x.js'))('playwright-core')); }

const argv = process.argv.slice(2);
const opt = { port: 8451, camera: null, settle: 25, window: 20, json: null, label: '', headed: false, chromium: null,
    noGame: false, throttle: 1, cpuQuota: 0, checkFps: null, repeat: 1 };
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
    else if (a === '--no-game') opt.noGame = true;
    else if (a === '--throttle') opt.throttle = +argv[++i];
    else if (a === '--cpu-quota') opt.cpuQuota = +argv[++i];
    else if (a === '--check-fps') opt.checkFps = +argv[++i];
    else if (a === '--repeat') opt.repeat = Math.max(1, +argv[++i]);
    else extras.push(a);
}
if (!opt.camera) { console.error('--camera FILE.y4m is required'); process.exit(2); }
if (!extras.length) extras.push('');

const load = () => os.loadavg()[0].toFixed(0);
const sleep = (s) => new Promise((r) => setTimeout(r, s * 1000));
const origin = `http://127.0.0.1:${opt.port}`;

// --cpu-quota: launch the browser through a wrapper that puts it in its own
// cgroup scope with a CPU quota (Playwright's pipe fds survive the exec).
function quotaWrapper(pct) {
    const real = opt.chromium || chromium.executablePath();
    const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'posebench-'));
    const w = path.join(dir, 'chromium-quota.sh');
    fs.writeFileSync(w, `#!/bin/sh\nexec systemd-run --user --scope -q -p CPUQuota=${pct}% ${JSON.stringify(real)} "$@"\n`);
    fs.chmodSync(w, 0o755);
    return w;
}
const executablePath = opt.cpuQuota > 0 ? quotaWrapper(opt.cpuQuota) : opt.chromium;

const results = [];
for (let round = 0; round < opt.repeat; round++) for (const extra of extras) {
    const loadStart = load();
    const browser = await chromium.launch({
        headless: !opt.headed,
        ...(executablePath ? { executablePath } : {}),
        args: ['--no-sandbox', '--enable-unsafe-webgpu', '--use-angle=vulkan', '--ozone-platform=x11',
            '--enable-features=Vulkan,VulkanFromANGLE,WebAssemblyJSPromiseIntegration',
            '--autoplay-policy=no-user-gesture-required', '--mute-audio',
            '--use-fake-device-for-media-stream', '--use-fake-ui-for-media-stream',
            `--use-file-for-fake-video-capture=${path.resolve(opt.camera)}`],
    });
    const page = await (await browser.newContext({ viewport: { width: 1280, height: 720 } })).newPage();
    const logs = [];
    page.on('console', (m) => { const t = m.text(); if (/dc3-pose|pose-worker|WebGPU|adapter|BOOT|fatal/i.test(t)) logs.push(t.slice(0, 200)); });
    if (opt.noGame) {
        // At the server root, so pose_input.js's relative pose/... URLs resolve
        // exactly as they do from index.html; COOP/COEP as server.py sends.
        await page.route(`${origin}/__posebench.html*`, (route) => route.fulfill({ contentType: 'text/html',
            headers: { 'Cross-Origin-Opener-Policy': 'same-origin', 'Cross-Origin-Embedder-Policy': 'require-corp' },
            body: '<!doctype html><title>pose bench</title><body><div id="canvas-container" style="width:640px;height:360px;position:relative"></div>' +
                '<script type="module" src="/pose/pose_input.js"></script></body>' }));
    }
    if (opt.throttle > 1) await (await page.context().newCDPSession(page)).send('Emulation.setCPUThrottlingRate', { rate: opt.throttle });
    const url = opt.noGame ? `${origin}/__posebench.html?pose_stats=1${extra}` : `${origin}/?fast_boot=1&pose_stats=1${extra}`;
    await page.goto(url, { waitUntil: 'domcontentloaded' });
    await sleep(opt.settle);
    const grab = (reset) => page.evaluate((reset) => ({ ...(window.dc3PoseSummary ? window.dc3PoseSummary({ reset }) : {}),
        gameFrame: window.dc3FrameCount || 0, at: performance.now() }), reset);
    const a = await grab(true);
    await sleep(opt.window);
    const b = await grab(false);
    const loadEnd = load();
    const span = (b.at - a.at) / 1000;
    const rate = (k) => (a[k] !== undefined && b[k] !== undefined ? +((b[k] - a[k]) / span).toFixed(2) : null);
    const r = {
        label: opt.label, extra, round, throttle: opt.throttle, cpuQuota: opt.cpuQuota, noGame: opt.noGame, load: [+loadStart, +loadEnd],
        camFps: rate('camFrames'), poseFps: rate('framesIn'), deliveredFps: opt.noGame ? null : rate('consumed'),
        rvfcFps: rate('rvfcFrames'), gameFps: +((b.gameFrame - a.gameFrame) / span).toFixed(1),
        skipped: b.skipped - a.skipped, overwritten: (b.overwritten || 0) - (a.overwritten || 0),
        delegate: b.delegate, model: b.model, numPoses: b.numPoses, frames: b.frames, input: b.input,
        probe: b.probe || null, state: b.state, personFrames: b.framesWithPerson - a.framesWithPerson,
        pushed: b.pushed - a.pushed, stages: b.stages || {}, pipeline: b.pipeline || {},
    };
    results.push(r);
    const fmt = (o) => Object.entries(o).map(([k, v]) => `${k} ${v[0]}/${v[1]}`).join('  ');
    console.log(`${opt.label} ${extra || '(default)'} [round ${round + 1}${opt.throttle > 1 ? `, main thread x${opt.throttle}` : ''}${opt.cpuQuota ? `, cpu quota ${opt.cpuQuota}%` : ''}${opt.noGame ? ', no game' : ''}]` +
        `\tcamera/s=${r.camFps}\tpose/s=${r.poseFps}\tdelivered/s=${r.deliveredFps}\tgame fps=${r.gameFps}` +
        `\tskipped=${r.skipped} overwritten=${r.overwritten}\tload ${loadStart}->${loadEnd}` +
        `\n  delegate=${r.delegate} model=${r.model} seen=${r.numPoses} input=${r.input || 'native'} picture=${JSON.stringify(r.frames)}` +
        ` person-frames=${r.personFrames} state=${r.state}${r.probe ? ' probe=' + JSON.stringify(r.probe) : ''}` +
        `\n  stages   ${fmt(r.stages)}   (ms p50/p95)` +
        (Object.keys(r.pipeline).length ? `\n  pipeline ${fmt(r.pipeline)}` : ''));
    for (const l of logs.filter((l) => /ready|delegate|failed|error|WebGPU|adapter/i.test(l)).slice(0, 6)) console.log('   ', l);
    // The game counts frames only once it is running (main_web.cpp BOOT_RUNNING):
    // game fps 0 means the numbers above are the pose pipeline WITHOUT the game.
    if (!opt.noGame && !r.gameFps) console.log('    NOTE: game not running in this window (dc3FrameCount did not advance)');
    if (opt.json) fs.appendFileSync(opt.json, JSON.stringify(r) + '\n');
    await browser.close();
}

if (opt.checkFps !== null) {
    let code = 0;
    for (const r of results) {
        const got = r.noGame ? r.poseFps : r.deliveredFps;
        const what = r.noGame ? 'pose frames/s out of the worker' : 'pose frames/s delivered to the game';
        if (!got) { console.log(`CHECK FAIL ${r.extra || '(default)'}: no pose frames (${what} = ${got})`); code = Math.max(code, 2); }
        else if (got < opt.checkFps) { console.log(`CHECK FAIL ${r.extra || '(default)'} round ${r.round + 1}: ${got} ${what} < ${opt.checkFps} (load ${r.load.join('->')})`); code = Math.max(code, 1); }
        else console.log(`CHECK PASS ${r.extra || '(default)'} round ${r.round + 1}: ${got} ${what} >= ${opt.checkFps}`);
    }
    process.exit(code);
}
