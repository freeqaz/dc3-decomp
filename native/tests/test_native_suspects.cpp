// Native-only (HX_NATIVE) code that diverged from the Xbox image, pinned one
// divergence per test.  Branch `native-suspects`: the pass over the (b) ADDS
// regions the native-additions triage had judged from their code alone -- see
// "Suspect pass" in docs/decomp/patterns/native-shadow-bodies-are-unmeasured.md.
#include "test_helpers.h"

#include "meta_ham/HamUI.h"
#include "meta_ham/PassiveMessenger.h"
#include "meta_ham/SkeletonIdentifier.h"
#include "obj/Dir.h"
#include "obj/Object.h"
#include "platform/FFmpegMovieImpl.h"
#include "platform/MeshDrawShowing.h"
#include "rndobj/Mat.h"
#include "rndobj/Mesh.h"
#include "utl/BufStream.h"
#include "world/Crowd3DCharHandle.h"
#include "world/Spotlight.h"

#include <cstring>
#include <vector>

namespace {

class NativeSuspectsTest : public EngineTestFixture {};

// A BinStream holding what ObjPtr / ObjPtrList / ObjPtrVec serialise: an
// optional count, then length-prefixed names.
struct NameStream {
    std::vector<char> mBytes;
    BufStream *mStream = nullptr;
    NameStream(std::initializer_list<const char *> names, bool withCount) {
        mBytes.resize(512);
        BufStream out(mBytes.data(), (int)mBytes.size(), true);
        if (withCount)
            out << (int)names.size();
        for (const char *n : names)
            out << n;
        mStream = new BufStream(mBytes.data(), (int)mBytes.size(), true);
    }
    ~NameStream() { delete mStream; }
    BinStream &bs() { return *mStream; }
};

// ----------------------------------------------------------------------------
// ObjRefConcrete::Load / ObjPtrVec::Load / ObjPtrList::Load
// ----------------------------------------------------------------------------

// Every ObjRefConcrete / ObjPtrVec / ObjPtrList ::Load instantiation is 100%
// matched: the image resolves a name with dir->FindObject(name, false, true) --
// this dir and its subdirs, never a parent -- and only when the ref has an
// owner.  Native added (1) a walk up Dir() / the loader's ParentDir and then
// Main(), and (2) owner-less resolution against an explicit dir.  Both bind
// objects the image leaves null.
struct DirFixture {
    ObjectDir *parent;
    ObjectDir *child;
    Hmx::Object *owner;
    Hmx::Object *inParent;
    Hmx::Object *inChild;
    DirFixture() {
        parent = Hmx::Object::New<ObjectDir>();
        parent->SetName("suspects_parent", ObjectDir::Main());
        child = Hmx::Object::New<ObjectDir>();
        child->SetName("suspects_child", parent); // child->Dir() == parent
        owner = child->New<Hmx::Object>("suspects_owner.obj");
        inParent = parent->New<Hmx::Object>("suspects_only_in_parent.obj");
        inChild = child->New<Hmx::Object>("suspects_in_child.obj");
    }
    ~DirFixture() {
        delete child;
        delete parent;
    }
};

TEST_F(NativeSuspectsTest, ObjPtrLoadDoesNotSearchParentDirs) {
    DirFixture f;
    ASSERT_EQ(f.child->Dir(), f.parent);

    {
        NameStream s({ "suspects_in_child.obj" }, false);
        ObjPtr<Hmx::Object> p(f.owner);
        EXPECT_TRUE(p.Load(s.bs(), false, nullptr));
        EXPECT_EQ(p.Ptr(), f.inChild) << "control: a name in the owner's dir binds";
    }
    {
        NameStream s({ "suspects_only_in_parent.obj" }, false);
        ObjPtr<Hmx::Object> p(f.owner);
        bool found = p.Load(s.bs(), false, nullptr);
        EXPECT_EQ(p.Ptr(), nullptr)
            << "ObjPtr::Load bound a name that lives only in the owner dir's PARENT. "
               "The image (ObjRefConcrete::Load, 100% matched) calls "
               "dir->FindObject(name, false, true) and leaves the ref null.";
        EXPECT_FALSE(found);
    }
}

TEST_F(NativeSuspectsTest, ObjPtrVecAndListLoadDoNotSearchParentDirs) {
    DirFixture f;
    {
        NameStream s({ "suspects_in_child.obj", "suspects_only_in_parent.obj" }, true);
        ObjPtrVec<Hmx::Object> v(f.owner, (EraseMode)0, kObjListAllowNull);
        EXPECT_FALSE(v.Load(s.bs(), false, nullptr));
        // The image skips an unresolved non-empty name (ret = false, no
        // push), whatever the list mode.
        ASSERT_EQ(v.size(), 1u)
            << "ObjPtrVec::Load bound a parent-dir object the image leaves out";
        EXPECT_EQ(v[0], f.inChild) << "control";
    }
    {
        NameStream s({ "suspects_in_child.obj", "suspects_only_in_parent.obj" }, true);
        ObjPtrList<Hmx::Object> l(f.owner, kObjListNoNull);
        EXPECT_FALSE(l.Load(s.bs(), false, nullptr, true));
        ASSERT_EQ(l.size(), 1u)
            << "ObjPtrList::Load bound a parent-dir object the image leaves out";
        EXPECT_EQ(l.front(), f.inChild) << "control";
    }
}

TEST_F(NativeSuspectsTest, OwnerlessObjPtrLoadResolvesNothing) {
    DirFixture f;
    NameStream s({ "suspects_in_child.obj" }, false);
    ObjPtr<Hmx::Object> p(nullptr);
    EXPECT_TRUE(p.Load(s.bs(), false, f.child));
    EXPECT_EQ(p.Ptr(), nullptr)
        << "an owner-less ObjPtr resolved against an explicit dir.  The image's "
           "test is `refOwner && dir`: with no owner the ref stays null.";
}

// ----------------------------------------------------------------------------
// Spotlight::BuildBoard
// ----------------------------------------------------------------------------

// Spotlight::Init (8282DDF8 bl ?BuildBoard) builds the shared unit quad every
// lens disk and floor spot is drawn with: New<RndMesh> (8282BFF8), stored to
// sDiskMesh (8282BFFC), 4 verts / 2 faces.  Native returned at the top ("no
// renderer") and left sDiskMesh null, so Spotlight::DrawShowing's lens and
// floor-spot arms, MakeWorldSphere and SpotlightDrawer::DrawLenses had nothing
// to draw with (and dereference it: MILO_ASSERT does not stop natively).
TEST_F(NativeSuspectsTest, SpotlightInitBuildsTheDiskMesh) {
    RndMesh *disk = Spotlight::GetDiskMesh();
    ASSERT_NE(disk, nullptr)
        << "Spotlight::Init left sDiskMesh null: native BuildBoard returned before "
           "building the lens/floor-spot quad the image builds.";
    EXPECT_EQ(disk->Verts().size(), 4u);
    EXPECT_EQ(disk->Faces().size(), 2u);
    EXPECT_FLOAT_EQ(disk->Verts()[3].pos.x, 0.5f);
    EXPECT_FLOAT_EQ(disk->Verts()[3].pos.y, 0.5f);
}

// ----------------------------------------------------------------------------
// HamUI::IsTimelineResetAllowed
// ----------------------------------------------------------------------------

// UIManager::Poll restarts the UI timer and UI seconds on a screen transition
// only when IsTimelineResetAllowed().  The image's HamUI body refuses while a
// passive message is queued or recently dismissed, while skeleton
// identification is in progress, or while the help bar's write icon shows /
// animates.  Native returned true whenever ThePassiveMessenger OR
// TheSkeletonIdentifier was null -- and TheSkeletonIdentifier is ALWAYS null
// natively (no Kinect; native ShellInput::Init skips it) while
// ThePassiveMessenger is created (CursorPanel is a PassiveMessagesPanel), so
// every other term was skipped on every transition.
TEST_F(NativeSuspectsTest, TimelineResetRefusedWhileAPassiveMessageIsQueued) {
    ASSERT_EQ(TheSkeletonIdentifier, nullptr) << "precondition: no Kinect natively";
    Hmx::Object *callback = new Hmx::Object();
    PassiveMessenger *made = nullptr;
    if (!ThePassiveMessenger)
        made = new PassiveMessenger(callback);
    ASSERT_NE(ThePassiveMessenger, nullptr);
    ThePassiveMessenger->TriggerStringMsg(
        String("suspects test message"), Symbol("none"), kPassiveMessageGeneral,
        gNullStr, 0
    );
    ASSERT_TRUE(ThePassiveMessenger->HasMessages());
    EXPECT_FALSE(TheHamUI.IsTimelineResetAllowed())
        << "a UI timeline reset was allowed with a passive message queued: native "
           "returned true because TheSkeletonIdentifier is null, skipping the "
           "image's HasMessages / HasRecentlyDismissedMessage / help-bar terms";
    delete made;
    delete callback;
}

// ----------------------------------------------------------------------------
// FFmpegMovieImpl::Ready  (and MoviePanel::IsLoaded)
// ----------------------------------------------------------------------------

// The image's BinkMovieImpl::Ready (82E221C8) answers "is an async load
// pending": mLoader->IsLoaded(), else mMovieLoader->IsLoaded(), else true.
// FFmpegMovieImpl opens synchronously -- there is never a pending load -- but
// Ready() returned mReady, false until a SUCCESSFUL open.  No .bik ships, so it
// was false forever, and MoviePanel::IsLoaded carried a native block that
// skipped the Ready() term (and with it the image's mSubtitlesLoader wait).
TEST_F(NativeSuspectsTest, MovieWithNoPendingLoadIsReady) {
    FFmpegMovieImpl movie;
    EXPECT_TRUE(movie.Ready())
        << "a movie that never began reported not-ready; the image answers true "
           "when no loader is pending";
    bool ok = movie.BeginFromFile(
        "/nonexistent/suspects/video.bik", 1.0f, false, false, false, false, 0,
        nullptr, kLoadFront
    );
    EXPECT_FALSE(ok) << "control: the file does not exist";
    EXPECT_TRUE(movie.Ready())
        << "after a synchronous (failed) open nothing is loading; the image's "
           "Ready() is true once its loader has finished";
}

// ----------------------------------------------------------------------------
// WorldCrowd3DCharHandle::SyncProperty
// ----------------------------------------------------------------------------

// The image's vtable slot is the ICF-folded empty BEGIN_PROPSYNCS body
// (82711F80 -> 827118E0: `_i == _prop->Size()` returns true -- the path
// resolved to this object itself -- anything else returns false).  Native
// returned false unconditionally ("TODO: property synchronization").
TEST_F(NativeSuspectsTest, CrowdCharHandleSyncPropertyResolvesTheEmptyPath) {
    WorldCrowd3DCharHandle *handle = Hmx::Object::New<WorldCrowd3DCharHandle>();
    DataArray *prop = new DataArray(1);
    prop->Node(0) = Symbol("suspects_prop");
    DataNode val;
    EXPECT_TRUE(handle->SyncProperty(val, prop, 1, kPropGet))
        << "at _i == Size() the image's SyncProperty returns true";
    EXPECT_FALSE(handle->SyncProperty(val, prop, 0, kPropGet))
        << "control: an unknown property name is not handled";
    prop->Release();
    delete handle;
}

// ----------------------------------------------------------------------------
// RndMesh::DrawShowing (native: milo-native-engine src/platform/Mesh_Wgpu.cpp)
// ----------------------------------------------------------------------------

// The image's override is DxMesh::DrawShowing (826229B0, 100% matched in
// src/system/rnddx9/Mesh.cpp).  Its only refusal is `!geom->CanDraw()` -- no
// GPU buffers and not mutable.  It never tests Showing(): RndDrawable::Draw()
// is the showing gate, and every caller that invokes DrawShowing() directly
// (UIListMeshElement::Draw on a list's hidden template mesh, RndText, RndLine,
// RndRibbon, RndMultiMeshProxy, CharFeedback, ...) draws the mesh regardless.
// It never tests the name either: which LOD a Character draws is decided by
// Character::DrawShowing / DrawLodOrShadow from its mLods groups, and its
// shadow pass (DrawLodOrShadow drawMode 4 -> mShadow.Draw()) deliberately draws
// the *_lod meshes.  Native refused both a hidden named mesh and every mesh
// whose name contains "_lod".
struct DrawShowingFixture {
    ObjectDir *dir;
    RndMat *mat;
    DrawShowingFixture() {
        dir = Hmx::Object::New<ObjectDir>();
        dir->SetName("suspects_drawshowing", ObjectDir::Main());
        mat = dir->New<RndMat>("suspects_drawshowing.mat");
    }
    ~DrawShowingFixture() { delete dir; }
    RndMesh *Mesh(const char *name, bool showing) {
        RndMesh *mesh = dir->New<RndMesh>(name);
        mesh->SetMat(mat);
        mesh->SetShowing(showing);
        return mesh;
    }
};

TEST_F(NativeSuspectsTest, MeshDrawShowingDrawsAHiddenNamedMesh) {
    DrawShowingFixture f;
    // The shape UIListMeshElement::Draw hands it: list_choose_mode.milo's
    // template meshes are hidden in the file and drawn once per list element.
    RndMesh *hidden = f.Mesh("suspects_row_template.mesh", false);
    const char *skip = RndMeshDrawShowingSkip(hidden);
    EXPECT_EQ(nullptr, skip) << "refused a hidden named mesh: " << (skip ? skip : "");
    EXPECT_EQ(nullptr, RndMeshDrawShowingSkip(f.Mesh("suspects_shown.mesh", true)))
        << "control: a showing mesh with a material is drawn";
    // DxMesh::DrawShowing (rnddx9/Mesh.cpp) hands a null material straight to
    // RndShader::SelectConfig, whose Select swaps in TheRnd.DefaultMat(), so a
    // material-less mesh is drawn too. (This used to be the "can refuse"
    // control, which only held while native skipped such meshes.)
    RndMesh *noMat = f.Mesh("suspects_no_mat.mesh", true);
    noMat->SetMat(nullptr);
    skip = RndMeshDrawShowingSkip(noMat);
    EXPECT_EQ(nullptr, skip) << "refused a mesh with no material: " << (skip ? skip : "");
    EXPECT_NE(nullptr, RndMeshDrawShowingSkip(f.Mesh("grid_80by60_cube.mesh", true)))
        << "control: the predicate can refuse (consumer content filter, Kinect depth grid)";
}

TEST_F(NativeSuspectsTest, MeshDrawShowingDrawsLodNamedMeshes) {
    DrawShowingFixture f;
    // rasa05_lod.mesh and friends are in rasa05's LOD-1 group and in its
    // mShadow list; emilia01's emilia_head_lod1.1.mesh is in LOD group 0 -- the
    // full-detail group.  The name says nothing about whether it is drawn.
    for (const char *name :
         {"rasa05_lod.mesh", "rasa05_lod.3.mesh", "emilia_head_lod1.1.mesh"}) {
        const char *skip = RndMeshDrawShowingSkip(f.Mesh(name, true));
        EXPECT_EQ(nullptr, skip) << name << " refused: " << (skip ? skip : "");
    }
    EXPECT_NE(nullptr, RndMeshDrawShowingSkip(f.Mesh("grid_80by60_cube.mesh", true)))
        << "control: a consumer content filter still applies (Kinect depth grid)";
}

} // namespace
