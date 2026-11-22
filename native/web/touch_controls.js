// DC3 web -- on-screen buttons for touch-only devices (phones, tablets).
//
// The game reads the keyboard through a bitmask the engine keeps from
// document keydown/keyup (native/src/platform/Joypad_Native.cpp, web branch),
// and the page's splash skip listens for the same keys. A button here
// dispatches the key a keyboard player would press, so every screen that a
// keyboard can drive, a thumb can too (a camera player can also use the
// game's own hand cursor, POSE_INPUT.md "Gesture menus").
//
// Shown when the device has no fine pointer (`(any-pointer: fine)` false) or
// with ?touch=1; ?touch=0 hides it.  Layout: a d-pad on the left and
// A (Enter, confirm) / B (Esc, back) / Start (Space) on the right, outside the
// game picture when the window has room (portrait), over its lower corners
// when it does not (landscape), clear of the pose panel in the bottom centre;
// in the portrait layout (portrait.js) one big cluster at the deck's bottom.
(function () {
    'use strict';
    var q = new URLSearchParams(location.search);
    var force = q.get('touch');
    var fine = window.matchMedia && window.matchMedia('(any-pointer: fine)').matches;
    var show = force === '1' || (force !== '0' && !fine && ('ontouchstart' in window || navigator.maxTouchPoints > 0));
    // The page refused to boot the engine (no secure context, WebGPU or
    // JSPI): its message owns the screen, and buttons would only cover it
    // (W-31: on an iPhone they sat over the JSPI message).
    if (window.dc3BootBlocked) {
        window.dc3TouchControls = { shown: false, presses: 0, blocked: String(window.dc3BootBlocked) };
        return;
    }
    window.dc3TouchControls = { shown: !!show, presses: 0 };
    if (!show) return;

    var css = '' +
        '#dc3-touch { position:fixed; inset:0; pointer-events:none; z-index:20; }' +
        '#dc3-touch button { pointer-events:auto; position:absolute; width:var(--b); height:var(--b);' +
        '  border-radius:50%; border:2px solid #fff6; background:#0008; color:#fffc;' +
        '  font:600 calc(var(--b) * 0.32)/1 system-ui,sans-serif; touch-action:none;' +
        '  -webkit-user-select:none; user-select:none; -webkit-tap-highlight-color:transparent; padding:0; }' +
        '#dc3-touch button.on { background:#c0f8; border-color:#fff; }' +
        '#dc3-touch { --b:min(15vmin, 72px); --g:calc(var(--b) * 0.1); }' +
        '#dc3-touch .dpad { position:absolute; left:calc(var(--g) * 3 + env(safe-area-inset-left, 0px));' +
        '  bottom:calc(var(--g) * 3 + env(safe-area-inset-bottom, 0px));' +
        '  width:calc(var(--b) * 3); height:calc(var(--b) * 3); }' +
        '#dc3-touch .face { position:absolute; right:calc(var(--g) * 3 + env(safe-area-inset-right, 0px));' +
        '  bottom:calc(var(--g) * 3 + env(safe-area-inset-bottom, 0px));' +
        '  width:calc(var(--b) * 2.6); height:calc(var(--b) * 3); }' +
        '#dc3-touch .hint { position:absolute; left:50%; top:calc(var(--g) * 2 + env(safe-area-inset-top, 0px)); transform:translateX(-50%);' +
        '  color:#ccc; background:#000b; border:1px solid #555; border-radius:6px; padding:6px 10px;' +
        '  font:14px/1.35 system-ui,sans-serif; max-width:90vw; text-align:center; display:none; pointer-events:auto; }' +
        '@media (orientation:portrait) { #dc3-touch .hint { display:block; } }' +
        // Portrait layout (portrait.js, html.dc3-portrait): the picture is at
        // the top and the deck under it; the buttons are one big remote-style
        // cluster at the bottom centre -- the d-pad with A in its middle, B
        // and Start in two corners -- as wide as a quarter of the screen per
        // button, less when the deck is short. No hint: the deck replaces it.
        '#dc3-touch button { left:calc(var(--b) * var(--lx)); top:calc(var(--b) * var(--ly)); }' +
        'html.dc3-portrait #dc3-touch { --b:var(--tb, 18vw); }' +
        'html.dc3-portrait #dc3-touch .hint { display:none; }' +
        'html.dc3-portrait #dc3-touch .dpad, html.dc3-portrait #dc3-touch .face { left:50%; right:auto;' +
        '  transform:translateX(-50%); width:calc(var(--b) * 3); height:calc(var(--b) * 3);' +
        '  bottom:calc(var(--g) * 4 + env(safe-area-inset-bottom, 0px)); }' +
        'html.dc3-portrait #dc3-touch .face { pointer-events:none; }' +
        'html.dc3-portrait #dc3-touch button { left:calc(var(--b) * var(--px)); top:calc(var(--b) * var(--py)); }' +
        'html.dc3-portrait #dc3-touch .dpad button { border-radius:22%; }' +
        // a small gap between the d-pad's arms and the A in their middle
        'html.dc3-portrait #dc3-touch .dpad button, html.dc3-portrait #dc3-touch button[data-key="Enter"] {' +
        '  width:calc(var(--b) * 0.92); height:calc(var(--b) * 0.92); margin:calc(var(--b) * 0.04); }' +
        'html.dc3-portrait #dc3-touch button[data-key="Enter"] { background:#a0c8; border-color:#fffa; font-size:calc(var(--b) * 0.4); }' +
        'html.dc3-portrait #dc3-touch button[data-key="Escape"], html.dc3-portrait #dc3-touch button[data-key=" "] {' +
        '  width:calc(var(--b) * 0.8); height:calc(var(--b) * 0.8); font-size:calc(var(--b) * 0.28); }';
    var style = document.createElement('style');
    style.textContent = css;
    document.head.appendChild(style);

    var root = document.createElement('div');
    root.id = 'dc3-touch';
    // [label, key, container, left, top, portrait left, portrait top] in
    // button units inside the container (in portrait both containers are the
    // same 3x3 box, so the face buttons sit in the d-pad's middle and corners)
    var buttons = [
        ['▲', 'ArrowUp', 'dpad', 1, 0, 1, 0], ['▼', 'ArrowDown', 'dpad', 1, 2, 1, 2],
        ['◀', 'ArrowLeft', 'dpad', 0, 1, 0, 1], ['▶', 'ArrowRight', 'dpad', 2, 1, 2, 1],
        ['A', 'Enter', 'face', 1.6, 1.0, 1, 1], ['B', 'Escape', 'face', 0.6, 2.0, 0.1, 2.1],
        ['▶❙', ' ', 'face', 0.0, 0.0, 2.1, 0.1],
    ];
    var boxes = {};
    ['dpad', 'face'].forEach(function (k) {
        var d = document.createElement('div');
        d.className = k;
        root.appendChild(d);
        boxes[k] = d;
    });
    var hint = document.createElement('div');
    hint.className = 'hint';
    hint.textContent = 'Turn your phone sideways for a bigger picture.';
    root.appendChild(hint);

    function send(type, key) {
        var code = key === ' ' ? 'Space' : key;
        document.dispatchEvent(new KeyboardEvent(type, { key: key, code: code, bubbles: true, cancelable: true }));
    }
    buttons.forEach(function (b) {
        var el = document.createElement('button');
        el.textContent = b[0];
        el.setAttribute('aria-label', b[1] === ' ' ? 'Start' : b[1]);
        el.dataset.key = b[1];
        el.style.setProperty('--lx', b[3]);
        el.style.setProperty('--ly', b[4]);
        el.style.setProperty('--px', b[5]);
        el.style.setProperty('--py', b[6]);
        // The engine samples the key bitmask once per game frame, so a tap whose
        // keydown and keyup land between two frames is never seen. Hold every
        // press for at least kMinHoldMs (a few frames even at 30 fps).
        var kMinHoldMs = 120;
        var down = false, downAt = 0, upTimer = 0;
        function press(e) {
            e.preventDefault();
            if (down) return;
            if (upTimer) { clearTimeout(upTimer); upTimer = 0; send('keyup', b[1]); }
            down = true;
            downAt = performance.now();
            el.classList.add('on');
            window.dc3TouchControls.presses++;
            send('keydown', b[1]);
        }
        function release(e) {
            if (e) e.preventDefault();
            if (!down) return;
            down = false;
            el.classList.remove('on');
            var left = kMinHoldMs - (performance.now() - downAt);
            if (left <= 0) { send('keyup', b[1]); return; }
            upTimer = setTimeout(function () { upTimer = 0; send('keyup', b[1]); }, left);
        }
        el.addEventListener('pointerdown', press);
        el.addEventListener('pointerup', release);
        el.addEventListener('pointercancel', release);
        el.addEventListener('pointerleave', release);
        el.addEventListener('contextmenu', function (e) { e.preventDefault(); });
        boxes[b[2]].appendChild(el);
    });
    function mount() { document.body.appendChild(root); }
    if (document.body) mount(); else document.addEventListener('DOMContentLoaded', mount);
    console.log('[dc3-touch] on-screen buttons shown (' + (force === '1' ? '?touch=1' : 'no fine pointer') + ')');
})();
