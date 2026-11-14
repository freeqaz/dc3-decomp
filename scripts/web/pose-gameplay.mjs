#!/usr/bin/env node
// Play one song in the web build with a FAKE CAMERA and record what the pose
// pipeline and the game did -- the positive/negative controls for web pose
// input (docs/native/POSE_INPUT.md).
//
//   node scripts/web/pose-gameplay.mjs --port 8432 --camera FILE.y4m --out DIR
//        [--song-index 3] [--timeout 480] [--extra "&pose_model=lite"]
//        [--difficulty easy|medium|expert|beginner] [--no-select-difficulty]
//   --camera none   no fake device (the page sees no camera -> stand-in skeleton)
//   --unmute        drop --mute-audio (the audio worklet then runs on the box's
//                   real output; with it muted Chromium clocks a fake sink)
//   --camera deny   a fake device but permission refused
//   --pacing        record frame pacing (scripts/web/lib/pacing.mjs, PLAN W-14):
//                   game-frame and rAF intervals per phase, long tasks, every
//                   request, and the engine's per-frame cost counters
//                   (adds env DC3_FRAME_TRACE=1); writes DIR/pacing.json and
//                   prints the per-phase table and the worst stalls
//   --then-song-index N [--then-difficulty D]   after the first song, continue
//                   through results to song select and play song N too (W-10:
//                   two songs back to back with the camera, both must score);
//                   the check then also needs both songs played through and,
//                   with a camera, a non-zero score on each
//   --trace FILE [--trace-seconds 90]   a Chrome trace (devtools.timeline, v8 GC,
//                   gpu) of the first N seconds, for stalls --pacing cannot
//                   attribute (open it in chrome://tracing or Perfetto)
//   --env "K=V;K2=V2"   more engine env vars, merged into the page's one ?env=
//                   (a second &env= in --extra is ignored by the page), e.g.
//                   --env DC3_POSE_FILL=off
//   --chrome-args "A B"  extra Chromium flags, e.g. "--disable-frame-rate-limit
//                   --disable-gpu-vsync" (an unthrottled rAF: H-34)
//   --max-timer-errors-per-s R   the song-clock check (W-12): exit 1 when more
//                   than R "timer error is large" lines a game second are
//                   logged. Default 0.25 with --unmute, off when muted (a muted
//                   Chromium clocks a fake sink; its rate says little about a
//                   player's). Measured on thehustle/easy/no camera, unmuted:
//                   0.93/s before W-12, 0.05/s after. --recheck takes the
//                   default from the run's own summary (audio.unmuted).
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
const unmute = argv.includes('--unmute');
const maxTimerErrorsArg = arg('max-timer-errors-per-s', null);
const pacing = argv.includes('--pacing');
const thenSongIndex = arg('then-song-index', null) === null ? null : +arg('then-song-index');
const thenDifficulty = arg('then-difficulty', difficulty);
const traceFile = arg('trace', null);
const extraEnv = arg('env', '');
const traceSeconds = +arg('trace-seconds', 90);
const chromeArgs = (arg('chrome-args', '') || '').split(/\s+/).filter(Boolean);

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

// "timer error is large" during gameplay: how often, and how big.
function timerStats(list, gameSeconds) {
    const g = list.filter((x) => x.screen === 'game_screen').map((x) => Math.abs(x.ms)).sort((a, b) => a - b);
    const q = (f) => (g.length ? +g[Math.min(g.length - 1, Math.floor(f * g.length))].toFixed(1) : null);
    return { total: list.length, inGame: g.length, perGameSecond: gameSeconds ? +(g.length / gameSeconds).toFixed(2) : null,
             medianMs: q(0.5), p90Ms: q(0.9), maxMs: g.length ? +g[g.length - 1].toFixed(1) : null };
}

// The W-12 regression check: the song clock must not keep jumping in game.
function checkClock(chk, ts, unmuted) {
    const maxTimerErrors = maxTimerErrorsArg !== null ? maxTimerErrorsArg : unmuted ? '0.25' : null;
    if (maxTimerErrors === null || !chk.reachedGame) return;
    const r = ts && ts.perGameSecond;
    if (r === null || r === undefined) chk.fails.push('no timer-error statistics (run predates them)');
    else if (r > +maxTimerErrors) chk.fails.push(`song clock: ${r} "timer error is large" lines a game second > ${maxTimerErrors} (${ts.inGame} in game, median ${ts.medianMs} ms, max ${ts.maxMs} ms)`);
    else return;
    chk.pass = false; chk.code = 1; chk.line = `CHECK FAIL: ${chk.fails.join('; ')}`;
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
    checkClock(c, sm.timerErrors, !!(sm.audio && sm.audio.unmuted));
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
        '--disable-sync', '--autoplay-policy=no-user-gesture-required',
        ...(unmute ? [] : ['--mute-audio']),
        ...media,
        ...chromeArgs,
    ],
});
const ctx = await browser.newContext({ viewport: { width: 1280, height: 860 } });
if (camera === 'deny') await ctx.clearPermissions();
const page = await ctx.newPage();
let pacingLib = null;
if (pacing) {
    pacingLib = await import('./lib/pacing.mjs');
    await pacingLib.installPacing(page);
}
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

const screens = [], errors = [], samples = [], detect = [], movePassed = [], poseLog = [], timerErrors = [];
page.on('console', (m) => {
    const e = { t: el(), type: m.type(), text: m.text() };
    appendFileSync(logf, JSON.stringify(e) + '\n');
    const mm = e.text.match(/will enter '([^']+)'/);
    if (mm) { screens.push({ t: e.t, screen: mm[1] }); console.log(`[${e.t}s] SCREEN ${mm[1]}`); }
    const d = parseDetect(e.text, e.t);
    if (d) detect.push(d);
    const mp = e.text.match(/move_passed #(\d+) player=(\d) frac=([-\d.naif]+)/i);
    if (mp) movePassed.push({ t: e.t, n: +mp[1], p: +mp[2], frac: parseFloat(mp[3]) });
    // StandardStream::UpdateTime: the song clock jumped more than 50 ms against
    // its own timer between two polls (W-12).
    const te = e.text.match(/timer error is large: (-?[\d.]+)/);
    if (te) timerErrors.push({ t: e.t, ms: parseFloat(te[1]), screen: cur() });
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
                audio: window._dc3Audio && window._dc3Audio.ctx ? {
                    rate: window._dc3Audio.ctx.sampleRate, state: window._dc3Audio.ctx.state,
                    baseLatency: window._dc3Audio.ctx.baseLatency, outputLatency: window._dc3Audio.ctx.outputLatency,
                    underruns: window._dc3Audio.underruns ? window._dc3Audio.underruns.underrunEvents : null,
                } : null,
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
const url = `http://127.0.0.1:${port}/?x=1&env=${encodeURIComponent('DC3_DETECTFRAC_PROBE=1;DC3_SCORING_DEBUG=1' + (pacing ? ';DC3_FRAME_TRACE=1' : '') + (extraEnv ? ';' + extraEnv : ''))}${extra}`;
// One song from song select: index, difficulty, then play it through.
// Returns 'left_gameplay_to_<screen>' or 'timeout_in_<screen>'.
const songs = [];
async function playSong(idx, diff, tag, deadline) {
    await new Promise((r) => setTimeout(r, 3000));
    for (let i = 0; i < idx; i++) { await press('ArrowDown'); await new Promise((r) => setTimeout(r, 400)); }
    await shot(`${tag}song_select`);
    await press('Enter');
    const b2 = screens.length;
    const song = { tag, index: idx, difficulty: diff, fromScreen: b2, tStart: el() };
    songs.push(song);
    const inGame = () => screens.slice(b2).some((s) => s.screen === 'game_screen');
    // Difficulty: the seldiff list opens on `beginner`; Down N times, then the
    // Enter loop below confirms it, `play` and `skip_waiting`.
    if (await waitFor(() => screens.slice(b2).some((s) => s.screen === 'multiuser_screen') || inGame(), 30000) && !inGame()) {
        await new Promise((r) => setTimeout(r, 2500));
        const downs = selectDifficulty ? DIFFS.indexOf(diff) : 0;
        for (let i = 0; i < downs; i++) { await press('ArrowDown'); await new Promise((r) => setTimeout(r, 500)); }
        console.log(`[${el()}s] multiuser_screen: ${downs} down(s) for ${selectDifficulty ? diff : '(not selected: Enter-only driver)'}`);
    }
    for (let i = 0; i < 30 && !inGame(); i++) {
        await new Promise((r) => setTimeout(r, 2500));
        if (!/loading/.test(cur())) await press('Enter');
    }
    let lastShot = 0;
    const ok = await waitFor(() => {
        if (cur() === 'game_screen' && Date.now() - lastShot > 20000) { lastShot = Date.now(); shot(`${tag}ingame`); }
        const sl = screens.slice(b2);
        const gi = sl.findIndex((s) => s.screen === 'game_screen');
        return gi >= 0 && sl.slice(gi + 1).some((s) => !/game_screen|pause/.test(s.screen));
    }, Math.max(0, deadline - Date.now()));
    song.result = ok ? `left_gameplay_to_${cur()}` : `timeout_in_${cur()}`;
    song.tEnd = el();
    return song.result;
}
let tracing = null;
if (traceFile) {
    await browser.startTracing(page, { path: traceFile, screenshots: false,
        categories: ['devtools.timeline', 'disabled-by-default-devtools.timeline', 'disabled-by-default-devtools.timeline.frame',
            'v8', 'v8.gc', 'disabled-by-default-v8.gc', 'blink', 'blink.user_timing', 'gpu', 'viz', 'cc', 'toplevel', 'loading', 'netlog'] });
    tracing = new Promise((r) => setTimeout(r, traceSeconds * 1000)).then(() => browser.stopTracing())
        .then(() => console.log(`[${el()}s] trace written: ${traceFile}`)).catch((e) => console.log('trace failed: ' + e.message));
}
try {
    await page.goto(url, { waitUntil: 'domcontentloaded', timeout: 30000 });
    const deadline = Date.now() + timeoutS * 1000;
    if (!await waitFor(() => seen('attract_screen'), 120000)) throw new Error('never reached attract_screen');
    await shot('attract');
    for (const target of ['title_screen', 'main_screen', 'choose_mode_screen', 'song_select_screen']) {
        // 20 tries: with a camera on, a first boot can run the party-mode
        // tutorial (tutorial_party_mode_screen_0..3, Enter each) before main.
        for (let tries = 0; tries < 20 && !seen(target); tries++) {
            if (await waitFor(() => seen(target), 4000)) break;
            if (Date.now() > deadline) break;
            // The title list is two items (title_screen_menu, start_the_party)
            // and a dancing camera's hand cursor can leave the highlight on
            // the second: Enter then starts Party Time and its tutorial
            // (measured: console `HamNavList ... sym='start_the_party'
            // selecting=0`). Up first puts it back on the first item.
            if (cur() === 'title_screen') { await press('ArrowUp'); await new Promise((r) => setTimeout(r, 300)); }
            await press('Enter');
        }
        if (!seen(target)) throw new Error(`never reached ${target} (current ${cur()})`);
    }
    result = await playSong(songIndex, difficulty, '', deadline);
    if (thenSongIndex !== null && /^left_gameplay_/.test(result)) {
        // Through the results screens back to song select (Enter), then song B.
        const back = screens.length;
        for (let i = 0; i < 14 && !(cur() === 'song_select_screen' && screens.length > back); i++) {
            await new Promise((r) => setTimeout(r, 3000));
            await shot(`results_${i}_${cur()}`);
            if (cur() !== 'song_select_screen') await press('Enter');
        }
        if (cur() !== 'song_select_screen') result = `second_song_not_reached (at ${cur()})`;
        else {
            const r2 = await playSong(thenSongIndex, thenDifficulty, 'B_', deadline);
            result = /^left_gameplay_/.test(r2) ? r2 : `second_song_${r2}`;
        }
    }
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
if (tracing) await Promise.race([tracing, new Promise((r) => setTimeout(r, 30000))]);

// Score: the last engine-side reading taken while in gameplay.
const inGameSamples = samples.filter((s) => s.screen === 'game_screen' && s.game && s.game.players);
const last = inGameSamples.length ? inGameSamples[inGameSamples.length - 1] : null;
const chk = checkDetect(detect, difficulty, result);
checkClock(chk, timerStats(timerErrors, inGameSamples.length), unmute);
const ratings = [];
for (const s of inGameSamples) { const r = s.game.players[0].rating; if (r && (!ratings.length || ratings[ratings.length - 1] !== r)) ratings.push(r); }
const pageLast = [...samples].reverse().find((s) => s.page);
// Per-song final score: the last in-game sample between that song's start and end.
for (const sg of songs) {
    const inSong = samples.filter((s) => s.t >= sg.tStart && s.t <= (sg.tEnd || 1e9) && s.screen === 'game_screen' && s.game && s.game.players);
    const lastS = inSong.length ? inSong[inSong.length - 1] : null;
    sg.finalScoreP0 = lastS ? lastS.game.players[0].score : null;
    sg.gameSeconds = inSong.length;
    sg.trackedSeconds = inSong.filter((s) => s.game.players[0].tracked).length;
}
if (thenSongIndex !== null) {
    if (songs.length < 2) { chk.pass = false; chk.code = 2; chk.fails.push('second song not played'); chk.line = `CHECK FAIL: second song not played (${result})`; }
    else if (camera !== 'none' && songs.some((sg) => !(sg.finalScoreP0 > 0))) {
        chk.pass = false; chk.code = 1;
        chk.fails.push(`a song scored 0 with the camera: ${songs.map((sg) => `${sg.tag || 'A_'}${sg.finalScoreP0}`).join(' ')}`);
        chk.line = `CHECK FAIL: ${chk.fails.join('; ')}`;
    }
}
let pacingReport = null;
if (pacingLib) {
    try {
        const raw = await Promise.race([pacingLib.collectPacing(page), new Promise((_, r) => setTimeout(() => r(new Error('collect timeout')), 20000))]);
        pacingReport = pacingLib.analyzePacing(raw);
        writeFileSync(`${out}/pacing.json`, JSON.stringify({ report: pacingReport, raw }, null, 0));
        console.log(pacingLib.formatPacing(pacingReport));
    } catch (e) { console.log('PACING collect failed: ' + e.message); }
}
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
    audio: { unmuted: unmute, ctx: [...samples].reverse().map((s) => s.audio).find(Boolean) || null },
    timerErrors: timerStats(timerErrors, inGameSamples.length),
    songs: songs.map(({ fromScreen, ...r }) => r),
    pacing: pacingReport ? { phases: pacingReport.phases, raf: pacingReport.raf, stalls: pacingReport.stalls, causes: pacingReport.causes } : null,
    screens, samples, detect,
};
summary.check = { pass: chk.pass, reachedGame: chk.reachedGame, fails: chk.fails };
writeFileSync(`${out}/summary.json`, JSON.stringify(summary, null, 1));
console.log(`RESULT ${result} camera=${camera} difficulty=${difficulty}${selectDifficulty ? '' : '(not selected)'} engine=${chk.engine.join(',') || '?'} score=${summary.finalScoreP0} detectFrac(p0) n=${chk.stats.n} mean=${chk.stats.mean && chk.stats.mean.toFixed(4)} min=${chk.stats.min} max=${chk.stats.max} branches=${JSON.stringify(chk.stats.branches)} ratings=${ratings.join('>')} tracked ${summary.gameSecondsTracked}/${summary.gameSeconds}s wasmMax=${summary.wasmMBMax}MB errors=${errors.length}`);
console.log(`AUDIO unmuted=${unmute} ctx=${JSON.stringify(summary.audio.ctx)} timer errors in game: ${JSON.stringify(summary.timerErrors)}`);
if (songs.length > 1) console.log(`SONGS ${songs.map((sg) => `${sg.tag || 'A_'}index=${sg.index} ${sg.result} score=${sg.finalScoreP0} tracked ${sg.trackedSeconds}/${sg.gameSeconds}s`).join(' | ')}`);
console.log(chk.line);
try { await Promise.race([browser.close(), new Promise((r) => setTimeout(r, 5000))]); } catch {}
process.exit(chk.code);
