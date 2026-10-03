#pragma once
#include "math\SHA1.h"
#include "math\Vec.h"
#include "obj/Object.h"
#include "rndobj\Mesh.h"
#include "rndobj\Trans.h"
#include "utl\MemMgr.h"

/** "Feeds the bones when executed." */
class CharCollide : public RndTransformable {
public:
    enum Shape {
        kCollidePlane = 0,
        kCollideSphere = 1,
        kCollideInsideSphere = 2,
        kCollideCigar = 3,
        kCollideInsideCigar = 4
    };

    struct CharCollideStruct {
        int vertIdx;
        Vector3 vec;
    };

    // Hmx::Object
    virtual ~CharCollide();
    OBJ_CLASSNAME(CharCollide)
    OBJ_SET_TYPE(CharCollide)
    virtual DataNode Handle(DataArray *, bool);
    virtual bool SyncProperty(DataNode &, DataArray *, int, PropOp);
    virtual void Save(BinStream &);
    virtual void Copy(const Hmx::Object *, CopyType);
    virtual void Load(BinStream &);
    // RndHighlightable
    virtual void Highlight();

    OBJ_MEM_OVERLOAD(0x15)
    NEW_OBJ(CharCollide)

    void SyncShape();
    void CopyOriginalToCur();
    float GetCurRadius() const { return mCurRadius[0]; }
    float GetCurRadius1() const { return mCurRadius[1]; }
    float GetCurLength0() const { return mCurLength[0]; }
    float GetCurLength1() const { return mCurLength[1]; }
    int GetFlags() const { return mFlags; }
    Shape GetShape() const { return mShape; }
    const Vector3 &Axis() const { return unk1fc; }
    // w14-b: 99.954%, 15 register-only rows in the cigar arm -- the image sums
    // Dot(out, unk1fc) as y,z,x, we emit y,x,z. Tried: swapped Dot args, all
    // six explicit term orders (break the plane arm, 96.1), Dot temp, axis ref,
    // Axis() accessor, explicit ScaleAdd, Dot*unk1f8: none better.
    // w21-as: 99.954 -> 99.969, 15 -> 4 rows.  Three changes, found with a
    // standalone cl.exe probe that reproduced the old listing exactly:
    //  - the target's dead `addi r11, r3, 0x1fc` is a reference bound to the
    //    axis (`const Vector3 &axis = unk1fc;`);
    //  - the cigar dot is written with the image's association (y + z) + x
    //    (fmuls 0x200, fmadds 0x204, fmadds 0x1fc); any non-Dot spelling here
    //    otherwise reshuffles the PLANE arm too;
    //  - the subtraction is spelled as three locals declared z, y, x (any order
    //    with z before y keeps the image's z-first subtract and puts the x term
    //    last with the image's operand order; Subtract() does not).
    // REMAINING (4 rows): inside the y/z pair MSVC emits the z product first
    // (`lfs 0x204; fmuls ..., f0`) where the image emits y first.  Every probe
    // spelling gave z first: both pair operand orders, x + (pair), separate
    // accumulator statements, named products, locals for the axis components,
    // dy/dz instead of out.y/out.z, Clamp vs Min(Max()), a named projection,
    // componentwise ScaleAdd, all six subtract declaration orders.
    float GetRadius(const Vector3 &pos, Vector3 &out) const {
        float dz = pos.z - unk20c.z;
        float dy = pos.y - unk20c.y;
        float dx = pos.x - unk20c.x;
        out.Set(dx, dy, dz);
        float ret = mCurRadius[0];
        if (mShape >= kCollideCigar) {
            const Vector3 &axis = unk1fc;
            float clamped = Clamp(
                mCurLength[0],
                mCurLength[1],
                unk1f8 * ((axis.y * out.y + axis.z * out.z) + axis.x * out.x)
            );
            ScaleAdd(out, axis, -clamped, out);
            Interp(ret, mCurRadius[1], unk1f4 * (clamped - mCurLength[0]), ret);
        } else if (mShape == kCollidePlane) {
            ret = Dot(out, unk1fc);
            Scale(unk1fc, ret, out);
        }
        return ret;
    }
    /** "Cache world state for collision queries during simulation" */
    void SyncWorldState(); // defined in CharHair.cpp (target TU)

protected:
    CharCollide();
    int NumSpheres(Shape) const;

    /** "Type of collision" */
    Shape mShape; // 0xc0
    int mFlags; // 0xc4
    /** "Optional mesh that will deform, used to resize ourselves.
        If this is set, make sure you are not parented to any bone with scale,
        such as an exo bone" */
    ObjPtr<RndMesh> mMesh; // 0xc8
    CSHA1::Digest mDigest; // 0xdc
    CharCollideStruct unkStructs[8]; // 0xF0
    /** radius0: "Radius of the sphere, or of length0 hemisphere if cigar" */
    /** radius1: "cigar: Radius of length1 hemisphere" */
    float mOrigRadius[2]; // 0x190
    /** length0: "cigar: placement of radius0 hemisphere along X axis,
        must be < than length0, not used for sphere shapes" */
    /** length1: "cigar: placement of radius1 hemisphere along X axis,
        must be >= length0" */
    float mOrigLength[2]; // 0x198
    Transform unk1a0; // 0x1a0
    float mCurRadius[2]; // 0x1e0
    float mCurLength[2]; // 0x1e8
    /** "For spheres + cigars, finds mesh points along positive y axis (the green one),
        makes a better fit for spheres where only one side should be the fit,
        like for chest and back collision volumes" */
    bool mMeshYBias; // 0x1f0
    float unk1f4; // 0x1f4
    float unk1f8; // 0x1f8
    Vector3 unk1fc; // 0x1fc
    Vector3 unk20c; // 0x20c
};
