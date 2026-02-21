#include "DebugGraph.h"
#include "rndobj/Graph.h"
#include "utl/MakeString.h"

void DebugGraph::AddData(float data, bool b)  {
    Sample sample;
    sample.data = data;
    sample.b = b;
    mSamples.push_front(sample);

    if (mSamples.size() == unk38 + 1) {
        mSamples.pop_back();
    }
}

void DebugGraph::Draw() {
    RndGraph *graph = RndGraph::GetOneFrame();
    graph->AddRectFilled2D(mRect, mColorB);

    if (unk50) {
        Vector2 minPos(mRect.x, (mRect.y + mRect.h) - 0.02f);
        graph->AddScreenString(MakeString("%.3f", unk3c), minPos, mColorA);
        Vector2 maxPos(mRect.x, mRect.y);
        graph->AddScreenString(MakeString("%.3f", unk40), maxPos, mColorA);
    }

    float range = unk40 - unk3c;
    if (unk44 != FLT_MAX) {
        float normThresh = (unk44 - unk3c) / range;
        float clampedX = 0.0f;
        if (-normThresh < 0.0f) {
            clampedX = normThresh;
        }
        float cx = 1.0f;
        if ((clampedX - 1.0f) < 0.0f) {
            cx = clampedX;
        }
        float clampedY = 0.0f;
        if (-normThresh < 0.0f) {
            clampedY = normThresh;
        }
        float cy = 1.0f;
        if ((clampedY - 1.0f) < 0.0f) {
            cy = clampedY;
        }
        Vector2 lineStart(mRect.x, (1.0f - cx) * mRect.h + mRect.y);
        Vector2 lineEnd(mRect.x + mRect.w, (1.0f - cy) * mRect.h + mRect.y);
        Hmx::Color white(1.0f, 1.0f, 1.0f, 1.0f);
        graph->AddScreenLine(lineStart, lineEnd, white, false);

        Vector2 labelPos(mRect.x, 0.0f);
        float normThresh2 = (unk44 - unk3c) / range;
        float clamped2 = 0.0f;
        if (-normThresh2 < 0.0f) {
            clamped2 = normThresh2;
        }
        float c2 = 1.0f;
        if ((clamped2 - 1.0f) < 0.0f) {
            c2 = clamped2;
        }
        labelPos.y = (1.0f - c2) * mRect.h + mRect.y;
        Hmx::Color white2(1.0f, 1.0f, 1.0f, 1.0f);
        graph->AddScreenString(MakeString("%.3f", unk44), labelPos, white2);
    }

    Vector2 namePos(mRect.x + 0.1f, mRect.y);
    Hmx::Color white3(1.0f, 1.0f, 1.0f, 1.0f);
    graph->AddScreenString(unk48.c_str(), namePos, white3);

    std::list<Sample>::iterator it = mSamples.begin();
    if (it != mSamples.end()) {
        int idx = 1;
        float normVal = (it->data - unk3c) / range;
        float normIdx = 0.0f / (float)(unk38 - 1);
        float clampedVal = 0.0f;
        if (-normVal < 0.0f) {
            clampedVal = normVal;
        }
        float clampedIdx = 0.0f;
        if (-normIdx < 0.0f) {
            clampedIdx = normIdx;
        }
        float cv = 1.0f;
        if ((clampedVal - 1.0f) < 0.0f) {
            cv = clampedVal;
        }
        float ci = 1.0f;
        if ((clampedIdx - 1.0f) < 0.0f) {
            ci = clampedIdx;
        }
        Vector2 prevPt((1.0f - ci) * mRect.w + mRect.x, (1.0f - cv) * mRect.h + mRect.y);
        ++it;
        while (it != mSamples.end()) {
            float normVal2 = (it->data - unk3c) / range;
            float normIdx2 = (float)idx / (float)(unk38 - 1);
            float clampedVal2 = 0.0f;
            if (-normVal2 < 0.0f) {
                clampedVal2 = normVal2;
            }
            float clampedIdx2 = 0.0f;
            if (-normIdx2 < 0.0f) {
                clampedIdx2 = normIdx2;
            }
            float cv2 = 1.0f;
            if ((clampedVal2 - 1.0f) < 0.0f) {
                cv2 = clampedVal2;
            }
            float ci2 = 1.0f;
            if ((clampedIdx2 - 1.0f) < 0.0f) {
                ci2 = clampedIdx2;
            }
            Vector2 curPt((1.0f - ci2) * mRect.w + mRect.x, (1.0f - cv2) * mRect.h + mRect.y);
            if (it->b) {
                Vector2 topPt(curPt.x, mRect.y);
                Vector2 botPt(curPt.x, mRect.h + mRect.y);
                Hmx::Color white4(1.0f, 1.0f, 1.0f, 1.0f);
                graph->AddScreenLine(topPt, botPt, white4, false);
            }
            graph->AddScreenLine(curPt, prevPt, mColorA, false);
            prevPt = curPt;
            idx++;
            ++it;
        }
    }
}