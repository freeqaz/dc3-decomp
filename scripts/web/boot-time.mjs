#!/usr/bin/env node
// Seconds from opening the page to the main menu, as a player gets there
// (PLAN H-123; the desktop twin is scripts/native_boot_time.py).
//
//   node scripts/web/boot-time.mjs [--port 8420] [--runs 3] [--console returning|first]
//                                  [--extra "&env=DC3_BOOT_INTRO=1"] [--out DIR]
//                                  [--versus "&env=DC3_BOOT_INTRO=1"]
//                                  [--presses auto|all|title|none] [--stop-at title_screen]
//                                  [--linger 3]
//
// One browser profile, made new for the run. `--console returning` (the
// default) first loads the page once and plays to the main menu, so the save
// store (PLAN W-36) holds the profile and the global options, the way a
// browser that has played before does; each measured load then opens the page
// again. `--console first` measures loads with `&reset_save=1`, a fresh console
// every time; its first load is also the browser's first visit (a cold HTTP
// cache: the build and the asset bundles are downloaded), so it is printed and
// kept out of the medians, and one more load is made in its place.
//
// A measured load presses Enter the way a player who is in a hurry would: half
// a second after the attract movie comes up, three quarters of a second after
// the title screen does, then every half second until that screen is left.
// It prints when each screen was entered, in seconds from the navigation.
// `--presses auto` (the default) leaves the attract press out when the engine
// said "DC3 Boot: straight to the title screen": there is no movie to skip,
// the attract screen is up for about half a second under the page's splash
// video, and a press that lands in it costs about 0.8 s (measured: the attract
// screen then lasts 1.3 s instead of 0.5). `--presses all` presses anyway.
//
// `--presses none --stop-at title_screen` presses nothing and stops three
// seconds (`--linger`) after the title screen came up: what a returning browser does by
// itself (the intro passed, the page's splash video taken down, the game still
// on the title screen), or how far a first launch gets with no player.
//
// `--versus EXTRA` measures two URLs in turn in the same browser (the page, the
// page + EXTRA, the page, ...), `--runs` loads of each, so a box whose load
// changes during the run changes both alike. With "&env=DC3_BOOT_INTRO=1" that
// is the returning boot against the original sequence on one build.
//
// Exit 0 when every load reached the main menu (or --stop-at), 1 when one did
// not (reported as such, never as a time). Needs a GPU: run outside the sandbox.
import { mkdirSync, mkdtempSync, rmSync, writeFileSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';

const args = process.argv.slice(2);
const arg = (name, dflt) => { const i = args.indexOf(`--${name}`); return i >= 0 ? args[i + 1] : dflt; };
const port = +arg('port', 8420);
const runs = +arg('runs', 3);
const kind = arg('console', 'returning');
const extra = arg('extra', '');
const out = arg('out', null);   // boot-time.json goes here when given
const timeoutS = +arg('timeout', 180);
const versus = arg('versus', null);
const presses = arg('presses', 'auto');
const stopAt = arg('stop-at', 'main_screen');
if (!['auto', 'all', 'title', 'none'].includes(presses)) { console.error('--presses auto|all|title|none'); process.exit(2); }
if (!['returning', 'first'].includes(kind)) { console.error('--console returning|first'); process.exit(2); }

const { chromium } = await import('playwright');
if (out) mkdirSync(out, { recursive: true });
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
const profileDir = mkdtempSync(join(tmpdir(), 'dc3-boot-time-profile-'));

const ctx = await chromium.launchPersistentContext(profileDir, {
    headless: !process.env.DISPLAY,
    viewport: { width: 1280, height: 720 },
    args: [
        '--no-sandbox', '--enable-unsafe-webgpu', '--use-angle=vulkan', '--ozone-platform=x11',
        '--enable-features=Vulkan,VulkanFromANGLE,WebAssemblyJSPromiseIntegration',
        '--disable-extensions', '--disable-background-networking', '--disable-default-apps',
        '--disable-sync', '--autoplay-policy=no-user-gesture-required', '--mute-audio',
    ],
});
const page = ctx.pages()[0] || await ctx.newPage();

// The current load's console: screens as they are entered, and a few lines
// worth quoting.
let nav = 0, screens = [], notes = [];
page.on('console', (m) => {
    const text = m.text();
    const t = +((Date.now() - nav) / 1000).toFixed(2);
    const s = text.match(/will enter '([^']+)'/);
    if (s) screens.push({ screen: s[1], t });
    if (/DC3 Boot: |\[dc3-save\] (restored|nothing stored|reset)/.test(text)) notes.push(text.replace(/; global options.*/, '').slice(0, 160));
});
const cur = () => (screens.length ? screens[screens.length - 1] : null);
const press = async (key) => {
    try {
        await page.evaluate(() => { const c = document.getElementById('dc3-canvas'); if (c) c.focus(); }).catch(() => {});
        await page.keyboard.down(key); await sleep(60); await page.keyboard.up(key);
    } catch (e) {}
};

const PRESS_AFTER = (presses === 'all' || presses === 'auto') ? { attract_screen: 0.5, title_screen: 0.75 }
    : presses === 'title' ? { title_screen: 0.75 } : {};
// --presses auto: this load's engine said it passes the intro by itself.
const straight = () => presses === 'auto' && notes.some((n) => n.includes('DC3 Boot: straight to the title screen'));
const LINGER_S = +arg('linger', 3);   // --stop-at a screen that is not the main menu: watch it this long
// One page load, driven to the main menu (or --stop-at). Returns
// { ok, screens, presses, notes, splashGone }.
async function load(url) {
    screens = []; notes = [];
    nav = Date.now();
    await page.goto(url, { waitUntil: 'domcontentloaded', timeout: 30000 });
    const deadline = nav + timeoutS * 1000;
    let count = 0, last = 0, splashGone = null;
    while (Date.now() < deadline) {
        const c = cur();
        const now = (Date.now() - nav) / 1000;
        if (c && c.screen === 'main_screen') break;
        if (c && c.screen === stopAt && now - c.t >= LINGER_S) break;
        if (splashGone === null) {
            const there = await page.evaluate(() => !!document.getElementById('splash-video')).catch(() => true);
            if (!there) splashGone = +now.toFixed(2);
        }
        if (c && PRESS_AFTER[c.screen] !== undefined && now - c.t >= PRESS_AFTER[c.screen] && now - last >= 0.5
                && !(c.screen === 'attract_screen' && straight())) {
            last = now; count++;
            await press('Enter');
        }
        await sleep(40);
    }
    const c = cur();
    return { ok: !!c && c.screen === stopAt, screens: screens.slice(), presses: count, notes: notes.slice(), splashGone };
}

// The load that makes a returning browser: always pressed through to the main
// menu, whatever --presses / --stop-at say about the measured loads.
async function seedLoad(url) {
    screens = []; notes = [];
    nav = Date.now();
    await page.goto(url, { waitUntil: 'domcontentloaded', timeout: 30000 });
    const deadline = nav + timeoutS * 1000;
    const after = { attract_screen: 0.5, title_screen: 0.75 };
    let last = 0;
    while (Date.now() < deadline) {
        const c = cur();
        const now = (Date.now() - nav) / 1000;
        if (c && c.screen === 'main_screen') break;
        if (c && after[c.screen] !== undefined && now - c.t >= after[c.screen] && now - last >= 0.5) { last = now; await press('Enter'); }
        await sleep(40);
    }
    const c = cur();
    return { ok: !!c && c.screen === 'main_screen', screens: screens.slice() };
}

const base = `http://127.0.0.1:${port}/?x=1${extra}`;
const results = [];
let failed = 0;
try {
    if (kind === 'returning') {
        const seed = await seedLoad(base);
        await sleep(2500);   // the title -> main save reaches IndexedDB in ~60 ms
        const state = await page.evaluate(() => window.dc3SaveState || null).catch(() => null);
        console.log(`seed load: ${seed.ok ? 'main menu' : 'DID NOT REACH THE MAIN MENU'} at ${seed.ok ? seed.screens[seed.screens.length - 1].t : '-'} s; ` +
            `save store ${state ? `${state.mode}, ${state.stores} store(s)` : 'absent (a build without it)'}`);
        if (!seed.ok) throw new Error('the seed load did not reach the main menu');
    }
    if (kind === 'first') {
        const cold = await load(base + '&reset_save=1');
        const t = (name) => { const x = cold.screens.find((y) => y.screen === name); return x ? x.t.toFixed(1) : '-'; };
        console.log(`first visit (cold HTTP cache, fresh console): ${cold.ok ? 'ok' : 'DID NOT REACH THE MAIN MENU'}; ` +
            `title ${t('title_screen')} s, main menu ${t('main_screen')} s`);
        if (!cold.ok) throw new Error('the first visit did not reach the main menu');
        await sleep(2500);
    }
    const arms = versus === null ? [''] : ['', versus];
    for (let i = 0; i < runs; i++) {
        for (const arm of arms) {
            const r = await load((kind === 'first' ? base + '&reset_save=1' : base) + arm);
            r.arm = arm;
            results.push(r);
            if (!r.ok) failed++;
            console.log(`load ${i + 1} (${kind} console${extra + arm ? ' ' + extra + arm : ''}): ${r.ok ? 'ok' : 'DID NOT REACH ' + stopAt + ' (at ' + (cur() ? cur().screen : 'no screen') + ')'}; ` +
                `${r.presses} key press(es); page splash video gone by ${r.splashGone === null ? '-' : r.splashGone + ' s'}`);
            for (const n of r.notes) console.log(`    ${n}`);
            console.log('    ' + r.screens.map((s) => `${s.screen} ${s.t.toFixed(1)}`).join('  '));
            await sleep(2500);
        }
    }
} catch (e) {
    console.log(`error: ${e.message}`);
    failed++;
}
const at = (r, name) => { const s = r.screens.find((x) => x.screen === name); return s ? s.t : null; };
const median = (v) => { const a = v.slice().sort((x, y) => x - y); return a.length ? a[Math.floor(a.length / 2)] : null; };
console.log('');
for (const arm of (versus === null ? [''] : ['', versus])) {
    const all = results.filter((r) => (r.arm || '') === arm);
    const ok = all.filter((r) => r.ok);
    const line = (name) => { const v = ok.map((r) => at(r, name)).filter((x) => x !== null); return v.length ? `${v.map((x) => x.toFixed(1)).join(' ')} s (median ${median(v).toFixed(1)})` : '-'; };
    const tag = `${kind} console${extra + arm ? ' ' + extra + arm : ''}`;
    console.log(`BOOT-TO-TITLE ${tag}: ${line('title_screen')}`);
    console.log(`BOOT-TO-MAIN  ${tag}: ${line('main_screen')}, ${ok.length} of ${all.length} loads` +
        (stopAt === 'main_screen' ? '' : ` reached ${stopAt} and were still on it ${LINGER_S} s later`));
}
if (out) writeFileSync(`${out}/boot-time.json`, JSON.stringify({ port, kind, extra, versus, results }, null, 1));
try { await Promise.race([ctx.close(), sleep(5000)]); } catch (e) {}
// The browser profile was made for this run (about 95 MB by now).
try { rmSync(profileDir, { recursive: true, force: true }); } catch (e) {}
process.exit(failed ? 1 : 0);
