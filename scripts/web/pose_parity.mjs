#!/usr/bin/env node
// JS-vs-Python parity for the web pose input. Reference: scripts/web/pose_parity.py
// (the desktop MediaPipeBackend over a frame directory).
//
//   node scripts/web/pose_parity.mjs geometry ref.json
//       Feed the Python run's RAW landmarks through native/web/pose/pose_geometry.js
//       and compare DC3-20 joints. Tests the port of the geometry alone; should
//       agree to float rounding.
//   node scripts/web/pose_parity.mjs browser ref.json --port 8432 [--frames DIR]
//       [--delegate CPU|GPU] [--model full]
//       Run the shipped web pipeline (pose_worker.js: tasks-vision in a worker +
//       pose_geometry.js) in Chromium over the same PNG frames at the same
//       timestamps, and compare. Tests inference + geometry end to end; the
//       residual is the JS vs Python MediaPipe runtimes.
//   --negative   sabotage control: compare against the reference shifted by one
//                frame, which MUST read clearly worse (a comparison that cannot
//                fail is not a comparison).
import { readFileSync, writeFileSync } from 'fs';
import { dirname, join, resolve } from 'path';
import { fileURLToPath } from 'url';
import { PoseGeometry, DC3_JOINT_NAMES } from '../../native/web/pose/pose_geometry.js';

const here = dirname(fileURLToPath(import.meta.url));
const argv = process.argv.slice(2);
const mode = argv[0];
const refPath = argv[1];
const arg = (k, d) => { const i = argv.indexOf('--' + k); return i >= 0 ? argv[i + 1] : d; };
const flag = (k) => argv.includes('--' + k);
if (!mode || !refPath) { console.error('usage: pose_parity.mjs geometry|browser ref.json [...]'); process.exit(2); }
const ref = JSON.parse(readFileSync(refPath, 'utf8'));

function compare(refFrames, jsFrames, label) {
    const shift = flag('negative') ? 1 : 0;
    const d = [], dRel = [], perJoint = DC3_JOINT_NAMES.map(() => []);
    let both = 0, refOnly = 0, jsOnly = 0, none = 0, confDiff = 0, confN = 0;
    for (let i = 0; i + shift < refFrames.length && i < jsFrames.length; i++) {
        const r = refFrames[i + shift].joints[0];
        const j = jsFrames[i][0];
        if (r && j) both++; else if (r) { refOnly++; continue; } else if (j) { jsOnly++; continue; } else { none++; continue; }
        const rh = r.joints[0], jh = j.joints[0];
        for (let k = 0; k < 20; k++) {
            const a = r.joints[k], b = j.joints[k];
            const e = Math.hypot(a[0] - b[0], a[1] - b[1], a[2] - b[2]);
            d.push(e); perJoint[k].push(e);
            dRel.push(Math.hypot((a[0] - rh[0]) - (b[0] - jh[0]), (a[1] - rh[1]) - (b[1] - jh[1]), (a[2] - rh[2]) - (b[2] - jh[2])));
            confDiff = Math.max(confDiff, Math.abs(a[3] - b[3])); confN++;
        }
    }
    const q = (a, p) => { if (!a.length) return NaN; const s = [...a].sort((x, y) => x - y); return s[Math.min(s.length - 1, Math.floor(p * s.length))]; };
    const mean = (a) => a.reduce((s, x) => s + x, 0) / (a.length || 1);
    const res = {
        label, negativeControl: !!shift, frames: Math.min(refFrames.length, jsFrames.length),
        detection: { both, pythonOnly: refOnly, jsOnly, neither: none },
        jointErrorM: { mean: mean(d), p50: q(d, 0.5), p95: q(d, 0.95), max: q(d, 1) },
        rootRelativeErrorM: { mean: mean(dRel), p50: q(dRel, 0.5), p95: q(dRel, 0.95), max: q(dRel, 1) },
        maxConfDiff: confDiff,
        worstJointsP95: DC3_JOINT_NAMES.map((n, k) => [n, q(perJoint[k], 0.95)]).sort((a, b) => b[1] - a[1]).slice(0, 5),
    };
    console.log(JSON.stringify(res, null, 1));
    return res;
}

if (mode === 'geometry') {
    const g = new PoseGeometry();
    const jsFrames = ref.frames.map((fr) => g.process(fr.raw, fr.width, fr.height, fr.tsMs)
        .map((p) => ({ trackId: p.trackId, joints: p.joints.map((x, k) => [...x, p.conf[k]]) })));
    // track ids must agree too
    let idMismatch = 0;
    ref.frames.forEach((fr, i) => fr.joints.forEach((p, k) => { if (!jsFrames[i][k] || jsFrames[i][k].trackId !== p.trackId) idMismatch++; }));
    const res = compare(ref.frames, jsFrames, 'geometry-only (identical landmarks)');
    console.log('trackId mismatches:', idMismatch);
    if (arg('out')) writeFileSync(arg('out'), JSON.stringify({ ...res, idMismatch }, null, 1));
} else if (mode === 'browser') {
    const { chromium } = await import('playwright');
    const port = +arg('port', 8432);
    const framesDir = resolve(arg('frames', dirname(resolve(refPath))));
    const origin = `http://127.0.0.1:${port}`;
    const browser = await chromium.launch({
        headless: true,
        args: ['--no-sandbox', '--enable-unsafe-webgpu', '--use-angle=vulkan', '--ozone-platform=x11',
            '--enable-features=Vulkan,VulkanFromANGLE', '--disable-extensions'],
    });
    const page = await browser.newPage();
    page.on('console', (m) => { if (m.type() === 'error' || /dc3-pose/.test(m.text())) console.log('[page] ' + m.text()); });
    // The page sits at the server root so pose_input.js's relative pose/...
    // URLs resolve exactly as they do from index.html.
    await page.route(`${origin}/__parity.html*`, (route) => route.fulfill({ contentType: 'text/html', headers: { 'Cross-Origin-Opener-Policy': 'same-origin', 'Cross-Origin-Embedder-Policy': 'require-corp' }, body: '<!doctype html><title>parity</title><body></body>' }));
    await page.route(`${origin}/__parity_frames/**`, (route) => {
        const name = new URL(route.request().url()).pathname.split('/').pop();
        return route.fulfill({ contentType: 'image/png', headers: { 'Cross-Origin-Resource-Policy': 'same-origin' }, body: readFileSync(join(framesDir, name)) });
    });
    const q = new URLSearchParams({ pose: 'off', pose_model: arg('model', 'full'), pose_delegate: arg('delegate', 'CPU') });
    await page.goto(`${origin}/__parity.html?${q}`);
    const t0 = Date.now();
    const out = await page.evaluate(async ({ frames }) => {
        window.__dc3PoseNoAutoInit = true;
        const mod = await import('/pose/pose_input.js');
        const sources = await mod.resolveSources();
        const { worker, ready } = await mod.createWorker(sources);
        const info = await ready;
        const results = [], timing = [];
        for (const fr of frames) {
            const blob = await (await fetch('/__parity_frames/' + fr.file)).blob();
            const bmp = await createImageBitmap(blob, { colorSpaceConversion: 'none', premultiplyAlpha: 'none' });
            const msg = await new Promise((res) => {
                const h = (ev) => { if (ev.data.type === 'pose' || ev.data.type === 'error') { worker.removeEventListener('message', h); res(ev.data); } };
                worker.addEventListener('message', h);
                worker.postMessage({ type: 'frame', image: bmp, tsMs: fr.tsMs, captureSec: fr.tsMs / 1000 }, [bmp]);
            });
            if (msg.type === 'error') throw new Error(msg.message);
            const persons = [];
            for (let i = 0; i < msg.count; i++) {
                const b = i * 81;
                const joints = [];
                for (let k = 0; k < 20; k++) joints.push(Array.from(msg.floats.slice(b + 1 + k * 4, b + 5 + k * 4)));
                persons.push({ trackId: msg.floats[b], joints });
            }
            results.push(persons);
            timing.push(msg.inferMs + msg.geomMs);
        }
        return { results, timing, info, source: sources.source };
    }, { frames: ref.frames.map((f) => ({ file: f.file, tsMs: f.tsMs })) });
    const s = [...out.timing].sort((a, b) => a - b);
    console.log(`browser: ${out.results.length} frames in ${((Date.now() - t0) / 1000).toFixed(1)}s, delegate=${out.info.delegate} source=${out.source} load=${Math.round(out.info.loadMs)}ms, per-frame infer+geom p50=${s[s.length >> 1].toFixed(1)}ms p95=${s[Math.floor(s.length * 0.95)].toFixed(1)}ms`);
    const res = compare(ref.frames, out.results, `browser pipeline (${out.info.delegate})`);
    if (arg('out')) writeFileSync(arg('out'), JSON.stringify({ ...res, timingP50: s[s.length >> 1], timingP95: s[Math.floor(s.length * 0.95)], delegate: out.info.delegate }, null, 1));
    await browser.close();
} else {
    console.error('unknown mode ' + mode); process.exit(2);
}
