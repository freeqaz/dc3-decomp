#pragma once
#include "math/Geo.h"
#include "obj\Data.h"
#include "obj/Object.h"
#include "rndobj\Mesh.h"

class SkeletonExtentTracker : public Hmx::Object {
public:
    SkeletonExtentTracker();
    // Hmx::Object
    virtual DataNode Handle(DataArray *, bool);

    void Poll();
    void ApplyToMeshVerts(RndMesh *, bool) const;
    void StartTracking(int);

private:
    Hmx::Rect GetViewBox() const;

    // w14-f: two Vector2s, not four floats.  StartTracking (inlined into Handle)
    // builds each extent as a stack temp and copies it with one ld/std pair
    // (`stfs f0, 0x58(r31)` x2 / `ld r11, 0x58(r31)` / `std r11, 0x34(r26)`);
    // four float members store each component directly.
    Vector2 mMin; // 0x2c
    Vector2 mMax; // 0x34
    int mTrackingID; // 0x3c
};
