// test_pipeline_warm_classes.cpp -- the load-time pipeline warm asks each
// dynamic type its classes once (np-gfx spotperf round 7, branch
// np-framecost7, PLAN H-118; native/src/platform/PipelineWarm.{h,cpp}).
//
// The warm walk (H-104) asked every object which of ~16 classes it is with a
// cascade of dynamic_casts, and at song end each loaded dir re-walks every
// live reflection's draws: 3-5 ms of each loader frame, almost all in
// __dynamic_cast, for keys that were already warm. NativeWarmClassOf asks a
// type once and keeps the answers. This pins:
//  - the bits of each class the walk asks about are exactly what dynamic_cast
//    answers for an object of it (checked here against the casts themselves,
//    for the 16 classes and for two the walk asks about but must not match: a
//    material and a texture), so the cascades still take the same branches;
//  - a second object of a type, and the same object again, ask nothing (no
//    new type classified);
//  - null is no class.
// test_pipeline_warm.cpp still pins the walk end to end (a loaded dir draws
// with no pipeline create).
//
// Sabotage (dc3-decomp archive/np-gfx-2026-11-14/lanes/spotperf/round7):
//  - the cache bypassed (every call asks and counts) -> red;
//  - one class's bit taken from a neighbour's cast (RndRibbon's from
//    HamRibbon) -> red.

#include "test_helpers.h"

#include "char/Character.h"
#include "hamobj/CharFeedback.h"
#include "hamobj/HamRibbon.h"
#include "obj/Dir.h"
#include "platform/PipelineWarm.h"
#include "rndobj/Flare.h"
#include "rndobj/Font.h"
#include "rndobj/Group.h"
#include "rndobj/Mat.h"
#include "rndobj/Mesh.h"
#include "rndobj/Part.h"
#include "rndobj/PropAnim.h"
#include "rndobj/Ribbon.h"
#include "rndobj/ScreenMask.h"
#include "rndobj/Tex.h"
#include "rndobj/TexBlender.h"
#include "rndobj/Text.h"
#include "ui/PanelDir.h"
#include "world/Reflection.h"

#include <string>
#include <vector>

namespace {

class PipelineWarmClassesTest : public EngineTestFixture {};

// What the walk's casts answer for `obj`, written out cast by cast.
uint32_t ByCasts(Hmx::Object *obj) {
    uint32_t c = 0;
    c |= dynamic_cast<RndMesh *>(obj) ? kWarmIsMesh : 0;
    c |= dynamic_cast<RndText *>(obj) ? kWarmIsText : 0;
    c |= dynamic_cast<HamRibbon *>(obj) ? kWarmIsHamRibbon : 0;
    c |= dynamic_cast<RndRibbon *>(obj) ? kWarmIsRibbon : 0;
    c |= dynamic_cast<CharFeedback *>(obj) ? kWarmIsCharFeedback : 0;
    c |= dynamic_cast<RndFlare *>(obj) ? kWarmIsFlare : 0;
    c |= dynamic_cast<RndScreenMask *>(obj) ? kWarmIsScreenMask : 0;
    c |= dynamic_cast<RndTexBlender *>(obj) ? kWarmIsTexBlender : 0;
    c |= dynamic_cast<RndParticleSys *>(obj) ? kWarmIsParticleSys : 0;
    c |= dynamic_cast<RndFont *>(obj) ? kWarmIsFont : 0;
    c |= dynamic_cast<RndPropAnim *>(obj) ? kWarmIsPropAnim : 0;
    c |= dynamic_cast<WorldReflection *>(obj) ? kWarmIsReflection : 0;
    c |= dynamic_cast<ObjectDir *>(obj) ? kWarmIsDir : 0;
    c |= dynamic_cast<RndGroup *>(obj) ? kWarmIsGroup : 0;
    c |= dynamic_cast<PanelDir *>(obj) ? kWarmIsPanelDir : 0;
    c |= dynamic_cast<Character *>(obj) ? kWarmIsCharacter : 0;
    return c;
}

struct Made {
    std::string name;
    uint32_t expect;  // the class bit an object of it must carry (0: none)
    Hmx::Object *a, *b;
};

template <class T> Made Make(const char *name, uint32_t expect) {
    return Made{ name, expect, Hmx::Object::New<T>(), Hmx::Object::New<T>() };
}

} // namespace

TEST_F(PipelineWarmClassesTest, EachTypeIsAskedOnceAndAnswersAsItsCasts) {
    if (IsSkipped()) return;
    std::vector<Made> made = {
        Make<RndMesh>("RndMesh", kWarmIsMesh),
        Make<RndText>("RndText", kWarmIsText),
        Make<HamRibbon>("HamRibbon", kWarmIsHamRibbon),
        Make<RndRibbon>("RndRibbon", kWarmIsRibbon),
        Make<CharFeedback>("CharFeedback", kWarmIsCharFeedback),
        Make<RndFlare>("RndFlare", kWarmIsFlare),
        Make<RndScreenMask>("RndScreenMask", kWarmIsScreenMask),
        Make<RndTexBlender>("RndTexBlender", kWarmIsTexBlender),
        Make<RndParticleSys>("RndParticleSys", kWarmIsParticleSys),
        Make<RndFont>("RndFont", kWarmIsFont),
        Make<RndPropAnim>("RndPropAnim", kWarmIsPropAnim),
        Make<WorldReflection>("WorldReflection", kWarmIsReflection),
        Make<ObjectDir>("ObjectDir", kWarmIsDir),
        Make<RndGroup>("RndGroup", kWarmIsGroup),
        Make<PanelDir>("PanelDir", kWarmIsPanelDir | kWarmIsDir),
        Make<Character>("Character", kWarmIsCharacter | kWarmIsDir),
        Make<RndMat>("RndMat", 0),
        Make<RndTex>("RndTex", 0),
    };
    for (const Made &m : made) {
        SCOPED_TRACE(m.name);
        ASSERT_NE(m.a, nullptr);
        ASSERT_NE(m.b, nullptr);
        const uint32_t first = NativeWarmClassOf(m.a);
        EXPECT_EQ(first, ByCasts(m.a));
        EXPECT_EQ(first & m.expect, m.expect) << "the object is not its own class";
        if (!m.expect) EXPECT_EQ(first, 0u) << "a material or texture matched a class the walk asks about";
        // Asked: a second object of the type, and the first again, ask nothing.
        const int asked = NativeWarmTypesClassified();
        EXPECT_EQ(NativeWarmClassOf(m.b), first);
        EXPECT_EQ(NativeWarmClassOf(m.a), first);
        EXPECT_EQ(NativeWarmTypesClassified(), asked) << "a type already asked was asked again";
    }
    EXPECT_EQ(NativeWarmClassOf(nullptr), 0u);
    for (Made &m : made) {
        delete m.b;
        delete m.a;
    }
}
