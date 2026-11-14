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
#include "rndobj/Mesh.h"

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

    // Text glyph colour (PLAN G-12). The engine routes a text mesh's atlas
    // ALPHA into RGB unless the game marks it a colour font -- right for an
    // atlas that keeps its glyphs in alpha with RGB == 0, wrong for DC3, whose
    // shader samples the atlas's RGB like any other texture. Every DC3 font
    // atlas is DXT5 (bitmap order 0x18) with real RGB:
    //   icons_buttons.mat (ui/resource/fonts/button_icon.milo) -- coloured
    //     discs with a dark letter: the help bar's (Y)/(A)/(B);
    //   icons.mat / icons_ext.mat -- grey/white glyphs: the checkbox pair
    //     'a' (unchecked) / 'b' (checked) HamNavProvider::Text sets;
    //   relay_wide_black_shadow, eagle-bold numbers, vibration_numbers --
    //     coloured or black-shadowed glyphs;
    //   default / dev / money -- RGB white, where both routes agree.
    // With the alpha route the button glyphs drew as blank white discs and the
    // two checkbox glyphs as the same white square, so a toggled option looked
    // unchanged. Only unnamed meshes are text (RndText builds its per-font
    // sub-meshes with no name); the engine asks only for those.
    DrawMaterialPolicy QueryDrawMaterialPolicy(RndMesh* mesh, RndMat* /*mat*/,
                                               bool /*skinned*/, RndMesh* /*owner*/,
                                               const char* /*camName*/) override {
        DrawMaterialPolicy p;
        p.isColorIcon = IsTextGlyphMesh(mesh);
        return p;
    }

    // UI colour (PLAN G-24). The Xbox 360 renderer is gamma space end to end:
    // the frame is D3DFMT_A8R8G8B8 (CreateBackBuffers, 100%), bitmaps fetch
    // with no gamma sign (DxRnd::D3DFormatForBitmap, 100%), and the shipped
    // standard pixel shaders multiply the stored texel (the prelit textured
    // permutation is `tfetch2D r0` then `mul oC0, r0, r2`; no log/exp touches
    // a colour). The engine's dc3 shader instead treats texel * colour as
    // linear and sRGB-encodes the result, which lifted every UI colour: the
    // move cards' deep purple (front-buffer ~ 80,20,75) drew as a washed pink
    // (~ 164,83,165). The world before EndWorld stays on the shader's linear
    // model (lane np-gfx-light owns it); everything drawn after it is UI.
    bool UiShadesInGammaSpace() override { return true; }

    // UI lighting (PLAN G-24). DxRnd shades a UI mesh like any other: a lit
    // material takes the environ selected when it draws. The score digits are
    // the case that shows it: Eagle-Book(numbers*).mat is prelit 0 /
    // use_environ 1, and score.grp leads with score.env (ambient 127,0,255;
    // light score.lit 226,63,255 x3), so retail digits are magenta while the
    // white atlas alone (what forced-prelit drew) is grey-white. The same font
    // is intensify 1 under a 0.5 font_alpha, and the shipped standard pixel
    // shaders scale the whole texel by intensify (`mul r0, r0, c5.yyyy`,
    // alpha included), so retail digits are opaque.
    bool UiUsesRetailMaterialTerms() override { return true; }

    // RndText marks every glyph page it creates (Text.cpp AllocateMeshes and
    // FontMap3d's meshes call MarkTextGlyphMesh), so the engine can draw only
    // those without depth test and culling and leave spotlight beams, ribbons,
    // lines and crowd billboards -- unnamed too -- their material's.
    bool MarksTextGlyphMeshes() override { return true; }

    static bool IsTextGlyphMesh(RndMesh* mesh) {
        const char* name = mesh ? mesh->Name() : nullptr;
        return name && !name[0];
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
