// milo-env-dump — environ ambient colour (alpha included) inventory, JSON lines.
//
// Usage: milo-env-dump <input.milo_xbox>...
//
// Loads each file through the decomp's own RndEnviron::Load, so a value means
// what the game reads. Lines:
//   {"kind":"env",...}   every RndEnviron: own ambient colour, the colour its
//                        AmbientColor() returns (the ambient_fog_owner's), owner name.
//   {"kind":"dir",...}   every RndDir with an environ: which environ it draws under.
//   {"kind":"mat",...}   every RndMat with use_environ on: blend, alpha, prelit.
//   {"kind":"anim",...}  every PropAnim key track whose target is a RndEnviron, or
//                        whose property path names ambient_*: keys as authored.
// Written for lane A77 (does any DC3 content use an environ ambient alpha < 1,
// the retail c1.w term).

#include "os/Debug.h"
#include "os/System.h"
#include "obj/Dir.h"
#include "obj/DirLoader.h"
#include "rndobj/Rnd.h"
#include "rndobj/Dir.h"
#include "rndobj/Env.h"
#include "rndobj/Mat.h"
#include "rndobj/Group.h"
#include "rndobj/Mesh.h"
#include "rndobj/PropAnim.h"
#include "rndobj/PropKeys.h"
#include "char/Char.h"
#include "world/World.h"
#include "hamobj/Ham.h"
#include "flow/Flow.h"
#include "utl/FilePath.h"
#include "ui/UIScreen.h"
#include "ui/UIPanel.h"
#include "ui/PanelDir.h"
#include "ui/UIComponent.h"
#include "ui/UIButton.h"
#include "ui/UIColor.h"
#include "ui/UILabel.h"
#include "ui/UIList.h"
#include "ui/UIPicture.h"
#include "ui/UISlider.h"
#include "ui/UITrigger.h"
#include "ui/InlineHelp.h"
#include "ui/UIFontImporter.h"
#include "ui/UIGuide.h"
#include "ui/LabelNumberTicker.h"
#include "ui/LabelShrinkWrapper.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <climits>
#include <string>
#include <unistd.h>

extern Rnd &TheRnd;
void SetFileChecksumData();

namespace {

struct EnvPeek : RndEnviron {
    static const EnvPeek &Of(const RndEnviron *e) { return *static_cast<const EnvPeek *>(e); }
    using RndEnviron::mAmbientColor;
    using RndEnviron::mAmbientFogOwner;
};

std::string Esc(const char *s) {
    std::string o;
    for (; s && *s; s++) {
        if (*s == '"' || *s == '\\') o += '\\';
        o += *s;
    }
    return o;
}

std::string Col(const Hmx::Color &c) {
    char b[160];
    snprintf(b, sizeof(b), "[%.6g,%.6g,%.6g,%.6g]", c.red, c.green, c.blue, c.alpha);
    return b;
}

void DumpAnim(const char *file, RndPropAnim *anim) {
    for (std::list<PropKeys *>::const_iterator k = anim->AllKeys().begin(); k != anim->AllKeys().end(); ++k) {
        PropKeys *keys = *k;
        if (!keys) continue;
        String prop;
        if (keys->Prop()) keys->Prop()->Print(prop, kDataArray, true, 0);
        Hmx::Object *target = keys->Target();
        const bool envTarget = dynamic_cast<RndEnviron *>(target) != nullptr;
        const bool ambientProp = strstr(prop.c_str(), "ambient") != nullptr;
        if (!envTarget && !ambientProp) continue;
        std::string o = "{\"kind\":\"anim\",\"file\":\"" + Esc(file) + "\",\"anim\":\"" + Esc(anim->Name())
            + "\",\"target\":\"" + Esc(target ? target->Name() : "") + "\",\"target_class\":\""
            + Esc(target ? target->ClassName().Str() : "") + "\",\"prop\":\"" + Esc(prop.c_str())
            + "\",\"type\":";
        char b[200];
        snprintf(b, sizeof(b), "%d", (int)keys->KeysType());
        o += b;
        o += ",\"keys\":[";
        if (keys->KeysType() == PropKeys::kFloat) {
            Keys<float, float> *fk = keys->AsFloatKeys();
            for (size_t i = 0; fk && i < fk->size(); i++) {
                snprintf(b, sizeof(b), "[%g,%g],", (*fk)[i].frame, (*fk)[i].value);
                o += b;
            }
        } else if (keys->KeysType() == PropKeys::kColor) {
            Keys<Hmx::Color, Hmx::Color> *ck = keys->AsColorKeys();
            for (size_t i = 0; ck && i < ck->size(); i++) {
                o += "[";
                snprintf(b, sizeof(b), "%g", (*ck)[i].frame);
                o += b;
                o += "," + Col((*ck)[i].value) + "],";
            }
        }
        if (o.back() == ',') o.pop_back();
        o += "]}";
        printf("%s\n", o.c_str());
    }
}

// Replays the draw order (RndDir::DrawShowing, RndGroup::DrawShowing): an
// environ in a draw list selects itself (RndEnviron::Draw) and stays current
// for the rest of that list and anything it draws; the tracker restores the
// previous one when the list ends. Prints which environ each mesh draws under.
void Walk(const char *file, const char *root, RndDrawable *d, RndEnviron *cur, int depth);

void WalkList(const char *file, const char *root, const std::vector<RndDrawable *> &draws,
              RndEnviron *cur, int depth) {
    for (size_t i = 0; i < draws.size(); i++) {
        RndDrawable *d = draws[i];
        if (!d) continue;
        if (RndEnviron *e = dynamic_cast<RndEnviron *>(d)) {
            cur = e;
            continue;
        }
        Walk(file, root, d, cur, depth);
    }
}

void Walk(const char *file, const char *root, RndDrawable *d, RndEnviron *cur, int depth) {
    if (depth > 12) return;
    if (RndGroup *g = dynamic_cast<RndGroup *>(d)) {
        WalkList(file, root, g->Draws(), cur, depth + 1);
        return;
    }
    if (RndDir *rd = dynamic_cast<RndDir *>(d)) {
        std::vector<RndDrawable *> v;
        for (int i = 0; i < rd->NumDraws(); i++) v.push_back(rd->GetDraw(i));
        WalkList(file, root, v, rd->GetEnv() ? rd->GetEnv() : cur, depth + 1);
        return;
    }
    if (!cur) return;
    RndMesh *mesh = dynamic_cast<RndMesh *>(d);
    RndMat *mat = mesh ? mesh->Mat() : nullptr;
    printf("{\"kind\":\"under\",\"file\":\"%s\",\"root\":\"%s\",\"env\":\"%s\",\"env_alpha\":%g,"
           "\"draw\":\"%s\",\"class\":\"%s\",\"mat\":\"%s\",\"use_environ\":%d,\"blend\":%d,"
           "\"mat_alpha\":%g}\n",
           Esc(file).c_str(), Esc(root).c_str(), Esc(cur->Name()).c_str(), cur->AmbientColor().alpha,
           Esc(d->Name()).c_str(), Esc(d->ClassName().Str()).c_str(), Esc(mat ? mat->Name() : "").c_str(),
           mat ? (mat->UseEnviron() ? 1 : 0) : -1, mat ? (int)mat->GetBlend() : -1,
           mat ? mat->Alpha() : -1.0f);
}

} // namespace

int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Usage: milo-env-dump <input.milo_xbox>...\n");
        return 1;
    }
    unsetenv("MILO_RENDER");
    setenv("MILO_NORENDER", "1", 1);

    InitMakeString();
    SetFileChecksumData();
    SystemPreInit(argc, argv, "config/ham_preinit_keep.dta");
    TheRnd.PreInit();
    SystemInit("config/ham_keep.dta");
    TheRnd.Init();
    FlowInit();
    CharInit();
    WorldInit();
    // The UI classes UIManager::Init registers (ui/UI.cpp), without the rest
    // of its start-up, so shell and HUD milos load as their own classes.
    REGISTER_OBJ_FACTORY(UIScreen)
    REGISTER_OBJ_FACTORY(UIPanel)
    REGISTER_OBJ_FACTORY(PanelDir)
    UIComponent::Init();
    UIButton::Init();
    REGISTER_OBJ_FACTORY(UIColor)
    UILabel::Init();
    UIList::Init();
    REGISTER_OBJ_FACTORY(UIPicture)
    UISlider::Init();
    REGISTER_OBJ_FACTORY(UITrigger)
    InlineHelp::Init();
    REGISTER_OBJ_FACTORY(UIFontImporter)
    REGISTER_OBJ_FACTORY(UIGuide)
    LabelNumberTicker::Init();
    LabelShrinkWrapper::Init();
    HamInit();

    for (int i = 1; i < argc; i++) {
        char absPath[PATH_MAX];
        if (!realpath(argv[i], absPath)) {
            fprintf(stderr, "cannot resolve '%s'\n", argv[i]);
            continue;
        }
        ObjDirPtr<ObjectDir> dir;
        FilePath fp(absPath);
        dir.LoadFile(fp, false, false, kLoadFront, false);
        ObjectDir *scene = dir;
        if (!scene) {
            fprintf(stderr, "failed to load '%s'\n", absPath);
            printf("{\"kind\":\"loadfail\",\"file\":\"%s\"}\n", Esc(argv[i]).c_str());
            continue;
        }
        int envs = 0, mats = 0, dirs = 0;
        for (ObjDirItr<RndEnviron> it(scene, true); it != nullptr; ++it) {
            const EnvPeek &e = EnvPeek::Of(it);
            const RndEnviron *owner = e.mAmbientFogOwner.Ptr();
            printf("{\"kind\":\"env\",\"file\":\"%s\",\"path\":\"%s\",\"name\":\"%s\",\"own_ambient\":%s,"
                   "\"ambient\":%s,\"owner\":\"%s\"}\n",
                   Esc(argv[i]).c_str(), Esc(PathName(it)).c_str(), Esc(it->Name()).c_str(),
                   Col(e.mAmbientColor).c_str(), Col(it->AmbientColor()).c_str(),
                   Esc(owner ? owner->Name() : "").c_str());
            envs++;
        }
        for (ObjDirItr<RndDir> it(scene, true); it != nullptr; ++it) {
            RndEnviron *env = it->GetEnv();
            if (!env) continue;
            printf("{\"kind\":\"dir\",\"file\":\"%s\",\"dir\":\"%s\",\"class\":\"%s\",\"env\":\"%s\","
                   "\"env_ambient\":%s}\n",
                   Esc(argv[i]).c_str(), Esc(it->Name()).c_str(), Esc(it->ClassName().Str()).c_str(),
                   Esc(env->Name()).c_str(), Col(env->AmbientColor()).c_str());
            dirs++;
        }
        for (ObjDirItr<RndMat> it(scene, true); it != nullptr; ++it) {
            if (!it->UseEnviron()) continue;
            printf("{\"kind\":\"mat\",\"file\":\"%s\",\"name\":\"%s\",\"blend\":%d,\"alpha\":%g,"
                   "\"prelit\":%d}\n",
                   Esc(argv[i]).c_str(), Esc(it->Name()).c_str(), (int)it->GetBlend(), it->Alpha(),
                   it->Prelit() ? 1 : 0);
            mats++;
        }
        if (RndDir *rootDir = dynamic_cast<RndDir *>(scene)) {
            Walk(argv[i], rootDir->Name(), rootDir, nullptr, 0);
        }
        for (ObjDirItr<RndDir> it(scene, true); it != nullptr; ++it) {
            if (&*it != scene) Walk(argv[i], it->Name(), it, nullptr, 0);
        }
        for (ObjDirItr<RndPropAnim> it(scene, true); it != nullptr; ++it) DumpAnim(argv[i], it);
        fprintf(stderr, "%s: %d environs, %d dirs-with-env, %d use_environ mats\n", argv[i], envs, dirs,
                mats);
    }
    fflush(stdout);
    _exit(0);
}
