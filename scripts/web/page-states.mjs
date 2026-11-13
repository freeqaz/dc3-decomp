#!/usr/bin/env node
// Screenshot the page's own states (W-8), no gameplay: the player page while
// loading and once the first screen is up, its error card (no WebGPU), and the
// ?dev harness. Fails (exit 1) if the player page shows any dev-harness element
// or the dev page hides one -- the check that the default page stays
// player-facing.
//
//   node scripts/web/page-states.mjs --port 8434 [--out DIR]
import { chromium } from 'playwright';
import { mkdirSync } from 'fs';
import { parseArgs, waitForServer } from './lib/core.mjs';

const opts = parseArgs({ port: { type: 'number', default: 8420 }, out: { type: 'string', default: '/tmp/dc3-web/page-states' } });
mkdirSync(opts.out, { recursive: true });
await waitForServer(opts.port);
const browser = await chromium.launch({
    headless: !process.env.DISPLAY,
    args: ['--no-sandbox', '--enable-unsafe-webgpu', '--use-angle=vulkan', '--ozone-platform=x11',
        '--enable-features=Vulkan,VulkanFromANGLE,WebAssemblyJSPromiseIntegration', '--mute-audio'],
});
const DEV_IDS = ['status', 'controls', 'toolbar', 'console'];
const visible = (page) => page.evaluate((ids) => Object.fromEntries(ids.concat(['loading']).map((id) => {
    const el = document.getElementById(id);
    if (!el) return [id, false];
    const cs = getComputedStyle(el);
    return [id, cs.display !== 'none' && cs.visibility !== 'hidden' && +cs.opacity > 0];
})), DEV_IDS);
let failures = 0;
const check = (cond, msg) => { console.log(`${cond ? 'ok  ' : 'FAIL'} ${msg}`); if (!cond) failures++; };

async function open(query, { noGpu = false } = {}) {
    const ctx = await browser.newContext({ viewport: { width: 1280, height: 720 } });
    const page = await ctx.newPage();
    if (noGpu) await page.addInitScript(() => Object.defineProperty(Navigator.prototype, 'gpu', { get: () => undefined }));
    const logs = [];
    page.on('console', (m) => logs.push(m.text()));
    await page.goto(`http://127.0.0.1:${opts.port}/${query}`, { waitUntil: 'domcontentloaded' });
    return { ctx, page, logs };
}
const until = async (pred, ms) => { const d = Date.now() + ms; while (Date.now() < d) { if (await pred()) return true; await new Promise((r) => setTimeout(r, 250)); } return false; };

try {
    // Player page: loading card, then the game once a screen is entered.
    {
        const { ctx, page, logs } = await open('?pose=off');
        await new Promise((r) => setTimeout(r, 1500));
        let v = await visible(page);
        await page.screenshot({ path: `${opts.out}/player-loading.png` });
        check(DEV_IDS.every((id) => !v[id]), `player page hides the dev harness while loading ${JSON.stringify(v)}`);
        const up = await until(() => logs.some((l) => l.includes("will enter '")), 120000);
        check(up, 'player page reached a screen');
        await new Promise((r) => setTimeout(r, 1000));
        v = await visible(page);
        check(!v.loading, 'loading card hidden once a screen is entered');
        check(DEV_IDS.every((id) => !v[id]), 'player page still hides the dev harness');
        const nLines = await page.evaluate(() => document.getElementById('console').childElementCount);
        check(nLines === 0, `player page keeps no console lines (${nLines})`);
        await page.screenshot({ path: `${opts.out}/player-running.png` });
        await ctx.close();
    }
    // Error card: no WebGPU.
    {
        const { ctx, page } = await open('?pose=off', { noGpu: true });
        await new Promise((r) => setTimeout(r, 1500));
        const v = await visible(page);
        const text = await page.evaluate(() => (document.getElementById('loading') || {}).innerText || '');
        check(v.loading && /WebGPU not available/.test(text), `error card shown without WebGPU: "${text.split('\n')[0]}"`);
        await page.screenshot({ path: `${opts.out}/player-no-webgpu.png` });
        await ctx.close();
    }
    // Developer harness.
    {
        const { ctx, page, logs } = await open('?dev&pose=off');
        await until(() => logs.some((l) => l.includes("will enter '")), 120000);
        await new Promise((r) => setTimeout(r, 1000));
        const v = await visible(page);
        check(DEV_IDS.every((id) => v[id]) && !v.loading, `?dev shows the harness ${JSON.stringify(v)}`);
        const nLines = await page.evaluate(() => document.getElementById('console').childElementCount);
        check(nLines > 0 && nLines <= 500, `?dev console has lines, capped at 500 (${nLines})`);
        await page.screenshot({ path: `${opts.out}/dev.png` });
        await ctx.close();
    }
} finally {
    await browser.close();
}
console.log(`${failures ? 'FAIL' : 'PASS'} page states (${opts.out})`);
process.exit(failures ? 1 : 0);
