// dc3_render_hook.cpp — DC3-specific implementation of the engine's
// GameRenderHook interface.
//
// Phase 0.2a (see rb3/docs/native/NATIVE_PORT_ROADMAP.md): the engine's
// Rnd_Wgpu.cpp used to `#include "hamobj/HamDirector.h"`, etc. for two
// DC3-shaped stages (HamDirector overlay draw, per-HamCharacter impostor
// render-to-texture loop). Those stages now live behind GameRenderHook, and
// this file is the DC3 implementation slot.
//
// State of DC3's current native renderer
// --------------------------------------
// The matching dc3 native renderer at this commit does NOT actively call
// either stage — the historical `NativeVenueInit()` (which iterated
// HamCharacter / called HamDirector::VenueEnter) was removed in commit
// a97fbac6 once the proper DTA panel flow was wired up. As a result, both
// hook methods here are intentionally no-ops today: the slot is the right
// shape for either decomp to fill, and the engine's renderer dispatches to
// it unconditionally, but DC3 has nothing to issue.
//
// When DC3 needs to bring those passes back (e.g. for the gameplay
// impostor RTTs once the gameplay flow lights up further), the bodies below
// are where the HamDirector / HamCharacter logic lives. Keeping the
// implementation slot in place ensures the engine never has to relearn
// "DC3 might want a hook here" later.
//
// Linkage
// -------
// A file-scope static struct `HamRenderHookAutoRegister` registers
// `gHamHook` with the engine at C++ static-init time. Every DC3 target that
// links this TU (dc3-native, milo-viewer, render-test, milo-tests) gets the
// hook registered automatically; there is no explicit init call.

#include "platform/GameRenderHook.h"

// DC3 game headers — these were the ones removed from Rnd_Wgpu.cpp in
// Phase 0.2a. They live here in DC3 glue, never in the engine.
#include "hamobj/HamDirector.h"
#include "hamobj/HamCharacter.h"
#include "hamobj/HamGameData.h"
#include "obj/Dir.h"
#include "obj/Utl.h"

namespace {

class HamRenderHook : public GameRenderHook {
public:
    // Game HUD overlay pass — the engine has set up an overlay render pass
    // (post-processed venue already resolved into the framebuffer, 1x, no
    // depth) and now hands control to the game to issue HUD draws.
    //
    // On Xbox-shape DC3 this would call `TheHamDirector->Draw()` for the
    // gameplay HUD layer (flashcards, score, multiplier, autoplay scoring
    // glyphs). The current native build wires those panels through the DTA
    // panel flow which itself reaches the renderer via UIScreen::Draw, so no
    // extra dispatch is needed here today. Left as a deliberate no-op slot
    // for re-introduction later.
    void DrawGameOverlay(void* /*renderCtx*/) override {
        // No-op today. See comment above.
    }

    // Per-HamCharacter impostor / render-to-texture pre-pass — runs after
    // the shadow + DrawPreClear stages but before the main frame pass.
    //
    // The legacy (Xbox) path iterates HamCharacters with impostor RTT
    // targets and renders each into its own off-screen texture. The current
    // native build does not yet exercise the impostor path; left as a
    // deliberate no-op slot for re-introduction.
    void RenderCharacterImpostors(void* /*renderCtx*/) override {
        // No-op today. See comment above.
    }
};

HamRenderHook gHamHook;

struct HamRenderHookAutoRegister {
    HamRenderHookAutoRegister() { SetGameRenderHook(&gHamHook); }
};

HamRenderHookAutoRegister gHamRenderHookAutoRegister;

}  // namespace

// Public init hook, callable from startup code that wants explicit ordering
// over static-init. Idempotent.
void RegisterHamRenderHook() {
    SetGameRenderHook(&gHamHook);
}
