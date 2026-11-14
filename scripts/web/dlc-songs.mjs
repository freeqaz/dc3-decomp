#!/usr/bin/env node
// Play songs BY NAME in the web build and record what a DLC song needs to be
// playable there (PLAN W-23..W-28, docs/native/DLC.md "Web"): the song is in
// song select, it loads, it plays to the results screen, its audio mixes
// non-silent samples, and its files leave MEMFS once the player is back at song
// select.
//
//   node scripts/web/dlc-songs.mjs --port 8463 --out DIR --songs milkshake,hotinherre,sandstorm,thehustle
//        [--difficulty easy] [--timeout 2400] [--env "K=V;K2=V2"] [--extra "&profile=release"]
//        [--expect-missing NAME,...]   names that must NOT be in song select (the DLC-off control)
//        [--no-audio]                  skip the 30 s mix capture per song
//        [--boot-only]                 stop at main_screen: boot bytes and time (W-26)
//
// Songs are picked in a closed loop (Down/Up until the focused list's selected
// symbol is the song), through the test hooks in native/src/main_web.cpp
// (dc3_focus_*), so a package's songs moving the list cannot pick a wrong song.
// Needs a GPU (run outside the sandbox) and `npm ci --prefix scripts/web`.
//
// Per song (DIR/summary.json, songs[]): whether it was listed, the screens it
// went through, the screen after gameplay, the ratings player 0 got, a 30 s
// capture of the engine's mixed output (window MixSources -> WAV through
// _dc3_start_capture/_dc3_download_capture; RMS and peak in dBFS and the share of
// non-zero samples), memory (wasm heap, JS heap, MEMFS bytes under /data and
// /data/dlc) at song select before it, in game, and back at song select, and the
// "DLC song files dropped" line. Plus the whole song-select row list
// (DIR/songlist.json) and boot bytes/time to main_screen.
//
// Exit status: 0 = every song was listed, played through to a non-gameplay
// screen, and (unless --no-audio) its capture is non-silent (RMS > -60 dBFS),
// every --expect-missing name was absent, and the page threw no error (a throw
// in the frame loop's tick stops the game). 1 = one of those failed. 2 = the
// run never reached song select.
import { mkdirSync, writeFileSync, appendFileSync, readFileSync } from 'fs';

const argv = process.argv.slice(2);
const arg = (k, d) => { const i = argv.indexOf('--' + k); return i >= 0 ? argv[i + 1] : d; };
const port = +arg('port', 8463);
const out = arg('out', '/tmp/dc3-dlc-songs');
const songNames = (arg('songs', 'milkshake') || '').split(',').filter(Boolean);
const expectMissing = (arg('expect-missing', '') || '').split(',').filter(Boolean);
const timeoutS = +arg('timeout', 2400);
const extra = arg('extra', '');
const extraEnv = arg('env', '');
const DIFFS = ['beginner', 'easy', 'medium', 'expert'];
const difficulty = arg('difficulty', 'easy');
const audio = !argv.includes('--no-audio');
const bootOnly = argv.includes('--boot-only');
const SILENT_DB = -60;

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
const ctx = await browser.newContext({ viewport: { width: 1280, height: 860 }, acceptDownloads: true });
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

// Network: every finished response's encoded body + headers, and every 404.
const net = { requests: 0, bytes: 0, failed: [], notFound: [], dlcRequests: 0, dlcBytes: 0 };
const netMarks = {};
page.on('requestfinished', async (req) => {
    try {
        const s = await req.sizes();
        const resp = await req.response();
        const b = s.responseBodySize + s.responseHeadersSize;
        net.requests++; net.bytes += b;
        if (/\/api\/(file|bundle)\/dlc/.test(req.url())) { net.dlcRequests++; net.dlcBytes += b; }
        if (resp && resp.status() === 404) net.notFound.push({ t: el(), url: req.url(), screen: cur() });
    } catch (e) {}
});
page.on('requestfailed', (req) => net.failed.push({ t: el(), url: req.url(), err: req.failure() && req.failure().errorText }));

const screens = [], errors = [], audioLog = [], dropLog = [], dlcLog = [];
page.on('console', (m) => {
    const e = { t: el(), type: m.type(), text: m.text() };
    appendFileSync(logf, JSON.stringify(e) + '\n');
    const mm = e.text.match(/will enter '([^']+)'/);
    if (mm) {
        screens.push({ t: e.t, screen: mm[1] });
        netMarks[mm[1]] = netMarks[mm[1]] || { t: e.t, bytes: net.bytes, requests: net.requests };
        console.log(`[${e.t}s] SCREEN ${mm[1]}`);
    }
    if (/DLC song files dropped/.test(e.text)) { dropLog.push(e); console.log(`[${e.t}s] ${e.text}`); }
    if (/DC3 Web: DLC|ContentMgr|cnt[0-9a-f]{8}:/i.test(e.text)) { dlcLog.push(e); if (dlcLog.length < 40) console.log(`[${e.t}s] ${e.text.slice(0, 200)}`); }
    if (/mogg|vorbis|ogg|StreamPlayer|StandardStream|AudioDevice/i.test(e.text)) audioLog.push(e);
});
page.on('pageerror', (err) => { errors.push({ t: el(), msg: String(err.message || err) }); console.log(`[${el()}s] PAGEERROR ${err.message}`); });
page.on('crash', () => { errors.push({ t: el(), msg: 'CRASH' }); console.log('CRASH'); });

const cur = () => (screens.length ? screens[screens.length - 1].screen : '');
const seen = (n) => screens.some((s) => s.screen === n);
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
const waitFor = async (pred, ms) => { const d = Date.now() + ms; while (Date.now() < d) { if (await pred()) return true; await sleep(250); } return false; };
const ev = async (fn, a, ms = 5000) => Promise.race([page.evaluate(fn, a), sleep(ms).then(() => { throw new Error('eval timeout'); })]);
const call = (name, ret, types, args) => ev(([n, r, t, a]) => window.Module && window.Module.ccall ? window.Module.ccall(n, r, t, a) : null, [name, ret, types, args]);
const shot = async (name) => {
    const p = `${out}/${String(Math.round(el())).padStart(4, '0')}s_${name}.png`;
    try { await Promise.race([page.screenshot({ path: p }), sleep(8000).then(() => { throw new Error('to'); })]); } catch (e) {}
    return p;
};
const press = async (key) => {
    try {
        await page.evaluate(() => document.getElementById('dc3-canvas').focus()).catch(() => {});
        await page.keyboard.down(key); await sleep(150); await page.keyboard.up(key);
    } catch (e) {}
};
const mem = async (where) => {
    try {
        const m = await ev(() => ({
            wasmMB: window.__dc3Mem ? +(window.__dc3Mem.buffer.byteLength / 1048576).toFixed(1) : null,
            jsMB: performance.memory ? +(performance.memory.usedJSHeapSize / 1048576).toFixed(1) : null,
        }));
        const data = await call('dc3_memfs_bytes', 'number', ['string'], ['/data']);
        const dlc = await call('dc3_memfs_bytes', 'number', ['string'], ['/data/dlc']);
        return { where, t: el(), screen: cur(), ...m, memfsDataMB: +(data / 1048576).toFixed(1), memfsDlcMB: +(dlc / 1048576).toFixed(1) };
    } catch (e) { return { where, t: el(), error: e.message }; }
};

// Per-second sampler: game state (score/rating) and memory every 10th.
const samples = [];
let stop = false;
const sampler = (async () => {
    let n = 0;
    while (!stop) {
        try {
            const s = await ev(() => ({ game: window.dc3PoseGame || null, frame: window.dc3FrameCount || 0,
                wasmMB: window.__dc3Mem ? +(window.__dc3Mem.buffer.byteLength / 1048576).toFixed(1) : null,
                jsMB: performance.memory ? +(performance.memory.usedJSHeapSize / 1048576).toFixed(1) : null }), null, 4000);
            s.t = el(); s.screen = cur(); samples.push(s);
            if (n++ % 15 === 0) {
                const g = s.game && s.game.players ? s.game.players[0] : null;
                console.log(`[${s.t}s] frame=${s.frame} wasm=${s.wasmMB}MB js=${s.jsMB}MB screen=${s.screen} score=${g && g.score} rating=${g && g.rating}`);
            }
        } catch (e) { samples.push({ t: el(), stalled: true }); }
        await sleep(1000);
    }
})();

// Closed-loop highlight: Down/Up until the focused list's selection is `name`.
async function highlight(name) {
    const target = await call('dc3_focus_data_index', 'number', ['string'], [name]);
    if (target < 0) return { ok: false, why: `not in the focused list (data_index ${target})` };
    for (let i = 0; i < 400; i++) {
        const sel = await call('dc3_focus_selected_sym', 'string', [], []);
        if (sel === name) {
            await waitFor(async () => (await call('dc3_focus_settled', 'number', [], [])) === 1, 5000);
            if ((await call('dc3_focus_selected_sym', 'string', [], [])) === name) return { ok: true, target, presses: i };
        }
        const at = sel ? await call('dc3_focus_data_index', 'number', ['string'], [sel]) : -1;
        await press(at >= 0 && at > target ? 'ArrowUp' : 'ArrowDown');
        await sleep(220);
    }
    return { ok: false, why: `selection never reached ${name} in 400 presses (at ${await call('dc3_focus_selected_sym', 'string', [], [])})`, target };
}

async function listRows() {
    const n = await call('dc3_focus_num_data', 'number', [], []);
    const rows = [];
    for (let i = 0; i < n; i++) rows.push(await call('dc3_focus_data_symbol', 'string', ['number'], [i]));
    return rows;
}

// A 30 s capture of MixSources output, saved as WAV, measured in node.
async function captureAudio(tag) {
    await call('dc3_start_capture', null, [], []);
    await sleep(32000);
    const dl = page.waitForEvent('download', { timeout: 15000 });
    await call('dc3_download_capture', null, [], []);
    const d = await dl;
    const path = `${out}/${tag}_capture.wav`;
    await d.saveAs(path);
    const buf = readFileSync(path);
    const pcm = new Int16Array(buf.buffer, buf.byteOffset + 44, (buf.length - 44) >> 1);
    let sum = 0, peak = 0, nz = 0;
    for (const s of pcm) { sum += s * s; const a = Math.abs(s); if (a > peak) peak = a; if (s) nz++; }
    const rms = Math.sqrt(sum / Math.max(1, pcm.length)) / 32768;
    const db = (x) => (x > 0 ? +(20 * Math.log10(x)).toFixed(1) : -Infinity);
    return { path, seconds: +(pcm.length / 2 / buf.readUInt32LE(24)).toFixed(1), rate: buf.readUInt32LE(24),
             rmsDb: db(rms), peakDb: db(peak / 32768), nonZero: +(nz / Math.max(1, pcm.length)).toFixed(3) };
}

const songs = [];
async function playSong(name, deadline) {
    const sg = { name, tStart: el() };
    songs.push(sg);
    sg.memBefore = await mem('song_select_before');
    const h = await highlight(name);
    sg.listed = h.ok; sg.highlight = h;
    if (!h.ok) { sg.result = `not_listed: ${h.why}`; console.log(`[${el()}s] ${name}: ${sg.result}`); return sg; }
    sg.songSelectShot = await shot(`${name}_song_select`);
    await press('Enter');
    const b2 = screens.length;
    const inGame = () => screens.slice(b2).some((s) => s.screen === 'game_screen');
    if (await waitFor(() => screens.slice(b2).some((s) => s.screen === 'multiuser_screen') || inGame(), 30000) && !inGame()) {
        await sleep(2500);
        for (let i = 0; i < DIFFS.indexOf(difficulty); i++) { await press('ArrowDown'); await sleep(500); }
    }
    for (let i = 0; i < 30 && !inGame(); i++) { await sleep(2500); if (!/loading/.test(cur())) await press('Enter'); }
    if (!inGame()) { sg.result = `never_in_game (at ${cur()})`; return sg; }
    sg.tGame = el();
    await sleep(20000);
    sg.gameplayShot = await shot(`${name}_gameplay`);
    sg.memInGame = await mem('in_game');
    if (audio && cur() === 'game_screen') {
        try { sg.audio = await captureAudio(name); } catch (e) { sg.audio = { error: e.message }; }
        console.log(`[${el()}s] ${name}: capture ${JSON.stringify(sg.audio)}`);
    }
    const ok = await waitFor(() => {
        const sl = screens.slice(b2);
        const gi = sl.findIndex((s) => s.screen === 'game_screen');
        return gi >= 0 && sl.slice(gi + 1).some((s) => !/game_screen|pause/.test(s.screen));
    }, Math.max(0, deadline - Date.now()));
    sg.afterGame = cur();
    sg.result = ok ? `left_gameplay_to_${cur()}` : `timeout_in_${cur()}`;
    sg.tEnd = el();
    sg.screens = screens.slice(b2).map((s) => s.screen);
    if (!ok) return sg;
    await sleep(4000);
    sg.resultsShot = await shot(`${name}_results_${cur()}`);
    const back = screens.length, drops = dropLog.length;
    for (let i = 0; i < 14 && !(cur() === 'song_select_screen' && screens.length > back); i++) {
        await sleep(3000);
        if (cur() !== 'song_select_screen') await press('Enter');
    }
    sg.backAtSongSelect = cur() === 'song_select_screen';
    await sleep(3000);
    sg.drop = dropLog.slice(drops).map((e) => e.text);
    sg.memAfter = await mem('song_select_after');
    return sg;
}

let result = 'timeout', songRows = null;
const env = 'DC3_SCORING_DEBUG=1' + (extraEnv ? ';' + extraEnv : '');
const url = `http://127.0.0.1:${port}/?x=1&env=${encodeURIComponent(env)}${extra}`;
const missingResults = [];
try {
    await page.goto(url, { waitUntil: 'domcontentloaded', timeout: 30000 });
    const deadline = Date.now() + timeoutS * 1000;
    if (!await waitFor(() => seen('attract_screen'), 180000)) throw new Error('never reached attract_screen');
    for (const target of ['title_screen', 'main_screen', 'choose_mode_screen', 'song_select_screen']) {
        if (bootOnly && target === 'choose_mode_screen') break;
        for (let tries = 0; tries < 20 && !seen(target); tries++) {
            if (await waitFor(() => seen(target), 4000)) break;
            if (Date.now() > deadline) break;
            if (cur() === 'autosave_warning_screen') continue;
            const ups = { title_screen: 2, main_screen: 6, choose_mode_screen: 5 }[cur()] || 0;
            for (let u = 0; u < ups; u++) { await press('ArrowUp'); await sleep(350); }
            await press('Enter');
        }
        if (!seen(target)) throw new Error(`never reached ${target} (current ${cur()})`);
    }
    if (bootOnly) {
        await sleep(3000);
        result = 'boot_only';
    } else {
        // The song list is the focused list once it has rows.
        await waitFor(async () => (await call('dc3_focus_num_data', 'number', [], [])) > 10, 60000);
        await sleep(2000);
        songRows = await listRows();
        writeFileSync(`${out}/songlist.json`, JSON.stringify(songRows, null, 1));
        console.log(`[${el()}s] song select: ${songRows.length} rows`);
        for (const n of expectMissing) missingResults.push({ name: n, index: await call('dc3_focus_data_index', 'number', ['string'], [n]) });
        result = 'done';
        for (const n of songNames) {
            const sg = await playSong(n, deadline);
            console.log(`[${el()}s] SONG ${n}: ${sg.result} audio=${sg.audio ? sg.audio.rmsDb + 'dB' : '-'} drop=${(sg.drop || []).join(' / ')}`);
            if (!/^left_gameplay_/.test(sg.result)) { if (!sg.listed) continue; result = `stopped at ${n}: ${sg.result}`; break; }
            if (!sg.backAtSongSelect) { result = `not back at song select after ${n} (at ${cur()})`; break; }
        }
    }
} catch (e) {
    result = 'error: ' + e.message;
    await shot('error');
}
stop = true;
await sampler;

const fails = [];
if (!bootOnly) {
    if (!songRows) fails.push('never listed song select');
    for (const sg of songs) {
        if (!sg.listed) { fails.push(`${sg.name}: ${sg.result}`); continue; }
        if (!/^left_gameplay_/.test(sg.result || '')) fails.push(`${sg.name}: ${sg.result}`);
        if (audio && !(sg.audio && sg.audio.rmsDb > SILENT_DB)) fails.push(`${sg.name}: capture silent or missing (${JSON.stringify(sg.audio)})`);
        const ratings = [];
        for (const s of samples) if (s.t >= (sg.tGame || 1e9) && s.t <= (sg.tEnd || 1e9) && s.game && s.game.players) {
            const r = s.game.players[0].rating; if (r && ratings[ratings.length - 1] !== r) ratings.push(r);
        }
        sg.ratings = ratings;
    }
    for (const m of missingResults) if (m.index >= 0) fails.push(`${m.name} is listed (row ${m.index}) but was expected missing`);
    for (const e of errors) fails.push(`page error at ${e.t}s: ${e.msg.slice(0, 160)}`);
    if (songs.length < songNames.length && songRows) fails.push(`played ${songs.length} of ${songNames.length} songs (${result})`);
}
const code = !bootOnly && !songRows ? 2 : fails.length || /^error/.test(result) ? 1 : 0;
const boot = { mainScreen: netMarks.main_screen || null, titleScreen: netMarks.title_screen || null, attract: netMarks.attract_screen || null };
const summary = { result, url, elapsed: el(), errors, fails, boot, net: { ...net, notFound: net.notFound.slice(0, 200) },
                  songRowCount: songRows ? songRows.length : null, missing: missingResults,
                  songs, dropLog, dlcLog: dlcLog.slice(0, 200), audioLog: audioLog.slice(0, 300), screens, samples };
writeFileSync(`${out}/summary.json`, JSON.stringify(summary, null, 1));
console.log(`BOOT main_screen at ${boot.mainScreen && boot.mainScreen.t}s after ${boot.mainScreen && (boot.mainScreen.bytes / 1048576).toFixed(2)} MB / ${boot.mainScreen && boot.mainScreen.requests} requests; title at ${boot.titleScreen && boot.titleScreen.t}s ${boot.titleScreen && (boot.titleScreen.bytes / 1048576).toFixed(2)} MB`);
console.log(`NET total ${(net.bytes / 1048576).toFixed(1)} MB / ${net.requests} requests, dlc ${(net.dlcBytes / 1048576).toFixed(1)} MB / ${net.dlcRequests}, 404s ${net.notFound.length}, failed ${net.failed.length}`);
for (const sg of songs) console.log(`SONG ${sg.name} ${sg.result} ratings=${(sg.ratings || []).join('>')} audio=${JSON.stringify(sg.audio || null)} mem before=${JSON.stringify(sg.memBefore)} game=${JSON.stringify(sg.memInGame || null)} after=${JSON.stringify(sg.memAfter || null)}`);
console.log(fails.length || code ? `CHECK FAIL (${result}): ${fails.join('; ')}` : `CHECK PASS (${result})`);
try { await Promise.race([browser.close(), sleep(5000)]); } catch {}
process.exit(code);
