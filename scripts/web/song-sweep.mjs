#!/usr/bin/env node
// Every song in the web build to gameplay + N seconds, one fresh page per song:
// reached gameplay? bounced to the title? load time, wasm heap, page errors, a
// gameplay screenshot.  The web counterpart of scripts/native_song_sweep.py
// (which plays each song to the results on desktop).
//
//   node scripts/web/song-sweep.mjs --port 8438 --order SONG_ORDER.json --out DIR
//        [--songs iceicebaby,indaclub] [--seconds 30] [--timeout 300]
//
// SONG_ORDER.json is what native_song_sweep.py's discovery run writes
// (<out>/song_order.json: the song-select rows as the game lists them and the
// entry focus); web and desktop run the same provider, so a song N rows below
// the entry focus is N ArrowDowns on either.  The game's own
// "DC3 HamNavList: select ... sym='<song>'" console line confirms each pick.
//
// A bounce is visible only as the screen chain: the web console carries no
// MILO_NOTIFY lines (PLAN W-9), so "Could not cache" never shows here.  The
// preload bug read loading_screen -> preloading_screen ->
// meta_loading_uisink_default_screen -> title_screen.
//
// Heap: wasmMB is WebAssembly.Memory byteLength sampled once a second -- with
// the release build's geometric growth that is the reservation, not demand
// (docs/native/WEB.md "Heap per venue").
//
// Exit 0 when every song reached game_screen and stayed for --seconds without a
// page error; 1 otherwise.
import { mkdirSync, writeFileSync, appendFileSync, readFileSync } from 'fs';

const argv = process.argv.slice(2);
const arg = (k, d) => { const i = argv.indexOf('--' + k); return i >= 0 ? argv[i + 1] : d; };
const port = +arg('port', 8438);
const outRoot = arg('out', '/tmp/dc3-web-song-sweep');
const seconds = +arg('seconds', 30);
const timeoutS = +arg('timeout', 300);
const order = JSON.parse(readFileSync(arg('order'), 'utf8'));
const entry = order.entry_index;
let songs = order.rows.filter(([, s, hdr]) => !hdr && s !== 'playlists' && s !== 'random_song');
const want = arg('songs', '');
if (want) { const w = want.split(','); songs = songs.filter(([, s]) => w.includes(s)); }

const { chromium } = await import('playwright');
mkdirSync(outRoot, { recursive: true });

async function playOne(index, song) {
    const out = `${outRoot}/${song}`;
    mkdirSync(out, { recursive: true });
    const t0 = Date.now();
    const el = () => +((Date.now() - t0) / 1000).toFixed(2);
    const logf = `${out}/console.jsonl`;
    writeFileSync(logf, '');
    const browser = await chromium.launch({
        headless: !process.env.DISPLAY,
        args: ['--no-sandbox', '--enable-unsafe-webgpu', '--use-angle=vulkan', '--ozone-platform=x11',
            '--enable-features=Vulkan,VulkanFromANGLE,WebAssemblyJSPromiseIntegration',
            '--disable-extensions', '--disable-background-networking', '--disable-default-apps',
            '--disable-sync', '--autoplay-policy=no-user-gesture-required', '--mute-audio'],
    });
    const ctx = await browser.newContext({ viewport: { width: 1280, height: 860 } });
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
    const screens = [], errors = [], selects = [], http404 = [];
    let wasmMax = 0, wasmAtGame = null;
    page.on('console', (m) => {
        const e = { t: el(), type: m.type(), text: m.text() };
        appendFileSync(logf, JSON.stringify(e) + '\n');
        const mm = e.text.match(/will enter '([^']+)'/);
        if (mm) screens.push({ t: e.t, screen: mm[1] });
        const sel = e.text.match(/HamNavList: select name='right_hand.hnl' selected=(\d+) first=\d+ sym='(\w+)'/);
        if (sel) selects.push({ index: +sel[1], sym: sel[2] });
    });
    page.on('response', (r) => { if (r.status() === 404) http404.push(r.url().replace(/^https?:\/\/[^/]+/, '')); });
    page.on('pageerror', (err) => errors.push({ t: el(), msg: String(err.message || err) }));
    page.on('crash', () => errors.push({ t: el(), msg: 'CRASH' }));
    const press = async (key) => {
        try {
            await page.evaluate(() => document.getElementById('dc3-canvas').focus()).catch(() => {});
            await page.keyboard.down(key); await new Promise((r) => setTimeout(r, 150)); await page.keyboard.up(key);
        } catch (e) {}
    };
    const cur = () => (screens.length ? screens[screens.length - 1].screen : '');
    const seen = (n, from = 0) => screens.slice(from).some((s) => s.screen === n);
    const waitFor = async (pred, ms) => { const d = Date.now() + ms; while (Date.now() < d) { if (pred()) return true; await new Promise((r) => setTimeout(r, 250)); } return false; };
    const shot = async (name) => {
        try { await Promise.race([page.screenshot({ path: `${out}/${name}.png` }), new Promise((_, r) => setTimeout(() => r(new Error('to')), 8000))]); return `${out}/${name}.png`; } catch (e) { return null; }
    };
    let stop = false;
    const sampler = (async () => {
        while (!stop) {
            try {
                const mb = await Promise.race([page.evaluate(() => (window.__dc3Mem ? window.__dc3Mem.buffer.byteLength / 1048576 : 0)),
                    new Promise((_, r) => setTimeout(() => r(new Error('eval timeout')), 4000))]);
                wasmMax = Math.max(wasmMax, mb);
            } catch (e) {}
            await new Promise((r) => setTimeout(r, 1000));
        }
    })();
    const res = { song, index, downs: index - entry, result: 'timeout', gameplay: false, bounced: false };
    try {
        await page.goto(`http://127.0.0.1:${port}/?x=1`, { waitUntil: 'domcontentloaded', timeout: 30000 });
        const deadline = Date.now() + timeoutS * 1000;
        if (!await waitFor(() => seen('attract_screen'), 120000)) throw new Error('never reached attract_screen');
        for (const target of ['title_screen', 'main_screen', 'choose_mode_screen', 'song_select_screen']) {
            for (let tries = 0; tries < 8 && !seen(target); tries++) {
                if (await waitFor(() => seen(target), 4000)) break;
                if (Date.now() > deadline) break;
                await press('Enter');
            }
            if (!seen(target)) throw new Error(`never reached ${target} (current ${cur()})`);
        }
        await new Promise((r) => setTimeout(r, 3000));
        for (let i = 0; i < res.downs; i++) { await press('ArrowDown'); await new Promise((r) => setTimeout(r, 400)); }
        await new Promise((r) => setTimeout(r, 1000));
        await press('Enter');
        const b2 = screens.length;
        const inGame = () => seen('game_screen', b2);
        const bounced = () => { const sl = screens.slice(b2); const li = sl.findIndex((s) => s.screen === 'loading_screen'); return li >= 0 && sl.slice(li).some((s) => s.screen === 'title_screen'); };
        if (await waitFor(() => seen('multiuser_screen', b2) || inGame(), 30000) && !inGame()) {
            await new Promise((r) => setTimeout(r, 2500));
            await press('ArrowDown'); // Easy: the seldiff list opens on beginner
            await new Promise((r) => setTimeout(r, 500));
        }
        for (let i = 0; i < 30 && !inGame() && !bounced(); i++) {
            await new Promise((r) => setTimeout(r, 2500));
            if (!/loading|game_screen|title/.test(cur())) await press('Enter');
        }
        await waitFor(() => inGame() || bounced(), Math.max(0, deadline - Date.now()));
        const tl = screens.slice(b2).find((s) => s.screen === 'loading_screen');
        const tg = screens.slice(b2).find((s) => s.screen === 'game_screen');
        res.loadS = tl && tg ? +(tg.t - tl.t).toFixed(1) : null;
        res.bounced = bounced();
        res.gameplay = inGame() && !res.bounced;
        if (res.gameplay) {
            wasmAtGame = wasmMax;
            await new Promise((r) => setTimeout(r, seconds * 1000));
            res.gameplayPng = await shot('gameplay');
            res.stillInGame = cur() === 'game_screen';
            res.result = res.stillInGame ? `gameplay+${seconds}s` : `left_gameplay_to_${cur()}`;
        } else {
            res.result = res.bounced ? 'BOUNCED_to_title' : `no_gameplay_in_${cur()}`;
            await shot('stuck');
        }
    } catch (e) {
        res.result = 'error: ' + e.message;
        await shot('error');
    }
    stop = true;
    await sampler;
    try { await Promise.race([browser.close(), new Promise((r) => setTimeout(r, 5000))]); } catch {}
    Object.assign(res, {
        selected: selects.length ? selects[selects.length - 1].sym : null,
        wasmMaxMB: +wasmMax.toFixed(1), wasmAtGameMB: wasmAtGame && +wasmAtGame.toFixed(1),
        pageErrors: errors, http404: [...new Set(http404)], elapsed: el(),
        chain: screens.map((s) => s.screen).join(' > '),
    });
    writeFileSync(`${out}/result.json`, JSON.stringify(res, null, 1));
    return res;
}

const all = [];
for (const [index, song] of songs) {
    const r = await playOne(index, song);
    all.push(r);
    console.log(`[web-sweep] ${song} (idx ${index}, sel ${r.selected}): ${r.result} load=${r.loadS}s wasmMax=${r.wasmMaxMB}MB errors=${r.pageErrors.length} 404=${r.http404.length}`);
}
const md = ['| song | selected | result | load s | wasm at game MB | wasm max MB | page errors | 404s |', '|---|---|---|---|---|---|---|---|'];
for (const r of all) md.push(`| ${r.song} | ${r.selected === r.song ? 'ok' : r.selected} | ${r.result} | ${r.loadS} | ${r.wasmAtGameMB} | ${r.wasmMaxMB} | ${r.pageErrors.length} | ${r.http404.length} |`);
writeFileSync(`${outRoot}/summary.md`, md.join('\n') + '\n');
writeFileSync(`${outRoot}/summary.json`, JSON.stringify(all, null, 1));
console.log(md.join('\n'));
process.exit(all.every((r) => r.gameplay && r.stillInGame && !r.pageErrors.length) ? 0 : 1);
