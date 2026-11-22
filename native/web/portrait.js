// DC3 web -- portrait layout for a phone held upright (PLAN W-16 follow-up,
// np-portrait).
//
// The game is authored at 16:9 (a 1280x720 virtual screen; RndCam keeps the
// vertical field of view and derives the horizontal one from the aspect, so a
// taller frame would crop every menu's sides rather than show more of it). So
// in portrait the 16:9 picture fills the screen's width, pinned to the top,
// and the space under it -- about two thirds of an upright phone -- becomes a
// deck: what is on screen (and the song, once one is picked), the camera
// view, and big touch buttons (touch_controls.js lays them out in the deck).
//
// Mode, from ?portrait=:
//   (absent)  auto: on for a touch-only device (phone, tablet) while its
//             viewport is taller than it is wide
//   1         on for any device while the viewport is portrait (a tall
//             desktop browser window)
//   0         off: the old layout -- the picture centred, the buttons under
//             it and a hint asking to turn the phone
// Rotating the phone switches the layout at once (resize/orientationchange);
// the engine then takes the new canvas box once it has held 250 ms
// (GpuDevice::FollowCanvas), as for any resize.
//
// A song held upright (np-portrait2, PLAN W-37 step 2): on game_screen the
// picture turns portrait-shaped. The page sets html.dc3-portrait-song (the
// canvas box becomes 9:16, full width at the top, a slim strip under it with
// B, Start and the camera view) and asks the engine for a 9:16 frame
// (milo_web_set_portrait_frame). The engine draws the world camera 9:16
// with the 16:9 shot's vertical view -- the dancer fills the frame -- and the
// 16:9 HUD larger and right-anchored, so the flashcards and the score are
// whole (native/src/platform/NativePortrait.h). Anything else (a menu,
// pause, results, landscape) is the 16:9 frame and step 1's layout.
// ?portraitsong=0 keeps step 1's 16:9 picture during songs too.
//
// window.dc3Portrait = { mode, active, changes, screen, song, song9x16,
// frame } for the tests (scripts/web/phone.mjs).
(function () {
    'use strict';
    // ?portrait= / ?portraitsong= read like ?dev: 0/false/off/no, 1/true/on/yes
    // (index.html's dc3Switch); anything else is auto.
    var sw = window.dc3Switch || function (v) { return v === '0' ? false : v === '1' ? true : null; };
    var qs = new URLSearchParams(location.search);
    var q = sw(qs.get('portrait'));
    var mode = q === false ? 'off' : q === true ? 'on' : 'auto';
    var touchOnly = !!window.dc3TouchOnly;  // index.html: (pointer: coarse)
    var songMode = sw(qs.get('portraitsong')) !== false;
    var state = window.dc3Portrait = { mode: mode, active: false, changes: 0, screen: '', song: '',
                                       song9x16: false, songChanges: 0, frameAsked: -1 };
    // The page refused to boot (no secure context, WebGPU or JSPI): its card
    // owns the screen.
    if (window.dc3BootBlocked) { state.blocked = String(window.dc3BootBlocked); return; }
    if (mode === 'off' || window.dc3DevPage) return;  // ?dev keeps its own layout

    var root = document.documentElement;
    var deck = null, nowEl = null, songEl = null;  // (inset: see layoutDeck)

    // Screen names from the engine's "will enter '<screen>'" lines (index.html
    // re-sends them as a dc3-screen event), in the words a player uses.
    var NAMES = {
        attract_screen: 'Dance Central 3', title_screen: 'Press A to start',
        main_screen: 'Main menu', choose_mode_screen: 'Choose a mode',
        song_select_screen: 'Pick a song', game_screen: 'Now dancing',
        pause_screen: 'Paused', perform_pause_screen: 'Paused', results_screen: 'Results',
        real_loading_screen: 'Loading the song', multiuser_screen: 'Who is playing',
        options_screen: 'Options', loading_screen: 'Loading',
        autosave_warning_screen: 'Autosave', wait_main_after_saveload_screen: 'Loading your profile',
    };
    function pretty(s) {
        if (NAMES[s]) return NAMES[s];
        return s.replace(/_screen$/, '').replace(/_/g, ' ').replace(/^./, function (c) { return c.toUpperCase(); });
    }

    function build() {
        deck = document.createElement('div');
        deck.id = 'dc3-deck';
        deck.innerHTML = '<div class="now"><div class="label">Now</div>' +
            '<div class="screen"></div><div class="song"></div></div>' +
            '<div class="tip">Turn sideways for the full screen</div>';
        nowEl = deck.querySelector('.screen');
        songEl = deck.querySelector('.song');
        // reads env(safe-area-inset-bottom) for layoutDeck
        inset = document.createElement('div');
        inset.style.cssText = 'position:fixed;visibility:hidden;pointer-events:none;height:0;padding-bottom:env(safe-area-inset-bottom, 0px)';
        document.body.appendChild(inset);
        document.body.appendChild(deck);
        render();
    }
    function render() {
        if (!nowEl) return;
        nowEl.textContent = state.screen ? pretty(state.screen) : 'Loading…';
        songEl.textContent = state.song ? state.song : '';
        songEl.style.display = state.song ? 'block' : 'none';
        layoutDeck();
    }

    // The deck's layout, measured rather than guessed (np-portrait2 round
    // 2: a fixed 140 px panel offset put the camera view over a long screen
    // name, and a short deck -- a split screen, a near-square foldable --
    // stacked the buttons over the card and the camera view).
    //   stacked  the now card, the tip, the camera view under them, the
    //            button cluster centred at the bottom
    //   compact  (html.dc3-portrait-compact) when the stack does not fit:
    //            the cluster at the right, the now card and the camera view
    //            in the column left of it, no tip
    // It sets --tb (one button), --panel-top and --view-h on <html>. Every
    // button is at least 44 px (touch_controls.js floors B and Start too).
    var inset = null, TB_MIN = 48, VIEW_MIN = 48;
    function clearLayout() {
        root.classList.remove('dc3-portrait-compact');
        ['--tb', '--panel-top', '--view-h', '--col-w'].forEach(function (k) { root.style.removeProperty(k); });
    }
    function layoutDeck() {
        if (!deck) return;
        if (!state.active || state.song9x16 || getComputedStyle(deck).display === 'none') { clearLayout(); return; }
        var vw = window.innerWidth;
        var top = deck.getBoundingClientRect().top;
        var bottom = window.innerHeight - (inset ? inset.getBoundingClientRect().height : 0);
        var deckH = bottom - top;
        var px = function (v) { return Math.round(v * 10) / 10 + 'px'; };
        // stacked
        var tb = Math.min(0.24 * vw, 112, Math.max(TB_MIN, (deckH - 250) / 3.2));
        root.classList.remove('dc3-portrait-compact');
        root.style.setProperty('--tb', px(tb));
        root.style.removeProperty('--col-w');
        var tip = deck.querySelector('.tip').getBoundingClientRect();
        var panelTop = tip.bottom + 8;
        var viewH = Math.min(0.26 * vw, bottom - 3.4 * tb - 12 - panelTop);
        var compact = viewH < VIEW_MIN;
        if (compact) {
            tb = Math.max(TB_MIN, Math.min(0.24 * vw, 112, (deckH - 24) / 3.2, (vw - 28 - 12 - 170) / 3));
            var colW = vw - 28 - 12 - 3 * tb;
            root.classList.add('dc3-portrait-compact');
            root.style.setProperty('--tb', px(tb));
            root.style.setProperty('--col-w', px(colW));
            panelTop = deck.querySelector('.now').getBoundingClientRect().bottom + 8;
            viewH = Math.max(40, Math.min(colW * 0.45, bottom - 12 - panelTop));
        }
        root.style.setProperty('--panel-top', px(panelTop));
        root.style.setProperty('--view-h', px(Math.max(compact ? 40 : VIEW_MIN, viewH)));
        state.layout = compact ? 'compact' : 'stacked';
    }

    function update() {
        var portrait = window.innerHeight > window.innerWidth;
        var on = portrait && (mode === 'on' || touchOnly);
        if (on !== state.active) {
            state.active = on;
            state.changes++;
            root.classList.toggle('dc3-portrait', on);
            window.dispatchEvent(new CustomEvent('dc3-portrait', { detail: { active: on } }));
        }
        updateSong();
        layoutDeck();
    }

    // The song's 9:16 frame: on only while the layout is portrait and the
    // game is on game_screen. The class first (the canvas box), then the
    // engine's frame shape; the engine waits for the new box to settle.
    var frameTimer = 0;
    // The runtime is up (index.html's onRuntimeInitialized sets
    // dc3RuntimeUp). The export's wrapper exists BEFORE that, and calling it
    // then ABORTS the whole runtime in a build with assertions (the debug
    // build: "native function called before runtime initialization") -- a
    // page that loaded upright then never reached the title, by timing.
    function runtimeUp() { return !!(window.dc3RuntimeUp && window.Module); }
    function askFrame() {
        frameTimer = 0;
        var want = state.song9x16 ? 1 : 0;
        if (state.frameAsked === want) return;
        // The engine starts with the 16:9 frame: nothing to ask until a song.
        if (state.frameAsked === -1 && want === 0) { state.frameAsked = 0; return; }
        try {
            if (runtimeUp() && typeof Module._milo_web_set_portrait_frame === 'function') {
                Module._milo_web_set_portrait_frame(want);
                state.frameAsked = want;
                return;
            }
        } catch (err) { /* not ready */ }
        frameTimer = setTimeout(askFrame, 500);  // the engine is not up yet
    }
    function updateSong() {
        var on = songMode && state.active && state.screen === 'game_screen';
        if (on !== state.song9x16) {
            state.song9x16 = on;
            state.songChanges++;
            root.classList.toggle('dc3-portrait-song', on);
            window.dispatchEvent(new CustomEvent('dc3-portrait-song', { detail: { active: on } }));
            layoutDeck();
        }
        if (!frameTimer) askFrame();
    }
    window.dc3PortraitFrame = function () {  // the engine's frame shape, for the tests
        if (!runtimeUp()) return -1;
        try { return Module._milo_web_portrait_frame(); } catch (err) { return -1; }
    };

    // The song being played, asked of the game itself (dc3_dta_eval, the
    // page's DTA bridge): MetaPerformer's song, then its metadata's title.
    // A few tries on game_screen only, and never while the deck is hidden.
    function dta(expr) {
        try {
            if (!runtimeUp() || typeof Module.ccall !== 'function') return null;
            var r = JSON.parse(Module.ccall('dc3_dta_eval', 'string', ['string'], [expr]));
            return r && r.ok ? r.result : null;
        } catch (err) { return null; }
    }
    function value(v) {
        if (v === null || v === undefined) return '';
        if (typeof v === 'string') return v;
        if (typeof v === 'object') return String(v.value !== undefined ? v.value : v.str !== undefined ? v.str : v.sym !== undefined ? v.sym : '');
        return String(v);
    }
    var songTimer = 0, songTries = 0;
    function askSong() {
        songTimer = 0;
        if (state.screen !== 'game_screen' || state.song) return;
        var shortName = value(dta('{meta_performer get_song}'));
        if (shortName && shortName !== '0') {
            var title = value(dta('{do {set $dc3_portrait_meta {song_mgr data {song_mgr get_song_id ' + shortName + '}}} {$dc3_portrait_meta title}}'));
            state.song = title && title !== '0' ? title : shortName;
            state.songShort = shortName;
            render();
            return;
        }
        if (++songTries < 15) songTimer = setTimeout(askSong, 2000);
    }
    window.dc3PortraitDta = dta;  // phone.mjs probes the bridge with it

    document.addEventListener('dc3-screen', function (e) {
        state.screen = e.detail.screen;
        // The song stays through pause and results; a menu clears it.
        if (/^(title|main|choose_mode|song_select|attract)_screen$/.test(state.screen)) state.song = '';
        if (state.screen === 'game_screen' && !songTimer) { songTries = 0; songTimer = setTimeout(askSong, 3000); }
        render();
        updateSong();
    });

    update();
    window.addEventListener('resize', update);
    window.addEventListener('orientationchange', update);
    if (window.matchMedia) {
        var mq = window.matchMedia('(orientation: portrait)');
        if (mq.addEventListener) mq.addEventListener('change', update);
    }
    if (document.body) build(); else document.addEventListener('DOMContentLoaded', build);
})();
