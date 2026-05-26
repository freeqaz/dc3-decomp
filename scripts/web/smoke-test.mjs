#!/usr/bin/env node
/**
 * DC3 Web — Boot smoke test.
 *
 * Regression guard for the AppLabel WASM call_indirect vtable crash
 * (filed 2026-03-22, fixed by PR #217 2026-04-09, validated 2026-05-26).
 *
 * Boots to main_screen and asserts:
 *   1. MainMenuProvider populates items — proves the
 *      `dynamic_cast<AppLabel*>(uiLabel)` -> `SetTextToken()` path in
 *      `src/lazer/meta_ham/MainMenuProvider.cpp::Text()` executes successfully.
 *   2. No `pageerror` events fire (WASM/JS exception).
 *   3. No `function signature mismatch` / `call_indirect` strings in console
 *      output (defense in depth — the WASM trap signature).
 *
 * Exits 0 on success, 1 on failure.
 *
 * Usage:
 *   # Start server first
 *   python3 native/web/server.py --port 8420 &
 *
 *   node scripts/web/smoke-test.mjs [--port 8420] [--verbose]
 *   npm run web:smoke-test
 *
 * See docs/native/TODO.md §8.6 for context.
 */

import {
    parseArgs, waitForServer, launchBrowser, createCapture,
    navigateTo, outputDir, saveLogs, cleanup,
} from './lib/core.mjs';

const opts = parseArgs({
    port:    { type: 'number', default: 8420 },
    out:     { type: 'string' },
    verbose: { type: 'flag' },
});

let browser;
const failures = [];

try {
    await waitForServer(opts.port);
    const { browser: b, page } = await launchBrowser(opts.port);
    browser = b;

    const cap = createCapture(page, { verbose: opts.verbose });

    // Navigate boot -> main_screen. AppLabel virtuals fire here when
    // MainMenuProvider::Text() does dynamic_cast<AppLabel*>(uiLabel).
    await navigateTo(page, cap, 'main_screen');

    // Give main_screen a few frames so MainMenuProvider::UpdateList runs.
    await new Promise(r => setTimeout(r, 2000));

    const dir = outputDir('smoke-test', opts.out);
    saveLogs(cap.logs, dir);

    // 1. MainMenuProvider must have populated items. The diagnostic printf
    //    in UpdateList() fires only after the (dynamic_cast + virtual call)
    //    path that originally triggered the WASM call_indirect trap.
    const mainMenuPopulated = cap.logs.some(l =>
        l.text.includes('DC3 MainMenuProvider:') && /\d+ items/.test(l.text)
    );
    if (!mainMenuPopulated) {
        failures.push('MainMenuProvider never populated items at main_screen');
    }

    // 2. No WASM/JS exceptions during navigation. The original AppLabel bug
    //    surfaced as a pageerror; any pageerror is a regression worth flagging.
    for (const err of cap.errors) {
        failures.push(`pageerror: ${err}`);
        if (/function signature mismatch|call_indirect/i.test(err)) {
            failures.push('  ^ AppLabel-class WASM vtable regression suspected');
            failures.push('  ^ see docs/native/TODO.md §8.6 and PR #217');
        }
    }

    // 3. Defense-in-depth: scan console logs for the WASM trap signatures
    //    even if pageerror didn't fire.
    const sigMatch = cap.logs.find(l =>
        /function signature mismatch|call_indirect type/i.test(l.text)
    );
    if (sigMatch) {
        failures.push(`WASM trap signature in console: ${sigMatch.text}`);
        failures.push('  ^ AppLabel-class regression suspected — see TODO.md §8.6');
    }

    console.log('\n=== Smoke test ===');
    if (failures.length === 0) {
        console.log('PASS — main_screen reached, MainMenuProvider populated, no pageerror');
    } else {
        console.log('FAIL');
        for (const f of failures) console.log(`  - ${f}`);
    }
    console.log(`Logs: ${dir}`);

    process.exit(failures.length === 0 ? 0 : 1);
} catch (e) {
    console.error(`Error: ${e.message}`);
    process.exit(1);
} finally {
    if (browser) await cleanup(browser);
}
