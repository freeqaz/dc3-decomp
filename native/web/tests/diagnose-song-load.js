#!/usr/bin/env node
/**
 * DC3 Web Port — Song Loading Diagnosis
 *
 * Navigates to game_screen via JS injection (simulating the screen transition)
 * to reproduce and diagnose the song-loading hang.
 *
 * Usage:
 *   xvfb-run -a node native/web/tests/diagnose-song-load.js --no-server
 *   xvfb-run -a node native/web/tests/diagnose-song-load.js --no-server --verbose
 */

const { chromium } = require('playwright');
const { spawn } = require('child_process');
const http = require('http');
const path = require('path');

const args = process.argv.slice(2);
const hasFlag = (name) => args.includes(`--${name}`);
function getArg(name, defaultVal) {
    const idx = args.indexOf(`--${name}`);
    if (idx === -1) return defaultVal;
    return args[idx + 1] || defaultVal;
}

const PORT = parseInt(getArg('port', '8420'), 10);
const URL = `http://localhost:${PORT}`;
const VERBOSE = hasFlag('verbose');
const NO_SERVER = hasFlag('no-server');
const TIMEOUT_S = parseInt(getArg('timeout', '90'), 10);
const HANG_TIMEOUT_S = parseInt(getArg('hang-timeout', '15'), 10);
const SAVE_LOGS = getArg('save-logs', null);

function log(msg) { console.log(`[diag] ${msg}`); }

function waitForServer(url, timeoutMs = 15000) {
    return new Promise((resolve, reject) => {
        const deadline = Date.now() + timeoutMs;
        const check = () => {
            http.get(`${url}/api/health`, (res) => {
                if (res.statusCode === 200) return resolve();
                retry();
            }).on('error', retry);
        };
        const retry = () => {
            if (Date.now() > deadline) return reject(new Error('Server did not start'));
            setTimeout(check, 300);
        };
        check();
    });
}

(async () => {
    let serverProc = null;
    let browser = null;
    let exitCode = 0;

    const allLogs = [];
    const errors = [];
    let lastLogTime = Date.now();
    const startTime = Date.now();

    // Milestones we want to track (strings from actual engine output)
    const milestones = {
        'attract_screen': false,
        'main_screen': false,
        'transition complete': false,
        'game_screen': false,
        'PollForLoading': false,
        'FileMerger': false,
        'song merger': false,
        'IsLoaded': false,
        'StartIntro': false,
    };

    try {
        // -- Server --
        if (!NO_SERVER) {
            const serverScript = path.resolve(__dirname, '..', 'server.py');
            log(`Starting server on port ${PORT}`);
            serverProc = spawn('python', [serverScript, '--port', String(PORT)], {
                stdio: ['ignore', 'pipe', 'pipe'],
                env: { ...process.env }
            });
            try { await waitForServer(URL); } catch (e) { log(`ERROR: ${e.message}`); process.exit(3); }
            log('Server ready');
        }

        // -- Browser --
        log('Launching Chrome with WebGPU...');
        browser = await chromium.launch({
            headless: !process.env.DISPLAY,
            args: [
                '--no-sandbox',
                '--enable-unsafe-webgpu',
                '--use-angle=vulkan',
                '--enable-features=Vulkan,VulkanFromANGLE',
                '--ozone-platform=x11',
                '--disable-extensions',
                '--mute-audio',
            ],
        });
        const page = await browser.newPage({ viewport: { width: 1280, height: 720 } });

        // -- Console capture --
        page.on('console', (msg) => {
            const text = msg.text();
            const elapsed = ((Date.now() - startTime) / 1000).toFixed(2);
            const entry = { elapsed, type: msg.type(), text };
            allLogs.push(entry);
            lastLogTime = Date.now();

            if (VERBOSE || msg.type() === 'error') {
                console.log(`  [${elapsed}s ${msg.type()}] ${text}`);
            }

            // Track milestones
            for (const [key] of Object.entries(milestones)) {
                if (!milestones[key] && text.includes(key)) {
                    milestones[key] = elapsed;
                    if (!VERBOSE) log(`Milestone: "${key}" at ${elapsed}s`);
                }
            }
        });

        page.on('pageerror', (err) => {
            errors.push(err.message || String(err));
            console.log(`  [PAGE_ERROR] ${err.message}`);
        });

        // -- Navigate to app --
        log(`Loading ${URL}...`);
        await page.goto(URL, { waitUntil: 'domcontentloaded', timeout: 30000 });

        // -- Wait for attract screen (engine is running) --
        log('Waiting for attract screen...');
        const attractReady = await new Promise((resolve) => {
            const check = setInterval(() => {
                const found = allLogs.some(l => l.text.includes('attract_screen'));
                if (found || Date.now() - startTime > 30000) {
                    clearInterval(check);
                    resolve(found);
                }
            }, 500);
        });

        if (!attractReady) {
            log('FAIL: attract screen never appeared');
            exitCode = 1;
            throw new Error('attract screen timeout');
        }

        // Wait for attract screen to stabilize + a few render frames
        await new Promise(r => setTimeout(r, 3000));

        // -- Navigate to game_screen via simulated input --
        log('Sending input to navigate past attract screen...');
        // Web input uses window._dc3Keys bitmask polled per-frame by JoypadPoll.
        // Bit positions: X/A=6, Circle/B=5, Start=11, DUp=12, DDown=14, DLeft=15, DRight=13
        // We must hold buttons for multiple frames (>100ms at 30fps) for the engine to see them.

        async function holdButton(bitMask, holdMs = 200, label = '') {
            if (label) log(`  Button: ${label} (0x${bitMask.toString(16)}) hold=${holdMs}ms`);
            await page.evaluate((mask) => { window._dc3Keys |= mask; }, bitMask);
            await new Promise(r => setTimeout(r, holdMs));
            await page.evaluate((mask) => { window._dc3Keys &= ~mask; }, bitMask);
            await new Promise(r => setTimeout(r, 300)); // gap between presses
        }

        const BTN_A     = 1 << 6;   // kPad_X (confirm)
        const BTN_START = 1 << 11;  // kPad_Start
        const BTN_DOWN  = 1 << 14;  // kPad_DDown
        const BTN_UP    = 1 << 12;  // kPad_DUp

        // Step 1: Dismiss attract screen (Start or A)
        await holdButton(BTN_START, 200, 'Start (dismiss attract)');
        await new Promise(r => setTimeout(r, 1000)); // wait for transition

        // Step 2: Press Start again if needed (some screens need it)
        await holdButton(BTN_START, 200, 'Start (skip title)');
        await new Promise(r => setTimeout(r, 1500)); // wait for main menu

        // Step 3: Press A to confirm on main menu (selects first option = "Perform")
        await holdButton(BTN_A, 200, 'A (main menu: Perform)');
        await new Promise(r => setTimeout(r, 1500));

        // Step 4: Press A for mode selection (choose_mode_screen → first mode)
        await holdButton(BTN_A, 200, 'A (choose mode)');
        await new Promise(r => setTimeout(r, 2000));

        // Step 5: We're on song_select_screen. The list shows tier headers.
        // Press A to expand the tier header.
        await holdButton(BTN_A, 200, 'A (expand tier)');
        await new Promise(r => setTimeout(r, 1500));

        // Step 6: Navigate down to a song within the expanded tier
        await holdButton(BTN_DOWN, 200, 'Down (to song)');
        await new Promise(r => setTimeout(r, 500));
        await holdButton(BTN_DOWN, 200, 'Down (to song 2)');
        await new Promise(r => setTimeout(r, 500));

        // Step 7: Select the song
        await holdButton(BTN_A, 200, 'A (select song)');
        await new Promise(r => setTimeout(r, 2000));

        // Step 8: Confirm any follow-up screens (difficulty, ready, etc.)
        await holdButton(BTN_A, 200, 'A (confirm)');
        await new Promise(r => setTimeout(r, 1000));
        await holdButton(BTN_A, 200, 'A (confirm 2)');
        await new Promise(r => setTimeout(r, 1000));
        await holdButton(BTN_START, 200, 'Start (ready up)');
        await new Promise(r => setTimeout(r, 500));

        // -- Monitor for hang during song loading --
        log(`Monitoring for ${TIMEOUT_S}s (hang threshold: ${HANG_TIMEOUT_S}s)...`);
        const monitorDeadline = Date.now() + TIMEOUT_S * 1000;
        let hangDetected = false;

        while (Date.now() < monitorDeadline) {
            await new Promise(r => setTimeout(r, 1000));

            const silentMs = Date.now() - lastLogTime;

            // Report progress every 10s
            const elapsedTotal = ((Date.now() - startTime) / 1000).toFixed(0);
            if (parseInt(elapsedTotal) % 10 === 0) {
                const lastLog = allLogs[allLogs.length - 1];
                log(`  ${elapsedTotal}s: ${allLogs.length} logs, silent=${(silentMs/1000).toFixed(1)}s, last="${lastLog?.text?.substring(0, 60)}"`);
            }

            // Hang detection
            if (allLogs.length > 20 && silentMs > HANG_TIMEOUT_S * 1000) {
                log(`HANG DETECTED at ${elapsedTotal}s — no output for ${(silentMs/1000).toFixed(1)}s`);
                hangDetected = true;
                exitCode = 2;
                break;
            }

            // Success: game reached loading state 4
            if (milestones['DONE (state 4)']) {
                log('Song loading completed successfully!');
                break;
            }
        }

        // -- Results --
        log('');
        log('=== Diagnosis Results ===');
        log(`Total logs: ${allLogs.length}`);
        log(`Errors: ${errors.length}`);
        log(`Hang detected: ${hangDetected}`);
        log('');
        log('Milestones:');
        for (const [key, val] of Object.entries(milestones)) {
            log(`  ${val ? `[${val}s]` : '[ -- ]'} ${key}`);
        }

        // Last 30 log lines
        log('');
        log('--- Last 30 log lines ---');
        const tail = allLogs.slice(-30);
        for (const e of tail) {
            console.log(`  [${e.elapsed}s ${e.type}] ${e.text}`);
        }

        // Grep for interesting patterns
        const patterns = ['FAIL:', 'HANG', 'ERROR', 'ASSERT', 'abort', 'RuntimeError',
                          'PollForLoading', 'IsLoaded', 'LoadMoveData', 'WebAssets: FAILED',
                          'AsyncFile FAILED', 'world.fm', 'song merger'];
        const interesting = allLogs.filter(l =>
            patterns.some(p => l.text.includes(p))
        );
        if (interesting.length > 0) {
            log('');
            log('--- Interesting log lines ---');
            for (const e of interesting) {
                console.log(`  [${e.elapsed}s ${e.type}] ${e.text}`);
            }
        }

        if (SAVE_LOGS) {
            require('fs').writeFileSync(SAVE_LOGS, JSON.stringify(allLogs, null, 2));
            log(`Full logs saved to ${SAVE_LOGS}`);
        }

    } catch (e) {
        if (exitCode === 0) exitCode = 3;
        log(`Error: ${e.message}`);
    } finally {
        if (browser) await browser.close().catch(() => {});
        if (serverProc) {
            serverProc.kill('SIGTERM');
            await new Promise(r => setTimeout(r, 500));
        }
    }

    log(`Exit code: ${exitCode}`);
    process.exit(exitCode);
})();
