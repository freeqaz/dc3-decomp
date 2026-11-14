// DC3 web pose input -- camera, worker, engine push, and the player-facing
// camera panel. Loaded by index.html as a module.
//
//   getUserMedia --(MediaStreamTrackProcessor | createImageBitmap)--> pose_worker.js
//     (tasks-vision PoseLandmarker VIDEO + pose_geometry.js = pose_mediapipe.py)
//   --> Module.dc3PosePush (dc3_pre.js) --> dc3_pose_push (PoseInput_Web.cpp)
//   --> NativeSkeletonProvider::PublishFrame --> the game's skeleton + scoring
//
// URL parameters (all optional):
//   pose=off            no camera, the game scores the stand-in skeleton
//   pose=manual         do not ask for the camera until the button is clicked
//   pose_model=lite|full|heavy   (default full, the desktop default)
//   pose_delegate=CPU|GPU        (default CPU, as the desktop server runs)
//   pose_transport=stream|bitmap (default stream when MediaStreamTrackProcessor exists)
//   pose_cdn=1          load tasks-vision + model from the CDN instead of this server
//                       (the only way the page uses a CDN: a server without them
//                       is an error, not a silent fallback)
//   pose_hfov=<deg>     camera horizontal FOV (default 58.51, DC3's Kinect)
//   pose_frames=WxH|off the camera picture + person mask the game draws the
//                       player view from (PLAN P-4; default 320x240, the
//                       desktop default; off = skeleton only)
//   pose_persons=1|2    MediaPipe numPoses (default 2; pose_server.py
//                       --max-persons). 1 is ~40% less inference but finds
//                       one player only (POSE_INPUT.md#performance)
//   pose_probe=<s>      with pose_persons=2: numPoses 1 on every frame, and a
//                       numPoses 2 probe every <s> seconds that switches to 2
//                       when it sees a second person (pose_mediapipe.py
//                       --probe-second-person; 0 = off, numPoses 2 every frame)
//   pose_slow=STAGE:MS  instrument check: busy work in one worker stage
//                       (scripts/pose_bench/web_pose_fps.mjs)
//
// Test hooks: window.dc3PoseStats (page side) and window.dc3PoseGame (what the
// engine sees, published by PoseInput_Web.cpp).

const TASKS_VISION_VERSION = '1.0.0';
const CDN_BASE = `https://cdn.jsdelivr.net/npm/@mediapipe/tasks-vision@${TASKS_VISION_VERSION}`;
const CDN_MODEL = (m) =>
    `https://storage.googleapis.com/mediapipe-models/pose_landmarker/pose_landmarker_${m}/float16/1/pose_landmarker_${m}.task`;

const params = new URLSearchParams(location.search);
const opt = {
    mode: params.get('pose') || 'auto',
    model: params.get('pose_model') || 'full',
    delegate: (params.get('pose_delegate') || 'CPU').toUpperCase(),
    transport: params.get('pose_transport') ||
        (typeof MediaStreamTrackProcessor !== 'undefined' ? 'stream' : 'bitmap'),
    cdn: params.get('pose_cdn') === '1',
    hfov: params.get('pose_hfov') ? parseFloat(params.get('pose_hfov')) : 58.51,
    frames: parseFrames(params.get('pose_frames')),
    // Instrument check only (scripts/pose_bench/web_pose_fps.mjs): STAGE:MS of
    // busy work in one worker stage.
    slow: params.get('pose_slow') || null,
    // MediaPipe numPoses (pose_server.py --max-persons, capped at 2 the same
    // way). 1 skips the per-frame person detector that 2 runs while fewer
    // than 2 people are tracked, but a second player is never found.
    persons: Math.min(2, Math.max(1, parseInt(params.get('pose_persons') || '2', 10) || 2)),
    probeSec: Math.max(0, parseFloat(params.get('pose_probe') || '0') || 0),
};

function parseFrames(v) {
    if (v === 'off' || v === '0') return null;
    const m = /^(\d+)x(\d+)$/.exec(v || '320x240');
    if (!m) return [320, 240];
    const w = Math.min(1920, Math.max(16, +m[1])), h = Math.min(1920, Math.max(16, +m[2]));
    return [w, h];
}

const stats = window.dc3PoseStats = {
    state: 'idle', detail: '', model: opt.model, delegate: opt.delegate,
    transport: opt.transport, source: null, loadMs: null,
    framesIn: 0, framesWithPerson: 0, pushed: 0, pushRejected: 0,
    lastCount: 0, inferMs: [], mainMs: [], fps: 0, camera: null,
    frames: opt.frames, picturesPushed: 0, picturesRejected: 0, pictureMs: [],
    stageMs: {},   // per-stage rings: the worker's msg.stages + xfer + main
};
const RING = 600;   // 20 s at 30 fps: a measurement window's worth
function ring(arr, v) { arr.push(v); if (arr.length > RING) arr.shift(); }
// summary({reset: true}) also empties the rings, so the next call describes
// only what came after (a measurement window).
export function summary(o) {
    const q = (a, p) => { if (!a.length) return null; const s = [...a].sort((x, y) => x - y); return +s[Math.min(s.length - 1, Math.floor(p * s.length))].toFixed(2); };
    const stages = {};
    for (const [k, a] of Object.entries(stats.stageMs)) stages[k] = [q(a, 0.5), q(a, 0.95)];
    const out = {
        ...stats, inferMs: undefined, mainMs: undefined, stageMs: undefined, stages,
        inferMsP50: q(stats.inferMs, 0.5), inferMsP95: q(stats.inferMs, 0.95),
        pictureMs: undefined, pictureMsP50: q(stats.pictureMs, 0.5),
        pictureMsP95: q(stats.pictureMs, 0.95),
        mainMsP50: q(stats.mainMs, 0.5), mainMsP95: q(stats.mainMs, 0.95),
    };
    if (o && o.reset) {
        stats.inferMs.length = 0; stats.mainMs.length = 0; stats.pictureMs.length = 0;
        stats.stageMs = {};
    }
    return out;
}
window.dc3PoseSummary = summary;

// ---------------------------------------------------------------- UI panel
const css = `
/* Bottom centre, inside the game's bottom helpbar band (the lowest ~12% of
   the 16:9 frame), whose left and right ends carry the hand menus' back and
   help items; sized from the game frame (cq units), not the window (W-13). */
#canvas-container { container-type:size; }
#pose-panel { position:absolute; left:50%; bottom:1.2cqh; transform:translateX(-50%);
  display:flex; align-items:flex-end; gap:0.8cqh; max-width:48cqw; z-index:10;
  font:clamp(10px, 1.65cqh, 15px)/1.3 'Segoe UI',system-ui,sans-serif; color:#eee;
  pointer-events:auto; }
#pose-view { position:relative; flex:none; height:10cqh; aspect-ratio:4/3; background:#000c;
  border:1px solid #444; border-radius:6px; overflow:hidden; display:none; }
#pose-view video, #pose-view canvas { position:absolute; inset:0; width:100%; height:100%;
  object-fit:cover; transform:scaleX(-1); }
#pose-view video { opacity:0.55; }
#pose-side { display:flex; flex-direction:column; align-items:flex-start; gap:0.5cqh; min-width:0; }
#pose-status { box-sizing:border-box; max-width:38cqw; padding:3px 7px; border-radius:4px;
  background:#000b; border:1px solid #444; overflow-wrap:anywhere; }
#pose-status.ok { border-color:#3c6; color:#9f9; }
#pose-status.warn { border-color:#c93; color:#fc6; }
#pose-status.err { border-color:#c44; color:#f99; }
#pose-btn { display:none; background:#1a1a1a; color:#ccc; border:1px solid #555;
  border-radius:4px; padding:3px 10px; cursor:pointer; font:inherit; }
`;
let ui = null;
function buildUi() {
    const host = document.getElementById('canvas-container') || document.body;
    const style = document.createElement('style');
    style.textContent = css;
    document.head.appendChild(style);
    const panel = document.createElement('div');
    panel.id = 'pose-panel';
    panel.innerHTML = '<div id="pose-view"><video muted playsinline></video><canvas width="320" height="240"></canvas></div>' +
        '<div id="pose-side"><div id="pose-status">Camera: starting…</div><button id="pose-btn">Use camera</button></div>';
    host.appendChild(panel);
    ui = {
        view: panel.querySelector('#pose-view'), video: panel.querySelector('video'),
        canvas: panel.querySelector('canvas'), status: panel.querySelector('#pose-status'),
        btn: panel.querySelector('#pose-btn'),
    };
    ui.ctx = ui.canvas.getContext('2d');
    ui.btn.addEventListener('click', () => { ui.btn.style.display = 'none'; start(); });
}

function setState(state, text, cls, showButton) {
    stats.state = state;
    stats.detail = text;
    if (!ui) return;
    ui.status.textContent = text;
    ui.status.className = cls || '';
    ui.btn.style.display = showButton ? 'inline-block' : 'none';
    console.log('[dc3-pose] state=' + state + ' ' + text);
}

// BlazePose connections drawn in the preview (arms, torso, legs, face line).
const BONES = [[11, 12], [11, 13], [13, 15], [12, 14], [14, 16], [11, 23], [12, 24], [23, 24],
    [23, 25], [25, 27], [27, 31], [24, 26], [26, 28], [28, 32], [7, 8], [15, 19], [16, 20]];
function drawOverlay(image2d, numPoses) {
    const { ctx, canvas } = ui;
    ctx.clearRect(0, 0, canvas.width, canvas.height);
    for (let p = 0; p < numPoses; p++) {
        const pt = (k) => [image2d[(p * 33 + k) * 3] * canvas.width, image2d[(p * 33 + k) * 3 + 1] * canvas.height, image2d[(p * 33 + k) * 3 + 2]];
        ctx.lineWidth = 3;
        for (const [a, b] of BONES) {
            const A = pt(a), B = pt(b);
            const v = Math.min(A[2], B[2]);
            if (v < 0.3) continue; // the engine treats these as untracked (held)
            ctx.strokeStyle = v < 0.6 ? '#fc6' : (p === 0 ? '#5f8' : '#6cf');
            ctx.beginPath(); ctx.moveTo(A[0], A[1]); ctx.lineTo(B[0], B[1]); ctx.stroke();
        }
    }
}

// ---------------------------------------------------------------- engine push
function engineReady() {
    return typeof window.wasmReady !== 'undefined' && window.wasmReady &&
        window.Module && typeof window.Module.dc3PosePush === 'function';
}
function pushToEngine(floats, count, captureSec) {
    if (!engineReady()) { stats.pushRejected++; return false; }
    let ok = 0;
    try { ok = window.Module.dc3PosePush(floats, count, captureSec); } catch (e) { ok = 0; }
    if (ok) stats.pushed++; else stats.pushRejected++;
    return !!ok;
}

// The frame's picture + mask (PLAN P-4), right after its skeletons: the mask's
// ordinals name the persons in `floats`.  dc3_camera_push (CameraInput_Web.cpp)
// copies globalThis.dc3CameraPending into the engine.
function pushCameraToEngine(msg) {
    const M = window.Module;
    if (!M || typeof M._dc3_camera_push !== 'function') { stats.picturesRejected++; return; }
    const n = msg.count;
    const trackIds = new Int32Array(n), depthM = new Float32Array(n);
    for (let i = 0; i < n; i++) {
        trackIds[i] = msg.floats[i * 81];
        depthM[i] = msg.floats[i * 81 + 3]; // joint 0 (hip centre) z
    }
    const c = msg.camera;
    globalThis.dc3CameraPending = { rgb: c.rgb, mask: c.mask, trackIds, depthM };
    let ok = 0;
    try { ok = M._dc3_camera_push(c.width, c.height, n, msg.captureSec); } catch (e) { ok = 0; }
    if (ok) stats.picturesPushed++; else stats.picturesRejected++;
    ring(stats.pictureMs, c.ms);
}

// ---------------------------------------------------------------- pipeline
let worker = null;
let stream = null;
let lastPoseT = 0;

export async function resolveSources() {
    const local = {
        bundleUrl: new URL('pose/vendor/tasks-vision/vision_bundle.mjs', location.href).href,
        wasmBase: new URL('pose/vendor/tasks-vision/wasm', location.href).href,
        modelUrl: new URL(`pose/models/pose_landmarker_${opt.model}.task`, location.href).href,
        source: 'local',
    };
    const cdn = { bundleUrl: `${CDN_BASE}/vision_bundle.mjs`, wasmBase: `${CDN_BASE}/wasm`, modelUrl: CDN_MODEL(opt.model), source: 'cdn' };
    if (opt.cdn) return cdn;
    const exists = async (u) => { try { return (await fetch(u, { method: 'HEAD' })).ok; } catch (e) { return false; } };
    const [b, m] = await Promise.all([exists(local.bundleUrl), exists(local.modelUrl)]);
    if (b && m) return local;
    // Never fall back to a CDN silently (W-13): the page would quietly depend
    // on jsdelivr / storage.googleapis.com from inside its COOP/COEP origin.
    // A deploy without them is a broken deploy (scripts/web/build.sh fails on
    // it); ?pose_cdn=1 is the explicit opt-in.
    const missing = [b ? null : 'tasks-vision', m ? null : `pose_landmarker_${opt.model}.task`].filter(Boolean).join(' + ');
    throw new Error(`not deployed on this server: ${missing} (scripts/web/build.sh vendors them; ?pose_cdn=1 loads them from the CDN)`);
}

function onWorkerMessage(ev) {
    const msg = ev.data;
    if (msg.type === 'pose') {
        const t0 = performance.now();
        stats.framesIn++;
        stats.lastCount = msg.count;
        if (msg.count > 0) stats.framesWithPerson++;
        ring(stats.inferMs, msg.inferMs + msg.geomMs);
        stats.numPoses = msg.numPoses;
        if (msg.probe) stats.probe = msg.probe;
        const stageRing = (k, v) => ring(stats.stageMs[k] || (stats.stageMs[k] = []), v);
        if (msg.stages) for (const [k, v] of Object.entries(msg.stages)) stageRing(k, v);
        if (msg.postedAt) stageRing('xfer', performance.timeOrigin + t0 - msg.postedAt);
        if (lastPoseT) {
            const dt = (t0 - lastPoseT) / 1000;
            stats.fps = +(0.9 * (stats.fps || 1 / dt) + 0.1 / dt).toFixed(1);
        }
        lastPoseT = t0;
        // Test hook: window.dc3PoseHold(true) sends the engine empty frames
        // (nobody in view) until dc3PoseHold(false), so a driver can work the
        // menus with the keyboard while a fake camera dances: the dancer's
        // hand cursor otherwise picks menu items on its own
        // (scripts/web/pose-gameplay.mjs).
        const pushed = hold ? pushToEngine(new Float32Array(0), 0, msg.captureSec)
            : pushToEngine(msg.floats, msg.count, msg.captureSec);
        if (pushed && msg.camera && !hold) pushCameraToEngine(msg);
        drawOverlay(msg.image2d, msg.numPoses);
        if (!pushed) setStateOnce('waiting', 'Camera ready — waiting for the game…', 'warn');
        else if (msg.count === 0) setStateOnce('noperson', 'No one in view — step into the camera', 'warn');
        else setStateOnce('tracking', 'Tracking you ✓', 'ok');
        ring(stats.mainMs, performance.now() - t0);
        ring(stats.stageMs.main || (stats.stageMs.main = []), performance.now() - t0);
        if (frameCb) frameCb(msg);
    } else if (msg.type === 'ready') {
        stats.loadMs = Math.round(msg.loadMs);
        stats.delegate = msg.delegate;
        console.log(`[dc3-pose] model ready: ${opt.model} delegate=${msg.delegate} load=${stats.loadMs}ms source=${stats.source}`);
    } else if (msg.type === 'error') {
        console.error('[dc3-pose] worker: ' + msg.message);
        if (stats.state === 'loading') setState('error', 'Pose model failed to load — playing without camera', 'err', true);
    }
}
function setStateOnce(state, text, cls) { if (stats.state !== state) setState(state, text, cls, false); }

let hold = false;
window.dc3PoseHold = (on) => { hold = !!on; stats.held = hold; };
let frameCb = null;
export function onPose(cb) { frameCb = cb; }

export async function createWorker(sources) {
    const w = new Worker(new URL('./pose_worker.js', import.meta.url), { type: 'module' });
    const ready = new Promise((resolve, reject) => {
        const h = (ev) => {
            if (ev.data.type === 'ready') { w.removeEventListener('message', h); resolve(ev.data); }
            if (ev.data.type === 'error') { w.removeEventListener('message', h); reject(new Error(ev.data.message)); }
        };
        w.addEventListener('message', h);
    });
    w.postMessage({ type: 'init', ...sources, delegate: opt.delegate, hfovDeg: opt.hfov, numPoses: opt.persons,
        frames: opt.frames, slow: opt.slow, probeSec: opt.probeSec });
    return { worker: w, ready };
}

async function start() {
    if (!navigator.mediaDevices || !navigator.mediaDevices.getUserMedia) {
        setState('nocamera', 'No camera API in this browser — playing without camera', 'err');
        return;
    }
    setState('requesting', 'Allow camera access to dance (see the browser prompt)', 'warn');
    try {
        // 640x480 like the desktop server's request; the geometry is
        // aspect-correct (one focal length from the horizontal FOV) either way.
        stream = await navigator.mediaDevices.getUserMedia({
            // facingMode 'user': a phone's front camera, which faces the
            // player (a laptop's one camera satisfies it either way).
            video: { width: { ideal: 640 }, height: { ideal: 480 }, frameRate: { ideal: 30 }, facingMode: { ideal: 'user' } },
            audio: false,
        });
    } catch (e) {
        const name = e && e.name;
        if (name === 'NotAllowedError' || name === 'SecurityError')
            setState('denied', 'Camera blocked — the game is using a stand-in player. Allow it to dance.', 'err', true);
        else if (name === 'NotFoundError' || name === 'OverconstrainedError')
            setState('nocamera', 'No camera found — the game is using a stand-in player', 'err', true);
        else
            setState('error', 'Camera error (' + (name || e) + ') — using a stand-in player', 'err', true);
        return;
    }
    const track = stream.getVideoTracks()[0];
    const s = track.getSettings();
    stats.camera = { label: track.label, width: s.width, height: s.height, frameRate: s.frameRate };
    ui.video.srcObject = stream;
    ui.video.play().catch(() => {});
    ui.view.style.display = 'block';
    track.addEventListener('ended', () => {
        if (window.Module && window.Module.dc3PoseStop) window.Module.dc3PoseStop();
        if (window.Module && window.Module._dc3_camera_stop) window.Module._dc3_camera_stop();
        setState('ended', 'Camera disconnected — using a stand-in player', 'err', true);
    });

    setState('loading', 'Loading pose model…', 'warn');
    let sources;
    try {
        sources = await resolveSources();
    } catch (e) {
        console.error('[dc3-pose] pose model ' + e.message);
        stats.source = 'missing';
        setState('error', 'Pose model is not installed on this server — playing without camera', 'err', false);
        return;
    }
    stats.source = sources.source;
    let created;
    try {
        created = await createWorker(sources);
        worker = created.worker;
        worker.addEventListener('message', onWorkerMessage);
        const r = await created.ready;
        stats.loadMs = Math.round(r.loadMs);
        stats.delegate = r.delegate;
    } catch (e) {
        console.error('[dc3-pose] model load failed: ' + e);
        setState('error', 'Pose model failed to load — playing without camera', 'err', true);
        return;
    }
    setState('noperson', 'Camera on — looking for you…', 'warn');

    if (opt.transport === 'stream' && typeof MediaStreamTrackProcessor !== 'undefined') {
        // Frames go straight from the camera to the worker: zero main-thread
        // work per frame. maxBufferSize 1 drops stale frames if inference is
        // slower than the camera rather than queueing latency.
        const proc = new MediaStreamTrackProcessor({ track: track.clone(), maxBufferSize: 1 });
        worker.postMessage({ type: 'stream', readable: proc.readable }, [proc.readable]);
    } else {
        stats.transport = 'bitmap';
        let inFlight = false;
        worker.addEventListener('message', (ev) => { if (ev.data.type === 'pose') inFlight = false; });
        const v = ui.video;
        const pump = async (now, meta) => {
            if (!inFlight && v.readyState >= 2) {
                inFlight = true;
                const bmp = await createImageBitmap(v);
                const captureSec = (meta && meta.captureTime ? meta.captureTime : performance.now()) / 1000;
                worker.postMessage({ type: 'frame', image: bmp, tsMs: captureSec * 1000, captureSec }, [bmp]);
            }
            v.requestVideoFrameCallback(pump);
        };
        v.requestVideoFrameCallback(pump);
    }
}

export function init() {
    if (opt.mode === 'off') { stats.state = 'off'; return; }
    buildUi();
    if (opt.mode === 'manual') { setState('idle', 'Dance with your camera', '', true); return; }
    start();
}

if (!window.__dc3PoseNoAutoInit) init();
