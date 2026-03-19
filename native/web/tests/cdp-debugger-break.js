#!/usr/bin/env node
/**
 * DC3 Web Port — CDP Debugger Break
 *
 * Navigates to gameplay, detects the hang, then uses Chrome DevTools Protocol
 * to pause execution and dump the call stack — showing exactly where the
 * WASM is stuck.
 *
 * Usage:
 *   xvfb-run -a --server-args="-screen 0 1920x1080x24" \
 *     node native/web/tests/cdp-debugger-break.js --no-server
 */

const { chromium } = require('playwright');
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
const NO_SERVER = hasFlag('no-server');
const VERBOSE = hasFlag('verbose');
const HANG_SILENCE_S = parseInt(getArg('silence', '5'), 10);

function log(msg) { console.log(`[cdp-break] ${msg}`); }

(async () => {
    let browser = null;

    const allLogs = [];
    let lastLogTime = Date.now();
    const startTime = Date.now();
    const elapsed = () => ((Date.now() - startTime) / 1000).toFixed(2);

    try {
        // -- Launch browser with remote debugging --
        log('Launching Chrome with WebGPU + remote debugging...');
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
            allLogs.push({ t: elapsed(), type: msg.type(), text });
            if (text.trim().length > 0) {
                lastLogTime = Date.now();
            }
            if (VERBOSE || msg.type() === 'error') {
                console.log(`  [${elapsed()}s ${msg.type()}] ${text}`);
            }
        });

        page.on('pageerror', (err) => {
            console.log(`  [PAGE_ERROR] ${err.message}`);
        });

        // -- Get CDP session --
        const cdp = await page.context().newCDPSession(page);
        log('CDP session established');

        // Enable the Debugger domain
        await cdp.send('Debugger.enable');
        log('Debugger domain enabled');

        // Listen for Debugger.paused events
        let pausedResolve = null;
        const pausedPromise = () => new Promise(resolve => { pausedResolve = resolve; });

        cdp.on('Debugger.paused', (params) => {
            log('=== DEBUGGER PAUSED ===');
            if (pausedResolve) pausedResolve(params);
        });

        // -- Navigate --
        log(`Loading ${URL}...`);
        await page.goto(URL, { waitUntil: 'domcontentloaded', timeout: 30000 });

        // -- Helper: wait for log containing text --
        async function waitForLog(text, timeoutMs = 30000) {
            const deadline = Date.now() + timeoutMs;
            while (Date.now() < deadline) {
                if (allLogs.some(l => l.text.includes(text))) return true;
                await new Promise(r => setTimeout(r, 200));
            }
            return false;
        }

        // -- Helper: press key with hold --
        async function pressKey(key, holdMs = 150, label = '') {
            if (label) log(`  Key: ${label}`);
            try {
                await Promise.race([
                    (async () => {
                        await page.keyboard.down(key);
                        await new Promise(r => setTimeout(r, holdMs));
                        await page.keyboard.up(key);
                    })(),
                    new Promise(r => setTimeout(r, 3000)), // timeout if page frozen
                ]);
                await new Promise(r => setTimeout(r, 200));
            } catch {}
        }

        // -- Wait for attract screen --
        log('Waiting for engine startup...');
        if (!await waitForLog('attract_screen', 30000)) {
            log('FAIL: engine never started');
            process.exit(3);
        }
        await new Promise(r => setTimeout(r, 2000));
        await page.click('canvas');

        // -- Navigate to song select --
        // Key presses need longer holds (300ms) and longer stabilization waits
        // because the engine processes input per-frame and screens take time to
        // fully initialize their panels before accepting input.
        await pressKey(' ', 300, 'Start (dismiss attract)');
        await waitForLog("will enter 'title_screen'");
        await new Promise(r => setTimeout(r, 2000));

        await pressKey(' ', 300, 'Start (skip title)');
        await waitForLog("will enter 'main_screen'");
        await new Promise(r => setTimeout(r, 3000));

        await pressKey('Enter', 300, 'A (main menu)');
        if (!await waitForLog("will enter 'choose_mode_screen'")) {
            log('Retrying: A (main menu)');
            await pressKey('Enter', 300, 'A (main menu retry)');
            await waitForLog("will enter 'choose_mode_screen'");
        }
        await new Promise(r => setTimeout(r, 2000));

        await pressKey('Enter', 300, 'A (choose mode)');
        if (!await waitForLog("will enter 'song_select_screen'")) {
            log('Retrying: A (choose mode)');
            await pressKey('Enter', 300, 'A (choose mode retry)');
            if (!await waitForLog("will enter 'song_select_screen'")) {
                log('FAIL: never reached song select');
                process.exit(1);
            }
        }
        await new Promise(r => setTimeout(r, 2000));

        // Scroll to a song and select it
        for (let i = 0; i < 3; i++) {
            await pressKey('ArrowDown', 300, `Down ${i+1}`);
            await new Promise(r => setTimeout(r, 500));
        }
        await pressKey('Enter', 300, 'A (select song)');
        log('Song selected, waiting for game to load and hang...');

        // Press confirmations concurrently (non-blocking)
        const doConfirms = async () => {
            for (let i = 0; i < 8; i++) {
                await new Promise(r => setTimeout(r, 2000));
                await pressKey('Enter', 300, `A (confirm ${i+1})`);
            }
            await pressKey(' ', 300, 'Start');
        };
        doConfirms(); // fire and forget

        // -- Wait for silence (the hang) --
        log(`Waiting for ${HANG_SILENCE_S}s of silence to detect hang...`);
        while (true) {
            await new Promise(r => setTimeout(r, 1000));
            const silentMs = Date.now() - lastLogTime;
            const totalElapsed = elapsed();

            if (silentMs > HANG_SILENCE_S * 1000 && allLogs.length > 50) {
                log(`${totalElapsed}s: ${(silentMs/1000).toFixed(1)}s of silence detected — triggering debugger break`);
                break;
            }

            // Safety: don't wait forever
            if (Date.now() - startTime > 180000) {
                log('Safety timeout reached (180s)');
                break;
            }
        }

        // -- Trigger Debugger.pause and capture stack --
        log('Sending Debugger.pause()...');
        const waitForPause = pausedPromise();
        await cdp.send('Debugger.pause');

        // Wait for the pause event (should be near-instant)
        const pauseResult = await Promise.race([
            waitForPause,
            new Promise(r => setTimeout(r, 5000)).then(() => null),
        ]);

        if (!pauseResult) {
            log('WARNING: Debugger.pause did not trigger within 5s');
            log('The WASM may be in a state where CDP cannot pause it');
        } else {
            const { callFrames, reason } = pauseResult;
            log(`Paused! Reason: ${reason}`);
            log(`Call stack depth: ${callFrames.length}`);
            log('');
            log('=== CALL STACK ===');

            for (let i = 0; i < callFrames.length; i++) {
                const frame = callFrames[i];
                const { functionName, url, location } = frame;
                const name = functionName || '<anonymous>';
                const file = url ? path.basename(url) : '<unknown>';
                const line = location.lineNumber;
                const col = location.columnNumber;

                log(`  #${i}: ${name}`);
                log(`       at ${file}:${line}:${col}`);

                // For WASM frames, try to get scope variables
                if (url && url.includes('wasm')) {
                    log(`       [WASM frame — url: ${url}]`);
                }
            }

            // Also try to get more details from the top frame
            if (callFrames.length > 0) {
                const topFrame = callFrames[0];
                log('');
                log('=== TOP FRAME DETAILS ===');
                log(`  Function: ${topFrame.functionName || '<anonymous>'}`);
                log(`  URL: ${topFrame.url}`);
                log(`  Line: ${topFrame.location.lineNumber}`);
                log(`  Column: ${topFrame.location.columnNumber}`);

                // Try to evaluate something in the paused context
                try {
                    const evalResult = await cdp.send('Debugger.evaluateOnCallFrame', {
                        callFrameId: topFrame.callFrameId,
                        expression: 'typeof self !== "undefined" ? "worker" : "main"',
                    });
                    log(`  Thread: ${evalResult.result.value}`);
                } catch (e) {
                    log(`  (couldn't evaluate on frame: ${e.message})`);
                }

                // Try to get WASM function index if it's a WASM frame
                if (topFrame.url && topFrame.url.includes('wasm')) {
                    log(`  WASM function index: ${topFrame.location.lineNumber}`);
                    log('  (use wasm-objdump or wasm-decompile to map this index to a C++ function)');
                }
            }

            // Resume so browser can clean up
            try {
                await cdp.send('Debugger.resume');
            } catch {}
        }

        // Print last few meaningful log lines for context
        log('');
        log('=== LAST 10 NON-EMPTY LOG LINES ===');
        const meaningful = allLogs.filter(l => l.text.trim().length > 0);
        for (const entry of meaningful.slice(-10)) {
            console.log(`  [${entry.t}s ${entry.type}] ${entry.text}`);
        }

    } catch (e) {
        log(`Error: ${e.message}`);
    } finally {
        if (browser) await browser.close().catch(() => {});
    }

    log('Done');
    process.exit(0);
})();
