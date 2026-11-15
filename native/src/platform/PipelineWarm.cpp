// Load-time material pipeline warm (np-gfx spotperf round 3, np-hitch; PLAN
// H-33).
//
// A material pipeline is compiled the first time a draw asks for its key
// (PipelineManager::GetPipeline): 20-40 ms each on the dev box, 160-310 ms on
// a driver cache miss, and in a song that first time is mid-song -- the HUD's
// star and shockwave, the reflection of the backup dancers, the results
// screen's first frame. DirLoader::Cleanup calls NativeWarmLoadedDir when a
// dir has finished loading, which is on the loading screen, and this asks the
// renderer to compile, off the main thread (PipelineManager::WarmAsync), every
// key the dir's meshes would ask for in the passes they are drawn in.
//
// The keys come from the content, through the function the draw itself uses
// (MeshPipelineKey, engine platform/Mesh_Wgpu.cpp); nothing is recorded. What
// one mesh's material says is extended by what else is loaded:
//  - property animations on a material's blend, z mode or alpha write
//    (RndPropAnim; the HUD's feedback rings and meters, 92 such tracks in a
//    rollerrink song) -- each keyed value is a key the draw can ask for;
//  - the font of every bitmap text (RndFont): a text builds its glyph pages
//    when its string is set, so a page a later string needs (an icon font's)
//    is in no loaded dir, but its font is;
//  - the material of every ribbon (HamRibbon, RndRibbon): a ribbon's mesh is
//    its own and joins a dir only at its first draw;
//  - a CharFeedback's fail material, which it puts on its character's limb
//    meshes (skinned) when it syncs, and draws when a move is failed;
//  - every particle system (RndParticleSys), flare and screen mask (RndFlare,
//    RndScreenMask: a DrawRect with their material), whose pipelines are the
//    particle and rect renderers' (platform/Part_Wgpu.cpp, gfx/DrawRect2D.cpp),
//    1-10 ms each and compiled here synchronously.
// What the content does not say is the pass, so this says it, by where the
// object sits:
//  - everything: the world pass (the main frame: venue, characters, and the
//    HUD drawn into the world -- score star, meters, flashcards);
//  - under a UI panel (PanelDir): also the overlay pass (what a panel draws
//    after it ends the world, PanelDir::DrawShowing);
//  - under a Character: also a render-to-texture pass with depth (the
//    character drawn into a target of its own);
//  - a WorldReflection's draws: the reflection pass (cull flipped). Its draws
//    name objects that load after it (the dancers, the in-world score star),
//    so each later load walks every live reflection's draws again.
// "Under" follows subdirs and the dirs placed in a dir (proxies: a HUD's
// score_star_display holds its score stars that way).
//
// A draw whose key this misses compiles at the draw, as before; a draw whose
// key is still compiling waits for it (never skips). The pipelines are DC3's
// content and DC3's passes, so the walk is DC3's: rb3 and rb3-xenon do not
// call it.

#include "obj/Data.h"
#include "obj/DataUtl.h"
#include "obj/Dir.h"
#include "obj/Object.h"
#include "char/Character.h"
#include "hamobj/CharFeedback.h"
#include "hamobj/HamRibbon.h"
#include "rndobj/Draw.h"
#include "rndobj/Flare.h"
#include "rndobj/Font.h"
#include "rndobj/Group.h"
#include "rndobj/Mat.h"
#include "rndobj/Mesh.h"
#include "rndobj/Part.h"
#include "rndobj/PropAnim.h"
#include "rndobj/PropKeys.h"
#include "rndobj/Ribbon.h"
#include "rndobj/ScreenMask.h"
#include "rndobj/Text.h"
#include "ui/PanelDir.h"
#include "world/Reflection.h"

#include "platform/MeshPipelineKey.h"
#include "platform/Part_Wgpu.h"
#include "platform/Rnd_Wgpu.h"

#include <chrono>
#include <list>
#include <map>
#include <set>
#include <unordered_map>
#include <vector>

namespace {

enum Pass { kWorld = 1, kOverlay = 2, kTexture = 4 };

struct Contexts {
    MeshPassContext world, reflection, overlay, texture;
};

// The states the loaded property animations key on each material.
struct MatStates {
    std::set<int> blends, zModes, alphaWrites;
};
typedef std::map<RndMat *, MatStates> AnimatedStates;

void CollectAnimatedStates(RndPropAnim *anim, AnimatedStates &out) {
    static const Symbol blend("blend"), zMode("z_mode"), alphaWrite("alpha_write");
    for (PropKeys *k : anim->AllKeys()) {
        if (!k || !k->Prop() || k->Prop()->Size() != 1 || k->Prop()->Node(0).Type() != kDataSymbol)
            continue;
        RndMat *mat = dynamic_cast<RndMat *>(k->Target());
        if (!mat) continue;
        const Symbol prop = k->Prop()->Node(0).LiteralSym();
        std::set<int> *into = prop == blend ? &out[mat].blends
            : prop == zMode                 ? &out[mat].zModes
            : prop == alphaWrite            ? &out[mat].alphaWrites
                                            : nullptr;
        if (!into) continue;
        switch (k->KeysType()) {
        case PropKeys::kSymbol: {
            // An enum property keyed by its macro name (SymbolKeys::SetFrame,
            // kMacro: DataGetMacro(value)->Int(0)).
            Keys<Symbol, Symbol> *keys = k->AsSymbolKeys();
            for (size_t i = 0; keys && i < keys->size(); ++i) {
                DataArray *macro = DataGetMacro((*keys)[i].value);
                if (macro && macro->Size() > 0 && macro->Node(0).Type() == kDataInt)
                    into->insert(macro->Node(0).LiteralInt());
            }
            break;
        }
        case PropKeys::kBool: {
            Keys<bool, bool> *keys = k->AsBoolKeys();
            for (size_t i = 0; keys && i < keys->size(); ++i) into->insert((*keys)[i].value ? 1 : 0);
            break;
        }
        case PropKeys::kFloat: {
            Keys<float, float> *keys = k->AsFloatKeys();
            for (size_t i = 0; keys && i < keys->size(); ++i) into->insert((int)(*keys)[i].value);
            break;
        }
        default:
            break;
        }
    }
}

// Every combination of the animated states (each set with the material's
// own), less the material's own.
std::vector<MatKeyVariant> VariantsOf(RndMat *mat, const MatStates &s) {
    std::vector<int> blends(s.blends.begin(), s.blends.end());
    std::vector<int> zModes(s.zModes.begin(), s.zModes.end());
    std::vector<int> writes(s.alphaWrites.begin(), s.alphaWrites.end());
    blends.push_back(-1);
    zModes.push_back(-1);
    writes.push_back(-1);
    std::vector<MatKeyVariant> out;
    for (int b : blends)
        for (int z : zModes)
            for (int w : writes) {
                if (b < 0 && z < 0 && w < 0) continue;
                MatKeyVariant v;
                v.blend = b;
                v.zMode = z;
                v.alphaWrite = w;
                out.push_back(v);
            }
    (void)mat;
    return out;
}

class Warm {
public:
    explicit Warm(const Contexts &c) : mCtx(c) {}

    // Walk `dir` and what it holds, with the passes `passes` (more are added
    // under a panel or a character).
    void Visit(ObjectDir *dir, int passes, int depth = 0) {
        if (!dir || depth > 32) return;
        if (dynamic_cast<PanelDir *>(dir)) passes |= kOverlay;
        if (dynamic_cast<Character *>(dir)) passes |= kTexture;
        int &seen = mDirs[dir];
        if ((seen & passes) == passes) return;
        seen |= passes;
        mOrder.push_back(std::make_pair(dir, passes));
        std::vector<ObjectDir *> children;
        for (ObjDirItr<Hmx::Object> it(dir, false); it != nullptr; ++it) {
            Hmx::Object *obj = it;
            if (RndPropAnim *anim = dynamic_cast<RndPropAnim *>(obj)) {
                CollectAnimatedStates(anim, mAnimated);
            } else if (WorldReflection *refl = dynamic_cast<WorldReflection *>(obj)) {
                mReflections.push_back(refl);
            } else if (ObjectDir *child = dynamic_cast<ObjectDir *>(obj)) {
                if (child != dir) children.push_back(child);
            }
        }
        for (size_t i = 0; i < dir->SubDirs().size(); ++i)
            children.push_back(dir->SubDirs()[i]);
        for (ObjectDir *child : children) Visit(child, passes, depth + 1);
    }

    // Queue every visited dir's keys, now that the animated states of all of
    // them are known.
    int Queue() {
        int n = 0;
        for (const std::pair<ObjectDir *, int> &d : mOrder) {
            if (d.second & kWorld) n += QueueDir(d.first, mCtx.world);
            if (d.second & kOverlay) n += QueueDir(d.first, mCtx.overlay);
            if (d.second & kTexture) n += QueueDir(d.first, mCtx.texture);
        }
        return n;
    }

    // A drawable as WorldReflection::DrawShowing reaches it: a mesh, a text,
    // a dir (and what it holds), or a group of them.
    int QueueDrawable(Hmx::Object *obj, const MeshPassContext &ctx, int depth) {
        if (!obj || depth > 16) return 0;
        if (RndMesh *mesh = dynamic_cast<RndMesh *>(obj)) return QueueMesh(mesh, ctx);
        if (RndText *text = dynamic_cast<RndText *>(obj)) return QueueText(text, ctx);
        if (int n = QueueOther(obj, ctx)) return n;
        if (ObjectDir *dir = dynamic_cast<ObjectDir *>(obj)) {
            Warm sub(mCtx);
            sub.mAnimated = mAnimated;
            sub.Visit(dir, kWorld);  // the passes mark the visit; ctx is what is queued
            int n = 0;
            for (const std::pair<ObjectDir *, int> &d : sub.mOrder) n += sub.QueueDir(d.first, ctx);
            return n;
        }
        int n = 0;
        if (RndGroup *group = dynamic_cast<RndGroup *>(obj)) {
            for (auto it = group->Objects().begin(); it != group->Objects().end(); ++it)
                n += QueueDrawable(*it, ctx, depth + 1);
        }
        return n;
    }

    const std::vector<WorldReflection *> &Reflections() const { return mReflections; }
    AnimatedStates mAnimated;

private:
    int QueueMesh(RndMesh *mesh, const MeshPassContext &ctx) {
        RndMat *mat = mesh->Mat();
        AnimatedStates::const_iterator a = mat ? mAnimated.find(mat) : mAnimated.end();
        if (a == mAnimated.end()) return WarmMeshPipelines(mesh, ctx);
        const std::vector<MatKeyVariant> v = VariantsOf(mat, a->second);
        return WarmMeshPipelines(mesh, ctx, v.data(), (int)v.size());
    }
    // What draws a mesh that is in no dir (a ribbon's own), or draws without
    // a mesh (a particle system; a flare's or a screen mask's rect).
    int QueueOther(Hmx::Object *obj, const MeshPassContext &ctx) {
        if (HamRibbon *r = dynamic_cast<HamRibbon *>(obj))  // bones per segment
            return QueueMaterial(r->GetMat(), true, ctx);
        if (RndRibbon *r = dynamic_cast<RndRibbon *>(obj))  // no bones
            return QueueMaterial(r->GetMat(), false, ctx);
        if (CharFeedback *f = dynamic_cast<CharFeedback *>(obj))  // a character's limbs
            return QueueMaterial(f->GetFailMat(), true, ctx);
        if (RndFlare *f = dynamic_cast<RndFlare *>(obj))
            return f->GetMat() ? gWgpuRnd->WarmRectPipelines(f->GetMat(), ctx) : 0;
        if (RndScreenMask *m = dynamic_cast<RndScreenMask *>(obj))
            return m->GetMat() ? gWgpuRnd->WarmRectPipelines(m->GetMat(), ctx) : 0;
        if (RndParticleSys *p = dynamic_cast<RndParticleSys *>(obj)) {
            RndMat *mat = p->GetMat();
            if (!mat) return 0;
            AnimatedStates::const_iterator a = mAnimated.find(mat);
            if (a == mAnimated.end()) return WarmParticlePipelines(mat, ctx);
            const std::vector<int> blends(a->second.blends.begin(), a->second.blends.end());
            return WarmParticlePipelines(mat, ctx, blends.data(), (int)blends.size());
        }
        return 0;
    }
    int QueueMaterial(RndMat *mat, bool skinned, const MeshPassContext &ctx) {
        if (!mat) return 0;
        AnimatedStates::const_iterator a = mAnimated.find(mat);
        if (a == mAnimated.end()) return WarmMaterialPipelines(mat, skinned, ctx);
        const std::vector<MatKeyVariant> v = VariantsOf(mat, a->second);
        return WarmMaterialPipelines(mat, skinned, ctx, v.data(), (int)v.size());
    }
    int QueueText(RndText *text, const MeshPassContext &ctx) {
        int n = 0;
        for (std::list<RndTransformable *>::const_iterator it = text->Children().begin();
             it != text->Children().end(); ++it) {
            if (RndMesh *page = dynamic_cast<RndMesh *>(*it)) n += WarmMeshPipelines(page, ctx);
        }
        return n;
    }
    int QueueDir(ObjectDir *dir, const MeshPassContext &ctx) {
        int n = 0;
        for (ObjDirItr<Hmx::Object> it(dir, false); it != nullptr; ++it) {
            Hmx::Object *obj = it;
            if (RndMesh *mesh = dynamic_cast<RndMesh *>(obj)) {
                n += QueueMesh(mesh, ctx);
            } else if (RndText *text = dynamic_cast<RndText *>(obj)) {
                n += QueueText(text, ctx);
            } else if (int q = QueueOther(obj, ctx)) {
                n += q;
            } else if (RndFont *font = dynamic_cast<RndFont *>(obj)) {
                // Its pages draw in the world and in the overlay of whatever
                // panel shows them, which need not be the dir it loaded with.
                if (&ctx == &mCtx.world) {
                    for (int i = 0; i < font->NumMats(); ++i) {
                        n += WarmTextPagePipelines(font->Mat(i), mCtx.world);
                        n += WarmTextPagePipelines(font->Mat(i), mCtx.overlay);
                    }
                }
            }
        }
        return n;
    }

    const Contexts &mCtx;
    std::unordered_map<ObjectDir *, int> mDirs;
    std::vector<std::pair<ObjectDir *, int> > mOrder;
    std::vector<WorldReflection *> mReflections;
};

// Every WorldReflection a loaded dir has held, for as long as it lives (the
// list drops a deleted one).
ObjPtrList<WorldReflection> &LiveReflections() {
    static ObjPtrList<WorldReflection> *sList =
        new ObjPtrList<WorldReflection>(nullptr, kObjListNoNull);  // never destroyed
    return *sList;
}

} // namespace

void NativeWarmLoadedDir(ObjectDir *dir) {
    if (!dir || !gWgpuRnd || !gWgpuRnd->Gpu().IsReady()) return;
    const auto t0 = std::chrono::steady_clock::now();
    Contexts c;
    c.world = gWgpuRnd->WorldPassContext(false);
    c.reflection = gWgpuRnd->WorldPassContext(true);
    c.overlay = gWgpuRnd->OverlayPassContext();
    c.texture = gWgpuRnd->TexturePassContext(true);

    Warm warm(c);
    warm.Visit(dir, kWorld);
    int queued = warm.Queue();

    ObjPtrList<WorldReflection> &reflections = LiveReflections();
    for (WorldReflection *r : warm.Reflections())
        if (reflections.find(r) == reflections.end()) reflections.push_back(r);
    for (auto r = reflections.begin(); r != reflections.end(); ++r) {
        const ObjPtrList<RndDrawable> &draws = (*r)->Draws();
        for (auto d = draws.begin(); d != draws.end(); ++d)
            queued += warm.QueueDrawable(*d, c.reflection, 0);
    }
    if (queued > 0)
        fprintf(stderr, "[PIPEWARM] %s: %d keys queued (walk %.2f ms)\n",
                dir->Name() ? dir->Name() : "?", queued,
                std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0)
                    .count());
}
