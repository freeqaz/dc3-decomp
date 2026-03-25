#!/usr/bin/env node
/**
 * DC3 Web — Audio receiver probe.
 *
 * Navigates to gameplay, then dumps the browser audio mixer state from the
 * instrumented web build at key milestones.
 *
 * Usage:
 *   node scripts/web/audio-probe.mjs [--song-index 3] [--timeout 90]
 *     [--hang-timeout 15] [--post-game-seconds 10] [--port 8420]
 *     [--out DIR] [--verbose]
 */

import {
    parseArgs, waitForServer, launchBrowser, createCapture,
    navigateTo, selectSong, pressKey,
    outputDir, screenshot, saveLogs, cleanup,
} from './lib/core.mjs';

const opts = parseArgs({
    port:                { type: 'number', default: 8420 },
    'song-index':        { type: 'number', default: 3 },
    timeout:             { type: 'number', default: 90 },
    'hang-timeout':      { type: 'number', default: 15 },
    'post-game-seconds': { type: 'number', default: 10 },
    out:                 { type: 'string' },
    verbose:             { type: 'flag' },
});

function hasLog(cap, text) {
    return cap.logs.some(l => l.text.includes(text));
}

async function dumpAudio(page, cap, label) {
    console.log(`[audio] dumping "${label}"`);
    const state = await page.evaluate((probeLabel) => {
        const audio = window._dc3Audio || null;
        const info = {
            label: probeLabel,
            started: !!audio?.started,
            ctxState: audio?.ctx?.state || 'missing',
            hasWorklet: !!audio?.worklet,
            hasSab: !!audio?.sab,
        };
        console.log(`DC3 AUDIO PROBE ${JSON.stringify(info)}`);
        if (typeof window.dc3AudioStats === 'function') {
            window.dc3AudioStats();
        } else {
            console.log('DC3 AUDIO PROBE dc3AudioStats unavailable');
        }
        return info;
    }, label);
    await new Promise(r => setTimeout(r, 500));
    const hits = cap.logs.filter(l => l.text.includes('AudioDevice: active source count=')).length;
    console.log(`[audio] "${label}" complete, source-dump-count=${hits}, ctx=${state.ctxState}`);
}

let browser;
try {
    await waitForServer(opts.port);
    const { browser: b, page } = await launchBrowser(opts.port);
    browser = b;

    const cap = createCapture(page, { verbose: opts.verbose });
    const dir = outputDir('audio-probe', opts.out);

    await navigateTo(page, cap, 'song_select_screen');
    await new Promise(r => setTimeout(r, 1500));
    await screenshot(page, dir, 'song_select');
    await dumpAudio(page, cap, 'song_select');

    await selectSong(page, cap, { scrolls: opts['song-index'] });
    console.log('[audio] Song selected, monitoring gameplay transition...');

    const confirmKeys = async () => {
        for (let i = 0; i < 5; i++) {
            await new Promise(r => setTimeout(r, 2000));
            await pressKey(page, 'Enter');
        }
    };
    confirmKeys();

    let hangDetected = false;
    let gameEnterAt = null;
    let dumpedGameEnter = false;
    let dumpedLoaded = false;
    let dumpedLate = false;
    const deadline = Date.now() + opts.timeout * 1000;

    while (Date.now() < deadline) {
        await new Promise(r => setTimeout(r, 500));

        if (!dumpedGameEnter && hasLog(cap, "will enter 'game_screen'")) {
            dumpedGameEnter = true;
            gameEnterAt = Date.now();
            await screenshot(page, dir, 'game_screen');
            await dumpAudio(page, cap, 'game_screen_enter');
        }

        if (!dumpedLoaded && hasLog(cap, 'DONE (state 4)')) {
            dumpedLoaded = true;
            await dumpAudio(page, cap, 'loading_done');
        }

        if (gameEnterAt && !dumpedLate &&
            Date.now() - gameEnterAt > opts['post-game-seconds'] * 1000) {
            dumpedLate = true;
            await screenshot(page, dir, 'gameplay_late');
            await dumpAudio(page, cap, `gameplay_plus_${opts['post-game-seconds']}s`);
            break;
        }

        if (cap.logs.length > 20 && cap.silenceMs() > opts['hang-timeout'] * 1000) {
            console.log(`[audio] HANG: no output for ${(cap.silenceMs() / 1000).toFixed(1)}s`);
            hangDetected = true;
            await screenshot(page, dir, 'audio_hang');
            await dumpAudio(page, cap, 'hang');
            break;
        }
    }

    saveLogs(cap.logs, dir);

    console.log('\n=== Audio Probe Results ===');
    console.log(`Logs: ${cap.logs.length}, Errors: ${cap.errors.length}, Hang: ${hangDetected}`);
    console.log(`Output: ${dir}`);

    process.exit(hangDetected ? 2 : 0);
} catch (e) {
    console.error(`Error: ${e.message}`);
    process.exit(1);
} finally {
    if (browser) await cleanup(browser);
}
