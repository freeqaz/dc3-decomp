// Native-shadow regression tests: decomp bodies that the native build
// compiled OUT (`#ifndef HX_NATIVE` with no else, or an empty `#ifdef
// HX_NATIVE` stub), so the native game ran an empty function where the Xbox
// runs real code. Companion to test_native_shadow.cpp; same rules -- each
// test was watched FAILING against the pre-fix native body (see the commit
// that introduced it).

#include "test_helpers.h"

#include "math/Key.h"
#include "math/Mtx.h"
#include "obj/Object.h"
#include "obj/Task.h"
#include "rndobj/Mesh.h"
#include "rndobj/Ribbon.h"
#include "rndobj/Trans.h"

#include <cmath>

namespace {

class NativeShadowBodiesTest : public EngineTestFixture {};

// ---------------------------------------------------------------------------
// RndRibbon::ConstructMesh / UpdateMesh / UpdateChase were wrapped whole in
// `#ifndef HX_NATIVE` (82075c4ea deleted the native spellings instead of
// porting the int-cast pointer arithmetic), so on native a ribbon never built
// its tube mesh, never laid out its verts and never recorded its trail.
// Image: Ribbon.s ConstructMesh 8271621C bl VertVector::resize(segs*sides*2),
// 82716270 vector<Face>::erase / 8271628C _M_fill_insert to segs*sides*2
// faces, face loop 827162C4..82716358, 82716374 bl RndMesh::Sync(0x3f).
// ---------------------------------------------------------------------------
class ProbeRibbon : public RndRibbon {
public:
    ProbeRibbon() {}
    RndMesh *Mesh() const { return mMesh; }
    void Configure(int sides, int segs, float width, float decay, bool taper) {
        mNumSides = sides;
        mNumSegments = segs;
        mWidth = width;
        mDecay = decay;
        mTaper = taper;
    }
    Keys<Transform, Transform> &Trail() { return mTransforms; }
    void Follow(RndTransformable *t) { mFollowA = t; }
    float LastTime() const { return mLastTime; }
};

void ExpectFace(RndMesh *mesh, int idx, int a, int b, int c) {
    const RndMesh::Face &f = mesh->Faces()[idx];
    EXPECT_EQ(f.v1, a) << "face " << idx << " v1";
    EXPECT_EQ(f.v2, b) << "face " << idx << " v2";
    EXPECT_EQ(f.v3, c) << "face " << idx << " v3";
}

void ExpectVec(const Vector3 &v, float x, float y, float z, const char *what) {
    EXPECT_NEAR(v.x, x, 1e-4f) << what << ".x";
    EXPECT_NEAR(v.y, y, 1e-4f) << what << ".y";
    EXPECT_NEAR(v.z, z, 1e-4f) << what << ".z";
}

TEST_F(NativeShadowBodiesTest, RibbonConstructMeshBuildsTheTube) {
    ProbeRibbon *r = new ProbeRibbon();
    r->Configure(3, 2, 2.0f, 4.0f, false);
    r->ConstructMesh();

    RndMesh *mesh = r->Mesh();
    // VertVector::resize(sides * segs * 2), faces resized to the same count.
    ASSERT_EQ(mesh->Verts().size(), 12) << "ConstructMesh did not size the verts";
    ASSERT_EQ((int)mesh->Faces().size(), 12) << "ConstructMesh did not size the faces";

    // For segment s, side i: base = 2*ns*s, v = base+i, n = (i+1)%ns + base.
    // Face 2(base/2+i)   = { v,     n,     n+ns }
    // Face 2(base/2+i)+1 = { n+ns,  v+ns,  v    }
    ExpectFace(mesh, 0, 0, 1, 4);
    ExpectFace(mesh, 1, 4, 3, 0);
    ExpectFace(mesh, 2, 1, 2, 5);
    ExpectFace(mesh, 3, 5, 4, 1);
    ExpectFace(mesh, 4, 2, 0, 3); // wrap: (2+1)%3 = 0
    ExpectFace(mesh, 5, 3, 5, 2);
    ExpectFace(mesh, 6, 6, 7, 10); // segment 1, base 6
    ExpectFace(mesh, 7, 10, 9, 6);
    ExpectFace(mesh, 10, 8, 6, 9);
    ExpectFace(mesh, 11, 9, 11, 8);

    // Shrinking erases the tail faces (the image's erase arm).
    r->Configure(3, 1, 2.0f, 4.0f, false);
    r->ConstructMesh();
    EXPECT_EQ(mesh->Verts().size(), 6);
    EXPECT_EQ((int)mesh->Faces().size(), 6);
    ExpectFace(mesh, 4, 2, 0, 3);

    // Zero segments is an early return: nothing is touched.
    r->Configure(3, 0, 2.0f, 4.0f, false);
    r->ConstructMesh();
    EXPECT_EQ(mesh->Verts().size(), 6);
    delete r;
}

// UpdateMesh (image 827143D0 cos / 827143DC sin, 82714518 Sync(0x1f)): for
// seg s, side i, row r the vert at ns*r + 2*ns*s + i sits on the ring around
// trail key min(r+s, last): pos = key * (sin a, 0, cos a) * halfWidth*taper,
// a = i*2pi/ns; its normal is row 0's (pos - key.v) normalised; tex is
// (1 - (latest - key.frame)/decay, i/ns).
TEST_F(NativeShadowBodiesTest, RibbonUpdateMeshLaysVertsOnTheTrail) {
    ProbeRibbon *r = new ProbeRibbon();
    r->Configure(3, 2, 2.0f, 4.0f, false);
    // Size the verts directly so this test observes UpdateMesh/UpdateChase
    // on their own, independent of ConstructMesh.
    r->Mesh()->Verts().resize(12);
    ASSERT_EQ(r->Mesh()->Verts().size(), 12);

    Transform k0 = Transform::IDXfm();
    Transform k1 = Transform::IDXfm();
    k1.v.Set(0.0f, 10.0f, 0.0f);
    r->Trail().push_back(Key<Transform>(k0, 0.0f));
    r->Trail().push_back(Key<Transform>(k1, 2.0f));
    r->UpdateMesh();

    RndMesh::VertVector &v = r->Mesh()->Verts();
    const float s = std::sin(6.2831855f / 3.0f), c = std::cos(6.2831855f / 3.0f);
    ExpectVec(v[0].pos, 0, 0, 1, "vert0.pos");
    ExpectVec(v[0].norm, 0, 0, 1, "vert0.norm");
    EXPECT_NEAR(v[0].tex.x, 0.5f, 1e-5f);
    EXPECT_NEAR(v[0].tex.y, 0.0f, 1e-5f);
    ExpectVec(v[3].pos, 0, 10, 1, "vert3.pos");
    EXPECT_NEAR(v[3].tex.x, 1.0f, 1e-5f);
    // seg 1, side 1: row 0 on key 1, row 1 clamps key 2 -> key 1.
    ExpectVec(v[7].pos, s, 10, c, "vert7.pos");
    ExpectVec(v[10].pos, s, 10, c, "vert10.pos");
    ExpectVec(v[10].norm, s, 0, c, "vert10.norm (row 0's normal)");
    EXPECT_NEAR(v[10].tex.y, 1.0f / 3.0f, 1e-5f);

    // Taper scales the radius by 1 - (latest - frame)/decay.
    r->Configure(3, 2, 2.0f, 4.0f, true);
    r->UpdateMesh();
    ExpectVec(v[0].pos, 0, 0, 0.5f, "tapered vert0.pos");
    ExpectVec(v[0].norm, 0, 0, 1, "tapered vert0.norm");
    delete r;
}

// UpdateChase (image 82715760 TaskMgr::Seconds(kRealTime); first key pushed
// at 82715948 with frame = now and v = the follow point; 82715D88 bl
// UpdateMesh; 82715D8C stfs f19 -> mLastTime).
TEST_F(NativeShadowBodiesTest, RibbonUpdateChaseRecordsTheFollowPoint) {
    ProbeRibbon *r = new ProbeRibbon();
    r->Configure(3, 2, 2.0f, 4.0f, false);
    // Size the verts directly so this test observes UpdateMesh/UpdateChase
    // on their own, independent of ConstructMesh.
    r->Mesh()->Verts().resize(12);
    ASSERT_EQ(r->Mesh()->Verts().size(), 12);

    // No follow target: early return, nothing recorded.
    r->UpdateChase();
    EXPECT_EQ((int)r->Trail().size(), 0);
    EXPECT_EQ(r->LastTime(), -1.0f);

    RndTransformable *t = Hmx::Object::New<RndTransformable>();
    t->SetLocalPos(Vector3(1.0f, 2.0f, 3.0f));
    r->Follow(t);
    r->UpdateChase();
    float now = TheTaskMgr.Seconds(TaskMgr::kRealTime);
    ASSERT_EQ((int)r->Trail().size(), 1) << "UpdateChase recorded no trail key";
    EXPECT_EQ(r->Trail()[0].frame, now);
    ExpectVec(r->Trail()[0].value.v, 1, 2, 3, "trail[0].v");
    EXPECT_EQ(r->LastTime(), now);
    // ...and it re-laid the mesh around that key.
    ExpectVec(r->Mesh()->Verts()[0].pos, 1, 2, 4, "vert0.pos");

    r->Follow(nullptr);
    delete r;
    delete t;
}

} // namespace
