#!/usr/bin/env node
// W-9: a web run's MILO_NOTIFY lines reach the browser's devtools console, and
// only there. Boots the player page to its first screen and checks:
//   * the devtools console carries the boot notify a desktop run prints
//     ("Resetting macro kRockCentralConnectionTimeout ...", ui/challenges/
//     challenges.dta line 4) as a `MILO_NOTIFY:` console.warn line;
//   * the player page keeps no console lines (the tap bypasses Module.print);
//   * ?dev's own console (Module.print/printErr, capped at 500) carries no
//     MILO_ tap line either -- devtools is the sink, not the page;
//   * the notifies a desktop run (DC3_TAG_MODALS=1) prints on the way to the
//     title -- "needs localization", missing glyphs -- are there too;
//   * ?env=DC3_TAG_MODALS=0 turns it off.
// Exit 1 on any failure. A build without the web tap fails the first check.
//
//   node scripts/web/notify-console.mjs --port 8441
import { chromium } from 'playwright';
import { writeFileSync } from 'fs';
import { parseArgs, waitForServer } from './lib/core.mjs';

const opts = parseArgs({ port: { type: 'number', default: 8420 }, dump: { type: 'string', default: '' } });
await waitForServer(opts.port);
const browser = await chromium.launch({
    headless: !process.env.DISPLAY,
    args: ['--no-sandbox', '--enable-unsafe-webgpu', '--use-angle=vulkan', '--ozone-platform=x11',
        '--enable-features=Vulkan,VulkanFromANGLE,WebAssemblyJSPromiseIntegration', '--mute-audio'],
});
let failures = 0;
const check = (cond, msg) => { console.log(`${cond ? 'ok  ' : 'FAIL'} ${msg}`); if (!cond) failures++; };
const until = async (pred, ms) => { const d = Date.now() + ms; while (Date.now() < d) { if (await pred()) return true; await new Promise((r) => setTimeout(r, 250)); } return false; };
const BOOT_NOTIFY = 'MILO_NOTIFY: Resetting macro kRockCentralConnectionTimeout';

async function run(query, { toTitle = false } = {}) {
    const ctx = await browser.newContext({ viewport: { width: 1280, height: 720 } });
    const page = await ctx.newPage();
    const logs = [];
    page.on('console', (m) => logs.push({ type: m.type(), text: m.text() }));
    await page.goto(`http://127.0.0.1:${opts.port}/${query}`, { waitUntil: 'domcontentloaded' });
    const up = await until(() => logs.some((l) => l.text.includes("will enter '")), 120000);
    // The route a desktop boot takes: attract -> autosave warning -> title.
    // Loading those screens raises the localization and missing-glyph notifies.
    if (toTitle && up) {
        await page.locator('canvas').first().click().catch(() => {});
        await until(async () => {
            if (logs.some((l) => l.text.includes("Screen 'title_screen' Enter"))) return true;
            await page.keyboard.press('Enter').catch(() => {});
            return false;
        }, 90000);
    }
    await new Promise((r) => setTimeout(r, 2000));
    const pageLines = await page.evaluate(() => Array.from(document.getElementById('console').children, (d) => d.textContent));
    await ctx.close();
    return { up, logs, pageLines };
}

try {
    {
        const { up, logs, pageLines } = await run('?pose=off', { toTitle: true });
        check(up, 'player page reached a screen');
        check(logs.some((l) => l.text.includes("Screen 'title_screen' Enter")), 'player page reached title_screen');
        const tap = logs.filter((l) => l.text.startsWith('MILO_'));
        const boot = logs.find((l) => l.text.startsWith(BOOT_NOTIFY));
        check(!!boot, `devtools console carries the boot notify (${tap.length} MILO_ lines, ` +
            `${tap.filter((l) => l.text.startsWith('MILO_NOTIFY:')).length} NOTIFY)`);
        check(!boot || boot.type === 'warning', `as console.warn (${boot ? boot.type : '-'})`);
        // Raised on the way to the title by a desktop run of the same route
        // (DC3_TAG_MODALS=1): a localization notify and a missing-glyph one.
        check(tap.some((l) => /^MILO_NOTIFY: ".*" needs localization/.test(l.text)),
            'devtools console carries the "needs localization" notifies a desktop run prints');
        check(tap.some((l) => /^MILO_NOTIFY: .*missing from .*\.font/.test(l.text)),
            'devtools console carries the missing-glyph notifies a desktop run prints');
        check(pageLines.length === 0, `player page keeps no console lines (${pageLines.length})`);
        for (const l of tap.slice(0, 5)) console.log(`     ${l.type}: ${l.text.slice(0, 140)}`);
        // --dump FILE: every console line of this run, to compare with a desktop log.
        if (opts.dump) writeFileSync(opts.dump, logs.map((l) => l.text).join('\n') + '\n');
    }
    {
        const { up, logs, pageLines } = await run('?dev&pose=off');
        check(up, '?dev reached a screen');
        check(logs.some((l) => l.text.startsWith(BOOT_NOTIFY)), '?dev: devtools console carries the boot notify');
        const inPage = pageLines.filter((t) => t.startsWith('MILO_'));
        check(pageLines.length > 0 && inPage.length === 0,
            `?dev page console has engine lines but no tap lines (${pageLines.length} lines, ${inPage.length} MILO_)`);
    }
    {
        const { up, logs } = await run('?pose=off&env=DC3_TAG_MODALS=0');
        check(up, 'DC3_TAG_MODALS=0 page reached a screen');
        const tap = logs.filter((l) => l.text.startsWith('MILO_NOTIFY:') || l.text.startsWith('MILO_WARN:'));
        check(tap.length === 0, `DC3_TAG_MODALS=0 silences the tap (${tap.length} lines)`);
    }
} finally {
    await browser.close();
}
console.log(failures ? `NOTIFY-CONSOLE: ${failures} FAILED` : 'NOTIFY-CONSOLE: PASS');
process.exit(failures ? 1 : 0);
