#!/usr/bin/env node
// Print the song-select order as the web build presents it: boot, reach
// song_select_screen, press ArrowDown N times and record which song's preview
// the page requests after each press. Index i is what pose-gameplay.mjs
// --song-index i will pick. A preview can lag its press, so a name printed
// twice (3 and 4 both "thehustle") means one index belongs to a neighbour;
// the game's own "DC3 HamNavList: select ... sym='<song>'" console line in
// a pose-gameplay run is the ground truth.
//
//   node scripts/web/song-order.mjs --port 8434 [--count 70]
import { parseArgs, waitForServer, launchBrowser, createCapture, navigateTo, pressKey, cleanup } from './lib/core.mjs';

const opts = parseArgs({ port: { type: 'number', default: 8420 }, count: { type: 'number', default: 70 } });
await waitForServer(opts.port);
const { browser, page } = await launchBrowser(opts.port);
const cap = createCapture(page);
const previews = [];
page.on('request', (r) => {
    const m = r.url().match(/\/api\/file\/songs\/([a-z0-9_]+)\/[a-z0-9_]+_prev\./);
    if (m) previews.push({ t: Date.now(), song: m[1] });
});
try {
    await navigateTo(page, cap, 'song_select_screen');
    await new Promise((r) => setTimeout(r, 3000));
    const order = [];
    const pick = () => (previews.length ? previews[previews.length - 1].song : null);
    order.push(pick());
    for (let i = 1; i <= opts.count; i++) {
        await pressKey(page, 'ArrowDown');
        await new Promise((r) => setTimeout(r, 900));
        order.push(pick());
    }
    order.forEach((s, i) => console.log(`${i}\t${s}`));
} finally {
    await cleanup(browser);
}
