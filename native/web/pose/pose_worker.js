// DC3 web pose input -- inference worker (module worker).
//
// Runs MediaPipe tasks-vision PoseLandmarker in VIDEO mode (the same
// pose_landmarker_*.task models and options as native/scripts/pose_server.py:
// num_poses 2, detection/presence/tracking confidence 0.5) and the DC3 joint
// geometry (pose_geometry.js), off the main thread so the game's frame loop
// never waits on inference.
//
// main -> worker
//   { type: 'init', bundleUrl, wasmBase, modelUrl, delegate, hfovDeg, numPoses }
//   { type: 'stream', readable }        MediaStreamTrackProcessor frames (preferred)
//   { type: 'frame', image, tsMs, captureSec }   one ImageBitmap/VideoFrame (fallback,
//                                        and the parity harness, which passes
//                                        explicit timestamps)
//   { type: 'reset' }                   drop tracks + filters
// worker -> main
//   { type: 'ready', delegate, loadMs }  | { type: 'error', message }
//   { type: 'pose', floats, count, image2d, width, height, tsMs, captureSec,
//     inferMs, geomMs, raw? }
import { PoseGeometry, posesFromResult, packPersons } from './pose_geometry.js';

let landmarker = null;
let geometry = null;
let cfg = null;
let lastTsMs = -1;
let busy = false;

function post(msg, transfer) { self.postMessage(msg, transfer || []); }

async function init(msg) {
    cfg = msg;
    const t0 = performance.now();
    const vision = await import(msg.bundleUrl);
    // `true`: the ES-module wasm loader, which is what a module worker can load
    // (tasks-vision falls back to import() when importScripts is unavailable).
    const fileset = await vision.FilesetResolver.forVisionTasks(msg.wasmBase, true);
    const make = (delegate) => vision.PoseLandmarker.createFromOptions(fileset, {
        baseOptions: { modelAssetPath: msg.modelUrl, delegate },
        runningMode: 'VIDEO',
        numPoses: msg.numPoses || 2,
        minPoseDetectionConfidence: 0.5,
        minPosePresenceConfidence: 0.5,
        minTrackingConfidence: 0.5,
        outputSegmentationMasks: false,
    });
    let delegate = msg.delegate || 'CPU';
    try {
        landmarker = await make(delegate);
    } catch (e) {
        if (delegate === 'CPU') throw e;
        console.warn('[pose-worker] ' + delegate + ' delegate failed (' + e + '); using CPU');
        delegate = 'CPU';
        landmarker = await make(delegate);
    }
    geometry = new PoseGeometry({ hfovDeg: msg.hfovDeg });
    post({ type: 'ready', delegate, loadMs: performance.now() - t0 });
}

function sourceSize(img) {
    if (img.displayWidth !== undefined) return [img.displayWidth, img.displayHeight];
    return [img.width, img.height];
}

function processImage(img, tsMs, captureSec, wantRaw) {
    // VIDEO mode needs strictly increasing timestamps.
    if (tsMs <= lastTsMs) tsMs = lastTsMs + 1;
    lastTsMs = tsMs;
    const [width, height] = sourceSize(img);
    const t0 = performance.now();
    const res = landmarker.detectForVideo(img, tsMs);
    const t1 = performance.now();
    const poses = posesFromResult(res);
    const persons = geometry.process(poses, width, height, tsMs);
    const { floats, count } = packPersons(persons);
    const t2 = performance.now();
    // 2D landmarks for the preview overlay: per pose 33 x (x, y, visibility).
    const image2d = new Float32Array(Math.max(poses.length, 1) * 33 * 3);
    poses.forEach((p, pi) => {
        if (!p.image) return;
        for (let k = 0; k < 33; k++) {
            image2d[(pi * 33 + k) * 3] = p.image[k][0];
            image2d[(pi * 33 + k) * 3 + 1] = p.image[k][1];
            image2d[(pi * 33 + k) * 3 + 2] = p.vis[k];
        }
    });
    const msg = {
        type: 'pose', floats, count, image2d, numPoses: poses.length,
        width, height, tsMs, captureSec, inferMs: t1 - t0, geomMs: t2 - t1,
    };
    if (wantRaw) msg.raw = poses;
    post(msg, [floats.buffer, image2d.buffer]);
}

async function pumpStream(readable) {
    const reader = readable.getReader();
    for (;;) {
        const { value: frame, done } = await reader.read();
        if (done) break;
        try {
            // VideoFrame.timestamp is the capture time in microseconds: it is
            // both MediaPipe's VIDEO-mode clock and the sensor timestamp the
            // engine takes frame intervals from (as Xbox does from
            // NUI_SKELETON_FRAME), so a late-delivered frame keeps its true
            // interval.
            const captureSec = frame.timestamp ? frame.timestamp / 1e6 : performance.now() / 1000;
            processImage(frame, captureSec * 1000, captureSec, false);
        } catch (e) {
            post({ type: 'error', message: 'inference: ' + e });
        } finally {
            frame.close();
        }
    }
    post({ type: 'ended' });
}

self.onmessage = async (ev) => {
    const msg = ev.data;
    try {
        if (msg.type === 'init') {
            await init(msg);
        } else if (msg.type === 'stream') {
            pumpStream(msg.readable);
        } else if (msg.type === 'frame') {
            if (busy) { msg.image.close && msg.image.close(); return; }
            busy = true;
            try {
                processImage(msg.image, msg.tsMs, msg.captureSec, !!msg.wantRaw);
            } finally {
                busy = false;
                msg.image.close && msg.image.close();
            }
        } else if (msg.type === 'reset') {
            geometry && geometry.reset();
        }
    } catch (e) {
        post({ type: 'error', message: String(e && e.message || e) });
    }
};
