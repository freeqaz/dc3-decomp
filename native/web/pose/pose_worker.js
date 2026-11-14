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
//     frames: [w, h] | null, slow?: 'STAGE:MS', probeSec?: s }
//                               probeSec > 0 with numPoses >= 2: the second-person
//                               probe (SecondPersonProbe in pose_mediapipe.py)
//                               frames: also return the camera picture + person
//                               mask (PLAN P-4; segmentation masks on)
//   { type: 'stream', readable }        MediaStreamTrackProcessor frames (preferred)
//   { type: 'frame', image, tsMs, captureSec }   one ImageBitmap/VideoFrame (fallback,
//                                        and the parity harness, which passes
//                                        explicit timestamps)
//   { type: 'reset' }                   drop tracks + filters
// worker -> main
//   { type: 'ready', delegate, loadMs }  | { type: 'error', message }
//   { type: 'pose', floats, count, image2d, width, height, tsMs, captureSec,
//     inferMs, geomMs, stages: {stage: ms}, postedAt (timeOrigin-based ms),
//     raw?, camera?: { width, height, rgb, mask, ms } }
//     camera.mask: one byte per pixel, 0 = nobody, k = the k-th person of
//     `floats` (the same rule as pose_server.py --frames)
import { PoseGeometry, posesFromResult, packPersons } from './pose_geometry.js';

let landmarker = null;
let geometry = null;
// Second-person probe (PLAN L-3; pose_mediapipe.py SecondPersonProbe). With
// numPoses 2 and one dancer, PoseLandmarker re-runs its person detector on
// every frame (it tracks fewer people than it was asked for), which is most
// of the difference between numPoses 1 and 2. So: `single` (numPoses 1) on
// every frame while fewer than two people are seen, `multi` (numPoses N) on
// the same frame every probeSec as a probe, and on every frame once it has
// seen two, back to single after it has seen fewer than two for 1 s. The
// landmarks always come from one landmarker's own result: with one player the
// output is numPoses 1's (what the scoring calibration was recorded at).
// Desktop runs the probe on a thread; here it runs on the same worker, after
// the single pass of that frame (one frame in probeSec costs both).
let probe = null;
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
    if (typeof msg.slow === 'string') {
        const [stage, ms] = msg.slow.split(':');
        cfg.slow = { stage, ms: +ms || 0 };
    }
    // tasks-vision clears self.ModuleFactory once a task has taken it, and the
    // ES-module loader only sets it when first evaluated (import() is cached),
    // so a second createFromOptions in this worker throws "ModuleFactory not
    // set." Hand it the cached module's factory again before each one.
    const refactory = async () => {
        if (!self.ModuleFactory) self.ModuleFactory = (await import(fileset.wasmLoaderPath)).default;
    };
    let delegate = msg.delegate || 'CPU';
    const wantProbe = (msg.numPoses || 2) >= 2 && msg.probeSec > 0;
    const makeOne = wantProbe
        ? (d) => vision.PoseLandmarker.createFromOptions(fileset, {
            baseOptions: { modelAssetPath: msg.modelUrl, delegate: d },
            runningMode: 'VIDEO', numPoses: 1,
            minPoseDetectionConfidence: 0.5, minPosePresenceConfidence: 0.5, minTrackingConfidence: 0.5,
            outputSegmentationMasks: !!msg.frames,
        })
        : null;
    try {
        landmarker = await make(delegate);
        if (makeOne) { await refactory(); probe = { single: await makeOne(delegate) }; }
    } catch (e) {
        if (delegate === 'CPU') throw e;
        console.warn('[pose-worker] ' + delegate + ' delegate failed (' + e + '); using CPU');
        delegate = 'CPU';
        probe = null;
        await refactory();
        landmarker = await make(delegate);
        if (makeOne) { await refactory(); probe = { single: await makeOne(delegate) }; }
    }
    if (probe) Object.assign(probe, {
        intervalMs: msg.probeSec * 1000, holdMs: 1000, mode: 'single',
        lastProbeMs: null, fewSinceMs: null, probes: 0, switches: 0,
    });
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
// ?pose_slow=STAGE:MS -- busy work added to one stage: the instrument check
// for scripts/pose_bench/web_pose_fps.mjs (that stage, and only it, grows).
function slow(stage) {
    if (!cfg.slow || cfg.slow.stage !== stage) return;
    const end = performance.now() + cfg.slow.ms;
    while (performance.now() < end) { /* spin */ }
}

function cameraBlock(img, res, persons, st) {
    const [w, h] = cfg.frames;
    let t = performance.now();
    slow('camera');
    if (!frameCanvas || frameCanvas.width !== w || frameCanvas.height !== h)
        frameCanvas = new OffscreenCanvas(w, h);
    const ctx = frameCanvas.getContext('2d', { willReadFrequently: true });
    ctx.drawImage(img, 0, 0, w, h);
    const rgba = ctx.getImageData(0, 0, w, h).data;
    let t1 = performance.now(); st.draw = t1 - t; t = t1;
    const n = w * h;
    const rgb = new Uint8Array(n * 3);
    for (let i = 0, j = 0; i < rgba.length; i += 4, j += 3) {
        rgb[j] = rgba[i]; rgb[j + 1] = rgba[i + 1]; rgb[j + 2] = rgba[i + 2];
    }
    t1 = performance.now(); st.rgb = t1 - t; t = t1;
    const mask = new Uint8Array(n);
    const best = new Float32Array(n).fill(MASK_THRESHOLD);
    const masks = res.segmentationMasks || [];
    let getMs = 0;
    persons.forEach((person, k) => {
        const m = masks[person.poseIndex];
        if (!m) return;
        const g0 = performance.now();
        const f = m.getAsFloat32Array();
        getMs += performance.now() - g0;
        const mw = m.width, mh = m.height;
        for (let y = 0; y < h; y++) {
            const row = Math.min(mh - 1, Math.floor((y + 0.5) * mh / h)) * mw;
            for (let x = 0; x < w; x++) {
                const v = f[row + Math.min(mw - 1, Math.floor((x + 0.5) * mw / w))];
                const o = y * w + x;
                if (v > best[o]) { best[o] = v; mask[o] = k + 1; }
            }
        }
    });
    st.maskget = getMs;
    st.mask = performance.now() - t - getMs;
    return { width: w, height: h, rgb, mask };
}

// One frame's PoseLandmarker result: `landmarker` (numPoses N), or the probe's
// choice of single/multi. st.probe = the probe pass's ms on the frames it ran.
function detect(img, tsMs, st) {
    if (!probe) return landmarker.detectForVideo(img, tsMs);
    const p = probe;
    if (p.mode === 'single') {
        const res = p.single.detectForVideo(img, tsMs);
        if (p.lastProbeMs === null || tsMs - p.lastProbeMs >= p.intervalMs) {
            p.lastProbeMs = tsMs;
            p.probes++;
            const t = performance.now();
            const pr = landmarker.detectForVideo(img, tsMs);
            const found = pr.landmarks ? pr.landmarks.length : 0;
            st.probe = performance.now() - t;
            if (found >= 2) {
                // Two people: this frame already has multi's answer, use it.
                p.mode = 'multi'; p.fewSinceMs = null; p.switches++;
                if (res.close) res.close();
                return pr;
            }
            if (pr.close) pr.close();
        }
        return res;
    }
    const res = landmarker.detectForVideo(img, tsMs);
    const n = res.landmarks ? res.landmarks.length : 0;
    if (n >= 2) p.fewSinceMs = null;
    else if (p.fewSinceMs === null) p.fewSinceMs = tsMs;
    else if (tsMs - p.fewSinceMs >= p.holdMs) {
        p.mode = 'single'; p.switches++;
        p.lastProbeMs = tsMs; // just looked: next probe a full interval on
    }
    return res;
}

function processImage(img, tsMs, captureSec, wantRaw, waitMs) {
    // VIDEO mode needs strictly increasing timestamps.
    if (tsMs <= lastTsMs) tsMs = lastTsMs + 1;
    lastTsMs = tsMs;
    const [width, height] = sourceSize(img);
    const st = {};   // per-stage ms (scripts/pose_bench/web_pose_fps.mjs)
    if (waitMs !== undefined) st.wait = waitMs;
    const t0 = performance.now();
    slow('infer');
    const res = detect(img, tsMs, st);
    const t1 = performance.now();
    const poses = posesFromResult(res);
    const persons = geometry.process(poses, width, height, tsMs);
    const { floats, count } = packPersons(persons);
    const t2 = performance.now();
    st.infer = t1 - t0;
    st.geom = t2 - t1;
    let camera = null;
    if (cfg.frames) {
        // packPersons keeps the first 6: the ordinals must name those
        camera = cameraBlock(img, res, persons.slice(0, count), st);
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
    st.busy = performance.now() - t0;
    const msg = {
        type: 'pose', floats, count, image2d, numPoses: poses.length,
        width, height, tsMs, captureSec, inferMs: t1 - t0, geomMs: t2 - t1,
        stages: st, postedAt: performance.timeOrigin + performance.now(),
    };
    if (probe) msg.probe = { mode: probe.mode, probes: probe.probes, switches: probe.switches };
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
        const w0 = performance.now();
        const { value: frame, done } = await reader.read();
        const waitMs = performance.now() - w0;
        if (done) break;
        try {
            // VideoFrame.timestamp is the capture time in microseconds: it is
            // both MediaPipe's VIDEO-mode clock and the sensor timestamp the
            // engine takes frame intervals from (as Xbox does from
            // NUI_SKELETON_FRAME), so a late-delivered frame keeps its true
            // interval.
            const captureSec = frame.timestamp ? frame.timestamp / 1e6 : performance.now() / 1000;
            processImage(frame, captureSec * 1000, captureSec, false, waitMs);
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
