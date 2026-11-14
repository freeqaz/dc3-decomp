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
// when it does not (landscape), clear of the pose panel in the bottom centre.
(function () {
    'use strict';
    var q = new URLSearchParams(location.search);
    var force = q.get('touch');
    var fine = window.matchMedia && window.matchMedia('(any-pointer: fine)').matches;
    var show = force === '1' || (force !== '0' && !fine && ('ontouchstart' in window || navigator.maxTouchPoints > 0));
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
        '#dc3-touch .dpad { position:absolute; left:calc(var(--g) * 3); bottom:calc(var(--g) * 3);' +
        '  width:calc(var(--b) * 3); height:calc(var(--b) * 3); }' +
        '#dc3-touch .face { position:absolute; right:calc(var(--g) * 3); bottom:calc(var(--g) * 3);' +
        '  width:calc(var(--b) * 2.6); height:calc(var(--b) * 3); }' +
        '#dc3-touch .hint { position:absolute; left:50%; top:calc(var(--g) * 2); transform:translateX(-50%);' +
        '  color:#ccc; background:#000b; border:1px solid #555; border-radius:6px; padding:6px 10px;' +
        '  font:14px/1.35 system-ui,sans-serif; max-width:90vw; text-align:center; display:none; pointer-events:auto; }' +
        '@media (orientation:portrait) { #dc3-touch .hint { display:block; } }';
    var style = document.createElement('style');
    style.textContent = css;
    document.head.appendChild(style);

    var root = document.createElement('div');
    root.id = 'dc3-touch';
    // [label, key, container, left, top] in button units inside the container
    var buttons = [
        ['▲', 'ArrowUp', 'dpad', 1, 0], ['▼', 'ArrowDown', 'dpad', 1, 2],
        ['◀', 'ArrowLeft', 'dpad', 0, 1], ['▶', 'ArrowRight', 'dpad', 2, 1],
        ['A', 'Enter', 'face', 1.6, 1.0], ['B', 'Escape', 'face', 0.6, 2.0],
        ['▶❙', ' ', 'face', 0.0, 0.0],
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
        el.style.left = 'calc(var(--b) * ' + b[3] + ')';
        el.style.top = 'calc(var(--b) * ' + b[4] + ')';
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
