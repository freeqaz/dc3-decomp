#!/usr/bin/env node
/**
 * DC3 Web — CDP debugger hang diagnosis.
 *
 * Navigates to gameplay, waits for console silence (hang), then fires
 * Debugger.pause() and dumps the WASM call stack.
 *
 * Usage:
 *   node scripts/web/cdp-break.mjs [--song-index 3] [--silence 5]
 *     [--out DIR] [--port 8420] [--verbose]
 */

import { resolve, basename } from 'path';
import {
    parseArgs, waitForServer, launchBrowser, createCapture,
    navigateTo, selectSong, pressKey,
    outputDir, saveLogs, cleanup,
} from './lib/core.mjs';

const opts = parseArgs({
    port:         { type: 'number', default: 8420 },
    'song-index': { type: 'number', default: 3 },
    silence:      { type: 'number', default: 5 },
    out:          { type: 'string' },
    verbose:      { type: 'flag' },
});

let browser;
try {
    await waitForServer(opts.port);
    const { browser: b, page } = await launchBrowser(opts.port);
    browser = b;

    const cap = createCapture(page, { verbose: opts.verbose });

    // Set up CDP session
    const cdp = await page.context().newCDPSession(page);
    await cdp.send('Debugger.enable');
    console.log('[cdp] Debugger domain enabled');

    let pausedResolve;
    cdp.on('Debugger.paused', (params) => {
        console.log('[cdp] === DEBUGGER PAUSED ===');
        if (pausedResolve) pausedResolve(params);
    });

    // Navigate to gameplay
    await navigateTo(page, cap, 'song_select_screen');
    await new Promise(r => setTimeout(r, 2000));

    await selectSong(page, cap, { scrolls: opts['song-index'] });
    console.log('[cdp] Song selected, waiting for hang...');

    // Fire confirmation keys in background
    const confirmKeys = async () => {
        for (let i = 0; i < 8; i++) {
            await new Promise(r => setTimeout(r, 2000));
            await pressKey(page, 'Enter');
        }
        await pressKey(page, ' ');
    };
    confirmKeys();

    // Wait for silence (the hang)
    const safetyDeadline = Date.now() + 180000;
    while (Date.now() < safetyDeadline) {
        await new Promise(r => setTimeout(r, 1000));
        if (cap.logs.length > 50 && cap.silenceMs() > opts.silence * 1000) {
            console.log(`[cdp] ${(cap.silenceMs()/1000).toFixed(1)}s of silence — triggering break`);
            break;
        }
    }

    // Trigger pause and capture call stack
    const waitForPause = new Promise(r => { pausedResolve = r; });
    await cdp.send('Debugger.pause');

    const pauseResult = await Promise.race([
        waitForPause,
        new Promise(r => setTimeout(r, 5000)).then(() => null),
    ]);

    if (!pauseResult) {
        console.log('[cdp] WARNING: Debugger.pause did not trigger within 5s');
        console.log('[cdp] WASM may have crashed, not hung. Check PAGE_ERROR above.');
    } else {
        const { callFrames, reason } = pauseResult;
        console.log(`[cdp] Paused! Reason: ${reason}, stack depth: ${callFrames.length}\n`);
        console.log('=== CALL STACK ===');

        for (let i = 0; i < callFrames.length; i++) {
            const { functionName, url, location } = callFrames[i];
            const name = functionName || '<anonymous>';
            const file = url ? basename(url) : '<unknown>';
            console.log(`  #${i}: ${name}`);
            console.log(`       at ${file}:${location.lineNumber}:${location.columnNumber}`);
            if (url && url.includes('wasm')) {
                console.log(`       [WASM frame]`);
            }
        }

        if (callFrames.length > 0) {
            const top = callFrames[0];
            console.log('\n=== TOP FRAME ===');
            console.log(`  Function: ${top.functionName || '<anonymous>'}`);
            console.log(`  URL: ${top.url}`);
            console.log(`  Line: ${top.location.lineNumber}`);

            try {
                const evalResult = await cdp.send('Debugger.evaluateOnCallFrame', {
                    callFrameId: top.callFrameId,
                    expression: 'typeof self !== "undefined" ? "worker" : "main"',
                });
                console.log(`  Thread: ${evalResult.result.value}`);
            } catch (e) {
                console.log(`  (couldn't evaluate: ${e.message})`);
            }

            if (top.url && top.url.includes('wasm')) {
                console.log(`  WASM function index: ${top.location.lineNumber}`);
                console.log('  (use wasm-objdump to map index to C++ function)');
            }
        }

        try { await cdp.send('Debugger.resume'); } catch {}
    }

    // Save logs
    const dir = outputDir('cdp-break', opts.out);
    saveLogs(cap.logs, dir);

    console.log('\n=== LAST 10 LOG LINES ===');
    const meaningful = cap.logs.filter(l => l.text.trim().length > 0);
    for (const e of meaningful.slice(-10)) {
        console.log(`  [${e.elapsed}s ${e.type}] ${e.text}`);
    }

    console.log(`\nOutput: ${dir}`);
} catch (e) {
    console.error(`Error: ${e.message}`);
    process.exit(1);
} finally {
    if (browser) await cleanup(browser);
}
