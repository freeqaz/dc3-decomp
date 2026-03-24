#!/usr/bin/env node
/**
 * DC3 Web — Take a screenshot.
 *
 * Navigates to song_select and saves a PNG. The default command for agents.
 *
 * Usage:
 *   node scripts/web/screenshot.mjs [--out DIR] [--port 8420] [--verbose]
 */

import {
    parseArgs, waitForServer, launchBrowser, createCapture,
    navigateTo, outputDir, screenshot, saveLogs, cleanup,
} from './lib/core.mjs';

const opts = parseArgs({
    port:    { type: 'number', default: 8420 },
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

    // Let song list populate
    await new Promise(r => setTimeout(r, 3000));

    const dir = outputDir('screenshot', opts.out);
    await screenshot(page, dir, 'screenshot');
    saveLogs(cap.logs, dir);

    console.log(`\nDone. Output: ${dir}`);
} catch (e) {
    console.error(`Error: ${e.message}`);
    process.exit(1);
} finally {
    if (browser) await cleanup(browser);
}
