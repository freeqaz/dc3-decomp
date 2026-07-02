// DC3 Web Port — Entry Point
// Bootstraps the engine in the browser via Emscripten.
//
// Boot sequence (state machine, driven by emscripten_set_main_loop):
//   BOOT_INIT         → create MEMFS dirs, start bundle download
//   BOOT_FETCHING     → poll until bundle download complete
//   BOOT_ENGINE_INIT  → App constructor (shared with native desktop)
//   BOOT_GPU_WAIT     → wait for async WebGPU adapter/device
//   BOOT_GPU_READY    → initialize GPU resources (pipelines, buffers)
//   BOOT_RUNNING      → per-frame via App::RunOneFrame()

#ifdef __EMSCRIPTEN__

#include <emscripten/emscripten.h>
#include <emscripten/html5.h>
#include <emscripten/em_asm.h>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <set>
#include <string>

#include "App.h"
#include "platform/WebAssets.h"
#include "platform/Rnd_Wgpu.h"
#include "ui/UI.h"
#include "ui/UIScreen.h"

extern void NativeSetDataDir(const char *);
extern WgpuRnd *gWgpuRnd;

// ============================================================================
// Boot state machine
// ============================================================================

enum BootState {
    BOOT_INIT,
    BOOT_FETCHING,
    BOOT_ENGINE_INIT,
    BOOT_GPU_WAIT,
    BOOT_GPU_READY,
    BOOT_RUNNING,
    BOOT_ERROR,
};

static BootState sBootState = BOOT_INIT;
static App *sApp = nullptr;
static int sFrameCount = 0;
static int sGpuWaitFrames = 0;
static const int kGpuWaitTimeout = 300; // ~5 seconds at 60fps

// ============================================================================
// Per-screen dependency bundle prefetch (ported from rb3 main_web.cpp 46a59614)
//
// When the user ENTERs a screen, fire an ASYNC fetch of the NEXT screen's
// dependency bundle (/api/bundle/screen/<name>) so its .milo_xbox land in warm
// MEMFS during the dwell, BEFORE that screen's panel loaders ask for them.
// Reuses the boot-bundle async fetch+unpack path (WebAssetsFetchBundle): the
// bundle downloads off-thread and unpacks into /data/<rel>, and the engine's
// File ctor serves the now-resident bytes from MEMFS instead of freezing the
// wasm thread on a per-file sync XHR.
//
// The attract screen is the best prefetch window in the whole boot: the user
// watches the attract loop for seconds while the wasm thread is idle, so
// attract_screen fires the title_flow bundle (the 26-file / 82.5 MB shell
// working set the title→main transitions otherwise trickle in as sync XHRs —
// 7.7 s + 6.3 s freezes at 50 Mbit). title_screen repeats it as a fallback;
// the per-session seen-set makes the repeat free. main_screen prefetches the
// choose_mode/song_select set during the menu dwell.
//
// A screen whose bundle manifest is absent emits an empty bundle (server.py),
// so an unmapped/unknown screen is a harmless no-op. Default ON for web; opt
// out with DC3_SCREEN_BUNDLES_OFF (truthy). Tunable mapping via
// DC3_SCREEN_BUNDLE_NEXT="from:to,from2:a+b" ('+' = several bundles).
// ============================================================================

static const char *kDefaultScreenBundleMap =
    "attract_screen:title_flow,"
    "title_screen:title_flow+song_select,main_screen:song_select";

static bool ScreenBundlesEnabled() {
    // Default ON; opt out with a TRUTHY DC3_SCREEN_BUNDLES_OFF. A literal "0"
    // (or empty) means "not disabled" so an A/B run can force ON unambiguously.
    static int s = -1;
    if (s < 0) {
        const char *e = ::getenv("DC3_SCREEN_BUNDLES_OFF");
        s = (e && e[0] && e[0] != '0') ? 0 : 1; // truthy OFF flag => disabled
    }
    return s != 0;
}

// SFX sidecar bundle prefetch (default ON; DC3_SFX_BUNDLE_OFF=1 disables).
// One async fetch of every .ogg kXMA sidecar (/api/bundle/sfx_pcm, ~18 MB) so
// bank loads find warm MEMFS instead of paying a blocking per-key sync XHR per
// distinct SFX (measured: 514 fetches / 82.8 MB raw PCM in one boot).
static bool SfxBundleEnabled() {
    static int s = -1;
    if (s < 0) {
        const char *e = ::getenv("DC3_SFX_BUNDLE_OFF");
        s = (e && e[0] && e[0] != '0') ? 0 : 1; // truthy OFF flag => disabled
        // The bundle carries .ogg sidecars; when the ogg load path is disabled
        // (DC3_SFX_OGG_OFF A/B arm, XmaPcmSidecar.h) the runtime would never
        // read them, so skip the download too.
        const char *ogg = ::getenv("DC3_SFX_OGG_OFF");
        if (ogg && ogg[0] && ogg[0] != '0')
            s = 0;
    }
    return s != 0;
}

// Resolve the bundle name(s) to fetch when entering `fromScreen`, or "" if
// none. Parsed once from DC3_SCREEN_BUNDLE_NEXT (or the default). Keys are
// UIScreen object names; values name server bundles (screen-<name>.manifest).
static std::string NextScreenBundleName(const char *fromScreen) {
    static std::map<std::string, std::string> sMap;
    static bool sInit = false;
    if (!sInit) {
        sInit = true;
        const char *spec = ::getenv("DC3_SCREEN_BUNDLE_NEXT");
        std::string s = (spec && spec[0]) ? spec : kDefaultScreenBundleMap;
        size_t pos = 0;
        while (pos < s.size()) {
            size_t comma = s.find(',', pos);
            std::string pair = s.substr(
                pos, comma == std::string::npos ? std::string::npos : comma - pos);
            size_t colon = pair.find(':');
            if (colon != std::string::npos) {
                std::string from = pair.substr(0, colon);
                std::string to = pair.substr(colon + 1);
                if (!from.empty() && !to.empty())
                    sMap[from] = to;
            }
            if (comma == std::string::npos)
                break;
            pos = comma + 1;
        }
    }
    if (!fromScreen)
        return "";
    std::map<std::string, std::string>::const_iterator it = sMap.find(fromScreen);
    return it == sMap.end() ? "" : it->second;
}

// Called each frame from BOOT_RUNNING. On a change of current screen, fire the
// matching screen bundle(s) once. Cheap (a strcmp + a static-set lookup) when
// the screen hasn't changed or has no mapping.
static void WebScreenBundleHook() {
    if (!ScreenBundlesEnabled())
        return;
    UIScreen *scr = TheUI ? TheUI->CurrentScreen() : nullptr;
    const char *name = (scr && scr->Name()) ? scr->Name() : "";
    if (!name[0])
        return;

    static std::string sLastScreen;
    if (sLastScreen == name)
        return; // no transition this frame
    sLastScreen = name;

    std::string bundles = NextScreenBundleName(name);
    if (bundles.empty())
        return;

    // A mapping value may be a '+'-separated list ("title_flow+song_select");
    // fire each bundle at most once per session.
    static std::set<std::string> sFired;
    size_t pos = 0;
    while (pos <= bundles.size()) {
        size_t plus = bundles.find('+', pos);
        std::string bundle = bundles.substr(
            pos, plus == std::string::npos ? std::string::npos : plus - pos);
        if (!bundle.empty() && !sFired.count(bundle)) {
            sFired.insert(bundle);
            std::string url = "/api/bundle/screen/" + bundle;
            printf("DC3 Web: screen '%s' entered -> prefetch bundle %s\n", name,
                   url.c_str());
            WebAssetsFetchBundle(url.c_str());
        }
        if (plus == std::string::npos)
            break;
        pos = plus + 1;
    }
}

// ============================================================================
// Main loop — drives the boot state machine
// ============================================================================

static void mainLoop() {
    switch (sBootState) {

    case BOOT_INIT: {
        printf("DC3 Web: downloading assets (bundle)...\n");
        WebAssetsInit();
        WebAssetsFetchBundle();
        sBootState = BOOT_FETCHING;
        break;
    }

    case BOOT_FETCHING: {
        if (!WebAssetsAllDone()) break;

        int ok = WebAssetsCompletedCount();
        int fail = WebAssetsFailedCount();
        printf("DC3 Web: assets ready (%d files, %d errors)\n", ok, fail);
        sBootState = BOOT_ENGINE_INIT;
        break;
    }

    case BOOT_ENGINE_INIT: {
        // Fired AFTER the BOOT_FETCHING AllDone gate so this 18 MB fetch never
        // blocks engine init; it downloads while App boots. Sidecars a bank
        // load wins the race against still fall back to the per-key sync fetch
        // in XmaPcmSidecar.h (now the compact .ogg, so a lost race is cheap).
        if (SfxBundleEnabled()) {
            printf("DC3 Web: prefetching SFX sidecar bundle (/api/bundle/sfx_pcm)\n");
            WebAssetsFetchBundle("/api/bundle/sfx_pcm");
        }
        printf("DC3 Web: initializing engine via App...\n");
        NativeSetDataDir("/data");
        sApp = new App(0, nullptr);
        sBootState = BOOT_GPU_WAIT;
        printf("DC3 Web: waiting for GPU...\n");
        break;
    }

    case BOOT_GPU_WAIT: {
        // GPU may already be initialized if JSPI yielding during App constructor
        // allowed async callbacks to fire and InitGpuResources was called.
        if (gWgpuRnd && gWgpuRnd->GpuResourcesReady()) {
            printf("DC3 Web: GPU already initialized during App constructor (JSPI)\n");
            sBootState = BOOT_RUNNING;
            break;
        }
        sGpuWaitFrames++;
        if (gWgpuRnd) {
            gWgpuRnd->Gpu().PollEvents();
            if (gWgpuRnd->Gpu().IsReady()) {
                sBootState = BOOT_GPU_READY;
                break;
            }
        }
        if (sGpuWaitFrames >= kGpuWaitTimeout) {
            printf("DC3 Web: GPU not ready after %d frames — proceeding without rendering\n", sGpuWaitFrames);
            sBootState = BOOT_RUNNING;
        }
        break;
    }

    case BOOT_GPU_READY: {
        printf("DC3 Web: GPU ready, initializing resources...\n");
        gWgpuRnd->InitGpuResources();
        printf("DC3 Web: entering render loop\n");
        sBootState = BOOT_RUNNING;
        break;
    }

    case BOOT_RUNNING: {
        sFrameCount++;
        sApp->RunOneFrame();
        WebScreenBundleHook();
        EM_ASM({ window.dc3FrameCount = $0; }, sFrameCount);
        break;
    }

    case BOOT_ERROR:
        break;
    }
}

// ============================================================================
// Exported C API (called from JS)
// ============================================================================

extern "C" {

EMSCRIPTEN_KEEPALIVE
void dc3_resize_canvas(int w, int h) {
    if (gWgpuRnd && w > 0 && h > 0) {
        gWgpuRnd->Gpu().ResizeSurface(w, h);
    }
}

EMSCRIPTEN_KEEPALIVE
void dc3MainLoopTick() {
    mainLoop();
}

} // extern "C"

// ============================================================================
// Entry point
// ============================================================================

int main(int argc, char **argv) {
    printf("DC3 Web Port — Initializing\n");
#ifdef MILO_WEB_ASYNCIFY
    // JSPI mode: JS drives the frame loop via requestAnimationFrame + await.
    // Each tick calls dc3MainLoopTick() which can yield to the browser
    // (via emscripten_sleep) during synchronous file loads, letting the
    // loading screen render while .milo_xbox files download.
    EM_ASM({
        async function tick() {
            await Module._dc3MainLoopTick();
            requestAnimationFrame(tick);
        }
        requestAnimationFrame(tick);
    });
    emscripten_exit_with_live_runtime();
#else
    // Fallback: cooperative main loop (no async yielding — file loads block).
    emscripten_set_main_loop(mainLoop, 0, true);
#endif
    return EXIT_SUCCESS;
}

#endif // __EMSCRIPTEN__
