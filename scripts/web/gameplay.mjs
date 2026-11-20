#!/usr/bin/env node
/**
 * DC3 Web — Song loading / gameplay diagnosis.
 *
 * Navigates to song_select, selects a song, monitors loading milestones,
 * detects hangs via console silence.
 *
 * Exit: 0 the song loaded, 1 error, 2 hang, 3 the song did not load in time.
 *
 * Usage:
 *   node scripts/web/gameplay.mjs [--song-index 3] [--timeout 90]
 *     [--hang-timeout 15] [--out DIR] [--port 8420] [--verbose]
 */

import {
    parseArgs, waitForServer, launchBrowser, createCapture,
    navigateTo, selectSong, pressKey,
    outputDir, screenshot, saveLogs, cleanup,
} from './lib/core.mjs';

const opts = parseArgs({
    port:           { type: 'number', default: 8420 },
    'song-index':   { type: 'number', default: 3 },
    timeout:        { type: 'number', default: 90 },
    'hang-timeout': { type: 'number', default: 15 },
    out:            { type: 'string' },
    verbose:        { type: 'flag' },
});

const MILESTONES = [
    'attract_screen', 'main_screen', 'game_screen',
    'PollForLoading', 'FileMerger', 'song merger',
    'IsLoaded', 'DONE (state 4)', 'StartIntro',
];

let browser;
try {
    await waitForServer(opts.port);
    const { browser: b, page } = await launchBrowser(opts.port);
    browser = b;

    const cap = createCapture(page, { verbose: opts.verbose });
    const dir = outputDir('gameplay', opts.out);

    // Track milestones
    const reached = {};
    const checkMilestones = () => {
        for (const m of MILESTONES) {
            if (!reached[m]) {
                const hit = cap.logs.find(l => l.text.includes(m));
                if (hit) {
                    reached[m] = hit.elapsed;
                    if (!opts.verbose) console.log(`[milestone] "${m}" at ${hit.elapsed}s`);
                }
            }
        }
    };

    await navigateTo(page, cap, 'song_select_screen');
    await new Promise(r => setTimeout(r, 1500));
    await screenshot(page, dir, 'song_select');

    // Select a song
    await selectSong(page, cap, { scrolls: opts['song-index'] });
    console.log('[gameplay] Song selected, monitoring loading...');

    // Fire confirmation keys in the background (non-blocking) until the game
    // screen is entered. A fixed count is not enough: a browser profile that
    // has never played gets the first-run tutorial (five screens) between the
    // pick and the song, and each screen waits for a press. Five presses ended
    // inside the tutorial, where the console goes quiet, and that silence was
    // reported as a hang.
    const gameEntered = () => cap.logs.some(l => l.text.includes("Screen 'game_screen' Enter"));
    let confirming = true;
    let confirmPresses = 0;
    const confirmKeys = async () => {
        while (confirming && !gameEntered()) {
            await new Promise(r => setTimeout(r, 2000));
            if (!confirming || gameEntered()) break;
            await pressKey(page, 'Enter');
            confirmPresses++;
        }
    };
    confirmKeys();

    // Monitor for loading completion or hang
    let hangDetected = false;
    let doneAt = null;
    const deadline = Date.now() + opts.timeout * 1000;

    while (Date.now() < deadline) {
        await new Promise(r => setTimeout(r, 1000));
        checkMilestones();

        // Hang detection
        if (cap.logs.length > 20 && cap.silenceMs() > opts['hang-timeout'] * 1000) {
            console.log(`[gameplay] HANG: no output for ${(cap.silenceMs()/1000).toFixed(1)}s`);
            hangDetected = true;
            await screenshot(page, dir, 'gameplay_hang');
            break;
        }

        // Loading complete — wait for gameplay render
        if (reached['DONE (state 4)'] && !doneAt) {
            doneAt = Date.now();
            console.log('[gameplay] Song loading completed!');
        }
        if (doneAt && Date.now() - doneAt > 10000) {
            await screenshot(page, dir, 'gameplay');
            break;
        }
    }

    confirming = false;
    saveLogs(cap.logs, dir);

    // Report
    console.log('\n=== Results ===');
    console.log(`Logs: ${cap.logs.length}, Errors: ${cap.errors.length}, Hang: ${hangDetected}`);
    console.log(`Game screen entered: ${gameEntered()} (${confirmPresses} confirm presses)`);
    console.log('\nMilestones:');
    for (const m of MILESTONES) {
        console.log(`  ${reached[m] ? `[${reached[m]}s]` : '[ -- ]'} ${m}`);
    }
    console.log(`\nOutput: ${dir}`);

    // Exit 0 only when the song finished loading: a run that timed out on
    // the way (no hang, no song) is not a pass.
    const loaded = !!reached['DONE (state 4)'];
    if (!hangDetected && !loaded) {
        console.log(`[gameplay] NOT LOADED: the song did not finish loading within ${opts.timeout}s`);
    }
    process.exit(hangDetected ? 2 : loaded ? 0 : 3);
} catch (e) {
    console.error(`Error: ${e.message}`);
    process.exit(1);
} finally {
    if (browser) await cleanup(browser);
}
