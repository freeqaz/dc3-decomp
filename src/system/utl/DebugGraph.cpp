#include "DebugGraph.h"
#include "rndobj\Graph.h"
#include "utl\MakeString.h"

void DebugGraph::AddData(float data, bool b) {
    Sample sample;
    sample.data = data;
    sample.b = b;
    mSamples.push_front(sample);

    if (mSamples.size() == mMaxSamples + 1) {
        mSamples.pop_back();
    }
}

inline float clamp(float val) {
    float c = -val >= 0.0f ? 0.0f : val;
    return c - 1.0f >= 0.0f ? 1.0f : c;
}

void DebugGraph::Draw() {
    RndGraph *rnd = RndGraph::GetOneFrame();

    Hmx::Rect rect(mRect.x, mRect.y, mRect.w, mRect.h);
    rnd->AddRectFilled2D(rect, mColorB);

    if (mIsVisible) {
        Vector2 minPos(mRect.x, mRect.y + mRect.h - 0.02f);
        rnd->AddScreenString(MakeString("%.3f", mMinValue), minPos, mColorA);

        Vector2 maxPos(mRect.x, mRect.y);
        rnd->AddScreenString(MakeString("%.3f", mMaxValue), maxPos, mColorA);
    }

    if (mThresholdValue != FLT_MAX) {
        Hmx::Color color(1.0f, 1.0f, 1.0f, 1.0f);

        Vector2 p2(
            mRect.w + mRect.x,
            mRect.y + mRect.h * (1.0f - clamp((mThresholdValue - mMinValue) / (mMaxValue - mMinValue)))
        );

        Vector2 p1(
            mRect.x, mRect.y + mRect.h * (1.0f - clamp((mThresholdValue - mMinValue) / (mMaxValue - mMinValue)))
        );

        rnd->AddScreenLine(p1, p2, color, false);

        Vector2 textPos(
            mRect.x, mRect.y + mRect.h * (1.0f - clamp((mThresholdValue - mMinValue) / (mMaxValue - mMinValue)))
        );

        rnd->AddScreenString(
            MakeString("%.3f", mThresholdValue), textPos, Hmx::Color(1.0f, 1.0f, 1.0f, 1.0f)
        );
    }

    Vector2 titlePos;

    titlePos.y = mRect.y;
    Hmx::Color color(1.0f, 1.0f, 1.0f, 1.0f);
    titlePos.x = mRect.x + 0.1f;

    rnd->AddScreenString(mGraphName.c_str(), titlePos, color);

    if (!mSamples.empty()) {
        auto it = mSamples.begin();
        int idx = 1;
        Vector2 prevPos(
            mRect.x + mRect.w * (1.0f - clamp(0.0f / (float)(mMaxSamples - 1))),
            mRect.y + mRect.h * (1.0f - clamp((it->data - mMinValue) / (mMaxValue - mMinValue)))
        );

        ++it;

        for (; it != mSamples.end(); ++it) {
            Vector2 curPos(
                mRect.x + mRect.w * (1.0f - clamp((float)idx / (float)(mMaxSamples - 1))),
                mRect.y + mRect.h * (1.0f - clamp((it->data - mMinValue) / (mMaxValue - mMinValue)))
            );

            if (it->b) {
                rnd->AddScreenLine(
                    Vector2(
                        mRect.x
                            + mRect.w * (1.0f - clamp((float)idx / (float)(mMaxSamples - 1))),
                        mRect.y
                    ),
                    Vector2(
                        mRect.x
                            + mRect.w * (1.0f - clamp((float)idx / (float)(mMaxSamples - 1))),
                        mRect.y + mRect.h
                    ),
                    Hmx::Color(1.0f, 1.0f, 1.0f, 1.0f),
                    false
                );
            }
            rnd->AddScreenLine(curPos, prevPos, mColorA, false);
            prevPos = curPos;
            idx++;
        }
    }
}
