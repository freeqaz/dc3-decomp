#!/usr/bin/env node
/**
 * DC3 Web Port — Song List Scroll Test
 *
 * Navigates to song_select_screen, then scrolls down repeatedly,
 * taking screenshots after each scroll to verify visual updates.
 *
 * Usage:
 *   python3 native/web/server.py --port 8420 &
 *   xvfb-run -a --server-args="-screen 0 1920x1080x24" \
 *     node native/web/tests/test-song-scroll.js --no-server --verbose
 */

const { chromium } = require('playwright');
const { spawn } = require('child_process');
const http = require('http');
const path = require('path');
const fs = require('fs');

const args = process.argv.slice(2);
const hasFlag = (name) => args.includes(`--${name}`);
function getArg(name, defaultVal) {
    const idx = args.indexOf(`--${name}`);
    if (idx === -1) return defaultVal;
    return args[idx + 1] || defaultVal;
}

const PORT = parseInt(getArg('port', '8420'), 10);
const URL = `http://localhost:${PORT}`;
const VERBOSE = hasFlag('verbose');
const NO_SERVER = hasFlag('no-server');
const SCROLL_COUNT = parseInt(getArg('scrolls', '10'), 10);
const OUT_DIR = getArg('out', '/tmp/claude-1000/scroll-test');

function log(msg) { console.log(`[scroll-test] ${msg}`); }

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

(async () => {
    let serverProc = null;
    let browser = null;
    let exitCode = 0;

    const allLogs = [];
    const scrollLogs = []; // UIList scroll-specific logs
    const startTime = Date.now();

    fs.mkdirSync(OUT_DIR, { recursive: true });

    try {
        // -- Server --
        if (!NO_SERVER) {
            const serverScript = path.resolve(__dirname, '..', 'server.py');
            log(`Starting server on port ${PORT}`);
            serverProc = spawn('python3', [serverScript, '--port', String(PORT)], {
                stdio: ['ignore', 'pipe', 'pipe'],
                env: { ...process.env }
            });
            try { await waitForServer(URL); } catch (e) { log(`ERROR: ${e.message}`); process.exit(3); }
            log('Server ready');
        }

        // -- Browser --
        log('Launching Chrome with WebGPU...');
        browser = await chromium.launch({
            headless: !process.env.DISPLAY,
            args: [
                '--no-sandbox',
                '--enable-unsafe-webgpu',
                '--use-angle=vulkan',
                '--enable-features=Vulkan,VulkanFromANGLE',
                '--ozone-platform=x11',
                '--disable-extensions',
                '--mute-audio',
            ],
        });
        const page = await browser.newPage({ viewport: { width: 1280, height: 720 } });

        // -- Console capture --
        page.on('console', (msg) => {
            const text = msg.text();
            const elapsed = ((Date.now() - startTime) / 1000).toFixed(2);
            allLogs.push({ elapsed, type: msg.type(), text });
            if (VERBOSE || msg.type() === 'error') {
                console.log(`  [${elapsed}s ${msg.type()}] ${text}`);
            }
            // Capture scroll-specific logs
            if (text.includes('DC3 UIListDir::StartScroll') ||
                text.includes('DC3 UIListDir::CompleteScroll') ||
                text.includes('DC3 UIListSlot') ||
                text.includes('DC3 SCROLL')) {
                scrollLogs.push({ elapsed, text });
            }
        });

        page.on('pageerror', (err) => {
            console.log(`  [PAGE_ERROR] ${err.message}`);
        });

        // -- Navigate to app --
        log(`Loading ${URL}...`);
        await page.goto(URL, { waitUntil: 'domcontentloaded', timeout: 30000 });

        // -- Wait for attract screen --
        log('Waiting for attract screen...');
        const attractReady = await new Promise((resolve) => {
            const check = setInterval(() => {
                const found = allLogs.some(l => l.text.includes('attract_screen'));
                if (found || Date.now() - startTime > 30000) {
                    clearInterval(check);
                    resolve(found);
                }
            }, 500);
        });

        if (!attractReady) {
            log('FAIL: attract screen never appeared');
            process.exit(1);
        }
        await new Promise(r => setTimeout(r, 3000));

        // -- Helpers --
        await page.click('canvas');

        async function pressKey(key, holdMs = 150, label = '') {
            if (label) log(`  Key: ${label} (${key})`);
            await page.keyboard.down(key);
            await new Promise(r => setTimeout(r, holdMs));
            await page.keyboard.up(key);
            await new Promise(r => setTimeout(r, 200));
        }

        async function waitForScreen(screenName, timeoutMs = 10000) {
            log(`  Waiting for screen: ${screenName}...`);
            const deadline = Date.now() + timeoutMs;
            while (Date.now() < deadline) {
                const found = allLogs.some(l =>
                    l.text.includes(`will enter '${screenName}'`)
                );
                if (found) {
                    log(`  Screen '${screenName}' entered`);
                    await new Promise(r => setTimeout(r, 500));
                    return true;
                }
                await new Promise(r => setTimeout(r, 200));
            }
            log(`  TIMEOUT waiting for screen '${screenName}'`);
            return false;
        }

        // -- Navigate to song_select --
        await pressKey(' ', 150, 'Start (dismiss attract)');
        await waitForScreen('title_screen');

        await pressKey(' ', 150, 'Start (skip title)');
        await waitForScreen('main_screen');

        await pressKey('Enter', 150, 'A (main menu → choose_mode)');
        await waitForScreen('choose_mode_screen');

        await pressKey('Enter', 150, 'A (choose mode → song_select)');
        if (!await waitForScreen('song_select_screen', 15000)) {
            log('FAIL: never reached song_select_screen');
            process.exit(1);
        }

        // Extra time for song list to populate
        await new Promise(r => setTimeout(r, 2000));
        await page.screenshot({ path: `${OUT_DIR}/00_initial.png` });
        log('Screenshot: initial song list');

        // -- Scroll test --
        log(`\n=== SCROLL TEST: ${SCROLL_COUNT} scrolls down ===\n`);

        for (let i = 0; i < SCROLL_COUNT; i++) {
            const preScrollLogs = scrollLogs.length;

            await pressKey('ArrowDown', 150, `Down (scroll ${i + 1}/${SCROLL_COUNT})`);
            // Wait for scroll animation to complete
            await new Promise(r => setTimeout(r, 400));

            await page.screenshot({ path: `${OUT_DIR}/${String(i + 1).padStart(2, '0')}_after_down.png` });

            // Report any new scroll logs
            const newLogs = scrollLogs.slice(preScrollLogs);
            if (newLogs.length > 0) {
                for (const l of newLogs) {
                    log(`  SCROLL_LOG: ${l.text}`);
                }
            } else {
                log(`  (no scroll log output for scroll ${i + 1})`);
            }
        }

        log('\n=== SCROLL TEST COMPLETE ===');
        log(`Total scroll logs captured: ${scrollLogs.length}`);
        log(`Screenshots saved to: ${OUT_DIR}/`);

        // -- Summary --
        log('\n--- All scroll logs ---');
        for (const l of scrollLogs) {
            log(`  [${l.elapsed}s] ${l.text}`);
        }

    } catch (err) {
        log(`Error: ${err.message}`);
        exitCode = 1;
    } finally {
        if (browser) await browser.close().catch(() => {});
        if (serverProc) serverProc.kill();
    }

    process.exit(exitCode);
})();
