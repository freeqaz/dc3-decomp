#!/usr/bin/env node
// Phone checks for the player page (no real phone: Playwright's mobile
// emulation -- a Pixel 7 viewport, touch, isMobile, a mobile UA -- on the
// desktop Chromium, so WebGPU is the desktop's).
//
//   node scripts/web/phone.mjs --port 8461 [--out DIR] [--lan-host 192.168.x.y]
//        [--sabotage]
//
// Checks (exit 1 if any fails):
//   portrait + landscape  the on-screen buttons are shown (no fine pointer),
//                         all 7 inside the viewport, none over the pose panel,
//                         no horizontal page scroll, the game canvas on screen;
//                         a screenshot of each
//   tap                   landscape, ?pose=off: on title_screen a TAP on the A
//                         button (touchscreen, not a click) moves the game to
//                         the next screen
//   camera denied         getUserMedia rejects NotAllowedError (the player
//                         tapped Block): the pose panel says
//                         "Camera blocked" and shows its retry button
//   desktop control       1280x720, no touch: the buttons are NOT shown
//   insecure              http://<lan-host>:<port>/ (not localhost, so not a
//                         secure context): the "needs a secure connection"
//                         card, and the engine never boots
//   iphone, no JSPI       iPhone 15 emulation with WebAssembly.Suspending
//                         removed (Safari 26): the refusal card says iOS 27
//                         and covers the screen; no touch buttons, no pose
//                         panel, getUserMedia never called, engine not booted
//                         (W-31: the owner's iPhone had the camera panel and
//                         the buttons over the message)
//   mac safari, no JSPI   the card names Safari 27
//   version on the card   an iPhone whose user agent says Version/26.6.3
//                         (the owner's phone after "updating to iOS 27") is
//                         told it reports iOS 26.6.3; one that says 27.0 is
//                         not told to update to 27; Chrome on iPhone (no
//                         Version/ token) gets no version line
//   sound button          iOS's audio rule, emulated in an init script (a
//                         context starts suspended; resume() needs a user
//                         activation): once the game runs with its
//                         AudioContext suspended, "Tap for sound" shows; a
//                         touchscreen tap on it resumes the context and hides it
// --sabotage serves a touch_controls.js that dispatches its keys on window
// instead of document (the engine listens on document, capture phase): the
// tap check must then FAIL. That is the negative control for the tap.
// --sabotage-sound serves an index.html that does not track AudioContexts:
// the sound-button check must then FAIL.
//   portrait (np-portrait) Pixel 7 upright: html.dc3-portrait; the 16:9
//                         picture fills the width at the top; the deck fills
//                         the rest with no turn-the-phone hint; 7 buttons under
//                         the picture, A >= 80 px; the camera view under the
//                         "now" card, clear of the buttons; no scroll; the deck
//                         names the screen; the picture is drawn. Then rotated
//                         to landscape (layout off, 16:9 fit, the engine's
//                         buffer follows, picture drawn) and back (same as
//                         before, buffer back), and a tap on A still leaves the
//                         title. ?portrait=0 gives the old page; a 720x1280
//                         desktop window is unchanged unless ?portrait=1.
//   shots                 --only shots --tag T [--env K=V] [--orients ...]:
//                         title, the next screen, song select, gameplay in
//                         portrait and landscape, as <T>-<orientation>-<n>.png;
//                         after the change, the deck must name the song
// --sabotage-portrait class|rotate|switch|screen|size breaks one part of the
// portrait layout (see the comment at its route); run with --only portrait.
// --only iphone|sound|portrait|shots runs just those checks.
import { createRequire } from 'node:module';
import { mkdirSync } from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const here = path.dirname(fileURLToPath(import.meta.url));
const require = createRequire(path.join(here, 'package.json'));
const { chromium, devices } = require('playwright');

const argv = process.argv.slice(2);
const arg = (k, d) => { const i = argv.indexOf('--' + k); return i >= 0 ? argv[i + 1] : d; };
const port = +arg('port', 8461);
const out = arg('out', '/tmp/dc3-web/phone');
const sabotage = argv.includes('--sabotage');
const sabotageSound = argv.includes('--sabotage-sound');
const only = arg('only', '');
const lanHost = arg('lan-host', Object.values(os.networkInterfaces()).flat()
    .find((a) => a && a.family === 'IPv4' && !a.internal)?.address);
mkdirSync(out, { recursive: true });

const browser = await chromium.launch({
    headless: !process.env.DISPLAY,
    args: ['--no-sandbox', '--enable-unsafe-webgpu', '--use-angle=vulkan', '--ozone-platform=x11',
        '--enable-features=Vulkan,VulkanFromANGLE,WebAssemblyJSPromiseIntegration', '--mute-audio',
        '--autoplay-policy=no-user-gesture-required', '--use-fake-device-for-media-stream', '--use-fake-ui-for-media-stream'],
});
let failures = 0, checks = 0;
const check = (cond, msg) => { checks++; console.log(`${cond ? 'ok  ' : 'FAIL'} ${msg}`); if (!cond) failures++; };
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
const pixel = devices['Pixel 7'];

const { defaultBrowserType: _ignored, ...iphone } = devices['iPhone 15'];
const MAC_SAFARI_26 = 'Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) AppleWebKit/605.1.15 (KHTML, like Gecko) Version/26.0 Safari/605.1.15';

async function open({ landscape = false, mobile = true, query = '?pose=off', host = '127.0.0.1', camera = false, block = false,
                      device = null, noJspi = false, userAgent = null, inBrowser = browser, iosAudio = false } = {}) {
    const vp = mobile ? pixel.viewport : { width: 1280, height: 720 };
    const ctx = await inBrowser.newContext(device ? { ...device, ...(landscape ? { viewport: { width: device.viewport.height, height: device.viewport.width } } : {}) }
        : userAgent ? { viewport: vp, userAgent } : mobile
        ? { ...pixel, viewport: landscape ? { width: vp.height, height: vp.width } : vp,
            screen: landscape ? { width: pixel.screen?.height || vp.height, height: pixel.screen?.width || vp.width } : pixel.screen }
        : { viewport: vp });
    if (camera) await ctx.grantPermissions(['camera'], { origin: `http://${host}:${port}` });
    const page = await ctx.newPage();
    // The player tapping "Block" on the permission prompt (the browser runs
    // with --use-fake-ui-for-media-stream, which accepts every prompt).
    if (block) await page.addInitScript(() => {
        navigator.mediaDevices.getUserMedia = () => Promise.reject(new DOMException('Permission denied', 'NotAllowedError'));
    });
    // Safari 26: WebGPU, no JSPI. Also count camera requests.
    await page.addInitScript((noJspi) => {
        if (noJspi && typeof WebAssembly !== 'undefined') delete WebAssembly.Suspending;
        window.__gumCalls = 0;
        if (navigator.mediaDevices && navigator.mediaDevices.getUserMedia) {
            const g = navigator.mediaDevices.getUserMedia.bind(navigator.mediaDevices);
            navigator.mediaDevices.getUserMedia = (...a) => { window.__gumCalls++; return g(...a); };
        }
    }, noJspi);
    // iOS's audio rule, which Chromium here does not apply (every autoplay
    // policy flag still starts an AudioContext "running" on this box,
    // MEASURED): a context starts suspended, and resume() only works during a
    // user activation (navigator.userActivation.isActive: the end of a tap).
    if (iosAudio) await page.addInitScript(() => {
        const Orig = window.AudioContext;
        window.AudioContext = class extends Orig {
            constructor(o) { super(o); super.suspend(); }
            resume() {
                if (navigator.userActivation && navigator.userActivation.isActive) return super.resume();
                return Promise.reject(new DOMException('needs a user gesture (iOS rule, emulated)', 'NotAllowedError'));
            }
        };
    });
    if (sabotageSound) {
        await page.route(/\/(index\.html)?(\?.*)?$/, async (route) => {
            const r = await route.fetch();
            const body = (await r.text()).replace('window.AudioContext = Tracked;', '');
            await route.fulfill({ response: r, body });
        });
    }
    // --sabotage-portrait KIND: break one part of the portrait layout; the
    // portrait check named for it must then FAIL (--only portrait).
    //   class   portrait.js never sets html.dc3-portrait
    //   rotate  portrait.js decides once at load and ignores rotation
    //   switch  ?portrait=0 is ignored
    //   screen  the page never tells portrait.js the screen
    //   size    the buttons keep the old landscape size in portrait
    const sabPortrait = arg('sabotage-portrait', '');
    if (sabPortrait) {
        const edits = {
            class: ['portrait.js', "root.classList.toggle('dc3-portrait', on);", 'void 0;'],
            rotate: ['portrait.js', "window.addEventListener('resize', update);", 'update = function () {};'],
            switch: ['portrait.js', "q === '0' ? 'off'", "false ? 'off'"],
            screen: ['index', "if (sm) document.dispatchEvent(new CustomEvent('dc3-screen'", "if (false) document.dispatchEvent(new CustomEvent('dc3-screen'"],
            size: ['touch_controls.js', "'html.dc3-portrait #dc3-touch { --b:var(--tb, 18vw); }' +", ''],
        };
        const [file, from, to] = edits[sabPortrait];
        await page.route(file === 'index' ? /\/(index\.html)?(\?.*)?$/ : '**/' + file, async (route) => {
            const r = await route.fetch();
            const text = await r.text();
            if (!text.includes(from)) console.log(`SABOTAGE ${sabPortrait}: pattern not found in ${file}`);
            await route.fulfill({ response: r, body: text.replace(from, to) });
        });
    }
    if (sabotage) {
        await page.route('**/touch_controls.js', async (route) => {
            const r = await route.fetch();
            const body = (await r.text()).replace('document.dispatchEvent(', 'window.dispatchEvent(');
            await route.fulfill({ response: r, body });
        });
    }
    const st = { screen: '', screens: [], pose: '', logs: [] };
    page.on('console', (m) => {
        const t = m.text();
        st.logs.push(t);
        const s = /will enter '([^']+)'/.exec(t);
        if (s) { st.screen = s[1]; st.screens.push(s[1]); }
        const p = /\[dc3-pose\] state=(\S+)/.exec(t);
        if (p) st.pose = p[1];
    });
    await page.goto(`http://${host}:${port}/${query}`, { waitUntil: 'domcontentloaded', timeout: 30000 });
    return { ctx, page, st };
}
async function waitFor(fn, ms) {
    const end = Date.now() + ms;
    while (Date.now() < end) { if (await fn()) return true; await sleep(250); }
    return false;
}

const layout = (page) => page.evaluate(() => {
    const r = (el) => { const b = el.getBoundingClientRect(); return { l: b.left, t: b.top, r: b.right, b: b.bottom }; };
    const btns = [...document.querySelectorAll('#dc3-touch button')].map(r);
    const panel = document.getElementById('pose-panel');
    const canvas = document.querySelector('canvas#canvas') || document.querySelector('canvas');
    return {
        shown: !!(window.dc3TouchControls && window.dc3TouchControls.shown),
        btns, panel: panel && getComputedStyle(panel).display !== 'none' ? r(panel) : null,
        canvas: canvas ? r(canvas) : null,
        vw: innerWidth, vh: innerHeight, sw: document.documentElement.scrollWidth,
        sh: document.documentElement.scrollHeight,
    };
});
const overlaps = (a, b) => a.l < b.r && b.l < a.r && a.t < b.b && b.t < a.b;

async function refusal(page) {
    return page.evaluate(() => {
        const c = document.getElementById('loading');
        const b = c.getBoundingClientRect();
        return {
            text: c.innerText, cls: c.className, rect: [b.left, b.top, b.right, b.bottom],
            vw: innerWidth, vh: innerHeight, blocked: window.dc3BootBlocked || null,
            touchDom: !!document.getElementById('dc3-touch'), touch: window.dc3TouchControls || null,
            panel: !!document.getElementById('pose-panel'), gum: window.__gumCalls,
            pose: window.dc3PoseStats ? window.dc3PoseStats.state : null,
            sound: !!document.getElementById('dc3-sound') && getComputedStyle(document.getElementById('dc3-sound')).display !== 'none',
        };
    });
}

try {
    // --- iPhone on Safari 26: WebGPU, no JSPI ---------------------------------
    if (!only || only === 'iphone') for (const landscape of [false, true]) {
        const name = 'iphone ' + (landscape ? 'landscape' : 'portrait') + ', no JSPI';
        const { ctx, page, st } = await open({ device: iphone, landscape, noJspi: true, query: '' });
        await sleep(4000);
        const R = await refusal(page);
        check(R.blocked === 'JSPI unavailable' && /iOS 27/.test(R.text) && /Safari 27/.test(R.text) && !/Use Chrome/.test(R.text),
            `${name}: card asks for iOS 27 (blocked=${R.blocked}) "${R.text.replace(/\s+/g, ' ').slice(0, 90)}…"`);
        check(Math.abs(R.rect[0]) < 1 && Math.abs(R.rect[1]) < 1 && Math.abs(R.rect[2] - R.vw) < 1 && Math.abs(R.rect[3] - R.vh) < 1,
            `${name}: the card covers the ${R.vw}x${R.vh} screen (${R.rect.map((v) => v.toFixed(0)).join(',')})`);
        check(!R.touchDom && R.touch && R.touch.shown === false && R.touch.blocked, `${name}: no touch buttons`);
        check(!R.panel && R.gum === 0 && R.pose === 'off', `${name}: camera not asked for (getUserMedia calls ${R.gum}, pose ${R.pose}, panel ${R.panel})`);
        check(st.screens.length === 0, `${name}: engine not booted (${st.screens.length} screens)`);
        await page.screenshot({ path: path.join(out, `iphone-nojspi-${landscape ? 'landscape' : 'portrait'}.png`) });
        await ctx.close();
    }
    if (!only || only === 'iphone') {
        const { ctx, page } = await open({ mobile: false, userAgent: MAC_SAFARI_26, noJspi: true, query: '' });
        await sleep(3000);
        const R = await refusal(page);
        check(/Safari 27/.test(R.text) && /Chrome or Edge 137/.test(R.text) && !/iOS 27/.test(R.text),
            `mac safari, no JSPI: "${R.text.replace(/\s+/g, ' ').slice(0, 110)}…"`);
        await ctx.close();
    }

    // --- the card names the version the phone reports ------------------------
    // The owner's phone said "iOS 27" and its user agent said Version/26.6.3
    // (nginx access log, 2026-11-15): an iOS 26 point update, not iOS 27. The
    // OS token is frozen at 18_7, so Version/ is the only evidence.
    if (!only || only === 'iphone') {
        const ua = (v) => `Mozilla/5.0 (iPhone; CPU iPhone OS 18_7 like Mac OS X) AppleWebKit/605.1.15 (KHTML, like Gecko) ${v} Mobile/15E148 Safari/604.1`;
        const cases = [
            ['iphone on 26.6.3', ua('Version/26.6.3'), (t) => /reports iOS 26\.6\.3 \(Safari 26\.6\.3\)/.test(t) && /needs iOS 27/.test(t) && /Upgrade to iOS 27/.test(t), '26.6.3'],
            ['iphone on 27.0, no JSPI', ua('Version/27.0'), (t) => /reports iOS 27\.0 /.test(t) && !/needs iOS 27/.test(t) && !/Software Update/.test(t), '27.0'],
            ['chrome on iphone', ua('CriOS/151.0.7000.10'), (t) => !/This device reports/.test(t) && /needs iOS 27/.test(t), null],
        ];
        for (const [name, userAgent, good, version] of cases) {
            const { ctx, page } = await open({ device: { ...iphone, userAgent }, noJspi: true, query: '' });
            await sleep(3000);
            const R = await refusal(page);
            const seen = await page.evaluate(() => window.dc3SafariVersion);
            check(R.blocked === 'JSPI unavailable' && good(R.text) && seen === version,
                `${name}: version ${seen} "${R.text.replace(/\s+/g, ' ').slice(0, 150)}…"`);
            if (version === '26.6.3') await page.screenshot({ path: path.join(out, 'iphone-26.6.3.png') });
            await ctx.close();
        }
    }

    // --- sound: iOS's gesture rule (emulated), the "Tap for sound" button -----
    if (!only || only === 'sound') {
        const b2 = browser;
        {
            const { ctx, page, st } = await open({ landscape: true, query: '?pose=off&env=DC3_AUDIO_GAIN%3D0', iosAudio: true });
            const reached = await waitFor(() => st.screen === 'attract_screen' || st.screen === 'title_screen', 150000);
            await waitFor(() => page.evaluate(() => window.dc3SoundState && window.dc3SoundState().button), 8000);
            const before = await page.evaluate(() => window.dc3SoundState ? window.dc3SoundState() : null);
            check(reached && before && before.contexts.includes('suspended') && before.button,
                `sound: game running, AudioContext ${JSON.stringify(before && before.contexts)}, "Tap for sound" ${before && before.button ? 'shown' : 'NOT shown'}`);
            await page.screenshot({ path: path.join(out, 'sound-button.png') });
            if (before && before.button) {
                const at = await page.evaluate(() => { const r = document.getElementById('dc3-sound').getBoundingClientRect(); return { x: r.left + r.width / 2, y: r.top + r.height / 2 }; });
                await page.touchscreen.tap(at.x, at.y);
                const ok = await waitFor(() => page.evaluate(() => { const s = window.dc3SoundState(); return s.contexts.every((c) => c === 'running') && !s.button; }), 5000);
                const after = await page.evaluate(() => window.dc3SoundState());
                check(ok, `sound: a tap on it -> AudioContext ${JSON.stringify(after.contexts)}, button ${after.button ? 'still shown' : 'hidden'}`);
            }
            await ctx.close();
        }
    }
    // --- screenshots: title, main menu, song select, gameplay ----------------
    // --only shots [--tag before|after] [--env K=V] [--query-extra '&portrait=0']
    // Drives the game with the keyboard (Enter through the menus, Enter on a
    // song) in portrait and landscape and saves <tag>-<orientation>-<n>.png.
    if (only === 'shots') {
        const tag = arg('tag', 'shots');
        const env = arg('env', '');
        const extra = arg('query-extra', '');
        const orients = (arg('orients', 'portrait,landscape')).split(',');
        const q = '?' + (argv.includes('--pose') ? '' : 'pose=off&') + 'env=' + encodeURIComponent('DC3_AUDIO_GAIN=0' + (env ? ';' + env : '')) + extra;  // one env= (KEY=VAL;KEY=VAL)
        // Keys as the touch buttons send them (a KeyboardEvent on document,
        // held 160 ms), so the run does not depend on page focus.
        const key = async (page, k) => {
            const send = (t) => page.evaluate(([t, k]) => document.dispatchEvent(new KeyboardEvent(t, { key: k, code: k === ' ' ? 'Space' : k, bubbles: true })), [t, k]);
            await send('keydown'); await sleep(160); await send('keyup'); await sleep(300);
        };
        // Enter until the screen changes (3 tries, 12 s each).
        const advance = async (page, st) => {
            for (let i = 0; i < 3; i++) {
                const n = st.screens.length, from = st.screen;
                await key(page, 'Enter');
                if (await waitFor(() => st.screens.length > n && st.screen !== from, 12000)) return true;
            }
            return false;
        };
        for (const o of orients) {
            const dev = arg('device', '') === 'iphone' ? iphone : null;
            const { ctx, page, st } = await open({ landscape: o === 'landscape', query: q, camera: true, device: dev });
            const shot = async (n) => {
                const f = path.join(out, `${tag}-${dev ? 'iphone-' : ''}${o}-${n}.png`);
                await page.screenshot({ path: f });
                const L = await layout(page);
                const buf = await page.evaluate(() => { const c = document.getElementById('dc3-canvas'); return [c.width, c.height]; });
                const deck = await page.evaluate(() => window.dc3Portrait ? JSON.stringify({ active: window.dc3Portrait.active, screen: window.dc3Portrait.screen, song: window.dc3Portrait.song }) : '-');
                console.log(`     ${f}  screen=${st.screen} canvas css ${JSON.stringify(L.canvas)} buffer ${buf.join('x')} viewport ${L.vw}x${L.vh} deck ${deck}`);
            };
            const at = async (name, ms) => waitFor(() => st.screen === name, ms);
            if (await at('title_screen', 150000)) { await sleep(3000); await shot('1-title'); }
            await advance(page, st);
            await sleep(3000); await shot('2-' + st.screen.replace(/_screen$/, ''));
            for (let i = 0; i < 4 && st.screen !== 'song_select_screen'; i++) { await advance(page, st); await sleep(2000); }
            if (st.screen === 'song_select_screen') { await sleep(4000); await shot('3-songselect'); }
            console.log(`     screens: ${st.screens.join(' > ')}`);
            for (const l of st.logs.filter((l) => l.includes('[NativeSettings]'))) console.log(`     engine: ${l}`);
            await key(page, 'ArrowDown'); await sleep(500); await key(page, 'ArrowDown'); await sleep(800); await key(page, 'Enter');
            const t0 = Date.now();
            // A profile that never played gets the first-run tutorial (five
            // screens) and the multiuser screen between the pick and the song:
            // Enter every 2 s until game_screen is entered (gameplay.mjs).
            const entered = () => st.screens.includes('game_screen') || st.logs.some((l) => /PollForLoading|DONE \(state 4\)/.test(l));
            for (let i = 0; i < 30 && !entered(); i++) { await sleep(2000); if (!entered()) await key(page, 'Enter'); }
            let loaded = await waitFor(() => st.logs.some((l) => l.includes('DONE (state 4)')), 90000);
            console.log(`     gameplay loaded=${loaded} after ${((Date.now() - t0) / 1000).toFixed(0)} s, screen=${st.screen}`);
            if (loaded) {
                await sleep(22000);
                await shot('4-gameplay');
                const probe = await page.evaluate(() => {
                    try { return Module.ccall('dc3_dta_eval', 'string', ['string'], ['{meta_performer get_song}']); } catch (e) { return 'ERR ' + e; }
                });
                console.log(`     dta {meta_performer get_song} -> ${probe}`);
                if (tag !== 'before') {
                    const s = await page.evaluate(() => window.dc3Portrait && window.dc3Portrait.song);
                    if (o === 'portrait') check(!!s && s.length > 1, `shots portrait gameplay: the deck names the song "${s}"`);
                }
            }
            await ctx.close();
        }
    }
    // --- probe: does a tap on A leave the title, with and without turning ---
    if (only === 'probe') {
        for (const turn of (arg('turns', 'none,twice')).split(',')) {
            // --camera: the fake camera on (pose on), as the portrait checks run.
            const cam = !!arg('camera', '');
            const { ctx, page, st } = await open(cam ? { query: '?env=DC3_AUDIO_GAIN%3D0', camera: true } : { query: '?pose=off&env=DC3_AUDIO_GAIN%3D0' });
            const reached = await waitFor(() => st.screen === 'title_screen', 240000);
            await sleep(2500);
            const pv = pixel.viewport;
            if (turn === 'twice') {
                await page.setViewportSize({ width: pv.height, height: pv.width }); await sleep(2500);
                await page.setViewportSize(pv); await sleep(2500);
            } else if (turn === 'once') {
                await page.setViewportSize({ width: pv.height, height: pv.width }); await sleep(5000);
            } else await sleep(5000);
            const results = [];
            for (let i = 0; i < 3; i++) {
                const A = await page.evaluate(() => { const b = document.querySelector('#dc3-touch button[data-key="Enter"]').getBoundingClientRect(); return [b.left + b.width / 2, b.top + b.height / 2]; });
                const n = st.screens.length;
                await page.touchscreen.tap(A[0], A[1]);
                const moved = await waitFor(() => st.screens.length > n, 6000);
                results.push(moved ? st.screen : 'stayed');
                if (moved) break;
            }
            console.log(`probe camera=${cam} turn=${turn} reached=${reached}: taps -> ${results.join(', ')}; logs after title: ${st.logs.filter((l) => /canvas box|Joypad|keydown/i.test(l)).slice(-4).join(' | ')}`);
            await ctx.close();
        }
    }

    // --- portrait layout, rotation, the switch back (np-portrait) -----------
    if (!only || only === 'portrait') {
        const P = (page) => page.evaluate(() => {
            const r = (el) => { if (!el) return null; const b = el.getBoundingClientRect(); return { l: b.left, t: b.top, r: b.right, b: b.bottom, w: b.width, h: b.height }; };
            const vis = (el) => !!el && getComputedStyle(el).display !== 'none' && el.getBoundingClientRect().height > 0;
            const c = document.getElementById('dc3-canvas');
            const deck = document.getElementById('dc3-deck');
            const hint = document.querySelector('#dc3-touch .hint');
            return {
                cls: document.documentElement.classList.contains('dc3-portrait'),
                state: window.dc3Portrait ? { ...window.dc3Portrait } : null,
                canvas: r(c), buffer: [c.width, c.height], reconf: window.miloFrameReconfigures || 0,
                deck: vis(deck) ? r(deck) : null, now: deck ? deck.querySelector('.screen').textContent : '',
                nowCard: deck ? r(deck.querySelector('.now')) : null,
                hint: vis(hint), view: vis(document.getElementById('pose-view')) ? r(document.getElementById('pose-view')) : null,
                btns: [...document.querySelectorAll('#dc3-touch button')].map((b) => ({ key: b.dataset.key, ...r(b) })),
                vw: innerWidth, vh: innerHeight, sh: document.documentElement.scrollHeight,
            };
        });
        // Mean level and lit share of the canvas's pixels in a screenshot.
        const lit = async (page, c) => {
            const png = await page.screenshot({ clip: { x: c.l, y: c.t, width: c.w, height: c.h } });
            return page.evaluate(async (b64) => {
                const img = new Image();
                img.src = 'data:image/png;base64,' + b64;
                await img.decode();
                const cv = document.createElement('canvas');
                cv.width = img.width; cv.height = img.height;
                const g = cv.getContext('2d');
                g.drawImage(img, 0, 0);
                const d = g.getImageData(0, 0, cv.width, cv.height).data;
                let sum = 0, on = 0;
                for (let i = 0; i < d.length; i += 4) { const y = (d[i] + d[i + 1] + d[i + 2]) / 3; sum += y; if (y > 24) on++; }
                return { mean: sum / (d.length / 4), share: on / (d.length / 4) };
            }, png.toString('base64'));
        };
        const near = (a, b, tol = 1.5) => Math.abs(a - b) <= tol;
        const portraitOk = (L, name) => {
            check(L.cls && L.state && L.state.active && L.state.mode === 'auto', `${name}: portrait layout on (class=${L.cls}, state=${JSON.stringify(L.state && { mode: L.state.mode, active: L.state.active, changes: L.state.changes })})`);
            check(L.canvas && near(L.canvas.t, 0) && near(L.canvas.l, 0) && near(L.canvas.w, L.vw) && near(L.canvas.h, L.vw * 9 / 16),
                `${name}: the picture fills the width at the top (${L.canvas && [L.canvas.l, L.canvas.t, L.canvas.w, L.canvas.h].map((v) => v.toFixed(1)).join(',')} in ${L.vw}x${L.vh})`);
            check(L.deck && L.deck.t >= L.canvas.b - 1 && near(L.deck.b, L.vh) && !L.hint,
                `${name}: the deck fills the space under it (${L.deck && [L.deck.t, L.deck.b].map((v) => v.toFixed(0)).join('..')}), no turn-the-phone hint (${L.hint ? 'SHOWN' : 'hidden'})`);
            const A = L.btns.find((b) => b.key === 'Enter');
            check(L.btns.length === 7 && L.btns.every((b) => b.t >= L.canvas.b && b.b <= L.vh + 0.5 && b.l >= 0 && b.r <= L.vw + 0.5) && A && A.w >= 80,
                `${name}: 7 buttons under the picture and on screen, A ${A ? A.w.toFixed(0) : '-'} px (>= 80; 62 before)`);
            check(L.view && L.nowCard && L.view.t >= L.nowCard.b && L.view.h >= 60 && L.btns.every((b) => !overlaps(b, L.view)),
                `${name}: the camera view under the now card, ${L.view ? L.view.h.toFixed(0) : '-'} px tall (>= 60), clear of the buttons`);
            check(L.sh <= L.vh, `${name}: no vertical scroll (${L.sh} in ${L.vh})`);
        };
        const { ctx, page, st } = await open({ query: '?env=DC3_AUDIO_GAIN%3D0', camera: true });
        const reached = await waitFor(() => st.screen === 'title_screen', 240000);
        await sleep(2500);
        let L = await P(page);
        portraitOk(L, 'portrait');
        check(reached && /Press A/.test(L.now), `portrait: the deck says what is on screen: "${L.now}" (${st.screen})`);
        await page.screenshot({ path: path.join(out, 'portrait-deck.png') });
        const buf0 = L.buffer.join('x');
        let pic = await lit(page, L.canvas);
        check(pic.share > 0.2, `portrait: the picture is drawn (mean ${pic.mean.toFixed(1)}, ${(pic.share * 100).toFixed(0)}% lit)`);
        // Rotate to landscape and back, as a player turning the phone.
        const pv = pixel.viewport;
        await page.setViewportSize({ width: pv.height, height: pv.width });
        await sleep(2500);
        L = await P(page);
        const fitW = Math.min(L.vw, L.vh * 16 / 9), fitH = Math.min(L.vh, L.vw * 9 / 16);
        check(!L.cls && !L.deck && L.canvas && near(L.canvas.w, fitW) && near(L.canvas.h, fitH) && L.btns.every((b) => b.l >= 0 && b.t >= 0 && b.r <= L.vw + 0.5 && b.b <= L.vh + 0.5),
            `rotate to landscape: layout off, picture ${L.canvas && L.canvas.w.toFixed(0)}x${L.canvas && L.canvas.h.toFixed(0)} (fit ${fitW.toFixed(0)}x${fitH.toFixed(0)}), buttons on screen`);
        const bufL = L.buffer.join('x');
        check(bufL !== buf0 && near(L.buffer[0] / L.buffer[1], 16 / 9, 0.02), `rotate to landscape: the engine took the new box (buffer ${buf0} -> ${bufL}, reconfigures ${L.reconf})`);
        pic = await lit(page, L.canvas);
        check(pic.share > 0.2, `rotate to landscape: the picture is drawn (mean ${pic.mean.toFixed(1)}, ${(pic.share * 100).toFixed(0)}% lit)`);
        await page.screenshot({ path: path.join(out, 'rotate-landscape.png') });
        await page.setViewportSize(pv);
        await sleep(2500);
        L = await P(page);
        portraitOk(L, 'rotate back to portrait');
        check(L.buffer.join('x') === buf0, `rotate back to portrait: buffer ${bufL} -> ${L.buffer.join('x')} (was ${buf0})`);
        pic = await lit(page, L.canvas);
        check(pic.share > 0.2, `rotate back to portrait: the picture is drawn (mean ${pic.mean.toFixed(1)}, ${(pic.share * 100).toFixed(0)}% lit)`);
        await page.screenshot({ path: path.join(out, 'rotate-back.png') });
        // Input after the two turns: a tap on the cluster's A leaves the title.
        const A = L.btns.find((b) => b.key === 'Enter');
        const before = st.screens.length, from = st.screen;
        const hit = await page.evaluate(([x, y]) => { const e = document.elementFromPoint(x, y); return e ? (e.dataset && e.dataset.key ? 'button ' + e.dataset.key : e.tagName + '#' + e.id) : '-'; }, [(A.l + A.r) / 2, (A.t + A.b) / 2]);
        const p0 = await page.evaluate(() => window.dc3TouchControls.presses);
        // Up to three taps, 6 s apart. With the camera on, the title ignores
        // the FIRST A press even with no rotation at all (`--only probe
        // --camera 1 --turns none`: stayed, then wait_main_after_saveload);
        // with the camera off the first tap leaves it after no, one or two
        // turns. So a second tap is the title, not the rotation; the count
        // is printed.
        let moved = false, taps = 0;
        while (!moved && taps < 3) {
            taps++;
            await page.touchscreen.tap((A.l + A.r) / 2, (A.t + A.b) / 2);
            moved = await waitFor(() => st.screens.length > before && st.screen !== from, 6000);
        }
        await sleep(1500);
        L = await P(page);
        const p1 = await page.evaluate(() => window.dc3TouchControls.presses);
        check(moved && from === 'title_screen' && L.now !== 'Press A to start', `after rotating: tapping A (${hit}, ${taps} tap(s), presses ${p0} -> ${p1}) left ${from} -> ${st.screen}, the deck says "${L.now}"`);
        await ctx.close();

        // The switch back: ?portrait=0 is the old page (centred picture, hint).
        {
            const { ctx, page } = await open({ query: '?pose=off&portrait=0' });
            await sleep(4000);
            const L = await P(page);
            check(!L.cls && !L.deck && L.state && L.state.mode === 'off' && L.hint && L.canvas.t > 100,
                `?portrait=0: the old layout (class=${L.cls}, deck ${L.deck ? 'shown' : 'none'}, hint ${L.hint ? 'shown' : 'hidden'}, picture top ${L.canvas.t.toFixed(0)})`);
            await page.screenshot({ path: path.join(out, 'portrait-off.png') });
            await ctx.close();
        }
        // A tall desktop window: unchanged unless ?portrait=1.
        for (const [q, want] of [['?pose=off', false], ['?pose=off&portrait=1', true]]) {
            const { ctx, page } = await open({ mobile: false, query: q });
            await page.setViewportSize({ width: 720, height: 1280 });
            await sleep(3000);
            const L = await P(page);
            check(L.cls === want && L.btns.length === 0, `desktop 720x1280 ${q}: portrait layout ${L.cls ? 'on' : 'off'} (want ${want ? 'on' : 'off'}), no buttons, picture top ${L.canvas.t.toFixed(0)}`);
            await ctx.close();
        }
    }
    if (only) throw 'only';

    // --- layout, portrait and landscape (pose on so the panel exists) --------
    for (const landscape of [false, true]) {
        const name = landscape ? 'landscape' : 'portrait';
        const { ctx, page, st } = await open({ landscape, query: '?env=DC3_AUDIO_GAIN%3D0', camera: true });
        await waitFor(() => st.screen === 'attract_screen' || st.screen === 'title_screen', 120000);
        await sleep(2000);
        const L = await layout(page);
        check(L.shown, `${name}: on-screen buttons shown`);
        check(L.btns.length === 7, `${name}: 7 buttons (got ${L.btns.length})`);
        check(L.btns.every((b) => b.l >= 0 && b.t >= 0 && b.r <= L.vw + 0.5 && b.b <= L.vh + 0.5),
            `${name}: every button inside the ${L.vw}x${L.vh} viewport`);
        check(!L.panel || L.btns.every((b) => !overlaps(b, L.panel)), `${name}: no button over the pose panel`);
        check(L.sw <= L.vw, `${name}: no horizontal scroll (scrollWidth ${L.sw}, viewport ${L.vw})`);
        check(L.sh <= L.vh, `${name}: no vertical scroll (scrollHeight ${L.sh}, viewport ${L.vh})`);
        check(L.canvas && L.canvas.r > 0 && L.canvas.b > 0 && L.canvas.l < L.vw && L.canvas.t < L.vh,
            `${name}: game canvas on screen ${JSON.stringify(L.canvas)}`);
        check(st.screens.length > 0, `${name}: the engine booted to ${st.screen || '(nothing)'}`);
        check(['tracking', 'noperson', 'waiting'].includes(st.pose), `${name}: camera granted, pose panel state=${st.pose || '-'}`);
        await page.screenshot({ path: path.join(out, `${name}.png`) });
        console.log(`     screenshot ${path.join(out, name + '.png')}`);
        await ctx.close();
    }

    // --- tap A on the title screen -------------------------------------------
    {
        const { ctx, page, st } = await open({ landscape: true, query: '?pose=off&env=DC3_AUDIO_GAIN%3D0' });
        const reached = await waitFor(() => st.screen === 'title_screen', 150000);
        check(reached, `tap: reached title_screen (${st.screens.join(' > ')})`);
        if (reached) {
            await sleep(2500);
            const a = await page.evaluate(() => {
                const b = document.querySelector('#dc3-touch button[data-key="Enter"]').getBoundingClientRect();
                return { x: b.left + b.width / 2, y: b.top + b.height / 2 };
            });
            const before = st.screens.length;
            await page.touchscreen.tap(a.x, a.y);
            const moved = await waitFor(() => st.screens.length > before && st.screen !== 'title_screen', 10000);
            const presses = await page.evaluate(() => window.dc3TouchControls.presses);
            check(moved, `tap: A (touch at ${a.x.toFixed(0)},${a.y.toFixed(0)}, ${presses} press) left title_screen -> ${st.screen}`);
        }
        await ctx.close();
    }

    // --- camera permission not granted ---------------------------------------
    {
        const { ctx, page, st } = await open({ landscape: true, query: '?env=DC3_AUDIO_GAIN%3D0', block: true });
        await waitFor(() => ['denied', 'nocamera', 'error', 'tracking', 'noperson'].includes(st.pose), 60000);
        const ui = await page.evaluate(() => ({
            text: document.getElementById('pose-status')?.textContent || '',
            btn: (() => { const b = document.getElementById('pose-btn'); return !!b && getComputedStyle(b).display !== 'none'; })(),
        }));
        check(st.pose === 'denied' && /blocked/i.test(ui.text) && ui.btn,
            `camera denied: state=${st.pose} "${ui.text}" retry button ${ui.btn ? 'shown' : 'hidden'}`);
        await page.screenshot({ path: path.join(out, 'camera-denied.png') });
        await ctx.close();
    }

    // --- desktop control -----------------------------------------------------
    {
        const { ctx, page } = await open({ mobile: false });
        await sleep(3000);
        const L = await layout(page);
        check(!L.shown && L.btns.length === 0, `desktop: no on-screen buttons (shown=${L.shown}, ${L.btns.length} buttons)`);
        await ctx.close();
    }

    // --- insecure origin -----------------------------------------------------
    if (lanHost) {
        const { ctx, page, st } = await open({ host: lanHost });
        await sleep(5000);
        const s = await page.evaluate(() => ({
            secure: window.isSecureContext,
            text: document.getElementById('loading')?.innerText || document.body.innerText.slice(0, 400),
        }));
        check(!s.secure && /secure connection/i.test(s.text) && st.screens.length === 0,
            `insecure http://${lanHost}:${port}: secure=${s.secure}, card "${s.text.split('\n')[0]}", screens=${st.screens.length}`);
        await page.screenshot({ path: path.join(out, 'insecure.png') });
        await ctx.close();
    } else {
        console.log('skip insecure: no non-loopback IPv4 address');
    }
} catch (e) {
    if (e !== 'only') {
        console.log('ERROR ' + (e.stack || e));
        failures++;
    }
} finally {
    await browser.close();
}
console.log(`PHONE: ${checks} checks, ${checks - failures} passed, ${failures} failed`);
console.log(failures ? `PHONE: ${failures} check(s) FAILED${sabotage || sabotageSound || arg('sabotage-portrait', '') ? ' (sabotage run: expected)' : ''}` : 'PHONE: all checks passed');
process.exit(failures ? 1 : 0);
