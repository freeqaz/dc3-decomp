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
// --only iphone|sound runs just those checks (no game boot except sound).
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
let failures = 0;
const check = (cond, msg) => { console.log(`${cond ? 'ok  ' : 'FAIL'} ${msg}`); if (!cond) failures++; };
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
console.log(failures ? `PHONE: ${failures} check(s) FAILED${sabotage || sabotageSound ? ' (sabotage run: expected)' : ''}` : 'PHONE: all checks passed');
process.exit(failures ? 1 : 0);
