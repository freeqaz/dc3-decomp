#!/usr/bin/env node
/**
 * DC3 Web Port — Smoke Test
 *
 * Launches Chrome via Playwright, navigates to the web port, captures all
 * WASM console output, and detects hangs/crashes.
 *
 * Usage:
 *   node native/web/tests/web-smoke.js                # auto-start server
 *   node native/web/tests/web-smoke.js --no-server    # server already running
 *   node native/web/tests/web-smoke.js --timeout 60   # custom timeout (seconds)
 *   node native/web/tests/web-smoke.js --wait-for "DONE (state 4)"  # wait for specific log
 *
 * Requires: xvfb-run wrapper for WebGPU (ANGLE Vulkan needs X11)
 *   xvfb-run -a node native/web/tests/web-smoke.js
 *
 * Exit codes:
 *   0 = success (target log line seen or timeout reached without errors)
 *   1 = page crash or WebGPU init failure
 *   2 = hang detected (no console output for --hang-timeout seconds)
 *   3 = infrastructure error (server won't start, browser won't launch)
 */

const { chromium } = require('playwright');
const { spawn } = require('child_process');
const http = require('http');
const path = require('path');

// ---------------------------------------------------------------------------
// CLI args
// ---------------------------------------------------------------------------
const args = process.argv.slice(2);
function getArg(name, defaultVal) {
    const idx = args.indexOf(`--${name}`);
    if (idx === -1) return defaultVal;
    return args[idx + 1] || defaultVal;
}
const hasFlag = (name) => args.includes(`--${name}`);

const PORT = parseInt(getArg('port', '8420'), 10);
const URL = `http://localhost:${PORT}`;
const TIMEOUT_S = parseInt(getArg('timeout', '60'), 10);
const HANG_TIMEOUT_S = parseInt(getArg('hang-timeout', '10'), 10);
const WAIT_FOR = getArg('wait-for', null);
const NO_SERVER = hasFlag('no-server');
const VERBOSE = hasFlag('verbose');
const SAVE_LOGS = getArg('save-logs', null); // file path to save all logs

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------
function log(msg) { console.log(`[web-smoke] ${msg}`); }
function logV(msg) { if (VERBOSE) console.log(`[web-smoke] ${msg}`); }

function waitForServer(url, timeoutMs = 15000) {
    return new Promise((resolve, reject) => {
        const deadline = Date.now() + timeoutMs;
        const check = () => {
            http.get(`${url}/api/health`, (res) => {
                if (res.statusCode === 200) return resolve();
                retry();
            }).on('error', retry);
        };
        const retry = () => {
            if (Date.now() > deadline) return reject(new Error('Server did not start'));
            setTimeout(check, 300);
        };
        check();
    });
}

// ---------------------------------------------------------------------------
// Main
// ---------------------------------------------------------------------------
(async () => {
    let serverProc = null;
    let browser = null;
    let exitCode = 0;

    const allLogs = [];        // { time, type, text }
    const errors = [];         // fatal errors
    let lastLogTime = Date.now();
    let targetSeen = false;

    try {
        // -- Start server if needed ------------------------------------------
        if (!NO_SERVER) {
            const serverScript = path.resolve(__dirname, '..', 'server.py');
            log(`Starting server: python ${serverScript} --port ${PORT}`);
            serverProc = spawn('python', [serverScript, '--port', String(PORT)], {
                stdio: ['ignore', 'pipe', 'pipe'],
                env: { ...process.env }
            });
            serverProc.stdout.on('data', (d) => logV(`[server] ${d.toString().trim()}`));
            serverProc.stderr.on('data', (d) => logV(`[server] ${d.toString().trim()}`));

            try {
                await waitForServer(URL);
                log(`Server ready on ${URL}`);
            } catch (e) {
                log(`ERROR: ${e.message}`);
                process.exit(3);
            }
        } else {
            log(`Using existing server at ${URL}`);
        }

        // -- Launch browser --------------------------------------------------
        log('Launching Chrome with WebGPU...');
        browser = await chromium.launch({
            // headless: false + xvfb-run gives us a real GPU context.
            // If no xvfb detected, fall back to headless with SwiftShader.
            headless: !process.env.DISPLAY,
            args: [
                '--no-sandbox',
                '--enable-unsafe-webgpu',
                '--use-angle=vulkan',
                '--enable-features=Vulkan,VulkanFromANGLE',
                '--ozone-platform=x11',
                // Disable unneeded features for faster startup
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

        // -- Wire up console capture -----------------------------------------
        page.on('console', (msg) => {
            const entry = {
                time: ((Date.now() - lastLogTime) / 1000).toFixed(2),
                type: msg.type(),
                text: msg.text(),
            };
            allLogs.push(entry);
            lastLogTime = Date.now();

            const prefix = entry.type === 'error' ? 'ERR' : 'LOG';
            if (VERBOSE || entry.type === 'error') {
                console.log(`  [${prefix} +${entry.time}s] ${entry.text}`);
            }

            // Check for target line
            if (WAIT_FOR && entry.text.includes(WAIT_FOR)) {
                targetSeen = true;
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

        // -- Navigate --------------------------------------------------------
        log(`Navigating to ${URL} (timeout: ${TIMEOUT_S}s, hang: ${HANG_TIMEOUT_S}s)`);
        lastLogTime = Date.now();

        try {
            await page.goto(URL, {
                waitUntil: 'domcontentloaded',
                timeout: 30000,
            });
        } catch (e) {
            log(`Navigation error: ${e.message}`);
            exitCode = 3;
            throw e;
        }

        // -- Poll for completion or hang -------------------------------------
        const overallDeadline = Date.now() + TIMEOUT_S * 1000;

        while (Date.now() < overallDeadline) {
            await new Promise(r => setTimeout(r, 500));

            // Check for target log
            if (WAIT_FOR && targetSeen) {
                log(`Target log seen: "${WAIT_FOR}"`);
                break;
            }

            // Check for crash
            if (errors.some(e => e.includes('crashed'))) {
                log('Page crashed!');
                exitCode = 1;
                break;
            }

            // Check for WebGPU failure
            if (errors.some(e => e.includes('WebGPU') || e.includes('gpu'))) {
                log('WebGPU initialization failed');
                exitCode = 1;
                break;
            }

            // Check for hang: no console output for HANG_TIMEOUT_S seconds
            const silentMs = Date.now() - lastLogTime;
            if (allLogs.length > 5 && silentMs > HANG_TIMEOUT_S * 1000) {
                log(`HANG DETECTED: no console output for ${(silentMs/1000).toFixed(1)}s`);
                log(`Last log: "${allLogs[allLogs.length - 1]?.text}"`);
                exitCode = 2;
                break;
            }
        }

        if (!WAIT_FOR && exitCode === 0 && Date.now() >= overallDeadline) {
            log(`Timeout reached (${TIMEOUT_S}s) — no errors detected`);
        }

        // -- Summary ---------------------------------------------------------
        log('');
        log('=== Console Output Summary ===');
        log(`Total log lines: ${allLogs.length}`);
        log(`Errors: ${errors.length}`);
        if (WAIT_FOR) {
            log(`Target "${WAIT_FOR}": ${targetSeen ? 'SEEN' : 'NOT SEEN'}`);
            if (!targetSeen && exitCode === 0) exitCode = 2;
        }

        // Print last 20 log lines
        const tail = allLogs.slice(-20);
        if (tail.length > 0) {
            log('');
            log('--- Last 20 log lines ---');
            for (const entry of tail) {
                console.log(`  [${entry.type}] ${entry.text}`);
            }
        }

        if (errors.length > 0) {
            log('');
            log('--- Errors ---');
            for (const e of errors) {
                console.log(`  ${e}`);
            }
        }

        // Save full logs if requested
        if (SAVE_LOGS) {
            const fs = require('fs');
            fs.writeFileSync(SAVE_LOGS, JSON.stringify(allLogs, null, 2));
            log(`Full logs saved to ${SAVE_LOGS}`);
        }

    } catch (e) {
        if (exitCode === 0) exitCode = 3;
        log(`Error: ${e.message}`);
    } finally {
        if (browser) await browser.close().catch(() => {});
        if (serverProc) {
            serverProc.kill('SIGTERM');
            // Give it a moment to clean up
            await new Promise(r => setTimeout(r, 500));
            if (!serverProc.killed) serverProc.kill('SIGKILL');
        }
    }

    log(`Exit code: ${exitCode}`);
    process.exit(exitCode);
})();
