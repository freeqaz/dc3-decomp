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
        // RESIDUAL (w7-as, 4 rows): the image does NOT pass the bool straight
        // through here.  It materialises the enumerator with a real BRANCH --
        //   li r4, 0x1 / lbz r11, 0x14(r24) / cmplwi r11, 0x0 /
        //   ... / bne <bctrl> / li r4, 0x0
        // -- with the compare and its branch scheduled around the three vtable
        // loads.  NEGATIVE RESULT (2026-09-14): neither source form that
        // *should* produce that branch does.  MSVC if-converts both:
        //   `state.mSelected ? kFocused : kNormal`        -> subic/subfe, 92.5
        //   `State s = kFocused; if (!sel) s = kNormal;`  -> subfic/subfe/and, 93.4
        // The plain cast below leaves the four target instructions unpaired but
        // scores 94.3, and is the only spelling of the three that does not also
        // rotate the callee-saved set.
        mLabelPlaceholder->SetState((UIComponent::State)(int)state.mSelected);

        // Re-read through the cast at every use rather than caching a named
        // `elem` local: the image reloads `lwz r11, 0x18(r24)` THREE times
        // (0x8248329C, 0x824832C8, 0x824832E8) and keeps nothing in a
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
            //   0x824832C8  lwz  r11, 0x18(r24)
            //   0x824832CC  stfs f1,  0x24(r11)
            // -- a bare `stfs`, so the destination is a float, and 0x24 is the
            // only float at that offset.  We wrote 0x38 and had to round-trip
            // the value through a stack slot (`stfs f1, 0x50(r1)` /
            // `lwz r10, 0x50(r1)` / `stw r10, 0x38(r31)`), which is also where
            // our extra 0x10 of frame came from.
            ((UIListElementDrawState *)state.mElemDrawState)->mAlpha =
                GetLabelTotalAlpha();

            Vector3 *scale = &sBigScale;
            if (state.mBigScale == 0.0f) {
                scale = &sNormalScale;
            }
            *(Vector3 *)&((UIListElementDrawState *)state.mElemDrawState)->mScaleX =
                *scale;
        }
    }

    float savedAlpha;
    if (TheLoadMgr.EditMode() && mLabelPlaceholder) {
        savedAlpha = ((const UILabel *)(HamLabel *)mLabelPlaceholder)->Style(0).GetAlpha();
        // The label pointer is read BEFORE GetLabelTotalAlpha() clobbers the
        // volatiles -- `lwz r30, 0x31c(r31)` at 0x82483334 sits above the call,
        // and the call site is then just `mr r3, r30`.  Written as
        // `mLabelPlaceholder->Style(0).SetAlpha(t)` MSVC evaluates the argument
        // first (right to left) and re-loads 0x31c afterwards.
        HamLabel *label = mLabelPlaceholder;
        label->Style(0).SetAlpha(GetLabelTotalAlpha());
    }

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

    // RESIDUAL (w7-as, 96.2 canonical): what is left is one block-placement
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

        if (entering == paddedStates[i].mActive) {
            if (!paddedStates[i].mSelected) {
                DrawRibbon(i, ribbonXfm, xfm, paddedStates[i], paddingPerSide, numItems, startOffset, disengaged);
            } else {
                selectedXfm = ribbonXfm;
                selectedIdx = i;
            }
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
