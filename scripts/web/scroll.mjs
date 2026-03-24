#!/usr/bin/env node
/**
 * DC3 Web — Song list scroll test with per-scroll screenshots.
 *
 * Usage:
 *   node scripts/web/scroll.mjs [--scrolls 10] [--out DIR] [--port 8420] [--verbose]
 */

import {
    parseArgs, waitForServer, launchBrowser, createCapture,
    navigateTo, pressKey, outputDir, screenshot, saveLogs, cleanup,
} from './lib/core.mjs';

const opts = parseArgs({
    port:    { type: 'number', default: 8420 },
    scrolls: { type: 'number', default: 10 },
    out:     { type: 'string' },
    verbose: { type: 'flag' },
});

let browser;
try {
    await waitForServer(opts.port);
    const { browser: b, page } = await launchBrowser(opts.port);
    browser = b;

    const cap = createCapture(page, { verbose: opts.verbose });
    await navigateTo(page, cap, 'song_select_screen');
    await new Promise(r => setTimeout(r, 3000));

    const dir = outputDir('scroll', opts.out);
    await screenshot(page, dir, '00_initial');

    console.log(`\n=== SCROLL TEST: ${opts.scrolls} scrolls ===\n`);

    for (let i = 0; i < opts.scrolls; i++) {
        await pressKey(page, 'ArrowDown');
        await new Promise(r => setTimeout(r, 1000));
        const name = String(i + 1).padStart(2, '0') + '_after_down';
        await screenshot(page, dir, name);
    }

    saveLogs(cap.logs, dir);
    console.log(`\nDone. ${opts.scrolls + 1} screenshots in ${dir}`);
} catch (e) {
    console.error(`Error: ${e.message}`);
    process.exit(1);
} finally {
    if (browser) await cleanup(browser);
}
