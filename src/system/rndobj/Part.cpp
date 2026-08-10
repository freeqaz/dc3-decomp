#include "rndobj\Part.h"
#include "math\Geo.h"
#include "math\Rand.h"
#include "math\Rot.h"
#include "math\Trig.h"
#include "obj\Data.h"
#include "obj\DataFunc.h"
#include "obj\Object.h"
#include "obj\Task.h"
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
#include "utl\BinStream.h"
#include "utl\Loader.h"
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
            RndTransformable *parent =
                c->mMotionParent.Ptr() ? c->mMotionParent.Ptr() : this;
            SetRelativeMotion(c->mRelativeMotion, parent);
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
            float f1, f2, f3;
            d.stream >> v1;
            d.stream >> f1 >> f2 >> f3;
            p150.Set(f1, f2, f3, -(v1.x * f1 + v1.y * f2 + v1.z * f3));
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
        d >> mRotate >> mRPM;
        d >> mRPMDrag;
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
        Add(mRelativeXfm.v, mLastWorldXfm.v, mRelativeXfm.v);
    }
    Subtract(mMotionParent->WorldXfm().v, mLastWorldXfm.v, mMotionParentDelta);
    mLastWorldXfm = mMotionParent->WorldXfm();
}

// TODO: 69.3% match (AT_LIMIT). 2340-byte function, implemented from 0.1% stub.
//
// Remaining diff breakdown (614 instructions total):
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

    float dragFactor;

    float oneOverThirty = 1.0f / 30.0f;
    if (mDrag > 0.0f) {
        float powResult = std::pow(1.0f - mDrag, frameSpan * oneOverThirty);
        dragFactor = powResult;
    } else {
        dragFactor = 1.0f;
    }

    float rpmDragFactor;
    if (mRotate && mRPMDrag > 0.0f) {
        rpmDragFactor = std::pow(1.0f - mRPMDrag, frameSpan * oneOverThirty);
    } else {
        rpmDragFactor = 1.0f;
    }

    bool isFancy = (mType == kFancy);
    bool isRotate = mRotate;
    bool isBubble = mBubble;

    // Force direction scaled by frameSpan, then transformed through the
    // relative-space matrix. Target emits the addi+stw pointer-passing pattern
    // characteristic of the inlined Multiply(Vector3, Matrix3, Vector3 &) call.
    Vector3 deltaForce;
    deltaForce.x = mForceDir.x * frameSpan;
    deltaForce.y = mForceDir.y * frameSpan;
    deltaForce.z = mForceDir.z * frameSpan;
    Vector3 relForce;
    Multiply(deltaForce, mRelativeXfm.m, relForce);
    float relForceRow0 = relForce.x;
    float relForceRow1 = relForce.y;
    float relForceRow2 = relForce.z;

    // Bounce plane is a stack-allocated Plane in the target binary; matches
    // RB3 source layout (Plane bouncePlane local, populated from mBounce->WorldXfm()).
    // Use Plane constructor with Vector3 refs — target emits addi+stw at idx 138/144
    // to compute &bxf.v and &bxf2.m.z, then passes them via address.
    Plane bouncePlane;
    bool bounce = (mBounce != NULL);
    if (bounce) {
        const Transform &bxf = mBounce->WorldXfm();
        const Transform &bxf2 = mBounce->WorldXfm();
        bouncePlane.a = bxf2.m.z.x;
        bouncePlane.b = bxf2.m.z.y;
        bouncePlane.c = bxf2.m.z.z;
        float dot = bouncePlane.a * bxf.v.x + bouncePlane.b * bxf.v.y
                    + bouncePlane.c * bxf.v.z;
        bouncePlane.d = -dot;
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
                        p->mTileTime = std::fmod(tileTime, mTileHoldTime);
                    }
                }

                // Hoist references to p->pos / p->vel so the compiler keeps
                // their addresses in callee-saved registers across the loop body.
                Vector4 &pos = p->pos;
                Vector4 &vel = p->vel;

                // Birth momentum (fancy only). Route through scalar temps so
                // the compiler emits fmuls+fadds (separate) rather than fmadds.
                if (isFancy && mBirthMomentum) {
                    RndFancyParticle *fp = (RndFancyParticle *)p;
                    float momentumScale = mBirthMomentumAmount * frameSpan * oneOverThirty;
                    float bvX = fp->mRPMVelocity * momentumScale;
                    float bvY = fp->mPitchAngularVel * momentumScale;
                    float bvZ = fp->mBirthVelocityX * momentumScale;
                    pos.x += bvX;
                    pos.y += bvY;
                    pos.z += bvZ;
                }

                // Position integration. Route through temps to force fmuls+fadds.
                float dx_pos = frameSpan * vel.x;
                float dy_pos = vel.y * frameSpan;
                float dz_pos = frameSpan * vel.z;
                pos.x += dx_pos;
                pos.y += dy_pos;
                pos.z += dz_pos;

                // Bounce plane reflection
                if (bounce) {
                    float dist = bouncePlane.a * pos.x + bouncePlane.b * pos.y
                        + bouncePlane.c * pos.z + bouncePlane.d;
                    if (dist < 0.0f) {
                        float velDotN =
                            bouncePlane.b * vel.y + vel.x * bouncePlane.a
                            + bouncePlane.c * vel.z;
                        if (velDotN < 0.0f) {
                            // Route through scalar temps to emit fmuls+fsubs
                            // (separate) rather than the fused fnmsubs.
                            float reflect = velDotN * two;
                            float rx = bouncePlane.a * reflect;
                            float ry = bouncePlane.b * reflect;
                            float rz = bouncePlane.c * reflect;
                            vel.x -= rx;
                            vel.y -= ry;
                            vel.z -= rz;
                        }
                    }
                }

                // Attractors. Target recomputes mAttractors.size() each iteration
                // (loop condition calls .size() rather than caching it).
                for (unsigned int i = 0; i < mAttractors.size(); i++) {
                    Attractor &a = mAttractors[i];
                    if (a.mAttractor != NULL) {
                        const Transform &axf = a.mAttractor->WorldXfm();
                        float dz = axf.v.z - pos.z;
                        float dy = axf.v.y - pos.y;
                        float strength = a.mStrength;
                        float dx = axf.v.x - pos.x;

                        // TODO: target uses beq (to special case) + dead code after,
                        // ours uses bne (skip special case). diff_op at idx 340.
                        // Target dead code: li r11,0; clrlwi. r11,r11,24; beq (always taken).
                        // Suggests original may have used a bool for this condition.
                        if (strength == magicStrength) {
                            dz = 0.0f;
                            auto _tmp0 = a.mAttractor.Owner();
                            RndParticleSys *ps =
                                dynamic_cast<RndParticleSys *>(_tmp0);
                            if (ps != NULL) {
                                const Transform &t1xf = a.mAttractor->WorldXfm();
                                const Transform &t2xf = ps->WorldXfm();
                                float relY = t2xf.v.y - t1xf.v.y;
                                float relX = t2xf.v.x - t1xf.v.x;
                                strength *= (relX * relX + relY * relY) + epsilon;
                            }
                        }

                        float distSq =
                            dy * dy + (dx * dx + dz * dz) + epsilon;
                        float scale = (strength * frameSpan) / distSq;
                        // Force compiler to emit fmuls + fadds (separate) like target,
                        // not fmadds (fused) — by routing through scalar temps.
                        float vx_inc = scale * dx;
                        float vy_inc = scale * dy;
                        float vz_inc = scale * dz;
                        vel.x += vx_inc;
                        vel.z += vz_inc;
                        vel.y += vy_inc;
                    }
                }

                vel.x += relForceRow0;
                vel.z += relForceRow2;
                vel.y += relForceRow1;

                if (isFancy) {
                    vel.y *= dragFactor;
                    vel.z *= dragFactor;
                    vel.x *= dragFactor;

                    RndFancyParticle *fp = (RndFancyParticle *)p;

                    // Bubble oscillation effect — uses bubbleFreq/bubblePhase
                    // and bubbleDir.xyz (matches RB3 idiom and target field offsets).
                    if (isBubble) {
                        float sinVal =
                            FastSin(fp->bubbleFreq * dt + fp->bubblePhase + halfPi);
                        float bubbleScale = fp->bubbleFreq * sinVal * frameSpan;
                        pos.x += fp->bubbleDir.x * bubbleScale;
                        pos.y += fp->bubbleDir.y * bubbleScale;
                        pos.z += fp->bubbleDir.z * bubbleScale;
                    }

                    // RPM rotation and swing arm — uses RPF/swingArmVel
                    // (matches RB3 idiom and target field offsets 0xb0/0xb4).
                    if (isRotate) {
                        p->angle += fp->RPF * frameSpan;
                        fp->RPF *= rpmDragFactor;
                        p->swingArm += fp->swingArmVel * frameSpan;
                    }

                    // Fancy color: 2-phase Hermite-like blend (before/after midcolFrame).
                    // Blend formula: colorScale = (1-t)*t*frameSpan*6 where t is normalized
                    // time within the current phase. Phase 1 uses midcolVel, phase 2 uses colVel.
                    float colorScale;
                    float cr, cg, cb, ca;
                    if (dt < fp->midcolFrame) {
                        float t = (dt - p->birthFrame) * vel.w;
                        colorScale = (1.0f - t) * t * frameSpan * sixf;
                        ca = fp->midcolVel.alpha * colorScale;
                        cb = fp->midcolVel.blue * colorScale;
                        cg = fp->midcolVel.green * colorScale;
                        cr = colorScale * fp->midcolVel.red;
                    } else {
                        float t = (dt - fp->midcolFrame) * fp->bubblePhase;
                        colorScale = (1.0f - t) * t * frameSpan * sixf;
                        ca = p->colVel.alpha * colorScale;
                        cb = p->colVel.blue * colorScale;
                        cg = p->colVel.green * colorScale;
                        cr = p->colVel.red * colorScale;
                    }

                    // Clamp color channels to [0, 1] using fneg+fsel pattern
                    float newR = cr + p->col.red;
                    float newA = ca + p->col.alpha;
                    float newB = cb + p->col.blue;
                    float newG = cg + p->col.green;

                    newR = (-newR >= 0.0f) ? 0.0f : newR;
                    newA = (-newA >= 0.0f) ? 0.0f : newA;
                    newB = (-newB >= 0.0f) ? 0.0f : newB;
                    newG = (-newG >= 0.0f) ? 0.0f : newG;

                    p->col.red = (newR - 1.0f >= 0.0f) ? 1.0f : newR;
                    p->col.alpha = (newA - 1.0f >= 0.0f) ? 1.0f : newA;
                    p->col.blue = (newB - 1.0f >= 0.0f) ? 1.0f : newB;
                    p->col.green = (newG - 1.0f >= 0.0f) ? 1.0f : newG;

                    // Fancy size: 3-phase (grow / sustain / shrink)
                    float sizeVelRate, timeSince, invDuration;
                    if (dt < fp->growFrame) {
                        invDuration = fp->beginGrow;
                        timeSince = dt - p->birthFrame;
                        sizeVelRate = fp->growVel;
                    } else if (dt < fp->shrinkFrame) {
                        invDuration = fp->midGrow;
                        timeSince = dt - fp->growFrame;
                        sizeVelRate = p->sizeVel;
                    } else {
                        timeSince = dt - fp->shrinkFrame;
                        invDuration = fp->endGrow;
                        sizeVelRate = fp->shrinkVel;
                    }
                    float st = timeSince * invDuration;
                    p->size +=
                        sizeVelRate * ((1.0f - st) * st * frameSpan * sixf);
                } else {
                    // Basic particle: single-phase color/size update.
                    // Route through scalar temps so the compiler emits
                    // fmuls+fadds (separate) rather than fmadds.
                    float t = (dt - p->birthFrame) * pos.w;
                    float scale = (1.0f - t) * t * frameSpan * sixf;
                    float dr = p->colVel.red * scale;
                    float dg = p->colVel.green * scale;
                    float db = p->colVel.blue * scale;
                    float da = p->colVel.alpha * scale;
                    float ds = p->sizeVel * scale;
                    p->size += ds;
                    p->col.red += dr;
                    p->col.green += dg;
                    p->col.blue += db;
                    p->col.alpha += da;
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
    if (mPreserveParticles == 0) {
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
                        f32 pitchMid = LimitAng(mPitch.y - mPitch.x) * halfSample + mPitch.x;
                        f32 yawMid = LimitAng(mYaw.y - mYaw.x) * halfSample + mYaw.x;
                        f32 speedMid = (mSpeed.y - mSpeed.x) * halfSample + mSpeed.x;

                        f32 halfPi = 1.57079637f;
                        f32 cosPitch = FastSin(pitchMid + halfPi);
                        f32 negXVel = -(FastSin(yawMid) * cosPitch * speedMid);
                        f32 yVel = FastSin(yawMid + halfPi) * cosPitch * speedMid;
                        f32 sinPitch = FastSin(pitchMid);

                        baseVel.Set(
                            negXVel * frameUpdate,
                            yVel * frameUpdate,
                            sinPitch * speedMid * frameUpdate
                        );

                        Multiply(baseVel, mSubSampleXfm, baseVel);
                    } else {
                        baseVel = mSubSampleXfm.v;
                    }

                    memcpy(&mSubSampleXfm, &locToRel, sizeof(Transform));

                    int count = mSubSamples;
                    f32 stepSize = frameUpdate / (f32)mSubSamples;
                    Vector3 interpOffset;
                    if (count != 0) {
                        do {
                            CreateParticles(currentFrame, stepSize, locToRel);
                            Interp(interpOffset, baseVel, 1.0f / (f32)count, interpOffset);
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
        memcpy(&fancyParticle->mRPMVelocity, &mMotionParentDelta, 16);
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
