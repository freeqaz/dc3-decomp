// Native dir-cascade guards retired by lane w23-cas (2026-10-08).
//
// Three HX_NATIVE guards existed only to tolerate states the native
// ~ObjectDir cascade (obj/Dir.cpp: Phase-0 NullifyAllRefs over the whole dir
// tree, Phase-1 destroy, Phase-2 deferred free) used to leave behind:
//
//   * SortDraws (rndobj/Utl.cpp) sorted NULL drawables last (a37908240):
//     NullifyObj nulled kObjListNoNull nodes in place, so TheRnd's pre-clear
//     draw list kept a NULL that the next PreClearDrawAddOrRemove sorted.
//     Since d376d7cc9 a NoNull node's NullifyObj unlinks and deletes itself.
//   * ~RndTransformable (rndobj/Trans.cpp) cleared mChildren instead of
//     running the image loop (2d85d15c5): Phase 0 NullifyObj'd a child's
//     mParent without SetTransParent, so the child later died without leaving
//     mChildren and the parent's loop wrote into it.  Since 73642d516 an
//     ObjOwnerPtr gets the image's Replace(nullptr) step -> SetTransParent.
//   * ~HamCamTransform (hamobj/HamCamTransform.cpp) skipped ClearOldCrowds
//     (365019af3): ~Object skipped ReplaceRefs, leaving mCamshots pointing at
//     destroyed shots.  Every in-cascade ~Object now nulls its ring.
//
// Each test drives the image path through a real dir cascade and asserts the
// invariant the guard was standing in for.

#include "test_helpers.h"
#include "obj/Dir.h"
#include "obj/Object.h"
#include "rndobj/Mesh.h"
#include "rndobj/Rnd.h"
#include "rndobj/Trans.h"
#include "hamobj/HamCamShot.h"
#include "hamobj/HamCamTransform.h"
#include "world/CameraShot.h"

class CascadeGuardsTest : public EngineTestFixture {};

namespace {
    ObjectDir *CascadeDir(const char *name) {
        ObjectDir *dir = Hmx::Object::New<ObjectDir>();
        dir->SetName(name, ObjectDir::Main());
        return dir;
    }

    // Checks, from the derived dtor (i.e. just before ~RndTransformable runs
    // the image's mChildren loop), that every child is a live transformable
    // still parented here -- the state the image loop writes through.
    int sBadChildren = 0;
    int sParentDtors = 0;
    class TransProbe : public RndTransformable {
    public:
        TransProbe() {}
        virtual ~TransProbe() {
            sParentDtors++;
            for (std::list<RndTransformable *>::const_iterator it = Children().begin();
                 it != Children().end(); ++it) {
                RndTransformable *c = *it;
                if (!c || !c->IsRefAlive() || c->TransParent() != this)
                    sBadChildren++;
            }
        }
    };

    class ShotProbe : public HamCamShot {
    public:
        ShotProbe() {}
        int NumCrowds() const { return mCrowds.size(); }
    };

    class CamTransformProbe : public HamCamTransform {
    public:
        CamTransformProbe() {}
        ObjVector<TransformArea> &Areas() { return mAreas; }
    };
}

// Parent and children in the dying dir, plus a child and a grandparent that
// survive.  Both name orders, so the hash iteration order varies.
static void CheckTransCascade(bool childrenFirst) {
    sBadChildren = 0;
    sParentDtors = 0;
    ObjectDir *dir = CascadeDir(childrenFirst ? "cg_trans_dir_b" : "cg_trans_dir_a");
    RndTransformable *grand = Hmx::Object::New<RndTransformable>();
    TransProbe *parent = new TransProbe();
    RndTransformable *inA = Hmx::Object::New<RndTransformable>();
    TransProbe *inB = new TransProbe(); // itself a parent of `deep`
    RndTransformable *deep = Hmx::Object::New<RndTransformable>();
    RndTransformable *outside = Hmx::Object::New<RndTransformable>();
    if (childrenFirst) {
        deep->SetName("deep.trans", dir);
        inB->SetName("in_b.trans", dir);
        inA->SetName("in_a.trans", dir);
        parent->SetName("parent.trans", dir);
    } else {
        parent->SetName("parent.trans", dir);
        inA->SetName("in_a.trans", dir);
        inB->SetName("in_b.trans", dir);
        deep->SetName("deep.trans", dir);
    }
    parent->SetTransParent(grand, false);
    inA->SetTransParent(parent, false);
    inB->SetTransParent(parent, false);
    deep->SetTransParent(inB, false);
    outside->SetTransParent(parent, false);
    ASSERT_EQ(parent->Children().size(), 3u);

    delete dir;

    EXPECT_EQ(sParentDtors, 2);
    EXPECT_EQ(sBadChildren, 0)
        << "a parent's mChildren named a dead or re-parented child when "
           "~RndTransformable ran its loop";
    EXPECT_EQ(outside->TransParent(), nullptr)
        << "the surviving child still names its destroyed parent";
    EXPECT_TRUE(grand->Children().empty())
        << "the destroyed parent is still in its surviving parent's mChildren";
    delete outside;
    delete grand;
}

TEST_F(CascadeGuardsTest, TransChildrenAreLiveWhenTheParentDiesParentNamedFirst) {
    CheckTransCascade(false);
}
TEST_F(CascadeGuardsTest, TransChildrenAreLiveWhenTheParentDiesChildrenNamedFirst) {
    CheckTransCascade(true);
}

// A drawable registered in TheRnd's pre-clear list dies in a cascade; the
// next registration sorts the list with SortDraws, which (as in the image)
// dereferences every entry.
TEST_F(CascadeGuardsTest, PreClearDrawListHoldsNoNullAfterACascade) {
    ObjectDir *dir = CascadeDir("cg_preclear_dir");
    RndMesh *dying = Hmx::Object::New<RndMesh>();
    dying->SetName("dying.mesh", dir);
    RndMesh *other = Hmx::Object::New<RndMesh>();
    other->SetName("other.mesh", ObjectDir::Main());
    TheRnd.PreClearDrawAddOrRemove(dying, true, false);
    TheRnd.PreClearDrawAddOrRemove(dying, true, true);
    delete dir;
    // Sorts both lists with SortDraws; a NULL node would be dereferenced.
    TheRnd.PreClearDrawAddOrRemove(other, true, false);
    TheRnd.PreClearDrawAddOrRemove(other, true, true);
    TheRnd.PreClearDrawAddOrRemove(other, false, false);
    TheRnd.PreClearDrawAddOrRemove(other, false, true);
    delete other;
}

// ~HamCamTransform runs ClearOldCrowds in a cascade, as the image does: a
// surviving area's surviving shot drops its NULL crowd entries, and a shot
// that died in the same cascade is never touched.
TEST_F(CascadeGuardsTest, CamTransformClearsSurvivingShotCrowdsInACascade) {
    ObjectDir *dir = CascadeDir("cg_camxfm_dir");
    CamTransformProbe *xfm = new CamTransformProbe();
    xfm->SetName("areas.camxfm", dir);
    RndTransformable *area = Hmx::Object::New<RndTransformable>();
    ShotProbe *dyingShot = new ShotProbe();
    dyingShot->SetName("dying.shot", dir);
    ShotProbe *liveShot = new ShotProbe();
    CamShotCrowd empty(liveShot);
    liveShot->AddCrowd(empty);
    ASSERT_EQ(liveShot->NumCrowds(), 1);

    xfm->Areas().push_back(TransformArea(xfm));
    TransformArea &ta = xfm->Areas().back();
    ta.mArea = area;
    ta.mCamshots.push_back(dyingShot);
    ta.mCamshots.push_back(liveShot);

    delete dir;

    EXPECT_EQ(liveShot->NumCrowds(), 0)
        << "~HamCamTransform did not run ClearOldCrowds in the cascade";
    delete liveShot;
    delete area;
}
