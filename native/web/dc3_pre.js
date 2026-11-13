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
// (DC3 joints, camera-space metres). Returns 1 if the engine took the frame,
// 0 if it is not up yet (or DC3_POSE=off).
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

Module.preRun = Module.preRun || [];
Module.preRun.push(function () {
    try {
        // dc3 registers no WebMusicStem; off-main mode breaks the adaptive-latency heartbeat (engine Bug-4) — default off until dc3 ports the stem bridge.
        if (ENV['RB3_WEB_OFFMAIN_MIX'] === undefined) ENV['RB3_WEB_OFFMAIN_MIX'] = '0';

        var spec = new URLSearchParams(self.location.search).get('env');
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
});
