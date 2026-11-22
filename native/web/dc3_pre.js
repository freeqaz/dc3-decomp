// DC3 Web — pre-js glue (runs inside the generated dc3-web.js scope, before
// the wasm runtime starts). Counterpart of rb3's rb3_pre.js (a small subset).
//
// URL → ENV bridge: forward `?env=KEY=VAL;KEY2=VAL2` from the page URL into
// Emscripten's ENV before main(), so ::getenv sees the values — runtime A/B of
// engine tunables without a rebuild, e.g.
//   ?env=DC3_SCREEN_BUNDLES_OFF=1
//   ?env=DC3_SCREEN_BUNDLE_NEXT=title_screen:song_select
// ENV is only in scope inside the generated JS (not exported to the page),
// which is why this must be a --pre-js and not index.html code.
// Pose input (native/web/pose/pose_input.js → native/src/platform/PoseInput_Web.cpp).
// Lives here, not in the page, because HEAPF32 is only in scope inside the
// generated JS. `floats` holds `count` persons of [trackId, 20 × (x, y, z, conf)]
// (DC3 joints, camera-space metres). Returns the engine's frame id for it
// (> 0) if the engine took the frame, 0 if it is not up yet (or DC3_POSE=off).
Module.dc3PoseFloatsPerPerson = 81;
Module.dc3PosePush = function (floats, count, timestampSec) {
    if (!Module['_dc3_pose_push'] || typeof HEAPF32 === 'undefined' || !HEAPF32) return 0;
    var cap = Module['_dc3_pose_capacity']();
    if (count > cap) count = cap;
    var ptr = Module['_dc3_pose_buffer']();
    if (count > 0) HEAPF32.set(floats.subarray(0, count * 81), ptr >> 2);
    return Module['_dc3_pose_push'](count, ptr, timestampSec);
};
Module.dc3PoseStop = function () {
    if (Module['_dc3_pose_stop']) Module['_dc3_pose_stop']();
};

// ---------------------------------------------------------------------------
// Save store (PLAN W-36): the player's save data survives a page reload.
//
// The game writes its saves with plain file I/O (MemcardMgr_Stub.cpp: the
// profile, <DC3_SAVE_DIR>/profile_<pad>_save.dat; Cache_Stub.cpp: the global
// options, <DC3_CACHE_DIR>/global/global), and on web those files are in MEMFS,
// which is gone on reload. So every reload was a new console: no scores, no
// unlocks, default options, and every first-run tutorial again.
//
// This keeps exactly those files in IndexedDB (database "dc3-save", one record
// a file):
//   boot    the records are written back into MEMFS. The boot state machine
//           (main_web.cpp, BOOT_FETCHING) waits for dc3SaveReady() before it
//           creates the App, so the game's first save search sees them. The
//           load runs beside the asset bundle download and adds no boot time.
//   save    the two backends call Dc3SaveStoreChanged() after every write and
//           delete (platform/SaveStore_Web.h -> Module.dc3SaveChanged); 50 ms
//           later the files that differ from what the store holds are written
//           in one transaction. Also when the page is hidden or unloaded.
//   reset   ?reset_save=1 empties the store before the game boots (a fresh
//           console) and takes the parameter out of the address bar, so the
//           next reload keeps what is saved after it.
//   off     ?save=off neither reads nor writes the store for this page load.
//
// Why not emscripten's IDBFS: its sync is whole-tree and timestamp-driven: it
// writes every file whose mtime differs from the stored one and deletes every
// stored file it does not have. A second tab that booted before the first one
// saved would, on its own next save (or on being hidden), put its older
// profile back and delete files it never saw. Here a file is written only when
// its bytes differ from what THIS page last loaded or stored, and deleted only
// when this page deleted it. It also keeps the song cache
// (<DC3_CACHE_DIR>/songcache/songcache, 95 KB, built at every boot from the
// song lists) out of the store, and needs no extra emscripten library.
//
// Not stored if IndexedDB is missing, refuses, or does not answer in 5 s
// (some private windows): the game then runs as before, on a fresh console,
// and the console says so once. window.dc3SaveState reports all of it.
// ---------------------------------------------------------------------------
var dc3SaveStoreStart = (function () {
    var DB_NAME = 'dc3-save', STORE = 'files';
    var LOAD_TIMEOUT_MS = 5000, STORE_DELAY_MS = 50;
    var g = (typeof window !== 'undefined') ? window : globalThis;
    var state = g.dc3SaveState = {
        mode: 'starting',   // starting | on | off (?save=off) | unavailable (not stored this session)
        ready: false,       // the boot may go on to the game's first save search
        reset: false,       // ?reset_save=1 emptied the store on this load
        loaded: [],         // [key, bytes] restored into MEMFS at boot
        stores: 0,          // write transactions completed
        lastStore: null,    // { why, keys, deleted, bytes, ms }
        pending: false,     // a change waits to be stored, or a transaction is in flight
        error: null,
    };
    var db = null, roots = null, synced = {}, timer = 0, inflight = null, again = null;

    function say(msg) { console.log('[dc3-save] ' + msg); }

    // Where the two backends write. The page sets both variables (unless
    // ?env= already did: the preRun below applies it first), so this file and
    // the C++ agree by construction.
    function resolveRoots() {
        var base = (ENV['HOME'] || '/home/web_user') + '/.local/share/dc3';
        if (!ENV['DC3_SAVE_DIR']) ENV['DC3_SAVE_DIR'] = base + '/saves';
        if (!ENV['DC3_CACHE_DIR']) ENV['DC3_CACHE_DIR'] = base + '/cache';
        roots = [
            { key: 'saves', dir: ENV['DC3_SAVE_DIR'] },
            { key: 'global', dir: ENV['DC3_CACHE_DIR'] + '/global' },
        ];
    }
    function pathOf(key) {
        for (var i = 0; i < roots.length; i++) {
            if (key.indexOf(roots[i].key + '/') === 0) return roots[i].dir + key.slice(roots[i].key.length);
        }
        return null;
    }
    function walk(dir, rel, out) {
        var names;
        try { names = FS.readdir(dir); } catch (e) { return; }  // not created yet
        for (var i = 0; i < names.length; i++) {
            var n = names[i];
            if (n === '.' || n === '..') continue;
            var st;
            try { st = FS.stat(dir + '/' + n); } catch (e) { continue; }
            if (FS.isDir(st.mode)) walk(dir + '/' + n, rel + n + '/', out);
            else if (FS.isFile(st.mode)) out[rel + n] = dir + '/' + n;
        }
    }
    function localFiles() {
        var out = {};
        for (var i = 0; i < roots.length; i++) walk(roots[i].dir, roots[i].key + '/', out);
        return out;
    }
    function same(a, b) {
        if (!a || !b || a.length !== b.length) return false;
        for (var i = 0; i < a.length; i++) if (a[i] !== b[i]) return false;
        return true;
    }

    function unavailable(why) {
        if (state.ready) return;
        state.mode = 'unavailable';
        state.error = why;
        state.ready = true;
        console.warn('[dc3-save] save data will NOT be kept across reloads this session: ' + why);
    }

    function openDb() {
        return new Promise(function (resolve, reject) {
            var req = indexedDB.open(DB_NAME, 1);
            req.onupgradeneeded = function () {
                if (!req.result.objectStoreNames.contains(STORE)) req.result.createObjectStore(STORE);
            };
            req.onsuccess = function () { resolve(req.result); };
            req.onerror = function () { reject(req.error || new Error('open failed')); };
            req.onblocked = function () { reject(new Error('open blocked by another tab')); };
        });
    }
    // Every record as [key, value]; with `wipe`, the store is emptied instead.
    function readAll(d, wipe) {
        return new Promise(function (resolve, reject) {
            var out = [];
            var tx = d.transaction(STORE, wipe ? 'readwrite' : 'readonly');
            var st = tx.objectStore(STORE);
            if (wipe) st.clear();
            else st.openCursor().onsuccess = function (ev) {
                var c = ev.target.result;
                if (!c) return;
                out.push([String(c.key), c.value]);
                c.continue();
            };
            tx.oncomplete = function () { resolve(out); };
            tx.onerror = tx.onabort = function () { reject(tx.error || new Error('read failed')); };
        });
    }
    function dropUrlParam(name) {
        try {
            var kept = location.search.replace(/^\?/, '').split('&').filter(function (p) {
                return p && p.split('=')[0] !== name;
            });
            history.replaceState(null, '', location.pathname + (kept.length ? '?' + kept.join('&') : '') + location.hash);
        } catch (e) {}
    }

    function start() {
        resolveRoots();
        var params = new URLSearchParams(location.search);
        var off = /^(off|0|false|no)$/i.test(params.get('save') || '');
        var reset = /^(1|true|yes|on)$/i.test(params.get('reset_save') || '');
        if (off) {
            state.mode = 'off';
            state.ready = true;
            say('off (?save=off): this page load starts on a fresh console and stores nothing');
            return;
        }
        if (typeof indexedDB === 'undefined' || !indexedDB) { unavailable('this browser has no IndexedDB here'); return; }
        var t0 = performance.now();
        var deadline = setTimeout(function () { unavailable('IndexedDB did not answer in ' + LOAD_TIMEOUT_MS + ' ms'); }, LOAD_TIMEOUT_MS);
        openDb().then(function (d) {
            return readAll(d, reset).then(function (records) { return [d, records]; });
        }).then(function (r) {
            clearTimeout(deadline);
            // Too late: the game already booted on a fresh console. Storing now
            // would replace the save that was not loaded, so stay unavailable.
            if (state.ready) { try { r[0].close(); } catch (e) {} return; }
            var bytes = 0;
            for (var i = 0; i < r[1].length; i++) {
                var key = r[1][i][0], rec = r[1][i][1], path = pathOf(key);
                if (!path || !rec || !rec.data) continue;
                var data = new Uint8Array(rec.data);
                FS.mkdirTree(path.slice(0, path.lastIndexOf('/')));
                FS.writeFile(path, data);
                synced[key] = data;
                state.loaded.push([key, data.length]);
                bytes += data.length;
            }
            db = r[0];
            db.onversionchange = function () { try { db.close(); } catch (e) {} db = null; };
            state.reset = reset;
            state.mode = 'on';
            state.ready = true;
            var ms = (performance.now() - t0).toFixed(0);
            if (reset) {
                say('reset (?reset_save=1): the stored save data was deleted; this is a fresh console');
                dropUrlParam('reset_save');
            } else if (state.loaded.length) {
                say('restored ' + state.loaded.length + ' file(s), ' + bytes + ' bytes, in ' + ms + ' ms: ' +
                    state.loaded.map(function (e) { return e[0]; }).join(', '));
            } else {
                say('nothing stored yet: a fresh console (' + ms + ' ms)');
            }
        }).catch(function (e) {
            clearTimeout(deadline);
            unavailable(String((e && e.message) || e));
        });
    }

    // Write what differs from the store. Resolves true when a transaction
    // committed, false when there was nothing to write or it failed.
    function flush(why) {
        if (state.mode !== 'on' || !db) return Promise.resolve(false);
        if (inflight) { again = why; return inflight; }
        var files = localFiles(), puts = [], dels = [], key;
        for (key in files) {
            var data;
            try { data = FS.readFile(files[key]); } catch (e) { continue; }
            if (!same(data, synced[key])) puts.push([key, data]);
        }
        for (key in synced) if (!(key in files)) dels.push(key);
        if (!puts.length && !dels.length) { state.pending = false; return Promise.resolve(false); }
        state.pending = true;
        var t0 = performance.now();
        inflight = new Promise(function (resolve) {
            function done(err) {
                inflight = null;
                if (err) {
                    state.error = String((err && err.message) || err);
                    console.warn('[dc3-save] could not store save data (' + why + '): ' + state.error);
                } else {
                    var bytes = 0;
                    puts.forEach(function (p) { synced[p[0]] = p[1]; bytes += p[1].length; });
                    dels.forEach(function (k) { delete synced[k]; });
                    state.stores++;
                    state.error = null;
                    state.lastStore = {
                        why: why, keys: puts.map(function (p) { return p[0]; }), deleted: dels,
                        bytes: bytes, ms: +(performance.now() - t0).toFixed(1),
                    };
                    say('stored (' + why + '): ' +
                        puts.map(function (p) { return p[0] + ' ' + p[1].length + ' bytes'; })
                            .concat(dels.map(function (k) { return k + ' deleted'; })).join(', ') +
                        ' in ' + state.lastStore.ms + ' ms');
                }
                if (again) { var w = again; again = null; schedule(w); }
                else if (!err) state.pending = false;
                resolve(!err);
            }
            var tx;
            try {
                tx = db.transaction(STORE, 'readwrite');
                var st = tx.objectStore(STORE);
                puts.forEach(function (p) { st.put({ data: p[1], time: Date.now() }, p[0]); });
                dels.forEach(function (k) { st.delete(k); });
            } catch (e) { done(e); return; }
            tx.oncomplete = function () { done(null); };
            tx.onerror = tx.onabort = function () { done(tx.error || new Error('transaction aborted')); };
            // Do not wait for the request callbacks before committing: on
            // pagehide there may be no later task to run them in.
            if (tx.commit) { try { tx.commit(); } catch (e) {} }
        });
        return inflight;
    }
    function schedule(why) {
        state.pending = true;
        if (timer) return;
        timer = setTimeout(function () { timer = 0; flush(why); }, STORE_DELAY_MS);
    }
    function flushNow(why) {
        if (timer) { clearTimeout(timer); timer = 0; }
        return flush(why);
    }

    // C++ -> here, after each save write/delete (platform/SaveStore_Web.h).
    Module.dc3SaveChanged = function () { if (state.mode === 'on') schedule('save'); };
    // The boot gate (main_web.cpp BOOT_FETCHING).
    Module.dc3SaveReady = function () { return state.ready ? 1 : 0; };
    // For tests and the console: store now; resolves when it is in IndexedDB.
    g.dc3SaveFlush = function () { return flushNow('manual'); };
    // For tests and the console: every file the game has under its save and
    // cache directories, as { path, bytes, stored }. `stored` is whether the
    // store keeps that file; nothing is kept while the store is off or
    // unavailable.
    g.dc3SaveFiles = function () {
        if (!roots) return [];
        var all = {}, kept = {}, files = state.mode === 'on' ? localFiles() : {}, key, out = [];
        for (key in files) kept[files[key]] = true;
        walk(ENV['DC3_SAVE_DIR'], 's/', all);
        walk(ENV['DC3_CACHE_DIR'], 'c/', all);
        for (key in all) {
            var bytes = -1;
            try { bytes = FS.stat(all[key]).size; } catch (e) {}
            out.push({ path: all[key], bytes: bytes, stored: !!kept[all[key]] });
        }
        return out;
    };

    if (typeof document !== 'undefined') {
        document.addEventListener('visibilitychange', function () {
            if (document.visibilityState === 'hidden') flushNow('page hidden');
        });
        g.addEventListener('pagehide', function () { flushNow('page unload'); });
    }

    return function () {
        try { start(); } catch (e) { unavailable(String((e && e.message) || e)); }
    };
})();

function dc3ApplyUrlEnv() {
    try {
        // dc3 registers no WebMusicStem; off-main mode breaks the adaptive-latency heartbeat (engine Bug-4) — default off until dc3 ports the stem bridge.
        if (ENV['RB3_WEB_OFFMAIN_MIX'] === undefined) ENV['RB3_WEB_OFFMAIN_MIX'] = '0';

        var params = new URLSearchParams(self.location.search);
        // ?profile=release -> DC3_PROFILE=release: the player defaults (no
        // developer Options rows, dev menu or overlays). Unset = dev, the
        // default (native/src/platform/System_Native.cpp, Dc3ProfileApply).
        // An explicit ?env=DC3_PROFILE=... below still wins.
        var profile = params.get('profile');
        if (profile) {
            ENV['DC3_PROFILE'] = profile;
            console.log('[dc3-env] DC3_PROFILE=' + profile + ' (?profile)');
        }

        var spec = params.get('env');
        if (!spec) return;
        spec.split(';').forEach(function (pair) {
            var eq = pair.indexOf('=');
            if (eq > 0) {
                var k = pair.slice(0, eq).trim();
                ENV[k] = pair.slice(eq + 1);
                console.log('[dc3-env] ' + k + '=' + ENV[k]);
            }
        });
    } catch (e) {
        console.warn('[dc3-env] bridge failed:', e);
    }
}

// One preRun, in this order: the save store reads DC3_SAVE_DIR / DC3_CACHE_DIR,
// which ?env= may set. (ENV is final once the runtime starts.)
Module.preRun = Module.preRun || [];
Module.preRun.push(function () {
    dc3ApplyUrlEnv();
    dc3SaveStoreStart();
});

// Pointer input in the game's menus (np-pointer; native/src/platform/Pointer_Native.h):
// a mouse hovers and clicks a row, a finger taps it, the wheel and a finger's
// drag scroll the list (a flick keeps going), the right button is back.
// Pointer Events on the canvas, so mouse, pen and touch share one path; the
// on-screen pad (touch_controls.js) is its own elements above the canvas and
// is not touched.  Coordinates go in relative to the PICTURE -- where the
// canvas's buffer is shown inside its CSS box (dc3PictureBox: the whole box
// when the buffer is stretched to it, which is the page's normal layout; the
// contained/covered rectangle under object-fit, e.g. fullscreen's contain) --
// with the picture's CSS size; the engine scales that onto its surface (the
// canvas buffer, all of which is the frame on the web) and into the frame.
// Nothing assumes 16:9 or a centred box.  ?env=DC3_POINTER=0 is the original:
// nothing is sent and the canvas keeps the browser's touch, wheel and menu.
function dc3PictureBox(c) {
    var r = c.getBoundingClientRect();
    // the content box: getBoundingClientRect includes border and padding
    var cs = typeof getComputedStyle === 'function' ? getComputedStyle(c) : null;
    function px(v) { var n = parseFloat(v); return isFinite(n) ? n : 0; }
    var l = r.left, t = r.top, w = r.width, h = r.height;
    if (cs) {
        var bl = px(cs.borderLeftWidth) + px(cs.paddingLeft), bt = px(cs.borderTopWidth) + px(cs.paddingTop);
        l += bl; t += bt;
        w -= bl + px(cs.borderRightWidth) + px(cs.paddingRight);
        h -= bt + px(cs.borderBottomWidth) + px(cs.paddingBottom);
    }
    var fit = cs ? cs.objectFit : 'fill', bw = c.width, bh = c.height;
    if (!fit || fit === 'fill' || !(bw > 0 && bh > 0) || !(w > 0 && h > 0))
        return { left: l, top: t, width: w, height: h };
    var k = fit === 'cover' ? Math.max(w / bw, h / bh)
          : fit === 'none' ? 1
          : fit === 'scale-down' ? Math.min(1, Math.min(w / bw, h / bh))
          : Math.min(w / bw, h / bh); // contain
    var pw = bw * k, ph = bh * k;
    // object-position: its computed form is two lengths/percentages ("50% 50%")
    var pos = (cs.objectPosition || '50% 50%').split(/\s+/);
    function at(v, free) { return /%$/.test(v) ? free * parseFloat(v) / 100 : px(v); }
    return { left: l + at(pos[0] || '50%', w - pw), top: t + at(pos[1] || '50%', h - ph), width: pw, height: ph };
}
(function () {
    if (typeof document === 'undefined') return;
    var KIND = { move: 0, down: 1, up: 2, wheel: 3, cancel: 4, back: 5 };
    var activeId = null;
    var swallowId = null; // a press that dismissed the splash: not the game's
    // DC3_POINTER=0 (?env=DC3_POINTER=0) is the original page: the browser
    // keeps the canvas's touch gestures, wheel and context menu, and nothing
    // is sent.  Unknown until the engine's runtime is up; until then nothing
    // is sent either (there is nothing to send it to).
    var enabled = null;
    function isEnabled() {
        if (enabled === null) {
            var fn = Module['_dc3_pointer_enabled'];
            // never call into the module before its runtime is up (a debug
            // build aborts on that): runtimeInitialized is the generated
            // code's own flag, in this --pre-js's scope
            var up = typeof runtimeInitialized !== 'undefined' ? runtimeInitialized : !!Module['calledRun'];
            if (typeof fn !== 'function' || !up) return false;
            try { enabled = !!fn(); } catch (err) { return false; }
            if (enabled) {
                // the browser must not scroll, zoom or take a drag on the game
                // as its own (body has touch-action: manipulation, under which
                // a real phone cancelled a finger's drag after two moves --
                // Fable review).  A rule, not only the inline style, so a
                // layout script restyling the canvas cannot drop it.
                var st = document.createElement('style');
                st.textContent = 'html.dc3-pointer #dc3-canvas { touch-action: none !important; }';
                (document.head || document.documentElement).appendChild(st);
                document.documentElement.classList.add('dc3-pointer');
                var c = document.getElementById('dc3-canvas');
                if (c) c.style.touchAction = 'none';
            }
        }
        return enabled;
    }
    // the runtime comes up a few seconds in: settle the switch then (bounded)
    var polls = 0;
    var poll = setInterval(function () {
        isEnabled();
        if (enabled !== null || ++polls > 600) clearInterval(poll);
    }, 500);
    function splashUp() {
        var v = document.getElementById('splash-video');
        return !!(v && v.style.opacity !== '0' && v.src && !v.ended);
    }
    function send(kind, e, wheel) {
        var fn = Module['_dc3_pointer'];
        var c = document.getElementById('dc3-canvas');
        if (!fn || !c || !isEnabled()) return;
        var r = dc3PictureBox(c);
        if (r.width <= 0 || r.height <= 0) return;
        var touch = e && e.pointerType && e.pointerType !== 'mouse' ? 1 : 0;
        var x = e ? e.clientX - r.left : 0, y = e ? e.clientY - r.top : 0;
        // the event's own time: a slow frame hands over a burst of events,
        // and the release velocity needs when each happened (Fable review)
        var age = e && typeof e.timeStamp === 'number' && typeof performance !== 'undefined'
            ? (performance.now() - e.timeStamp) / 1000 : 0;
        if (!(age >= 0 && age < 0.5)) age = 0; // another clock, or nonsense: now
        fn(kind, x, y, r.width, r.height, wheel || 0, touch, age);
    }
    function lose() {
        // a lost up (capture taken away, the tab hidden, the window blurred)
        // must not leave a finger down on the game
        if (activeId === null) return;
        activeId = null;
        send(KIND.cancel, null);
    }
    function attach() {
        var c = document.getElementById('dc3-canvas');
        if (!c || c.dataset.dc3Pointer) return !!c;
        c.dataset.dc3Pointer = '1';
        isEnabled();
        c.addEventListener('pointerdown', function (e) {
            if (!isEnabled()) return;
            if (splashUp()) { swallowId = e.pointerId; return; } // it skips the splash (index.html)
            if (e.button === 2) { send(KIND.back, e); return; }
            if (e.button !== 0 || (activeId !== null && activeId !== e.pointerId)) return;
            activeId = e.pointerId;
            try { c.setPointerCapture(e.pointerId); } catch (err) { /* old browser */ }
            send(KIND.down, e);
            if (e.pointerType !== 'mouse') e.preventDefault();
        });
        c.addEventListener('pointermove', function (e) {
            if (activeId !== null && e.pointerId !== activeId) return;
            if (activeId === null && e.pointerType !== 'mouse') return;
            send(KIND.move, e);
        });
        function end(kind) {
            return function (e) {
                if (e.pointerId === swallowId) { swallowId = null; return; }
                if (e.pointerId !== activeId) return;
                activeId = null;
                send(kind, e);
            };
        }
        c.addEventListener('pointerup', end(KIND.up));
        c.addEventListener('pointercancel', end(KIND.cancel));
        c.addEventListener('lostpointercapture', function (e) {
            // fires after pointerup too; then activeId is already clear
            if (e.pointerId === activeId) lose();
        });
        window.addEventListener('blur', lose);
        document.addEventListener('visibilitychange', function () { if (document.hidden) lose(); });
        c.addEventListener('wheel', function (e) {
            if (!isEnabled()) return;
            // pixels -> rows: a mouse notch is ~100 px (a line, 3 rows of text)
            var rows = e.deltaMode === 1 ? e.deltaY / 3 : e.deltaMode === 2 ? e.deltaY * 5 : e.deltaY / 100;
            send(KIND.wheel, e, rows);
            e.preventDefault();
        }, { passive: false });
        c.addEventListener('contextmenu', function (e) { if (isEnabled()) e.preventDefault(); });
        return true;
    }
    if (!attach()) document.addEventListener('DOMContentLoaded', attach);
})();

// For tests and tools (scripts/phone_pointer.mjs): GET /api/pointer/state's
// JSON, from the engine now, with each drawn row's centre also in PAGE CSS
// pixels (row.page = [x, y]) -- mapped back through the same two rectangles a
// pointer is mapped in by: the picture inside the canvas's CSS box
// (dc3PictureBox, s.picture) and where the frame sits on the engine's surface
// (s.present; on the web the whole canvas buffer).  null before the engine is
// up.
Module.dc3PointerState = function () {
    var fn = Module['_dc3_pointer_state'];
    var c = typeof document !== 'undefined' && document.getElementById('dc3-canvas');
    if (!fn || !c) return null;
    var s = JSON.parse(UTF8ToString(fn()));
    var r = dc3PictureBox(c), p = s.present;
    if (p && p.surface_w > 0 && p.w > 0) {
        s.lists.forEach(function (l) {
            l.rows.forEach(function (row) {
                var bx = p.x + row.x / p.frame_w * p.w, by = p.y + row.y / p.frame_h * p.h;
                row.page = [r.left + bx * r.width / p.surface_w, r.top + by * r.height / p.surface_h];
            });
        });
        s.picture = [r.left, r.top, r.width, r.height];
        var cr = c.getBoundingClientRect();
        s.canvas = [cr.left, cr.top, cr.width, cr.height, c.width, c.height];
    }
    return s;
};
if (typeof window !== 'undefined') window.dc3PointerState = Module.dc3PointerState;
