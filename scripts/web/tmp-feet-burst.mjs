#!/usr/bin/env node
// TEMP — feet-in-floor verification: navigate to gameplay, then burst-capture
// screenshots through the routine so dancer feet are visible at crouch beats.
import {
    parseArgs, waitForServer, launchBrowser, createCapture,
    navigateTo, selectSong, screenshot, saveLogs, cleanup,
} from './lib/core.mjs';

const opts = parseArgs({
    port:  { type: 'number', default: 8420 },
    out:   { type: 'string', default: '/tmp/dc3-web-feet-burst' },
    shots: { type: 'number', default: 30 },
    'shot-interval': { type: 'number', default: 3 },
    'warmup': { type: 'number', default: 25 },
});

let browser;
try {
    await waitForServer(opts.port);
    const { browser: b, page } = await launchBrowser(opts.port);
    browser = b;
    const cap = createCapture(page, { verbose: false });

    await navigateTo(page, cap, 'song_select_screen');
    await new Promise(r => setTimeout(r, 1500));
    await selectSong(page, cap, { scrolls: 3 });

    // Wait for gameplay (Game::Poll songMs lines appear once the song runs)
    const t0 = Date.now();
    let playing = false;
    while (Date.now() - t0 < 90000) {
        if (cap.logs.some(l => l.text.includes('Game::Poll'))) { playing = true; break; }
        await new Promise(r => setTimeout(r, 1000));
    }
    if (!playing) { console.log('never reached gameplay'); process.exit(1); }
    console.log('gameplay reached; warmup', opts.warmup, 's');
    await new Promise(r => setTimeout(r, opts.warmup * 1000));

    for (let i = 1; i <= opts.shots; i++) {
        const last = [...cap.logs].reverse().find(l => l.text.includes('songMs='));
        const ms = last ? (last.text.match(/songMs=([0-9.]+)/) || [])[1] : '?';
        await screenshot(page, opts.out, `shot${String(i).padStart(2, '0')}-songMs${ms}`);
        await new Promise(r => setTimeout(r, opts['shot-interval'] * 1000));
    }
    saveLogs(cap.logs, opts.out);
    console.log('done:', opts.out);
} finally {
    await cleanup(browser);
}
