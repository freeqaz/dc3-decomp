#!/usr/bin/env node
/**
 * DC3 Web Port — Screenshot Validation Test
 *
 * Quick test to verify that headless WebGPU screenshots capture real
 * rendered content (not white/blank). Checks:
 *   1. Chrome launches with WebGPU in headless=new mode
 *   2. Engine reaches BOOT_RUNNING (window.__webgpuReady = true)
 *   3. Screenshot has non-trivial pixel data (not all white/black)
 *
 * Usage:
 *   node native/web/tests/test-screenshot.js              # auto-start server
 *   node native/web/tests/test-screenshot.js --no-server  # server already running
 *   node native/web/tests/test-screenshot.js --frames 10  # wait N engine frames
 *   node native/web/tests/test-screenshot.js --gpu-info   # dump chrome://gpu
 *
 * No xvfb needed — --headless=new provides its own compositor.
 */

const { spawn } = require('child_process');
const http = require('http');
const path = require('path');
const fs = require('fs');
const { launchBrowser, waitForWebGPUReady, screenshotReady } = require('./launch-helpers');

const args = process.argv.slice(2);
const hasFlag = (name) => args.includes(`--${name}`);
function getArg(name, defaultVal) {
    const idx = args.indexOf(`--${name}`);
    if (idx === -1) return defaultVal;
    return args[idx + 1] || defaultVal;
}

const PORT = parseInt(getArg('port', '8420'), 10);
const URL = `http://localhost:${PORT}`;
const NO_SERVER = hasFlag('no-server');
const VERBOSE = hasFlag('verbose');
const GPU_INFO = hasFlag('gpu-info');
const WAIT_FRAMES = parseInt(getArg('frames', '30'), 10);
const OUT_DIR = getArg('out', '/tmp/claude-1000/web-screenshot-test');

function log(msg) { console.log(`[screenshot] ${msg}`); }

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
            try { await waitForServer(URL); } catch (e) {
                log(`ERROR: server failed to start: ${e.message}`);
                process.exit(3);
            }
            log('Server ready');
        }

        // -- Browser --
        log('Launching Chrome (headless=new + Vulkan ANGLE)...');
        browser = await launchBrowser();
        const context = await browser.newContext({ viewport: { width: 1280, height: 720 } });
        const page = await context.newPage();

        // -- Optional: dump GPU adapter info --
        if (GPU_INFO) {
            log('Checking WebGPU adapter info...');
            // Navigate to localhost first (WebGPU requires secure context)
            const gpuPage = await context.newPage();
            await gpuPage.goto(URL, { timeout: 10000, waitUntil: 'domcontentloaded' });
            const gpuInfo = await gpuPage.evaluate(async () => {
                if (!navigator.gpu) return 'WebGPU: NOT AVAILABLE (no secure context?)';
                const adapter = await navigator.gpu.requestAdapter();
                if (!adapter) return 'WebGPU: no adapter';
                const info = adapter.info || {};
                return `WebGPU: OK — vendor=${info.vendor} arch=${info.architecture} desc=${info.description}`;
            });
            log(gpuInfo);
            await gpuPage.close();
        }

        // -- Console capture --
        const allLogs = [];
        page.on('console', (msg) => {
            const text = msg.text();
            allLogs.push(text);
            if (VERBOSE) console.log(`  [${msg.type()}] ${text}`);
        });
        page.on('pageerror', (err) => {
            console.log(`  [PAGE_ERROR] ${err.message}`);
        });

        // -- Navigate --
        log(`Loading ${URL}...`);
        await page.goto(URL, { waitUntil: 'domcontentloaded', timeout: 30000 });

        // -- Wait for WebGPU readiness --
        log('Waiting for __webgpuReady...');
        try {
            await waitForWebGPUReady(page, 60000);
            log('WebGPU ready!');
        } catch (e) {
            log('FAIL: __webgpuReady never set (WebGPU may not have initialized)');
            // Take a screenshot anyway to see what we got
            await page.screenshot({ path: `${OUT_DIR}/fail_no_webgpu.png` });
            log(`Failure screenshot: ${OUT_DIR}/fail_no_webgpu.png`);
            exitCode = 1;
            throw e;
        }

        // -- Wait for engine to render some frames --
        log(`Waiting for ${WAIT_FRAMES} engine frames...`);
        await page.waitForFunction(
            (n) => (window.dc3FrameCount || 0) >= n,
            WAIT_FRAMES,
            { timeout: 60000 }
        );
        const frameCount = await page.evaluate(() => window.dc3FrameCount);
        log(`Engine at frame ${frameCount}`);

        // -- Take screenshots --
        const shotPath = `${OUT_DIR}/frame_${frameCount}.png`;
        await screenshotReady(page, shotPath);
        log(`Screenshot saved: ${shotPath}`);

        // -- Validate screenshot isn't blank --
        const stat = fs.statSync(shotPath);
        log(`File size: ${stat.size} bytes`);

        // A 1280x720 all-white PNG compresses to ~5-6KB.
        // A 1280x720 all-black PNG compresses to ~2-3KB.
        // Real rendered content should be significantly larger.
        if (stat.size < 8000) {
            log('WARNING: screenshot may be blank (very small file size)');
            log('Check chrome://gpu with --gpu-info flag to verify WebGPU status');
            exitCode = 1;
        } else {
            log(`PASS: screenshot has substantial content (${stat.size} bytes > 8KB threshold)`);
        }

        // -- Take a second screenshot a bit later for comparison --
        await new Promise(r => setTimeout(r, 2000));
        const frameCount2 = await page.evaluate(() => window.dc3FrameCount);
        const shotPath2 = `${OUT_DIR}/frame_${frameCount2}.png`;
        await screenshotReady(page, shotPath2);
        log(`Second screenshot: ${shotPath2} (frame ${frameCount2})`);

        const stat2 = fs.statSync(shotPath2);
        const sizeDiff = Math.abs(stat.size - stat2.size);
        const pctDiff = stat.size > 0 ? (sizeDiff / stat.size * 100).toFixed(1) : 0;
        log(`Size comparison: ${stat.size}B vs ${stat2.size}B (${pctDiff}% diff)`);

    } catch (e) {
        if (exitCode === 0) exitCode = 1;
        log(`Error: ${e.message}`);
    } finally {
        if (browser) await browser.close().catch(() => {});
        if (serverProc) {
            serverProc.kill('SIGTERM');
            await new Promise(r => setTimeout(r, 500));
            if (!serverProc.killed) serverProc.kill('SIGKILL');
        }
    }

    log(`\nResult: ${exitCode === 0 ? 'PASS' : 'FAIL'}`);
    log(`Screenshots: ${OUT_DIR}/`);
    process.exit(exitCode);
})();
