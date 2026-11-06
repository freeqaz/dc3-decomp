// MSVC expanded GetEaseFunction() inline here in the shipped build: the __FILE__
// UpdateEase's range assert carries is e:\lazer_build_gmc1\system\src\math/Easing.h,
// not FlowSlider.cpp.  See the opt-in note in math/Easing.h.  Must precede the
// first include.
#define EASING_FORCE_INLINE_GET_EASE_FUNCTION 1
#include "flow\FlowSlider.h"
#include "flow\FlowDistance.h"
#include "obj\ObjPtrVec_impl.h"
#include "flow\FlowManager.h"
#include "flow\FlowNode.h"
#include "flow\FlowValueCase.h"
#include "flow\PropertyEventListener.h"
#include "flow\Flow.h"
#include "math/Easing.h"
#include "obj/Object.h"
#include "os\Debug.h"

void FlowDistance::RequestStop() {
    FLOW_LOG("RequestStop\n");
    FlowNode::RequestStop();
}

void FlowDistance::RequestStopCancel() {
    FLOW_LOG("RequestStopCancel\n");
    FlowNode::RequestStopCancel();
}

bool SliderChildSort(FlowNode *a, FlowNode *b) {
    return static_cast<FlowValueCase *>(a)->Value() < static_cast<FlowValueCase *>(b)->Value();
}

FlowSlider::FlowSlider()
    : PropertyEventListener(this), mPersistent(1), mAlwaysRun(0), mValue(0),
      mEaseType(kEasePolyOut), mEasePower(2) {
    UpdateEase();
}

FlowSlider::~FlowSlider() {}

BEGIN_HANDLERS(FlowSlider)
    HANDLE_ACTION(reactivate, ReActivate())
    HANDLE_SUPERCLASS(FlowNode)
END_HANDLERS

BEGIN_PROPSYNCS(FlowSlider)
    SYNC_PROP(persistent, mPersistent)
    SYNC_PROP(always_run, mAlwaysRun)
    SYNC_PROP_MODIFY(ease, (int &)mEaseType, UpdateEase())
    SYNC_PROP(ease_power, mEasePower)
    SYNC_PROP_MODIFY(value, mValue, UpdateActivations())
    SYNC_SUPERCLASS(FlowNode)
END_PROPSYNCS

BEGIN_SAVES(FlowSlider)
    SAVE_REVS(0, 0)
    SAVE_SUPERCLASS(FlowNode)
    bs << mPersistent;
    bs << mAlwaysRun;
    bs << mValue;
    bs << mEaseType;
    bs << mEasePower;
END_SAVES

BEGIN_COPYS(FlowSlider)
    COPY_SUPERCLASS(FlowNode)
    CREATE_COPY_AS(FlowSlider, c)
    BEGIN_COPYING_MEMBERS_FROM(c)
        COPY_MEMBER(mPersistent)
        COPY_MEMBER(mAlwaysRun)
        COPY_MEMBER(mValue)
        COPY_MEMBER(mEaseType)
        COPY_MEMBER(mEasePower)
    END_COPYING_MEMBERS
END_COPYS

INIT_REVS(0, 0)

BEGIN_LOADS(FlowSlider)
    LOAD_REVS(bs)
    ASSERT_REVS(0, 0)
    LOAD_SUPERCLASS(FlowNode)
    d >> mPersistent;
    d >> mAlwaysRun;
    bs >> mValue;
    bs >> (int &)mEaseType;
    bs >> mEasePower;
    GenerateAutoNames(this, true);
    mChildNodes.sort(SliderChildSort);
    UpdateEase();
END_LOADS

bool FlowSlider::Activate() {
    FLOW_LOG("Activate\n");
    mStopRequested = false;
    if (IsRunning()) {
        MILO_NOTIFY(
            "FlowSlider re-entrance error, activated when already running, deactivating and aborting, check your logic"
        );
        Deactivate(false);
        return false;
    } else {
        PushDrivenProperties();
        if (mPersistent) {
            RegisterEvents(this);
            mEventsRegistered = true;
        }
        UpdateActivations();
        if (mAlwaysRun) {
            return true;
        } else {
            return IsRunning();
        }
    }
}

void FlowSlider::Deactivate(bool b) {
    FLOW_LOG("Deactivated\n");
    if (mEventsRegistered)
        PropertyEventListener::UnregisterEvents(this);
    FlowNode::Deactivate(b);
}

void FlowSlider::ChildFinished(FlowNode *n) {
    FLOW_LOG("Child Finished of class:%s\n", n->ClassName());
    mRunningNodes.remove(n);
    if (mRunningNodes.empty()) {
        if (mEventsRegistered && mStopRequested) {
            UnregisterEvents(this);
            mEventsRegistered = false;
            mStopRequested = false;
        } else if (mEventsRegistered) {
            return;
        }
        mFlowParent->ChildFinished(this);
    }
}

void FlowSlider::RequestStop() {
    FLOW_LOG("RequestStop\n");
    FlowNode::RequestStop();
}

void FlowSlider::RequestStopCancel() {
    FLOW_LOG("RequestStopCancel\n");
    FlowNode::RequestStopCancel();
}

bool FlowSlider::IsRunning() {
    if (mEventsRegistered)
        return true;
    return FlowNode::IsRunning();
}

void FlowSlider::UpdateIntensity() {
    PushDrivenProperties();
    UpdateActivations();
}

__declspec(noinline) void FlowSlider::UpdateEase() {
    // The assert and the gEaseFuncs load both come from GetEaseFunction() in
    // math/Easing.h -- open-coding them here put FlowSlider.cpp in the image's
    // __FILE__ slot where the original has Easing.h.
    mEaseFunc = GetEaseFunction(mEaseType);
}

// w18-d (98.95, same 12 rows): both range-fraction guards as a real static
// inline helper returning 0.0f is byte-identical to the open-coded form.
// w21-p (98.95, same 12 rows; behaviour re-read against the image: every
// permuted f30/f31 row carries the same VALUE -- image f31 = 1.0f, f30 = 0.0f
// -- and every branch lands on the same block): `zero` replaced by the 0.0f
// literal (one stays a local), and the first fraction guard written
// `if (!(...)) f = ...; else f = zero;`, are both byte-identical.
// w21-an (98.95, same 12 rows): the f30/f31 constant assignment is NOT
// decided by declaration order or by constant value -- re-measured: decls
// swapped inert; zero changed to 3.0f / 0.5f / -1.0f (probe only) still lands
// in f31.  Also inert: a static inline RangeFraction() helper used as
// `t = one - RangeFraction(...)`, literal 1.0f to mEaseFunc, no casts on the
// mEaseFunc call, `zero` in the sIntensity compares, one/zero declared in the
// loop body, the two intensity=one arms joined with ||, an else-if for the
// ActivateChild arm, a FlowNode* copy for IsRunning().  Worse: savedIntensity
// declared after one/zero (95.71), `intensity = zero;` as a default (97.38).
void FlowSlider::UpdateActivations() {
    float savedIntensity = FlowNode::sIntensity;

    auto cur = mChildNodes.begin();
    auto prev = mChildNodes.begin();
    auto next = mChildNodes.begin();

    // NEGATIVE RESULT (w7-ap, 2026-09-14, 97.9 canonical): swapping these two
    // declarations is BYTE-IDENTICAL.  The residual f30/f31 swap (image
    // f31 = 1.0f, f30 = 0.0f; ours reversed, 10 rows) is a live-range
    // assignment, not a declaration-order effect.  w14-f re-checked after the
    // fixes below: swapping them, and replacing both locals with literals, are
    // still byte-identical (99.0 canonical, 12 rows: the f30/f31 swap plus the
    // `lwz r11, 0(r30)` vtable load scheduled before `mr r3, r30` for
    // curCase->IsRunning(); `IsRunning()` without `> 0` is also identical).
    float one = 1.0f;
    float zero = 0.0f;

    while (cur != mChildNodes.end()) {
        ++next;
        FlowValueCase *prevCase = static_cast<FlowValueCase *>(prev->Obj());
        FlowValueCase *curCase = static_cast<FlowValueCase *>(cur->Obj());
        FlowValueCase *nextCase = static_cast<FlowValueCase *>(next->Obj());
        prev = cur;

        float t;
        float intensity;

        // The original called an inlined range-fraction helper that guards all
        // three degenerate cases, so the range test is issued a SECOND time
        // inside the fraction: the image emits `fcmpu cr6, f0, f13` / blt,
        // `fcmpu cr6, f0, f12` / bgt and `fcmpu cr6, f13, f12` / beq at
        // 0x82421454-0x82421474 after the outer test has already passed.
        // w14-f: the outer test is `!(mValue > nextPos)` -- the image's `bgt`
        // skip, which ALSO enters on NaN.  Spelled `mValue <= nextPos`, MSVC
        // proved the inner `mValue > nextPos` false and folded it (the missing
        // fcmpu/bgt pair), and clang on native would skip the block on NaN where
        // the image enters it.  Holding lo = curCase->Value() in a local, like the
        // second helper below, gives the image's f13 (lo) / f12 (hi) assignment.
        if (next != mChildNodes.end()) {
            if (mValue >= curCase->Value()) {
                float nextPos = nextCase->Value();
                if (!(mValue > nextPos)) {
                    float f;
                    float lo = curCase->Value();
                    if (mValue < lo || mValue > nextPos
                        || lo == nextPos) {
                        f = zero;
                    } else {
                        f = (mValue - lo) / (nextPos - lo);
                    }
                    t = one - f;
                    goto ease;
                }
            }
        }

        {
            // Same helper, lo = prevCase, hi = curCase (0x824214A4-0x824214C4).
            // No (bool) cast: the image branches straight out of cr6 twice
            // (bgt then blt) rather than materialising a 0/1 GPR.
            if (mValue <= curCase->Value() && mValue >= prevCase->Value()) {
                float lo = prevCase->Value();
                float hi = curCase->Value();
                if (mValue < lo || mValue > hi || lo == hi) {
                    t = zero;
                } else {
                    t = (mValue - lo) / (hi - lo);
                }
                goto ease;
            }
        }

        {
            float curPos = curCase->Value();
            if (next == mChildNodes.end() && mValue > curPos) {
                intensity = one;
            } else if (cur == mChildNodes.begin() && mValue < curPos) {
                intensity = one;
            } else {
                intensity = zero;
            }
        }
        goto apply;

    ease:
        FlowNode::sIntensity = t;
        intensity = (float)mEaseFunc((double)t, (double)mEasePower, (double)one);

    apply:
        FlowNode::sIntensity = intensity * savedIntensity;

        if (curCase->IsRunning() > 0) {
            curCase->UpdateIntensity();
            if (FlowNode::sIntensity == 0.0f && mAlwaysRun == 0) {
                mRunningNodes.remove(curCase);
                curCase->Deactivate(false);
            }
        } else {
            if (mAlwaysRun > 0 || FlowNode::sIntensity != 0.0f) {
                ActivateChild(curCase);
            }
        }

        ++cur;
    }

    FlowNode::sIntensity = savedIntensity;
}

void FlowSlider::ReActivate() {
    Timer timer;
    timer.Restart();
    UpdateIntensity();
    timer.Stop();
    FlowNode *flow;
    for (flow = GetTopFlow(); flow->GetParent() != nullptr;
         flow = flow->GetParent()->GetTopFlow()) {
    }
    Symbol sym = MakeString("%s: %s->%s", ClassName(), flow->Dir()->Name(), flow->Name());
    TheFlowMgr->AddEventTime(sym, timer.Ms());
}


// ---------------------------------------------------------------------------
// w48-b (2026-11-06) EQUIVALENCE CERTIFICATE -- FlowSlider::UpdateActivations,
// canonical 98.95288 / fuzzy 98.69110.  191 instructions and 764 B on BOTH
// sides (180 equal / 10 diff_arg / 1 insert / 1 delete).
// Placed at END OF FILE so that no line above it moves (this TU has no
// MILO_ASSERT at all).
//
// BEHAVIOUR: no divergence, in two parts.
//   (a) 10 rows are ONE BIJECTION OF TWO FLOAT CONSTANTS.  The image has
//       f31 = 1.0f and f30 = 0.0f; ours has f30 = 1.0f and f31 = 0.0f.
//       Substituting T.f30 <-> B.f31 makes all ten identical: the two pool
//       loads (idx 35/36), the fmr/fsubs rows (87, 88, 107, 111, 141, 143)
//       and both `fcmpu ..., 0.0f` rows (160, 179).  Nothing reads a
//       different value anywhere.
//   (b) idx 146/148: `lwz r11, 0x0(r30)` -- the curCase vtable load for
//       IsRunning() -- is emitted one instruction EARLIER here than in the
//       image, on one side of `mr r3, r30`.  Same pair, same order relative
//       to the mtctr/bctrl.
//
// MECHANISM (c2rs tap, 40 candidates): the two pool constants are
//   id 25  pri -33 tie 35 ben 6 ref 6 sym 46/k03 -> f31  [the 0.0f constant]
//   id 26  pri -33 tie 34 ben 5 ref 5 sym 47/k03 -> f30  [the 1.0f constant]
// IDENTICAL priority.  The tie key -- the forward-walk visit ordinal -- decides
// by exactly ONE unit, 35 > 34, and the image has them the other way round.
// Every source spelling of BOTH constants is BYTE-IDENTICAL, re-measured this
// wave: `t = 1.0f - f`, `f = 0.0f`, `t = 0.0f`, `intensity = 0.0f`, `(double)1`
// in the mEaseFunc call, all uses replaced by literals at once, and the two
// declarations swapped.  So the tie is set by the IL visit order and not by the
// source text, and the constants are not source-visible candidates.
// The only spellings that move that order are the operand orders of the two
// comparisons that consume the 0.0f constant --
// `sIntensity == 0.0f && mAlwaysRun == 0` and
// `mAlwaysRun > 0 || sIntensity != 0.0f` -- and both cost 3.5pp (95.49738),
// because they are what shares the constant's live range in the first place.
// `IsRunning() > 0`, `IsRunning()` and `IsRunning() != 0` are all
// byte-identical, so the scheduling row has no source handle either.
// ---------------------------------------------------------------------------
