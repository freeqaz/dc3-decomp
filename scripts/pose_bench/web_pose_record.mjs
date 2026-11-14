// Record a dc3-pose-fixture from a video through the WEB pose pipeline (the
// shipped pose_worker.js: tasks-vision in a worker + pose_geometry.js), with
// any pose_* URL knobs -- the web twin of scripts/video_calib/record_segment.py,
// so a web setting (GPU delegate, lite model, input scale, probe) can be scored
// by the same replay sweep as the desktop recording (PLAN W-20).
//
//   node scripts/pose_bench/web_pose_record.mjs --port 8462 --video clip.mp4 \
//       --t0 8 --start -5 --dur 161.3 --out song.dc3pose [--extra "&pose_delegate=GPU"] \
//       [--crop 640:480:0:0] [--fps 30] [--no-gpu]
//
// Frames: ffmpeg decodes the same segment record_segment.py does (accurate
// seek to t0 + start, crop, fps) as RGBA; a small HTTP server in this process
// hands them to the page one at a time.  Each frame k goes to the worker as a
// VideoFrame at k * 1000 / fps ms -- the timestamps record_segment.py gives
// MediaPipe -- and is written at song time start + k / fps.  The person is
// the lowest track id, as record_segment.py picks.  Output is the format
// record_segment.py writes (then scripts/video_calib/fixture_transform.py).
//
// Serve the build first (python3 native/web/server.py --port 8462).  The GPU
// delegate needs a GPU (run outside the sandbox, under the GPU lock).
import { spawn } from 'node:child_process';
import { createRequire } from 'node:module';
import http from 'node:http';
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const here = path.dirname(fileURLToPath(import.meta.url));
const require = createRequire(path.join(here, '..', 'web', 'package.json'));
const { chromium } = require('playwright');

const argv = process.argv.slice(2);
const arg = (k, d) => { const i = argv.indexOf('--' + k); return i >= 0 ? argv[i + 1] : d; };
const port = +arg('port', 8462);
const video = arg('video');
const t0 = +arg('t0', 0);
const start = +arg('start', -2);
const dur = +arg('dur', 0);
const out = arg('out');
const extra = arg('extra', '');
const crop = arg('crop', '640:480:0:0');
const fps = +arg('fps', 30);
if (!video || !out || !dur) { console.error('usage: web_pose_record.mjs --video V --t0 T --start S --dur D --out F [--extra "&pose_..."]'); process.exit(2); }
const [w, h] = crop.split(':').map(Number);
const size = w * h * 4;

// ffmpeg -> frame queue, paused while a few frames wait.
const ff = spawn('ffmpeg', ['-nostdin', '-loglevel', 'error', '-ss', (t0 + start).toFixed(3), '-i', video,
    '-t', dur.toFixed(3), '-vf', `crop=${crop},fps=${fps}`, '-an', '-f', 'rawvideo', '-pix_fmt', 'rgba', '-'],
    { stdio: ['ignore', 'pipe', 'inherit'] });
const frames = [];
let pendingBuf = Buffer.alloc(0), ended = false, waiter = null;
ff.stdout.on('data', (d) => {
    pendingBuf = Buffer.concat([pendingBuf, d]);
    while (pendingBuf.length >= size) { frames.push(pendingBuf.subarray(0, size)); pendingBuf = pendingBuf.subarray(size); }
    if (frames.length > 8) ff.stdout.pause();
    if (waiter) { const f = waiter; waiter = null; f(); }
});
ff.stdout.on('end', () => { ended = true; if (waiter) { const f = waiter; waiter = null; f(); } });
async function nextFrame() {
    while (!frames.length && !ended) await new Promise((r) => { waiter = r; });
    const f = frames.shift() || null;
    if (frames.length <= 8) ff.stdout.resume();
    return f;
}
const server = http.createServer(async (req, res) => {
    const f = await nextFrame();
    res.writeHead(f ? 200 : 204, { 'Content-Type': 'application/octet-stream', 'Access-Control-Allow-Origin': '*',
        'Cross-Origin-Resource-Policy': 'cross-origin', 'Cache-Control': 'no-store' });
    res.end(f || undefined);
});
await new Promise((r) => server.listen(0, '127.0.0.1', r));
const framePort = server.address().port;

const gpuArgs = argv.includes('--no-gpu') ? ['--disable-gpu'] :
    ['--enable-unsafe-webgpu', '--use-angle=vulkan', '--enable-features=Vulkan,VulkanFromANGLE'];
const browser = await chromium.launch({ headless: true, args: ['--no-sandbox', '--ozone-platform=x11', '--disable-extensions',
    // the frame server is another loopback port: not a Local Network Access question
    '--disable-features=LocalNetworkAccessChecks,BlockInsecurePrivateNetworkRequests', ...gpuArgs] });
const page = await browser.newPage();
page.on('console', (m) => { if (m.type() === 'error' || /dc3-pose|pose-worker/.test(m.text())) console.log('[page] ' + m.text()); });
const origin = `http://127.0.0.1:${port}`;
await page.route(`${origin}/__record.html*`, (route) => route.fulfill({ contentType: 'text/html',
    headers: { 'Cross-Origin-Opener-Policy': 'same-origin', 'Cross-Origin-Embedder-Policy': 'require-corp' },
    body: '<!doctype html><title>record</title><body></body>' }));
await page.goto(`${origin}/__record.html?pose=off${extra}`);
const lines = [];
await page.exposeFunction('__recLine', (l) => { lines.push(l); });
const t1 = Date.now();
const info = await page.evaluate(async ({ framePort, w, h, fps, start }) => {
    window.__dc3PoseNoAutoInit = true;
    const mod = await import('/pose/pose_input.js');
    const sources = await mod.resolveSources();
    const { worker, ready } = await mod.createWorker(sources);
    const info = await ready;
    const busy = [];
    let n = 0, seen = 0;
    const s = window.dc3PoseStats;
    for (;;) {
        const r = await fetch(`http://127.0.0.1:${framePort}/f`);
        if (r.status !== 200) break;
        const buf = await r.arrayBuffer();
        const tsMs = n * 1000 / fps;
        const vf = new VideoFrame(new Uint8Array(buf), { format: 'RGBA', codedWidth: w, codedHeight: h, timestamp: Math.round(tsMs * 1000) });
        const msg = await new Promise((res) => {
            const hd = (ev) => { if (ev.data.type === 'pose' || ev.data.type === 'error') { worker.removeEventListener('message', hd); res(ev.data); } };
            worker.addEventListener('message', hd);
            worker.postMessage({ type: 'frame', image: vf, tsMs, captureSec: tsMs / 1000 }, [vf]);
        });
        if (msg.type === 'error') throw new Error(msg.message);
        busy.push(msg.stages.busy);
        const t = (start + n / fps).toFixed(4);
        if (msg.count > 0) {
            let best = 0;
            for (let i = 1; i < msg.count; i++) if (msg.floats[i * 81] < msg.floats[best * 81]) best = i;
            const vals = [];
            for (let k = 0; k < 20; k++) {
                const b = best * 81 + 1 + k * 4;
                vals.push(msg.floats[b].toFixed(5), msg.floats[b + 1].toFixed(5), msg.floats[b + 2].toFixed(5), msg.floats[b + 3].toFixed(3));
            }
            window.__recLine(`${t} 1 ${vals.join(' ')}`);
            seen++;
        } else window.__recLine(`${t} 0`);
        n++;
        if (msg.probe) s.probe = msg.probe;
    }
    busy.sort((a, b) => a - b);
    return { ...info, n, seen, busyP50: busy[busy.length >> 1], busyP95: busy[Math.floor(busy.length * 0.95)], probe: s.probe || null, model: s.model };
}, { framePort, w, h, fps, start });
const hdr = `# dc3-pose-fixture v1\n# web_pose_record.mjs t0=${t0} start=${start} dur=${dur} crop=${crop} width=0 fps=${fps} ` +
    `model=${info.model} delegate=${info.delegate} extra=${extra || '-'} mode=video\n`;
fs.writeFileSync(out, hdr + lines.join('\n') + '\n');
console.log(`${out}: ${info.n} frames, person in ${info.seen}, delegate=${info.delegate}${info.delegateWhy ? " (" + info.delegateWhy + ")" : ""}, worker busy p50 ${info.busyP50 && info.busyP50.toFixed(1)} ms ` +
    `p95 ${info.busyP95 && info.busyP95.toFixed(1)} ms, ${((Date.now() - t1) / 1000).toFixed(0)} s${info.probe ? ' probe=' + JSON.stringify(info.probe) : ''}`);
await browser.close();
server.close();
ff.kill();
process.exit(info.n ? 0 : 1);
