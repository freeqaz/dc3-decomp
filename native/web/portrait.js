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
    var q = new URLSearchParams(location.search).get('portrait');
    var mode = q === '0' ? 'off' : q === '1' ? 'on' : 'auto';
    var fine = window.matchMedia && window.matchMedia('(any-pointer: fine)').matches;
    var touchOnly = !fine && ('ontouchstart' in window || navigator.maxTouchPoints > 0);
    var songMode = new URLSearchParams(location.search).get('portraitsong') !== '0';
    var state = window.dc3Portrait = { mode: mode, active: false, changes: 0, screen: '', song: '',
                                       song9x16: false, songChanges: 0, frameAsked: -1 };
    // The page refused to boot (no secure context, WebGPU or JSPI): its card
    // owns the screen.
    if (window.dc3BootBlocked) { state.blocked = String(window.dc3BootBlocked); return; }
    if (mode === 'off' || window.dc3DevPage) return;  // ?dev keeps its own layout

    var root = document.documentElement;
    var deck = null, nowEl = null, songEl = null;

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
        document.body.appendChild(deck);
        render();
    }
    function render() {
        if (!nowEl) return;
        nowEl.textContent = state.screen ? pretty(state.screen) : 'Loading…';
        songEl.textContent = state.song ? state.song : '';
        songEl.style.display = state.song ? 'block' : 'none';
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
    }

    // The song's 9:16 frame: on only while the layout is portrait and the
    // game is on game_screen. The class first (the canvas box), then the
    // engine's frame shape; the engine waits for the new box to settle.
    var frameTimer = 0;
    function askFrame() {
        frameTimer = 0;
        var want = state.song9x16 ? 1 : 0;
        if (state.frameAsked === want) return;
        try {
            if (window.Module && typeof Module._milo_web_set_portrait_frame === 'function') {
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
        }
        if (!frameTimer) askFrame();
    }
    window.dc3PortraitFrame = function () {  // the engine's frame shape, for the tests
        try { return Module._milo_web_portrait_frame(); } catch (err) { return -1; }
    };

    // The song being played, asked of the game itself (dc3_dta_eval, the
    // page's DTA bridge): MetaPerformer's song, then its metadata's title.
    // A few tries on game_screen only, and never while the deck is hidden.
    function dta(expr) {
        try {
            if (!window.Module || typeof Module.ccall !== 'function') return null;
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
