#include "char/CharHair.h"
#include "char/CharCollide.h"
#include "char/Character.h"
#include "math/Rot.h"
#include "obj/Object.h"
#include "obj/Task.h"
#include "rndobj/Poll.h"
#include "rndobj/PostProc.h"
#include "utl/BinStream.h"
#include "world/Dir.h"

CharHair *gHair;
CharHair::Strand *gStrand;

#pragma region CharHair

CharHair::CharHair()
    : mStiffness(0.04), mTorsion(0.1), mInertia(0.7), mGravity(1), mWeight(0.5),
      mFriction(0.3), mWind(1), mFlat(1), mMinSlack(0), mMaxSlack(0), mStrands(this),
      mReset(1), mSimulate(1), mUsePostProc(1), mWindObj(this), mCollides(this),
      mManagedHookup(0) {}

CharHair::~CharHair() {}

BEGIN_HANDLERS(CharHair)
    HANDLE_ACTION(reset, mReset = _msg->Int(2))
    HANDLE_ACTION(hookup, Hookup())
    HANDLE_ACTION(set_cloth, SetCloth(_msg->Int(2)))
    HANDLE_ACTION(freeze_pose, FreezePose())
    HANDLE_SUPERCLASS(RndPollable)
    HANDLE_SUPERCLASS(Hmx::Object)
END_HANDLERS

BEGIN_PROPSYNCS(CharHair)
    gHair = this;
    SYNC_PROP(stiffness, mStiffness)
    SYNC_PROP(torsion, mTorsion)
    SYNC_PROP(inertia, mInertia)
    SYNC_PROP(gravity, mGravity)
    SYNC_PROP(weight, mWeight)
    SYNC_PROP(friction, mFriction)
    SYNC_PROP(wind_obj, mWindObj)
    SYNC_PROP(wind, mWind)
    SYNC_PROP(flat, mFlat)
    SYNC_PROP(strands, mStrands)
    SYNC_PROP(simulate, mSimulate)
    SYNC_PROP(min_slack, mMinSlack)
    SYNC_PROP(max_slack, mMaxSlack)
    SYNC_SUPERCLASS(Hmx::Object)
END_PROPSYNCS

BEGIN_SAVES(CharHair)
    SAVE_REVS(0xD, 0)
    SAVE_SUPERCLASS(Hmx::Object)
    bs << mStiffness;
    bs << mTorsion;
    bs << mInertia;
    bs << mGravity;
    bs << mWeight;
    bs << mFriction;
    bs << mMinSlack;
    bs << mMaxSlack;
    bs << mStrands;
    bs << mSimulate;
    bs << mWindObj;
    bs << mWind;
    bs << mFlat;
END_SAVES

BEGIN_COPYS(CharHair)
    COPY_SUPERCLASS(Hmx::Object)
    CREATE_COPY(CharHair)
    BEGIN_COPYING_MEMBERS
        COPY_MEMBER(mStiffness)
        COPY_MEMBER(mInertia)
        COPY_MEMBER(mGravity)
        COPY_MEMBER(mWeight)
        COPY_MEMBER(mFriction)
        COPY_MEMBER(mTorsion)
        COPY_MEMBER(mStrands)
        COPY_MEMBER(mSimulate)
        COPY_MEMBER(mMinSlack)
        COPY_MEMBER(mMaxSlack)
        COPY_MEMBER(mWindObj)
        COPY_MEMBER(mWind)
        COPY_MEMBER(mFlat)
    END_COPYING_MEMBERS
END_COPYS

void CharHair::SetName(const char *name, ObjectDir *dir) {
    Hmx::Object::SetName(name, dir);
    mUsePostProc = dynamic_cast<Character *>(dir) || dynamic_cast<WorldDir *>(dir);
}

void CharHair::Poll() {
    Character *cur = Character::Current();
    if (cur) {
        if (cur->Synced()) {
            Hookup();
        }
        if (cur->Teleported()) {
            mReset = 1;
        }
        if (cur->LODCheck()) {
            DoReset(0);
            return;
        }
    }
    if (mReset > 0) {
        DoReset(mReset);
    }
    if (TheTaskMgr.DeltaSeconds() != 0) {
        SimulateLoops(1, GetFPS());
    } else {
        SimulateZeroTime();
    }
}

void CharHair::Enter() {
    mReset = 1;
    RndPollable::Enter();
    Hookup();
}

void CharHair::PollDeps(
    std::list<Hmx::Object *> &changedBy, std::list<Hmx::Object *> &change
) {
    for (int i = 0; i < mStrands.size(); i++) {
        changedBy.push_back(mStrands[i].Root());
        change.push_back(mStrands[i].Root());
    }
}

void CharHair::SetCloth(bool b) {
    for (int i = 0; i < mStrands.size(); i++) {
        Strand &strand = mStrands[i];
        Strand &modidx = mStrands[Mod(i + 1, mStrands.size())];
        for (int j = 0; j < strand.Points().size(); j++) {
            Point &point = strand.Points()[j];
            bool b1 = b && j < modidx.Points().size();
            point.sideLength = b1 ? Distance(point.pos, modidx.Points()[j].pos) : -1.0f;
        }
    }
}

void CharHair::Hookup() {
    if (!mManagedHookup) {
        ObjPtrList<CharCollide> list(this);
        for (ObjDirItr<CharCollide> it(Dir(), true); it != nullptr; ++it) {
            list.push_back(it);
        }
        list.sort(SortCollides());
        Hookup(list);
    }
}

void CharHair::FreezePose() {
    bool oldSim = mSimulate;
    Hookup();
    SimulateLoops(200, 60);
    mSimulate = oldSim;
    FreezePoseRaw();
}

float CharHair::GetFPS() {
    if (mUsePostProc && RndPostProc::Current()
        && RndPostProc::Current()->EmulateFPS() > 0) {
        float fps = RndPostProc::Current()->EmulateFPS();
        if (fps != 60.0f)
            fps = 60.0f - fps;
        return fps;
    }
    return 60.0f;
}

void CharHair::SimulateZeroTime() {
    if (mSimulate) {
        for (int i = 0; i < mStrands.size(); i++) {
            Strand &curStrand = mStrands[i];
            RndTransformable *root = curStrand.Root();
            if (root && curStrand.Root()->TransParent()) {
                Transform tf50;
                Vector3 v2c = curStrand.Root()->WorldXfm().v;
                Multiply(
                    curStrand.RootMat(),
                    curStrand.Root()->TransParent()->WorldXfm().m,
                    tf50.m
                );
                ObjVector<Point> &points = curStrand.Points();
                for (int j = 0; j < points.size(); j++) {
                    Point &curPoint = points[j];
                    Hmx::Matrix3 m78;
                    Subtract(curPoint.pos, v2c, m78.y);
                    m78.z = curPoint.lastZ;
                    Normalize(m78, tf50.m);
                    if (curPoint.bone) {
                        curPoint.bone->SetWorldXfm(tf50);
                    }
                    v2c = curPoint.pos;
                }
            }
        }
    }
}

INIT_REVS(11, 0)

BinStream &operator>>(BinStreamRev &bsrev, ObjVector<CharHair::Strand> &vec);

void CharHair::Load(BinStream &bs) {
    LOAD_REVS(bs);
    ASSERT_REVS(13, 0);
    LOAD_SUPERCLASS(Hmx::Object)
    bs >> mStiffness >> mTorsion >> mInertia >> mGravity >> mWeight >> mFriction;
    if (d.rev < 8) {
        mMinSlack = 0.0f;
        mMaxSlack = 0.0f;
    } else
        bs >> mMinSlack >> mMaxSlack;
    d >> mStrands;
    d >> mSimulate;
    if (d.rev > 10)
        bs >> mWindObj;
    if (d.rev > 11)
        bs >> mWind;
    if (d.rev > 12)
        bs >> mFlat;
}

#pragma endregion CharHair
#pragma region CharHair::Point

BEGIN_CUSTOM_PROPSYNC(CharHair::Point)
    SYNC_PROP(bone, o.bone)
    SYNC_PROP(length, o.length)
    SYNC_PROP(collides, o.collides)
    SYNC_PROP(radius, o.radius)
    SYNC_PROP(outer_radius, o.outerRadius)
    SYNC_PROP(side_length, o.sideLength)
END_CUSTOM_PROPSYNC

void operator<<(BinStream &bs, const CharHair::Point &p) {
    bs << p.pos;
    bs << p.bone;
    bs << p.length;
    bs << p.radius;
    bs << p.outerRadius;
    bs << p.sideLength;
    bs << p.unk78;
}

BinStream &operator>>(BinStream &bs, CharHair::Point &pt) {
    bs >> pt.pos;
    bs >> pt.bone;
    bs >> pt.length;
    bs >> pt.radius;
    bs >> pt.outerRadius;
    bs >> pt.sideLength;
    bs >> pt.unk78;
    pt.collides.clear();
    pt.force.Zero();
    pt.lastFriction.Zero();
    pt.lastZ.Zero();
    return bs;
}

void operator>>(BinStreamRev &d, CharHair::Point &pt) {
    char buf[0x100];
    char buf2[0x100];
    d >> pt.pos;
    d >> pt.bone;
    d >> pt.length;
    if (d.rev < 3) {
        int i;
        d.stream >> i;
        d.stream.ReadString(buf, 0xFF);
    } else if (d.rev == 3) {
        int i;
        d.stream >> i;
    }
    d >> pt.radius;
    if (d.rev > 1)
        d >> pt.outerRadius;
    else
        pt.outerRadius = 0;
    if (d.rev < 9 && d.rev > 5) {
        float f;
        d >> f;
        pt.radius += f;
        pt.outerRadius += f;
    }
    if (d.rev == 6) {
        d.stream.ReadString(buf2, 0xFF);
    }
    if (d.rev < 8) {
        pt.sideLength = -1.0f;
        if (d.rev > 5) {
            int i;
            d.stream >> i >> i;
        }
    } else {
        bool b = false;
        if (d.rev < 9)
            d >> b;
        d >> pt.sideLength;
        if (d.rev < 9 && !b) {
            pt.sideLength = -1.0f;
        }
    }
    if (d.rev > 9) {
        d >> pt.unk78;
    }
    pt.collides.clear();
    pt.force.Zero();
    pt.lastFriction.Zero();
    pt.lastZ.Zero();
}

BinStream &operator>>(BinStreamRev &bsrev, ObjVector<CharHair::Point> &vec) {
    BinStream &bs = bsrev.stream;
    int count;
    bs.ReadEndian(&count, 4);
    vec.resize(count);

    CharHair::Point *pt = vec.begin();
    while (pt != vec.end()) {
        bsrev >> *pt;
        pt++;
    }

    return bs;
}

#pragma endregion CharHair::Point
#pragma region CharHair::Strand

CharHair::Strand::Strand(Hmx::Object *o)
    : mShowSpheres(0), mShowCollide(0), mShowPose(0), mRoot(o, 0), mAngle(0.0f),
      mPoints(o), mHookupFlags(0) {
    mBaseMat.Identity();
    mRootMat.Identity();
}

CharHair::Strand::Strand(const Strand &rhs)
    : mShowSpheres(rhs.mShowSpheres), mShowCollide(rhs.mShowCollide),
      mShowPose(rhs.mShowPose), mRoot(rhs.mRoot), mAngle(rhs.mAngle),
      mPoints(rhs.mPoints), mHookupFlags(rhs.mHookupFlags) {
    const Hmx::Matrix3& src = rhs.mBaseMat;
    mBaseMat = src;
    mRootMat = rhs.mRootMat;
}

void CharHair::Strand::SetAngle(float angle) {
    mAngle = angle;
    Hmx::Matrix3 m38;
    MakeRotMatrixX(mAngle * DEG2RAD, m38);
    Multiply(m38, mBaseMat, mRootMat);
}

void CharHair::Strand::Load(BinStreamRev &d) {
    d >> mRoot;
    d >> mAngle;
    d >> mPoints;
    d >> mBaseMat >> mRootMat;
    if (d.rev > 2) {
        d >> mHookupFlags;
    } else
        mHookupFlags = 0;
}

BEGIN_CUSTOM_PROPSYNC(CharHair::Strand)
    gStrand = &o;
    SYNC_PROP_SET(root, o.mRoot.Ptr(), o.SetRoot(_val.Obj<RndTransformable>()))
    SYNC_PROP_SET(angle, o.mAngle, o.SetAngle(_val.Float()))
    SYNC_PROP(points, o.mPoints)
    SYNC_PROP(hookup_flags, o.mHookupFlags)
    SYNC_PROP(show_spheres, o.mShowSpheres)
    SYNC_PROP(show_collide, o.mShowCollide)
    SYNC_PROP(show_pose, o.mShowPose)
END_CUSTOM_PROPSYNC

void CharHair::Strand::Save(BinStream &bs) const {
    bs << mRoot;
    bs << mAngle;
    bs << mPoints;
    bs << mBaseMat;
    bs << mRootMat;
    bs << mHookupFlags;
}

#pragma endregion CharHair::Strand
#pragma region ObjVector_Strand

template<>
void ObjVector<CharHair::Strand>::resize(unsigned int n) {
    std::vector<CharHair::Strand>::resize(n, CharHair::Strand(mOwner));
}

BinStream &operator>>(BinStreamRev &bsrev, ObjVector<CharHair::Strand> &vec) {
    BinStream &bs = bsrev.stream;
    int count;
    bs.ReadEndian(&count, 4);
    vec.resize(count);

    CharHair::Strand *strand = vec.begin();
    while (strand != vec.end()) {
        strand->Load(bsrev);
        strand++;
    }

    return bs;
}

#pragma endregion ObjVector_Strand
