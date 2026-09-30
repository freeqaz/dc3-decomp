// Native-only (HX_NATIVE) code that diverged from the Xbox image, pinned one
// divergence per test.  Branch `native-suspects`: the pass over the (b) ADDS
// regions the native-additions triage had judged from their code alone -- see
// "Suspect pass" in docs/decomp/patterns/native-shadow-bodies-are-unmeasured.md.
#include "test_helpers.h"

#include "obj/Dir.h"
#include "obj/Object.h"
#include "utl/BufStream.h"

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

} // namespace
