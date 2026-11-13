// DC3 web pose input -- inference worker (module worker).
//
// Runs MediaPipe tasks-vision PoseLandmarker in VIDEO mode (the same
// pose_landmarker_*.task models and options as native/scripts/pose_server.py:
// num_poses 2, detection/presence/tracking confidence 0.5) and the DC3 joint
// geometry (pose_geometry.js), off the main thread so the game's frame loop
// never waits on inference.
//
// main -> worker
//   { type: 'init', bundleUrl, wasmBase, modelUrl, delegate, hfovDeg, numPoses,
//     frames: [w, h] | null }   frames: also return the camera picture + person
//                               mask (PLAN P-4; segmentation masks on)
//   { type: 'stream', readable }        MediaStreamTrackProcessor frames (preferred)
//   { type: 'frame', image, tsMs, captureSec }   one ImageBitmap/VideoFrame (fallback,
//                                        and the parity harness, which passes
//                                        explicit timestamps)
//   { type: 'reset' }                   drop tracks + filters
// worker -> main
//   { type: 'ready', delegate, loadMs }  | { type: 'error', message }
//   { type: 'pose', floats, count, image2d, width, height, tsMs, captureSec,
//     inferMs, geomMs, raw?, camera?: { width, height, rgb, mask, ms } }
//     camera.mask: one byte per pixel, 0 = nobody, k = the k-th person of
//     `floats` (the same rule as pose_server.py --frames)
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
        outputSegmentationMasks: !!msg.frames,
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

// pose_server.py MASK_THRESHOLD: a pixel belongs to the most confident person
// whose mask passes it.
const MASK_THRESHOLD = 0.5;
let frameCanvas = null;

// The v3 camera block, as pose_server.py camera_block builds it: the frame
// scaled to cfg.frames as RGB888, and the persons' masks (in `persons` order,
// matched through poseIndex) as an ordinal byte per pixel.  As captured, not
// mirrored -- the engine mirrors into the Kinect's view.
function cameraBlock(img, res, persons) {
    const [w, h] = cfg.frames;
    if (!frameCanvas || frameCanvas.width !== w || frameCanvas.height !== h)
        frameCanvas = new OffscreenCanvas(w, h);
    const ctx = frameCanvas.getContext('2d', { willReadFrequently: true });
    ctx.drawImage(img, 0, 0, w, h);
    const rgba = ctx.getImageData(0, 0, w, h).data;
    const rgb = new Uint8Array(w * h * 3);
    for (let i = 0, j = 0; i < rgba.length; i += 4, j += 3) {
        rgb[j] = rgba[i]; rgb[j + 1] = rgba[i + 1]; rgb[j + 2] = rgba[i + 2];
    }
    const mask = new Uint8Array(w * h);
    const best = new Float32Array(w * h).fill(MASK_THRESHOLD);
    const masks = res.segmentationMasks || [];
    persons.forEach((person, k) => {
        const m = masks[person.poseIndex];
        if (!m) return;
        const mw = m.width, mh = m.height, f = m.getAsFloat32Array();
        for (let y = 0; y < h; y++) {
            const row = Math.min(mh - 1, Math.floor((y + 0.5) * mh / h)) * mw;
            for (let x = 0; x < w; x++) {
                const v = f[row + Math.min(mw - 1, Math.floor((x + 0.5) * mw / w))];
                const o = y * w + x;
                if (v > best[o]) { best[o] = v; mask[o] = k + 1; }
            }
        }
    });
    return { width: w, height: h, rgb, mask };
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
    let camera = null;
    if (cfg.frames) {
        // packPersons keeps the first 6: the ordinals must name those
        camera = cameraBlock(img, res, persons.slice(0, count));
        camera.ms = performance.now() - t2;
    }
    if (res.close) res.close(); // frees the segmentation masks
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
    const transfer = [floats.buffer, image2d.buffer];
    if (camera) {
        msg.camera = camera;
        transfer.push(camera.rgb.buffer, camera.mask.buffer);
    }
    post(msg, transfer);
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
