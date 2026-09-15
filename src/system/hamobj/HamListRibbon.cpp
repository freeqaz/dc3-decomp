#include "hamobj\HamListRibbon.h"
#include "hamobj\HamLabel.h"
#include "math\Mtx.h"
#include "obj\Data.h"
#include "obj/Object.h"
#include "rndobj\Dir.h"
#include "rndobj\Env.h"
#include "rndobj\Text.h"
#include "ui\UIListWidget.h"
#include "utl/BinStream.h"
#include "utl/Loader.h"

// HamListRibbonDrawState ctor is defined in HamNavList.cpp

#pragma region ScrollAnims

void HamListRibbon::ScrollAnims::SetScrollFrame(float frame) {
    if (mScrollAnim)
        mScrollAnim->SetFrame(frame, 1);
}

void HamListRibbon::ScrollAnims::SetAnims(int i1) {
    if (mScrollAnim) {
        float frame = mScrollAnim->GetFrame();
        if (i1 == 0) {
            if (mScrollFade)
                mScrollFade->SetFrame(1 - frame, 1);
        } else if (i1 > 0 && i1 < 4) {
            if (mScrollActive)
                mScrollActive->SetFrame(frame, 1);
        } else if (i1 == 4) {
            if (mScrollFade)
                mScrollFade->SetFrame(frame, 1);
        } else if (mScrollFaded)
            mScrollFaded->SetFrame(frame, 1);
    }
}

void HamListRibbon::ScrollAnims::Save(BinStream &bs) const {
    bs << mScrollAnim;
    bs << mScrollActive;
    bs << mScrollFade;
    bs << mScrollFaded;
}

void HamListRibbon::ScrollAnims::Load(BinStreamRev &bs) {
    bs >> mScrollAnim;
    bs >> mScrollActive;
    bs >> mScrollFade;
    bs >> mScrollFaded;
}

#pragma endregion
#pragma region HamListRibbon

HamListRibbon::HamListRibbon()
    : mScrollAnims(this), mTestMode(0), mTestNumDisplay(4), mTestSelectedIndex(0),
      mSpacing(25), mMode(kRibbonSlide), mTestEntering(0), mPaddedSize(0),
      mPaddedSpacing(29), mSelectToggle(0), mSwellAnim(this), mSlideAnim(this),
      mSelectAnim(this), mSelectToggleAnim(this), mSelectInactiveAnim(this),
      mSelectAllAnim(this), mDisengageAnim(this), mEnterAnim(this),
      mLabelPlaceholder(this), mHighlightSounds(this), mSelectSounds(this),
      mEnterFlow(this), mSlideSound(this), mSlideSoundAnim(this), mScrollSound(this),
      mScrollSoundAnim(this) {}

BEGIN_HANDLERS(HamListRibbon)
    HANDLE(enter_blacklight_mode, OnEnterBlacklightMode)
    HANDLE(exit_blacklight_mode, OnExitBlacklightMode)
    HANDLE_SUPERCLASS(RndDir)
END_HANDLERS

BEGIN_CUSTOM_PROPSYNC(HamListRibbon::ScrollAnims)
    SYNC_PROP(scroll_anim, o.mScrollAnim)
    SYNC_PROP(scroll_active, o.mScrollActive)
    SYNC_PROP(scroll_fade, o.mScrollFade)
    SYNC_PROP(scroll_faded, o.mScrollFaded)
END_CUSTOM_PROPSYNC

BEGIN_PROPSYNCS(HamListRibbon)
    SYNC_PROP(test_mode, mTestMode)
    SYNC_PROP(test_entering, mTestEntering)
    SYNC_PROP(test_num_display, mTestNumDisplay)
    SYNC_PROP(test_selected_index, mTestSelectedIndex)
    SYNC_PROP(spacing, mSpacing)
    SYNC_PROP(padded_size, mPaddedSize)
    SYNC_PROP(padded_spacing, mPaddedSpacing)
    SYNC_PROP_SET(mode, (int &)mMode, mMode = (RibbonMode)_val.Int())
    SYNC_PROP(swell_anim, mSwellAnim)
    SYNC_PROP(slide_anim, mSlideAnim)
    SYNC_PROP(select_anim, mSelectAnim)
    SYNC_PROP(select_inactive_anim, mSelectInactiveAnim)
    SYNC_PROP(select_all_anim, mSelectAllAnim)
    SYNC_PROP(select_toggle_anim, mSelectToggleAnim)
    SYNC_PROP(enter_flow, mEnterFlow)
    SYNC_PROP(enter_anim, mEnterAnim)
    SYNC_PROP(disengage_anim, mDisengageAnim)
    SYNC_PROP(scroll_anims, mScrollAnims)
    SYNC_PROP(label_placeholder, mLabelPlaceholder)
    SYNC_PROP(highlight_sounds, mHighlightSounds)
    SYNC_PROP(select_sounds, mSelectSounds)
    SYNC_PROP(slide_sound, mSlideSound)
    SYNC_PROP(slide_sound_anim, mSlideSoundAnim)
    SYNC_PROP(scroll_sound, mScrollSound)
    SYNC_PROP(scroll_sound_anim, mScrollSoundAnim)
    SYNC_SUPERCLASS(RndDir)
END_PROPSYNCS

BEGIN_SAVES(HamListRibbon)
    SAVE_REVS(11, 0)
    SAVE_SUPERCLASS(RndDir)
    bs << mSpacing;
    bs << mSwellAnim;
    bs << mSlideAnim;
    bs << mSelectAnim;
    bs << mSelectInactiveAnim;
    bs << mSelectAllAnim;
    bs << mLabelPlaceholder;
    mScrollAnims.Save(bs);
    bs << mDisengageAnim;
    bs << mSlideSound;
    bs << mSlideSoundAnim;
    bs << mScrollSound;
    bs << mScrollSoundAnim;
    bs << mEnterFlow;
    bs << mEnterAnim;
    bs << mPaddedSize;
    bs << mPaddedSpacing;
    bs << mHighlightSounds;
    bs << mSelectSounds;
    bs << mSelectToggleAnim;
END_SAVES

BEGIN_COPYS(HamListRibbon)
    COPY_SUPERCLASS(RndDir)
    CREATE_COPY(HamListRibbon)
    BEGIN_COPYING_MEMBERS
        COPY_MEMBER(mMode)
        COPY_MEMBER(mSpacing)
        COPY_MEMBER(mSwellAnim)
        COPY_MEMBER(mSlideAnim)
        COPY_MEMBER(mSelectAnim)
        COPY_MEMBER(mSelectInactiveAnim)
        COPY_MEMBER(mSelectAllAnim)
        COPY_MEMBER(mSelectToggleAnim)
        COPY_MEMBER(mEnterFlow)
        COPY_MEMBER(mEnterAnim)
        COPY_MEMBER(mLabelPlaceholder)
        COPY_MEMBER(mScrollAnims)
        COPY_MEMBER(mDisengageAnim)
        COPY_MEMBER(mHighlightSounds)
        COPY_MEMBER(mSelectSounds)
        COPY_MEMBER(mSlideSound)
        COPY_MEMBER(mSlideSoundAnim)
        COPY_MEMBER(mScrollSound)
        COPY_MEMBER(mScrollSoundAnim)
        COPY_MEMBER(mPaddedSize)
        COPY_MEMBER(mPaddedSpacing)
    END_COPYING_MEMBERS
END_COPYS

const int HamListRibbon::sNumListSelectable = 5;

INIT_REVS(11, 0)

void HamListRibbon::PreLoad(BinStream &bs) {
    LOAD_REVS(bs)
    ASSERT_REVS(0xB, 0)
    RndDir::PreLoad(d.stream);
    d.PushRev(this);
}

void HamListRibbon::PostLoad(BinStream &bs) {
    BinStreamRev d(bs, bs.PopRev(this));
    RndDir::PostLoad(d.stream);
    d >> mSpacing;
    d >> mSwellAnim;
    d >> mSlideAnim;
    d >> mSelectAnim;
    d >> mSelectInactiveAnim;
    d >> mSelectAllAnim;
    d >> mLabelPlaceholder;
    if (d.rev >= 2) {
        mScrollAnims.Load(d);
    }
    if (d.rev >= 3) {
        d >> mDisengageAnim;
    }
    // Residual, 6 rows, 97.80540 canonical, and it is ONE instruction's
    // placement: the image emits `lis r28, gNullStr@ha` at 0x82485BF4, i.e. in
    // the same basic block as the `cmpwi r27, 0x4 / blt` below it, whereas we
    // emit it after the branch at the top of this block.  Both sides have
    // exactly the same three references to gNullStr (the anchor plus the two
    // Symbol ctors at 0x54 and 0x58), so there is no third use pulling the
    // anchor up -- it is MSVC placing the same CSE one dominator higher.
    // REFUTED: swapping `Symbol s; int num;` to `int num; Symbol s;` in the
    // rev < 9 arm is byte-for-byte inert.
    if (d.rev >= 4) {
        if (d.rev < 9) {
            Symbol s;
            int num;
            d >> num;
            for (int i = 0; i < num; i++) {
                d >> s;
            }
            d >> num;
            for (int i = 0; i < num; i++) {
                d >> s;
            }
        }
        if (d.rev == 4) {
            Symbol s;
            d >> s;
            d >> s;
        }
    }
    if (d.rev >= 5) {
        d >> mSlideSound;
        d >> mSlideSoundAnim;
        d >> mScrollSound;
        d >> mScrollSoundAnim;
    }
    if (d.rev >= 10) {
        d >> mEnterFlow;
    }
    if (d.rev >= 6) {
        d >> mEnterAnim;
    }
    if (d.rev >= 7) {
        d >> mPaddedSize;
    }
    if (d.rev >= 8) {
        d >> mPaddedSpacing;
    }
    if (d.rev >= 9) {
        d >> mHighlightSounds;
        d >> mSelectSounds;
    }
    if (d.rev >= 11) {
        d >> mSelectToggleAnim;
    }
}

void HamListRibbon::DrawShowing() {
    if (!mTestMode) {
        RndDir::DrawShowing();
    } else {
        std::vector<HamListRibbonDrawState> drawStates(mTestNumDisplay);
        for (int i = 0; i < mTestNumDisplay; i++) {
            if (i == mTestSelectedIndex) {
                drawStates[i].mSelected = true;
                if (mMode == kRibbonSwell && !mTestEntering) {
                    float frame = GetFrame();
                    drawStates[i].mSwellSmoother.SetParams(frame, frame, 0);
                } else {
                    drawStates[i].mSwellSmoother.SetParams(1, 1, 0);
                }
            } else {
                drawStates[i].mSelected = false;
                drawStates[i].mSwellSmoother.SetParams(0, 0, 0);
            }
        }
        Transform xfm = WorldXfm();
        Draw(xfm, drawStates, true, false);
    }
}

float HamListRibbon::StartFrame() {
    if (mTestEntering && mEnterAnim) {
        return mEnterAnim->StartFrame();
    } else {
        switch (mMode) {
        case kRibbonSwell:
            if (mSwellAnim) {
                return mSwellAnim->StartFrame();
            } else {
                return 0;
            }
        case kRibbonSlide:
            if (mSlideAnim) {
                return mSlideAnim->StartFrame();
            } else {
                return 0;
            }
        case kRibbonSelect:
            if (mSelectToggle && mSelectToggleAnim) {
                return mSelectToggleAnim->StartFrame();
            } else {
                if (mSelectAnim && !mSelectAllAnim) {
                    return mSelectAnim->StartFrame();
                } else if (!mSelectAnim && mSelectAllAnim) {
                    return mSelectAllAnim->StartFrame();
                } else if (mSelectAnim && mSelectAllAnim) {
                    return Min(mSelectAnim->StartFrame(), mSelectAllAnim->StartFrame());
                }
            }
            return 0;
        default:
            return 0;
        }
    }
}

void HamListRibbon::HandleEnter() {
    if (mEnterFlow)
        mEnterFlow->Activate();
    ResetAnims(true);
}

void HamListRibbon::OnSelectDone() { ResetAnims(true); }

DataNode HamListRibbon::OnEnterBlacklightMode(const DataArray *a) {
    Flow *flow = DataDir()->Find<Flow>("activate_blacklight.flow", false);
    if (flow)
        flow->Activate();
    return 0;
}

DataNode HamListRibbon::OnExitBlacklightMode(const DataArray *a) {
    Flow *flow;
    if (a->Int(2) == 0) {
        flow = DataDir()->Find<Flow>("deactivate_blacklight.flow", false);
    } else {
        flow = DataDir()->Find<Flow>("deactivate_blacklight_immediate.flow", false);
    }
    if (flow) {
        flow->Activate();
    }
    return 0;
}

void HamListRibbon::PlayHighlightSound(int idx) {
    int numSounds = mHighlightSounds.size();
    if (numSounds != 0) {
#ifdef HX_NATIVE
        Flow *snd = mHighlightSounds[Min(idx, numSounds - 1)];
        if (snd)
            snd->Activate();
#else
        mHighlightSounds[Min(idx, numSounds - 1)]->Activate();
#endif
    }
}

void HamListRibbon::PlaySelectSound(int idx) {
    int numSounds = mSelectSounds.size();
    if (numSounds != 0 && idx >= 0) {
#ifdef HX_NATIVE
        Flow *snd = mSelectSounds[Min(idx, numSounds - 1)];
        if (snd)
            snd->Activate();
#else
        mSelectSounds[Min(idx, numSounds - 1)]->Activate();
#endif
    }
}

bool HamListRibbon::IsScrollable(int i1) const { return i1 > 6; }

void HamListRibbon::ResetAnims(bool b1) {
    if (mSelectInactiveAnim && (mSelectInactiveAnim->GetFrame() != 0 || b1)) {
        mSelectInactiveAnim->SetFrame(0, 1);
    }
    if (mSelectAnim && (mSelectAnim->GetFrame() != 0 || b1)) {
        mSelectAnim->SetFrame(0, 1);
    }
    if (mSelectToggleAnim && (mSelectToggleAnim->GetFrame() != 0 || b1)) {
        mSelectToggleAnim->SetFrame(0, 1);
    }
    if (mSlideAnim && (mSlideAnim->GetFrame() != 0 || b1)) {
        mSlideAnim->SetFrame(0, 1);
    }
    if (mSwellAnim && (mSwellAnim->GetFrame() != 0 || b1)) {
        mSwellAnim->SetFrame(0, 1);
    }
}

void HamListRibbon::SetAnims(bool b1, float f2) {
    if (mTestEntering)
        return;
    if (mSwellAnim) {
        mSwellAnim->SetFrame(f2, 1);
    }
    if (b1) {
        if (mMode == 1 && mSlideAnim) {
            mSlideAnim->SetFrame(GetFrame(), 1);
        }
        if (mMode == 2) {
            if (mSelectToggle && mSelectToggleAnim) {
                mSelectToggleAnim->SetFrame(GetFrame(), 1);
            } else if (mSelectAnim) {
                mSelectAnim->SetFrame(GetFrame(), 1);
            }
        }
    } else {
        if (mMode == 2 && !mSelectToggle && mSelectInactiveAnim) {
            mSelectInactiveAnim->SetFrame(GetFrame(), 1);
        }
    }
}

void HamListRibbon::SetDisengageFrame(float f1) {
    if (mDisengageAnim) {
        mDisengageAnim->SetFrame(f1, 1);
    }
}

float HamListRibbon::GetLabelTotalAlpha() const {
    float ret = 1;
    for (unsigned int i = 0; i < mLabelPlaceholder->NumStyles(); i++) {
        // retail reads the const UILabel::Style overload (?Style@UILabel@@QBA...);
        // ObjPtr::operator-> hands back a non-const UILabel* even from a const method.
        const UILabel *placeholder = mLabelPlaceholder;
        ret *= placeholder->Style(i).GetAlpha();
    }
    return ret;
}

void HamListRibbon::DrawRibbon(
    int index,
    const Transform &ribbonXfm,
    const Transform &worldXfm,
    const HamListRibbonDrawState &state,
    int paddingPerSide,
    int numItems,
    int startOffset,
    bool disengaged
) {
    bool inRange = index >= paddingPerSide && index < paddingPerSide + numItems;

    ResetAnims(false);
    SetAnims(state.mSelected, state.mSwellSmoother.Level());

    if (numItems > 6) {
        mScrollAnims.SetAnims((index - paddingPerSide) - startOffset);
    } else {
        if (mScrollAnims.mScrollActive) {
            mScrollAnims.mScrollActive->SetFrame(0.0f, 1.0f);
        }
    }

    Transform tempXfm;
    Multiply(ribbonXfm, worldXfm, tempXfm);
    SetWorldXfm(Transform::IDXfm());

    if (mLabelPlaceholder) {
        bool showLabel;
        if (!disengaged || (showLabel = true, !inRange)) {
            showLabel = false;
        }
        mLabelPlaceholder->SetShowing(showLabel);
        mLabelPlaceholder->mCanHaveFocus = true;
        // The image materialises the enumerator with a real BRANCH --
        //   0x82481570 li r4, 0x1 / 0x82481578 lbz r11, 0x14(r24) / cmplwi /
        //   ... the three vtable loads ... / 0x82481594 bne / li r4, 0x0 / bctrl
        // -- which is NOT a select: it is TWO virtual calls, one per arm,
        // tail-merged by MSVC into a single bctrl with only r4 differing.  The
        // value forms are all if-converted or folded instead (w7-as, 2026-09-14):
        //   `state.mSelected ? kFocused : kNormal`        -> subic/subfe, 92.5
        //   `State s = kFocused; if (!sel) s = kNormal;`  -> subfic/subfe/and, 93.4
        //   `(UIComponent::State)(int)state.mSelected`     -> lbz straight into r4, 94.3
        // The duplicated call pairs all four rows and also removes the r24/r27
        // callee-saved rotation those spellings caused (w7-bv: 95.0 -> 96.9).
        if (state.mSelected) {
            mLabelPlaceholder->SetState(UIComponent::kFocused);
        } else {
            mLabelPlaceholder->SetState(UIComponent::kNormal);
        }

        // Re-read through the cast at every use rather than caching a named
        // `elem` local: the image reloads `lwz rN, 0x18(r24)` THREE times
        // (0x82481654, 0x824816A0, 0x824816C0) and keeps nothing in a
        // callee-saved register for it.  A named local pins r31 for the whole
        // block, which pushed `this` out of r31 into r30 and rotated 23 rows.
        // (The cast is an identity cast on native, where mElemDrawState is
        // already the pointer, so the old #ifdef is not needed.)
        if ((UIListElementDrawState *)state.mElemDrawState) {
            // These are per-axis SCALE factors, not colours: the target's two
            // statics each take exactly three stores and never touch +0xc, which
            // Hmx::Color cannot do (both its 3- and 4-arg ctors write alpha).
            // Vector3's 3-arg ctor writes three floats, and a Vector3 assignment
            // in this build copies four words -- which is also why unk20 below
            // gets written.
            static Vector3 sBigScale(1.3f, 1.0f, 1.3f);
            static Vector3 sNormalScale(1.0f, 1.0f, 1.0f);

            const Transform &labelXfm = mLabelPlaceholder->WorldXfm();
            Vector3 pos = labelXfm.v;
            pos.z += ribbonXfm.v.z;
            *(Vector3 *)&((UIListElementDrawState *)state.mElemDrawState)->mPosX = pos;

            // BUG FIX (w7-as, 2026-09-14): this wrote the label alpha into
            // `mData` (offset 0x38, an int) via memcpy.  The image stores it as
            // a FLOAT into offset 0x24, which is `mAlpha`:
            //   0x824816A0  lwz  r11, 0x18(r24)
            //   0x824816A4  stfs f1,  0x24(r11)
            // -- a bare `stfs`, so the destination is a float, and 0x24 is the
            // only float at that offset.  We wrote 0x38 and had to round-trip
            // the value through a stack slot (`stfs f1, 0x50(r1)` /
            // `lwz r10, 0x50(r1)` / `stw r10, 0x38(r31)`), which is also where
            // our extra 0x10 of frame came from.
            ((UIListElementDrawState *)state.mElemDrawState)->mAlpha =
                GetLabelTotalAlpha();

            // A ternary of the two statics, tested `!= 0.0f` so that sBigScale is
            // the fall-through arm: 0x824816A8 `mr r11, r29` (sBigScale) sits
            // AFTER the mAlpha store, then `fcmpu / bne / 0x824816B8 mr r11, r30`.
            // The pointer-variable form (`Vector3 *scale = &sBigScale; if (==
            // 0.0f) scale = &sNormalScale;`) hoists its `mr` above the store and
            // hands the two statics r30/r29 instead of r29/r30 (9 rows, w7-bv);
            // `== 0.0f ? sNormalScale : sBigScale` fixes the registers but
            // inverts the branch (beq for bne).
            *(Vector3 *)&((UIListElementDrawState *)state.mElemDrawState)->mScaleX =
                state.mBigScale != 0.0f ? sBigScale : sNormalScale;
        }
    }

    // Initialised, and initialised HERE: the image keeps savedAlpha in f30,
    // the register that already holds the 0.0f literal from 0x824814C8
    // (SetFrame's first argument and the mBigScale compare), so `= 0.0f`
    // costs no instruction -- the else path simply leaves f30 alone.  Left
    // uninitialised, MSVC homes the variable at 0x50(r1), loads it back on
    // the else path (`lfs f31, 0x50(r1)`) and the frame grows to 0x110
    // (94.1 -> 95.0, w7-bv).  Declared at the top of the function the literal
    // is materialised there instead of at 0x824814C8 (95.9).
    float savedAlpha = 0.0f;
    if (TheLoadMgr.EditMode() && mLabelPlaceholder) {
        // Read the FIELD, not the inline accessor.  `Style(0).GetAlpha()`
        // returns through an inline temporary and MSVC sinks the load to the
        // block end: `mr r11, r3 / mr r3, r31 / lwz r30 / lfs f30, 0x24(r11)`
        // (97.8).  The field read is `lfs f30, 0x24(r3)` straight off the
        // const Style() return at 0x8248170C, and the total-alpha `fmr f31, f1`
        // then lands after `mr r3, r30` as at 0x82481724.  A named
        // `const HamLabel *` and a named `const RndText::Style &` are both
        // inert (w7-bv).
        savedAlpha =
            ((const UILabel *)(HamLabel *)mLabelPlaceholder)->Style(0).mFontColor.alpha;
        // The ObjPtr accessor's rvalue is homed in r30 across the argument
        // call (`lwz r30, 0x31c(r31)` at 0x82481714, `mr r3, r30` after it),
        // exactly as a named `HamLabel *label` is; only a NAMED
        // `float totalAlpha = GetLabelTotalAlpha();` made MSVC re-load 0x31c
        // afterwards (w7-as).
        mLabelPlaceholder->Style(0).SetAlpha(GetLabelTotalAlpha());
    }

    // RESIDUAL (w7-bv, 4 register rows, 100.0 canonical / 99.7 raw): the
    // image gives worldXfm r26 (0x82481468 `mr r26, r6`) and inRange r25
    // (0x824814A0 `clrlwi r25, r11, 24`); we allocate them the other way
    // round.  Both die before the label block (Multiply at 0x82481514, the
    // clrlwi. at 0x82481544) and every other callee-saved assignment matches.
    // Moving `Transform tempXfm;` above inRange is inert.
    SetWorldXfm(tempXfm);

    if (!state.mHidden) {
        for (RndDrawable **it = mDraws.begin(); it != mDraws.end(); ++it) {
            (*it)->Draw();
        }
    }

    if (TheLoadMgr.EditMode() && mLabelPlaceholder) {
        mLabelPlaceholder->Style(0).SetAlpha(savedAlpha);
        mLabelPlaceholder->SetShowing(true);
    }
}

void HamListRibbon::Draw(
    const Transform &xfm,
    const std::vector<HamListRibbonDrawState> &drawStates,
    bool entering,
    bool disengaged
) {
    RndEnvironTracker envTracker(mEnv, NULL);

    // Set up selectAllAnim
    if (mSelectAllAnim) {
        // The call is written out in BOTH arms: the image duplicates the
        // vtable load (`lwz r11, 0x0(r3) / lwz r11, 0xc(r11) / mtctr` at
        // 0x824835D4 and again at 0x824835EC) and shares only the `bctrl`
        // at 0x82483600, which a single call site with a merged `frame`
        // temp cannot produce.
        if (mMode == kRibbonSelect && !mTestEntering && !mSelectToggle) {
            mSelectAllAnim->SetFrame(GetFrame(), 1.0f);
        } else {
            mSelectAllAnim->SetFrame(0.0f, 1.0f);
        }
    }

    // Set up enterAnim
    if (mTestEntering && mEnterAnim) {
        mEnterAnim->SetFrame(GetFrame(), 1.0f);
    }

    // Calculate sizes
    int numItems = (int)drawStates.size();
    bool scrollable = numItems > 6;
    // The image keeps `li r17, 0x4` (0x82483648) when numItems > 6, and only
    // assigns `mr r17, r23` (= numItems) on the fall-through when it is not
    // (the `bne` at 0x8248366C skips that assignment).  We had the two arms
    // the other way round, and 5 where the image has 4.
    int visibleCount = scrollable ? sNumListSelectable - 1 : numItems;

    // Calculate padding per side
    // Argument order matters: `Max(0, x)` expands to `(0 < x) ? x : 0`, whose
    // mask is a full two-register signed compare against the zero MSVC already
    // holds in r14 (0x82483694 srwi r9,r14,31 / 0x824836A4 subfc r10,r11,r10 /
    // 0x824836AC subfe r10,r9,r8).  `Max(x, 0)` is `(x < 0) ? 0 : x`, a
    // three-instruction sign-bit mask that is a row shorter and mismatches.
    int paddingPerSide = Max(0, (mPaddedSize - numItems + 1) / 2);

    // Build padded draw states vector
    std::vector<HamListRibbonDrawState> paddedStates;
    HamListRibbonDrawState defaultState;

    for (int i = 0; i < paddingPerSide; i++)
        paddedStates.push_back(defaultState);
    for (int i = 0; i < numItems; i++)
        paddedStates.push_back(drawStates[i]);
    for (int i = 0; i < paddingPerSide; i++)
        paddedStates.push_back(defaultState);

    // Handle scrollable vs non-scrollable
    int startOffset = 0;
    // BUG FIX (w7-as, 2026-09-14): the two arms were swapped.  The image tests
    // `scrollable` and takes the half-2 / clamp-mTestSelectedIndex path when it
    // is TRUE, and only resets the scroll animation when it is FALSE:
    //   0x82483738  cmplwi cr6, r25, 0x0      ; r25 = (numItems > 6)
    //   0x8248373C  beq    cr6, .L_8248376C   ; NOT scrollable -> mScrollAnim
    //   0x82483740  srawi  r11, r23, 1        ; scrollable -> numItems/2 - 2
    //   ...
    //   0x8248376C  lwz    r3, 0x208(r28)     ; mScrollAnims.mScrollAnim
    // We clamped the selected index for short lists and reset the scroll anim
    // for long ones -- exactly backwards.
    if (scrollable) {
        int half = numItems / 2;
        startOffset = half - 2;
        if (mTestSelectedIndex < startOffset || mTestSelectedIndex > startOffset + 4) {
            mTestSelectedIndex = startOffset;
        }
    } else {
        if (mScrollAnims.mScrollAnim) {
            mScrollAnims.mScrollAnim->SetFrame(0.0f, 1.0f);
        }
    }

    // Calculate total count and padded count
    int totalCount = numItems;
    if ((int)mPaddedSize >= numItems) {
        totalCount = mPaddedSize;
    }
    int paddedCount = totalCount - visibleCount;

    // Calculate initial position offset
    float offset = (visibleCount * mSpacing + paddedCount * mPaddedSpacing) * 0.5f;
    if (paddedCount % 2 == 0) {
        offset = -(mSpacing * 0.5f - offset);
    } else if (visibleCount < (int)mPaddedSize) {
        offset += (mPaddedSpacing - mSpacing) * 0.5f;
    }

    // Save original transform and set up ribbon transform
    Transform savedXfm = xfm;
    Transform ribbonXfm;
    ribbonXfm.Reset();
    ribbonXfm.v.z = offset;

    unsigned int selectedIdx = 0xFFFFFFFF;
    Transform selectedXfm;

    // RESIDUAL (w7-as, 96.2 canonical; w7-bm 96.8, see the loop): what is left is one block-placement
    // difference plus its register knock-on.  The image parks the
    // `inRange ? mSpacing : mPaddedSpacing` select at 0x82483890, i.e. BELOW the
    // `if (!mSelected)` body at 0x824838A0, and branches back up to it from all
    // three predecessors (`b 0x360` at 0x8248389C and 0x824838C4); it also keeps
    // `ribbonXfm.v.z` cached in f30 across the loop, reloading it only after the
    // DrawRibbon call (`lfs f30, 0xb8(r31)`).  We lay the select out after the
    // body and reload v.z into f13 at the subtraction.  The `stb r26, 0x74(r31)`
    // at index 70 is a base-only home store for `scrollable` (no target
    // instruction references 0x74(r31) at all); `int scrollable` instead of
    // `bool` removes it but costs 5.6pp of register allocation -- 96.2 -> 90.6.
    // The (0xb0, 0xb4) store swap is inside the inlined Transform::Reset(), in
    // PCH-reached math/Mtx.h, which this lane may not touch.
    unsigned int totalPadded = paddedStates.size();
    for (unsigned int i = 0; i < totalPadded; i++) {
        bool inRange = ((int)i >= startOffset + paddingPerSide)
            && ((int)i < startOffset + paddingPerSide + visibleCount - 1);

        // 96.2 -> 97.5 canonical (w7-bm, 2026-09-15). Two levers: (1) writing
        // the spacing advance on the inactive path as well (this `continue`
        // block) lets MSVC cross-jump the two copies and drops the r26/r27
        // knock-on (8 rows): 96.8. It keeps the copy AFTER the arms as the
        // holder, so the image's placement (select at 0x824838B0, both arms
        // `b` back up to it) is still not reproduced: 1 diff_op + 3/4
        // insert/delete remain. Same 96.8 as an explicit if/else with the
        // tail in both arms; three single-predecessor copies (tail in each of
        // the three arms) is 94.6; a `goto`-labelled tail before the arms with
        // the arms jumping back is canonicalised straight back to the 96.2
        // layout. (2) `selectedIdx = i` BEFORE the Transform copy matches the
        // image's `mr r25, r30` ahead of `bl memcpy` (0x82483908): 97.5.
        // `int scrollable` re-measured on this state: 91.9, still refuted.
        if (entering != paddedStates[i].mActive) {
            float step = inRange ? mSpacing : mPaddedSpacing;
            ribbonXfm.v.z -= step;
            continue;
        }
        if (!paddedStates[i].mSelected) {
            DrawRibbon(i, ribbonXfm, xfm, paddedStates[i], paddingPerSide, numItems, startOffset, disengaged);
        } else {
            selectedIdx = i;
            selectedXfm = ribbonXfm;
        }

        // NEGATIVE RESULT (w7-as, 2026-09-14): inlining this as
        // `ribbonXfm.v.z -= inRange ? mSpacing : mPaddedSpacing;` keeps the same
        // 96.2 canonical but adds a commutative-operand row at the
        // `add r29, r11` index computation -- kept the named `step`.
        float step = inRange ? mSpacing : mPaddedSpacing;
        ribbonXfm.v.z -= step;
    }

    // Draw selected ribbon last (on top)
    if (selectedIdx != 0xFFFFFFFF) {
        DrawRibbon(selectedIdx, selectedXfm, xfm, paddedStates[selectedIdx], paddingPerSide, numItems, startOffset, disengaged);
    }

    // Handle edit mode animations
    if (TheLoadMgr.EditMode()) {
        SetAnims(true, 1.0f);
        if (mScrollAnims.mScrollAnim) {
            float scrollFrame = mScrollAnims.mScrollAnim->GetFrame();
            if (mScrollAnims.mScrollFade) {
                mScrollAnims.mScrollFade->SetFrame(1.0f - scrollFrame, 1.0f);
            }
        }
    }

    // Restore world transform
    SetWorldXfm(savedXfm);
}

float HamListRibbon::EndFrame() {
    if (mTestEntering && mEnterAnim) {
        return mEnterAnim->EndFrame();
    } else {
        switch (mMode) {
        case kRibbonSwell:
            if (mSwellAnim) {
                return mSwellAnim->EndFrame();
            } else {
                return 0;
            }
        case kRibbonSlide:
            if (mSlideAnim) {
                return mSlideAnim->EndFrame();
            } else {
                return 0;
            }
        case kRibbonSelect:
            if (mSelectToggle && mSelectToggleAnim) {
                return mSelectToggleAnim->EndFrame();
            } else {
                if (mSelectAnim && !mSelectAllAnim) {
                    return mSelectAnim->EndFrame();
                } else if (!mSelectAnim && mSelectAllAnim) {
                    return mSelectAllAnim->EndFrame();
                } else if (mSelectAnim && mSelectAllAnim) {
                    return Max(mSelectAnim->EndFrame(), mSelectAllAnim->EndFrame());
                }
            }
            return 0;
        default:
            return 0;
        }
    }
}
