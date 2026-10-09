#include "char\CharClipDisplay.h"
#include "char\CharBones.h"
#include "char\CharIKFoot.h"
#include "math/Geo.h"
#include "obj/Object.h"
#include "os\Debug.h"
#include "rndobj\Rnd.h"
#include <cmath>

float CharClipDisplay::sZoom = 1.0f;
float CharClipDisplay::sEm;
ObjectDir *CharClipDisplay::sDir;

void CharClipDisplay::Init(ObjectDir *dir) {
    sDir = dir;
    sEm = TheRnd.DrawString("", Vector2(0, 0), Hmx::Color(1.0f, 0.0f, 0.0f), false).y;
}

void CharClipDisplay::SetClip(CharClip *clip, bool b) {
    mClip = clip;
    SetText(clip->Name());
    SetStartEnd(clip->StartBeat(), clip->EndBeat(), b);
}

void CharClipDisplay::SetText(const char *text) {
    strcpy(mClipNameBuffer, text);
    mTextWidth = TheRnd.DrawString(text, Vector2(0, 0), Hmx::Color(1.0f, 0.0f, 0.0f), false).x
        + sEm;
}

float CharClipDisplay::LineSpacing() { return sEm * 2.0f; }

float CharClipDisplay::GetX(float beat) const {
    float endBeat = mEndBeat;
    float startBeat = mStartBeat;
    float beatRange = (endBeat > startBeat) ? (endBeat - startBeat) : 1.0f;
    float leftMargin = sEm * 3.0f;
    float textWidth = mTextWidth + mPadding + leftMargin;
    return ((TheRnd.Width() - leftMargin) - textWidth) * ((beat - startBeat) / beatRange) + textWidth;
}

Hmx::Object *CharClipDisplay::FindSource(Hmx::Object *obj) {
    for (ObjDirItr<Hmx::Object> it(ObjectDir::Main(), false); it != nullptr; ++it) {
        MsgSinks *sinks = it->Sinks();
        if (sinks != nullptr && sinks->HasSink(obj)) {
            return it;
        }
    }
    return nullptr;
}

// w12-b residual (one row: the image re-reads mStartBeat after storing it)
// closed by w21-bb, see the reference in the resetZoom arm.  Inert: writing
// the sum as `mStartBeat + (...)`; the RB3 trailing `GetX(mCursorBeat);`
// (dead, folds away).
__declspec(noinline) void
CharClipDisplay::SetStartEnd(float start, float end, bool resetZoom) {
    mViewStartBeat = start;
    mViewEndBeat = end;
    mStartBeat = start;
    mEndBeat = end;
    float zoomRange = 16.0f / sZoom;
    if (resetZoom) {
        float margin = sEm * 3.0f;
        float screenWidth = (float)(long long)TheRnd.Width();
        float textOffset = mPadding + mTextWidth + margin;
        // w21-bb: 98.75 -> 100.  Storing through a reference and reading the
        // member back by name stops MSVC forwarding the stored value, so it
        // re-reads mStartBeat (`lfs f12, 0xc(r3)` after the second lwa) as the
        // image does.  The reference name is ours; the same object is written
        // and read, so behaviour is unchanged.  Reading through the same
        // reference forwards again (93.5).
        float &startBeat = mStartBeat;
        startBeat =
            mCursorBeat - ((screenWidth * 0.5f - textOffset) * zoomRange) / screenWidth;
        mEndBeat = (((screenWidth - margin) - textOffset) * zoomRange)
                / (float)(long long)TheRnd.Width()
            + mStartBeat;
    } else {
        if (end - start > zoomRange) {
            float cursor = mCursorBeat;
            float halfZoom = zoomRange * 0.5f;
            if (cursor < halfZoom + start) {
                mEndBeat = zoomRange + start;
            } else {
                if (cursor > end - halfZoom) {
                    mStartBeat = end - zoomRange;
                    return;
                }
                mStartBeat = cursor - halfZoom;
                mEndBeat = halfZoom + cursor;
            }
        } else {
            if (end != start) {
                return;
            }
            mStartBeat = start - zoomRange * 0.5f;
            mEndBeat = zoomRange * 0.5f + end;
        }
    }
}

void CharClipDisplay::DrawBeatString(char const *c, float f1, Hmx::Color const &color) {
    // BUG FIX (w19-x): x is the beat's column minus 4, y is the row minus 18.
    // Image 0x823DF25C `fsubs f0, f12, f0` (f12 = mDrawPosY, 0x18; f0 = 18.0f)
    // stored to the Vector2's .y (0x54(r1)), then 0x823DF264 `fsubs f0, f1, f13` (f1 =
    // GetX(), f13 = 4.0f) stored to .x (0x50(r1)).  We had both the axes and
    // the offsets crossed (x = mDrawPosY - 4, y = GetX - 18).  RB3 agrees.
    float x = GetX(f1);
    Vector2 pos;
    pos.y = mDrawPosY - 18.0f;
    pos.x = x - 4.0f;
    TheRnd.DrawString(c, pos, color, true);
}

void CharClipDisplay::DrawBlend(float beat, float weight) {
    Hmx::Rect rect(0.0f, mDrawPosY + 1.0f, 0.0f, 2.0f);
    float x1 = GetX(beat);
    rect.x = x1;
    float x2 = GetX(beat + weight);
    Hmx::Color blendColor(0.0f, 0.0f, 1.0f, 0.4f);
    rect.w = x2 - x1;
    TheRnd.DrawRect(rect, blendColor, nullptr, nullptr, nullptr);
    rect.h = 4.0f;
    rect.y = mDrawPosY - 1.0f;
    rect.w = 3.0f;
    float midX = GetX(weight * 0.5f + beat);
    rect.x = midX - 1.0f;
    Hmx::Color markerColor(0.0f, 0.0f, 1.0f, 1.0f);
    TheRnd.DrawRect(rect, markerColor, nullptr, nullptr, nullptr);
}

void CharClipDisplay::DrawBeatString(float beat, Hmx::Color const &color) {
    const char *text;
    if (beat == (float)std::floor(beat)) {
        text = MakeString("%d", (int)beat);
    } else {
        text = MakeString("%.1f", beat);
    }
    DrawBeatString(text, beat, color);
}

void CharClipDisplay::DrawTrack() {
    Hmx::Color white(1.0f, 1.0f, 1.0f, 1.0f);
    Hmx::Color green(0.0f, 1.0f, 0.0f, 1.0f);
    Hmx::Color black(0.0f, 0.0f, 0.0f, 1.0f);

    // Compute displayed start/end beats (use the min of mViewStartBeat/mStartBeat and mViewEndBeat/mEndBeat)
    float startBeat = (mStartBeat - mViewStartBeat >= 0.0f) ? mStartBeat : mViewStartBeat;
    float endBeat = (mEndBeat - mViewEndBeat >= 0.0f) ? mViewEndBeat : mEndBeat;

    float drawY = mDrawPosY;
    float halfEm = sEm * 0.5f;
    float nameY = -(halfEm - drawY);

    // Draw track background rect
    Hmx::Rect trackRect;
    trackRect.x = GetX(startBeat);
    trackRect.y = drawY;
    trackRect.w = GetX(endBeat) - trackRect.x;
    trackRect.h = 3.0f;
    TheRnd.DrawRect(trackRect, white, nullptr, nullptr, nullptr);

    // Open residual (w7-r, 2026-09-14): the image never keeps 3.0f in a
    // register. It pins the literal-pool ANCHOR in a callee-saved GPR
    // (`lis r25, __real@40400000@ha`) and re-issues `lfs f, __real@40400000@l(r25)`
    // at all four use sites (823DF544 / 823DF5A0 / 823DF9E0 / 823DFA40:
    // trackRect.h, markerY, and the two `sEm * 3.0f` label offsets). We CSE the
    // value into callee-saved f21 instead, which is exactly the
    // GPR -1 / FPR +1 prologue delta (TGT 9/12 vs ours 8/13) and drags the
    // f-register numbering with it. REFUTED: moving `beat`/`markerRect` ahead of
    // markerY/markerH so the sub is scheduled after the loop-entry `bgt` (the
    // image's order) is exactly neutral -- 97.31, same 11 insert/delete rows.
    // The separate `lbl_82020B54` vs `__real@40000000` row is benign: that
    // .rdata word IS 2.0f, it is just pooled in CharacterTest's object.
    // w24-c3 (97.31 -> 97.8 instr-level): the marker rect's y/h are written
    // INSIDE the loop as `drawY - 3.0f` / `9.0f`, not through markerY/markerH
    // locals declared above it.  MSVC hoists the invariant subtraction to the
    // preheader AFTER the `bgt` loop guard (image 823DF5A0: `lfs f0,
    // __real@40400000@l(r25)` / `fsubs f27, f29, f0`), and no longer keeps
    // 3.0f in a callee-saved FPR there.
    // w24-c3 STOP at 97.817 (38 rows): still the 3.0f anchor/remat residual
    // above (r25 anchor + 4 reloads vs our f21), plus its f19/f20/f21 and
    // r23/r24/r25 renumbering, the label `0x64/0x14` load order, and the
    // nameColor r-channel store sunk below namePos.x (0x120 at 823DFAC8).
    // Measured inert: markerRect declared inside the loop; `if (mClip) {...}`
    // instead of the goto.  Worse: Rect ctor for trackRect (96.2).
    // decomp-synth hill_climb (2 rounds x 58 variants) found nothing.
    // Mechanism hypothesis (c2 priority colouring, c2-rs P_REGALLOC.md): the
    // 3.0f CSE temp is a candidate whose priority (sum of weight x n_live,
    // minus n_live where it is live but unused) comes out <= 0 in the image
    // -- live across the whole event/IK region with no use -- so it is left
    // in memory and rematerialised from the r25 anchor; ours stays > 0 and
    // gets f21.  Open question: what shortens or splits that range in the
    // image.  Related lead, not chased: every 2.0f in this TU (LineSpacing,
    // DrawBlend, the start label here) loads lbl_82020B54, a NON-COMDAT
    // .rdata word just below this object's split start (0x82020B58), never
    // __real@40000000 (0x820E6390) -- so the original likely had a TU-local
    // const float object for 2.0, and maybe for other constants too.
    // Draw integer beat markers
    float firstBeat = (float)std::ceil(startBeat);
    float lastBeat = (float)std::floor(endBeat);
    if (firstBeat + 1.0f != firstBeat) {
        float beat = firstBeat;
        Hmx::Rect markerRect;
        while (beat <= lastBeat) {
            markerRect.y = drawY - 3.0f;
            markerRect.h = 9.0f;
            markerRect.x = GetX(beat);
            markerRect.w = 1.0f;
            TheRnd.DrawRect(markerRect, green, nullptr, nullptr, nullptr);
            beat += 1.0f;
        }
    }

    if (mClip == nullptr)
        goto drawName;

    // Draw beat events
    {
        bool firstEvent = true;
        int idx = 0;
        float eventAlpha = 0.2f;
        float eventLabelOffset = 10.0f;
        while ((unsigned int)idx < (unsigned int)mClip->NumBeatEvents()) {
            const CharClip::BeatEvent &ev = mClip->BeatEvents()[idx];
            // w24-c3: labelPos is built from GetX() directly and the event
            // rect reads labelPos.x -- the image stores both labelPos halves
            // first (0x5c then 0x58), then the rect/colour; with a separate
            // `eventX` local MSVC sank the labelPos.x store below the colour.
            Vector2 labelPos(GetX(ev.beat), drawY);
            float halfEmVal = sEm * 0.5f;
            Hmx::Rect eventRect(labelPos.x, drawY - halfEmVal, 1.0f, halfEmVal);
            Hmx::Color eventColor(eventAlpha, eventAlpha, 1.0f, 1.0f);
            TheRnd.DrawRect(eventRect, eventColor, nullptr, nullptr, nullptr);

            if (firstEvent
                && (ev.beat > mCursorBeat
                    || (idx == 0 && mCursorBeat > mClip->BeatEvents().back().beat))) {
                Hmx::Color eventLabelColor(eventAlpha, eventAlpha, 1.0f, 1.0f);
                firstEvent = false;
                labelPos.y -= (halfEmVal + eventLabelOffset);
                TheRnd.DrawString(ev.event.Str(), labelPos, eventLabelColor, true);
            }
            idx += 1;
        }
    }

    // Find IK feet
    {
        CharIKFoot *leftIk = sDir->Find<CharIKFoot>("left.ikfoot", false);
        CharIKFoot *rightIk = sDir->Find<CharIKFoot>("right.ikfoot", false);
        if (leftIk == nullptr && rightIk == nullptr) {
            // No IK feet - draw sample markers
            Hmx::Rect sampleRect(0.0f, drawY + 1.0f, 1.0f, 1.0f);
            float frac;
            int startSample = mClip->BeatToSample(startBeat, &frac);
            int endSample = mClip->BeatToSample(endBeat, &frac);
            for (; startSample <= endSample; startSample++) {
                float sampleBeat = mClip->SampleToBeat(startSample);
                sampleRect.x = GetX(sampleBeat);
                TheRnd.DrawRect(sampleRect, black, nullptr, nullptr, nullptr);
            }
        } else {
            MILO_ASSERT(
                !rightIk || !leftIk || (rightIk->GetData() == leftIk->GetData()), 0xd1
            );
            // w22-a07 BUG FIX: the right-foot-only case used to declare its own
            // block-local `data` and `goto` past the left arm's initializer, so
            // the IK readout read an uninitialized stack slot (ours:
            // `lwz r11, 0x50(r1)`).  The image loads rightIk->GetData() there:
            // `lwz r11, 0xd4(r28)` at 0x823DF880 (r28 = rightIk), reached from
            // `bne cr6, 0xb88` right after the right.ikfoot Find.
            RndTransformable *data = leftIk ? leftIk->GetData() : rightIk->GetData();
            if (data != nullptr) {
                Symbol channelName
                    = CharBones::ChannelName(data->Name(), CharBones::TYPE_POS);
                void *channel = mClip->GetChannel(channelName);
                // A position channel evaluates to ONE Vector3: the image gives
                // it a single 16-byte slot (0x140(r1) in a 0x200 frame whose
                // save area starts at 0x150), and GetDataIndex() picks the axis.
                Vector3 channelData;
                mClip->EvaluateChannel(&channelData, channel, mCursorBeat);
                Hmx::Color ikColor(1.0f, 1.0f, 0.0f, 1.0f);
                float cursorX = GetX(mCursorBeat);
                float posY = mDrawPosY;
                if (leftIk != nullptr) {
                    const char *leftText
                        = MakeString("L: %.1f", (&channelData.x)[leftIk->GetDataIndex()]);
                    TheRnd.DrawString(
                        leftText,
                        Vector2(cursorX - 90.0f, posY + 10.0f),
                        ikColor,
                        true
                    );
                }
                if (rightIk != nullptr) {
                    const char *rightText
                        = MakeString("R: %.1f", (&channelData.x)[rightIk->GetDataIndex()]);
                    TheRnd.DrawString(
                        rightText,
                        Vector2(cursorX - 40.0f, posY + 10.0f),
                        ikColor,
                        true
                    );
                }
            }
        }

        // Draw beat labels for first and last beats
        DrawBeatString(firstBeat, green);
        DrawBeatString(lastBeat, green);

        // Draw start beat label
        {
            float labelX = -((sEm * 2.0f) - (sEm * 3.0f + mPadding + mTextWidth));
            Vector2 startPos(labelX, nameY);
            TheRnd.DrawString(MakeString("%.1f", mViewStartBeat), startPos, white, true);
        }

        // Draw end beat label
        {
            float screenWidth = (float)TheRnd.Width();
            float labelX = -(sEm * 3.0f - screenWidth);
            Vector2 endPos(labelX, nameY);
            TheRnd.DrawString(MakeString("%.1f", mViewEndBeat), endPos, white, true);
        }
    }

drawName:
    // Draw clip name
    {
        Hmx::Color nameColor(1.0f, 1.0f, 1.0f, 1.0f);
        Vector2 namePos(mPadding + sEm, nameY);
        TheRnd.DrawString(mClipNameBuffer, namePos, nameColor, true);
    }
}

void CharClipDisplay::DrawCursor() {
    Hmx::Color yellow(1.0f, 1.0f, 0.0f, 1.0f);
    Hmx::Rect rect;
    float x = GetX(mCursorBeat);
    rect.w = 1.0f;
    rect.x = x;
    rect.h = 9.0f;
    rect.y = mDrawPosY - 3.0f;
    TheRnd.DrawRect(rect, yellow, nullptr, nullptr, nullptr);
    const char *text;
    if (!(mBlendWeight >= 1.0f)) {
        text = MakeString("%.1f (%.2f)", mCursorBeat, mBlendWeight);
    } else {
        text = MakeString("%.1f", mCursorBeat);
    }
    DrawBeatString(text, mCursorBeat, yellow);
}