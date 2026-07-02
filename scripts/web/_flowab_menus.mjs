#!/usr/bin/env node
/**
 * One-off A/B harness for the PanelDir blanket-flow-activation re-test:
 * walk boot -> title -> main -> choose_mode -> song_select, screenshotting
 * each screen after a settle delay. Compare runs with default vs
 * MILO_NATIVE_FLOW_FILTER=none (via DC3_WEB_URL_EXTRA env bridge).
 *
 * Usage: node scripts/web/_flowab_menus.mjs --out /tmp/flowab-on
 */
import {
    parseArgs, waitForServer, launchBrowser, createCapture,
    navigateTo, outputDir, screenshot, saveLogs, cleanup,
} from './lib/core.mjs';

const opts = parseArgs({
    port: { type: 'number', default: 8420 },
    out:  { type: 'string' },
    verbose: { type: 'flag' },
});

const STOPS = [
    // [navigateTo target, settleMs, shotName]
    ['title_screen',       4000, 'title'],
    ['main_screen',        5000, 'main'],
    ['choose_mode_screen', 5000, 'choose_mode'],
    ['song_select_screen', 6000, 'song_select'],
];

let browser;
try {
    await waitForServer(opts.port);
    const { browser: b, page } = await launchBrowser(opts.port);
    browser = b;
    const cap = createCapture(page, { verbose: opts.verbose });
    const dir = outputDir('flowab', opts.out);

    for (const [target, settle, name] of STOPS) {
        await navigateTo(page, cap, target);
        await new Promise(r => setTimeout(r, settle));
        await screenshot(page, dir, name);
    }
    saveLogs(cap.logs, dir);
    console.log(`DONE ${dir}`);
} catch (e) {
    console.error('FAIL:', e.message);
    process.exitCode = 1;
} finally {
    await cleanup(browser);
}
