#include "char/ClipDistMap.h"
#include "char/CharBoneDir.h"
#include "char/CharBonesMeshes.h"
#include "char/CharClip.h"
#include "char/CharUtl.h"
#include "math/Trig.h"
#include "math/Utl.h"
#include "obj/Data.h"
#include "obj/Dir.h"
#include "os/Debug.h"
#include "rndobj/Rnd.h"
#include "rndobj/Trans.h"
#include "string.h"
#include "utl/Std.h"
#include <cmath>

extern "C" void OnlyReturns() {}

static const float sLargeFloat = kHugeFloat;

struct DistMapNodeSort {
    bool operator()(const ClipDistMap::Node &n1, const ClipDistMap::Node &n2) const {
        return n1.curBeat < n2.curBeat;
    }
};

void FindWeights(
    std::vector<RndTransformable *> &transes,
    std::vector<float> &floats,
    const DataArray *arr
) {
    floats.resize(transes.size());
    float f1 = 0;
    for (int i = 0; i < transes.size(); i++) {
        float len = Length(transes[i]->LocalXfm().v);
        if (arr) {
            float f84 = 1;
            arr->FindData(transes[i]->Name(), f84, false);
            len *= f84;
        }
        floats[i] = len;
        f1 += floats[i];
    }
    for (int i = 0; i < floats.size(); i++) {
        floats[i] *= floats.size() / f1;
    }
}

DistEntry &DistEntry::operator= (const DistEntry &right) {
    beat = right.beat;
    bones = right.bones;
    for (int i = 0; i < 4; i++) {
        facing[i] = right.facing[i];
    }
    return *this;
}

DistEntry::DistEntry(const DistEntry &entry) : beat(entry.beat), bones(entry.bones) {
    memcpy(facing, entry.facing, sizeof(entry.facing));
}

void ClipDistMap::GenerateDistEntry(
    CharBonesMeshes &boneMeshes,
    DistEntry &e,
    float beat,
    CharClip *clip,
    const std::vector<RndTransformable *> &bones
) {
    if (e.bones.empty()) {
        e.beat = beat;
        float blendWidth = mBlendWidth / 4.0f;
        float f14 = blendWidth / 2.0f + beat;
        void *facingChannel = clip->GetChannel("bone_facing.rotz");
        for (int i = 0; i < 4; i++, f14 += blendWidth) {
            e.facing[i] = 0;
            if (facingChannel) {
                clip->EvaluateChannel(&e.facing[i], facingChannel, f14);
            }
            e.facing[i] = LimitAng(e.facing[i]);
        }
        e.bones.resize(bones.size() * mNumSamples);
        int eBonesIdx = 0;
        blendWidth = mBlendWidth / (float)mNumSamples;
        f14 = blendWidth / 2.0f + e.beat;
        for (int i = 0; i < mNumSamples; i++, f14 += blendWidth) {
            CharUtlResetTransform(clip->GetResource());
            boneMeshes.Zero();
            clip->ScaleAdd(boneMeshes, 1, f14, 0);
            boneMeshes.PoseMeshes();
            for (int j = 0; j < bones.size(); j++) {
                e.bones[eBonesIdx++] = bones[j]->WorldXfm().v;
            }
        }
    }
}

ClipDistMap::ClipDistMap(
    CharClip *clip1, CharClip *clip2, float f1, float f2, int i, const DataArray *a
)
    : mClipA(clip1), mClipB(clip2), mWeightData(a), mSamplesPerBeat(8),
      mLastMinErr(sLargeFloat), mBeatAlign(f1), mBeatAlignOffset(0), mBlendWidth(f2),
      mNumSamples(i), mDists(CalcWidth(), CalcHeight()) {
    mBeatAlignPeriod = mBeatAlign * (float)mSamplesPerBeat + 0.5;
    if (mBeatAlignPeriod != 0) {
        float spb = (float)mSamplesPerBeat;
        float fNegB = -mBStart;
        float fNegA = -mAStart;
        int tmp = (int)(fNegA * spb) - (int)(fNegB * spb);
        mBeatAlignOffset = Offset(tmp);
    }
}

DistEntry::~DistEntry() {
    // vector<Vector3> destructor - deallocates bones
}


bool ClipDistMap::LocalMin(int col, int row) {
    int width = mDists.mWidth;
    float val = mDists(col, row);

    if (val == kHugeFloat) {
        return false;
    }

    if (mBeatAlign == 0.0f || !BeatAligned(col, row)) {
        for (int c = col - 1; c < col + 2; c++) {
            for (int r = row - 1; r < row + 2; r++) {
                if ((c != col || r != row) && c >= 0 && c < width && r >= 0 &&
                    r < mDists.mHeight) {
                    float neighbor = mDists(c, r);
                    if (neighbor != kHugeFloat && neighbor < val)
                        return false;
                }
            }
        }
    } else {
        if (col - 1 >= 0 && row - 1 >= 0 &&
            mDists(col - 1, row - 1) < val) {
            return false;
        }
        if (col + 1 < width && row + 1 < mDists.mHeight &&
            mDists(col + 1, row + 1) < val) {
            return false;
        }
    }

    return true;
}

bool ClipDistMap::BeatAligned(int i1, int i2) {
    int l1;
    int l2 = mBeatAlignPeriod;

    if (l2 == 0) {
        l1 = 0;
    } else {
        l1 = (i1 - i2) % l2;
        if (l1 < 0) {
            l1 += l2;
        }
    }

    return l1 == mBeatAlignOffset;
}

// Find the best transition node within a beat range by searching the distance map
// for the point with minimum error. Updates the node reference with the best match found.
// Returns true if a node was found with error below maxError.
bool ClipDistMap::FindBestNode(float maxError, float startBeat, float endBeat, ClipDistMap::Node &node) {
    // Validate beat range
    if (!(startBeat < endBeat)) {
        return false;
    }

    node.err = maxError;

    int startCol = (int)((startBeat - mAStart) * mSamplesPerBeat);
    int endCol = (int)((endBeat - mAStart) * mSamplesPerBeat);
    startCol = startCol & ~(startCol >> 31);
    int maxCol = endCol;
    if (mDists.mWidth < endCol) {
        maxCol = mDists.mWidth;
    }
    while (startCol < maxCol) {
        float curBeat = mAStart + (float)startCol / (float)mSamplesPerBeat;
        int rowIdx = mDists.mHeight - 1;
        if (rowIdx >= 0) {
            int rowCount = rowIdx + 1;
            do {
                float currentError = node.err;
                float cellError = mDists(rowIdx, startCol);
                float newError = (currentError - cellError >= 0.0f) ? cellError : currentError;
                node.err = newError;
                bool foundBetter = newError != currentError;
                if (foundBetter) {
                    node.curBeat = curBeat;
                    node.nextBeat = mBStart + (float)rowIdx / (float)mSamplesPerBeat;
                }
                rowIdx--;
                rowCount--;
            } while (rowCount != 0);
        }
        startCol++;
    }

    return node.err < maxError;
}

void ClipDistMap::FindBestNodeRecurse(
    float minDist, float searchRadius, float minGap, float startBeat, float endBeat
) {
    while (true) {
        MILO_ASSERT(minDist > 0, 0x26c);
        if (endBeat - startBeat <= searchRadius) break;

        float searchEnd = minDist + searchRadius + startBeat;
        float searchStart = endBeat - searchRadius - minDist;
        searchEnd = (endBeat - searchEnd >= 0.0f) ? endBeat : searchEnd;
        searchStart = (startBeat - searchStart >= 0.0f) ? searchStart : startBeat;

        Node node;
        if (!FindBestNode(minDist, searchStart, searchEnd, node)) break;

        // Check for duplicate curBeat in mNodes
        float curBeat = node.curBeat;
        unsigned int count = mNodes.size();
        unsigned int i = 0;
        for (; i < count; i++) {
            if (mNodes[i].curBeat == curBeat)
                goto skip;
        }
        mNodes.push_back(node);
    skip:;

        // Recurse on right half, loop on left half
        FindBestNodeRecurse(minDist, searchRadius, minGap, curBeat + minDist, endBeat);
        endBeat = curBeat - minDist;
    }
}

void ClipDistMap::FindNodes(float minErr, float maxDist, float endDist) {
    mNodes.clear();
    mLastMinErr = minErr;

    float f7 = maxDist * 0.45f;
    if (maxDist == 0) {
        f7 = sLargeFloat;
        endDist = f7;
    } else if (endDist == 0) {
        endDist = maxDist;
    }

    FindBestNodeRecurse(minErr, f7, -(f7 * 2.0f - maxDist), mAStart, mAEnd);
    std::sort(mNodes.begin(), mNodes.end(), DistMapNodeSort());
    if (!mNodes.empty() && endDist > 0) {
        if (mAEnd - mNodes.back().curBeat > endDist) {
            Node node;
            if (FindBestNode(minErr, mAEnd - endDist, mAEnd, node)) {
                mNodes.push_back(node);
                std::sort(mNodes.begin(), mNodes.end(), DistMapNodeSort());
            }
        }
    }
    for (int i = 1; i < (int)mNodes.size() - 1; i++) {
        if (mNodes[i + 1].curBeat - mNodes[i - 1].curBeat < maxDist) {
            mNodes.erase(mNodes.begin() + i);
            i--;
        }
    }
}

int ClipDistMap::CalcWidth() {
    float inv = 1.0f / (float)mSamplesPerBeat;
    float start = mClipA->StartBeat();
    float mod = Mod(start, inv);
    float f1 = start - mod;
    mAStart = f1;
    if (mAStart < mClipA->StartBeat()) {
        mAStart += inv;
    }

    float end = mClipA->EndBeat();
    float mod2 = Mod(end, inv);
    mAEnd = end - mod2;
    float next = mAEnd + inv;
    if (next <= mClipA->EndBeat()) {
        mAEnd = next;
    }

    float aStart = mAStart;
    int spb = mSamplesPerBeat;
    int width = Max(0, (int)(float)floorf(((mAEnd - aStart) * (float)spb) + 0.5f)) + 1;
    mAEnd = mAStart + (float)(width - 1) / (float)mSamplesPerBeat;
    return width;
}

int ClipDistMap::CalcHeight() {
    float start = mClipB->StartBeat();
    float inv = 1.0f / (float)mSamplesPerBeat;
    float mod = Mod(start, inv);
    float f1 = start - mod;
    mBStart = f1;
    if (mBStart < mClipB->StartBeat()) {
        mBStart += inv;
    }

    float end = mClipB->EndBeat();
    float mod2 = Mod(end, inv);
    float fVar = end - mod2;
    float next = fVar + inv;
    if (next <= mClipB->EndBeat()) {
        fVar = next;
    }

    int res = (int)(float)floor(((fVar - mBStart) * (float)mSamplesPerBeat) + 0.5f);
    return Max(0, res) + 1;
}

void ClipDistMap::Array2d::Resize(int w, int h) {
    delete this->mData;
    this->mWidth = w;
    this->mHeight = h;
    this->mData = (float *)new uint[h * w];
}

// Populate mClipB's transition graph and optionally find best nodes for given constraints.
// node1/node2 are output parameters updated with the minimum-error node from mNodes.
void ClipDistMap::SetNodes(ClipDistMap::Node *node1, ClipDistMap::Node *node2) {
    mClipA->GetTransitions().RemoveClip(mClipB);

    for (int i = 0; i < mNodes.size(); i++) {
        // Update node1 if this candidate has lower error (MIN)
        if (node1) {
            if (MinEq(node1->err, mNodes[i].err)) {
                *node1 = mNodes[i];
            }
        }

        // Update node2 if this candidate has higher error (MAX)
        if (node2) {
            if (MaxEq(node2->err, mNodes[i].err)) {
                *node2 = mNodes[i];
            }
        }

        // Add transition node to graph
        CharGraphNode graphNode;
        float nb = mNodes[i].nextBeat;
        graphNode.curBeat = mNodes[i].curBeat;
        graphNode.nextBeat = nb;
        mClipA->GetTransitions().AddNode(mClipB, graphNode);
    }
}

void ClipDistMap::FindDists(float maxFacing, DataArray *arr) {
    CharBoneDir *rsrcA = mClipA->GetResource();
    CharUtlBoneSaver saver(rsrcA);
    CharBonesMeshes meshes;
    meshes.SetName("tmp_bones", rsrcA);
    rsrcA->StuffBones(meshes, mClipA->GetContext());
    std::vector<RndTransformable *> transes;
    for (ObjDirItr<RndTransformable> it(rsrcA, true); it != nullptr; ++it) {
        if (strnicmp(it->Name(), "bone_", 5) == 0) {
            transes.push_back(it);
        }
    }
    mClipA->GetChannel("bone_facing.rotz");

    DataNode &dataVarABeat = DataVariable("a_beat");
    float varABeat = dataVarABeat.Float();
    DataNode &dataVarBBeat = DataVariable("b_beat");
    float varBBeat = dataVarBBeat.Float();
    DataNode &dataVarAStart = DataVariable("a_start");
    float varAStart = dataVarAStart.Float();
    DataNode &dataVarAEnd = DataVariable("a_end");
    float varAEnd = dataVarAEnd.Float();
    DataNode &dataVarBStart = DataVariable("b_start");
    float varBStart = dataVarBStart.Float();
    DataNode &dataVarBEnd = DataVariable("b_end");
    float varBEnd = dataVarBEnd.Float();
    DataNode &dataVarAMiddle = DataVariable("a_middle");
    float varAMiddle = dataVarAMiddle.Float();
    DataNode &dataVarBMiddle = DataVariable("b_middle");
    float varBMiddle = dataVarBMiddle.Float();
    DataNode &dataVarDelta = DataVariable("delta");
    float varDelta = dataVarDelta.Float();

    std::vector<DistEntry> distEntries;
    distEntries.resize(mDists.mHeight);
    std::vector<float> floatVec;
    float interpA = Interp(mClipA->StartBeat(), mClipA->EndBeat(), 0.5f);
    float interpB = Interp(mClipB->StartBeat(), mClipB->EndBeat(), 0.5f);
    mWorstErr = 0;

    int _width = mDists.mWidth;
    for (int i = 0; i < _width; i++) {
        float beatA = (float)i / (float)mSamplesPerBeat + mAStart;
        DistEntry newDistEntry;
        for (int j = 0; j < mDists.mHeight; j++) {
            mDists(i, j) = kHugeFloat;
            float beatB = (float)j / (float)mSamplesPerBeat + mBStart;
            if (mBeatAlign == 0.0f || BeatAligned(i, j)) {
                if (arr) {
                    dataVarABeat = beatA;
                    dataVarBBeat = beatB;
                    dataVarAStart = beatA - mClipA->StartBeat();
                    dataVarAEnd = mClipA->EndBeat() - beatA;
                    dataVarBStart = beatB - mClipB->StartBeat();
                    dataVarBEnd = mClipB->EndBeat() - beatB;
                    dataVarAMiddle = beatA - interpA;
                    dataVarBMiddle = beatB - interpB;
                    dataVarDelta = beatA - beatB;
                    if (arr->Evaluate(1).Int() == 0)
                        continue;
                }

                DistEntry &curDistEntry = distEntries[j];
                GenerateDistEntry(meshes, curDistEntry, (float)j / (float)mSamplesPerBeat + mBStart, mClipB, transes);
                GenerateDistEntry(meshes, newDistEntry, (float)i / (float)mSamplesPerBeat + mAStart, mClipA, transes);

                if (maxFacing > 0.0f) {
                    float *curFacing = curDistEntry.facing;
                    float *newFacing = newDistEntry.facing;
                    float facing = newFacing[0];
                    float weight = 0.33333334f;
                    int k = 3;
                    do {
                        float angleDiff1 = LimitAng(curFacing[1] - curFacing[0]);
                        float angleDiff2 = LimitAng(newFacing[1] - newFacing[0]);
                        curFacing++;
                        facing += (1.0f - weight) * angleDiff1 + weight * angleDiff2;
                        newFacing++;
                        weight += 0.33333334f;
                        k--;
                    } while (k != 0);
                    facing = LimitAng(facing - curDistEntry.facing[3]);
                    if (fabsf(facing) > maxFacing) {
                        mDists(i, j) = kHugeFloat;
                        continue;
                    }
                }

                if (floatVec.empty()) {
                    FindWeights(transes, floatVec, mWeightData);
                }
                float dist = 0;
                for (int k = 0; k < newDistEntry.bones.size(); k++) {
                    float curFloat = floatVec[k % floatVec.size()];
                    const Vector3 &curBone = curDistEntry.bones[k];
                    float dz = newDistEntry.bones[k].z - curBone.z;
                    float dy = newDistEntry.bones[k].y - curBone.y;
                    float dx = newDistEntry.bones[k].x - curBone.x;
                    dist += (dx * dx + dy * dy + dz * dz) * curFloat;
                }
                float err = std::sqrt(dist / (float)newDistEntry.bones.size());
                MaxEq(mWorstErr, err);
                mDists(i, j) = err;
            }
        }
    }

    dataVarABeat = varABeat;
    dataVarBBeat = varBBeat;
    dataVarAStart = varAStart;
    dataVarAEnd = varAEnd;
    dataVarBStart = varBStart;
    dataVarBEnd = varBEnd;
    dataVarAMiddle = varAMiddle;
    dataVarBMiddle = varBMiddle;
    dataVarDelta = varDelta;
}

void ClipDistMap::Draw(float x, float y, CharDriver *driver) {
    Hmx::Rect rect;

    // Draw left border
    rect.x = x - 1.0f;
    rect.y = y - 1.0f;
    rect.w = 1.0f;
    rect.h = ((float)mDists.mHeight + 1.0f) * 2.0f;
    Hmx::Color borderColor1(1.0f, 1.0f, 1.0f, 1.0f);
    TheRnd.DrawRect(rect, borderColor1, nullptr, nullptr, nullptr);

    // Draw right border
    Hmx::Color borderColor2(1.0f, 1.0f, 1.0f, 1.0f);
    rect.x = (float)(mDists.mWidth * 2) + x;
    TheRnd.DrawRect(rect, borderColor2, nullptr, nullptr, nullptr);

    // Draw top border
    Hmx::Color borderColor3(1.0f, 1.0f, 1.0f, 1.0f);
    rect.x = x - 1.0f;
    rect.h = 1.0f;
    rect.w = ((float)mDists.mWidth + 1.0f) * 2.0f;
    TheRnd.DrawRect(rect, borderColor3, nullptr, nullptr, nullptr);

    // Draw bottom border
    Hmx::Color borderColor4(1.0f, 1.0f, 1.0f, 1.0f);
    rect.y = (float)(mDists.mHeight * 2) + y;
    TheRnd.DrawRect(rect, borderColor4, nullptr, nullptr, nullptr);

    // Draw vertical grid lines at integer beats for clip A
    Hmx::Color gridColor1(0.0f, 0.0f, 0.0f, 1.0f);
    float beat = (float)ceil((double)mAStart);
    for (; (float)beat < (float)((float)mDists.mWidth / (float)mSamplesPerBeat + mAStart); beat = (float)((float)beat + 1.0f)) {
        rect.x = (beat - mAStart) * (float)mSamplesPerBeat * 2.0f + x;
        rect.y = y;
        rect.w = 1.0f;
        rect.h = (float)(mDists.mHeight * 2);
        TheRnd.DrawRect(rect, gridColor1, nullptr, nullptr, nullptr);
    }

    // Draw horizontal grid lines at integer beats for clip B
    Hmx::Color gridColor2(0.0f, 0.0f, 0.0f, 1.0f);
    beat = (float)ceil((double)mBStart);
    for (; (float)beat < (float)((float)mDists.mHeight / (float)mSamplesPerBeat + mBStart); beat = (float)((float)beat + 1.0f)) {
        rect.x = x;
        rect.y = ((beat - mBStart) * (float)mSamplesPerBeat - (float)(mDists.mHeight - 1)) * 2.0f + y;
        rect.w = (float)(mDists.mWidth * 2);
        rect.h = 1.0f;
        TheRnd.DrawRect(rect, gridColor2, nullptr, nullptr, nullptr);
    }

    // Draw distance map cells
    Hmx::Color cellColor;
    Hmx::Rect cellRect;
    cellRect.w = 2.0f;
    cellRect.h = 2.0f;
    for (int col = 0; col < mDists.mWidth; col++) {
        cellRect.x = (float)(col * 2) + x;
        for (int row = 0; row < mDists.mHeight; row++) {
            float err = mDists(col, row);
            if (err != kHugeFloat) {
                float c = (mWorstErr - err) / mWorstErr;
                cellColor.red = c;
                cellColor.green = c;
                cellColor.blue = c;
                cellColor.alpha = 1.0f;
                cellRect.y = (float)((mDists.mHeight - 1 - row) * 2) + y;
                if (err > mLastMinErr) {
                    cellColor.red = (c + 1.0f) * 0.5f;
                    cellColor.green = 0.0f;
                    cellColor.blue = 0.0f;
                }
                TheRnd.DrawRect(cellRect, cellColor, nullptr, nullptr, nullptr);
            }
        }
    }

    // Draw local minima
    for (int col = 0; col < mDists.mWidth; col++) {
        cellRect.x = (float)(col * 2) + x;
        for (int row = 0; row < mDists.mHeight; row++) {
            cellRect.y = (float)((mDists.mHeight - 1 - row) * 2) + y;
            if (LocalMin(col, row)) {
                Hmx::Color minColor(1.0f, 1.0f, 0.0f, 1.0f);
                TheRnd.DrawRect(cellRect, minColor, nullptr, nullptr, nullptr);
            }
        }
    }

    // Draw transition nodes
    for (unsigned int i = 0; i < mNodes.size(); i++) {
        Hmx::Color nodeColor(1.0f, 0.0f, 0.0f, 1.0f);
        DrawDot(x + 1.0f, y - 1.0f, mNodes[i].curBeat, mNodes[i].nextBeat, nodeColor);
    }

    // Draw current playback position if driver is provided
    if (driver && driver->First()) {
        float curBeatA = mClipA->StartBeat();
        CharClipDriver *cd = driver->First();
        do {
            if (cd->GetClip() == mClipA) {
                curBeatA = cd->mBeat;
            }
            cd = cd->Next();
        } while (cd != nullptr);

        float curBeatB = mClipB->StartBeat();
        cd = driver->First();
        do {
            if (cd->GetClip() == mClipB) {
                curBeatB = cd->mBeat;
                break;
            }
            cd = cd->Next();
        } while (cd != nullptr);

        if (mClipA == mClipB && curBeatA == curBeatB) {
            curBeatB = mBStart;
        }
        Hmx::Color driverColor(0.0f, 1.0f, 1.0f, 1.0f);
        DrawDot(x, y, curBeatA, curBeatB, driverColor);
    }
}

void ClipDistMap::DrawDot(float x, float y, float f3, float f4, Hmx::Color const &color) {
    Hmx::Rect rect;
    float scale = (float)mSamplesPerBeat;
    rect.w = 2.0f;
    rect.h = 2.0f;
    rect.x = (f3 - mAStart) * scale * 2.0f + (x - 1.0f);
    float heightOffset = (float)(mDists.mHeight - 1);
    float inner = heightOffset - (f4 - mBStart) * scale;
    rect.y = inner * 2.0f + y + 1.0f;
    TheRnd.DrawRect(rect, color, nullptr, nullptr, nullptr);
}
