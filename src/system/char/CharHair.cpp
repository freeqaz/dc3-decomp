#include "char/CharHair.h"
#include "char/CharCollide.h"
#include "char/Character.h"
#include "math/Geo.h"
#include "math/Rot.h"
#include "obj/Object.h"
#include "obj/Task.h"
#include "os/Timer.h"
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

void CharHair::FreezePoseRaw() {
    for (int i = 0; i < mStrands.size(); i++) {
        Strand &strand = mStrands[i];
        if (strand.Root() && strand.Root()->TransParent()) {
            ObjVector<Point> &pts = strand.Points();
            Transform parentXfm(strand.Root()->TransParent()->WorldXfm());
            Invert(parentXfm, parentXfm);
            for (int j = 0; j < pts.size(); j++) {
                Multiply(pts[j].pos, parentXfm, pts[j].unk78);
            }
        }
    }
}

void CharHair::DoReset(int reset) {
    for (int i = 0; i < mStrands.size(); i++) {
        Strand &strand = mStrands[i];
        if (strand.Root() && strand.Root()->TransParent()) {
            ObjVector<Point> &pts = strand.Points();
            Transform parentXfm(strand.Root()->TransParent()->WorldXfm());
            Vector3 strandRootPos(strand.Root()->WorldXfm().v);
            Vector3 strandRootX(strand.Root()->WorldXfm().m.x);
            for (int j = 0; j < pts.size(); j++) {
                Point &pt = pts[j];
                Multiply(pt.unk78, parentXfm, pt.pos);
                Vector3 fromPrev;
                Subtract(pt.pos, strandRootPos, fromPrev);
                strandRootPos = pt.pos;
                Cross(strandRootX, fromPrev, pt.lastZ);
                Normalize(pt.lastZ, pt.lastZ);
                Cross(fromPrev, pt.lastZ, strandRootX);
                pt.force.Zero();
                pt.lastFriction.Zero();
            }
        }
    }
    bool savedSim = mSimulate;
    float savedInertia = mInertia;
    float savedFriction = mFriction;
    mSimulate = true;
    mInertia = 0;
    mFriction = 0;
    SimulateLoops(reset, GetFPS());
    mSimulate = savedSim;
    mFriction = savedFriction;
    mInertia = savedInertia;
    mReset = 0;
}

void CharHair::SimulateLoops(int count, float fps) {
    if (!mSimulate || mStrands.size() == 0)
        return;
    START_AUTO_TIMER("char_hair");
    for (ObjPtrList<CharCollide>::iterator it = mCollides.begin(); it != mCollides.end(); ++it) {
        (*it)->SyncWorldState();
    }
    for (int n = 0; n < count; n++) {
        SimulateInternal(fps);
    }
}

void CharHair::SimulateInternal(float fps) {
    float sixtyOver = 60.0f / fps;
    float timeStep = (1.0f / fps) * sixtyOver;
    float stiffPow = std::pow(1.0f - mStiffness, sixtyOver * sixtyOver);
    Vector3 windForce(0, 0, 0);
    if (mWindObj && mStrands.size() > 0) {
        if (mStrands[0].Root()) {
            float secs = TheTaskMgr.Seconds(TaskMgr::kRealTime);
            mWindObj->GetWind(mStrands[0].Root()->WorldXfm().v, secs, windForce);
            windForce *= timeStep * 0.5f;
        }
    }
    windForce.z = windForce.z + mGravity * timeStep * -3.858268f;

    for (int i = 0; i < mStrands.size(); i++) {
        Strand &modStrand = mStrands[Mod(i + 1, mStrands.size())];
        Strand &curStrand = mStrands[i];
        if (curStrand.Root() && curStrand.Root()->TransParent()) {
            Transform strandXfm;
            strandXfm.v = curStrand.Root()->WorldXfm().v;
            Multiply(
                curStrand.RootMat(),
                curStrand.Root()->TransParent()->WorldXfm().m,
                strandXfm.m
            );
            ObjVector<Point> &points = curStrand.Points();
            for (int j = 0; j < points.size(); j++) {
                Point &pt = points[j];
                Vector3 oldPos(pt.pos);
                pt.pos += pt.force;
                pt.pos += windForce;
                if (pt.sideLength >= 0.0f) {
                    Vector3 toMod;
                    Point &modPt = modStrand.Points()[j];
                    Subtract(pt.pos, modPt.pos, toMod);
                    float distSq = LengthSquared(toMod);
                    float minLen = pt.sideLength - mMinSlack;
                    float minLenSq = minLen * minLen;
                    if (distSq < minLenSq) {
                        toMod *= (minLenSq / (minLenSq + distSq) - 0.5f);
                        pt.pos += toMod;
                        modPt.pos -= toMod;
                    } else {
                        float maxLen = pt.sideLength + mMaxSlack;
                        float maxLenSq = maxLen * maxLen;
                        if (maxLen > maxLenSq) {
                            toMod *= (maxLenSq / (maxLenSq + distSq) - 0.5f);
                            pt.pos += toMod;
                            modPt.pos -= toMod;
                        }
                    }
                }
                Hmx::Matrix3 boneFrame;
                Subtract(pt.pos, strandXfm.v, boneFrame.y);
                float boneLen = Length(boneFrame.y);
                float recipLen = boneLen > 0 ? (1.0f / boneLen) : 0.0f;
                float lenDiff = -(1.0f - pt.length * recipLen);
                if (j > 0) {
                    ScaleAdd(points[j - 1].force, boneFrame.y, -sixtyOver * 0.5f * lenDiff, points[j - 1].force);
                }
                ScaleAdd(pt.pos, boneFrame.y, lenDiff, pt.pos);
                Vector3 idealPos;
                ScaleAdd(strandXfm.v, strandXfm.m.y, pt.length, idealPos);
                Interp(pt.lastZ, strandXfm.m.z, mTorsion, boneFrame.z);

                // Collision
                if (pt.collides.size() != 0) {
                    float diffRad = pt.outerRadius - pt.radius;
                    float maxRad = Max(pt.radius, pt.outerRadius);
                    for (ObjPtrList<CharCollide>::iterator it = pt.collides.begin();
                         it != pt.collides.end();
                         ++it) {
                        CharCollide *col = *it;
                        float colRad = col->GetCurRadius();
                        Vector3 toPt;
                        Subtract(pt.pos, col->WorldXfm().v, toPt);
                        float dist = Length(toPt);
                        CharCollide::Shape colShape = col->GetShape();
                        if (colShape == CharCollide::kCollideSphere || colShape == CharCollide::kCollideCigar) {
                            float sumRad = colRad + maxRad;
                            if (dist < sumRad && dist > 0) {
                                float push = sumRad / dist - 1.0f;
                                ScaleAdd(pt.pos, toPt, push, pt.pos);
                                Scale(toPt, 1.0f / dist, boneFrame.z);
                            }
                        } else if (colShape == CharCollide::kCollideInsideSphere || colShape == CharCollide::kCollideInsideCigar) {
                            float minRad = colRad - maxRad;
                            if (dist > minRad && dist > 0) {
                                float pull = minRad / dist - 1.0f;
                                ScaleAdd(pt.pos, toPt, pull, pt.pos);
                                Scale(toPt, -1.0f / dist, boneFrame.z);
                            }
                        }
                    }
                }

                Subtract(pt.pos, strandXfm.v, boneFrame.y);
                float newBoneLen = Length(boneFrame.y);
                if (newBoneLen > 0)
                    boneFrame.y *= (1.0f / newBoneLen);
                Cross(boneFrame.y, boneFrame.z, strandXfm.m.x);
                float xLen = Length(strandXfm.m.x);
                if (xLen > 0)
                    strandXfm.m.x *= (1.0f / xLen);
                Cross(strandXfm.m.x, boneFrame.y, strandXfm.m.z);
                strandXfm.m.y = boneFrame.y;
                pt.lastZ = strandXfm.m.z;
                if (pt.bone)
                    pt.bone->SetWorldXfm(strandXfm);
                Subtract(idealPos, pt.pos, pt.force);
                Vector3 frictionDiff;
                Subtract(pt.lastFriction, pt.force, frictionDiff);
                pt.lastFriction = pt.force;
                pt.force *= 1.0f - stiffPow;
                ScaleAdd(pt.force, frictionDiff, -mFriction, pt.force);
                Vector3 movement;
                Subtract(pt.pos, oldPos, movement);
                ScaleAdd(pt.force, movement, mInertia, pt.force);
                strandXfm.v = pt.pos;
            }
        }
    }
}

void CharHair::Hookup(ObjPtrList<CharCollide> &collides) {
    mCollides.clear();
    for (int i = 0; i < mStrands.size(); i++) {
        Strand &strand = mStrands[i];
        if (strand.Root()) {
            for (int j = 0; j < strand.Points().size(); j++) {
                strand.Points()[j].collides.clear();
            }
            for (ObjPtrList<CharCollide>::iterator it = collides.begin();
                 it != collides.end();
                 ++it) {
                CharCollide *col = *it;
                bool rootAncestor = false;
                for (RndTransformable *t = strand.Root(); t != nullptr; t = t->TransParent()) {
                    if (t == col)
                        rootAncestor = true;
                }
                if (!rootAncestor) {
                    mCollides.push_back(col);
                    bool addedToPoint = false;
                    for (int j = 0; j < strand.Points().size(); j++) {
                        Point &pt = strand.Points()[j];
                        bool boneAncestor = false;
                        if (pt.bone) {
                            for (RndTransformable *t = pt.bone; t != nullptr; t = t->TransParent()) {
                                if (t == col)
                                    boneAncestor = true;
                            }
                        }
                        if (!boneAncestor) {
                            pt.collides.push_back(col);
                            addedToPoint = true;
                        }
                    }
                }
            }
        }
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

CharHair::Point::Point(Hmx::Object *owner)
    : bone(owner), length(0.0f), collides(owner), radius(0.0f), outerRadius(0.0f),
      sideLength(-1.0f) {
    pos.Zero();
    force.Zero();
    lastFriction.Zero();
    lastZ.Zero();
    unk78.Zero();
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

void CharHair::Strand::SetRoot(RndTransformable *trans) {
    mRoot = trans;
    if (!mRoot) {
        mPoints.resize(0);
    } else {
        float savedLength = mPoints.size() != 0 ? mPoints.back().length : 0.0f;
        mBaseMat = mRoot->LocalXfm().m;
        SetAngle(mAngle);

        // Count chain depth by walking Children until leaf
        int depth = 0;
        for (RndTransformable *it = mRoot; ; it = it->Children().front()) {
            depth++;
            if (it->Children().empty())
                break;
        }

        mPoints.resize(depth);
        // Assign bones: root gets points[0], then walk children
        mPoints[0].bone = mRoot;
        if (!mRoot->Children().empty()) {
            int idx = 0;
            for (RndTransformable *it = mRoot; !it->Children().empty(); ) {
                idx++;
                it = it->Children().front();
                mPoints[idx].bone = it;
            }
        }

        // Set length and pos for all but last point
        Point *prevPt = nullptr;
        for (int i = 1; i < (int)mPoints.size(); i++) {
            Point &prevPoint = mPoints[i - 1];
            prevPt = &prevPoint;
            RndTransformable *nextBone = mPoints[i].bone;
            prevPoint.length = nextBone->LocalXfm().v.y;
            prevPoint.pos = nextBone->WorldXfm().v;
        }

        // Set last point's length and pos
        Point &lastPt = mPoints.back();
        if (savedLength != 0.0f) {
            lastPt.length = savedLength;
        } else if (prevPt) {
            lastPt.length = prevPt->length;
        } else {
            lastPt.length = gUnitsPerMeter * 0.127f;
        }
        ScaleAdd(lastPt.bone->WorldXfm().v, lastPt.bone->WorldXfm().m.y, lastPt.length, lastPt.pos);
    }
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
