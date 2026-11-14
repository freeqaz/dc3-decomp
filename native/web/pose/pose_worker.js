// DC3 web pose input -- inference worker (module worker).
//
// Runs MediaPipe tasks-vision PoseLandmarker in VIDEO mode (the same
// pose_landmarker_*.task models and options as native/scripts/pose_server.py:
// detection/presence/tracking confidence 0.5) and the DC3 joint geometry
// (pose_geometry.js), off the main thread so the game's frame loop never waits
// on inference.  The pipeline, its costs and every knob: docs/native/WEB.md
// "Pose pipeline (W-17..W-22)".
//
// main -> worker
//   { type: 'init', bundleUrl, wasmBase, modelUrl, delegate, hfovDeg, numPoses,
//     frames: [w, h] | null, slow?: 'STAGE:MS', probeSec?: s, probeWorker?: bool,
//     input?: h, role?: 'probe' }
//       numPoses     PoseLandmarker numPoses (1 or 2)
//       probeSec     > 0 with numPoses >= 2: the second-person probe
//                    (SecondPersonProbe in pose_mediapipe.py): numPoses 1 on
//                    every frame, a numPoses 2 look every probeSec seconds
//       probeWorker  run that look in a second worker (this file, role
//                    'probe') instead of after the frame's own pass here
//       frames       also return the camera picture + person mask (PLAN P-4;
//                    segmentation masks on)
//       input        scale each frame to this height (aspect kept) before
//                    MediaPipe sees it; 0 = as captured.  The landmark model's
//                    input is 256x256 whatever it is given; what shrinks is the
//                    upload, the CPU graph's readback and image conversion, and
//                    the segmentation mask MediaPipe warps back to frame size
//   { type: 'stream', readable }        MediaStreamTrackProcessor frames (preferred)
//   { type: 'frame', image, tsMs, captureSec }   one ImageBitmap/VideoFrame (fallback,
//                                        and the parity/record harnesses, which pass
//                                        explicit timestamps)
//   { type: 'reset' }                   drop tracks + filters
// worker -> main
//   { type: 'ready', delegate, loadMs }  | { type: 'error', message }
//   { type: 'pose', seq, floats, count, image2d, width, height, inW, inH, tsMs,
//     captureSec, capUs, availAt, skipped, inferMs, geomMs, stages: {stage: ms},
//     postedAt, raw?, probe?, camera?: { width, height, rgb, mask, ms } }
//     seq        this worker's pose frame counter (1, 2, ...)
//     capUs      the VideoFrame's capture timestamp (us, the camera clock; on
//                Chromium the monotonic clock, not performance.now's origin)
//     availAt    when the frame reached this worker (timeOrigin-based ms)
//     postedAt   when this message was posted (timeOrigin-based ms)
//     skipped    camera frames the stream dropped before this one, from the
//                capture timestamps (MediaStreamTrackProcessor keeps only the
//                newest frame while inference runs: those are the dropped ones)
//     camera.mask: one byte per pixel, 0 = nobody, k = the k-th person of
//     `floats` (the same rule as pose_server.py --frames)
// probe role: { type: 'probe', image (VideoFrame), tsMs } -> { type: 'probed', found, ms }
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
// With probeWorker the look runs in a second worker on a clone of the frame,
// as desktop runs it on a thread, so the frame that probes costs this worker
// nothing; without it the look runs here after the frame's single pass.
let probe = null;
let cfg = null;
let lastTsMs = -1;
let busy = false;
let seq = 0;

function post(msg, transfer) { self.postMessage(msg, transfer || []); }

async function init(msg) {
    cfg = msg;
    const t0 = performance.now();
    const vision = await import(msg.bundleUrl);
    // `true`: the ES-module wasm loader, which is what a module worker can load
    // (tasks-vision falls back to import() when importScripts is unavailable).
    const fileset = await vision.FilesetResolver.forVisionTasks(msg.wasmBase, true);
    const isProbe = msg.role === 'probe';
    const options = (delegate, numPoses) => ({
        baseOptions: { modelAssetPath: msg.modelUrl, delegate },
        runningMode: 'VIDEO',
        numPoses,
        minPoseDetectionConfidence: 0.5,
        minPosePresenceConfidence: 0.5,
        minTrackingConfidence: 0.5,
        // The probe only counts people: no masks.
        outputSegmentationMasks: !!msg.frames && !isProbe,
    });
    const make = (delegate) => vision.PoseLandmarker.createFromOptions(fileset, options(delegate, msg.numPoses || 2));
    if (typeof msg.slow === 'string') {
        const [stage, ms] = msg.slow.split(':');
        cfg.slow = { stage, ms: +ms || 0 };
    }
    cfg.input = Math.max(0, msg.input | 0);
    // tasks-vision clears self.ModuleFactory once a task has taken it, and the
    // ES-module loader only sets it when first evaluated (import() is cached),
    // so a second createFromOptions in this worker throws "ModuleFactory not
    // set." Hand it the cached module's factory again before each one.
    const refactory = async () => {
        if (!self.ModuleFactory) self.ModuleFactory = (await import(fileset.wasmLoaderPath)).default;
    };
    // AUTO (the default, W-19): the GPU delegate unless this worker has no
    // WebGL2 or only a software rasteriser (SwiftShader, llvmpipe: slower
    // than XNNPACK), then CPU.  GPU/CPU force one (GPU still falls back to
    // CPU if the delegate fails to start).
    let delegate = (msg.delegate || 'CPU').toUpperCase();
    let delegateWhy = delegate === 'AUTO' ? '' : 'asked';
    if (delegate === 'AUTO') {
        const why = gpuUnusable();
        delegate = why ? 'CPU' : 'GPU';
        delegateWhy = why ? 'auto: ' + why : 'auto';
    }
    const wantProbe = !isProbe && (msg.numPoses || 2) >= 2 && msg.probeSec > 0;
    const makeOne = wantProbe ? (d) => vision.PoseLandmarker.createFromOptions(fileset, options(d, 1)) : null;
    try {
        landmarker = await make(delegate);
        if (makeOne) { await refactory(); probe = { single: await makeOne(delegate) }; }
    } catch (e) {
        if (delegate === 'CPU') throw e;
        console.warn('[pose-worker] ' + delegate + ' delegate failed (' + e + '); using CPU');
        delegate = 'CPU';
        delegateWhy = 'GPU failed: ' + String(e && e.message || e).slice(0, 80);
        probe = null;
        await refactory();
        landmarker = await make(delegate);
        if (makeOne) { await refactory(); probe = { single: await makeOne(delegate) }; }
    }
    if (probe) {
        Object.assign(probe, {
            intervalMs: msg.probeSec * 1000, holdMs: 1000, mode: 'single',
            lastProbeMs: null, fewSinceMs: null, probes: 0, switches: 0,
            worker: null, inFlight: false, lastMs: null, workerFailed: false,
        });
        if (msg.probeWorker) {
            // The probe worker loads its own tasks-vision + model (CPU: it
            // must not compete with this worker's GPU work, and it runs twice
            // a second).  Until it is ready the in-worker look stands in.
            try {
                const w = new Worker(new URL(import.meta.url), { type: 'module' });
                const ready = new Promise((resolve, reject) => {
                    w.onmessage = (ev) => {
                        if (ev.data.type === 'ready') resolve();
                        else if (ev.data.type === 'error') reject(new Error(ev.data.message));
                        else if (ev.data.type === 'probed') onProbed(ev.data);
                    };
                });
                w.postMessage({ ...msg, role: 'probe', numPoses: msg.numPoses || 2, delegate: 'CPU', frames: null,
                    probeSec: 0, probeWorker: false, slow: null });
                ready.then(() => { probe.worker = w; }, (e) => {
                    probe.workerFailed = true;
                    console.warn('[pose-worker] probe worker failed (' + e + '); probing in this worker');
                });
            } catch (e) {
                probe.workerFailed = true;
                console.warn('[pose-worker] no nested worker (' + e + '); probing in this worker');
            }
        }
    }
    geometry = new PoseGeometry({ hfovDeg: msg.hfovDeg });
    post({ type: 'ready', delegate, delegateWhy, loadMs: performance.now() - t0 });
}

// Why the GPU delegate would not help here, or null.  The delegate runs on
// WebGL2 in this worker (tasks-vision's own OffscreenCanvas), so that is
// what is asked; a software renderer is reported by its name.
function gpuUnusable() {
    try {
        if (typeof OffscreenCanvas === 'undefined') return 'no OffscreenCanvas';
        const gl = new OffscreenCanvas(1, 1).getContext('webgl2');
        if (!gl) return 'no WebGL2';
        const ext = gl.getExtension('WEBGL_debug_renderer_info');
        const r = String(gl.getParameter(ext ? ext.UNMASKED_RENDERER_WEBGL : gl.RENDERER) || '');
        const lose = gl.getExtension('WEBGL_lose_context');
        if (lose) lose.loseContext();
        if (/swiftshader|llvmpipe|softpipe|software|basic render/i.test(r)) return 'software WebGL (' + r.slice(0, 60) + ')';
        return null;
    } catch (e) {
        return 'WebGL2 check failed (' + e + ')';
    }
}

function sourceSize(img) {
    if (img.displayWidth !== undefined) return [img.displayWidth, img.displayHeight];
    return [img.width, img.height];
}

// pose_server.py MASK_THRESHOLD: a pixel belongs to the most confident person
// whose mask passes it.
const MASK_THRESHOLD = 0.5;
let frameCanvas = null;
let inCanvas = null;

// ?pose_slow=STAGE:MS -- busy work added to one stage: the instrument check
// for scripts/pose_bench/web_pose_fps.mjs (that stage, and only it, grows).
function slow(stage) {
    if (!cfg.slow || cfg.slow.stage !== stage) return;
    const end = performance.now() + cfg.slow.ms;
    while (performance.now() < end) { /* spin */ }
}

// The frame MediaPipe sees: the camera frame itself, or (cfg.input) the frame
// scaled to that height through a 2D canvas, as ImageData.  The same pixels
// serve the P-4 picture when it has the same size (`rgba`), so the scale is
// paid once.
function modelInput(img, width, height, st) {
    if (!cfg.input || cfg.input >= height) return { image: img, w: width, h: height, rgba: null };
    const t = performance.now();
    const h = cfg.input, w = Math.max(16, Math.round(width * h / height));
    if (!inCanvas || inCanvas.width !== w || inCanvas.height !== h) inCanvas = new OffscreenCanvas(w, h);
    const ctx = inCanvas.getContext('2d', { willReadFrequently: true });
    ctx.drawImage(img, 0, 0, w, h);
    const data = ctx.getImageData(0, 0, w, h);
    st.scale = performance.now() - t;
    return { image: data, w, h, rgba: data.data };
}

// The v3 camera block, as pose_server.py camera_block builds it: the frame
// scaled to cfg.frames as RGB888, and the persons' masks (in `persons` order,
// matched through poseIndex) as an ordinal byte per pixel.  As captured, not
// mirrored -- the engine mirrors into the Kinect's view.
function cameraBlock(img, input, res, persons, st) {
    const [w, h] = cfg.frames;
    let t = performance.now();
    slow('camera');
    let rgba = null;
    if (input.rgba && input.w === w && input.h === h) {
        rgba = input.rgba; // the model's input is the picture: no second scale
        st.draw = 0;
    } else {
        if (!frameCanvas || frameCanvas.width !== w || frameCanvas.height !== h)
            frameCanvas = new OffscreenCanvas(w, h);
        const ctx = frameCanvas.getContext('2d', { willReadFrequently: true });
        ctx.drawImage(img, 0, 0, w, h);
        rgba = ctx.getImageData(0, 0, w, h).data;
        st.draw = performance.now() - t;
    }
    if (st.draw) t += st.draw;
    let t1;
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

function onProbed(r) {
    const p = probe;
    p.inFlight = false;
    p.lastMs = r.ms;
    if (r.found >= 2 && p.mode === 'single') {
        // Two people: numPoses N from the next frame on.
        p.mode = 'multi'; p.fewSinceMs = null; p.switches++;
    }
}

// One frame's PoseLandmarker result: `landmarker` (numPoses N), or the probe's
// choice of single/multi. st.probe = the in-worker probe pass's ms on the
// frames it ran.
function detect(img, tsMs, st, frame) {
    if (!probe) return landmarker.detectForVideo(img, tsMs);
    const p = probe;
    if (p.mode === 'single') {
        const res = p.single.detectForVideo(img, tsMs);
        if (p.lastProbeMs === null || tsMs - p.lastProbeMs >= p.intervalMs) {
            if (p.worker && frame && typeof frame.clone === 'function') {
                // Hand the probe worker its own reference to this frame (a
                // VideoFrame clone shares the pixels); skip this look if the
                // last one has not come back.
                if (!p.inFlight) {
                    p.lastProbeMs = tsMs;
                    p.probes++;
                    p.inFlight = true;
                    const c = frame.clone();
                    p.worker.postMessage({ type: 'probe', image: c, tsMs }, [c]);
                }
                return res;
            }
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

let lastCapUs = null;
let capPeriodUs = null;

function processImage(img, tsMs, captureSec, wantRaw, waitMs, capUs, availAt) {
    // VIDEO mode needs strictly increasing timestamps.
    if (tsMs <= lastTsMs) tsMs = lastTsMs + 1;
    lastTsMs = tsMs;
    const [width, height] = sourceSize(img);
    const st = {};   // per-stage ms (scripts/pose_bench/web_pose_fps.mjs)
    if (waitMs !== undefined) st.wait = waitMs;
    const t0 = performance.now();
    // Frames the stream dropped while we were busy: the gap to the previous
    // processed frame in camera periods (the shortest gap seen is the period).
    let skipped = 0;
    if (capUs !== undefined && capUs !== null) {
        if (lastCapUs !== null) {
            const gap = capUs - lastCapUs;
            if (gap > 0 && (capPeriodUs === null || gap < capPeriodUs * 0.9)) capPeriodUs = gap;
            if (capPeriodUs) skipped = Math.max(0, Math.round(gap / capPeriodUs) - 1);
        }
        lastCapUs = capUs;
    }
    const input = modelInput(img, width, height, st);
    const t0i = performance.now();
    slow('infer');
    const res = detect(input.image, tsMs, st, img);
    const t1 = performance.now();
    const poses = posesFromResult(res);
    const persons = geometry.process(poses, width, height, tsMs);
    const { floats, count } = packPersons(persons);
    const t2 = performance.now();
    st.infer = t1 - t0i;
    st.geom = t2 - t1;
    let camera = null;
    if (cfg.frames) {
        // packPersons keeps the first 6: the ordinals must name those
        camera = cameraBlock(img, input, res, persons.slice(0, count), st);
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
        type: 'pose', seq: ++seq, floats, count, image2d, numPoses: poses.length,
        width, height, inW: input.w, inH: input.h, tsMs, captureSec,
        capUs: capUs === undefined ? null : capUs, availAt: availAt === undefined ? null : availAt, skipped,
        inferMs: t1 - t0i, geomMs: t2 - t1,
        stages: st, postedAt: performance.timeOrigin + performance.now(),
    };
    if (probe) msg.probe = { mode: probe.mode, probes: probe.probes, switches: probe.switches,
        worker: !!probe.worker, lastMs: probe.lastMs };
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
        const availAt = performance.timeOrigin + performance.now();
        const waitMs = performance.now() - w0;
        if (done) break;
        try {
            // VideoFrame.timestamp is the capture time in microseconds: it is
            // both MediaPipe's VIDEO-mode clock and the sensor timestamp the
            // engine takes frame intervals from (as Xbox does from
            // NUI_SKELETON_FRAME), so a late-delivered frame keeps its true
            // interval.
            const captureSec = frame.timestamp ? frame.timestamp / 1e6 : performance.now() / 1000;
            processImage(frame, captureSec * 1000, captureSec, false, waitMs, frame.timestamp || null, availAt);
        } catch (e) {
            post({ type: 'error', message: 'inference: ' + e });
        } finally {
            frame.close();
        }
    }
    post({ type: 'ended' });
}

// role 'probe': count the people in one frame with numPoses N.
function probeFrame(msg) {
    let found = 0;
    const t = performance.now();
    try {
        let ts = msg.tsMs;
        if (ts <= lastTsMs) ts = lastTsMs + 1;
        lastTsMs = ts;
        const r = landmarker.detectForVideo(msg.image, ts);
        found = r.landmarks ? r.landmarks.length : 0;
        if (r.close) r.close();
    } finally {
        msg.image.close && msg.image.close();
    }
    post({ type: 'probed', found, ms: performance.now() - t });
}

self.onmessage = async (ev) => {
    const msg = ev.data;
    try {
        if (msg.type === 'init') {
            await init(msg);
        } else if (msg.type === 'stream') {
            pumpStream(msg.readable);
        } else if (msg.type === 'probe') {
            probeFrame(msg);
        } else if (msg.type === 'frame') {
            if (busy) { msg.image.close && msg.image.close(); return; }
            busy = true;
            try {
                processImage(msg.image, msg.tsMs, msg.captureSec, !!msg.wantRaw, undefined,
                    msg.image.timestamp !== undefined ? msg.image.timestamp : null,
                    performance.timeOrigin + performance.now());
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
