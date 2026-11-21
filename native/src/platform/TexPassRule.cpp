// np-look47 (docs/native/PLAN.md item 47): dc3's half of the rule "a texture
// pass is drawn like the frame".
//
// The engine owns the other half (the target is an RndTex of type exactly
// kRendered with no mips) and asks this predicate only where a pass that
// clears the target begins, keeping the answer until the next clearing pass
// (milo-native-engine src/platform/TexGpu.h, SetTexPassLikeFramePredicate).
//
// dc3's half: the target is the output_texture of at least one
// RndTexRenderer, and every RndTexRenderer that has it as output draws a
// Character and does not force mips. The census (archive item47/CENSUS.md
// section 6) found that this keeps the character projection targets
// (projection.tex: Break It Down's dancer, the tutorial and calibration
// figures) and leaves out keep_me.tex (augmented_photo's Group-draw snapshot
// target, read back to the CPU), the scratch and blur targets and everything
// no renderer draws into.
//
// Read through the renderer's properties, not its members, so
// src/system/rndobj/TexRenderer.h stays as the original declares it.

#include "char/Character.h"
#include "obj/Data.h"
#include "obj/Object.h"
#include "platform/TexGpu.h"
#include "rndobj/Tex.h"
#include "rndobj/TexRenderer.h"

namespace {

Hmx::Object* PropertyObject(const Hmx::Object* o, const char* name) {
    const DataNode* n = o->Property(Symbol(name), false);
    if (!n || n->Type() != kDataObject) return nullptr;
    return n->GetObj();
}

bool PropertyTrue(const Hmx::Object* o, const char* name) {
    const DataNode* n = o->Property(Symbol(name), false);
    return n && n->Type() == kDataInt && n->Int() != 0;
}

}  // namespace

bool NativeTexPassDrawsCharacter(RndTex* tex) {
    if (!tex) return false;
    int renderers = 0;
    for (ObjRef::iterator it = tex->Refs().begin(); it != tex->Refs().end(); ++it) {
        RndTexRenderer* r = dynamic_cast<RndTexRenderer*>(it->RefOwner());
        // A renderer can hold the texture through another reference (its
        // environ or cam slot, or an output it has since been pointed away
        // from); only one whose output is this texture counts.
        if (!r || r->GetOutputTexture() != tex) continue;
        if (!dynamic_cast<Character*>(PropertyObject(r, "draw"))) return false;
        if (PropertyTrue(r, "force_mips")) return false;
        ++renderers;
    }
    return renderers > 0;
}

namespace {
struct TexPassRuleAutoRegister {
    TexPassRuleAutoRegister() { SetTexPassLikeFramePredicate(&NativeTexPassDrawsCharacter); }
};
TexPassRuleAutoRegister gTexPassRuleAutoRegister;
}  // namespace

// Public init hook for startup code that wants explicit ordering over static
// init (the dc3_render_hook.cpp pattern). Idempotent.
void NativeRegisterTexPassLikeFrame() {
    SetTexPassLikeFramePredicate(&NativeTexPassDrawsCharacter);
}
