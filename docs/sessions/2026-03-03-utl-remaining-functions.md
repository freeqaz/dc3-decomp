# Session: Utl.cpp Remaining Functions (2026-03-03)

## Overview

Four functions remain in `src/system/rndobj/Utl.cpp`:
1. **TessellateMesh** — subdivide all mesh faces by inserting edge midpoints
2. **BuildVisit** — recursive BSP tree traversal, building polygons from BSP planes
3. **BuildFromBSP** — top-level: call BuildVisit, then convert polygon list to mesh verts/faces
4. **UtilDrawCigar** — debug draw helper (10.3% match, likely very hard)

AmbientOcclusion.h/cpp also needs:
- `Edge::midpoint` field (short, at offset +0x4)
- `Edge::operator<` implementation (undirected comparison)
- `BlendVert` static method (average two vertices, zero color)

## Global State

Already declared in Utl.cpp at lines 50-51:
```cpp
std::list<BuildPoly> gChildPolys;  // at DAT_830a1990 in Ghidra
std::list<BuildPoly> gParentPolys; // at DAT_830a1998 in Ghidra
```

`BuildPoly` (from Utl.h):
```cpp
struct BuildPoly {
    Hmx::Polygon mPoly;    // 0x0 — has std::vector<Vector2> points
    Transform mTransform;  // 0xc — 3x3 matrix + translation vector
};
```

## Function 1: TessellateMesh

### What it does
Takes a mesh, subdivides every triangle face into 4 triangles by splitting each edge at its midpoint. Uses a `std::set<Edge>` to track which edges have already been split (so shared edges only create one midpoint vertex).

### Algorithm from Ghidra

```
TessellateMesh(RndMesh *mesh):
    set<Edge> edges;
    vector<Face> newFaces;
    vector<Vert> newVerts;

    RndMesh *geomOwner = mesh->mGeomOwner;  // offset 0x148 in Ghidra = 0x13c + padding?
    // Actually: mesh is RndDrawable(0xB0) + RndTransformable(0x50) = 0x100 base
    // mGeomOwner is at 0x13c in RndMesh. But Ghidra shows 0x148...
    // Wait: RndMesh inherits RndDrawable AND RndTransformable (multiple inheritance)
    // The this pointer adjustment means mGeomOwner could be at different offset from
    // the RndTransformable vtable pointer. Let me just use the C++ API.

    // Reserve space
    newFaces.reserve(mesh->Faces().size() * 4);
    newVerts.reserve(mesh->NumVerts() * 3);

    int nextVert = mesh->NumVerts();  // index for next new vertex

    for each face (v1, v2, v3) in mesh->Faces():
        // For each of the 3 edges, find or create midpoint

        // Edge 1: v1-v2
        Edge e12 = {v1, v2, 0xFFFF};  // midpoint = sentinel
        auto it = edges.find(e12);
        short mid12;
        if (it == edges.end()):
            mid12 = nextVert++;
            e12.midpoint = mid12;
            edges.insert(e12);
            Vert blended;
            BlendVert(verts[v1], verts[v2], blended);
            newVerts.push_back(blended);
        else:
            mid12 = it->midpoint;

        // Edge 2: v2-v3  (same pattern)
        Edge e23 = {v2, v3, 0xFFFF};
        ...

        // Edge 3: v3-v1  (same pattern)
        Edge e31 = {v3, v1, 0xFFFF};
        ...

        // Create 4 sub-faces
        newFaces.push_back(Face(v1, mid12, mid31));
        newFaces.push_back(Face(mid31, mid12, mid23));   // center triangle?
        newFaces.push_back(Face(mid12, v2, mid23));
        newFaces.push_back(Face(mid31, mid23, v3));

    // Replace mesh faces with newFaces
    mesh->Faces() = newFaces;  // assign_aux with forward_iterator_tag

    // Resize verts to include new midpoint verts
    int totalVerts = newVerts.size() + mesh->NumVerts();  // wait...
    // Actually from Ghidra:
    // resize(geomOwner->mVerts, (newVerts.end - newVerts.begin) / 0x60 + geomOwner->mNumVerts)
    // So it's: newVerts.size() + original numVerts
    mesh->Verts().resize(newVerts.size() + originalNumVerts);

    // Copy new verts into the mesh starting at originalNumVerts
    if (originalNumVerts < nextVert):
        for i in range(newVerts.size()):
            memcpy(&mesh->Verts()[originalNumVerts + i], &newVerts[i], sizeof(Vert))

    mesh->Sync(0x3f);

    // Cleanup (vectors and set go out of scope)
```

### Face winding from Ghidra

Looking at the Ghidra output more carefully for the 4 push_backs of faces. The local variables tell the story:

```
local_1b8 = face.v1       (original v1)
local_1b6 = local_210     (mid12)
local_1b4 = local_208     (mid31)
→ Face 1: (v1, mid12, mid31)

local_1c8 = local_208     (mid31)
local_1c6 = local_210     (mid12)
local_1c4 = local_218     (mid23)
→ Face 2: (mid31, mid12, mid23)

local_1c0 = local_210     (mid12)
local_1be = face.v2       (original v2)
local_1bc = local_218     (mid23)
→ Face 3: (mid12, v2, mid23)

local_1b0 = local_218     (mid23)
local_1ae = face.v3       (original v3)
local_1ac = local_208     (mid31)
→ Wait, re-reading...
```

Let me re-examine more carefully. The Ghidra locals for Face fields:

Face struct = { v1, v2, v3 } = 3 unsigned shorts.

Looking at the push_back ordering:
```
// Face A:
local_1b8 = *puVar13;        // original face.v1
local_1be = puVar13[1];      // original face.v2  -- wait this is for Face C
```

Hmm, the Ghidra decompiler interleaves assignments. Let me trace more carefully:

```cpp
local_1b8 = *puVar13;      // = face.v1
local_1be = puVar13[1];    // = face.v2  (but this is offset for a DIFFERENT face struct)
local_1b4 = local_208;     // = mid31
local_1b6 = local_210;     // = mid12
local_1ae = puVar13[2];    // = face.v3
local_1c8 = local_208;     // = mid31
local_1c6 = local_210;     // = mid12
local_1c4 = local_218;     // = mid23
local_1c0 = local_210;     // = mid12
local_1bc = local_218;     // = mid23
local_220 = (uint)local_218;
local_1b0 = local_218;     // = mid23
local_1ac = local_208;     // = mid31
```

The face structs are at:
- `local_1b8, local_1b6, local_1b4` → Face(v1, mid12, mid31)
- `local_1c8, local_1c6, local_1c4` → Face(mid31, mid12, mid23)  -- center
- `local_1c0, local_1be, local_1bc` → Face(mid12, v2, mid23)
- `local_1b0, local_1ae, local_1ac` → Face(mid23, v3, mid31)

Wait no. Let me reconsider. `local_1be = puVar13[1]` which is the original face.v2. And `local_1ae = puVar13[2]` which is face.v3.

So the 4 faces pushed are:
```
Face(face.v1, mid12, mid31)
Face(mid31, mid12, mid23)       -- center triangle connecting all 3 midpoints
Face(mid12, face.v2, mid23)
Face(mid23, face.v3, mid31)
```

Wait, that doesn't look right for the center. Standard midpoint subdivision creates:
```
Face(v1, mid12, mid31)     -- corner at v1
Face(mid12, v2, mid23)     -- corner at v2
Face(mid31, mid23, v3)     -- corner at v3
Face(mid12, mid23, mid31)  -- center
```

But Ghidra shows the center face as (mid31, mid12, mid23), and the other two corner faces use different orderings. The winding matters for normals. Let me trust the Ghidra output here:

```
Face 1: (v1, mid12, mid31)
Face 2: (mid31, mid12, mid23)
Face 3: (mid12, v2, mid23)
Face 4: (mid23, v3, mid31)
```

Hmm actually re-checking again:
```
local_1c0 = local_210     // mid12
local_1be = puVar13[1]    // face.v2
local_1bc = local_218     // mid23
```
So Face 3 would be at addresses local_1c0, local_1be, local_1bc → (mid12, v2, mid23). Yes.

And:
```
local_1b0 = local_218     // mid23
local_1ae = puVar13[2]    // face.v3
local_1ac = local_208     // mid31
```
Face 4: (mid23, v3, mid31).

These are DIFFERENT from the standard ordering but preserve winding direction (all CCW if original was CCW), just listed in a different vertex rotation. The standard would be:
- (v1, mid12, mid31) ✓
- (mid12, v2, mid23) — Ghidra has this as Face 3 ✓
- (mid23, v3, mid31) — Ghidra has this as Face 4 ✓
- (mid12, mid23, mid31) — standard center, but Ghidra shows (mid31, mid12, mid23) which is same triangle just rotated

So the faces in push order are:
1. (v1, mid12, mid31)
2. (mid31, mid12, mid23)
3. (mid12, v2, mid23)
4. (mid23, v3, mid31)

### Edge lookup pattern

When looking up an edge, the Edge struct has {v0, v1, midpoint}. The `operator<` does undirected comparison (treats (a,b) same as (b,a)). When creating a new edge:
- midpoint is set to `nextVert` (cast to short/ushort)
- The edge is inserted into the set

When an existing edge is found:
- The found node's v0, v1, midpoint are read back
- midpoint is extracted from offset +0x14 in the tree node (which is +0x4 past the 0x10 node header used by rb_tree)

The edge structs used for lookup are:
- Edge 1: {face.v1, face.v2, 0xFFFF} — local_214, local_212, local_210
- Edge 2: {face.v2, face.v3, 0xFFFF} — local_21c, local_21a, local_218
  Wait: local_212 = local_21c and local_20c = local_21a. Let me re-read...

Actually: `local_212 = local_21c` means they set edge1.v1 = v2 which equals edge2.v0. That's just setting up the edges correctly:
- Edge 1: {v1, v2}
- Edge 2: {v2, v3}
- Edge 3: {v3, v1} — local_20c = local_21a (=v3), local_20a = local_214 (=v1)

The `midpoint` field of each edge starts as 0xFFFF and gets set to the new vert index if the edge is newly created.

### Key detail: Edge struct search

From Ghidra, when `find` returns end (not found):
```
local_210 = (undefined2)uVar7;   // midpoint = nextVert
uVar7 = uVar7 + 1;               // nextVert++
insert_unique(a_Stack_1a8, &edge);
push_back(newVerts, blendedVert);
```

When found:
```
local_214 = *(ushort *)(p_Var4 + 0x10);   // edge.v0 from tree node
local_212 = *(ushort *)(p_Var4 + 0x12);   // edge.v1 from tree node
local_210 = *(undefined2 *)(p_Var4 + 0x14); // edge.midpoint from tree node
```

This means it reads back v0, v1, midpoint from the found edge. This is interesting — it overwrites the local v0/v1 with the canonical (found) values. But since `operator<` treats edges as undirected, the found edge might have v0/v1 swapped compared to our lookup key. This doesn't matter for midpoint, but it means the local variables get updated. We can mimic this by just reading `it->midpoint`.

Wait actually — it copies ALL THREE fields back. So local_214/local_212 (which are face.v1/face.v2 for edge 1) get overwritten with the stored edge's v0/v1. But then those locals aren't used for vertex access anymore (the vertex pointers pVVar11 etc were already computed). They ARE used later for face construction though...

No wait — local_214 is reused as face.v1 in the face construction. If it gets overwritten with the edge's stored v0/v1, the face vertex indices could change! But since operator< is undirected, the stored v0 could be v2 and stored v1 could be v1 (swapped). That would mess up the face...

Hmm, let me look more carefully. For edge 1 lookup:
- local_214 = face.v1, local_212 = face.v2 = local_21c (set earlier)
- If found: local_214 gets overwritten with the tree node's v0, local_212 with v1
- But face.v1 was stored in `*puVar13` and accessed earlier to compute pVVar11

Actually on re-reading, `local_214 = *(ushort *)puVar13` and `local_21c = puVar13[1]`, `local_21a = puVar13[2]`. These are the original face indices. Then `local_212 = local_21c` just copies v2 into the edge struct's second slot. If the edge is found, local_214 and local_212 get overwritten — but local_214 was used to compute pVVar11 already and won't be reused for that purpose. It IS used later in face construction though (`local_1b8 = *puVar13` which is separate from local_214).

Actually wait — `local_1b8 = *puVar13` reads the original face.v1 from the face array directly, NOT from local_214. So even if local_214 is overwritten by the edge find, the face v1 is preserved.

OK so the edge find overwrites local_214/local_212 but those aren't used for face construction (face verts come from puVar13 directly). Only the midpoint matters.

**Conclusion**: We don't need to copy v0/v1 back from the found edge. We just need the midpoint. The Ghidra output is an artifact of how the compiler lays out the Edge struct in locals.

### Implementation plan for TessellateMesh

```cpp
void TessellateMesh(RndMesh *mesh) {
    typedef RndAmbientOcclusion::Edge Edge;
    std::set<Edge> edges;
    std::vector<RndMesh::Face> newFaces;
    std::vector<RndMesh::Vert> newVerts;

    RndMesh *geomOwner = mesh->GetGeomOwner();

    newFaces.reserve(geomOwner->Faces().size() * 4);
    newVerts.reserve(geomOwner->Verts().size() * 3);

    unsigned short nextVert = (unsigned short)geomOwner->Verts().size();

    for (int i = 0; i < geomOwner->Faces().size(); i++) {
        RndMesh::Face &face = geomOwner->Faces()[i];
        unsigned short v1 = face.v1;
        unsigned short v2 = face.v2;
        unsigned short v3 = face.v3;

        RndMesh::Vert *verts = geomOwner->Verts().begin();

        RndMesh::Vert blend12, blend23, blend31;
        RndAmbientOcclusion::BlendVert(verts[v1], verts[v2], blend12);
        RndAmbientOcclusion::BlendVert(verts[v2], verts[v3], blend23);
        RndAmbientOcclusion::BlendVert(verts[v3], verts[v1], blend31);

        // Edge v1-v2
        unsigned short mid12;
        Edge e12 = {(short)v1, (short)v2, -1};
        std::set<Edge>::iterator it12 = edges.find(e12);
        if (it12 == edges.end()) {
            mid12 = nextVert++;
            e12.midpoint = mid12;
            edges.insert(e12);
            newVerts.push_back(blend12);
        } else {
            mid12 = it12->midpoint;
        }

        // Edge v2-v3
        unsigned short mid23;
        Edge e23 = {(short)v2, (short)v3, -1};
        std::set<Edge>::iterator it23 = edges.find(e23);
        if (it23 == edges.end()) {
            mid23 = nextVert++;
            e23.midpoint = mid23;
            edges.insert(e23);
            newVerts.push_back(blend23);
        } else {
            mid23 = it23->midpoint;
        }

        // Edge v3-v1
        unsigned short mid31;
        Edge e31 = {(short)v3, (short)v1, -1};
        std::set<Edge>::iterator it31 = edges.find(e31);
        if (it31 == edges.end()) {
            mid31 = nextVert++;
            e31.midpoint = mid31;
            edges.insert(e31);
            newVerts.push_back(blend31);
        } else {
            mid31 = it31->midpoint;
        }

        // 4 sub-faces
        RndMesh::Face f1, f2, f3, f4;
        f1.Set(v1, mid12, mid31);
        f2.Set(mid31, mid12, mid23);
        f3.Set(mid12, v2, mid23);
        f4.Set(mid23, v3, mid31);
        newFaces.push_back(f1);
        newFaces.push_back(f2);
        newFaces.push_back(f3);
        newFaces.push_back(f4);
    }

    // Replace faces
    geomOwner->Faces().assign(newFaces.begin(), newFaces.end());

    // Expand verts and copy new midpoint verts
    int origNumVerts = geomOwner->Verts().size();
    geomOwner->Verts().resize(origNumVerts + newVerts.size());

    for (int i = 0; i < (int)newVerts.size(); i++) {
        memcpy(&geomOwner->Verts()[origNumVerts + i], &newVerts[i], sizeof(RndMesh::Vert));
    }

    mesh->Sync(0x3f);
}
```

### Concerns
- The Ghidra output accesses `this->field_0x148` which is mGeomOwner — but through the `this` pointer which might be offset due to multiple inheritance. In our C++ code we just use `mesh->GetGeomOwner()` and the API methods.
- Need to verify that `Faces().assign()` matches `_M_assign_aux` with `forward_iterator_tag`.
- The Vert default constructor initializes all fields — the three blend verts are constructed then overwritten by BlendVert (which does memcpy).
- `nextVert` is tracked as a running counter starting from original vert count.

## Function 2: BuildVisit

### What it does
Recursively traverses a BSP tree. At each node, it creates a BuildPoly from the node's splitting plane and clips it (and all existing polys) against that plane. The "front" polys go to `gChildPolys`, the "back" polys get clipped the other direction. The recursion walks left then right subtrees.

### Algorithm from Ghidra

This is the most complex function. Here's my understanding:

```
BuildVisit(BSPNode *node):
    if (node == NULL) return;

    // Create a new BuildPoly for this node's plane
    BuildPoly newPoly;  // default constructed (empty polygon + identity transform)
    gParentPolys.insert(gParentPolys.end(), newPoly);  // append to parent list

    // Get reference to the just-inserted poly (last element = the one at end iterator - 1)
    // Actually it's inserted before end(), so it's the new last element
    BuildPoly &poly = gParentPolys.back();  // effectively

    // Set up the poly's transform from the plane normal
    Plane &plane = node->plane;
    float lenSq = plane.a*plane.a + plane.b*plane.b + plane.c*plane.c;
    float invLen = -(plane.d / lenSq);

    // Transform translation = point on plane closest to origin
    poly.mTransform.v.x = plane.a * invLen;
    poly.mTransform.v.y = plane.b * invLen;
    poly.mTransform.v.z = plane.c * invLen;

    // Transform.m.z = plane normal (a, b, c)
    poly.mTransform.m.z.x = plane.a;
    poly.mTransform.m.z.y = plane.b;
    poly.mTransform.m.z.z = plane.c;

    // Build orthonormal basis from plane normal
    // Start with up = (0, 1, 0)
    poly.mTransform.m.y.Set(0, 1, 0);

    // If plane normal is too close to Y axis, use X axis instead
    if (fabsf(plane.a * 0 + plane.c * 0 + plane.b * 1.0f) > 0.9f) {
        poly.mTransform.m.y.Set(1, 0, 0);
    }

    // Cross product: x = y × z (plane normal)
    Vector3 &up = poly.mTransform.m.y;
    Vector3 &normal = poly.mTransform.m.z;
    poly.mTransform.m.x.x = up.y * normal.z - up.z * normal.y;
    poly.mTransform.m.x.y = normal.x * up.z - normal.z * up.x;   // wait standard cross...
    // Actually: x = y cross z
    // x.x = y.y*z.z - y.z*z.y
    // x.y = y.z*z.x - y.x*z.z
    // x.z = y.x*z.y - y.y*z.x

    Normalize(poly.mTransform.m.x, poly.mTransform.m.x);

    // Recompute y = z × x (to make orthonormal)
    poly.mTransform.m.y.x = normal.y * poly.mTransform.m.x.z - normal.z * poly.mTransform.m.x.y;
    // etc...  y = z cross x

    // Add a large quad polygon (±10000 in the plane's local 2D space)
    poly.mPoly.points.push_back(Vector2(-10000, 10000));
    poly.mPoly.points.push_back(Vector2(-10000, -10000));
    poly.mPoly.points.push_back(Vector2(10000, -10000));
    poly.mPoly.points.push_back(Vector2(10000, 10000));

    // Now handle left/right subtrees
    if (node->left == NULL) {
        // Leaf node (no left child) — just clip and recurse right
        // Clip all parent polys against this plane (front side)
        for each poly in gParentPolys:
            Clip(poly, node->plane, true);

        BuildVisit(node->right);

        // Also clip child polys against this plane (front side)
        for each poly in gChildPolys:
            Clip(poly, node->plane, true);
    } else {
        // Internal node — need to handle both sides

        // Save current parent polys
        std::list<BuildPoly> savedParents(gParentPolys);  // copy

        // Clip parent polys against plane (back side, b=false)
        for each poly in gParentPolys:
            Clip(poly, node->plane, false);

        // Recurse left (back/behind the plane)
        BuildVisit(node->left);

        // Clip child polys against plane (back side)
        for each poly in gChildPolys:
            Clip(poly, node->plane, false);

        // Swap: move current results to temp, restore saved parents
        // This is complex list splicing...
        std::list<BuildPoly> tempChildren;
        tempChildren.swap(gChildPolys);    // save children from left subtree
        gParentPolys.swap(savedParents);   // restore original parents

        // Clip parent polys against plane (front side, b=true)
        for each poly in gParentPolys:
            Clip(poly, node->plane, true);

        // Recurse right (front/in front of plane)
        BuildVisit(node->right);

        // Clip child polys against plane (front side)
        for each poly in gChildPolys:
            Clip(poly, node->plane, true);

        // Splice saved children back into child list
        gParentPolys.splice(gParentPolys.end(), savedParents);
        gChildPolys.splice(gChildPolys.end(), tempChildren);

        // Clean up temp lists
        tempChildren.clear();
        savedParents.clear();
    }

    // Move polys whose plane matches this node's plane from parents to children
    // (These are the "final" polygons that live on this BSP splitting plane)
    auto it = gParentPolys.begin();
    while (it != gParentPolys.end()) {
        bool planeMatch = (it->mTransform.m.z.x == node->plane.a &&
                          it->mTransform.m.z.y == node->plane.b &&
                          it->mTransform.m.z.z == node->plane.c);
        if (planeMatch) {
            // Splice this poly from parents to front of children
            auto next = std::next(it);
            gChildPolys.splice(gChildPolys.begin(), gParentPolys, it);
            it = next;
        } else {
            ++it;
        }
    }
```

### Key observations

1. The "plane match" check at the end compares the Transform's z-axis (which was set to the plane normal) against the BSP node's plane normal. This identifies polys that were created for this exact splitting plane.

2. The splicing is done by `splice()` — this is an O(1) operation for `std::list` that transfers nodes between lists without copying.

3. The large quad (±10000) gets clipped down by recursive calls as the BSP tree is traversed, eventually leaving only the polygon area that's inside the BSP region.

4. The `Clip(poly, plane, true)` clips to the front side of the plane, `Clip(poly, plane, false)` clips to the back side.

### Ghidra offset mapping

In Ghidra, the BuildPoly offsets within list nodes:
- Node + 0x0 = next pointer
- Node + 0x4 = prev pointer  (or vice versa, stlport list)
- Node + 0x8 = BuildPoly data start
  - +0x8 = mPoly (Hmx::Polygon → vector<Vector2>)
  - +0x14 = mTransform starts (0x8 + 0xc = 0x14)
    - +0x14 = mTransform.m.x (Vector3)
    - +0x20 = mTransform.m.y
    - +0x2C = mTransform.m.z  → this is the plane normal storage
    - +0x38 = mTransform.v (translation)

So when Ghidra says `ppppppuVar14[0xd]` (offset 0xd * 4 = 0x34 from node start), that's... hmm. Let me recalculate. If ppppppuVar14 points to the list node, and the data starts at offset 0x8 (after next/prev), then:
- offset 0x8 = mPoly.points (vector: begin ptr, end ptr, capacity ptr = 12 bytes)
- offset 0x14 = mTransform.m.x.x
- offset 0x18 = mTransform.m.x.y
- offset 0x1C = mTransform.m.x.z
- offset 0x20 = mTransform.m.y.x
- offset 0x24 = mTransform.m.y.y
- offset 0x28 = mTransform.m.y.z
- offset 0x2C = mTransform.m.z.x  ← plane.a
- offset 0x30 = mTransform.m.z.y  ← plane.b
- offset 0x34 = mTransform.m.z.z  ← plane.c
- offset 0x38 = mTransform.v.x
- offset 0x3C = mTransform.v.y
- offset 0x40 = mTransform.v.z

Ghidra accesses ppppppuVar14[0xd] = offset 0x34 = mTransform.m.z.y (plane.b)
ppppppuVar14[0xe] = offset 0x38 = ... wait that's mTransform.v.x?

Hmm, that doesn't match. Let me reconsider. Ghidra offsets from ppppppuVar14:
- [5] = 0x14 = mTransform.m.x.x  ✓ (x-axis of basis)
- [6] = 0x18 = mTransform.m.x.y
- [7] = 0x1C = mTransform.m.x.z
- [9] = 0x24 = mTransform.m.y.x  (y-axis starts)
- [10] = 0x28 = mTransform.m.y.y
- [11] = 0x2C = mTransform.m.y.z
- [0xd] = 0x34 = mTransform.m.z.y

Wait that means [0xd] is at index 13 * 4 = 52 = 0x34. But mTransform.m.z starts at offset 0x2C from node. So:
- 0x2C = mTransform.m.z.x → index [0xB] = 11
- 0x30 = mTransform.m.z.y → index [0xC] = 12
- 0x34 = mTransform.m.z.z → index [0xD] = 13

So [0xd] = mTransform.m.z.z (plane.c), [0xe] = mTransform.v.x, [0xf] = mTransform.v.y, [0x10] = mTransform.v.z.

And the check `(float)ppppppuVar13[0xd] != *(float *)pPVar11` at the end:
- ppppppuVar13[0xd] = mTransform.m.z.z (plane normal c component)
- *(float *)pPVar11 = node->plane.a

That would be comparing c against a, which doesn't make sense. Unless the Ghidra indexing is from a different base...

Actually I realize ppppppuVar14 is initially set to `DAT_830a1998` which is the LIST SENTINEL, not a list node. So the offsets might be relative to the sentinel. But then later when iterating `ppppppuVar13 = ppppppuVar14` and it walks nodes via `*ppppppuVar13` (dereference = next pointer), the offsets would be from the node.

Let me just look at the end comparison more carefully:
```
(float)ppppppuVar13[0xd] != *(float *)pPVar11
(float)ppppppuVar13[0xe] != *(float *)(pPVar11 + 4)
(float)ppppppuVar13[0xf] != *(float *)(pPVar11 + 8)
```

pPVar11 is the BSPNode pointer. BSPNode has Plane at offset 0:
- *(float *)pPVar11 = plane.a
- *(float *)(pPVar11 + 4) = plane.b
- *(float *)(pPVar11 + 8) = plane.c

So ppppppuVar13[0xd], [0xe], [0xf] should be the transform.m.z components (the stored plane normal). The offsets are 0x34, 0x38, 0x3C. From the list node start:
- 0x34 = offset from node to mTransform.m.z.z...

Wait, I miscounted. Let me redo from scratch. A stlport list node has:
- offset 0x0: _M_next (pointer)
- offset 0x4: _M_prev (pointer)
- offset 0x8: data begins

BuildPoly layout:
- mPoly at 0x0 (Hmx::Polygon = vector<Vector2> = 3 pointers = 12 bytes, offsets 0x0-0xB)
- mTransform at 0xC (Transform = Matrix3 + Vector3 = 48 bytes)
  - m.x at 0xC (12 bytes: 0xC, 0x10, 0x14)
  - m.y at 0x18 (12 bytes: 0x18, 0x1C, 0x20)
  - m.z at 0x24 (12 bytes: 0x24, 0x28, 0x2C)
  - v at 0x30 (12 bytes: 0x30, 0x34, 0x38)

Total BuildPoly size = 0xC + 0x30 = 0x3C

In the list node, data starts at 0x8:
- Node+0x08 = mPoly.points._M_start
- Node+0x0C = mPoly.points._M_finish
- Node+0x10 = mPoly.points._M_end_of_storage
- Node+0x14 = mTransform.m.x.x
- Node+0x18 = mTransform.m.x.y
- Node+0x1C = mTransform.m.x.z
- Node+0x20 = mTransform.m.y.x
- Node+0x24 = mTransform.m.y.y
- Node+0x28 = mTransform.m.y.z
- Node+0x2C = mTransform.m.z.x  ← plane.a
- Node+0x30 = mTransform.m.z.y  ← plane.b
- Node+0x34 = mTransform.m.z.z  ← plane.c
- Node+0x38 = mTransform.v.x
- Node+0x3C = mTransform.v.y
- Node+0x40 = mTransform.v.z

At 4-byte indexed:
- [0] = next, [1] = prev
- [2] = mPoly start, [3] = mPoly finish, [4] = mPoly capacity
- [5] = m.x.x, [6] = m.x.y, [7] = m.x.z
- [8] = m.y.x, [9] = m.y.y, [10] = m.y.z (= 0xA)
- [11] = m.z.x (0xB), [12] = m.z.y (0xC), [13] = m.z.z (0xD)
- [14] = v.x (0xE), [15] = v.y (0xF), [16] = v.z (0x10)

Now the end comparison:
- [0xd] = index 13 = m.z.z → plane.c
- [0xe] = index 14 = v.x
- [0xf] = index 15 = v.y

Compared against:
- *(float *)pPVar11 = plane.a
- *(float *)(pPVar11 + 4) = plane.b
- *(float *)(pPVar11 + 8) = plane.c

So it's comparing (m.z.z, v.x, v.y) against (plane.a, plane.b, plane.c). That makes NO SENSE unless my offset calculation is wrong.

Let me reconsider. Maybe Hmx::Polygon isn't just a vector. Let me check.

```cpp
class Polygon {
    std::vector<Vector2> points;
};
```

Just a vector. On Xbox 360 with stlport, vector is:
- _M_start (4 bytes)
- _M_finish (4 bytes)
- _M_end_of_storage (4 bytes)

Total = 12 bytes. So Polygon = 12 bytes. And it's at offset 0x0 in BuildPoly. Transform at 0xC. That all checks out.

Hmm, wait. What if pPVar11 is not pointing to the BSPNode but was repurposed? Let me re-read the Ghidra output...

```
pPVar11 = (Plane *)__savegprlr_23(param_1);
```

So pPVar11 is the BSPNode* (param_1 cast to Plane*). And BSPNode starts with a Plane:
- 0x0: plane.a
- 0x4: plane.b
- 0x8: plane.c
- 0xC: plane.d
- 0x10: left
- 0x14: right

The end comparison checks:
```
ppppppuVar13[0xd] != *(float *)pPVar11       // against plane.a
ppppppuVar13[0xe] != *(float *)(pPVar11 + 4) // against plane.b
ppppppuVar13[0xf] != *(float *)(pPVar11 + 8) // against plane.c
```

For this to be a plane normal match, [0xd] should be m.z.x, [0xe] should be m.z.y, [0xf] should be m.z.z.

index 0xd = 13, byte offset = 0x34. From node start. If m.z.x is at node+0x2C, that's index 0xB = 11.

I'm off by 2 indices. That means either:
1. The list node header is larger (maybe 16 bytes instead of 8?)
2. Or BuildPoly has extra padding

Actually — stlport list nodes. Let me check the stlport source. Some stlport versions have the node like:
```cpp
struct _List_node : public _List_node_base {
    _Tp _M_data;
};
struct _List_node_base {
    _List_node_base *_M_next;
    _List_node_base *_M_prev;
};
```

So the node base is 8 bytes (two pointers), and data starts at offset 8. That's what I assumed.

But what if there's alignment padding? BuildPoly starts with a Polygon (vector), which starts with a pointer. Alignment of 4 is fine. No padding needed.

OR... maybe the Polygon class itself has padding or a vtable? Let me check:
```cpp
class Polygon {
    std::vector<Vector2> points;
};
```

No vtable, no virtual functions. Just a vector. 12 bytes.

Wait — does BuildPoly have a vtable? Let me check:
```cpp
struct BuildPoly {
    BuildPoly();
    ~BuildPoly();
    Hmx::Polygon mPoly;    // 0x0
    Transform mTransform;  // 0xc
};
```

The destructor is non-virtual (it's a struct, not inheriting from anything with virtuals). So no vtable. Size should be exactly 0xC + 0x30 = 0x3C.

Hmm, OK let me try a different approach. What if the indices in Ghidra are from the DATA START (offset 8), not from the node start? That would mean:
- Index from data: [0xd] = 13*4 = 0x34 from data start = 0x34
- In BuildPoly: 0x34 - 0x0 (mPoly offset) = ... still 0x34

That's still mTransform.v.y (0xC + 0x28 = 0x34). Still wrong.

OK let me try yet another interpretation. What if Ghidra is using byte offsets with the Plane* type (which is 1 byte per unit since Ghidra deduced it as Plane*)?

`pPVar11 + 4` means `(Plane*)pPVar11 + 4`. If Ghidra thinks Plane is some size, the addition would be `base + 4*sizeof(Plane)`. But Ghidra shows `*(float *)(pPVar11 + 4)` which accesses the plane.b field at byte offset 4. For sizeof(Plane) to give byte offset 4 at index +4... sizeof would have to be 1. So Ghidra cast pPVar11 as `Plane*` but the arithmetic treats it as byte pointer.

Anyway, I think I'm overanalyzing the Ghidra offsets. The logical meaning is clear:
- The end loop compares each BuildPoly's stored plane normal against the BSP node's plane normal
- If they match, splice from parents to children

Let me just write the code based on the logical understanding and not worry about exact Ghidra offset mapping.

## Function 3: BuildFromBSP

### What it does
Top-level function that converts a mesh's BSP tree into actual mesh geometry.

### Algorithm from Ghidra

```
BuildFromBSP(RndMesh *mesh):
    RndMesh *geomOwner = mesh->GetGeomOwner();
    BuildVisit(geomOwner->GetBSPTree());

    // Count total verts and faces from all BuildPolys
    int totalVerts = 0;
    unsigned int totalFaces = 0;

    auto it = gChildPolys.begin();
    while (it != gChildPolys.end()) {
        int numPoints = it->mPoly.points.size();
        if (numPoints < 3) {
            // Remove degenerate polygons
            it = gChildPolys.erase(it);
        } else {
            totalVerts += numPoints;
            totalFaces += numPoints - 2;  // fan triangulation
            ++it;
        }
    }

    // Resize mesh verts
    geomOwner->Verts().resize(totalVerts);

    // Resize mesh faces (erase excess or insert more)
    int currentFaces = geomOwner->Faces().size();
    if (totalFaces < currentFaces) {
        geomOwner->Faces().erase(
            geomOwner->Faces().begin() + totalFaces,
            geomOwner->Faces().end()
        );
    } else {
        RndMesh::Face emptyFace;
        geomOwner->Faces().insert(
            geomOwner->Faces().end(),
            totalFaces - currentFaces,
            emptyFace
        );
    }

    // Fill in verts and faces
    int vertIdx = 0;
    int faceIdx = 0;

    for each poly in gChildPolys:
        // Transform each 2D polygon point to 3D
        for each point in poly.mPoly.points:
            Vector3 pt2d(point.x, point.y, 0.0f);
            Multiply(pt2d, poly.mTransform, geomOwner->Verts(vertIdx).pos);
            vertIdx++;

        // Create fan triangulation
        int firstVert = vertIdx - poly.mPoly.points.size();
        for (int j = firstVert + 2; j < vertIdx; j++):
            geomOwner->Faces(faceIdx).Set(firstVert, j, j-1);
            // Wait, need to check actual winding...
            faceIdx++;

    // Clear global lists
    gParentPolys.clear();
    gChildPolys.clear();

    MakeNormals(mesh);
```

### Face construction detail from Ghidra

The face construction loop in Ghidra:
```
iVar9 = iVar14 - numPoints;  // firstVert = vertIdx - numPoints
iVar12 = iVar9 + 2;          // j starts at firstVert + 2
if (iVar12 < iVar14):        // while j < vertIdx
    iVar10 = iVar9 + 0x10001;  // ??? = firstVert + 0x10001
    do:
        uVar2 = (undefined2)iVar10;    // (short)(firstVert + 0x10001 + loopCount)
        uVar3 = (undefined2)iVar12;    // (short)j
        iVar12++;
        iVar10++;
        *puVar7 = (short)iVar9;        // face.v1 = firstVert
        puVar7[1] = uVar2;             // face.v2 = ???
        puVar7[2] = uVar3;             // face.v3 = j (before increment)
```

Wait, `iVar10 = iVar9 + 0x10001`. That's a weird constant. Let me think... `0x10001 = 65537 = 0x10000 + 1`. But we're casting to short, so `(short)(firstVert + 0x10001)` = `(short)(firstVert + 1)` since the 0x10000 gets truncated. So:
- face.v1 = (short)firstVert
- face.v2 = (short)(firstVert + 1 + loopIdx)  → which is (firstVert+1), (firstVert+2), ...
- face.v3 = (short)(firstVert + 2 + loopIdx)  → which is (firstVert+2), (firstVert+3), ...

Wait that means:
- First iteration: v1=first, v2=first+1, v3=first+2
- Second iteration: v1=first, v2=first+2, v3=first+3
- etc.

That's standard fan triangulation! The `0x10001` is just `firstVert + 1` when truncated to short. The 0x10000 is likely a compiler artifact from how it combined two shorts in a register.

So the face winding for fan triangulation is:
```
Face(firstVert, j-1, j)  // where j goes from firstVert+2 to vertIdx-1
```

Wait let me re-read: uVar2 corresponds to j-1 (firstVert+1, firstVert+2...) and uVar3 corresponds to j (firstVert+2, firstVert+3...). Actually:

iVar12 starts at firstVert+2, iVar10 starts at firstVert+1:
- Iteration 0: face = (firstVert, firstVert+1, firstVert+2)
- Iteration 1: face = (firstVert, firstVert+2, firstVert+3)
- Iteration 2: face = (firstVert, firstVert+3, firstVert+4)

Yes, standard fan triangulation anchored at firstVert.

### Implementation plan for BuildFromBSP

```cpp
void BuildFromBSP(RndMesh *mesh) {
    RndMesh *geomOwner = mesh->GetGeomOwner();
    BuildVisit(geomOwner->GetBSPTree());

    int totalVerts = 0;
    unsigned int totalFaces = 0;

    std::list<BuildPoly>::iterator it = gChildPolys.begin();
    while (it != gChildPolys.end()) {
        int numPoints = it->mPoly.points.size();
        if (numPoints < 3) {
            it = gChildPolys.erase(it);
        } else {
            totalVerts += numPoints;
            totalFaces += numPoints - 2;
            ++it;
        }
    }

    geomOwner->Verts().resize(totalVerts);

    // Adjust face count
    int currentFaces = (int)geomOwner->Faces().size();
    if (totalFaces < (unsigned int)currentFaces) {
        geomOwner->Faces().erase(
            geomOwner->Faces().begin() + totalFaces,
            geomOwner->Faces().end()
        );
    } else {
        RndMesh::Face emptyFace;
        geomOwner->Faces().insert(
            geomOwner->Faces().end(),
            totalFaces - currentFaces,
            emptyFace
        );
    }

    int vertIdx = 0;
    int faceIdx = 0;

    for (std::list<BuildPoly>::iterator pit = gChildPolys.begin();
         pit != gChildPolys.end(); ++pit) {
        // Transform 2D polygon points to 3D mesh vertices
        for (int p = 0; p < (int)pit->mPoly.points.size(); p++) {
            Vector3 pt(pit->mPoly.points[p].x, pit->mPoly.points[p].y, 0.0f);
            Multiply(pt, pit->mTransform, geomOwner->Verts(vertIdx).pos);
            vertIdx++;
        }

        // Fan triangulation
        int firstVert = vertIdx - (int)pit->mPoly.points.size();
        for (int j = firstVert + 2; j < vertIdx; j++) {
            geomOwner->Faces(faceIdx).Set(
                (unsigned short)firstVert,
                (unsigned short)(j - 1),
                (unsigned short)j
            );
            faceIdx++;
        }
    }

    gParentPolys.clear();
    gChildPolys.clear();

    MakeNormals(mesh);
}
```

## BlendVert Details

### Algorithm (from Ghidra, prior session analysis)

1. `memcpy(&out, &v1, sizeof(Vert))` — copy all of v1 into out
2. Add v2's pos, tex, color, norm to out (component-wise)
3. Compute `tx = v2.tangent.x + out.tangent.x` (just x for now)
4. Halve pos (×0.5), halve tex (×0.5), halve color (×0.5)
5. Compute `ty = v2.tangent.y + out.tangent.y`, `tz = v2.tangent.z + out.tangent.z`
6. Normalize norm in place
7. Normalize tangent (tx, ty, tz), store back to out.tangent.x/y/z
8. Zero all color components (color was averaged but then zeroed — AO will be recomputed)

### Why color is zeroed
The mesh is being tessellated for AO computation. Vertex colors store AO values. New midpoint vertices need their AO recomputed, so color is zeroed. The averaging step is either dead code the compiler didn't optimize away, or it's there for some other reason that gets overridden.

## Edge::operator<

Undirected edge comparison. Canonicalizes (v0, v1) to (min, max) order, then compares as a single 32-bit value.

```cpp
bool RndAmbientOcclusion::Edge::operator<(const Edge &other) const {
    short aMin = v0 < v1 ? v0 : v1;
    short aMax = v0 < v1 ? v1 : v0;
    short bMin = other.v0 < other.v1 ? other.v0 : other.v1;
    short bMax = other.v0 < other.v1 ? other.v1 : other.v0;
    unsigned int a = ((unsigned int)(unsigned short)aMax << 16) | (unsigned short)aMin;
    unsigned int b = ((unsigned int)(unsigned short)bMax << 16) | (unsigned short)bMin;
    return a < b;
}
```

## Next Steps

1. ✅ Already added `midpoint` field and `BlendVert` declaration to AmbientOcclusion.h
2. ✅ Already added `Edge::operator<` and `BlendVert` implementation to AmbientOcclusion.cpp
3. Implement TessellateMesh in Utl.cpp
4. Implement BuildVisit in Utl.cpp
5. Implement BuildFromBSP in Utl.cpp
6. Build and test each function with objdiff
7. Iterate on match percentages

## Risks and Unknowns

- **BuildVisit complexity**: The list splice operations are tricky. stlport's list::splice has specific semantics. Getting the splice order wrong could produce very different code.
- **Cross product ordering**: Need to match exact cross product computation order for register allocation.
- **TessellateMesh Vert construction**: The three Vert temporaries are default-constructed then overwritten by BlendVert. The constructor initializes many fields. Need to verify this matches.
- **Face.Set() vs direct assignment**: Ghidra shows direct stores to face shorts. Need to verify Face::Set() generates the same code.
- **BuildFromBSP face loop**: The 0x10001 constant suggests the compiler combined two short computations. Our loop with `j-1` and `j` should produce the same result.
