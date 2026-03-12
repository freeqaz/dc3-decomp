#!/usr/bin/env node
/**
 * DC3 Web Port — Headless test runner + screenshot capture.
 *
 * Launches server.py, boots headless Chromium with WebGPU, captures all
 * console output, detects hangs/crashes, and takes canvas screenshots.
 *
 * Usage:
 *   node scripts/web/test.mjs [options]
 *
 * WebGPU requires a real display — run with xvfb-run on headless servers:
 *   xvfb-run -a node scripts/web/test.mjs [options]
 *
 * Options:
 *   --no-build       Skip WASM build step
 *   --timeout <sec>  Max seconds to wait for frames (default: 30)
 *   --frames <n>     Target frame count before screenshotting (default: 5)
 *   --port <n>       Server port (default: 8420)
 *   --headless       Force headless mode (no GPU rendering — white canvas)
 *   --keep           Keep server running after test (for manual inspection)
 */

import { chromium } from 'playwright';
import { spawn, execSync } from 'child_process';
import { existsSync, mkdirSync, writeFileSync } from 'fs';
import { dirname, resolve } from 'path';
import { fileURLToPath } from 'url';

const __dirname = dirname(fileURLToPath(import.meta.url));
const ROOT = resolve(__dirname, '../..');
const BUILD_DIR = resolve(ROOT, 'native/web/build');
const RESULTS_DIR = resolve(__dirname, 'results');

// ---------------------------------------------------------------------------
// CLI args
// ---------------------------------------------------------------------------

const args = process.argv.slice(2);
function flag(name) { return args.includes(`--${name}`); }
function opt(name, def) {
    const idx = args.indexOf(`--${name}`);
    return idx >= 0 && args[idx + 1] ? args[idx + 1] : def;
}

const NO_BUILD = flag('no-build');
const TIMEOUT = parseInt(opt('timeout', '30'), 10) * 1000;
const TARGET_FRAMES = parseInt(opt('frames', '5'), 10);
const PORT = parseInt(opt('port', '8420'), 10);
const HEADLESS = flag('headless');  // Default: headed (WebGPU needs real display)
const KEEP = flag('keep');

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

function timestamp() {
    return new Date().toISOString().replace(/[:.]/g, '-').slice(0, 19);
}

function waitForServer(port, timeoutMs = 10000) {
    const start = Date.now();
    return new Promise((resolve, reject) => {
        const check = () => {
            fetch(`http://127.0.0.1:${port}/api/health`)
                .then(r => { if (r.ok) resolve(); else throw new Error(r.status); })
                .catch(() => {
                    if (Date.now() - start > timeoutMs) {
                        reject(new Error(`Server not ready after ${timeoutMs}ms`));
                    } else {
                        setTimeout(check, 200);
                    }
                });
        };
        check();
    });
}

// ---------------------------------------------------------------------------
// Build
// ---------------------------------------------------------------------------

function build() {
    if (NO_BUILD) {
        console.log('[test] Skipping build (--no-build)');
        return;
    }
    console.log('[test] Building dc3-web...');
    try {
        execSync(
            'source ~/emsdk/emsdk_env.sh && cmake --build . --target dc3-web 2>&1',
            { cwd: BUILD_DIR, stdio: 'inherit', shell: '/bin/bash', timeout: 120000 }
        );
        console.log('[test] Build OK');
    } catch (e) {
        console.error('[test] Build FAILED');
        process.exit(1);
    }
}

// ---------------------------------------------------------------------------
// Server
// ---------------------------------------------------------------------------

function startServer() {
    console.log(`[test] Starting server on :${PORT}...`);
    const server = spawn('python3', ['-u',
        resolve(ROOT, 'native/web/server.py'),
        '--port', String(PORT),
    ], {
        stdio: ['ignore', 'pipe', 'pipe'],
        cwd: ROOT,
    });

    server.stdout.on('data', d => {
        for (const line of d.toString().split('\n').filter(Boolean)) {
            console.log(`[server] ${line}`);
        }
    });
    server.stderr.on('data', d => {
        for (const line of d.toString().split('\n').filter(Boolean)) {
            console.log(`[server] ${line}`);
        }
    });
    server.on('error', e => {
        console.error(`[server] spawn error: ${e.message}`);
    });
    server.on('exit', (code, signal) => {
        if (code !== null && code !== 0) {
            console.error(`[server] exited with code ${code}`);
        } else if (signal) {
            console.log(`[server] killed by ${signal}`);
        }
    });

    return server;
}

// ---------------------------------------------------------------------------
// Browser + test
// ---------------------------------------------------------------------------

async function runTest() {
    const runDir = resolve(RESULTS_DIR, timestamp());
    mkdirSync(runDir, { recursive: true });

    const consoleLog = [];
    let lastMsgTime = Date.now();
    let pageError = null;
    let wasmTrap = null;

    console.log(`[test] Launching ${HEADLESS ? 'headless' : 'headed'} Chromium...`);

    const browser = await chromium.launch({
        headless: HEADLESS,
        args: [
            '--enable-features=Vulkan,UseSkiaRenderer',
            '--enable-unsafe-webgpu',
            '--use-angle=vulkan',
            '--enable-gpu',
            '--no-sandbox',
        ],
    });

    const page = await browser.newPage({ viewport: { width: 1280, height: 800 } });

    // Capture all console output
    page.on('console', msg => {
        const entry = {
            type: msg.type(),
            time: Date.now(),
            text: msg.text(),
        };
        consoleLog.push(entry);
        lastMsgTime = Date.now();

        const prefix = msg.type() === 'error' ? '\x1b[31m[err]\x1b[0m' : '[log]';
        console.log(`  ${prefix} ${msg.text()}`);

        // Detect WASM traps
        if (msg.text().includes('function signature mismatch') ||
            msg.text().includes('unreachable') ||
            msg.text().includes('RuntimeError')) {
            wasmTrap = msg.text();
        }
    });

    page.on('pageerror', err => {
        pageError = err.message;
        console.error(`  \x1b[31m[PAGE ERROR]\x1b[0m ${err.message}`);
    });

    page.on('crash', () => {
        console.error('  \x1b[31m[CRASH]\x1b[0m Page crashed');
        pageError = 'Page crashed';
    });

    console.log(`[test] Navigating to http://127.0.0.1:${PORT}...`);
    await page.goto(`http://127.0.0.1:${PORT}`, { waitUntil: 'domcontentloaded' });

    // Helper: run async op with timeout (WASM can block main thread, making
    // page.evaluate/screenshot hang forever)
    function withTimeout(promise, ms, fallback) {
        return Promise.race([
            promise,
            new Promise(resolve => setTimeout(() => resolve(fallback), ms)),
        ]);
    }

    // Wait for target frames or timeout
    let result = 'TIMEOUT';
    let frames = 0;
    try {
        await page.waitForFunction(
            `window.dc3FrameCount >= ${TARGET_FRAMES}`,
            { timeout: TIMEOUT }
        );
        result = 'PASS';
        frames = TARGET_FRAMES;
    } catch {
        // Try to read frame count (may hang if WASM blocks main thread)
        frames = await withTimeout(
            page.evaluate('window.dc3FrameCount || 0').catch(() => 0),
            3000, 0
        );
        if (wasmTrap) {
            result = 'WASM_TRAP';
        } else if (pageError) {
            result = 'CRASH';
        } else if (frames > 0) {
            result = `PARTIAL (${frames}/${TARGET_FRAMES} frames)`;
        } else {
            const silentMs = Date.now() - lastMsgTime;
            result = silentMs > 5000 ? 'HANG' : 'TIMEOUT';
        }
    }

    // Write console log FIRST (doesn't need browser interaction)
    writeFileSync(
        resolve(runDir, 'console.jsonl'),
        consoleLog.map(e => JSON.stringify(e)).join('\n') + '\n'
    );

    // Write summary BEFORE screenshots (so we always have results even if
    // screenshot hangs because WASM blocked the main thread)
    const summary = {
        result,
        frames,
        targetFrames: TARGET_FRAMES,
        timeoutMs: TIMEOUT,
        wasmTrap,
        pageError,
        messageCount: consoleLog.length,
        lastMessages: consoleLog.slice(-10).map(e => e.text),
        timestamp: new Date().toISOString(),
    };
    writeFileSync(resolve(runDir, 'summary.json'), JSON.stringify(summary, null, 2) + '\n');

    // Screenshot (with timeout — WASM hang blocks browser's main thread,
    // making page.screenshot() hang too)
    const screenshotTimeout = 5000;
    const canvasEl = page.locator('#dc3-canvas');
    const canvasShot = await withTimeout(
        canvasEl.screenshot({ path: resolve(runDir, 'canvas.png') }).then(() => true).catch(() => false),
        screenshotTimeout, false
    );
    if (canvasShot) {
        console.log('[test] Canvas screenshot saved');
    } else {
        console.log('[test] Canvas screenshot failed/timed out, trying full page...');
        const pageShot = await withTimeout(
            page.screenshot({ path: resolve(runDir, 'page.png'), fullPage: true }).then(() => true).catch(() => false),
            screenshotTimeout, false
        );
        if (pageShot) {
            console.log('[test] Full page screenshot saved');
        } else {
            console.log('[test] All screenshots failed (main thread likely blocked by WASM hang)');
        }
    }

    console.log('');
    console.log(`[test] ========================================`);
    console.log(`[test] Result: ${result}`);
    console.log(`[test] Frames: ${frames}/${TARGET_FRAMES}`);
    console.log(`[test] Messages: ${consoleLog.length}`);
    if (wasmTrap) console.log(`[test] WASM trap: ${wasmTrap}`);
    if (pageError) console.log(`[test] Page error: ${pageError}`);
    console.log(`[test] Output: ${runDir}`);
    console.log(`[test] ========================================`);

    // Force-kill browser (browser.close() can hang if WASM blocked main thread)
    await withTimeout(browser.close(), 3000, undefined);
    return result;
}

// ---------------------------------------------------------------------------
// Main
// ---------------------------------------------------------------------------

async function main() {
    build();

    const server = startServer();
    let exitCode = 0;

    try {
        await waitForServer(PORT);
        console.log('[test] Server ready');

        const result = await runTest();
        exitCode = result === 'PASS' ? 0 : 1;
    } catch (e) {
        console.error(`[test] Fatal: ${e.message}`);
        exitCode = 2;
    } finally {
        server.kill('SIGTERM');
        if (KEEP) {
            console.log('[test] --keep: server still running, ctrl-c to stop');
        }
    }

    // Force exit — Playwright browser zombies can prevent clean shutdown
    setTimeout(() => process.exit(exitCode), 1000);
    process.exit(exitCode);
}

main();
