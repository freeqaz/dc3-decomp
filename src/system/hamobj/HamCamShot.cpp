#include "hamobj/HamCamShot.h"
#include "char/Character.h"
#include "flow/PropertyEventProvider.h"
#include "hamobj/HamDirector.h"
#include "math/Mtx.h"
#include "math/Vec.h"
#include "obj/Data.h"
#include "obj/Dir.h"
#include "obj/Msg.h"
#include "obj/Object.h"
#include "obj/Utl.h"
#include "os/Debug.h"
#include "rndobj/Trans.h"
#include "utl/BinStream.h"
#include "utl/Loader.h"
#include "utl/Symbol.h"
#include "world/CameraShot.h"

HamCamShot *gHamCamShot;
std::list<HamCamShot::TargetCache> HamCamShot::sCache;

INIT_REVS(3, 0)

BinStream &operator>>(BinStreamRev &d, HamCamShot::Target &t);

BEGIN_LOADS(HamCamShot)
    LOAD_REVS(bs)
    ASSERT_REVS(3, 0)
    LOAD_SUPERCLASS(CamShot)
    d >> mTargets;
    d >> mZeroTime;
    d >> mMinTime;
    d >> mMaxTime;
    mNextShots.Load(bs, 1, nullptr, true);
    mOriginalSizeNextShots = mNextShots.size();
    if (d.rev > 1) {
        d >> (BinStreamEnum<HamPlayerFlags> &)mPlayerFlag;
    }
    if (d.rev > 2) {
        mMasterAnims.Load(bs, 1, nullptr, true);
    }
    ResetNextShot();
END_LOADS

void HamCamShot::EndAnim() {
    if (mNextShotIt == (ObjPtrList<HamCamShot>::iterator)0 ||
        mNextShotIt == mNextShots.end()) {
        for (ObjList<Target>::iterator it = mTargets.begin(); it != mTargets.end(); ++it) {
            Target &target = *it;
            if (!target.mTarget.Null()) {
                std::list<TargetCache>::iterator cacheIt = CreateTargetCache(target.mTarget);
                if (target.unk68p3 && target.unk68p4 && cacheIt->mTrans) {
                    TeleportTarget(cacheIt->mTrans, cacheIt->mTransform, true);
                }
                Character *theChar = dynamic_cast<Character *>(cacheIt->mTrans);
                if (theChar && target.mEnvOverride) {
                    theChar->SetEnv(nullptr);
                }
                sCache.erase(cacheIt);
            }
        }
        EndAnims(mMasterAnims);
        CamShot::EndAnim();
    } else {
        (*mNextShotIt)->EndAnim();
        ResetNextShot();
    }
}

void HamCamShot::SetPreFrame(float frame, float blend) {
    mTargetsFlipped = true;
    if (frame >= mZeroTime && mNextShotIt != 0) {
        float nextOffset = frame - mZeroTime;
        while (nextOffset < mNextShotDuration) {
            if (mNextShotIt == mNextShots.end()) break;
            HamCamShot *nextCurrent = *mNextShotIt;
            if (mCurrentShot && mCurrentShot != this) {
                mCurrentShot->EndAnim();
            }
            mCurrentShot = nextCurrent;
            if (mCurrentShot) {
                mCurrentShot->StartAnim();
                mNextShotDuration = mCurrentShot->GetTotalDuration();
                mNextShotOffset += mNextShotDuration;
            }
            ++mNextShotIt;
        }
        frame = nextOffset - mNextShotDuration;
        if (mNextShotOffset <= frame) {
            float maxDuration = kHugeFloat;
            do {
                bool iterated = IterateNextShot();
                if (!iterated) {
                    mNextShotDuration = maxDuration;
                } else {
                    frame -= mNextShotDuration;
                    mNextShotOffset += mNextShotDuration;
                    HamCamShot *nextShot = *mNextShotIt;
                    if (mCurrentShot && mCurrentShot != this) {
                        mCurrentShot->EndAnim();
                    }
                    mCurrentShot = nextShot;
                    if (mCurrentShot) {
                        mCurrentShot->StartAnim();
                        mNextShotDuration = mCurrentShot->GetTotalDuration();
                    }
                }
            } while (mNextShotDuration <= frame);
        }
    }
    if (mCurrentShot && mCurrentShot != this) {
        mCurrentShot->SetFrame(frame, 1.0f);
    }
}

DataNode HamCamShot::OnAllowableNextShots(const DataArray *a) {
    DataArrayPtr result;
    ObjDirItr<HamCamShot> dirIt(Dir(), true);
    while (dirIt) {
        HamCamShot *shot = &*dirIt;
        bool inNextShots = false;
        for (ObjPtrList<HamCamShot>::iterator it = mNextShots.begin();
             it != mNextShots.end(); ++it) {
            if (*it == shot) {
                inNextShots = true;
                break;
            }
        }
        if (!inNextShots) {
            result->Insert(result->Size(), DataNode(shot));
        }
        ++dirIt;
    }
    return DataNode(result);
}

void HamCamShot::UpdateTargetsFlipped() {
    bool flipped = AreTargetsFlipped();
    if ((flipped ? 1 : 0) != (mFlipActive ? 1 : 0)) {
        mFlipActive = flipped;
        if (flipped) {
            mFlipEndHideList = mFlipPostProcOverrides;
            mFlipDrawOverrides = mFlipGenHideList;
            mFlipShowList = mFlipEndHideList;
        } else {
            mFlipEndHideList = mFlipHideList;
            mFlipDrawOverrides = mFlipShowList;
            mFlipShowList = mFlipGenHideList;
        }
    }
}

void HamCamShot::Reteleport(const Vector3 &offset, bool teleport, Symbol sym) {
    for (ObjList<Target>::iterator it = mTargets.begin(); it != mTargets.end(); ++it) {
        Target &target = *it;
        if (target.mTarget.Null()) continue;
        if (!teleport && !target.mTeleport) continue;
        if (!sym.Null() && sym != target.mTarget) continue;
        std::list<TargetCache>::iterator cacheIt = CreateTargetCache(target.mTarget);
        if (cacheIt->mTrans) {
            Transform xfm = target.mTo;
            xfm.v += offset;
            if (mTargetsFlipped) {
                Target *flipTarget = GetFlipTarget(&target);
                if (flipTarget != &target) {
                    // Get the flipped target's transform
                    Transform flipXfm;
                    if (TargetTeleportTransform(flipTarget->mTarget, flipXfm)) {
                        Multiply(flipXfm, WorldXfm(), xfm);
                        TeleportTarget(cacheIt->mTrans, xfm, false);
                        sCache.erase(cacheIt);
                        continue;
                    }
                }
            }
            TeleportTarget(cacheIt->mTrans, xfm, false);
        }
        sCache.erase(cacheIt);
    }
    sCache.clear();
}

HamCamShot::HamCamShot()
    : mTargets(this), mMinTime(0), mMaxTime(0), mZeroTime(0), mPlayerFlag(kHamPlayerOff),
      mNextShots(this), mCurrentShot(this), mNextShotOffset(0), mNextShotDuration(0), mInSetFrame(0), mTotalDuration(0),
      mListingShots(0), mTargetsFlipped(0), mMasterAnims(this), mOriginalSizeNextShots(0), mFlipHideList(this), mFlipShowList(this),
      mFlipGenHideList(this), mFlipDrawOverrides(this), mFlipPostProcOverrides(this), mFlipEndHideList(this), mFlipActive(false) {
    mNearPlane = 10;
    mFarPlane = 10000;
    mNextShotIt = 0;
}

BEGIN_HANDLERS(HamCamShot)
    HANDLE(test_delta, OnTestDelta)
    HANDLE_EXPR(duration_seconds, GetTotalDurationSeconds())
    HANDLE_EXPR(duration, GetTotalDuration())
    HANDLE_ACTION(store, Store())
    HANDLE(add_target, AddTarget)
    HANDLE_EXPR(initial_shot, InitialShot())
    HANDLE_EXPR(num_shots, GetNumShots())
    HANDLE(allowable_next_shots, OnAllowableNextShots)
    HANDLE(list_all_next_shots, OnListAllNextShots)
    HANDLE_EXPR(find_target, FindTarget(_msg->Sym(2)))
    HANDLE(list_targets, OnListTargets)
    HANDLE_EXPR(get_original_size_next_shots, mOriginalSizeNextShots)
    HANDLE_ACTION(flip_target_anim_groups, FlipTargetAnimGroups())
    HANDLE_SUPERCLASS(CamShot)
END_HANDLERS

#define SYNC_PROP_SET_TARGET_BIT(s, member)                                              \
    {                                                                                    \
        _NEW_STATIC_SYMBOL(s)                                                            \
        if (sym == _s) {                                                                 \
            if (_op == kPropSet) {                                                       \
                member = _val.Int();                                                     \
            } else {                                                                     \
                _val = member;                                                           \
            }                                                                            \
            return true;                                                                 \
        }                                                                                \
    }

BEGIN_CUSTOM_PROPSYNC(HamCamShot::Target)
    SYNC_PROP_SET(target, o.mTarget, o.UpdateTarget(_val.Sym(), gHamCamShot))
    SYNC_PROP(to, o.mTo)
    SYNC_PROP_MODIFY(anim_group, o.mAnimGroup, gHamCamShot->StartAnim())
    SYNC_PROP(fast_forward, o.mFastForward)
    SYNC_PROP(forward_event, o.mForwardEvent)
    SYNC_PROP_SET_TARGET_BIT(force_lod, o.mForceLOD)
    SYNC_PROP_SET_TARGET_BIT(teleport, o.mTeleport)
    SYNC_PROP_SET_TARGET_BIT(return, o.mReturn)
    SYNC_PROP_SET_TARGET_BIT(self_shadow, o.mSelfShadow)
    SYNC_PROP(env_override, o.mEnvOverride)
    SYNC_PROP_SET(target_ptr, gHamCamShot->FindTarget(o.mTarget), )
END_CUSTOM_PROPSYNC

BEGIN_PROPSYNCS(HamCamShot)
    gHamCamShot = this;
    SYNC_PROP(targets, mTargets)
    SYNC_PROP_SET(
        player_flag, (int &)mPlayerFlag, mPlayerFlag = (HamPlayerFlags)_val.Int()
    )
    SYNC_PROP(zero_time, mZeroTime)
    SYNC_PROP(min_time, mMinTime)
    SYNC_PROP(max_time, mMaxTime)
    SYNC_PROP_MODIFY(next_shots, mNextShots, CheckNextShots(); ResetNextShot();)
    SYNC_PROP(master_anims, mMasterAnims)
    SYNC_SUPERCLASS(CamShot)
END_PROPSYNCS

BinStream &operator<<(BinStream &bs, const HamCamShot::Target &t) {
    bs << t.mTarget;
    bs << t.mTo;
    bs << t.mAnimGroup;
    bs << t.mFastForward;
    bs << t.mForwardEvent;
    unsigned int bits = (t.mForceLOD & 7) | (t.mTeleport ? 8 : 0) | (t.mReturn ? 16 : 0) |
                        (t.mSelfShadow ? 32 : 0) | (t.unk68p4 ? 64 : 0) | (t.unk68p3 ? 128 : 0);
    bs << bits;
    bs << t.mEnvOverride;
    return bs;
}

BinStream &operator>>(BinStreamRev &d, HamCamShot::Target &t) {
    d >> t.mTarget;
    d >> t.mTo;
    d >> t.mAnimGroup;
    d >> t.mFastForward;
    d >> t.mForwardEvent;
    unsigned int bits;
    d >> bits;
    t.mForceLOD = bits & 7;
    t.mTeleport = (bits >> 3) & 1;
    t.mReturn = (bits >> 4) & 1;
    t.mSelfShadow = (bits >> 5) & 1;
    t.unk68p4 = (bits >> 6) & 1;
    t.unk68p3 = (bits >> 7) & 1;
    d >> t.mEnvOverride;
    return d.stream;
}

BEGIN_SAVES(HamCamShot)
    SAVE_REVS(3, 0)
    SAVE_SUPERCLASS(CamShot)
    bs << mTargets;
    bs << mZeroTime;
    bs << mMinTime;
    bs << mMaxTime;
    bs << mNextShots;
    bs << mPlayerFlag;
    bs << mMasterAnims;
END_SAVES

BEGIN_COPYS(HamCamShot)
    COPY_SUPERCLASS(CamShot)
    CREATE_COPY(HamCamShot)
    BEGIN_COPYING_MEMBERS
        COPY_MEMBER(mTargets)
        COPY_MEMBER(mZeroTime)
        COPY_MEMBER(mMinTime)
        COPY_MEMBER(mMaxTime)
        COPY_MEMBER(mNextShots)
        COPY_MEMBER(mPlayerFlag)
        COPY_MEMBER(mMasterAnims)
        ResetNextShot();
    END_COPYING_MEMBERS
END_COPYS

void HamCamShot::StartAnim() {
    if (mCurrentShot && mCurrentShot != this) {
        mCurrentShot->EndAnim();
    }
    UpdateTargetsFlipped();
    ResetNextShot();
    CamShot::StartAnim();
    StartAnims(mMasterAnims);
    for (ObjList<Target>::iterator it = mTargets.begin(); it != mTargets.end(); ++it) {
        if (!it->mTarget.Null()) {
            std::list<TargetCache>::iterator cache = CreateTargetCache(it->mTarget);
            Character *theChar = dynamic_cast<Character *>(cache->mTrans);
            if (theChar) {
                theChar->SetSelfShadow(it->mSelfShadow);
                theChar->SetLodType((LODType)it->mForceLOD);
                static Message msg("play_group", 0, 0, 0, 0, 0);
                msg[0] = theChar;
                msg[1] = it->mAnimGroup;
                msg[2] = it->mFastForward / FramesPerUnit();
                msg[3] = Units();
                msg[4] = it->mForwardEvent;
                HandleType(msg);
                if (it->mEnvOverride) {
                    theChar->SetEnv(it->mEnvOverride);
                }
            }
        }
    }
    Reteleport(Vector3::ZeroVec(), true, gNullStr);
    mTotalDuration = GetTotalDuration();
    static Message camshot_changed("camshot_changed");
    TheHamProvider->Export(camshot_changed, true);
    sCache.clear();
}

void HamCamShot::ListAnimChildren(std::list<RndAnimatable *> &children) const {
    CamShot::ListAnimChildren(children);
    for (ObjPtrList<RndAnimatable>::iterator it = mMasterAnims.begin();
         it != mMasterAnims.end();
         ++it) {
        children.push_back(*it);
    }
}

bool HamCamShot::TargetTeleportTransform(Symbol s, Transform &xfm) {
    for (ObjList<Target>::iterator it = mTargets.begin(); it != mTargets.end(); ++it) {
        Target &cur = *it;
        if (cur.mTeleport && s == cur.mTarget) {
            xfm = cur.mTo;
            return true;
        }
    }
    return false;
}

bool HamCamShot::IterateNextShot() {
    bool ret = true;
    MILO_ASSERT(!mNextShots.empty(), 0x166);
    ObjPtrList<HamCamShot>::iterator it = mNextShotIt;
    if (it == 0) {
        it = mNextShots.begin();
        mNextShotIt = it;
    } else {
        ++mNextShotIt;
        if (mNextShotIt == 0) {
            ret = false;
            mNextShotIt = it;
        }
    }
    return ret;
}

void HamCamShot::Target::Store(HamCamShot *shot) {
    if (!mTarget.Null()) {
        std::list<TargetCache>::iterator it = shot->CreateTargetCache(mTarget);
        if (it->mTrans) {
            mTo = it->mTrans->LocalXfm();
        }
        HamCamShot::sCache.erase(it);
    }
}

void HamCamShot::Target::UpdateTarget(Symbol s, HamCamShot *shot) {
    if (mTarget != s) {
        mTarget = s;
        mAnimGroup = "";
    }
    Store(shot);
}

std::list<HamCamShot::TargetCache>::iterator HamCamShot::CreateTargetCache(Symbol s) {
    TargetCache cache;
    sCache.insert(sCache.begin(), cache);
    sCache.begin()->mTargetName = s;
    sCache.begin()->mTrans = FindTarget(s);
    return sCache.begin();
}

void HamCamShot::Store() {
    for (ObjList<Target>::iterator it = mTargets.begin(); it != mTargets.end(); ++it) {
        it->Store(this);
    }
}

DataNode HamCamShot::AddTarget(DataArray *target) {
    MILO_ASSERT(target->Size() != 2, 0x213);
    mTargets.push_back(Target(this));
    mTargets.back().mTarget = target->Sym(2);
    mTargets.back().Store(this);
    return 0;
}

DataNode HamCamShot::OnTestDelta(DataArray *a) {
    float f = a->Float(2);
    return (mMinTime == 0 || f >= mMinTime) && (mMaxTime == 0 || f <= mMaxTime);
}

DataNode HamCamShot::OnListTargets(const DataArray *a) {
    static Message msg("list_targets");
    DataNode handled = HandleType(msg);
    if (handled.Type() != kDataUnhandled) {
        return handled.Array();
    } else {
        return ObjectList(Dir(), "Trans", true);
    }
}

DataNode HamCamShot::OnListAllNextShots(const DataArray *a) {
    std::list<HamCamShot *> shots;
    ListNextShots(shots);
    DataArrayPtr ptr;
    for (std::list<HamCamShot *>::iterator it = shots.begin(); it != shots.end(); ++it) {
        ptr->Insert(ptr->Size(), *it);
    }
    return ptr;
}

RndTransformable *HamCamShot::FindTarget(Symbol target) {
    static Message msg("find_target", 0);
    msg[0] = target;
    DataNode handled = HandleType(msg);
    if (handled.Type() != kDataUnhandled) {
        return handled.Obj<RndTransformable>();
    } else {
        return Dir()->Find<RndTransformable>(target.Str(), false);
    }
}

void HamCamShot::TeleportTarget(RndTransformable *trans, const Transform &xfm, bool b3) {
    trans->SetLocalXfm(xfm);
    Character *theChar = dynamic_cast<Character *>(trans);
    if (theChar) {
        theChar->SetTeleport(true);
        static Message msg("teleport_char", 0, 0);
        msg[0] = trans;
        msg[1] = b3;
        HandleType(msg);
    }
}

void HamCamShot::ResetNextShot() {
    mNextShotIt = 0;
    mCurrentShot = this;
    mNextShotOffset = 0;
    mNextShotDuration = 0;
}

bool HamCamShot::ListNextShots(std::list<HamCamShot *> &shots) {
    if (mListingShots) {
        MILO_NOTIFY("%s infinite camera shot loop detected!", PathName(this));
        return false;
    } else {
        mListingShots = true;
        for (ObjPtrList<HamCamShot>::iterator it = mNextShots.begin();
             it != mNextShots.end();
             it) {
            shots.push_back(*it);
            if (!(*it)->ListNextShots(shots)) {
                mNextShots.erase(it++);
            } else {
                ++it;
            }
        }
        mListingShots = false;
        return true;
    }
}

int HamCamShot::GetNumShots() {
    std::list<HamCamShot *> shots;
    ListNextShots(shots);
    return shots.size() + 1;
}

float HamCamShot::GetTotalDuration() {
    float dur = mDuration;
    std::list<HamCamShot *> shots;
    ListNextShots(shots);
    for (std::list<HamCamShot *>::iterator it = shots.begin(); it != shots.end(); ++it) {
        dur += (*it)->mDuration;
    }
    return dur;
}

float HamCamShot::GetTotalDurationSeconds() {
    float dur = GetDurationSeconds();
    std::list<HamCamShot *> shots;
    ListNextShots(shots);
    for (std::list<HamCamShot *>::iterator it = shots.begin(); it != shots.end(); ++it) {
        dur += (*it)->GetDurationSeconds();
    }
    return dur;
}

void HamCamShot::CheckNextShots() {
    std::list<HamCamShot *> shots;
    ListNextShots(shots);
    if (TheLoadMgr.EditMode()) {
        mOriginalSizeNextShots = mNextShots.size();
    }
}

float HamCamShot::EndFrame() { return GetTotalDuration(); }

void HamCamShot::SetFrame(float frame, float blend) {
    if (!mTargetsFlipped) {
        SetPreFrame(frame, blend);
    }
    float origFrame = frame;
    bool inRange = (frame < mDuration) || mNextShots.empty();
    if (!inRange) {
        frame -= mNextShotOffset + mDuration;
    }
    if (CheckShotOver(origFrame)) {
        CamShot::SetShotOver();
    }
    if (this == mCurrentShot) {
        CamShot::SetFrame(frame, blend);
    } else {
        for (ObjPtrList<RndAnimatable>::iterator it = mAnims.begin(); it != mAnims.end(); ++it) {
            (*it)->SetFrame(frame, 1.0f);
        }
        mCurrentShot->SetFrameEx(frame, blend);
        RndAnimatable::SetFrame(origFrame, blend);
    }
    CamShot::SetFrames(mMasterAnims, origFrame);
    mTargetsFlipped = false;
}

void HamCamShot::SetFrameEx(float frame, float blend) {
    mInSetFrame = true;
    SetFrame(frame, blend);
    mInSetFrame = false;
}

bool HamCamShot::AreTargetsFlipped() const {
    static Symbol flip_camshot_targets("flip_camshot_targets");
    const DataNode *prop = TheHamProvider->Property(flip_camshot_targets, true);
    bool result;
    if (prop) {
        result = prop->Int(NULL) != 0;
    } else {
        result = false;
    }
    return result;
}

Symbol HamCamShot::GetFlipTarget(Symbol s) const {
    static Symbol player0("player0");
    static Symbol player1("player1");
    static Symbol backup0("backup0");
    static Symbol backup1("backup1");
    if (s == player0) {
        return player1;
    } else if (s == player1) {
        return player0;
    } else if (s == backup0) {
        return backup1;
    } else if (s == backup1) {
        return backup0;
    }
    return s;
}

HamCamShot::Target *HamCamShot::GetFlipTarget(Target *target) {
    Symbol origTarget = target->mTarget;
    Symbol flipped = GetFlipTarget(origTarget);
    Target *result = target;
    if (origTarget != flipped) {
        for (ObjList<Target>::iterator it = mTargets.begin(); it != mTargets.end(); ++it) {
            result = target;
            if (it->mTarget == flipped) {
                result = &*it;
                break;
            }
        }
    }
    return result;
}

RndDrawable *HamCamShot::GetFlipCharacter(RndDrawable *draw) {
    static Symbol player0("player0");
    static Symbol player1("player1");
    static Symbol backup0("backup0");
    static Symbol backup1("backup1");
    Symbol name(draw->Name());
    if (!TheHamDirector) return draw;
    HamCharacter *c;
    if (name == player0) {
        c = TheHamDirector->GetCharacter(1);
    } else if (name == player1) {
        c = TheHamDirector->GetCharacter(0);
    } else if (name == backup0) {
        c = TheHamDirector->GetBackup(1);
    } else if (name == backup1) {
        c = TheHamDirector->GetBackup(0);
    } else {
        return draw;
    }
    return c;
}

HamCharacter *CharacterNameToCharacter(Symbol s) {
    static Symbol player0("player0");
    static Symbol player1("player1");
    static Symbol backup0("backup0");
    static Symbol backup1("backup1");
    if (s == player0) {
        return TheHamDirector->GetCharacter(0);
    } else if (s == player1) {
        return TheHamDirector->GetCharacter(1);
    } else if (s == backup0) {
        return TheHamDirector->GetBackup(0);
    } else if (s == backup1) {
        return TheHamDirector->GetBackup(1);
    }
    return NULL;
}

void HamCamShot::FlipTargetAnimGroups() {
    static Symbol player0("player0");
    static Symbol player1("player1");

    ObjList<Target>::iterator p0;
    for (p0 = mTargets.begin(); p0 != mTargets.end(); ++p0) {
        if (p0->mTarget == player0) break;
    }

    ObjList<Target>::iterator p1;
    for (p1 = mTargets.begin(); p1 != mTargets.end(); ++p1) {
        if (p1->mTarget == player1) break;
    }

    if (p0 != mTargets.end()) {
        p0->mTarget = player1;
    }
    if (p1 != mTargets.end()) {
        p1->mTarget = player0;
    }
}

HamCamShot *HamCamShot::InitialShot() {
    HamCamShot *initialShot = this;
    ObjRef::iterator it = initialShot->Refs().begin();
    while (it != initialShot->Refs().end()) {
        HamCamShot *cur = dynamic_cast<HamCamShot *>((*it).RefOwner());
        if (cur) {
            for (ObjPtrList<HamCamShot>::iterator ni = cur->mNextShots.begin();
                 ni != cur->mNextShots.end();
                 ++ni) {
                if (*ni == initialShot) {
                    MILO_ASSERT(cur != this, 0x268);
                    initialShot = cur;
                    it = initialShot->Refs().begin();
                    break;
                }
            }
        } else {
            ++it;
        }
    }
    return initialShot;
}
