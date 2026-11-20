#!/usr/bin/env node
// Does the web build keep the player's save data across a page reload? (PLAN W-36)
//
//   node scripts/web/save-persist.mjs --port 8476 [--out DIR] [--song thehustle]
//        [--extra "&save=off"] [--quick] [--timeout 900] [--profile-dir DIR]
//
// One fresh browser profile (a new user-data dir: no IndexedDB), then four page
// loads of the same URL, driven by keyboard the way a player would:
//
//   1 FRESH    boot, Perform, pick the song. The first-run tutorial must come
//              up. Enter through it (its last screen marks it seen and
//              autosaves), play the song to the results screen (the game saves
//              the score), back to song select.
//   2 RELOAD   page.reload(). The store must hand both files back before the
//              game looks for them: the game prints its own "loaded N bytes"
//              line, the tutorial flag and the song's high score are what they
//              were, and picking the song goes straight to gameplay.
//   3 RESTART  the browser is closed and started again on the same profile
//              directory: the same checks, so the data is on disk, not only in
//              a live browser process.
//   4 RESET    the URL with &reset_save=1: the store is emptied, the parameter
//              leaves the address bar, the flag and the score are gone, and
//              picking the song shows the tutorial again.
//
// Exit 0 = every check passed. Exit 1 = a check failed (the list is printed
// and written to DIR/summary.json). Exit 2 = the run did not get far enough to
// judge (boot, navigation or the song timed out).
//
// The controls that must FAIL (exit 1), and why each one matters:
//   --extra "&save=off"     the same build with the store switched off
//   --port <baseline site>  a build from before the change (it has no test
//                           hooks either, so the flag/score checks are "not
//                           asked", neither passed nor failed; the screen and
//                           store checks need no hook)
// Both fail the RELOAD checks: the tutorial comes up again.
//
// --quick stops phase 1 after the tutorial (no song played, ~3 min less): the
// tutorial flag and the profile file are still checked, the score check is
// skipped and says so.
//
// What the checks read:
//   screens    the engine's "will enter '<screen>'" console lines
//   game       Module.ccall('dc3_dta_eval' / 'dc3_profile_song_score'),
//              main_web.cpp: the game's own answer, not the store's
//   store      the page's IndexedDB, opened by this script (not through
//              window.dc3SaveState), and the engine's "DC3 Native: saved /
//              loaded N bytes" lines
//
// Needs a GPU (run outside the sandbox) and `npm ci --prefix scripts/web`.
import { mkdirSync, writeFileSync, appendFileSync, mkdtempSync, rmSync } from 'fs';
import { tmpdir } from 'os';
import { join } from 'path';

const argv = process.argv.slice(2);
const arg = (k, d) => { const i = argv.indexOf('--' + k); return i >= 0 ? argv[i + 1] : d; };
const port = +arg('port', 8420);
const out = arg('out', `/tmp/dc3-web/save-persist-${new Date().toISOString().replace(/[:.]/g, '-').slice(0, 19)}`);
const song = arg('song', 'thehustle');
const extra = arg('extra', '');
const quick = argv.includes('--quick');
const timeoutS = +arg('timeout', 900);
// The browser profile: a new one, removed at the end (170 to 240 MB by then),
// unless --profile-dir names one to use and keep.
const ownProfile = !arg('profile-dir', null);
const profileDir = arg('profile-dir', null) || mkdtempSync(join(tmpdir(), 'dc3-save-persist-'));

const { chromium } = await import('playwright');
mkdirSync(out, { recursive: true });
const t0 = Date.now();
const el = () => +((Date.now() - t0) / 1000).toFixed(2);
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
const logf = `${out}/console.jsonl`;
writeFileSync(logf, '');
const deadline = Date.now() + timeoutS * 1000;

// Everything the console said, tagged with the page load it belongs to.
let load = 0;
const lines = [];   // { t, load, text }
const screens = []; // { t, load, screen }
const errors = [];
const mine = (list) => list.filter((e) => e.load === load);
const cur = () => { const s = mine(screens); return s.length ? s[s.length - 1].screen : ''; };
const seen = (name) => mine(screens).some((s) => s.screen === name);
const waitFor = async (pred, ms) => {
    const d = Math.min(Date.now() + ms, deadline);
    while (Date.now() < d) { if (await pred()) return true; await sleep(250); }
    return false;
};

let ctx = null, page = null;
async function openBrowser() {
    ctx = await chromium.launchPersistentContext(profileDir, {
        headless: !process.env.DISPLAY,
        viewport: { width: 1280, height: 720 },
        args: [
            '--no-sandbox', '--enable-unsafe-webgpu', '--use-angle=vulkan', '--ozone-platform=x11',
            '--enable-features=Vulkan,VulkanFromANGLE,WebAssemblyJSPromiseIntegration',
            '--disable-extensions', '--disable-background-networking', '--disable-default-apps',
            '--disable-sync', '--autoplay-policy=no-user-gesture-required', '--mute-audio',
        ],
    });
    page = ctx.pages()[0] || await ctx.newPage();
    page.on('console', (m) => {
        const e = { t: el(), load, type: m.type(), text: m.text() };
        lines.push(e);
        appendFileSync(logf, JSON.stringify(e) + '\n');
        const mm = e.text.match(/will enter '([^']+)'/);
        if (mm) { screens.push({ t: e.t, load, screen: mm[1] }); console.log(`[${e.t}s] SCREEN ${mm[1]}`); }
        if (/\[dc3-save\]|DC3 Native: (saved|loaded) /.test(e.text)) console.log(`[${e.t}s] ${e.text.slice(0, 220)}`);
    });
    page.on('pageerror', (err) => { errors.push({ t: el(), load, msg: String(err.message || err) }); console.log(`[${el()}s] PAGEERROR ${err.message}`); });
    page.on('crash', () => { errors.push({ t: el(), load, msg: 'CRASH' }); console.log('CRASH'); });
}

const ev = async (fn, a, ms = 5000) => Promise.race([page.evaluate(fn, a), sleep(ms).then(() => { throw new Error('eval timeout'); })]);
const call = async (name, ret, types, args) => {
    try {
        return await ev(([n, r, t, a]) => (window.Module && window.Module.ccall && window.Module['_' + n]) ? window.Module.ccall(n, r, t, a) : null, [name, ret, types, args]);
    } catch (e) { return null; }
};
const press = async (key) => {
    try {
        await page.evaluate(() => document.getElementById('dc3-canvas').focus()).catch(() => {});
        await page.keyboard.down(key); await sleep(150); await page.keyboard.up(key);
    } catch (e) {}
};
const shot = async (name) => {
    const p = `${out}/${String(Math.round(el())).padStart(4, '0')}s_${name}.png`;
    try { await Promise.race([page.screenshot({ path: p }), sleep(8000).then(() => { throw new Error('to'); })]); } catch (e) {}
};

// The game's own answers. null = the build has no such hook, or it failed.
async function dta(expr) {
    const raw = await call('dc3_dta_eval', 'string', ['string'], [expr]);
    if (!raw) return null;
    try { const j = JSON.parse(raw); return j.ok ? j.result.value : null; } catch (e) { return null; }
}
const tutorialSeen = () => dta('{profile_mgr has_seen_tutorial kTutorialGeneral}');
const songScore = () => call('dc3_profile_song_score', 'number', ['string'], [song]);

// The page's IndexedDB, read here rather than through window.dc3SaveState.
async function storeRecords() {
    try {
        return await ev(() => new Promise((resolve) => {
            const fail = (why) => resolve({ error: why });
            if (!window.indexedDB) return fail('no indexedDB');
            const list = indexedDB.databases ? indexedDB.databases() : Promise.resolve([{ name: 'dc3-save' }]);
            list.then((dbs) => {
                if (!dbs.some((d) => d.name === 'dc3-save')) return resolve({ records: [] });
                const req = indexedDB.open('dc3-save');
                req.onerror = () => fail('open failed');
                req.onsuccess = () => {
                    const db = req.result;
                    if (!db.objectStoreNames.contains('files')) { db.close(); return resolve({ records: [] }); }
                    const recs = [];
                    const tx = db.transaction('files', 'readonly');
                    tx.objectStore('files').openCursor().onsuccess = (e) => {
                        const c = e.target.result;
                        if (!c) return;
                        const d = c.value && c.value.data ? new Uint8Array(c.value.data) : new Uint8Array(0);
                        let h = 2166136261;   // FNV-1a, enough to tell two saves apart
                        for (let i = 0; i < d.length; i++) { h ^= d[i]; h = Math.imul(h, 16777619); }
                        recs.push({ key: String(c.key), bytes: d.length, fnv: (h >>> 0).toString(16) });
                        c.continue();
                    };
                    tx.oncomplete = () => { db.close(); resolve({ records: recs }); };
                    tx.onerror = () => { db.close(); fail('read failed'); };
                };
            }, () => fail('databases() failed'));
        }), null, 10000);
    } catch (e) { return { error: e.message }; }
}
const storeState = async () => { try { return await ev(() => window.dc3SaveState || null); } catch (e) { return null; } };

// Closed-loop highlight (scripts/web/dlc-songs.mjs): Down/Up until the focused
// list's selection is `name`.
async function highlight(name) {
    const target = await call('dc3_focus_data_index', 'number', ['string'], [name]);
    if (target === null || target < 0) return { ok: false, why: `not in the focused list (data_index ${target})` };
    for (let i = 0; i < 400; i++) {
        const sel = await call('dc3_focus_selected_sym', 'string', [], []);
        if (sel === name) {
            await waitFor(async () => (await call('dc3_focus_settled', 'number', [], [])) === 1, 5000);
            if ((await call('dc3_focus_selected_sym', 'string', [], [])) === name) return { ok: true, presses: i };
        }
        const at = sel ? await call('dc3_focus_data_index', 'number', ['string'], [sel]) : -1;
        await press(at >= 0 && at > target ? 'ArrowUp' : 'ArrowDown');
        await sleep(220);
    }
    return { ok: false, why: `selection never reached ${name}` };
}

class Stuck extends Error {}

// A page load, then keyboard to song select. `how` = 'goto' | 'reload'.
async function bootToSongSelect(tag, how, url) {
    load++;
    const tBoot = el();
    console.log(`\n[${tBoot}s] ===== load ${load}: ${tag} (${how}${url ? ' ' + url : ''})`);
    if (how === 'reload') await page.reload({ waitUntil: 'domcontentloaded', timeout: 30000 });
    else await page.goto(url, { waitUntil: 'domcontentloaded', timeout: 30000 });
    if (!await waitFor(() => mine(screens).length > 0, 180000)) throw new Stuck(`${tag}: the engine never entered a screen`);
    const firstScreen = cur();
    // What the browser held when this page load started: read at the first
    // screen, before the game's own first save (title -> main) can change it.
    const storeAtBoot = await storeRecords();
    const stateAtBoot = await storeState();
    for (const target of ['main_screen', 'choose_mode_screen', 'song_select_screen']) {
        for (let tries = 0; tries < 30 && !seen(target); tries++) {
            if (await waitFor(() => seen(target), 4000)) break;
            if (Date.now() > deadline) break;
            // autosave_warning_screen advances on its own; an Enter sent there
            // lands on the title list as it appears.
            if (cur() === 'autosave_warning_screen' || /loading/.test(cur())) continue;
            // Lists that do not wrap: Up to the first row (title: the menu;
            // main: Dance; choose mode: Perform), then confirm.
            const ups = { title_screen: 2, main_screen: 6, choose_mode_screen: 5 }[cur()] || 0;
            for (let u = 0; u < ups; u++) { await press('ArrowUp'); await sleep(350); }
            await press('Enter');
        }
        if (!seen(target)) throw new Stuck(`${tag}: never reached ${target} (at ${cur()})`);
        if (target === 'main_screen') console.log(`[${el()}s] main menu ${(el() - tBoot).toFixed(1)} s after the page load`);
    }
    await sleep(3000);
    return { firstScreen, storeAtBoot, stateAtBoot,
             bootSeconds: +(mine(screens).find((s) => s.screen === 'main_screen').t - tBoot).toFixed(1),
             screens: mine(screens).map((s) => s.screen) };
}

// From song select: pick the song and press Enter until gameplay starts or a
// tutorial screen comes up. Returns what came up.
async function pickSong(tag) {
    const h = await highlight(song);
    if (!h.ok) throw new Stuck(`${tag}: ${song} ${h.why}`);
    await shot(`${tag}_song_select`);
    const from = screens.length;
    const since = () => screens.slice(from).map((s) => s.screen);
    const tutorial = () => since().find((s) => /^tutorial_/.test(s)) || null;
    const inGame = () => since().includes('game_screen');
    await press('Enter');
    await waitFor(() => since().includes('multiuser_screen') || tutorial() || inGame(), 30000);
    for (let i = 0; i < 30 && !inGame() && !tutorial(); i++) {
        await sleep(2500);
        if (!/loading/.test(cur())) await press('Enter');
    }
    await shot(`${tag}_after_pick_${cur()}`);
    if (!inGame() && !tutorial()) throw new Stuck(`${tag}: neither gameplay nor a tutorial after picking ${song} (at ${cur()})`);
    return { tutorial: tutorial(), reachedGame: inGame(), screens: since() };
}

// Enter through the tutorial until gameplay starts.
async function finishTutorial(tag) {
    const from = screens.length;
    const inGame = () => screens.slice(from).some((s) => s.screen === 'game_screen') || cur() === 'game_screen';
    for (let i = 0; i < 60 && !inGame(); i++) {
        await sleep(2500);
        if (/^tutorial_/.test(cur())) await press('Enter');
        else if (!/loading/.test(cur())) await press('Enter');
    }
    if (!inGame()) throw new Stuck(`${tag}: the tutorial never led to gameplay (at ${cur()})`);
}

// Gameplay until the game leaves it, then Enter through the results back to
// song select.
async function finishSong(tag) {
    const from = screens.length;
    let lastShot = 0;
    const left = await waitFor(() => {
        if (cur() === 'game_screen' && Date.now() - lastShot > 30000) { lastShot = Date.now(); shot(`${tag}_ingame`); }
        return !/game_screen|pause|loading/.test(cur()) && cur() !== '';
    }, 420000);
    if (!left) throw new Stuck(`${tag}: the song did not end (at ${cur()})`);
    console.log(`[${el()}s] song over: ${cur()}`);
    for (let i = 0; i < 20 && cur() !== 'song_select_screen'; i++) {
        await sleep(3000);
        if (cur() !== 'song_select_screen' && !/loading/.test(cur())) await press('Enter');
    }
    if (cur() !== 'song_select_screen') throw new Stuck(`${tag}: never got back to song select after the song (at ${cur()})`);
    await sleep(3000);
    return { screens: screens.slice(from).map((s) => s.screen) };
}

const checks = [];
const check = (phase, name, pass, detail) => {
    checks.push({ phase, name, pass: !!pass, detail });
    console.log(`  ${pass ? 'ok  ' : 'FAIL'} [${phase}] ${name}${detail !== undefined ? ' — ' + detail : ''}`);
};
// A check on one of the game's own answers. A build without the hooks (the
// control run against a build from before the save store) is not asked, and
// that is neither a pass nor a failure: what fails there is what the player
// sees, the tutorial coming back.
let hooks = true;
const notAsked = [];
const asked = (phase, name, value, pass, detail) => {
    if (value === null && !hooks) {
        notAsked.push(`[${phase}] ${name}`);
        console.log(`  --   [${phase}] ${name} — not asked: this build has no dc3_dta_eval / dc3_profile_song_score`);
        return;
    }
    check(phase, name, pass, detail);
};
const savedLines = () => mine(lines).filter((e) => /DC3 Native: saved \d+ bytes/.test(e.text)).map((e) => e.text);
const loadedLines = () => mine(lines).filter((e) => /DC3 Native: loaded \d+ bytes/.test(e.text)).map((e) => e.text);
const saveLog = () => mine(lines).filter((e) => /\[dc3-save\]/.test(e.text)).map((e) => e.text);

const base = `http://127.0.0.1:${port}/?x=1${extra}`;
const summary = { port, song, extra, quick, profileDir, url: base, phases: {}, checks, errors };
let outcome = 'ok';
try {
    await openBrowser();

    // ---- 1 FRESH ---------------------------------------------------------
    const p1 = summary.phases.fresh = {};
    p1.boot = await bootToSongSelect('fresh', 'goto', base);
    hooks = !!(await ev(() => !!(window.Module && window.Module._dc3_dta_eval && window.Module._dc3_profile_song_score)).catch(() => false));
    summary.hooks = hooks;
    p1.tutorialSeenBefore = await tutorialSeen();
    p1.scoreBefore = await songScore();
    check('fresh', 'the store is empty on a new browser profile', p1.boot.storeAtBoot.records && p1.boot.storeAtBoot.records.length === 0, JSON.stringify(p1.boot.storeAtBoot));
    asked('fresh', 'the game says the tutorial has not been seen', p1.tutorialSeenBefore, p1.tutorialSeenBefore === 0, `has_seen_tutorial = ${p1.tutorialSeenBefore}`);
    asked('fresh', `no high score for ${song}`, p1.scoreBefore, p1.scoreBefore === -1, `score = ${p1.scoreBefore}`);
    p1.pick = await pickSong('fresh');
    check('fresh', 'picking the first song shows the tutorial', !!p1.pick.tutorial, p1.pick.screens.join(' > '));
    if (p1.pick.tutorial) await finishTutorial('fresh');
    await sleep(2000);
    p1.tutorialSeenAfter = await tutorialSeen();
    asked('fresh', 'the game says the tutorial has been seen after it', p1.tutorialSeenAfter, p1.tutorialSeenAfter === 1, `has_seen_tutorial = ${p1.tutorialSeenAfter}`);
    if (!quick) {
        p1.song = await finishSong('fresh');
        p1.score = await songScore();
        asked('fresh', `the profile has a score for ${song} after playing it`, p1.score, p1.score !== null && p1.score >= 0, `score = ${p1.score}`);
    } else {
        await sleep(4000);
        p1.score = null;
    }
    await sleep(1500);   // a save is in IndexedDB 50 ms + one transaction after it
    p1.saved = savedLines();
    p1.saveLog = saveLog();
    p1.store = await storeRecords();
    p1.stateEnd = await storeState();
    let left = p1.store;   // what the previous page load left in the store
    const keys1 = (p1.store.records || []).map((r) => r.key).sort();
    check('fresh', 'the game wrote its profile save', p1.saved.length > 0, `${p1.saved.length} "saved N bytes" line(s)`);
    check('fresh', 'IndexedDB holds the profile and the global options', keys1.includes('saves/profile_0_save.dat') && keys1.includes('global/global'), JSON.stringify(p1.store.records || p1.store));
    // Song select's own file (Recently Played and the remembered sort,
    // docs/native/SONG_SELECT.md) is in the save directory too, written when
    // the song reaches gameplay, so it is the third file the store holds.
    const expected1 = ['global/global', 'saves/profile_0_save.dat', 'saves/song_select.txt'];
    check('fresh', 'IndexedDB holds song select\'s list once the song reached gameplay', keys1.includes('saves/song_select.txt'), keys1.join(', '));
    check('fresh', 'nothing but those three files is stored', keys1.every((k) => expected1.includes(k)), keys1.join(', '));
    // What the game has on disk that the store does NOT keep, by measurement.
    p1.files = await ev(() => (window.dc3SaveFiles ? window.dc3SaveFiles() : null)).catch(() => null);
    if (p1.files) {
        const skipped = p1.files.filter((f) => !f.stored);
        console.log(`  files on disk: ${p1.files.map((f) => `${f.path} ${f.bytes} bytes${f.stored ? '' : ' (not stored)'}`).join('; ')}`);
        check('fresh', 'the only files not stored are the song cache', skipped.every((f) => /\/cache\/songcache\//.test(f.path)),
            skipped.length ? skipped.map((f) => `${f.path} ${f.bytes} bytes`).join(', ') : 'none on disk');
    }

    // ---- 2 RELOAD and 3 RESTART -------------------------------------------
    for (const tag of ['reload', 'restart']) {
        if (tag === 'restart') {
            await ctx.close();
            await sleep(1000);
            await openBrowser();
        }
        const p = summary.phases[tag] = {};
        p.boot = await bootToSongSelect(tag, tag === 'reload' ? 'reload' : 'goto', base);
        p.saveLog = saveLog();
        p.loaded = loadedLines();
        p.tutorialSeen = await tutorialSeen();
        p.score = await songScore();
        check(tag, 'the browser still held the files the last page load left', !!left.records && left.records.length > 0 && JSON.stringify(p.boot.storeAtBoot.records) === JSON.stringify(left.records),
            `${JSON.stringify(p.boot.storeAtBoot.records || p.boot.storeAtBoot)} vs ${JSON.stringify(left.records || left)}`);
        check(tag, 'the game loaded a profile save at boot', p.loaded.length > 0, p.loaded[0] || 'no "loaded N bytes" line');
        asked(tag, 'the game says the tutorial has been seen', p.tutorialSeen, p.tutorialSeen === 1, `has_seen_tutorial = ${p.tutorialSeen}`);
        if (!quick) asked(tag, `the high score for ${song} is back`, p.score, p.score !== null && p.score === p1.score && p.score >= 0, `score = ${p.score}, was ${p1.score}`);
        p.pick = await pickSong(tag);
        check(tag, 'picking the song goes to gameplay with no tutorial', p.pick.reachedGame && !p.pick.tutorial, p.pick.screens.join(' > '));
        await sleep(1500);
        left = p.store = await storeRecords();
    }

    // ---- 4 RESET -----------------------------------------------------------
    const p4 = summary.phases.reset = {};
    p4.boot = await bootToSongSelect('reset', 'goto', base + '&reset_save=1');
    p4.saveLog = saveLog();
    p4.href = await ev(() => location.href);
    p4.state = p4.boot.stateAtBoot;
    p4.tutorialSeen = await tutorialSeen();
    p4.score = await songScore();
    check('reset', 'the browser held nothing when the game booted', p4.boot.storeAtBoot.records && p4.boot.storeAtBoot.records.length === 0, JSON.stringify(p4.boot.storeAtBoot));
    check('reset', 'the parameter left the address bar', !/reset_save/.test(p4.href), p4.href);
    asked('reset', 'the game says the tutorial has not been seen', p4.tutorialSeen, p4.tutorialSeen === 0, `has_seen_tutorial = ${p4.tutorialSeen}`);
    asked('reset', `no high score for ${song}`, p4.score, p4.score === -1, `score = ${p4.score}`);
    check('reset', 'the page reports the reset', !!(p4.state && p4.state.reset), JSON.stringify(p4.state && { mode: p4.state.mode, reset: p4.state.reset, loaded: p4.state.loaded }));
    p4.pick = await pickSong('reset');
    check('reset', 'picking the song shows the tutorial again', !!p4.pick.tutorial, p4.pick.screens.join(' > '));
} catch (e) {
    outcome = e instanceof Stuck ? `stuck: ${e.message}` : `error: ${e.message}`;
    console.log(`\n${outcome}`);
    try { await shot('stuck'); } catch (e2) {}
}

const failed = checks.filter((c) => !c.pass);
summary.outcome = outcome;
summary.elapsed = el();
summary.failed = failed.map((c) => `[${c.phase}] ${c.name}`);
summary.notAsked = notAsked;
writeFileSync(`${out}/summary.json`, JSON.stringify(summary, null, 1));
console.log(`\nSAVE-PERSIST song=${song} extra=${extra || '(none)'} quick=${quick} checks=${checks.length} failed=${failed.length} not_asked=${notAsked.length} outcome=${outcome} elapsed=${el()}s out=${out}`);
for (const c of failed) console.log(`  FAILED [${c.phase}] ${c.name} — ${c.detail}`);
try { await Promise.race([ctx.close(), sleep(5000)]); } catch (e) {}
if (ownProfile) { try { rmSync(profileDir, { recursive: true, force: true }); } catch (e) {} }
// A check that failed is the answer (exit 1) even when the run then got stuck
// because of it; stuck with nothing failed is "could not judge" (exit 2).
const code = failed.length ? 1 : outcome !== 'ok' ? 2 : 0;
console.log(code === 0 ? 'CHECK PASS' : code === 1 ? 'CHECK FAIL' : 'CHECK NOT JUDGED');
process.exit(code);
