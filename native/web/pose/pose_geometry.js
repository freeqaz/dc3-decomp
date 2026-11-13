// DC3 web pose input -- BlazePose (MediaPipe) landmarks -> DC3's 20 Kinect
// joints in camera-space metres.
//
// A line-for-line port of native/scripts/pose_mediapipe.py (MediaPipeBackend:
// _absolute_root, _absolute_root_torso, _remap, OneEuroFilter, CentroidTracker),
// which the desktop build runs in pose_server.py. The REASONS for every
// constant (axis flips, centre-joint fractions, foot blend, the root DLT, the
// depth-only one-euro filter) are documented there and are not repeated here:
// change the Python and this file together, and re-run
// scripts/web/pose_parity.py (geometry parity on identical landmarks).
//
// Pure ES module, no DOM: runs in the pose worker and under Node.
//
// Output wire format (what dc3_pose_push takes, PoseInput_Web.cpp): per person
// 81 floats = [trackId, 20 x (x, y, z, conf)], DC3 joint order
// (src/system/gesture/BaseSkeleton.h).

// BlazePose 33-landmark indices
const NOSE = 0;
const L_EAR = 7, R_EAR = 8;
const L_SHOULDER = 11, R_SHOULDER = 12;
const L_ELBOW = 13, R_ELBOW = 14;
const L_WRIST = 15, R_WRIST = 16;
const L_PINKY = 17, R_PINKY = 18;
const L_INDEX = 19, R_INDEX = 20;
const L_HIP = 23, R_HIP = 24;
const L_KNEE = 25, R_KNEE = 26;
const L_ANKLE = 27, R_ANKLE = 28;
const L_HEEL = 29, R_HEEL = 30;
const L_FOOT = 31, R_FOOT = 32;

export const DC3_JOINT_NAMES = [
    'HipCenter', 'Spine', 'ShoulderCenter', 'Head',
    'ShoulderLeft', 'ElbowLeft', 'WristLeft', 'HandLeft',
    'ShoulderRight', 'ElbowRight', 'WristRight', 'HandRight',
    'HipLeft', 'KneeLeft', 'AnkleLeft',
    'HipRight', 'KneeRight', 'AnkleRight',
    'FootLeft', 'FootRight',
];
export const NUM_DC3_JOINTS = 20;
export const FLOATS_PER_PERSON = 1 + NUM_DC3_JOINTS * 4;
const [HIP_CENTER, SPINE, SHOULDER_CENTER, HEAD,
    SHOULDER_LEFT, ELBOW_LEFT, WRIST_LEFT, HAND_LEFT,
    SHOULDER_RIGHT, ELBOW_RIGHT, WRIST_RIGHT, HAND_RIGHT,
    HIP_LEFT, KNEE_LEFT, ANKLE_LEFT,
    HIP_RIGHT, KNEE_RIGHT, ANKLE_RIGHT,
    FOOT_LEFT, FOOT_RIGHT] = DC3_JOINT_NAMES.map((_, i) => i);

// pose_mediapipe.py constants (see the measurements documented there).
const FOOT_TOE_BLEND = 0.35;
const HIP_CENTER_UP = 0.24;
const SPINE_UP = 0.36;
const SPINE_BACK = 0.085;
const SHOULDER_CENTER_UP = 1.04;
export const DEFAULT_HFOV_DEG = 58.51;

export class OneEuroFilter {
    constructor(minCutoff = 0.2, beta = 0.1, dCutoff = 1.0) {
        this.minCutoff = minCutoff;
        this.beta = beta;
        this.dCutoff = dCutoff;
        this._x = null;
        this._dx = 0.0;
        this._t = null;
    }

    filter(x, tS) {
        if (this._x === null) {
            this._x = x;
            this._t = tS;
            return x;
        }
        const dt = tS - this._t;
        if (dt <= 0.0) return this._x;
        this._t = tS;
        const alpha = (cutoff) => {
            const tau = 1.0 / (2.0 * Math.PI * cutoff);
            return 1.0 / (1.0 + tau / dt);
        };
        const dx = (x - this._x) / dt;
        this._dx += alpha(this.dCutoff) * (dx - this._dx);
        const cutoff = this.minCutoff + this.beta * Math.abs(this._dx);
        this._x += alpha(cutoff) * (x - this._x);
        return this._x;
    }
}

export class CentroidTracker {
    constructor(maxDist = 0.25, maxMissing = 15) {
        this.maxDist = maxDist;
        this.maxMissing = maxMissing;
        this._nextId = 1;
        this._tracks = new Map(); // id -> { c: [x, y], missing }
    }

    // centroids: [[x, y], ...] in normalised image space. Returns ids.
    update(centroids) {
        const assigned = new Array(centroids.length).fill(null);
        const used = new Set();
        // Greedy nearest-neighbour, closest first; ties broken like Python's
        // tuple sort (dist, track id, detection index).
        const pairs = [];
        for (const [tid, tr] of this._tracks) {
            centroids.forEach((c, di) => {
                pairs.push([Math.hypot(c[0] - tr.c[0], c[1] - tr.c[1]), tid, di]);
            });
        }
        pairs.sort((a, b) => a[0] - b[0] || a[1] - b[1] || a[2] - b[2]);
        for (const [dist, tid, di] of pairs) {
            if (dist > this.maxDist || used.has(tid) || assigned[di] !== null) continue;
            assigned[di] = tid;
            used.add(tid);
        }
        centroids.forEach((c, di) => {
            if (assigned[di] === null) assigned[di] = this._nextId++;
        });
        centroids.forEach((c, di) => {
            this._tracks.set(assigned[di], { c: [c[0], c[1]], missing: 0 });
        });
        for (const [tid, tr] of [...this._tracks]) {
            if (!assigned.includes(tid)) {
                tr.missing += 1;
                if (tr.missing > this.maxMissing) this._tracks.delete(tid);
            }
        }
        return assigned;
    }

    reset() { this._tracks.clear(); }
}

// Solve a 3x3 system with partial pivoting (numpy.linalg.solve's LU). Returns
// null when singular, as numpy raises LinAlgError.
function solve3(A, y) {
    const m = [A[0].slice(), A[1].slice(), A[2].slice()];
    const v = y.slice();
    for (let col = 0; col < 3; col++) {
        let piv = col;
        for (let r = col + 1; r < 3; r++) if (Math.abs(m[r][col]) > Math.abs(m[piv][col])) piv = r;
        if (m[piv][col] === 0) return null;
        if (piv !== col) {
            [m[piv], m[col]] = [m[col], m[piv]];
            [v[piv], v[col]] = [v[col], v[piv]];
        }
        for (let r = col + 1; r < 3; r++) {
            const f = m[r][col] / m[col][col];
            for (let c = col; c < 3; c++) m[r][c] -= f * m[col][c];
            v[r] -= f * v[col];
        }
    }
    const x = [0, 0, 0];
    for (let r = 2; r >= 0; r--) {
        let s = v[r];
        for (let c = r + 1; c < 3; c++) s -= m[r][c] * x[c];
        x[r] = s / m[r][r];
    }
    return x;
}

const sub = (a, b) => [a[0] - b[0], a[1] - b[1], a[2] - b[2]];
const add = (a, b) => [a[0] + b[0], a[1] + b[1], a[2] + b[2]];
const scale = (a, s) => [a[0] * s, a[1] * s, a[2] * s];
const norm = (a) => Math.sqrt(a[0] * a[0] + a[1] * a[1] + a[2] * a[2]);
const cross = (a, b) => [
    a[1] * b[2] - a[2] * b[1],
    a[2] * b[0] - a[0] * b[2],
    a[0] * b[1] - a[1] * b[0],
];

export class PoseGeometry {
    constructor({ hfovDeg = DEFAULT_HFOV_DEG } = {}) {
        this._tracker = new CentroidTracker();
        this._rootZFilters = new Map();
        this._filterLastSeen = new Map();
        this._frameCount = 0;
        this._hfov = hfovDeg * Math.PI / 180;
    }

    reset() {
        this._tracker.reset();
        this._rootZFilters.clear();
        this._filterLastSeen.clear();
    }

    focalPx(width) { return (width * 0.5) / Math.tan(this._hfov * 0.5); }

    // world: [[x,y,z]*33] metres (MediaPipe world landmarks); imageXY:
    // [[x,y]*33] normalised; vis: [33]. Returns [Rx,Ry,Rz] or null.
    absoluteRoot(world, imageXY, vis, width, height) {
        const f = this.focalPx(width);
        const n = world.length;
        let count = 0;
        let wf2 = 0, wfa = 0, wfb = 0, wab = 0, y0 = 0, y1 = 0, y2 = 0;
        for (let i = 0; i < n; i++) {
            const w = vis[i] >= 0.3 ? Math.min(Math.max(vis[i], 0), 1) : 0;
            if (w !== 0) count++;
            const a = imageXY[i][0] * width - width * 0.5;
            const b = imageXY[i][1] * height - height * 0.5;
            const relX = -world[i][0], relY = -world[i][1], relZ = world[i][2];
            const ru = -f * relX - a * relZ;
            const rv = -f * relY - b * relZ;
            wf2 += w * f * f;
            wfa += w * f * a;
            wfb += w * f * b;
            wab += w * (a * a + b * b);
            y0 += w * f * ru;
            y1 += w * f * rv;
            y2 += w * (a * ru + b * rv);
        }
        if (count < 4) return this.absoluteRootTorso(world, imageXY, width, height);
        const root = solve3([[wf2, 0, wfa], [0, wf2, wfb], [wfa, wfb, wab]], [y0, y1, y2]);
        if (!root || !root.every(Number.isFinite))
            return this.absoluteRootTorso(world, imageXY, width, height);
        root[2] = Math.min(Math.max(root[2], 0.8), 8.0);
        return root;
    }

    absoluteRootTorso(world, imageXY, width, height) {
        const f = this.focalPx(width);
        const hipW = scale(add(world[L_HIP], world[R_HIP]), 0.5);
        const shoW = scale(add(world[L_SHOULDER], world[R_SHOULDER]), 0.5);
        const torsoVec = sub(shoW, hipW);
        const torsoM = Math.hypot(torsoVec[0], torsoVec[1]);
        const hipPx = [(imageXY[L_HIP][0] + imageXY[R_HIP][0]) * 0.5 * width,
            (imageXY[L_HIP][1] + imageXY[R_HIP][1]) * 0.5 * height];
        const shoPx = [(imageXY[L_SHOULDER][0] + imageXY[R_SHOULDER][0]) * 0.5 * width,
            (imageXY[L_SHOULDER][1] + imageXY[R_SHOULDER][1]) * 0.5 * height];
        const torsoPx = Math.hypot(shoPx[0] - hipPx[0], shoPx[1] - hipPx[1]);
        if (torsoM < 1e-3 || torsoPx < 1.0) return null;
        let zHip = f * torsoM / torsoPx;
        zHip = Math.min(Math.max(zHip, 0.8), 8.0);
        const xHip = -(hipPx[0] - width * 0.5) / f * zHip;
        const yHip = -(hipPx[1] - height * 0.5) / f * zHip;
        return [xHip, yHip, zHip];
    }

    // One MediaPipe PoseLandmarkerResult -> [{ trackId, joints: [[x,y,z]*20],
    // conf: [20] }] (pose_mediapipe.py MediaPipeBackend.process).
    // poses: [{ world: [[x,y,z]*33], image: [[x,y]*33] | null, vis: [33] | null }]
    process(poses, width, height, timestampMs) {
        if (!poses.length) {
            this._tracker.update([]);
            return [];
        }
        const people = [];
        const centroids = [];
        for (const p of poses) {
            const world = p.world;
            const imageXY = p.image || world.map(() => [0, 0]);
            const vis = p.vis || world.map(() => 1.0);
            const root = this.absoluteRoot(world, imageXY, vis, width, height);
            if (!root) continue;
            const { joints, conf } = this.remap(world, vis, root);
            people.push({ joints, conf, rootZ: root[2] });
            centroids.push([(imageXY[L_HIP][0] + imageXY[R_HIP][0]) * 0.5,
                (imageXY[L_HIP][1] + imageXY[R_HIP][1]) * 0.5]);
        }
        const ids = this._tracker.update(centroids);
        this._frameCount += 1;
        const tS = timestampMs / 1000.0;
        const out = [];
        people.forEach((person, k) => {
            const tid = ids[k];
            let filt = this._rootZFilters.get(tid);
            if (!filt) {
                filt = new OneEuroFilter();
                this._rootZFilters.set(tid, filt);
            }
            this._filterLastSeen.set(tid, this._frameCount);
            const dz = filt.filter(person.rootZ, tS) - person.rootZ;
            const joints = person.joints.map((j) => [j[0], j[1], j[2] + dz]);
            out.push({ trackId: tid, joints, conf: person.conf });
        });
        for (const [t, seen] of [...this._filterLastSeen]) {
            if (this._frameCount - seen > 60) {
                this._filterLastSeen.delete(t);
                this._rootZFilters.delete(t);
            }
        }
        return out;
    }

    remap(world, vis, root) {
        const cam = world.map((w) => [-w[0] + root[0], -w[1] + root[1], w[2] + root[2]]);
        const j = new Array(NUM_DC3_JOINTS);
        const c = new Array(NUM_DC3_JOINTS);
        const mid = (a, b) => scale(add(cam[a], cam[b]), 0.5);
        const minc = (...idx) => Math.min(...idx.map((i) => vis[i]));

        j[HIP_LEFT] = cam[L_HIP]; c[HIP_LEFT] = vis[L_HIP];
        j[HIP_RIGHT] = cam[R_HIP]; c[HIP_RIGHT] = vis[R_HIP];
        j[SHOULDER_LEFT] = cam[L_SHOULDER]; c[SHOULDER_LEFT] = vis[L_SHOULDER];
        j[SHOULDER_RIGHT] = cam[R_SHOULDER]; c[SHOULDER_RIGHT] = vis[R_SHOULDER];

        const hipMid = mid(L_HIP, R_HIP);
        const torso = sub(mid(L_SHOULDER, R_SHOULDER), hipMid);
        const torsoLen = norm(torso);
        let back = cross(torso, sub(cam[L_HIP], cam[R_HIP]));
        const backLen = norm(back);
        back = backLen > 1e-9 ? scale(back, 1 / backLen) : [0, 0, 0];

        const centreConf = minc(L_HIP, R_HIP, L_SHOULDER, R_SHOULDER);
        j[HIP_CENTER] = add(hipMid, scale(torso, HIP_CENTER_UP));
        j[SPINE] = add(add(hipMid, scale(torso, SPINE_UP)), scale(back, SPINE_BACK * torsoLen));
        j[SHOULDER_CENTER] = add(hipMid, scale(torso, SHOULDER_CENTER_UP));
        c[HIP_CENTER] = c[SPINE] = c[SHOULDER_CENTER] = centreConf;

        if (minc(L_EAR, R_EAR) > 0.3) {
            j[HEAD] = mid(L_EAR, R_EAR); c[HEAD] = minc(L_EAR, R_EAR);
        } else {
            j[HEAD] = cam[NOSE]; c[HEAD] = vis[NOSE];
        }

        j[ELBOW_LEFT] = cam[L_ELBOW]; c[ELBOW_LEFT] = vis[L_ELBOW];
        j[WRIST_LEFT] = cam[L_WRIST]; c[WRIST_LEFT] = vis[L_WRIST];
        j[ELBOW_RIGHT] = cam[R_ELBOW]; c[ELBOW_RIGHT] = vis[R_ELBOW];
        j[WRIST_RIGHT] = cam[R_WRIST]; c[WRIST_RIGHT] = vis[R_WRIST];

        j[HAND_LEFT] = mid(L_PINKY, L_INDEX); c[HAND_LEFT] = minc(L_PINKY, L_INDEX);
        j[HAND_RIGHT] = mid(R_PINKY, R_INDEX); c[HAND_RIGHT] = minc(R_PINKY, R_INDEX);

        j[KNEE_LEFT] = cam[L_KNEE]; c[KNEE_LEFT] = vis[L_KNEE];
        j[ANKLE_LEFT] = cam[L_ANKLE]; c[ANKLE_LEFT] = vis[L_ANKLE];
        j[KNEE_RIGHT] = cam[R_KNEE]; c[KNEE_RIGHT] = vis[R_KNEE];
        j[ANKLE_RIGHT] = cam[R_ANKLE]; c[ANKLE_RIGHT] = vis[R_ANKLE];

        j[FOOT_LEFT] = add(scale(cam[L_HEEL], 1 - FOOT_TOE_BLEND), scale(cam[L_FOOT], FOOT_TOE_BLEND));
        c[FOOT_LEFT] = minc(L_HEEL, L_FOOT);
        j[FOOT_RIGHT] = add(scale(cam[R_HEEL], 1 - FOOT_TOE_BLEND), scale(cam[R_FOOT], FOOT_TOE_BLEND));
        c[FOOT_RIGHT] = minc(R_HEEL, R_FOOT);

        return { joints: j, conf: c };
    }
}

// tasks-vision PoseLandmarkerResult -> PoseGeometry.process input.
export function posesFromResult(res) {
    const poses = [];
    const worlds = res.worldLandmarks || [];
    worlds.forEach((wlms, pi) => {
        const ilms = res.landmarks && pi < res.landmarks.length ? res.landmarks[pi] : null;
        poses.push({
            world: wlms.map((l) => [l.x, l.y, l.z]),
            image: ilms ? ilms.map((l) => [l.x, l.y]) : null,
            // Python: getattr(l, "visibility", 1.0)
            vis: ilms ? ilms.map((l) => (l.visibility === undefined ? 1.0 : l.visibility)) : null,
        });
    });
    return poses;
}

// process() output -> the dc3_pose_push float layout.
export function packPersons(persons, maxPersons = 6) {
    const n = Math.min(persons.length, maxPersons);
    const out = new Float32Array(Math.max(n, 1) * FLOATS_PER_PERSON);
    for (let i = 0; i < n; i++) {
        const base = i * FLOATS_PER_PERSON;
        out[base] = persons[i].trackId;
        for (let j = 0; j < NUM_DC3_JOINTS; j++) {
            const o = base + 1 + j * 4;
            out[o] = persons[i].joints[j][0];
            out[o + 1] = persons[i].joints[j][1];
            out[o + 2] = persons[i].joints[j][2];
            out[o + 3] = persons[i].conf[j];
        }
    }
    return { floats: out, count: n };
}
