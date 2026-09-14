#include "ui\UITrigger.h"
#include "math/Easing.h"
#include "obj\Data.h"
#include "obj/Object.h"
#include "rndobj\Anim.h"
#include "rndobj\EventTrigger.h"
#include "ui\UIComponent.h"
#include "utl/Loader.h"

UITrigger::UITrigger()
    : mBlockTransition(0), mCallbackObject(this), mEndTime(0), mDone(1) {}

BEGIN_PROPSYNCS(UITrigger)
    SYNC_PROP(block_transition, mBlockTransition)
    SYNC_PROP(callback_object, mCallbackObject)
    SYNC_SUPERCLASS(EventTrigger)
END_PROPSYNCS

BEGIN_SAVES(UITrigger)
    bs << 1;
    SAVE_SUPERCLASS(EventTrigger)
    bs << mBlockTransition;
END_SAVES

BEGIN_COPYS(UITrigger)
    COPY_SUPERCLASS(EventTrigger)
    CREATE_COPY(UITrigger)
    BEGIN_COPYING_MEMBERS
        COPY_MEMBER(mBlockTransition)
    END_COPYING_MEMBERS
END_COPYS

INIT_REVS(1, 0)

BEGIN_LOADS(UITrigger)
    LOAD_REVS(bs)
    ASSERT_REVS(1, 0)
    if (d.rev < 1) {
        UIComponent *uiCom = Hmx::Object::New<UIComponent>();
        uiCom->Load(bs);
        delete uiCom;
        Symbol sym;
        bs >> sym;
        UnregisterEvents();
        mTriggerEvents.clear();
        mTriggerEvents.push_back(sym);
        RegisterEvents();
        ObjPtr<RndAnimatable> animPtr(this);
        bs >> animPtr;
        mAnims.clear();
        mAnims.push_back();
        EventTrigger::Anim &anim = mAnims.back();
        anim.mAnim = animPtr;
    } else
        LOAD_SUPERCLASS(EventTrigger);
    d >> mBlockTransition;
END_LOADS

void UITrigger::Trigger() {
    EventTrigger::Trigger();
    mStartTime = TheTaskMgr.UISeconds();
    mEndTime = 0;
    FOREACH (it, mAnims) {
        if (it->mAnim) {
            // BEHAVIOURAL FIX: f4 is NOT pre-set to 0 and the period product is
            // NOT discarded.  The image keeps mPeriod * 30.0f in f4 and only
            // replaces it when it is zero:
            //   827B26B8  lfs   f0, 0x38(r30)     mPeriod
            //   827B26BC  fmuls f0, f0, f31       * 30.0f   -> f4 lives in f0
            //   827B26C0  fcmpu cr6, f0, f29      vs 0.0f
            //   827B26C4  bne   cr6, .L_827B2728  keep it, go straight to MaxEq
            // Written with `float f4 = 0;` and the product thrown away inside
            // the test, a non-zero period contributed 0 to mEndTime instead of
            // mPeriod*30.  Natively that shortens every UITrigger end time for
            // enabled anims with a period set.
            float f4;
            if (it->mEnable) {
                f4 = it->mPeriod * 30.0f;
                if (!f4) {
                    f4 = it->mScale;
                    if (!f4) {
                        f4 = 1.0f;
                    }
                    // fabsf, not std::fabs: std::fabs takes and returns
                    // DOUBLE, so the divide below came out as `fdiv` + `frsp`
                    // where the image has a single `fdivs f0, f13, f0`
                    // (827B26E8).  Both sides emit the same `fabs` instruction
                    // -- it is the divide's precision that the double round
                    // trip changed.
                    f4 = fabsf(it->mStart - it->mEnd) / f4;
                }
            } else {
                f4 = fabsf(it->mAnim->StartFrame() - it->mAnim->EndFrame());
            }
            MaxEq(mEndTime, (it->mDelay * 30.0f + f4) / 30.0f);
        }
    }
    if (mBlockTransition && mEndTime > 5.0f) {
        MILO_NOTIFY(
            "%s (%s) is blocking and really long! (%f seconds)",
            Name(),
            PathName(Dir()),
            mEndTime
        );
    }
    mEndTime += TheTaskMgr.UISeconds();
    mDone = false;
}

DataArray *UITrigger::SupportedEvents() {
    static DataArray *events =
        SystemConfig("objects", "UITrigger", "supported_events")->Array(1);
    return events;
}

void UITrigger::CheckAnims() {
    FOREACH (it, mAnims) {
        Anim &curAnim = *it;
        RndAnimatable *anim = curAnim.mAnim;
        if (anim && anim->GetRate() != RndAnimatable::k30_fps_ui) {
            if (TheLoadMgr.EditMode()) {
                MILO_NOTIFY("Setting animatable rate to k30_fps_ui for %s", anim->Name());
            }
            anim->SetRate(RndAnimatable::k30_fps_ui);
        }
        curAnim.mRate = RndAnimatable::k30_fps_ui;
    }
}

void UITrigger::Poll() {
    if (!mDone) {
        if (IsDone()) {
            mDone = true;
            if (mCallbackObject) {
                mCallbackObject->Handle(UITriggerCompleteMsg(this), true);
            }
        }
    }
}

void UITrigger::Enter() {
    mStartTime = TheTaskMgr.UISeconds();
    mEndTime = 0;
}

bool UITrigger::IsDone() const { return mEndTime <= TheTaskMgr.UISeconds(); }

bool UITrigger::IsBlocking() const {
    if (mStartTime > TheTaskMgr.UISeconds()) {
        const_cast<UITrigger *>(this)->mEndTime = 0;
    }
    return mBlockTransition && mEndTime && !IsDone();
}

void UITrigger::StopAnimations() {
    FOREACH (it, mAnims) {
        RndAnimatable *anim = (*it).mAnim;
        if (anim && anim->IsAnimating())
            anim->StopAnimation();
    }
}

void UITrigger::PlayStartOfAnims() {
    FOREACH (it, mAnims) {
        Anim &curAnim = *it;
        RndAnimatable *anim = curAnim.mAnim;
        if (anim) {
            float f3 = anim->StartFrame();
            float f4 = 0.0099999998f;
            if (curAnim.mEnable) {
                f3 = curAnim.mStart;
                if (f3 > curAnim.mEnd) {
                    f4 *= -1;
                }
            }
            anim->Animate(f3 + f4, f3, kTaskUISeconds, 0, 0, 0, kEaseLinear, 0, 0);
        }
    }
}

void UITrigger::PlayEndOfAnims() {
    FOREACH (it, mAnims) {
        Anim &curAnim = *it;
        RndAnimatable *anim = curAnim.mAnim;
        if (anim) {
            float f3 = anim->EndFrame();
            float f4 = 0.0099999998f;
            if (curAnim.mEnable) {
                f3 = curAnim.mEnd;
                if (curAnim.mStart > f3) {
                    f4 *= -1;
                }
            }
            anim->Animate(f3 - f4, f3, kTaskUISeconds, 0, 0, 0, kEaseLinear, 0, 0);
        }
    }
}

BEGIN_HANDLERS(UITrigger)
    HANDLE_EXPR(end_time, mEndTime)
    HANDLE_ACTION(play_start_of_anims, PlayStartOfAnims())
    HANDLE_ACTION(play_end_of_anims, PlayEndOfAnims())
    HANDLE_ACTION(stop_anims, StopAnimations())
    HANDLE_EXPR(is_done, IsDone())
    HANDLE_EXPR(is_blocking, IsBlocking())
    HANDLE_SUPERCLASS(EventTrigger)
END_HANDLERS
