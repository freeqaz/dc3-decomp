#!/usr/bin/env node
// Play one song in the web build with a FAKE CAMERA and record what the pose
// pipeline and the game did -- the positive/negative controls for web pose
// input (docs/native/POSE_INPUT.md).
//
//   node scripts/web/pose-gameplay.mjs --port 8432 --camera FILE.y4m --out DIR
//        [--song-index 3] [--timeout 480] [--extra "&pose_model=lite"]
//        [--difficulty easy|medium|expert|beginner] [--no-select-difficulty]
//   --camera none   no fake device (the page sees no camera -> stand-in skeleton)
//   --camera deny   a fake device but permission refused
//
// Fixtures: scripts/web/make_pose_fixtures.sh. Needs a GPU (run outside the
// sandbox) and `npm ci --prefix scripts/web` (Playwright 1.60 -> Chromium 1223
// headless shell, the build that gets a WebGPU adapter here).
//
// Writes DIR/summary.json: per-second samples of window.dc3PoseSummary() (page
// side) and window.dc3PoseGame (engine side: player 0's bound skeleton, score,
// rating), every DC3 DETECTFRAC line for player 0, the final score, and
// screenshots.
//
// Difficulty matters to what this measures. multiuser_screen opens on
// `beginner`, and MoveDir::ResetDetectFrames builds NO move keys for Beginner
// (the image skips it: `cmpwi r15, 3; beq` at 0x82505714), so on Beginner
// every per-beat DetectFrac reads branch=no-move-key keys=0 and only the async
// detectors rate moves. A driver that just presses Enter therefore never
// exercises the frame-based (ham2) scorer -- which is what np-web-pose's runs
// did, and why they read "DetectFrac 0 on web". The default is now Easy (one
// Down), the same as scripts/dc3-input-flows/betteroffalone.txt on desktop.
//
// Exit status (the regression check): 0 = the song played through and the
// DetectFrac probe agrees with the difficulty -- on a non-Beginner difficulty,
// player 0 has ham2 samples, every one finite, and the engine reports that
// difficulty; on Beginner, every p0 sample is no-move-key. 1 = that check
// failed. 2 = the song was not played through to the results screen (e.g. the
// empty-frame control, which the game pauses before the first beat -- no
// DetectFrac is taken at all, on desktop either). --no-select-difficulty keeps the
// old Enter-only driver (lands on Beginner) while still asserting the requested
// difficulty: it is the sabotage control and must exit 1.
//
//   node scripts/web/pose-gameplay.mjs --recheck DIR [--difficulty X]
// re-applies the check to a finished run's DIR/console.jsonl + summary.json with
// no browser -- for sabotage-testing the check itself, and for judging runs
// recorded before it existed.
import { mkdirSync, writeFileSync, appendFileSync, readFileSync } from 'fs';

const argv = process.argv.slice(2);
const arg = (k, d) => { const i = argv.indexOf('--' + k); return i >= 0 ? argv[i + 1] : d; };
const port = +arg('port', 8432);
const out = arg('out', '/tmp/dc3-pose-gameplay');
const camera = arg('camera', 'none');
const timeoutS = +arg('timeout', 480);
const songIndex = +arg('song-index', 3);
const extra = arg('extra', '');
const DIFFS = ['beginner', 'easy', 'medium', 'expert']; // seldiff list order on multiuser_screen
const difficulty = arg('difficulty', 'easy');
if (!DIFFS.includes(difficulty)) { console.error(`--difficulty must be one of ${DIFFS.join('|')}`); process.exit(2); }
const selectDifficulty = !argv.includes('--no-select-difficulty');

// One probe line: MoveDir.cpp Dc3DetectFracProbe. Branch names carry hyphens
// (no-move-key, async-empty-range) -- a \w+ branch pattern silently drops them.
const DETECT_RE = /DC3 DETECTFRAC\[\d+\] p=(\d) beat=(-?\d+) keys=(\d+) branch=([\w-]+) .*frac=([-\d.naif]+)(?: diff=(\w+))?/i;
const parseDetect = (text, t) => {
    const d = text.match(DETECT_RE);
    return d ? { t, p: +d[1], beat: +d[2], keys: +d[3], branch: d[4], frac: parseFloat(d[5]), diff: d[6] || null } : null;
};
// The regression check (see the header): does DetectFrac agree with the difficulty?
function checkDetect(detect, want, result) {
    const p0All = detect.filter((d) => d.p === 0);
    const fr = p0All.filter((d) => d.branch === 'ham2' && Number.isFinite(d.frac)).map((d) => d.frac);
    const nan = p0All.filter((d) => !Number.isFinite(d.frac)).length;
    const branches = p0All.reduce((m, d) => ((m[d.branch] = (m[d.branch] || 0) + 1), m), {});
    const engine = [...new Set(p0All.map((d) => d.diff).filter(Boolean))];
    const fails = [];
    const reachedGame = /^left_gameplay_/.test(result);
    if (!p0All.length) fails.push('no DC3 DETECTFRAC lines for player 0 (probe off, or no gameplay)');
    if (p0All.length && !engine.length) fails.push('probe lines carry no diff= field (engine predates the probe change)');
    if (engine.length && (engine.length !== 1 || engine[0] !== want)) fails.push(`engine played difficulty ${engine.join(',')}, requested ${want}`);
    if (want === 'beginner') {
        const other = p0All.filter((d) => d.branch !== 'no-move-key').length;
        if (other) fails.push(`Beginner should have no move keys, but ${other} p0 samples took another branch`);
    } else {
        if (!fr.length) fails.push(`no ham2 DetectFrac samples for player 0 on ${want} (branches: ${JSON.stringify(branches)})`);
        if (nan) fails.push(`${nan} non-finite DetectFrac samples`);
    }
    const stats = { n: fr.length, mean: fr.length ? fr.reduce((a, b) => a + b, 0) / fr.length : null,
                    min: fr.length ? Math.min(...fr) : null, max: fr.length ? Math.max(...fr) : null, nan, samples: p0All.length, branches };
    return { pass: reachedGame && !fails.length, reachedGame, fails, engine, stats,
             line: reachedGame ? (fails.length ? `CHECK FAIL: ${fails.join('; ')}` : 'CHECK PASS') : `CHECK FAIL: song not played through (${result})`,
             code: !reachedGame ? 2 : fails.length ? 1 : 0 };
}

const recheck = arg('recheck', null);
if (recheck) {
    const sm = JSON.parse(readFileSync(`${recheck}/summary.json`, 'utf8'));
    const det = [];
    for (const l of readFileSync(`${recheck}/console.jsonl`, 'utf8').split('\n')) {
        if (!l) continue;
        const e = JSON.parse(l);
        const d = parseDetect(e.text, e.t);
        if (d) det.push(d);
    }
    const c = checkDetect(det, difficulty, sm.result);
    console.log(`RECHECK ${recheck} difficulty=${difficulty} result=${sm.result} engine=${c.engine.join(',') || '?'} p0 samples=${c.stats.samples} branches=${JSON.stringify(c.stats.branches)} ham2 n=${c.stats.n} mean=${c.stats.mean && c.stats.mean.toFixed(4)}`);
    console.log(c.line);
    process.exit(c.code);
}

const { chromium } = await import('playwright');
mkdirSync(out, { recursive: true });

const t0 = Date.now();
const el = () => +((Date.now() - t0) / 1000).toFixed(2);
const logf = `${out}/console.jsonl`;
writeFileSync(logf, '');

const media = [];
if (camera !== 'none') {
    media.push('--use-fake-device-for-media-stream');
    if (camera !== 'deny') media.push('--use-fake-ui-for-media-stream', `--use-file-for-fake-video-capture=${camera}`);
}
const browser = await chromium.launch({
    headless: !process.env.DISPLAY,
    args: [
        '--no-sandbox', '--enable-unsafe-webgpu', '--use-angle=vulkan', '--ozone-platform=x11',
        '--enable-features=Vulkan,VulkanFromANGLE,WebAssemblyJSPromiseIntegration',
        '--disable-extensions', '--disable-background-networking', '--disable-default-apps',
        '--disable-sync', '--autoplay-policy=no-user-gesture-required', '--mute-audio',
        ...media,
    ],
});
const ctx = await browser.newContext({ viewport: { width: 1280, height: 860 } });
if (camera === 'deny') await ctx.clearPermissions();
const page = await ctx.newPage();
await page.addInitScript(() => {
    const grab = (res) => {
        try { const ex = (res.instance || res).exports || {}; for (const k in ex) if (ex[k] instanceof WebAssembly.Memory && !window.__dc3Mem) window.__dc3Mem = ex[k]; } catch (e) {}
        return res;
    };
    const is = WebAssembly.instantiateStreaming;
    if (is) WebAssembly.instantiateStreaming = (...a) => is.apply(WebAssembly, a).then(grab);
    const ii = WebAssembly.instantiate;
    WebAssembly.instantiate = (...a) => ii.apply(WebAssembly, a).then(grab);
});

const screens = [], errors = [], samples = [], detect = [], movePassed = [], poseLog = [];
page.on('console', (m) => {
    const e = { t: el(), type: m.type(), text: m.text() };
    appendFileSync(logf, JSON.stringify(e) + '\n');
    const mm = e.text.match(/will enter '([^']+)'/);
    if (mm) { screens.push({ t: e.t, screen: mm[1] }); console.log(`[${e.t}s] SCREEN ${mm[1]}`); }
    const d = parseDetect(e.text, e.t);
    if (d) detect.push(d);
    const mp = e.text.match(/move_passed #(\d+) player=(\d) frac=([-\d.naif]+)/i);
    if (mp) movePassed.push({ t: e.t, n: +mp[1], p: +mp[2], frac: parseFloat(mp[3]) });
    if (/DC3 POSE|dc3-pose/.test(e.text)) { poseLog.push(e); console.log(`[${e.t}s] ${e.text.slice(0, 160)}`); }
});
page.on('pageerror', (err) => { errors.push({ t: el(), msg: String(err.message || err) }); console.log(`[${el()}s] PAGEERROR ${err.message}`); });
page.on('crash', () => { errors.push({ t: el(), msg: 'CRASH' }); console.log('CRASH'); });

const shot = async (name) => {
    const p = `${out}/${String(Math.round(el())).padStart(4, '0')}s_${name}.png`;
    try { await Promise.race([page.screenshot({ path: p }), new Promise((_, r) => setTimeout(() => r(new Error('to')), 8000))]); } catch (e) {}
};
let stop = false;
const sampler = (async () => {
    let n = 0;
    while (!stop) {
        try {
            const s = await Promise.race([page.evaluate(() => ({
                frame: window.dc3FrameCount || 0,
                wasmMB: window.__dc3Mem ? +(window.__dc3Mem.buffer.byteLength / 1048576).toFixed(1) : null,
                jsMB: performance.memory ? +(performance.memory.usedJSHeapSize / 1048576).toFixed(1) : null,
                page: window.dc3PoseSummary ? window.dc3PoseSummary() : null,
                game: window.dc3PoseGame || null,
            })), new Promise((_, r) => setTimeout(() => r(new Error('eval timeout')), 4000))]);
            s.t = el(); s.screen = screens.length ? screens[screens.length - 1].screen : '';
            samples.push(s);
            if (n++ % 10 === 0) {
                const g = s.game && s.game.players ? s.game.players[0] : null;
                console.log(`[${s.t}s] frame=${s.frame} wasm=${s.wasmMB}MB pose=${s.page && s.page.state} in=${s.page && s.page.framesIn} pushed=${s.page && s.page.pushed} infer50=${s.page && s.page.inferMsP50} game: running=${s.game && s.game.providerRunning} tracked=${g && g.tracked} score=${g && g.score} rating=${g && g.rating} screen=${s.screen}`);
            }
        } catch (e) { samples.push({ t: el(), stalled: true }); }
        await new Promise((r) => setTimeout(r, 1000));
    }
})();

const press = async (key) => {
    try {
        await page.evaluate(() => document.getElementById('dc3-canvas').focus()).catch(() => {});
        await page.keyboard.down(key); await new Promise((r) => setTimeout(r, 150)); await page.keyboard.up(key);
    } catch (e) {}
};
const cur = () => (screens.length ? screens[screens.length - 1].screen : '');
const seen = (n) => screens.some((s) => s.screen === n);
const waitFor = async (pred, ms) => { const d = Date.now() + ms; while (Date.now() < d) { if (pred()) return true; await new Promise((r) => setTimeout(r, 250)); } return false; };

let result = 'timeout';
const url = `http://127.0.0.1:${port}/?x=1&env=${encodeURIComponent('DC3_DETECTFRAC_PROBE=1;DC3_SCORING_DEBUG=1')}${extra}`;
try {
    await page.goto(url, { waitUntil: 'domcontentloaded', timeout: 30000 });
    const deadline = Date.now() + timeoutS * 1000;
    if (!await waitFor(() => seen('attract_screen'), 120000)) throw new Error('never reached attract_screen');
    await shot('attract');
    for (const target of ['title_screen', 'main_screen', 'choose_mode_screen', 'song_select_screen']) {
        for (let tries = 0; tries < 8 && !seen(target); tries++) {
            if (await waitFor(() => seen(target), 4000)) break;
            if (Date.now() > deadline) break;
            await press('Enter');
        }
        if (!seen(target)) throw new Error(`never reached ${target} (current ${cur()})`);
    }
    await new Promise((r) => setTimeout(r, 3000));
    for (let i = 0; i < songIndex; i++) { await press('ArrowDown'); await new Promise((r) => setTimeout(r, 400)); }
    await shot('song_select');
    await press('Enter');
    const b2 = screens.length;
    const inGame = () => screens.slice(b2).some((s) => s.screen === 'game_screen');
    // Difficulty: the seldiff list opens on `beginner`; Down N times, then the
    // Enter loop below confirms it, `play` and `skip_waiting`.
    if (await waitFor(() => screens.slice(b2).some((s) => s.screen === 'multiuser_screen') || inGame(), 30000) && !inGame()) {
        await new Promise((r) => setTimeout(r, 2500));
        const downs = selectDifficulty ? DIFFS.indexOf(difficulty) : 0;
        for (let i = 0; i < downs; i++) { await press('ArrowDown'); await new Promise((r) => setTimeout(r, 500)); }
        console.log(`[${el()}s] multiuser_screen: ${downs} down(s) for ${selectDifficulty ? difficulty : '(not selected: Enter-only driver)'}`);
    }
    for (let i = 0; i < 30 && !inGame(); i++) {
        await new Promise((r) => setTimeout(r, 2500));
        if (!/loading/.test(cur())) await press('Enter');
    }
    let lastShot = 0;
    const ok = await waitFor(() => {
        if (cur() === 'game_screen' && Date.now() - lastShot > 20000) { lastShot = Date.now(); shot('ingame'); }
        const sl = screens.slice(b2);
        const gi = sl.findIndex((s) => s.screen === 'game_screen');
        return gi >= 0 && sl.slice(gi + 1).some((s) => !/game_screen|pause/.test(s.screen));
    }, Math.max(0, deadline - Date.now()));
    result = ok ? `left_gameplay_to_${cur()}` : `timeout_in_${cur()}`;
    await new Promise((r) => setTimeout(r, 5000));
    await shot('after_' + cur());
    await new Promise((r) => setTimeout(r, 6000));
    await shot('after2_' + cur());
} catch (e) {
    result = 'error: ' + e.message;
    await shot('error');
}
stop = true;
await sampler;

// Score: the last engine-side reading taken while in gameplay.
const inGameSamples = samples.filter((s) => s.screen === 'game_screen' && s.game && s.game.players);
const last = inGameSamples.length ? inGameSamples[inGameSamples.length - 1] : null;
const chk = checkDetect(detect, difficulty, result);
const ratings = [];
for (const s of inGameSamples) { const r = s.game.players[0].rating; if (r && (!ratings.length || ratings[ratings.length - 1] !== r)) ratings.push(r); }
const pageLast = [...samples].reverse().find((s) => s.page);
const summary = {
    result, camera, url, elapsed: el(), errors,
    finalScoreP0: last ? last.game.players[0].score : null,
    gameSecondsTracked: inGameSamples.filter((s) => s.game.players[0].tracked).length,
    gameSeconds: inGameSamples.length,
    difficultyRequested: difficulty, difficultySelected: selectDifficulty,
    difficultyEngine: chk.engine,
    detectFracP0: chk.stats,
    ratingChanges: ratings,
    movePassed: movePassed.slice(0, 12),
    pagePose: pageLast ? pageLast.page : null,
    wasmMBMax: Math.max(...samples.map((s) => s.wasmMB || 0)),
    screens, samples, detect,
};
summary.check = { pass: chk.pass, reachedGame: chk.reachedGame, fails: chk.fails };
writeFileSync(`${out}/summary.json`, JSON.stringify(summary, null, 1));
console.log(`RESULT ${result} camera=${camera} difficulty=${difficulty}${selectDifficulty ? '' : '(not selected)'} engine=${chk.engine.join(',') || '?'} score=${summary.finalScoreP0} detectFrac(p0) n=${chk.stats.n} mean=${chk.stats.mean && chk.stats.mean.toFixed(4)} min=${chk.stats.min} max=${chk.stats.max} branches=${JSON.stringify(chk.stats.branches)} ratings=${ratings.join('>')} tracked ${summary.gameSecondsTracked}/${summary.gameSeconds}s wasmMax=${summary.wasmMBMax}MB errors=${errors.length}`);
console.log(chk.line);
try { await Promise.race([browser.close(), new Promise((r) => setTimeout(r, 5000))]); } catch {}
process.exit(chk.code);
