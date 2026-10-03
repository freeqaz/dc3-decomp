#pragma once
#include "MoveDir.h"
#include "hamobj\Difficulty.h"
#include "hamobj\HamCharacter.h"
#include "math\Mtx.h"
#include "math\Vec.h"
#include "obj/Object.h"
#include "rndobj\Trans.h"
#include "utl\MemMgr.h"

struct BeatCollisionData {
    void Set(float, float, const Transform &, const Transform &);

    float mMinX; // 0x0
    float mMaxX; // 0x4
    Vector3 mOffset; // 0x8
};

struct SongCollisionOutput {
    // Names are ours; offsets and roles from SongCollision::CheckCollision.
    Vector3 mMinEdge[2]; // 0x00: each player's min-X extent, world space
    Vector3 mMaxEdge[2]; // 0x20: each player's max-X extent, world space
    Vector3 mPush[2]; // 0x40: extent projected onto the inter-player axis
    Transform mXfms[2]; // 0x60: each player's world transform
    bool mColliding; // 0xE0

    const Vector3 &WorldPos(int playerIdx) const { return mXfms[playerIdx].v; }

    // idx 0-5 walks mMinEdge, mMaxEdge, mPush as one 16-byte-stride array.
    const Vector3 &Offset(int idx) const {
        return reinterpret_cast<const Vector3 *>(this)[idx];
    }

    bool Colliding() const { return mColliding; }
};
static_assert(sizeof(SongCollisionOutput) == 0xE4, "SongCollisionOutput size mismatch");

/** "Contains data for handling potential character collisions" */
class SongCollision : public Hmx::Object {
public:
    // Hmx::Object
    OBJ_CLASSNAME(SongCollision);
    OBJ_SET_TYPE(SongCollision);
    virtual DataNode Handle(DataArray *, bool);
    virtual bool SyncProperty(DataNode &, DataArray *, int, PropOp);
    virtual void Save(BinStream &);
    virtual void Copy(const Hmx::Object *, Hmx::Object::CopyType);
    virtual void Load(BinStream &);
    virtual void Print();

    OBJ_MEM_OVERLOAD(0x2D)
    NEW_OBJ(SongCollision)

    void Update(MoveDir *);
    bool Equals(SongCollision *);
    bool IsCollision(
        int,
        int,
        const Difficulty *const,
        const Transform *const,
        std::vector<SongCollisionOutput> *
    ) const;

    static void Init();
    static void GatherUsefulBones(std::vector<RndTransformable *> &, HamCharacter *);

private:
    static float sCollisionTolerance;

    friend bool AreDancersColliding1D(
        std::vector<RndTransformable *> &,
        std::vector<RndTransformable *> &,
        const Vector3 &,
        const Vector3 &
    );

    const BeatCollisionData *BeatData(int, Difficulty) const;
    void CheckCollision(
        int, const Difficulty *const, const Transform *const, SongCollisionOutput &
    ) const;

protected:
    SongCollision();

    // indexed by difficulty, then beat
    // so mData[kDifficultyExpert][4] is
    // the BeatCollisionData for expert difficulty at beat 4
    std::vector<BeatCollisionData> mData[kNumDifficulties]; // 0x2c
};
