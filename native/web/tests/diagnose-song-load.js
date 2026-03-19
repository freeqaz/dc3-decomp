#!/usr/bin/env node
/**
 * DC3 Web Port — Song Loading Diagnosis
 *
 * Navigates to game_screen via JS injection (simulating the screen transition)
 * to reproduce and diagnose the song-loading hang.
 *
 * Usage:
 *   xvfb-run -a node native/web/tests/diagnose-song-load.js --no-server
 *   xvfb-run -a node native/web/tests/diagnose-song-load.js --no-server --verbose
 */

const { chromium } = require('playwright');
const { spawn } = require('child_process');
const http = require('http');
const path = require('path');

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
const TIMEOUT_S = parseInt(getArg('timeout', '90'), 10);
const HANG_TIMEOUT_S = parseInt(getArg('hang-timeout', '15'), 10);
const SAVE_LOGS = getArg('save-logs', null);

function log(msg) { console.log(`[diag] ${msg}`); }

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
    const errors = [];
    let lastLogTime = Date.now();
    const startTime = Date.now();

    // Milestones we want to track (strings from actual engine output)
    const milestones = {
        'attract_screen': false,
        'main_screen': false,
        'transition complete': false,
        'game_screen': false,
        'PollForLoading': false,
        'FileMerger': false,
        'song merger': false,
        'IsLoaded': false,
        'DONE (state 4)': false,
        'StartIntro': false,
    };

    try {
        // -- Server --
        if (!NO_SERVER) {
            const serverScript = path.resolve(__dirname, '..', 'server.py');
            log(`Starting server on port ${PORT}`);
            serverProc = spawn('python', [serverScript, '--port', String(PORT)], {
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
            const entry = { elapsed, type: msg.type(), text };
            allLogs.push(entry);
            // Only count non-empty lines as "activity" for hang detection
            if (text.trim().length > 0) {
                lastLogTime = Date.now();
            }

            if (VERBOSE || msg.type() === 'error') {
                console.log(`  [${elapsed}s ${msg.type()}] ${text}`);
            }

            // Track milestones
            for (const [key] of Object.entries(milestones)) {
                if (!milestones[key] && text.includes(key)) {
                    milestones[key] = elapsed;
                    if (!VERBOSE) log(`Milestone: "${key}" at ${elapsed}s`);
                }
            }
        });

        page.on('pageerror', (err) => {
            errors.push(err.message || String(err));
            console.log(`  [PAGE_ERROR] ${err.message}`);
        });

        // -- Navigate to app --
        log(`Loading ${URL}...`);
        await page.goto(URL, { waitUntil: 'domcontentloaded', timeout: 30000 });

        // -- Wait for attract screen (engine is running) --
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
            exitCode = 1;
            throw new Error('attract screen timeout');
        }

        // Wait for attract screen to stabilize + a few render frames
        await new Promise(r => setTimeout(r, 3000));

        // -- Navigate to game_screen via simulated input --
        log('Sending input to navigate past attract screen...');
        // Web input uses window._dc3Keys bitmask polled per-frame by JoypadPoll.
        // Bit positions: X/A=6, Circle/B=5, Start=11, DUp=12, DDown=14, DLeft=15, DRight=13
        // We must hold buttons for multiple frames (>100ms at 30fps) for the engine to see them.

        // Use real keyboard events (matching what the user does manually).
        // Must click the canvas first to ensure it has focus for key events.
        await page.click('canvas');

        async function pressKey(key, holdMs = 150, label = '') {
            if (label) log(`  Key: ${label} (${key})`);
            await page.keyboard.down(key);
            await new Promise(r => setTimeout(r, holdMs));
            await page.keyboard.up(key);
            await new Promise(r => setTimeout(r, 200));
        }

        // Wait for a specific screen to appear in the transition logs
        async function waitForScreen(screenName, timeoutMs = 10000) {
            log(`  Waiting for screen: ${screenName}...`);
            const deadline = Date.now() + timeoutMs;
            while (Date.now() < deadline) {
                // Check for "will enter '<screenName>'" in logs
                const found = allLogs.some(l =>
                    l.text.includes(`will enter '${screenName}'`)
                );
                if (found) {
                    log(`  Screen '${screenName}' entered`);
                    await new Promise(r => setTimeout(r, 500)); // let it stabilize
                    return true;
                }
                await new Promise(r => setTimeout(r, 200));
            }
            log(`  TIMEOUT waiting for screen '${screenName}'`);
            return false;
        }

        // Navigate: attract → title → main → choose_mode → song_select
        await pressKey(' ', 150, 'Start (dismiss attract)');
        await waitForScreen('title_screen');

        await pressKey(' ', 150, 'Start (skip title)');
        await waitForScreen('main_screen');

        await pressKey('Enter', 150, 'A (main menu → choose_mode)');
        await waitForScreen('choose_mode_screen');

        await pressKey('Enter', 150, 'A (choose mode → song_select)');
        if (!await waitForScreen('song_select_screen')) {
            log('FAIL: never reached song_select_screen');
            exitCode = 1;
            throw new Error('song_select_screen timeout');
        }

        // Extra stabilization for song select to populate
        await new Promise(r => setTimeout(r, 1500));
        await page.screenshot({ path: '/tmp/claude-1000/song_select.png' });
        log('Screenshot: song_select_screen');

        // Navigate down past the tier header to a song
        for (let i = 0; i < 3; i++) {
            await pressKey('ArrowDown', 150, `Down (${i+1})`);
            await new Promise(r => setTimeout(r, 300));
        }
        await page.screenshot({ path: '/tmp/claude-1000/after_down.png' });
        log('Screenshot: after Down x3');

        // Select the song — after this the game may hang, so we use
        // page.evaluate for key injection (non-blocking) + setTimeout monitoring
        await pressKey('Enter', 150, 'A (select song)');
        log('Song selected, monitoring for loading/hang...');

        // Non-blocking confirmation key presses via evaluate (won't block if page hangs)
        // The game auto-transitions through multiuser→loading→game_screen on native,
        // but press A/Start in case any confirmation dialogs appear.
        const confirmKeys = async () => {
            for (let i = 0; i < 5; i++) {
                try {
                    await Promise.race([
                        pressKey('Enter', 150, `A (confirm ${i+1})`),
                        new Promise(r => setTimeout(r, 3000)),
                    ]);
                    await new Promise(r => setTimeout(r, 500));
                } catch { break; }
            }
            try {
                await Promise.race([
                    pressKey(' ', 150, 'Start (ready up)'),
                    new Promise(r => setTimeout(r, 3000)),
                ]);
            } catch {}
        };

        // Run confirmation keys and monitoring concurrently
        let hangDetected = false;
        const monitor = async () => {
            let doneAt = null; // timestamp when DONE (state 4) was seen
            for (let tick = 0; tick < TIMEOUT_S; tick++) {
                await new Promise(r => setTimeout(r, 1000));

                const now = Date.now();
                const silentMs = now - lastLogTime;
                const elapsedTotal = ((now - startTime) / 1000).toFixed(0);

                if (tick % 5 === 0) {
                    const lastLog = allLogs[allLogs.length - 1];
                    log(`  ${elapsedTotal}s: ${allLogs.length} logs, silent=${(silentMs/1000).toFixed(1)}s, last="${lastLog?.text?.substring(0, 60)}"`);
                }

                if (allLogs.length > 20 && silentMs > HANG_TIMEOUT_S * 1000) {
                    log(`HANG DETECTED at ${elapsedTotal}s — no output for ${(silentMs/1000).toFixed(1)}s`);
                    hangDetected = true;
                    exitCode = 2;
                    // Take a screenshot of whatever state we're in
                    try {
                        await Promise.race([
                            page.screenshot({ path: '/tmp/claude-1000/gameplay_hang.png' }),
                            new Promise(r => setTimeout(r, 3000)),
                        ]);
                        log('Screenshot: gameplay_hang.png');
                    } catch {}
                    return;
                }

                // After loading completes, wait extra time for gameplay to render
                if (milestones['DONE (state 4)'] && !doneAt) {
                    doneAt = now;
                    log('Song loading completed! Waiting for gameplay to render...');
                }

                // 10s after loading done, take gameplay screenshot
                if (doneAt && now - doneAt > 10000) {
                    try {
                        await Promise.race([
                            page.screenshot({ path: '/tmp/claude-1000/gameplay.png' }),
                            new Promise(r => setTimeout(r, 3000)),
                        ]);
                        log('Screenshot: gameplay.png (10s after loading)');
                    } catch {}
                    return;
                }
            }
        };

        await Promise.race([
            Promise.all([confirmKeys(), monitor()]),
            new Promise(r => setTimeout(r, (TIMEOUT_S + 5) * 1000)),
        ]);

        // -- Results --
        log('');
        log('=== Diagnosis Results ===');
        log(`Total logs: ${allLogs.length}`);
        log(`Errors: ${errors.length}`);
        log(`Hang detected: ${hangDetected}`);
        log('');
        log('Milestones:');
        for (const [key, val] of Object.entries(milestones)) {
            log(`  ${val ? `[${val}s]` : '[ -- ]'} ${key}`);
        }

        // Last 30 log lines
        log('');
        log('--- Last 30 log lines ---');
        const tail = allLogs.slice(-30);
        for (const e of tail) {
            console.log(`  [${e.elapsed}s ${e.type}] ${e.text}`);
        }

        // Grep for interesting patterns
        const patterns = ['FAIL:', 'HANG', 'ERROR', 'ASSERT', 'abort', 'RuntimeError',
                          'PollForLoading', 'IsLoaded', 'LoadMoveData', 'WebAssets: FAILED',
                          'AsyncFile FAILED', 'world.fm', 'song merger'];
        const interesting = allLogs.filter(l =>
            patterns.some(p => l.text.includes(p))
        );
        if (interesting.length > 0) {
            log('');
            log('--- Interesting log lines ---');
            for (const e of interesting) {
                console.log(`  [${e.elapsed}s ${e.type}] ${e.text}`);
            }
        }

        if (SAVE_LOGS) {
            require('fs').writeFileSync(SAVE_LOGS, JSON.stringify(allLogs, null, 2));
            log(`Full logs saved to ${SAVE_LOGS}`);
        }

    } catch (e) {
        if (exitCode === 0) exitCode = 3;
        log(`Error: ${e.message}`);
    } finally {
        if (browser) await browser.close().catch(() => {});
        if (serverProc) {
            serverProc.kill('SIGTERM');
            await new Promise(r => setTimeout(r, 500));
        }
    }

    log(`Exit code: ${exitCode}`);
    process.exit(exitCode);
})();
