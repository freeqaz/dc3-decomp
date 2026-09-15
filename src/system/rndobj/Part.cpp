#include "rndobj\Part.h"
#include "math/Geo.h"
#include "math/Rand.h"
#include "math\Rot.h"
#include "math\Trig.h"
#include "obj\Data.h"
#include "obj\DataFunc.h"
#include "obj/Object.h"
#include "obj/Task.h"
#include "os\System.h"
#include "os\Timer.h"
#include "rndobj\Anim.h"
#include "rndobj\Draw.h"
#include "rndobj\Mesh.h"
#include "rndobj\Poll.h"
#include "rndobj\Trans.h"
#include "rndobj\Utl.h"
#include "rndobj\Mat.h"
#include "os\File.h"
#include "utl/BinStream.h"

// Matrix-times-column-vector, out.i = Dot(m.i, v): the same TU-local overload
// char/CharLookAt.cpp carries (Mtx.h's (Vector3, Matrix3) form is the
// transpose). MoveParticles rotates the scaled force this way -- the image
// seeds each row from the z term and folds y then x (826BF58C `fmadds f13,
// f5(0x22c=m.y.x), f9(v.x), f12`), and homes &m.z / &m.y but never &m.x
// (offset 0 of the reference is not homed), so the argument is the matrix
// reference itself, not three row references.
inline void Multiply(const Hmx::Matrix3 &m, const Vector3 &v, Vector3 &out) {
    out.Set(Dot(v, m.x), Dot(v, m.y), Dot(v, m.z));
}
#include "utl/Loader.h"
#include <cmath>

PartOverride gNoPartOverride;
ParticleCommonPool *gParticlePool;

namespace {
    int ParticlePoolSize() {
        return SystemConfig("rnd", "particlesys", "global_limit")->Int(1);
    }

    DataNode PrintParticlePoolSize(DataArray *) {
        MILO_LOG("Particle Pool Size:\n");
        if (gParticlePool) {
            int size = ParticlePoolSize();
            MILO_LOG(
                "   %d particles can be allocated, %.1f KB.\n",
                size,
                (float)((unsigned int)(size * 200) * 0.0009765625f)
            );
            MILO_LOG(
                "   %d particles active, %d is the high water mark.\n",
                gParticlePool->NumActiveParticles(),
                gParticlePool->HighWaterMark()
            );
            MILO_LOG(
                "   Adding 30%%, suggesting a particle global limit of %d (set in default.dta).\n",
                (int)(gParticlePool->HighWaterMark() * 1.3f)
            );
        }
        return 0;
    }
}

BinStream &operator<<(BinStream &bs, const RndParticle &p) {
    bs << p.pos << p.col << p.size;
    return bs;
}

BinStream &operator>>(BinStream &bs, RndParticle &p) {
    bs >> p.pos >> p.col >> p.size;
    return bs;
}

PartOverride::PartOverride() throw()
    : mask(0), life(0), speed(0), deltaSize(0), startColor(0), midColor(0), endColor(0),
      pitch(0, 0), yaw(0, 0), mesh(0), box(Vector3(0, 0, 0), Vector3(0, 0, 0)) {}

void InitParticleSystem() {
    if (!gParticlePool) {
        gParticlePool = new ParticleCommonPool();
    }
    if (gParticlePool) {
        gParticlePool->InitPool();
    }
    DataRegisterFunc("print_particle_pool_size", PrintParticlePoolSize);
}

void ParticleCommonPool::InitPool() {
    int size = ParticlePoolSize();
    mPoolParticles = new RndFancyParticle[size];
    for (int i = 0; i < size - 1; i++) {
        mPoolParticles[i].prev = nullptr;
        mPoolParticles[i].next = &mPoolParticles[i + 1];
    }
    mPoolParticles[size - 1].prev = nullptr;
    mPoolParticles[size - 1].next = nullptr;
    mPoolFreeParticles = mPoolParticles;
}

RndParticle *ParticleCommonPool::AllocateParticle() {
    RndParticle *cur = mPoolFreeParticles;
    RndParticle *ret = nullptr;
    if (cur) {
        mPoolFreeParticles = cur->next;
        cur->prev = cur;
        mNumActiveParticles++;
        ret = cur;
        if (mNumActiveParticles > mHighWaterMark) {
            mHighWaterMark = mNumActiveParticles;
        }
    }
    return ret;
}

BEGIN_CUSTOM_PROPSYNC(Attractor)
    SYNC_PROP(attractor, o.mAttractor)
    SYNC_PROP(strength, o.mStrength)
END_CUSTOM_PROPSYNC

BinStream &operator<<(BinStream &bs, const Attractor &a) {
    a.Save(bs);
    return bs;
}

void Attractor::Save(BinStream &bs) const {
    bs << mAttractor;
    bs << mStrength;
}

void Attractor::Load(BinStreamRev &d) {
    d >> mAttractor;
    d >> mStrength;
}

BinStreamRev &operator>>(BinStreamRev &d, Attractor &a) {
    a.Load(d);
    return d;
}

RndParticleSys::RndParticleSys()
    : mType(kBasic), mMaxParticles(0), mPersistentParticles(nullptr),
      mFreeParticles(nullptr), mActiveParticles(nullptr), mNumActive(0), mEmitCount(0),
      mFrameDrive(0), mLastFrame(0), mDrawCount(0), mPauseOffscreen(0), mPausedTime(0),
      mBubblePeriod(10, 10), mBubbleSize(1, 1), mLife(100, 100), mBoxExtent1(0, 0, 0),
      mBoxExtent2(0, 0, 0), mSpeed(1, 1), mPitch(0, 0), mYaw(0, 0), mEmitRate(1, 1),
      mStartSize(gUnitsPerMeter / 4, gUnitsPerMeter / 4), mDeltaSize(0, 0),
      mStartColorLow(1, 1, 1), mStartColorHigh(1, 1, 1), mEndColorLow(1, 1, 1),
      mEndColorHigh(1, 1, 1), mMeshEmitter(this), mMat(this), mPreserveParticles(0),
      mMotionParent(this), mBounce(this), mForceDir(0, 0, 0), mDrag(0), mBubble(0),
      mFastForward(0), mNeedForward(0), mRotate(0), mRPM(0, 0), mRPMDrag(0),
      mRandomDirection(1), mStartOffset(0, 0), mEndOffset(0, 0), mAlignWithVelocity(0),
      mStretchWithVelocity(0), mConstantArea(0), mPerspectiveStretch(0), mStretchScale(1),
      mScreenAspect(1), mSubSamples(0), mGrowRatio(0), mShrinkRatio(1),
      mMidColorRatio(0.5), mMidColorLow(1, 1, 1), mMidColorHigh(1, 1, 1),
      mBirthMomentum(0), mBirthMomentumAmount(1), mMaxBurst(0), mTimeTillBurst(0),
      mBurstInterval(15, 35), mBurstPeak(4, 8), mBurstLength(20, 30), mExplicitParts(0),
      mElapsedTime(0), mAnimateUVs(0), mLoopUVAnim(1), mRandomAnimStart(0),
      mTileHoldTime(0), mNumTilesAcross(1), mNumTilesDown(1), mNumTilesTotal(1),
      mStartingTile(0), mTotalTileTime(1), mInvTotalTileTime(1), mAttractors(this) {
    SetRelativeMotion(0, this);
    SetSubSamples(0);
}

bool RndParticleSys::Replace(ObjRef *ref, Hmx::Object *obj) {
    if (ref == &mMotionParent) {
        RndTransformable *trans = dynamic_cast<RndTransformable *>(obj);
        SetRelativeMotion(mRelativeMotion, trans);
        return true;
    }
    return RndTransformable::Replace(ref, obj);
}

RndParticleSys::~RndParticleSys() {
    if (mPreserveParticles) {
        if (mPersistentParticles)
            delete[] mPersistentParticles;
    } else if (mActiveParticles) {
        for (RndParticle *p = mActiveParticles; p != nullptr; p = FreeParticle(p))
            ;
    }
}

BEGIN_HANDLERS(RndParticleSys)
    HANDLE_EXPR(hi_emit_rate, Max(mEmitRate.x, mEmitRate.y))
    HANDLE(set_start_color, OnSetStartColor)
    HANDLE(set_end_color, OnSetEndColor)
    HANDLE(set_start_color_int, OnSetStartColorInt)
    HANDLE(set_end_color_int, OnSetEndColorInt)
    HANDLE(set_emit_rate, OnSetEmitRate)
    HANDLE(set_burst_interval, OnSetBurstInterval)
    HANDLE(set_burst_peak, OnSetBurstPeak)
    HANDLE(set_burst_length, OnSetBurstLength)
    HANDLE(add_emit_rate, OnAddEmitRate)
    HANDLE(launch_part, OnExplicitPart)
    HANDLE(launch_parts, OnExplicitParts)
    HANDLE(set_life, OnSetLife)
    HANDLE(set_speed, OnSetSpeed)
    HANDLE(set_rotate, OnSetRotate)
    HANDLE(set_swing_arm, OnSetSwingArm)
    HANDLE(set_drag, OnSetDrag)
    HANDLE(set_alignment, OnSetAlignment)
    HANDLE(set_start_size, OnSetStartSize)
    HANDLE(set_mat, OnSetMat)
    HANDLE(set_pos, OnSetPos)
    HANDLE_ACTION(set_mesh, SetMesh(_msg->Obj<RndMesh>(2)))
    HANDLE(active_particles, OnActiveParticles)
    HANDLE_EXPR(max_particles, mMaxParticles)
    HANDLE_ACTION(
        set_relative_parent,
        SetRelativeMotion(mRelativeMotion, _msg->Obj<RndTransformable>(2))
    )
    HANDLE_ACTION(clear_all_particles, FreeAllParticles())
    HANDLE_SUPERCLASS(RndDrawable)
    HANDLE_SUPERCLASS(RndAnimatable)
    HANDLE_SUPERCLASS(RndTransformable)
    HANDLE_SUPERCLASS(RndPollable)
    HANDLE_SUPERCLASS(Hmx::Object)
END_HANDLERS

bool AngleVectorSync(Vector2 &vec, DataNode &_val, DataArray *_prop, int _i, PropOp _op) {
    if (_i == _prop->Size())
        return true;
    else {
        Symbol sym = _prop->Sym(_i);
        static Symbol x("x");
        static Symbol y("y");
        float *coord = nullptr;
        if (sym == x) {
            coord = &vec.x;
            goto sync;
        } else if (sym == y) {
            coord = &vec.y;
            goto sync;
        } else
            return false;
    sync:
        if (_op == kPropSet)
            *coord = DegreesToRadians(_val.Float());
        else if (_op == kPropGet)
            _val = RadiansToDegrees(*coord);
        else
            return false;
    }
    return true;
}

BEGIN_PROPSYNCS(RndParticleSys)
    SYNC_PROP(mat, mMat)
    SYNC_PROP_SET(animate_uvs, mAnimateUVs, SetAnimatedUV(_val.Int()))
    SYNC_PROP(loop_uv_anim, mLoopUVAnim)
    SYNC_PROP(random_anim_start, mRandomAnimStart)
    SYNC_PROP_SET(tile_hold_time, mTileHoldTime, SetTileHoldTime(_val.Float()))
    SYNC_PROP_SET(num_tiles_across, mNumTilesAcross, mNumTilesAcross = Max(_val.Int(), 1))
    SYNC_PROP_SET(num_tiles_down, mNumTilesDown, mNumTilesDown = Max(_val.Int(), 1))
    SYNC_PROP_SET(num_tiles_total, mNumTilesTotal, SetNumTiles(_val.Int()))
    SYNC_PROP(starting_tile, mStartingTile)
    SYNC_PROP_SET(max_parts, mMaxParticles, SetPool(_val.Int(), mType))
    SYNC_PROP(emit_rate, mEmitRate)
    SYNC_PROP(screen_aspect, mScreenAspect)
    SYNC_PROP(life, mLife)
    SYNC_PROP(speed, mSpeed)
    SYNC_PROP(start_size, mStartSize)
    SYNC_PROP(delta_size, mDeltaSize)
    SYNC_PROP(force_dir, mForceDir)
    SYNC_PROP(bounce, mBounce)
    SYNC_PROP(start_color_low, mStartColorLow)
    SYNC_PROP(start_color_high, mStartColorHigh)
    SYNC_PROP(start_alpha_low, mStartColorLow.alpha)
    SYNC_PROP(start_alpha_high, mStartColorHigh.alpha)
    SYNC_PROP(end_color_low, mEndColorLow)
    SYNC_PROP(end_color_high, mEndColorHigh)
    SYNC_PROP(end_alpha_low, mEndColorLow.alpha)
    SYNC_PROP(end_alpha_high, mEndColorHigh.alpha)
    SYNC_PROP(preserve, mPreserveParticles)
    SYNC_PROP_SET(fancy, mType, SetPool(mMaxParticles, (Type)_val.Int()))
    // SYNC_PROP_SET(grow_ratio, mGrowRatio,SetGrowRatio(_val.Float()))
    {
        static Symbol _s("grow_ratio");
        if (sym == _s) {
            if (_op == kPropSet) {
                float f = _val.Float();
                if (f >= 0 && f <= mShrinkRatio) {
                    mGrowRatio = f;
                }
            } else {
                if (_op == (PropOp)0x40)
                    return false;
                _val = mGrowRatio;
            }
            return true;
        }
    }
    SYNC_PROP_SET(shrink_ratio, mShrinkRatio, SetShrinkRatio(_val.Float()))
    SYNC_PROP(drag, mDrag)
    SYNC_PROP(mid_color_ratio, mMidColorRatio)
    SYNC_PROP(mid_color_low, mMidColorLow)
    SYNC_PROP(mid_color_high, mMidColorHigh)
    SYNC_PROP(mid_alpha_low, mMidColorLow.alpha)
    SYNC_PROP(mid_alpha_high, mMidColorHigh.alpha)
    SYNC_PROP(bubble, mBubble)
    SYNC_PROP(bubble_period, mBubblePeriod)
    SYNC_PROP(bubble_size, mBubbleSize)
    SYNC_PROP(max_burst, mMaxBurst)
    SYNC_PROP(time_between, mBurstInterval)
    SYNC_PROP(peak_rate, mBurstPeak)
    SYNC_PROP(duration, mBurstLength)
    SYNC_PROP(spin, mRotate)
    SYNC_PROP(rpm, mRPM)
    SYNC_PROP(rpm_drag, mRPMDrag)
    SYNC_PROP(start_offset, mStartOffset)
    SYNC_PROP(end_offset, mEndOffset)
    SYNC_PROP(random_direction, mRandomDirection)
    SYNC_PROP(velocity_align, mAlignWithVelocity)
    SYNC_PROP(stretch_with_velocity, mStretchWithVelocity)
    SYNC_PROP(stretch_scale, mStretchScale)
    SYNC_PROP(constant_area, mConstantArea)
    SYNC_PROP(perspective, mPerspectiveStretch)
    SYNC_PROP_SET(mesh_emitter, mMeshEmitter.Ptr(), SetMesh(_val.Obj<RndMesh>()))
    SYNC_PROP(box_extent_1, mBoxExtent1)
    SYNC_PROP(box_extent_2, mBoxExtent2) {
        static Symbol _s("pitch");
        if (sym == _s) {
            AngleVectorSync(mPitch, _val, _prop, _i + 1, _op);
            return true;
        }
    }
    {
        static Symbol _s("yaw");
        if (sym == _s) {
            AngleVectorSync(mYaw, _val, _prop, _i + 1, _op);
            return true;
        }
    }
    SYNC_PROP_SET(
        motion_parent,
        mMotionParent.Ptr(),
        SetRelativeMotion(mRelativeMotion, _val.Obj<RndTransformable>())
    )
    SYNC_PROP_SET(
        relative_motion, mRelativeMotion, SetRelativeMotion(_val.Float(), mMotionParent)
    )
    SYNC_PROP_SET(subsamples, mSubSamples, SetSubSamples(_val.Int()))
    SYNC_PROP_SET(frame_drive, mFrameDrive, SetFrameDrive(_val.Int()))
    SYNC_PROP(pre_spawn, mFastForward)
    SYNC_PROP_SET(pause_offscreen, mPauseOffscreen, SetPauseOffscreen(_val.Int()))
    SYNC_PROP(attractors, mAttractors)
    SYNC_PROP(birth_momentum, mBirthMomentum)
    SYNC_PROP(birth_momentum_amount, mBirthMomentumAmount)
    SYNC_SUPERCLASS(RndAnimatable)
    SYNC_SUPERCLASS(RndTransformable)
    SYNC_SUPERCLASS(RndDrawable)
    SYNC_SUPERCLASS(Hmx::Object)
END_PROPSYNCS

BEGIN_SAVES(RndParticleSys)
    SAVE_REVS(0x29, 0)
    SAVE_SUPERCLASS(Hmx::Object)
    SAVE_SUPERCLASS(RndPollable)
    SAVE_SUPERCLASS(RndAnimatable)
    SAVE_SUPERCLASS(RndTransformable)
    SAVE_SUPERCLASS(RndDrawable)
    bs << mLife;
    bs << mScreenAspect;
    bs << mBoxExtent1;
    bs << mBoxExtent2;
    bs << mSpeed;
    bs << mPitch;
    bs << mYaw;
    bs << mEmitRate;
    bs << mMaxBurst;
    bs << mBurstInterval;
    bs << mBurstPeak;
    bs << mBurstLength;
    bs << mStartSize;
    bs << mDeltaSize;
    bs << mStartColorLow;
    bs << mStartColorHigh;
    bs << mEndColorLow;
    bs << mEndColorHigh;
    bs << mBounce;
    bs << mForceDir;
    bs << mMat;
    bs << mType;
    bs << mGrowRatio;
    bs << mShrinkRatio;
    bs << mMidColorRatio;
    bs << mMidColorLow;
    bs << mMidColorHigh;
    bs << mMaxParticles;
    bs << mBubblePeriod;
    bs << mBubbleSize;
    bs << mBubble;
    bs << mRotate;
    bs << mRPM;
    bs << mRPMDrag;
    bs << mRandomDirection;
    bs << mDrag;
    bs << mStartOffset;
    bs << mEndOffset;
    bs << mAlignWithVelocity;
    bs << mStretchWithVelocity;
    bs << mConstantArea;
    bs << mStretchScale;
    bs << mPerspectiveStretch;
    bs << mRelativeMotion;
    bs << mMotionParent;
    bs << mMeshEmitter;
    bs << mSubSamples;
    bs << mFrameDrive;
    bs << mPauseOffscreen;
    bs << mFastForward;
    bs << mAnimateUVs;
    bs << mTileHoldTime;
    bs << mNumTilesAcross;
    bs << mNumTilesDown;
    bs << mNumTilesTotal;
    bs << mStartingTile;
    bs << mLoopUVAnim;
    bs << mRandomAnimStart;
    bs << mAttractors;
    bs << mBirthMomentum;
    bs << mBirthMomentumAmount;
    bs << mPreserveParticles;
    mNeedForward = mFastForward;
    if (mPreserveParticles) {
        bs << mNumActive;
        for (RndParticle *p = mActiveParticles; p != nullptr; p = p->next) {
            bs << *p;
        }
    }
END_SAVES

BEGIN_COPYS(RndParticleSys)
    COPY_SUPERCLASS(Hmx::Object)
    COPY_SUPERCLASS(RndPollable)
    COPY_SUPERCLASS(RndAnimatable)
    COPY_SUPERCLASS(RndTransformable)
    COPY_SUPERCLASS(RndDrawable)
    CREATE_COPY(RndParticleSys)
    BEGIN_COPYING_MEMBERS
        COPY_MEMBER(mPreserveParticles)
        if (mPreserveParticles) {
            SetPool(c->mMaxParticles, c->mType);
            for (RndParticle *p = c->mActiveParticles; p != nullptr; p = p->next) {
                RndParticle *alloced = AllocParticle();
                if (!alloced)
                    break;
                RndParticle *next = alloced->next;
                RndParticle *prev = alloced->prev;
                *alloced = *p;
                alloced->next = next;
                alloced->prev = prev;
            }
        }
        COPY_MEMBER(mNumActive)
        mLastFrame = GetFrame();
        if (ty != kCopyFromMax) {
            COPY_MEMBER(mLife)
            COPY_MEMBER(mScreenAspect)
            COPY_MEMBER(mBoxExtent1)
            COPY_MEMBER(mBoxExtent2)
            COPY_MEMBER(mSpeed)
            COPY_MEMBER(mPitch)
            COPY_MEMBER(mYaw)
            COPY_MEMBER(mEmitRate)
            COPY_MEMBER(mMaxBurst)
            COPY_MEMBER(mBurstInterval)
            COPY_MEMBER(mBurstPeak)
            COPY_MEMBER(mBurstLength)
            COPY_MEMBER(mStartSize)
            COPY_MEMBER(mDeltaSize)
            COPY_MEMBER(mStartColorLow)
            COPY_MEMBER(mStartColorHigh)
            COPY_MEMBER(mEndColorLow)
            COPY_MEMBER(mEndColorHigh)
            COPY_MEMBER(mBounce)
            COPY_MEMBER(mForceDir)
            COPY_MEMBER(mMat)
            COPY_MEMBER(mBubblePeriod)
            COPY_MEMBER(mBubbleSize)
            COPY_MEMBER(mBubble)
            COPY_MEMBER(mRotate)
            COPY_MEMBER(mRPM)
            COPY_MEMBER(mRPMDrag)
            COPY_MEMBER(mRandomDirection)
            COPY_MEMBER(mDrag)
            COPY_MEMBER(mStartOffset)
            COPY_MEMBER(mEndOffset)
            COPY_MEMBER(mAlignWithVelocity)
            COPY_MEMBER(mStretchWithVelocity)
            COPY_MEMBER(mConstantArea)
            COPY_MEMBER(mPerspectiveStretch)
            COPY_MEMBER(mStretchScale)
            COPY_MEMBER(mFastForward)
            mNeedForward = mFastForward;
            COPY_MEMBER(mGrowRatio)
            COPY_MEMBER(mShrinkRatio)
            COPY_MEMBER(mMidColorRatio)
            COPY_MEMBER(mMidColorLow)
            COPY_MEMBER(mMidColorHigh)
            COPY_MEMBER(mMeshEmitter)
            COPY_MEMBER(mFrameDrive)
            COPY_MEMBER(mPauseOffscreen)
            mElapsedTime = mPausedTime = 0;
            COPY_MEMBER(mAnimateUVs)
            COPY_MEMBER(mLoopUVAnim)
            COPY_MEMBER(mRandomAnimStart)
            COPY_MEMBER(mTileHoldTime)
            COPY_MEMBER(mNumTilesAcross)
            COPY_MEMBER(mNumTilesDown)
            COPY_MEMBER(mNumTilesTotal)
            COPY_MEMBER(mStartingTile)
            COPY_MEMBER(mTotalTileTime)
            COPY_MEMBER(mInvTotalTileTime)
            COPY_MEMBER(mBirthMomentum)
            COPY_MEMBER(mBirthMomentumAmount)
            mAttractors.clear();
            for (unsigned int i = 0; i != c->mAttractors.size(); i++) {
                mAttractors.push_back(Attractor(c->mAttractors[i], this));
            }
            if (!mPreserveParticles) {
                SetPool(c->mMaxParticles, c->mType);
            }
            // A source that is its own motion parent copies as "relative to
            // self"; the identity test is on the Hmx::Object, not the
            // RndTransformable subobject.
            const Hmx::Object *srcParent = c->mMotionParent.Ptr();
            SetRelativeMotion(
                c->mRelativeMotion, srcParent == c ? this : c->mMotionParent.Ptr()
            );
            SetSubSamples(c->mSubSamples);
        }
    END_COPYING_MEMBERS
END_COPYS

void RndParticleSys::SetFrame(float frame, float blend) {
    RndAnimatable::SetFrame(frame, blend);
    if (mFrameDrive) {
        UpdateParticles();
        mLastFrame = frame;
        mPausedTime = 0;
    }
}

float RndParticleSys::EndFrame() {
    if (mFrameDrive) {
        return Max(mLife.x, mLife.y);
    } else
        return 0;
}

void RndParticleSys::Enter() {
    mNeedForward = mFastForward;
    mElapsedTime = 0;
    RndPollable::Enter();
}

void RndParticleSys::Poll() {
    if (!mFrameDrive) {
        mElapsedTime += (GetRate() == k30_fps_ui ? TheTaskMgr.DeltaUISeconds()
                                                 : TheTaskMgr.DeltaSeconds())
            * 30.0f;
        if (mDrawCount == 0) {
            if (Showing()
                && (mActiveParticles || mExplicitParts || mEmitRate.x > 0
                    || mEmitRate.y > 0 || mMaxBurst > 0)) {
                UpdateRelativeXfm();
                UpdateParticles();
            } else {
                mLastFrame = CalcFrame();
            }
        } else if (mActiveParticles && mDrawCount % 60 == 0 && !mPreserveParticles) {
            float currentFrame = CalcFrame();
            RndParticle *p = mActiveParticles;
            while (p) {
                bool dead = currentFrame >= p->deathFrame || currentFrame < p->birthFrame;
                if (dead) {
                    p = FreeParticle(p);
                } else {
                    p = p->next;
                }
            }
        }
        if (mSubSamples > 0 && Dirty()) {
            MakeLocToRel(mSubSampleXfm);
        }
        mDrawCount++;
    }
}

void RndParticleSys::UpdateSphere() {
    Sphere s;
    MakeWorldSphere(s, true);
    Transform tf;
    FastInvert(WorldXfm(), tf);
    Multiply(s, tf, s);
    SetSphere(s);
}

void RndParticleSys::DrawShowing() {
    if (mFrameDrive) {
        UpdateRelativeXfm();
    } else {
        if (mDrawCount > 1) {
            UpdateRelativeXfm();
            UpdateParticles();
        } else if (mRelativeMotion == 1) {
            UpdateRelativeXfm();
        }
        mDrawCount = 0;
    }
#ifdef HX_NATIVE
    extern void DrawParticlesBillboard(RndParticleSys*);
    DrawParticlesBillboard(this);
#endif
}

void RndParticleSys::Mats(std::list<RndMat *> &mats, bool) {
    if (mMat) {
        MatShaderOptions shaderOpts = GetDefaultMatShaderOpts(this, mMat);
        mMat->SetShaderOpts(shaderOpts);
        mats.push_back(mMat);
    }
}

INIT_REVS(0x29, 0)

BEGIN_LOADS(RndParticleSys)
    LOAD_REVS(bs)
    ASSERT_REVS(0x29, 0)
    if (d.rev > 0x16) {
        LOAD_SUPERCLASS(Hmx::Object)
    }
    if (d.rev > 0x1B) {
        LOAD_SUPERCLASS(RndPollable)
    }
    if (d.rev > 0) {
        LOAD_SUPERCLASS(RndAnimatable)
        LOAD_SUPERCLASS(RndTransformable)
        LOAD_SUPERCLASS(RndDrawable)
    }
    d >> mLife;
    if (d.rev > 0x23) {
        d >> mScreenAspect;
    }
    d >> mBoxExtent1;
    d >> mBoxExtent2;
    d >> mSpeed;
    d >> mPitch;
    d >> mYaw;
    d >> mEmitRate;
    if (d.rev > 0x20) {
        d >> mMaxBurst;
        d.stream >> mBurstInterval >> mBurstPeak >> mBurstLength;
    }
    d >> mStartSize;
    if (d.rev > 0xF)
        d >> mDeltaSize;
    d >> mStartColorLow;
    d >> mStartColorHigh;
    d >> mEndColorLow;
    d >> mEndColorHigh;
    if (d.rev > 0x19)
        d >> mBounce;
    else if (d.rev > 1) {
        bool ba7;
        Plane p150;
        d >> ba7;
        if (d.rev > 0xB) {
            d.stream >> (Hmx::Color &)p150;
        } else {
            Vector3 v1;
            // One chain: the target carries the BinStream& returned by each
            // `>>` into the next (it parks it in r29 across the calls) rather
            // than re-deriving d.stream per statement.
            d.stream >> v1 >> p150.a >> p150.b >> p150.c;
            // Term order is deliberate. The target seeds this dot product with
            // the C term (`lfs 0xc8` / `lfs 0x88` / `fmuls` = c*z), folds B in
            // with an fmadds, and folds A and the negation together into a
            // single fnmadds. MSVC seeds the chain with the left-most product
            // of the innermost sum, so naming C first is what puts c*z in the
            // fmuls; the parentheses on their own do nothing, /fp:fast
            // reassociates them away.
            p150.d = -(p150.a * v1.x + (p150.c * v1.z + p150.b * v1.y));
        }
        if (ba7) {
            bool old = TheLoadMgr.EditMode();
            TheLoadMgr.SetEditMode(true);
            const char *bounceName = MakeString("%s_bounce.trans", FileGetBase(Name()));
            mBounce = Dir()->New<RndTransformable>(bounceName);
            TheLoadMgr.SetEditMode(old);
            Transform worldXfm;
            Vector3 v128(reinterpret_cast<Vector3 &>(p150));
            worldXfm.m.z = v128;
            worldXfm.v = p150.On();
            Cross(Vector3(0, 1, 0), v128, worldXfm.m.x);
            Cross(v128, worldXfm.m.x, worldXfm.m.y);
            Normalize(worldXfm.m.x, worldXfm.m.x);
            Normalize(worldXfm.m.y, worldXfm.m.y);
            mBounce->SetWorldXfm(worldXfm);
        }
    } else {
        std::list<Plane> planes;
        d >> planes;
    }
    d >> mForceDir;
    d >> mMat;
    if (d.rev > 0x17 && d.rev < 0x19) {
        char buf[0x80];
        d.stream.ReadString(buf, 0x80);
        if (!mMat && buf[0] != '\0') {
            mMat = LookupOrCreateMat(buf, Dir());
        }
    }
    if (d.rev > 0x11) {
        d >> (int &)mType >> mGrowRatio >> mShrinkRatio >> mMidColorRatio;
        d.stream >> mMidColorLow >> mMidColorHigh;
    } else if (d.rev < 0xD) {
        int i94;
        d >> i94;
    }
    d >> mMaxParticles;

    if (d.rev > 2) {
        if (d.rev < 7) {
            int i98;
            d >> i98;
        } else if (d.rev < 0xD) {
            int i9c;
            d >> i9c;
        }
    }
    if (d.rev > 3) {
        d.stream >> mBubblePeriod >> mBubbleSize >> mBubble;
    }
    if (d.rev > 0x1D) {
        d >> mRotate;
        // mRPM and mRPMDrag ride the same BinStream&: the target's ReadEndian
        // for mRPMDrag reuses the stream returned by the Key<float> read
        // instead of reloading bs from its spill slot.
        d.stream >> mRPM >> mRPMDrag;
        if (d.rev > 0x24) {
            d >> mRandomDirection;
        }
        d >> mDrag;
    }
    if (d.rev > 0x1F) {
        d.stream >> mStartOffset >> mEndOffset;
        d >> mAlignWithVelocity >> mStretchWithVelocity >> mConstantArea >> mStretchScale;
    }
    if (d.rev > 0x21) {
        d >> mPerspectiveStretch;
    }

    if (d.rev > 4 && d.rev < 0xF) {
        bool baf;
        d >> baf;
        ZMode z = baf ? kZModeTransparent : kZModeDisable;
        if (mMat)
            mMat->SetZMode(z);
    }
    if (d.rev > 5 && d.rev < 0x11) {
        String str;
        d >> str;
    }
    if (d.rev == 8) {
        bool b1b0;
        d >> b1b0;
    }
    if (d.rev > 0xC && d.rev < 0xE) {
        int i1a0;
        d >> i1a0;
    }
    if (d.rev > 0x13) {
        d >> mRelativeMotion;
    } else if (d.rev > 0xC) {
        bool b;
        d >> b;
        mRelativeMotion = b;
    }
    if (d.rev > 0x1A) {
        d >> mMotionParent;
    }
    SetRelativeMotion(mRelativeMotion, mMotionParent);
    if (d.rev > 0x12) {
        d >> mMeshEmitter;
    }
    if (d.rev > 0x1E || d.rev == 0x15) {
        d >> mSubSamples;
    }
    SetSubSamples(mSubSamples);
    if (d.rev > 0x1B) {
        d >> mFrameDrive;
    } else {
        mFrameDrive = true;
    }
    if (d.rev > 0x22) {
        d >> mPauseOffscreen;
    } else {
        mPauseOffscreen = false;
    }
    if (d.rev > 0x1C) {
        d >> mFastForward;
    } else {
        mFastForward = false;
    }
    mNeedForward = mFastForward;
    if (d.rev > 0x26) {
        d >> mAnimateUVs;
        float tileHoldTime;
        d >> tileHoldTime;
        d >> mNumTilesAcross;
        d >> mNumTilesDown;
        d >> mNumTilesTotal;
        d >> mStartingTile;
        d >> mLoopUVAnim;
        d >> mRandomAnimStart;
        SetTileHoldTime(tileHoldTime);
    }
    if (d.rev > 0x27) {
        d >> mAttractors;
    }
    if (d.rev > 0x28) {
        d >> mBirthMomentum >> mBirthMomentumAmount;
    }
    if (d.rev > 0xA) {
        d >> mPreserveParticles;
        if (mPreserveParticles) {
            int count;
            d >> count;
            SetPool(mMaxParticles, mType);
            for (int i = 0; i < count; i++) {
                RndParticle *p = AllocParticle();
                if (p) {
                    p->angle = 0;
                    p->swingArm = 0;
                    p->vel.Set(0, 0, 0, 0);
                    d >> *p;
                } else {
                    MILO_NOTIFY_ONCE(
                        "Unable to allocate all particles for %s\n", PathName(this)
                    );
                    RndParticle pp;
                    d >> pp;
                }
            }
        } else {
            SetPool(mMaxParticles, mType);
        }
    } else {
        SetPool(mMaxParticles, mType);
    }
    mPausedTime = 0;
    mLastFrame = GetFrame();
END_LOADS

void RndParticleSys::SetPool(int max, Type ty) {
    if (mPreserveParticles) {
        SetPersistentPool(max, ty);
    } else {
        for (RndParticle *p = mActiveParticles; p != nullptr; p = FreeParticle(p))
            ;
        mType = ty;
        mMaxParticles = max;
        int limit = SystemConfig()
            ? SystemConfig("rnd", "particlesys", "local_limit")->Int(1)
            : mMaxParticles;
        if (mMaxParticles > limit) {
            MILO_NOTIFY(
                "Max particles for %s is too high (%d > %d). The max number of particles has been reset to %d.\n",
                PathName(this),
                mMaxParticles,
                limit,
                limit
            );
            mMaxParticles = limit;
        }
        mActiveParticles = nullptr;
        mNumActive = 0;
        mEmitCount = 0;
    }
}

void RndParticleSys::SetPersistentPool(int max, Type ty) {
    delete[] mPersistentParticles;
    mMaxParticles = max;
    mType = ty;

    // Allocate particle pool based on type
    if (max != 0) {
        if (ty == kFancy) {
            mPersistentParticles = new RndFancyParticle[max];
            RndFancyParticle *fp = (RndFancyParticle *)mPersistentParticles;
            RndFancyParticle *cur;
            // Build linked list: each particle points to the next
            for (int i = 0; i != max; i++) {
                cur = fp++;
                cur->next = fp;
            }
            cur->next = nullptr;
        } else {
            mPersistentParticles = new RndParticle[max];
            RndParticle *p = (RndParticle *)mPersistentParticles;
            RndParticle *cur;
            // Build linked list: each particle points to the next
            for (int i = 0; i != max; i++) {
                cur = p++;
                cur->next = p;
            }
            cur->next = nullptr;
        }
    } else {
        mPersistentParticles = nullptr;
    }

    // Initialize free list and state
    mActiveParticles = nullptr;
    mNumActive = 0;
    mFreeParticles = mPersistentParticles;
    mEmitCount = 0;
}

void RndParticleSys::SetTileHoldTime(float f1) {
    mTileHoldTime = f1;
    mTotalTileTime = mNumTilesTotal * mTileHoldTime;
    float &fref = mTotalTileTime;
    mTotalTileTime = Max(fref, 0.0001f);
    mInvTotalTileTime = 1.0f / fref;
}

void RndParticleSys::SetNumTiles(int num) {
    mNumTilesTotal = Max(num, 1);
    mTotalTileTime = mNumTilesTotal * mTileHoldTime;
    mTotalTileTime = Max(mTotalTileTime, 0.0001f);
    mInvTotalTileTime = 1.0f / mTotalTileTime;
}

void RndParticleSys::SetGrowRatio(float f) {
    if (f >= 0 && f <= mGrowRatio)
        mGrowRatio = f;
}

void RndParticleSys::SetShrinkRatio(float f) {
    if (f >= mGrowRatio && f <= 1.0f)
        mShrinkRatio = f;
}

void RndParticleSys::SetFrameDrive(bool b) {
    mFrameDrive = b;
    if (mFrameDrive) {
        mLastFrame = GetFrame();
    } else
        mDrawCount = 0;
    mPausedTime = 0;
}

void RndParticleSys::SetPauseOffscreen(bool b) {
    mPauseOffscreen = b;
    mPausedTime = 0;
}

void RndParticleSys::SetAnimatedUV(bool b) {
    if (mAnimateUVs != b) {
        SetPool(mMaxParticles, mType);
    }
    mAnimateUVs = b;
}

void RndParticleSys::SetMesh(RndMesh *mesh) {
    if (mesh) {
        SetTransParent(mesh, false);
        SetTransConstraint(RndTransformable::kConstraintParentWorld, 0, false);
        if (!mesh->GetKeepMeshData()) {
            MILO_NOTIFY(
                "keep_mesh_data should be checked for %s.  It's the mesh emitter for %s.\n",
                PathName(mesh),
                PathName(this)
            );
        }
    } else if (mMeshEmitter) {
        SetTransParent(0, false);
        SetTransConstraint(RndTransformable::kConstraintNone, 0, false);
    }
    mMeshEmitter = mesh;
}

RndParticle *RndParticleSys::AllocParticle() {
    RndParticle *p;
    if (mPreserveParticles) {
        p = mFreeParticles;
        if (!mFreeParticles)
            return nullptr;
        mFreeParticles = p->next;
    } else {
        p = gParticlePool->AllocateParticle();
        if (!p) {
            int size = ParticlePoolSize();
            MILO_NOTIFY_ONCE(
                "Can't allocate more particles for %s.\nGlobal max particle limit reached (%d).\n",
                PathName(this),
                size
            )
            return nullptr;
        }
    }
    p->prev = p;
    if (mActiveParticles) {
        mActiveParticles->prev = p;
    }
    p->next = mActiveParticles;
    mActiveParticles = p;
    mNumActive++;
    return p;
}

RndParticle *ParticleCommonPool::FreeParticle(RndParticle *p) {
    if (!p)
        return nullptr;
    else {
        RndParticle *ret = p->next;
        p->next = mPoolFreeParticles;
        p->prev = nullptr;
        mPoolFreeParticles = p;
        mNumActiveParticles--;
        return ret;
    }
}

RndParticle *RndParticleSys::FreeParticle(RndParticle *p) {
    if (!p)
        return nullptr;
    else {
        if (p == mActiveParticles) {
            mActiveParticles = p->next;
        } else {
            p->prev->next = p->next;
        }
        if (p->next) {
            p->next->prev = p->prev;
        }
        if (!p->prev) {
            MILO_FAIL("Already deallocated particle");
        }
        p->prev = nullptr;
        RndParticle *ret = nullptr;
        if (mPreserveParticles) {
            ret = p->next;
            p->next = mFreeParticles;
            mFreeParticles = p;
        } else {
            ret = gParticlePool->FreeParticle(p);
        }
        mNumActive--;
        return ret;
    }
}

void RndParticleSys::MakeLocToRel(Transform &tf) {
    if (mRelativeMotion == 1) {
        if (mMotionParent == this) {
            tf.Reset();
            return;
        }
    }
    Transpose(mRelativeXfm, tf);
    Multiply(WorldXfm(), tf, tf);
}

void RndParticleSys::SetSubSamples(int num) {
    mSubSamples = num;
    Transpose(mRelativeXfm, mSubSampleXfm);
    Multiply(WorldXfm(), mSubSampleXfm, mSubSampleXfm);
}

void RndParticleSys::UpdateRelativeXfm() {
#ifdef HX_NATIVE
    if (!mMotionParent)
        return;
#endif
    if (mRelativeMotion == 1) {
        mRelativeXfm = mMotionParent->WorldXfm();
    } else if (mRelativeMotion) {
        const Transform &worldXfm = mMotionParent->WorldXfm();
        Invert(mLastWorldXfm.m, mLastWorldXfm.m);
        Multiply(mLastWorldXfm.m, worldXfm.m, mLastWorldXfm.m);
        Hmx::Quat q28(0, 0, 0, 1);
        FastInterp(q28, Hmx::Quat(mLastWorldXfm.m), mRelativeMotion, q28);
        MakeRotMatrix(q28, mLastWorldXfm.m);
        Subtract(mRelativeXfm.v, mLastWorldXfm.v, mRelativeXfm.v);
        Multiply(mRelativeXfm, mLastWorldXfm.m, mRelativeXfm);
        Normalize(mRelativeXfm.m, mRelativeXfm.m);
        Interp(mLastWorldXfm.v, worldXfm.v, mRelativeMotion, mLastWorldXfm.v);
        // RESIDUAL (w7-ak, 99.98 canonical): the only 2 rows in this 524-byte
        // function are idx 88/90, the two Y-component loads of this Add --
        // the image loads 0x290 (mLastWorldXfm.v.y) before 0x250
        // (mRelativeXfm.v.y), we load them the other way round.  Pure
        // scheduling of two loads around the intervening 0x28c load: the X and
        // Z components are instruction-identical on both sides, and the
        // `fadds` register order is a consequence, not a source operand order
        // (the image is b+a on all three components, ours is b+a on X and Z
        // and a+b on Y from the SAME source expression).
        // NEGATIVE RESULT: swapping the first two arguments (Add is
        // commutative, and the out param aliases either way) is exactly inert.
        Add(mRelativeXfm.v, mLastWorldXfm.v, mRelativeXfm.v);
    }
    Subtract(mMotionParent->WorldXfm().v, mLastWorldXfm.v, mMotionParentDelta);
    mLastWorldXfm = mMotionParent->WorldXfm();
}

// 97.9 canonical (w7-bw). The breakdown below dates from the 69.3% state and
// is kept for its inventory of what the image does; most of it is closed.
//
// Remaining diff breakdown at 69.3% (614 instructions total):
//   - r29<->r30 register swap: 117 instructions. Target uses r30 for 'this',
//     our compiler picks r29. Unfixable compiler register allocation choice.
//   - 111 deletes: target has dead stores to stack slots 0x60/0x64 where it
//     caches intermediate pointers (addi rX, rBase, offset; stw rX, 0x60, r31).
//     Our compiler optimizes these away. Also target caches &p->pos in r25 and
//     &p->vel in r26 as dedicated pointer registers throughout the inner loop.
//   - 2 diff_ops remaining:
//     (1) idx 126: bounce WorldXfm call uses bl (call) in target vs b (branch)
//         in ours. Target reuses a shared branch point for the two WorldXfm calls.
//     (2) idx 340: attractor strength==0.015625 check uses beq (branch-if-equal
//         to special case) in target vs bne (skip special case) in ours. Target
//         also has dead code after (li 0; clrlwi. 0; beq - always-taken branch),
//         suggesting original code had a boolean variable for the condition.
//   - fmadds vs fmuls+fadds: our compiler fuses multiply-add in position update,
//     bounce reflection (fnmsubs vs fmuls+fsubs), and basic particle color/size.
//     Target uses separate instructions. Hard to prevent without volatile temps.
//   - Stack frame: target 0x1c0, ours larger. Target saves from r14 (savegprlr_14),
//     ours from r17 (3 fewer callee-saved GPRs).
//
// Potential improvements to investigate:
//   - Restructure bounce WorldXfm calls to match target's shared-branch pattern
//   - Try a bool variable for the attractor strength check to match dead code
//   - Volatile or separate-statement tricks to prevent fmadds fusion
//   - Declaration order changes to shift r14-r16 register assignment
//
// RndFancyParticle offset note: header comments are wrong by -8 bytes.
// RndParticle is 0x68 bytes (not 0x60), so RndFancyParticle fields start at 0x68.
// E.g. growFrame comment says 0x60 but actual compiled offset is 0x68,
// midcolFrame comment says 0x80 but actual is 0x88, etc.
void RndParticleSys::MoveParticles(float dt, float frameSpan) {
    START_AUTO_TIMER("psysmove");

    if (mActiveParticles == NULL || frameSpan == 0.0f)
        return;

    // The 1/30 is spelled inline at all three sites, not hoisted into a
    // named local: a named `oneOverThirty` (declared first) flips the
    // commutative operand order of ~12 fmadds/fmuls/fadds rows across the
    // whole body (826BF58C, 826BF6F4, 826BFA98 ...), 97.57 -> 97.23 with
    // the levers below applied; declared after dragFactor it is inert.
    // The registers are still the image's mirror (f30 holds 1/30 and f29
    // the pow result at 826BF4A4/826BF4B8; ours f29/f30), which costs the
    // two reload rows at 826BF9C4/826BF9DC (w7-bw).
    float dragFactor;
    if (mDrag > 0.0f) {
        dragFactor = std::pow(1.0f - mDrag, frameSpan * (1.0f / 30.0f));
    } else {
        dragFactor = 1.0f;
    }

    float rpmDragFactor;
    if (mRotate && mRPMDrag > 0.0f) {
        // Second pow evaluates the base before the exponent (826BF4D0 fsubs,
        // 826BF4D4 fmuls); the first does the reverse. A hoisted `exponent`
        // local is kept in f31 and passed by fmr on both calls (2 -> 3 rows);
        // naming the BASE instead sequences it first and closes both rows
        // (w7-bw).
        float rpmBase = 1.0f - mRPMDrag;
        rpmDragFactor = std::pow(rpmBase, frameSpan * (1.0f / 30.0f));
    } else {
        rpmDragFactor = 1.0f;
    }

    bool isFancy = (mType == kFancy);
    bool isRotate = mRotate;
    bool isBubble = mBubble;

    // The image rotates the scaled force by mRelativeXfm.m as a COLUMN
    // vector: relForce.i = Dot(m.i, force) (826BF58C..826BF59C seed the
    // y/x/z rows from 0x22c/0x21c/0x23c). Multiply(Vector3, Matrix3, Vector3&)
    // is the transpose of that and was a behavioural bug here.
    Vector3 relForce;
    Scale(mForceDir, frameSpan, relForce);
    Multiply(mRelativeXfm.m, relForce, relForce);

    Plane bouncePlane;
    bool bounce = (mBounce != NULL);
    if (bounce) {
        bouncePlane.Set(mBounce->WorldXfm().v, mBounce->WorldXfm().m.z);
    }

    int endTile = mNumTilesTotal + mStartingTile;
    RndParticle *p = mActiveParticles;

    if (p != NULL) {
        float sixf = 6.0f;
        float halfPi = 1.5707963705062866f;
        float epsilon = 1.1920928955078125e-07f;
        float magicStrength = 0.015625f;
        float two = 2.0f;

        do {
            bool dead;
            if (dt >= p->deathFrame || dt < p->birthFrame) {
                dead = true;
            } else {
                dead = false;
            }

            if (dead) {
                p = FreeParticle(p);
            } else {
                // UV tile animation
                if (mAnimateUVs) {
                    float tileTime = p->mTileTime + frameSpan;
                    p->mTileTime = tileTime;
                    if (p->mCurrentTileIndex < endTile && tileTime > mTileHoldTime) {
                        int newTile = p->mCurrentTileIndex + 1;
                        p->mCurrentTileIndex = newTile;
                        if (newTile >= endTile) {
                            if (mLoopUVAnim) {
                                p->mCurrentTileIndex = mStartingTile;
                            } else {
                                p->mCurrentTileIndex = endTile - 1;
                            }
                        }
                        // The image homes tileTime (`stfs f1, 0x64(r31)`,
                        // 826BF720) before `bl fmod`; nothing in the stlport
                        // float wrapper takes it by reference. 1 row, unexplained.
                        p->mTileTime = std::fmod(tileTime, mTileHoldTime);
                    }
                }

                if (isFancy && mBirthMomentum) {
                    RndFancyParticle *fp = (RndFancyParticle *)p;
                    Vector3 birthDelta;
                    Scale(
                        fp->mBirthVel,
                        mBirthMomentumAmount * frameSpan * (1.0f / 30.0f),
                        birthDelta
                    );
                    Add(p->Pos3(), birthDelta, p->Pos3());
                }

                ScaleAddEq(p->Pos3(), p->Vel3(), frameSpan);

                Vector3 &pos = p->Pos3();
                Vector3 &vel = p->Vel3();

                // Bounce plane reflection
                if (bounce) {
                    if (!(pos <= bouncePlane)) {
                        float velDotN = bouncePlane.c * vel.z + bouncePlane.b * vel.y
                            + bouncePlane.a * vel.x;
                        if (velDotN < 0.0f) {
                            // Named products: the image subtracts three
                            // separate fmuls (826BF830-826BF84C), not fnmsubs.
                            float reflect = velDotN * two;
                            float rz = bouncePlane.c * reflect;
                            float rx = bouncePlane.a * reflect;
                            float ry = bouncePlane.b * reflect;
                            vel.z -= rz;
                            vel.x -= rx;
                            vel.y -= ry;
                        }
                    }
                }

                // Attractors. Target recomputes mAttractors.size() each iteration
                // (loop condition calls .size() rather than caching it).
                // The image's back-edge at 826BF9F0 is `cmplw cr6, r22, r10` +
                // `bne` -- an inequality test, not `blt`. Same spelling as the
                // mAttractors walk in Copy().
                for (unsigned int i = 0; i != mAttractors.size(); i++) {
                    Attractor &a = mAttractors[i];
                    if (a.mAttractor != NULL) {
                        const Transform &axf = a.mAttractor->WorldXfm();
                        Vector3 d;
                        Subtract(axf.v, pos, d);
                        float strength = a.mStrength;

                        // The image MATERIALISES this test into a byte before
                        // branching on it (826BF8CC `li r11, 1` / 826BF8D8
                        // `fcmpu` / `li r11, 0` / `clrlwi. r11, r11, 24` /
                        // `beq`), which only a named bool produces.
                        bool isTetherAttractor = strength == magicStrength;
                        if (isTetherAttractor) {
                            d.z = 0.0f;
                            auto _tmp0 = a.mAttractor.Owner();
                            RndParticleSys *ps =
                                dynamic_cast<RndParticleSys *>(_tmp0);
                            if (ps != NULL) {
                                const Transform &t1xf = a.mAttractor->WorldXfm();
                                const Transform &t2xf = ps->WorldXfm();
                                Vector3 rel;
                                Subtract(t2xf.v, t1xf.v, rel);
                                strength *= (rel.x * rel.x + rel.y * rel.y) + epsilon;
                            }
                        }

                        float distSq = d.y * d.y + (d.x * d.x + d.z * d.z) + epsilon;
                        float scale = (strength * frameSpan) / distSq;
                        Vector3 delta;
                        Scale(d, scale, delta);
                        Add(vel, delta, vel);
                    }
                }

                // Residual (5 rows): the image loads vel.z, vel.y, adds z, THEN
                // loads vel.x (826BF9F4-826BFA00), and the drag block below
                // re-reads vel.z after storing it (`fmr f0, f12` / `lfs f12,
                // 0x8(r26)`, 826BFA28-826BFA2C). Add(vel, relForce, vel),
                // Add(relForce, vel, vel) and three explicit `+=` in z, y, x
                // order all load x first and keep z in a register. Named
                // loads in z, y, x order with the sums spelled force-first
                // for x and z and vel-first for y reproduce the image's three
                // fadds operand orders (826BFA00/826BFA10/826BFA14, w7-bw);
                // the hoisted x load stays.
                float vfz = vel.z;
                float vfy = vel.y;
                float vfx = vel.x;
                vel.x = relForce.x + vfx;
                vel.y = vfy + relForce.y;
                vel.z = relForce.z + vfz;

                if (isFancy) {
                    vel.y *= dragFactor;
                    vel.z *= dragFactor;
                    vel.x *= dragFactor;

                    RndFancyParticle *fp = (RndFancyParticle *)p;

                    if (isBubble) {
                        float sinVal =
                            FastSin(fp->bubbleFreq * dt + fp->bubblePhase + halfPi);
                        float bubbleScale = fp->bubbleFreq * sinVal * frameSpan;
                        ScaleAddEq(pos, fp->Bubble3(), bubbleScale);
                    }

                    if (isRotate) {
                        p->angle += fp->RPF * frameSpan;
                        fp->RPF *= rpmDragFactor;
                        p->swingArm += fp->swingArmVel * frameSpan;
                    }

                    // Fancy color: 2-phase blend (before/after midcolFrame).
                    // colorScale = (1-t)*t*frameSpan*6 where t is normalized
                    // time within the current phase. Phase 1 uses midcolVel, phase 2 uses colVel.
                    float cr, cg, cb, ca;
                    if (dt < fp->midcolFrame) {
                        float t = (dt - p->birthFrame) * p->vel.w;
                        float colorScale = (1.0f - t) * t * frameSpan * sixf;
                        Hmx::Color colorDelta;
                        Multiply(fp->midcolVel, colorScale, colorDelta);
                        ca = colorDelta.alpha;
                        cb = colorDelta.blue;
                        cg = colorDelta.green;
                        cr = colorDelta.red;
                    } else {
                        // bubbleDir.w (0xa4), NOT bubblePhase (0xac): the target
                        // reads `lfs f13, 0xa4(r29)` here. bubbleDir is a Vector4
                        // at 0x98, so +0xc is its w. rb3's matched Part.cpp agrees.
                        float t = (dt - fp->midcolFrame) * fp->bubbleDir.w;
                        float colorScale = (1.0f - t) * t * frameSpan * sixf;
                        ca = p->colVel.alpha * colorScale;
                        cb = p->colVel.blue * colorScale;
                        cg = p->colVel.green * colorScale;
                        cr = p->colVel.red * colorScale;
                    }
                    p->col.red = Clamp(0.0f, 1.0f, cr + p->col.red);
                    p->col.alpha = Clamp(0.0f, 1.0f, ca + p->col.alpha);
                    p->col.blue = Clamp(0.0f, 1.0f, cb + p->col.blue);
                    p->col.green = Clamp(0.0f, 1.0f, cg + p->col.green);

                    // Fancy size: 3-phase (grow / sustain / shrink). Each arm
                    // carries the whole update: the compiler merges the common
                    // tail itself, stopping at the per-arm `dt - X` subtraction
                    // (826BFBFC / 826BFC18 / 826BFC24 stay in their arms) and
                    // hoists the shared p->size load to the end of the fork
                    // block (826BFBE8). A shared tail written in source merges
                    // the subtraction too. Residual (4 rows): our allocator puts
                    // shrinkFrame in f0 where the image has f13, so arms 1 and 3
                    // both end `fsubs f0, f24, f0` and get cross-jumped into the
                    // tail; swapping the product's operand order is byte-inert.
                    if (dt < fp->growFrame) {
                        float st = (dt - p->birthFrame) * fp->beginGrow;
                        p->size += fp->growVel * ((1.0f - st) * st * frameSpan * sixf);
                    } else if (dt < fp->shrinkFrame) {
                        float st = (dt - fp->growFrame) * fp->midGrow;
                        p->size += p->sizeVel * ((1.0f - st) * st * frameSpan * sixf);
                    } else {
                        float st = (dt - fp->shrinkFrame) * fp->endGrow;
                        p->size += fp->shrinkVel * ((1.0f - st) * st * frameSpan * sixf);
                    }
                } else {
                    // Basic particle: single-phase color/size update.
                    float t = (dt - p->birthFrame) * p->pos.w;
                    float scale = (1.0f - t) * t * frameSpan * sixf;
                    Hmx::Color colorDelta;
                    Multiply(p->colVel, scale, colorDelta);
                    p->size += p->sizeVel * scale;
                    Add(p->col, colorDelta, p->col);
                }
                p = p->next;
            }
        } while (p != NULL);
    }
}

void RndParticleSys::CreateParticles(float f1, float f2, const Transform &tf) {
    if (f2 <= 0 || mNumActive >= mMaxParticles)
        return;
    else {
        mEmitCount += f2 * RandomFloat(mEmitRate.x, mEmitRate.y);
        mEmitCount += CheckBursts(f2) + (float)mExplicitParts;
        mExplicitParts = 0;
        while (mEmitCount >= 1.0f && mNumActive < mMaxParticles) {
            RndParticle *p = AllocParticle();
            if (!p) {
                mEmitCount = 0;
                return;
            }
            InitParticle(f1, p, &tf, gNoPartOverride);
            mEmitCount -= 1.0f;
        }
    }
}

void RndParticleSys::RunFastForward() {
    mNeedForward = false;

    float avgEmitRate = (mEmitRate.x + mEmitRate.y) * 0.5f;
    if (avgEmitRate < 0.0001f)
        return;

    float stepSize = 1.0f / avgEmitRate;
    float duration = Min(stepSize * (float)mMaxParticles, (mLife.x + mLife.y) * 0.5f);
    stepSize = Max(1.0f, stepSize);
    float currentFrame = CalcFrame();
    Transform xfm;
    MakeLocToRel(xfm);

    float frame;
    for (frame = currentFrame - duration; frame <= currentFrame; frame += stepSize) {
        MoveParticles(frame, stepSize);
        CreateParticles(frame, stepSize, xfm);
    }
}

void RndParticleSys::UpdateParticles() {
    // Return early when the flag is SET: preserving particles means this update
    // (which creates and reaps them) must not run. Target emits
    // `lbz r11, 0x218(r3); cmplwi r11, 0x0; bne <epilogue>` -- branch out when
    // non-zero. Corroborated by every other use in this file: SetPool and the
    // reaping loop are both guarded by !mPreserveParticles.
    if (mPreserveParticles != 0) {
        return;
    }

    f32 currentFrame = CalcFrame();

    if (mLastFrame == 0.0f) {
        mLastFrame = currentFrame;
    }

    if (mNeedForward != 0) {
        RunFastForward();
        if (mFrameDrive == 0) {
            mLastFrame = currentFrame;
        }
    } else {
        f32 frameUpdate = currentFrame - mLastFrame;
        if (mFrameDrive == 0) {
            mLastFrame = currentFrame;
        }

        if (frameUpdate != 0.0f) {
            if (mPauseOffscreen != 0) {
                if (frameUpdate > 4.0f) {
                    float excess = frameUpdate - 4.0f;
                    mPausedTime += excess;
                    frameUpdate = 4.0f;
                }
                currentFrame -= mPausedTime;
            }

            MoveParticles(currentFrame, frameUpdate);

            if ((mExplicitParts != 0) || (mEmitRate.x > 0.0f) || (mEmitRate.y > 0.0f) || (mMaxBurst != 0)) {
                Transform locToRel;
                MakeLocToRel(locToRel);

                if (mSubSamples > 1) {
                    Vector3 baseVel;
                    if (!mMeshEmitter) {
                        f32 halfSample = 0.5f;
                        // Both bounds of each range are named locals, high
                        // first, and the yaw pair is read AFTER the pitch
                        // LimitAng call: the image loads 0x18c then 0x188
                        // (826C4C34/38), calls, then 0x194/0x190 (826C4C48/4C),
                        // and keeps each low bound in f29/f27 across its call
                        // for the `+ lo` term.  Inlining `mPitch.x` into the
                        // expression reloads it after the call (w7-ao, 93.33);
                        // loading yawLo before the first call hoists the
                        // 0x190 load a call too early (94.43); naming only the
                        // low bounds loads x before y (99.98) -- w7-bt, 100.0.
                        f32 pitchHi = mPitch.y;
                        f32 pitchLo = mPitch.x;
                        f32 pitchMid = LimitAng(pitchHi - pitchLo) * halfSample + pitchLo;
                        f32 yawHi = mYaw.y;
                        f32 yawLo = mYaw.x;
                        f32 yawMid = LimitAng(yawHi - yawLo) * halfSample + yawLo;
                        f32 speedMid = (mSpeed.y - mSpeed.x) * halfSample + mSpeed.x;

                        f32 halfPi = 1.57079637f;
                        f32 cosPitch = FastSin(pitchMid + halfPi);
                        f32 negXVel = -(FastSin(yawMid) * cosPitch * speedMid);
                        f32 yVel = FastSin(yawMid + halfPi) * cosPitch * speedMid;
                        f32 sinPitch = FastSin(pitchMid);

                        // The frame scaling is a separate Scale() after the
                        // Set(), not folded into each component: with
                        // `Set(a * frameUpdate, ...)` MSVC hoists the x and y
                        // products and their stores above the final
                        // `bl FastSin` (w7-ao's 14-row residual at 94.43 --
                        // three spellings of the folded form were
                        // byte-identical, and inlining the last FastSin into
                        // the Set argument is too).  Set-then-Scale keeps
                        // negXVel/yVel in f29/f27 across the call and emits
                        // the three `fmuls fN, fN, f31; stfs` pairs after it,
                        // which is the image at 826C4CBC..826C4CD8 (w7-bt,
                        // 94.43 -> 98.87).
                        baseVel.Set(negXVel, yVel, sinPitch * speedMid);
                        Scale(baseVel, frameUpdate, baseVel);

                        Multiply(baseVel, mSubSampleXfm, baseVel);
                    } else {
                        baseVel = mSubSampleXfm.v;
                    }

                    memcpy(&mSubSampleXfm, &locToRel, sizeof(Transform));

                    int count = mSubSamples;
                    f32 stepSize = frameUpdate / (f32)mSubSamples;
                    if (count != 0) {
                        do {
                            CreateParticles(currentFrame, stepSize, locToRel);
                            // The sub-sample walk advances the emitter's own
                            // translation toward baseVel, so each sub-sample is
                            // emitted at an interpolated position.  The image
                            // passes r1+0xa0 as both source and destination at
                            // 826C4D64/826C4D7C, and r1+0x70 is locToRel (the
                            // memcpy into mSubSampleXfm at 826C4D10 names it) --
                            // 0xa0 is locToRel.v, not a separate local.
                            Interp(locToRel.v, baseVel, 1.0f / (f32)count, locToRel.v);
                            count--;
                        } while (count != 0);
                    }
                } else {
                    CreateParticles(currentFrame, frameUpdate, locToRel);
                }
            }
        }
    }
}

void RndParticleSys::FreeAllParticles() {
    for (RndParticle *p = mActiveParticles; p != nullptr; p = FreeParticle(p))
        ;
    mEmitCount = 0;
}

void RndParticleSys::ExplicitParticles(int i1, bool b2, PartOverride &partOverride) {
    if (b2) {
        float frame = CalcFrame();
        Transform tf;
        MakeLocToRel(tf);
        for (int i = 0; i < i1 && mNumActive < mMaxParticles; i++) {
            RndParticle *p = AllocParticle();
            if (!p)
                break;
            InitParticle(frame, p, &tf, partOverride);
        }
    } else {
        mExplicitParts += i1;
    }
}

#define PI 3.1415927f

void RndParticleSys::InitParticle(
    float f1, RndParticle *particle, const Transform *xfm, PartOverride &partOverride
) {
    particle->birthFrame = f1;
    if (partOverride.mask & 1) {
        particle->deathFrame = particle->birthFrame + partOverride.life;
    } else {
        particle->deathFrame = particle->birthFrame + RandomFloat(mLife.x, mLife.y);
    }
    particle->pos.w = particle->deathFrame > particle->birthFrame
        ? 1.0f / (particle->deathFrame - particle->birthFrame)
        : 0;
    RndMesh *mesh = mMeshEmitter;
    if (partOverride.mask & 0x100) {
        mesh = partOverride.mesh;
    }
    if (mesh && !mesh->Faces().empty()) {
        RandomPointOnMesh(mesh, particle->Pos3(), particle->Vel3());
    } else {
        if (partOverride.mask & 0x200) {
            particle->pos.x =
                RandomFloat(partOverride.box.mMin.x, partOverride.box.mMax.x);
            particle->pos.y =
                RandomFloat(partOverride.box.mMin.y, partOverride.box.mMax.y);
            particle->pos.z =
                RandomFloat(partOverride.box.mMin.z, partOverride.box.mMax.z);
        } else {
            particle->pos.x = RandomFloat(mBoxExtent1.x, mBoxExtent2.x);
            particle->pos.y = RandomFloat(mBoxExtent1.y, mBoxExtent2.y);
            particle->pos.z = RandomFloat(mBoxExtent1.z, mBoxExtent2.z);
        }
        float f8, f9;
        if (partOverride.mask & 0x80) {
            f8 = RandomFloat(partOverride.pitch.x, partOverride.pitch.y);
            f9 = RandomFloat(partOverride.yaw.x, partOverride.yaw.y);
        } else {
            f8 = RandomFloat(mPitch.x, mPitch.y);
            f9 = RandomFloat(mYaw.x, mYaw.y);
        }

        float cosPitch = FastCos(f8);
        float sinPitch = FastSin(f9);
        particle->vel.x = -cosPitch * sinPitch;
        particle->vel.y = cosPitch * FastCos(f9);
        particle->vel.z = FastSin(f8);
    }
    particle->Vel3() *=
        partOverride.mask & 2 ? partOverride.speed : RandomFloat(mSpeed.x, mSpeed.y);
    float f11 = particle->deathFrame != particle->birthFrame
        ? 1.0f / (particle->deathFrame - particle->birthFrame)
        : 0;
    if (mRotate) {
        particle->angle = RandomFloat(0, PI * 2);
        particle->swingArm = RandomFloat(mStartOffset.x, mStartOffset.y);
    } else {
        particle->angle = 0;
        particle->swingArm = 0;
    }
    if (partOverride.mask & 0x10) {
        particle->col = partOverride.startColor;
    } else {
        float lowH = 0, lowS = 0, lowL = 0;
        MakeHSL(mStartColorLow, lowH, lowS, lowL);
        float highH = 0, highS = 0, highL = 0;
        MakeHSL(mStartColorHigh, highH, highS, highL);
        MakeColor(
            RandomFloat(lowH, highH),
            RandomFloat(lowS, highS),
            RandomFloat(lowL, highL),
            particle->col
        );
        particle->col.alpha = RandomFloat(mStartColorLow.alpha, mStartColorHigh.alpha);
    }
    if (partOverride.mask & 4) {
        particle->size = partOverride.size;
    } else {
        particle->size = RandomFloat(mStartSize.x, mStartSize.y);
    }
    if (partOverride.mask & 8) {
        particle->sizeVel = partOverride.deltaSize;
    } else {
        particle->sizeVel = RandomFloat(mDeltaSize.x, mDeltaSize.y);
    }
    if (particle->sizeVel < -particle->size) {
        particle->sizeVel = -particle->size;
    }
    if (partOverride.mask & 0x40) {
        particle->colVel = partOverride.endColor;
    } else {
        float lowH = 0, lowS = 0, lowL = 0;
        MakeHSL(mEndColorLow, lowH, lowS, lowL);
        float highH = 0, highS = 0, highL = 0;
        MakeHSL(mEndColorHigh, highH, highS, highL);
        MakeColor(
            RandomFloat(lowH, highH),
            RandomFloat(lowS, highS),
            RandomFloat(lowL, highL),
            particle->colVel
        );
        particle->colVel.alpha = RandomFloat(mEndColorLow.alpha, mEndColorHigh.alpha);
    }
    if (mType == kFancy) {
        RndFancyParticle *fancyParticle = (RndFancyParticle *)particle;
        fancyParticle->mBirthVel = mMotionParentDelta;
        if (mBubble) {
            fancyParticle->bubbleFreq =
                (2 * PI) / RandomFloat(mBubblePeriod.x, mBubblePeriod.y);
            fancyParticle->bubblePhase = RandomFloat(0, 2 * PI);
            float f14 = RandomFloat(0, 2 * PI);
            float f20 = FastCos(f14);
            f14 = FastSin(f14);
            Scale(
                Vector3(f14, 0, f20),
                RandomFloat(mBubbleSize.x, mBubbleSize.y),
                fancyParticle->Bubble3()
            );
            Vector3 toAdd;
            Scale(fancyParticle->Bubble3(), FastSin(fancyParticle->bubblePhase), toAdd);
            Add(fancyParticle->Pos3(), toAdd, fancyParticle->Pos3());
            fancyParticle->bubblePhase =
                -(f1 * fancyParticle->bubbleFreq - fancyParticle->bubblePhase);
        }
        if (mRotate) {
            fancyParticle->RPF = RandomFloat(mRPM.x, mRPM.y) * 0.0034906587f;
            if (mRandomDirection && (RandomInt() & 0x100000)) {
                fancyParticle->RPF = -fancyParticle->RPF;
            }
            fancyParticle->swingArmVel =
                (RandomFloat(mEndOffset.x, mEndOffset.y) - fancyParticle->swingArm) * f11;
        } else {
            fancyParticle->RPF = 0;
            fancyParticle->swingArmVel = 0;
        }
        if (mGrowRatio != 0) {
            fancyParticle->growFrame =
                Interp(fancyParticle->birthFrame, fancyParticle->deathFrame, mGrowRatio);
            fancyParticle->growVel = fancyParticle->growFrame != fancyParticle->birthFrame
                ? fancyParticle->size
                    / (fancyParticle->growFrame - fancyParticle->birthFrame)
                : 0;
        } else {
            fancyParticle->growVel = 0;
            fancyParticle->growFrame = fancyParticle->birthFrame;
        }
        float death = fancyParticle->deathFrame;
        if (mShrinkRatio != 1) {
            fancyParticle->shrinkFrame =
                Interp(fancyParticle->birthFrame, death, mShrinkRatio);
            fancyParticle->shrinkVel = fancyParticle->shrinkFrame != death
                ? (fancyParticle->size + fancyParticle->sizeVel)
                    / (fancyParticle->shrinkFrame - death)
                : 0;
        } else {
            fancyParticle->shrinkVel = 0;
            fancyParticle->shrinkFrame = fancyParticle->birthFrame;
        }
        fancyParticle->beginGrow = fancyParticle->growFrame > fancyParticle->birthFrame
            ? 1.0f / (fancyParticle->growFrame - fancyParticle->birthFrame)
            : 0;
        fancyParticle->midGrow = fancyParticle->shrinkFrame > fancyParticle->growFrame
            ? 1.0f / (fancyParticle->shrinkFrame - fancyParticle->growFrame)
            : 0;
        fancyParticle->endGrow = fancyParticle->deathFrame > fancyParticle->shrinkFrame
            ? 1.0f / (fancyParticle->deathFrame - fancyParticle->shrinkFrame)
            : 0;
        if (mGrowRatio != 0) {
            fancyParticle->size = 0;
        }
        if (fancyParticle->shrinkFrame != fancyParticle->growFrame) {
            f11 = 1.0f / (fancyParticle->shrinkFrame - fancyParticle->growFrame);
        }
        fancyParticle->midcolFrame =
            Interp(fancyParticle->birthFrame, fancyParticle->deathFrame, mMidColorRatio);
        if (partOverride.mask & 0x20) {
            fancyParticle->midcolVel = partOverride.midColor;
        } else {
            fancyParticle->midcolVel.red =
                RandomFloat(mMidColorLow.red, mMidColorHigh.red);
            fancyParticle->midcolVel.green =
                RandomFloat(mMidColorLow.green, mMidColorHigh.green);
            fancyParticle->midcolVel.blue =
                RandomFloat(mMidColorLow.blue, mMidColorHigh.blue);
            fancyParticle->midcolVel.alpha =
                RandomFloat(mMidColorLow.alpha, mMidColorHigh.alpha);
        }
        if (fancyParticle->midcolFrame > fancyParticle->birthFrame) {
            fancyParticle->vel.w =
                1.0f / (fancyParticle->midcolFrame - fancyParticle->birthFrame);
        } else {
            fancyParticle->vel.w = 0;
        }
        fancyParticle->bubbleDir.w =
            fancyParticle->deathFrame > fancyParticle->midcolFrame
            ? 1.0f / (fancyParticle->deathFrame - fancyParticle->midcolFrame)
            : 0;
        Subtract(fancyParticle->colVel, fancyParticle->midcolVel, fancyParticle->colVel);
        if (fancyParticle->deathFrame != fancyParticle->midcolFrame) {
            float scalar =
                1.0f / (fancyParticle->deathFrame - fancyParticle->midcolFrame);
            Multiply(fancyParticle->colVel, scalar, fancyParticle->colVel);
        }
        if (fancyParticle->midcolFrame != fancyParticle->birthFrame) {
            Subtract(
                fancyParticle->midcolVel, fancyParticle->col, fancyParticle->midcolVel
            );
            if (fancyParticle->midcolFrame != fancyParticle->birthFrame) {
                float scalar =
                    1.0f / (fancyParticle->midcolFrame - fancyParticle->birthFrame);
                Multiply(fancyParticle->midcolVel, scalar, fancyParticle->midcolVel);
            }
        }
    } else {
        Subtract(particle->colVel, particle->col, particle->colVel);
        Multiply(particle->colVel, f11, particle->colVel);
    }
    particle->sizeVel *= f11;
    Transform tf;
    if (!xfm) {
        MakeLocToRel(tf);
        xfm = &tf;
    }
    Multiply(particle->Pos3(), *xfm, particle->Pos3());
    // These two inlined Multiply()s carry most of this function's residual
    // (~30 charged rows). The target seeds each output component's FMA chain
    // from a different matrix row than we do -- Y for all three at this site,
    // and Y/X/X at the Bubble3 site below, so it is /fp:fast scheduling rather
    // than one rule. Open-coding either one with per-component accumulators
    // (the lever math/Mtx.h documents) is REFUTED here: 99.2943 -> 98.5. See
    // the note on Multiply(Vector3, Matrix3, Vector3&) in math/Mtx.h.
    Multiply(particle->Vel3(), xfm->m, particle->Vel3());
    if (mBubble && mType == kFancy) {
        RndFancyParticle *fancyParticle = (RndFancyParticle *)particle;
        Multiply(fancyParticle->Bubble3(), xfm->m, fancyParticle->Bubble3());
    }
    if (mRandomAnimStart) {
        particle->mCurrentTileIndex = RandomInt(0, mNumTilesTotal);
    } else {
        particle->mCurrentTileIndex = mStartingTile;
    }
    particle->mTileTime = 0;
}

void RndParticleSys::InitParticle(RndParticle *p, const Transform *t) {
    InitParticle(CalcFrame(), p, t, gNoPartOverride);
}

void RndParticleSys::SetRelativeMotion(float motion, RndTransformable *parent) {
    mMotionParent = parent ? parent : this;
    mRelativeMotion = motion;
    mLastWorldXfm = mMotionParent->WorldXfm();
    if (motion == 1) {
        mRelativeXfm = mMotionParent->WorldXfm();
    } else {
        mRelativeXfm.Reset();
    }
    mMotionParentDelta.Zero();
}

DataNode RndParticleSys::OnSetStartColor(const DataArray *da) {
    DataArray *arr1 = da->Array(2);
    DataArray *arr2 = da->Array(3);
    SetStartColor(
        Hmx::Color(arr1->Float(0), arr1->Float(1), arr1->Float(2), arr1->Float(3)),
        Hmx::Color(arr2->Float(0), arr2->Float(1), arr2->Float(2), arr2->Float(3))
    );
    return 0;
}

DataNode RndParticleSys::OnSetStartColorInt(const DataArray *da) {
    Hmx::Color col1(da->Int(2));
    Hmx::Color col2(da->Int(3));
    col1.alpha = da->Float(4);
    col2.alpha = da->Float(5);
    SetStartColor(col1, col2);
    return 0;
}

DataNode RndParticleSys::OnSetEndColor(const DataArray *da) {
    DataArray *arr1 = da->Array(2);
    DataArray *arr2 = da->Array(3);
    SetEndColor(
        Hmx::Color(arr1->Float(0), arr1->Float(1), arr1->Float(2), arr1->Float(3)),
        Hmx::Color(arr2->Float(0), arr2->Float(1), arr2->Float(2), arr2->Float(3))
    );
    return 0;
}

DataNode RndParticleSys::OnSetEndColorInt(const DataArray *da) {
    Hmx::Color col1(da->Int(2));
    Hmx::Color col2(da->Int(3));
    col1.alpha = da->Float(4);
    col2.alpha = da->Float(5);
    SetEndColor(col1, col2);
    return 0;
}

DataNode RndParticleSys::OnSetEmitRate(const DataArray *da) {
    SetEmitRate(da->Float(2), da->Float(3));
    return 0;
}

DataNode RndParticleSys::OnAddEmitRate(const DataArray *da) {
    float add = da->Float(2);
    mEmitRate.x = Max(0.0f, mEmitRate.x + add);
    mEmitRate.y = Max(0.0f, mEmitRate.y + add);
    return !mEmitRate;
}

DataNode RndParticleSys::OnSetBurstInterval(const DataArray *da) {
    SetMaxBurst(da->Int(2));
    SetTimeBetweenBursts(da->Float(3), da->Float(4));
    return 0;
}

DataNode RndParticleSys::OnSetBurstPeak(const DataArray *da) {
    SetPeakRate(da->Float(2), da->Float(3));
    return 0;
}

DataNode RndParticleSys::OnSetBurstLength(const DataArray *da) {
    SetDuration(da->Float(2), da->Float(3));
    return 0;
}

DataNode RndParticleSys::OnSetLife(const DataArray *da) {
    SetLife(da->Float(2), da->Float(3));
    return 0;
}

DataNode RndParticleSys::OnSetSpeed(const DataArray *da) {
    SetSpeed(da->Float(2), da->Float(3));
    return 0;
}

DataNode RndParticleSys::OnSetRotate(const DataArray *da) {
    SetRotate(da->Int(2));
    SetRPM(da->Float(3), da->Float(4));
    SetRPMDrag(da->Float(4));
    return 0;
}

DataNode RndParticleSys::OnSetSwingArm(const DataArray *da) {
    SetStartOffset(da->Float(2), da->Float(3));
    SetEndOffset(da->Float(4), da->Float(5));
    return 0;
}

DataNode RndParticleSys::OnSetDrag(const DataArray *da) {
    SetDrag(da->Float(2));
    return 0;
}

DataNode RndParticleSys::OnSetAlignment(const DataArray *da) {
    SetAlignWithVelocity(da->Int(2));
    SetStretchWithVelocity(da->Int(3));
    SetConstantArea(da->Int(4));
    SetStretchScale(da->Float(5));
    return 0;
}

DataNode RndParticleSys::OnSetStartSize(const DataArray *da) {
    SetStartSize(da->Float(2), da->Float(3));
    return 0;
}

DataNode RndParticleSys::OnSetMat(const DataArray *da) {
    SetMat(da->Obj<RndMat>(2));
    return 0;
}

DataNode RndParticleSys::OnSetPos(const DataArray *da) {
    SetBoxExtent(
        Vector3(da->Float(2), da->Float(3), da->Float(4)),
        Vector3(da->Float(5), da->Float(6), da->Float(7))
    );
    return 0;
}

DataNode RndParticleSys::OnActiveParticles(const DataArray *da) {
    return mActiveParticles != nullptr;
}

DataNode RndParticleSys::OnExplicitPart(const DataArray *da) {
    ExplicitParticles(1, false, gNoPartOverride);
    return 0;
}

DataNode RndParticleSys::OnExplicitParts(const DataArray *da) {
    bool b = da->Size() >= 4 && da->Int(3);
    ExplicitParticles(da->Int(2), b, gNoPartOverride);
    return 0;
}

bool RndParticleSys::Burst::Set(float f1, float f2) {
    if (f2 > 0) {
        mPeakRate = f1;
        mHalfDuration = f2 * 0.5f;
        mRemainingDuration = f2;
        mInvHalfDuration = 1.0f / mHalfDuration;
        return true;
    } else
        return false;
}

float RndParticleSys::Burst::Emit(float f1) {
    mRemainingDuration -= f1;
    if (mRemainingDuration < 0)
        return -1;
    float ret = mRemainingDuration;
    if (ret > mHalfDuration) {
        ret = mHalfDuration * 2.0f - ret;
    }
    ret *= mInvHalfDuration;
    float ret2 = ret * ret;
    float ret3 = ret2 * ret;
    return (ret2 * 3.0f - ret3 * 2.0f) * mPeakRate * f1;
}

float RndParticleSys::CheckBursts(float f1) {
    if (f1 > 1)
        f1 = 1;
    float sum = 0;
    for (std::vector<Burst>::iterator it = mBursts.begin(); it != mBursts.end();) {
        float emit = it->Emit(f1);
        if (emit < 0)
            it = mBursts.erase(it);
        else {
            sum += emit;
            ++it;
        }
    }
    if (mBursts.size() < mMaxBurst) {
        mTimeTillBurst -= f1;
        if (mTimeTillBurst <= 0) {
            Burst burst;
            if (burst.Set(
                    RandomFloat(mBurstPeak.x, mBurstPeak.y),
                    RandomFloat(mBurstLength.x, mBurstLength.y)
                )) {
                mBursts.push_back(burst);
            }
            mTimeTillBurst = RandomFloat(mBurstInterval.x, mBurstInterval.y);
        }
    }
    return sum;
}

bool RndParticleSys::MakeWorldSphere(Sphere &s, bool b2) {
    if (b2) {
        s.Zero();
        for (RndParticle *p = mActiveParticles; p != nullptr; p = p->next) {
            Sphere s38;
            Multiply((const Vector3 &)p->pos, mRelativeXfm, s38.center);
            s38.radius = p->size * 0.5f;
            s.GrowToContain(s38);
        }
        return true;
    }
    if (mSphere.GetRadius()) {
        Multiply(mSphere, WorldXfm(), s);
        return true;
    }
    return false;
}
