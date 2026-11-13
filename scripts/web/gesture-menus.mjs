// Web: DC3's menus by gesture alone (PLAN P-3), the browser half of
// scripts/gesture_menu_flow.py.
//
// The page's camera pipeline (native/web/pose/) ends in Module.dc3PosePush ->
// dc3_pose_push (PoseInput_Web.cpp), the same PublishFrame seam the desktop
// socket uses.  A fake camera cannot make a person swipe, so this driver pushes
// the synthetic performer's joints straight into that seam: DC3's 20 joints in
// camera-space metres, the geometry of scripts/synthetic_kinect.py (STAND,
// menu_arm -- keep the two in step).  Everything after the push is the game's.
//
// Route: boot with NO pose provider (the controller-mode policy `auto` is then
// forced, as on desktop without a camera) -> at title_screen the performer
// steps in (first push: auto -> faithful; the image's idle timeout hands the UI
// to the hand cursor) -> right hand picks title -> main -> perform -> song
// select, then the left hand selects the helpbar's back -> choose_mode_screen.
// Item heights are the ones gesture_menu_flow.py reads off the desktop build
// (the web has no DTA eval); every select is checked by the engine's own
// `DC3 HamNavList: select ... sym=... selecting=0` line.
//
//   python3 native/web/server.py --port 8435 &      # this tree's native/web/build
//   node scripts/web/gesture-menus.mjs --port 8435 --out DIR
//   ... --env DC3_CONTROLLER_MODE=forced   # sabotage: no handover, no select, exit 1
//
// Exit 0: route completed with gesture selects only; 1: failed; 2: setup.
import { mkdirSync, writeFileSync, appendFileSync } from 'fs';

const argv = process.argv.slice(2);
const arg = (k, d) => { const i = argv.indexOf('--' + k); return i >= 0 ? argv[i + 1] : d; };
const port = +arg('port', 8435);
const out = arg('out', '/tmp/dc3-web-gesture-menus');
const timeoutS = +arg('timeout', 300);
const debug = argv.includes('--debug');
const env = arg('env', ''); // e.g. DC3_CONTROLLER_MODE=forced (the sabotage: must fail)

const { chromium } = await import('playwright');
mkdirSync(out, { recursive: true });
const t0 = Date.now();
const el = () => +((Date.now() - t0) / 1000).toFixed(2);
const logf = `${out}/console.jsonl`;
writeFileSync(logf, '');

const browser = await chromium.launch({
    headless: !process.env.DISPLAY,
    args: [
        '--no-sandbox', '--enable-unsafe-webgpu', '--use-angle=vulkan', '--ozone-platform=x11',
        '--enable-features=Vulkan,VulkanFromANGLE,WebAssemblyJSPromiseIntegration',
        '--disable-extensions', '--disable-background-networking', '--disable-default-apps',
        '--disable-sync', '--autoplay-policy=no-user-gesture-required', '--mute-audio',
    ],
});
const page = await (await browser.newContext({ viewport: { width: 1280, height: 860 } })).newPage();

const screens = [], selects = [], policy = [], modeLines = [], errors = [];
page.on('console', (m) => {
    const text = m.text();
    appendFileSync(logf, JSON.stringify({ t: el(), text }) + '\n');
    let mm = text.match(/will enter '([^']+)'/);
    if (mm) { screens.push(mm[1]); console.log(`[${el()}s] SCREEN ${mm[1]}`); }
    mm = text.match(/DC3 HamNavList: select name='([^']+)' .*sym='([^']*)' .*selecting=(\d)/);
    if (mm) { selects.push({ list: mm[1], sym: mm[2], button: mm[3] === '1' }); console.log(`[${el()}s] SELECT ${mm[1]} ${mm[2]} selecting=${mm[3]}`); }
    if (/controller mode policy/.test(text)) { policy.push(text); console.log(`[${el()}s] ${text}`); }
    if (/DC3 ControllerMode: (enter|exit)/.test(text)) { modeLines.push(text); console.log(`[${el()}s] ${text}`); }
});
page.on('pageerror', (e) => errors.push(String(e.message || e)));

// The performer, in the page: one push per 33 ms while running.
await page.addInitScript(() => {
    const STAND = [
        [0.00, 0.90], [0.00, 1.10], [0.00, 1.40], [0.00, 1.60],
        [-0.20, 1.40], [-0.25, 1.15], [-0.22, 0.90], [-0.22, 0.85],
        [0.20, 1.40], [0.25, 1.15], [0.22, 0.90], [0.22, 0.85],
        [-0.12, 0.85], [-0.12, 0.45], [-0.12, 0.05],
        [0.12, 0.85], [0.12, 0.45], [0.12, 0.05],
        [-0.12, 0.00], [0.12, 0.00],
    ];
    const ELBOW_L = 5, WRIST_L = 6, HAND_L = 7, ELBOW_R = 9, WRIST_R = 10, HAND_R = 11;
    const DEPTH = 2.5, SHOULDER_Y = 1.40, HIP_Y = 0.85, ELBOW_X = 0.42, FOREARM = 0.24, HAND = 0.06;
    const HOLD_S = 0.10;
    const handY = (h) => SHOULDER_Y - (h - 0.15) * (SHOULDER_Y - HIP_Y);
    const P = window.__performer = { pose: 'stand', side: 'right', h: 0.5, t0: 0, dur: 0.3, sweep: 110, pushes: 0, timer: null };
    const now = () => performance.now() / 1000;
    const joints = () => {
        const pts = STAND.map(([x, y]) => [x, y, DEPTH]);
        let side = P.side, angle = 0;
        if (P.pose === 'swipe') {
            const e = now() - P.t0;
            angle = P.sweep * Math.min(1, Math.max(0, e / P.dur));
            if (e > P.dur + HOLD_S) side = null;
        } else if (P.pose !== 'point') side = null;
        if (side) {
            const s = side === 'right' ? 1 : -1;
            const [eJ, wJ, hJ] = side === 'right' ? [ELBOW_R, WRIST_R, HAND_R] : [ELBOW_L, WRIST_L, HAND_L];
            const y = handY(P.h), a = angle * Math.PI / 180, ex = s * ELBOW_X;
            const fx = s * Math.cos(a), fz = -Math.sin(a);
            pts[eJ] = [ex, y, DEPTH];
            pts[wJ] = [ex + FOREARM * fx, y, DEPTH + FOREARM * fz];
            pts[hJ] = [ex + (FOREARM + HAND) * fx, y, DEPTH + (FOREARM + HAND) * fz];
        }
        const f = new Float32Array(81);
        f[0] = 5; // a Kinect tracking id (> 0), as synthetic_kinect.py sends
        pts.forEach(([x, y, z], j) => { f.set([x, y, z, 1.0], 1 + j * 4); });
        return f;
    };
    P.start = () => {
        if (P.timer) return;
        P.timer = setInterval(() => {
            if (window.Module && window.Module.dc3PosePush && window.Module.dc3PosePush(joints(), 1, now())) P.pushes++;
        }, 33);
    };
});

const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
const cur = () => screens[screens.length - 1] || '';
const waitFor = async (pred, ms) => { const d = Date.now() + ms; while (Date.now() < d) { if (pred()) return true; await sleep(100); } return false; };
const pose = (o) => page.evaluate((o) => { Object.assign(window.__performer, o); if (o.pose === 'swipe') window.__performer.t0 = performance.now() / 1000; }, o);
const shot = async (name) => { try { await page.screenshot({ path: `${out}/${String(Math.round(el())).padStart(4, '0')}s_${name}.png` }); } catch (e) {} };

// Point, hover, swipe; return the select it produced (or null).
async function pick(side, h, label) {
    const before = selects.length;
    await pose({ pose: 'point', side, h });
    await sleep(1200);
    await shot(`${cur()}_${label}`);
    await pose({ pose: 'swipe', side, h, dur: 0.3, sweep: 110 });
    await sleep(700);
    await pose({ pose: 'stand' });
    await waitFor(() => selects.length > before, 3000);
    return selects.length > before ? selects[before] : null;
}

let result = 'fail', failure = '';
try {
    await page.goto(`http://127.0.0.1:${port}/?x=1${debug ? '&debug=true' : ''}${env ? '&env=' + encodeURIComponent(env) : ''}`, { waitUntil: 'domcontentloaded', timeout: 30000 });
    if (!await waitFor(() => screens.includes('title_screen'), 150000)) throw new Error(`never reached title_screen (at ${cur()})`);
    await sleep(2000);
    await shot('title_before_performer');
    // The performer steps in: the first push starts the provider.
    await page.evaluate(() => window.__performer.start());
    // auto -> faithful, then the image's controller_mode_timeout (5000 ms)
    // hands the UI to the hand cursor.
    // Recorded, not required: under a forced policy (the sabotage) neither
    // happens, and the route below is what must then fail.
    const wentFaithful = await waitFor(() => policy.some((l) => /auto -> faithful/.test(l)), 10000);
    const handedOver = await waitFor(() => modeLines.some((l) => /exit/.test(l)), wentFaithful ? 12000 : 7000);
    console.log(`[${el()}s] performer in: auto->faithful=${wentFaithful} handover=${handedOver}`);
    await sleep(1000);
    const route = [
        { side: 'right', h: 0.0, sym: 'title_screen_menu', next: 'main_screen' },
        { side: 'right', h: 0.0, sym: 'gameplay', next: 'choose_mode_screen' },
        { side: 'right', h: 0.0, sym: 'perform', next: 'song_select_screen' },
        { side: 'left', h: 1.0, sym: null, next: 'choose_mode_screen' }, // helpbar back
    ];
    for (const step of route) {
        const at = cur();
        let ok = false;
        for (let attempt = 0; attempt < 3 && !ok; attempt++) {
            const s = await pick(step.side, step.h, step.sym || 'back');
            if (s && (step.sym === null || s.sym === step.sym)) ok = await waitFor(() => cur() === step.next, 15000);
            else if (s) throw new Error(`on ${at} the ${step.side} hand selected '${s.sym}', wanted '${step.sym}'`);
            if (!ok) await sleep(1500);
        }
        if (!ok) throw new Error(`no ${step.side}-hand select took ${at} to ${step.next} (at ${cur()})`);
        await sleep(2500); // the next list's enter animation
    }
    await shot('done');
    result = 'ok';
} catch (e) {
    failure = String(e.message || e);
    await shot('failure');
}
const pushes = await page.evaluate(() => window.__performer.pushes).catch(() => -1);
await browser.close();
const summary = {
    result, failure, screens, selects, policy, modeLines, pushes, errors: errors.slice(0, 10),
    selectsByGesture: selects.filter((s) => !s.button).length,
    selectsByButton: selects.filter((s) => s.button).length,
};
writeFileSync(`${out}/summary.json`, JSON.stringify(summary, null, 1));
console.log(`RESULT ${result}${failure ? ' (' + failure + ')' : ''} gesture=${summary.selectsByGesture} button=${summary.selectsByButton} pushes=${pushes} policy=${JSON.stringify(policy)}`);
process.exit(result === 'ok' && summary.selectsByButton === 0 ? 0 : 1);
