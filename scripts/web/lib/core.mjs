/**
 * DC3 Web Test — shared core module.
 *
 * Every command script (`screenshot.mjs`, `scroll.mjs`, …) imports from here
 * instead of duplicating browser launch, console capture, navigation, etc.
 */

import { chromium } from 'playwright';
import { mkdirSync, writeFileSync } from 'fs';
import { resolve } from 'path';
import http from 'http';

// ---------------------------------------------------------------------------
// CLI arg parsing
// ---------------------------------------------------------------------------

/**
 * Parse process.argv against a spec.
 *
 *   const opts = parseArgs({
 *     port:    { type: 'number', default: 8420 },
 *     verbose: { type: 'flag' },
 *     out:     { type: 'string' },
 *   });
 */
export function parseArgs(spec) {
    const argv = process.argv.slice(2);
    const result = {};

    for (const [name, def] of Object.entries(spec)) {
        const flag = `--${name}`;
        const idx = argv.indexOf(flag);

        if (def.type === 'flag') {
            result[name] = idx !== -1;
        } else if (idx !== -1 && idx + 1 < argv.length) {
            const raw = argv[idx + 1];
            result[name] = def.type === 'number' ? parseInt(raw, 10) : raw;
        } else {
            result[name] = def.default;
        }
    }

    return result;
}

// ---------------------------------------------------------------------------
// Server
// ---------------------------------------------------------------------------

/** Poll /api/health until the server is ready. */
export function waitForServer(port, timeoutMs = 15000) {
    return new Promise((resolve, reject) => {
        const deadline = Date.now() + timeoutMs;
        const check = () => {
            http.get(`http://127.0.0.1:${port}/api/health`, (res) => {
                if (res.statusCode === 200) return resolve();
                retry();
            }).on('error', retry);
        };
        const retry = () => {
            if (Date.now() > deadline)
                return reject(new Error(`Server not ready after ${timeoutMs}ms`));
            setTimeout(check, 300);
        };
        check();
    });
}

// ---------------------------------------------------------------------------
// Browser
// ---------------------------------------------------------------------------

/** Launch Chromium with WebGPU flags. Returns { browser, page }. */
export async function launchBrowser(port) {
    const browser = await chromium.launch({
        headless: !process.env.DISPLAY,
        args: [
            '--no-sandbox',
            '--enable-unsafe-webgpu',
            '--use-angle=vulkan',
            '--enable-features=Vulkan,VulkanFromANGLE',
            '--ozone-platform=x11',
            '--disable-extensions',
            '--disable-background-networking',
            '--disable-default-apps',
            '--disable-sync',
            '--mute-audio',
        ],
    });

    const context = await browser.newContext({
        viewport: { width: 1280, height: 720 },
    });
    const page = await context.newPage();

    await page.goto(`http://127.0.0.1:${port}`, {
        waitUntil: 'domcontentloaded',
        timeout: 30000,
    });

    return { browser, page };
}

// ---------------------------------------------------------------------------
// Console capture
// ---------------------------------------------------------------------------

/**
 * Wire page.on('console') + pageerror + crash.
 * Returns { logs, errors, waitForLog(text, ms), elapsed() }.
 */
export function createCapture(page, { verbose = false, prefix = 'dc3' } = {}) {
    const startTime = Date.now();
    const logs = [];      // { elapsed, type, text }
    const errors = [];    // string[]
    let lastLogTime = Date.now();

    const elapsed = () => ((Date.now() - startTime) / 1000).toFixed(2);

    page.on('console', (msg) => {
        const text = msg.text();
        const entry = { elapsed: elapsed(), type: msg.type(), text };
        logs.push(entry);
        if (text.trim().length > 0) lastLogTime = Date.now();

        if (verbose || msg.type() === 'error') {
            console.log(`  [${entry.elapsed}s ${msg.type()}] ${text}`);
        }
    });

    page.on('pageerror', (err) => {
        const text = err.message || String(err);
        errors.push(text);
        console.log(`  [PAGE_ERROR] ${text}`);
    });

    page.on('crash', () => {
        errors.push('Page crashed');
        console.log('  [CRASH] Page crashed!');
    });

    /** Poll logs for a substring. Resolves true/false. */
    function waitForLog(text, timeoutMs = 30000) {
        return new Promise((resolve) => {
            const deadline = Date.now() + timeoutMs;
            const check = () => {
                if (logs.some(l => l.text.includes(text))) return resolve(true);
                if (Date.now() > deadline) return resolve(false);
                setTimeout(check, 200);
            };
            check();
        });
    }

    /** Milliseconds since last non-empty log line. */
    function silenceMs() {
        return Date.now() - lastLogTime;
    }

    return { logs, errors, waitForLog, elapsed, silenceMs };
}

// ---------------------------------------------------------------------------
// Input helpers
// ---------------------------------------------------------------------------

/** Press a key with hold time and inter-key delay. 3s timeout guard. */
export async function pressKey(page, key, holdMs = 150) {
    try {
        await Promise.race([
            (async () => {
                await page.keyboard.down(key);
                await new Promise(r => setTimeout(r, holdMs));
                await page.keyboard.up(key);
            })(),
            new Promise(r => setTimeout(r, 3000)),
        ]);
        await new Promise(r => setTimeout(r, 200));
    } catch { /* page frozen — swallow */ }
}

/** Wait for `will enter '<name>'` in logs. */
export async function waitForScreen(capture, name, ms = 30000) {
    return capture.waitForLog(`will enter '${name}'`, ms);
}

/**
 * Full navigation: attract → title → main → choose_mode → song_select.
 * `target` is the final screen to reach (default: song_select_screen).
 * Clicks the canvas first for keyboard focus.
 */
export async function navigateTo(page, capture, target = 'song_select_screen') {
    const screens = [
        // [waitFor, key, label]
        ['attract_screen',      null,    'engine boot'],
        ['title_screen',        ' ',     'dismiss attract'],
        ['main_screen',         ' ',     'skip title'],
        ['choose_mode_screen',  'Enter', 'main → choose_mode'],
        ['song_select_screen',  'Enter', 'choose_mode → song_select'],
    ];

    // Wait for engine to boot
    const booted = await capture.waitForLog('attract_screen', 45000);
    if (!booted) throw new Error('Engine never booted (no attract_screen)');
    await new Promise(r => setTimeout(r, 3000));
    // Focus canvas via JS — page.click('canvas') fails when video overlays block it
    await page.evaluate(() => document.getElementById('dc3-canvas').focus());

    for (const [screen, key, label] of screens) {
        if (screen === 'attract_screen') continue; // already waited

        console.log(`[nav] ${label}`);
        await pressKey(page, key);

        if (!await waitForScreen(capture, screen, 30000)) {
            // Retry once — screens can be slow on first load
            console.log(`[nav] retrying: ${label}`);
            await pressKey(page, key);
            if (!await waitForScreen(capture, screen, 30000)) {
                throw new Error(`Never reached ${screen}`);
            }
        }

        // Let screen stabilize
        await new Promise(r => setTimeout(r, 1500));

        if (screen === target) return;
    }
}

/** Scroll down N times on song_select, then press Enter. */
export async function selectSong(page, capture, { scrolls = 3 } = {}) {
    for (let i = 0; i < scrolls; i++) {
        await pressKey(page, 'ArrowDown');
        await new Promise(r => setTimeout(r, 300));
    }
    await pressKey(page, 'Enter');
}

// ---------------------------------------------------------------------------
// Output helpers
// ---------------------------------------------------------------------------

/** Resolve output directory. Auto-generates timestamped path if none given. */
export function outputDir(name, explicit) {
    if (explicit) {
        mkdirSync(explicit, { recursive: true });
        return explicit;
    }
    const ts = new Date().toISOString().replace(/[:.]/g, '-').slice(0, 19);
    const dir = `/tmp/dc3-web/${name}-${ts}`;
    mkdirSync(dir, { recursive: true });
    return dir;
}

/** Take a screenshot with a 5s timeout guard. Returns true on success. */
export async function screenshot(page, dir, name) {
    const path = resolve(dir, `${name}.png`);
    try {
        await Promise.race([
            page.screenshot({ path }),
            new Promise((_, rej) => setTimeout(() => rej(new Error('timeout')), 5000)),
        ]);
        console.log(`[screenshot] ${path}`);
        return true;
    } catch {
        console.log(`[screenshot] FAILED: ${path}`);
        return false;
    }
}

/** Write logs as JSONL. */
export function saveLogs(logs, dir) {
    const path = resolve(dir, 'console.jsonl');
    writeFileSync(path, logs.map(e => JSON.stringify(e)).join('\n') + '\n');
    console.log(`[logs] ${path}`);
}

/** Close browser with 3s timeout. */
export async function cleanup(browser) {
    try {
        await Promise.race([
            browser.close(),
            new Promise(r => setTimeout(r, 3000)),
        ]);
    } catch { /* swallow */ }
}
