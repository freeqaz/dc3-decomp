#include "rndobj\Flare.h"
#include "Rnd.h"
#include "math/Geo.h"
#include "math\Rot.h"
#include "math\Utl.h"
#include "obj/Object.h"
#include "os\Debug.h"
#include "rndobj/Cam.h"
#include "rndobj\Draw.h"
#include "rndobj\Env.h"
#include "rndobj\HiResScreen.h"
#include "rndobj\Mat.h"
#include "rndobj\Trans.h"
#include "rndobj\Utl.h"
#include <string.h>

RndFlare::RndFlare()
    : mPointTest(true), mAreaTest(true), mVisible(false), mSizes(0.1, 0.1), mMat(this),
      mRange(0, 0), mOffset(0), mSteps(1), mStep(0), mOcclusionResult(0), mOcclusionReady(false),
      mOcclusionPending(false), mScaleFactors(1, 1) {
    mMatrix.Identity();
}

RndFlare::~RndFlare() { TheRnd.RemovePointTest(this); }

BEGIN_HANDLERS(RndFlare)
    HANDLE_ACTION(set_steps, SetSteps(_msg->Int(2)))
    HANDLE_ACTION(set_point_test, SetPointTest(_msg->Int(2)))
    HANDLE_SUPERCLASS(RndTransformable)
    HANDLE_SUPERCLASS(RndDrawable)
    HANDLE_SUPERCLASS(Hmx::Object)
END_HANDLERS

BEGIN_PROPSYNCS(RndFlare)
    SYNC_PROP(mat, mMat)
    SYNC_PROP(sizes, mSizes)
    SYNC_PROP(steps, mSteps)
    SYNC_PROP(range, mRange)
    SYNC_PROP(offset, mOffset)
    SYNC_PROP_MODIFY(point_test, mPointTest, TheRnd.RemovePointTest(this))
    SYNC_SUPERCLASS(RndTransformable)
    SYNC_SUPERCLASS(RndDrawable)
    SYNC_SUPERCLASS(Hmx::Object)
END_PROPSYNCS

BEGIN_SAVES(RndFlare)
    SAVE_REVS(7, 0)
    SAVE_SUPERCLASS(Hmx::Object)
    SAVE_SUPERCLASS(RndTransformable)
    SAVE_SUPERCLASS(RndDrawable)
    bs << mMat << mSizes << mRange << mSteps;
    bs << mPointTest;
    bs << mOffset;
END_SAVES

BEGIN_COPYS(RndFlare)
    CREATE_COPY_AS(RndFlare, f)
    MILO_ASSERT(f, 0x19);
    COPY_SUPERCLASS(Hmx::Object)
    COPY_SUPERCLASS(RndTransformable)
    COPY_SUPERCLASS(RndDrawable)
    COPY_MEMBER_FROM(f, mSizes)
    COPY_MEMBER_FROM(f, mMat)
    COPY_MEMBER_FROM(f, mVisible)
    COPY_MEMBER_FROM(f, mRange)
    COPY_MEMBER_FROM(f, mOffset)
    COPY_MEMBER_FROM(f, mSteps)
    COPY_MEMBER_FROM(f, mPointTest)
    mOcclusionPending = false;
    mOcclusionReady = false;
END_COPYS

INIT_REVS(7, 0)

BEGIN_LOADS(RndFlare)
    LOAD_REVS(bs)
    ASSERT_REVS(7, 0)
    if (d.rev > 3) {
        Hmx::Object::Load(bs);
    }
    RndTransformable::Load(bs);
    RndDrawable::Load(bs);
    if (d.rev > 0) {
        bs >> mMat;
    }
    if (d.rev > 2) {
        bs >> mSizes;
    } else {
        bs >> mSizes.x;
        mSizes.y = mSizes.x;
    }
    if (d.rev > 1) {
        bs >> mRange >> mSteps;
    }
    if (d.rev > 4) {
        d >> mPointTest;
    }
    if (d.rev > 6) {
        bs >> mOffset;
    }
    mOcclusionPending = false;
    mOcclusionReady = false;
    CalcScale();
END_LOADS

void RndFlare::Print() {
    TheDebug << "   mat: " << mMat << "\n";
    TheDebug << "   sizes: " << mSizes << "\n";
    TheDebug << "   range: " << mRange << "\n";
    TheDebug << "   offset:" << mOffset << "\n";
    TheDebug << "   steps: " << mSteps << "\n";
    TheDebug << "   point test: " << mPointTest << "\n";
}

void RndFlare::DrawShowing() {
    if (TheRnd.DrawMode() != 0)
        return;
    RndCam *cam = RndCam::Current();

    const Transform &worldXfm = WorldXfm();
    Vector2 screenPos;
    float depth = cam->WorldToScreen(worldXfm.v, screenPos);

    float scale;
    Hmx::Rect localRect = CalcRect(screenPos, scale);

    float visibility = 0.0f;
    if (RectOffscreen(localRect) || depth <= 0.0f) {
        mOcclusionResult = 0;
        mStep = 0;
        mOcclusionReady = false;
        mOcclusionPending = false;
    } else {
        bool useOccResult = false;
        if (mPointTest && !TheHiResScreen.IsActive()) {
            // Semantically this is `useOccResult = !mOcclusionPending && mOcclusionReady`,
            // and the image agrees.  Decoded straight off the listing
            // (8270BC98-8270BCBC): test 0x89 (= mOcclusionPending) first and take 0 when
            // it is set, otherwise test 0x88 (= mOcclusionReady) and take 1, otherwise 0;
            // the result is narrowed by `clrlwi r28,r11,24` and lives in r28.  That also
            // fixes the field order: the two stores after it are `stb r26,0x88` (ready =
            // false) then `stb r10,0x89` (pending = the old ready), so 0x88 is
            // mOcclusionReady and 0x89 is mOcclusionPending -- no behaviour difference.
            // Spelling the `&&` DOES reproduce that clrlwi and the r28 home, but it
            // stops MSVC sharing slot 0x50 between `scale` and the int->float conversion
            // scratch: the scratch takes 0x60 and 0x90, `dir` moves 0x60 -> 0x70 and the
            // frame grows to 0x140, re-opening about twenty displacement rows.  Net
            // 98.94 -> 97.75 canonical (w7-n 2026-09-14, w15-a, and w27-c 2026-11-02
            // re-measured it on this tree).  The older note here claimed the `&&` form
            // kept the image's 0x130 frame; it does not.
            // Keep the comma-operator form until a spelling is found that gets the
            // clrlwi without moving the slots.
            //
            // w27-c (2026-11-02): the 24 residual rows are ONE register exchange plus
            // that clrlwi.  The image keeps the `this-0xc0` interior pointer (13 uses:
            // WorldXfm_Force, CalcRect, RectOffscreen) in r27 and `useOccResult` in r28;
            // we take the opposite pair, which is 15 rows on its own.  Register-
            // insensitive comparison of the two instruction multisets leaves exactly one
            // unmatched instruction, the `clrlwi R,R,24` -- i.e. both bodies are the same
            // computation and this row set is not hiding a missing call or a wrong
            // field.  Two other rows are pure order: `fmadds f0,f13,f12` vs
            // `fmadds f0,f12,f13` (idx 118) and the two `stfs` to 0x30/0x34 (idx 214/215).
            // Dropping the `oldReady` temp below (RB3's literal `mOcclusionPending =
            // mOcclusionReady; mOcclusionReady = false;`) is byte-inert: same 24 rows,
            // same 98.94405 / 98.55944.
            if (mOcclusionPending || (useOccResult = true, !mOcclusionReady)) {
                useOccResult = false;
            }
            bool oldReady = mOcclusionReady;
            mOcclusionReady = false;
            mOcclusionPending = oldReady;

            const Transform &flareXfm = WorldXfm();
            const Transform &camXfm = cam->WorldXfm();
            Vector3 dir;
            Subtract(camXfm.v, flareXfm.v, dir);
            Normalize(dir, dir);

            const Transform &flareXfm2 = WorldXfm();
            float offset = mOffset;
            dir.x = offset * dir.x + flareXfm2.v.x;
            dir.y = dir.y * offset + flareXfm2.v.y;
            dir.z = dir.z * offset + flareXfm2.v.z;
            TheRnd.TestPoint(dir, this);
        } else {
            mOcclusionResult = scale;
            mVisible = true;
        }

        if (useOccResult) {
            mStep = mVisible ? mSteps : 0;
        } else {
            int steps = mSteps;
            int newStep = ((int)(unsigned char)mVisible * 2 + mStep) - 1;
            if (newStep <= steps) {
                steps = ((unsigned int)newStep >> 31) - 1 & newStep;
            }
            mStep = steps;
        }

        float ratio = (float)mStep / (float)mSteps;
        if (mAreaTest) {
            ratio = (mOcclusionResult / (localRect.w * localRect.h)) * ratio;
        }

        if (visibility < ratio) {
            float alpha = 1.0f;
            if (mMat) {
                float rangeX = mRange.x;
                float t;
                if (rangeX != mRange.y) {
                    t = (depth - mRange.y) / (rangeX - mRange.y);
                } else {
                    t = 1.0f;
                }
                alpha = Clamp(0.0f, 1.0f, t * ratio);

                float r = alpha, g = alpha, b = alpha;
                if (mMat->UseEnviron() && RndEnviron::Current()) {
                    const Hmx::Color &ambColor = RndEnviron::Current()->AmbientColor();
                    r = ambColor.red * alpha;
                    g = ambColor.green * alpha;
                    b = ambColor.blue * alpha;
                }
                mMat->SetColor(r, g, b);
            }

            if (mMat && mMat->GetTexGen() == kTexGenXfm) {
#ifdef HX_NATIVE
                // Retail copies a whole 0x40-byte Transform into a 0x30-byte
                // Matrix3 local and back, overrunning the local by 16 bytes; the
                // retail binary has this overrun too, so the X360 arm keeps it.
                // Natively keep a Transform: the rotation is rebuilt and the
                // translation is carried through unchanged (rb3-xenon W16-UB,
                // 63407036c).
                Transform texXfm = mMat->TexXfm();
                MakeRotMatrixZ(screenPos.x - 0.5f, texXfm.m);
                RndMat *mat = mMat;
                mat->TexXfm() = texXfm;
#else
                // w26-r: the local is a Transform, not a Matrix3.  The 0x40-byte
                // memcpy above and below is the image's own (a Matrix3 local is
                // overflowed by 16 bytes there); sizing the local to the copy
                // keeps every memcpy byte-for-byte identical on the X360 arm and
                // restores the image's 0x130 frame -- the Matrix3 version ran a
                // 0x120 one, 16 bytes short.  Same statements, same order.
                Transform texXfm;
                memcpy(&texXfm, &mMat->TexXfm(), 0x40);
                MakeRotMatrixZ(screenPos.x - 0.5f, texXfm.m);
                RndMat *mat = mMat;
                memcpy(&mat->TexXfm(), &texXfm, 0x40);
#endif
                mat->MarkDirty(2);
            }

            Hmx::Rect *drawRect;
            if (mAreaTest) {
                drawRect = &mArea;
            } else {
                drawRect = &CalcRect(screenPos, scale);
            }
            Hmx::Rect drawRectCopy = *drawRect;
            Hmx::Color white(1.0f, 1.0f, 1.0f, 1.0f);
            TheRnd.DrawRect(drawRectCopy, white, mMat, nullptr, nullptr);
        }
    }
}

void RndFlare::Mats(std::list<RndMat *> &list, bool) {
    if (mMat) {
        MatShaderOptions shaderOpts = GetDefaultMatShaderOpts(this, mMat);
        mMat->SetShaderOpts(shaderOpts);
        list.push_back(mMat);
    }
}

void RndFlare::SetMat(RndMat *mat) { mMat = mat; }

void RndFlare::SetPointTest(bool test) {
    if (!test && mPointTest) {
        TheRnd.RemovePointTest(this);
    }
    mPointTest = test;
}

Hmx::Rect &RndFlare::CalcRect(Vector2 &screenPos, float &visibleArea) {
    float flareSize = mSizes.x;
    if (flareSize != mSizes.y) {
        RndCam *cam = RndCam::Current();
        float dot = Dot(cam->WorldXfm().m.y, WorldXfm().m.y);
        float blend = Max(0.0f, -dot);
        flareSize = Interp(mSizes.x, mSizes.y, blend);
    }

    int width = TheRnd.Width();
    int height = TheRnd.Height();
    if (TheHiResScreen.IsActive()) {
        int tiling = TheHiResScreen.GetTiling();
        width = width * tiling - TheHiResScreen.GetPaddingX() * TheHiResScreen.GetTiling();
        height = height * tiling - TheHiResScreen.GetPaddingY() * TheHiResScreen.GetTiling();
        Hmx::Rect screenRect = TheHiResScreen.ScreenRect();
        screenPos.x -= screenRect.x;
        screenPos.y -= screenRect.y;
    }
    CalcScale();

    float sizeW = width * flareSize;
    mArea.w = mScaleFactors.x * sizeW;
    mArea.h = (mScaleFactors.y * height * sizeW) / (TheRnd.YRatio() * width);
    mArea.x = screenPos.x * width - mArea.w * 0.5f;
    mArea.y = screenPos.y * height - mArea.h * 0.5f;

    auto _tmp0 = Min<float>(width, mArea.x + mArea.w);
    float visibleHeight = Min<float>(height, mArea.y + mArea.h) - Max(0.0f, mArea.y);
    float visibleWidth = _tmp0 - Max(0.0f, mArea.x);
    visibleArea = visibleWidth * visibleHeight;
    return mArea;
}

bool RndFlare::RectOffscreen(const Hmx::Rect &r) const {
    if (r.x + r.w < 0)
        return true;
    else if (r.y + r.h < 0)
        return true;
    else if (r.x > TheRnd.Width())
        return true;
    else if (r.y > TheRnd.Height())
        return true;
    else
        return false;
}

// w25-oa: 75/76. The one row is the z term of the inlined Dot(v28, mMatrix.z)
// (c2's commutative sort key, C2RS-BRIDGE 8.7). mMatrix.z.z is a memory leaf on
// base temp 377 (377 & 3 == 1, key 0x14008), above the cross z, v28.z (V18,
// 0x10240), so the load goes first. The image has cross z first, so it needs that
// base == 0 mod 4 (key 0x10008), i.e. a temp shift of +3 or -1 before it. The
// shift must also keep z.x's base 386 != 0 mod 4; the y term (z.y temp 396
// over V14) holds for any shift. Nothing reachable without new code: all 18
// code-identical spellings in a 96-way paren/order grid keep 377. Parens on
// `-len`, Length(mMatrix.x) and Length(mMatrix.z) (grid k21100) move every temp by +3
// (380/389/399) and flip the z term as predicted, but reschedule the block (56/76).
// Named refs or an explicit Dot body change code, because the inline's
// reference params are what keep the loads off a separate base.
// Also inert (w25-gd, ~110 variants): Dot argument order, v28 declaration
// position, Cross before/after Length, named scaleY/scaleX/negLen, `0 < dot`.
void RndFlare::CalcScale() {
    if (mMatrix != WorldXfm().m) {
        Vector3 v28;
        mMatrix = WorldXfm().m;
        float len = Length(mMatrix.z);
        Cross(mMatrix.x, mMatrix.y, v28);
        mScaleFactors.Set(Length(mMatrix.x), Dot(v28, mMatrix.z) > 0.0f ? len : -len);
    }
}

void RndFlare::SetSteps(int i1) {
    if (i1 < 1) {
        i1 = 1;
    }
    if (mStep == mSteps) {
        mStep = i1;
    } else {
        float stepsFloat = (float)mSteps;
        float i1Float = (float)i1;
        mStep = (int)(i1Float / stepsFloat) * mStep;
    }
    mSteps = i1;
}

// ---------------------------------------------------------------------------
// w48-b (2026-11-06) EQUIVALENCE CERTIFICATE -- RndFlare::DrawShowing,
// canonical 98.94405 / fuzzy 98.55944.  TARGET 286 instructions, OURS 285
// (1144 B vs 1140 B): this row is ONE INSTRUCTION SHORT on our side.
// Placed at END OF FILE deliberately.  MILO_ASSERT takes an explicit line
// LITERAL rather than __LINE__ (src/system/os/Debug.h:93), so a comment could
// not have moved an assert's message, but nothing above this line is touched
// at all and the certificate therefore cannot perturb the row it describes.
//
// BEHAVIOUR: no divergence.  All 24 non-equal rows (263 equal / 21 diff_arg /
// 2 delete / 1 insert) fall into four groups.
//   (a) 15 rows are ONE register exchange.  The interior pointer `this-0xc0`
//       (13-14 uses: four inlined WorldXfm calls, CalcRect, RectOffscreen and
//       the two 0xbd(rN) drawable loads) is r27 in the image and r28 here;
//       `useOccResult` takes whichever is left.  Same value, same instruction.
//   (b) The instruction we do not emit is the image's `clrlwi r28, r11, 24`.
//       It narrows a merge value that is provably 0/1 on every path -- the
//       arms are `li r11,1` and `mr r11,r26` with r26 = 0 -- so the
//       narrowing is a no-op and the row is not hiding a missing call or a
//       wrong field.
//   (c) idx 118, one commutative `fmadds`: `dir.x = offset * dir.x + v.x`.
//       Only the x term differs; y and z (idx 122/126) already take the
//       image's operand order from the same source spelling, which is what
//       makes this a scheduling/key artifact rather than a form difference.
//   (d) idx 214/215, the order of two `stfs` inside RndMat::SetColor.  The
//       multiset is identical -- r, g and b all land on 0x2c/0x30/0x34.
//
// MECHANISM (c2rs tap: `cands.sh src/system/rndobj/Flare.cpp
// DrawShowing@RndFlare`; stage-tap-obj-differs 0, replay-vs-capture-differs 0,
// cands-errors 0, 40 candidates):
//   id 50  pri -15  tie 15  ben 17 ref 14  sym 48/k03 org arena#51 -> r28
//   id  5  pri -18  tie 69  ben  4 ref  4  sym  5/k04 org arena#6  -> r27
// They are the LAST TWO POPS of the worklist.  The image's assignment is the
// same two values with their priorities EQUAL, after which tie 69 beats tie 15
// and `useOccResult` takes r28 first.  So the lever is THREE priority units on
// one of the bottom two candidates.
// 22 spellings measured this wave, none reached it.  Byte-identical to
// baseline: an if/else with both arms assigning, and the comma form with
// `mOcclusionReady == false`.  Everything else loses: `useOccResult =
// !mOcclusionPending && mOcclusionReady` 97.75175, unbraced if/else 98.15734,
// named `occOn` 98.88112, `(...) == false` 98.03147, ternary 95.91608,
// default-false plus a single `= true` arm 98.59441, a named pending/ready
// pair 97.75175, operands reversed 98.88811, `!(a || !b)` 97.75175, nested if
// 98.59441, `= mOcclusionReady` 98.24476.
// The `&&` family is REFUSED and the reason is MEASURED, not inherited: every
// form that supplies the missing `clrlwi` ALSO grows the frame 0x130 -> 0x140
// (`stwu r1,-0x140` at idx 5; every stack slot shifts +0x10 -- 0x50->0x60,
// 0x58->0x68, 0x80->0x70, 0x90->0xa0), re-opening about twenty displacement
// rows and landing the row at 97.75175.  One instruction is not worth twenty
// rows.  Also byte-identical: deleting the never-reassigned `visibility` local
// and comparing the literal, and `ratio > visibility`.
// ---------------------------------------------------------------------------
