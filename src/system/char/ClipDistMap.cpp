#include "char\ClipDistMap.h"
#include "char\CharBoneDir.h"
#include "char\CharBonesMeshes.h"
#include "char\CharClip.h"
#include "char\CharUtl.h"
#include "math\Trig.h"
#include "math\Utl.h"
#include "obj\Data.h"
#include "obj\Dir.h"
#include "os\Debug.h"
#include "rndobj\Rnd.h"
#include "rndobj\Trans.h"
#include "string.h"
#include "utl\Std.h"
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
        // prevRow as a named local scoped after the column test: a named int
        // sorts below the width temp in the commutative mullw operand list
        // (key 0x10000|sid<<5 vs a temp's sid<<6), giving the image's
        // mullw width, prevRow. Declared before the column test it is
        // computed early and loses the addic. fusion.
        if (col - 1 >= 0) {
            int prevRow = row - 1;
            if (prevRow >= 0 && mDists(col - 1, prevRow) < val) {
                return false;
            }
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
    // The image clamps the low column with an unsigned sign-bit mask
    // (srwi/subi/and), which is MSVC's branchless lowering of `if (x < 0) x = 0`.
    // The `x & ~(x >> 31)` spelling lowers to srawi/andc instead.
    if (startCol < 0) {
        startCol = 0;
    }
    int maxCol = Min(endCol, mDists.mWidth);
    for (; startCol < maxCol; startCol++) {
        float curBeat = BeatA(startCol);
        for (int rowIdx = mDists.mHeight - 1; rowIdx >= 0; rowIdx--) {
            float currentError = node.err;
            float cellError = mDists(startCol, rowIdx);
            float newError = (currentError - cellError >= 0.0f) ? cellError : currentError;
            node.err = newError;
            bool foundBetter = newError != currentError;
            if (foundBetter) {
                // Lane history (50 lines moved): docs/decomp/notes/system/char/ClipDistMap.md
                node.curBeat = curBeat;
                const float &spb = (float)mSamplesPerBeat;
                node.nextBeat = mBStart + (float)rowIdx / spb;
            }
        }
    }

    return node.err < maxError;
}

// Lane history (14 lines moved): docs/decomp/notes/system/char/ClipDistMap.md
void ClipDistMap::FindBestNodeRecurse(
    float maxError, float minDist, float minGap, float startBeat, float endBeat
) {
    while (true) {
        MILO_ASSERT(minDist > 0, 0x26c);
        if (endBeat - startBeat <= minGap) break;

        float searchEnd = minDist + minGap + startBeat;
        float searchStart = endBeat - minGap - minDist;
        searchEnd = (endBeat - searchEnd >= 0.0f) ? endBeat : searchEnd;
        searchStart = (startBeat - searchStart >= 0.0f) ? searchStart : startBeat;

        Node node;
        if (!FindBestNode(maxError, searchStart, searchEnd, node)) break;

        // A node already in mNodes ENDS the search: 0x823E5F68 `beq cr6,
        // .L_823E5FA8` jumps straight to the epilogue (BUG FIX w19-x -- this
        // used to skip only the push_back and keep recursing, as RB3 does).
        float curBeat = node.curBeat;
        unsigned int count = mNodes.size();
        unsigned int i = 0;
        for (; i < count; i++) {
            if (mNodes[i].curBeat == curBeat)
                return;
        }
        mNodes.push_back(node);

        // Recurse on right half, loop on left half
        FindBestNodeRecurse(maxError, minDist, minGap, curBeat + minDist, endBeat);
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

    // Lane history (10 lines moved): docs/decomp/notes/system/char/ClipDistMap.md
    // > NEGATIVE RESULT (w13-c, 95.506): the 7 rows are the image converting
    // > mSamplesPerBeat (std/lfd/fcfid) BEFORE loading mAStart for the subtraction.
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

// Lane history (5 lines moved): docs/decomp/notes/system/char/ClipDistMap.md
void ClipDistMap::FindDists(float maxFacing, DataArray *arr) {
    CharBoneDir *rsrcA = mClipA->GetResource();
    CharUtlBoneSaver saver(rsrcA);
    CharBonesMeshes meshes;
    meshes.SetName("tmp_bones", rsrcA);
    rsrcA->StuffBones(meshes, mClipA->GetContext());
    std::vector<RndTransformable *> transes;
    for (ObjDirItr<RndTransformable> it(rsrcA, true); it != nullptr; ++it) {
        // The literal is "bone_" but the target compares only 4 chars (li r5, 4).
        if (strnicmp(it->Name(), "bone_", 4) == 0) {
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

    for (int i = 0; i < mDists.mWidth; i++) {
        float beatA = (float)i / (float)mSamplesPerBeat + mAStart;
        DistEntry newDistEntry;
        for (int j = 0; j < mDists.mHeight; j++) {
            mDists(i, j) = sLargeFloat;
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
                    // Integrate the facing curve: the NEW entry's delta is
                    // weighted (1 - w) and the CUR entry's delta w, and the new
                    // delta is the first LimitAng call. The target walks one
                    // pointer through cur and addresses new relative to it.
                    float facing = newDistEntry.facing[0];
                    float weight = 0.33333334f;
                    for (int k = 0; k < 3; k++) {
                        float newDiff = LimitAng(newDistEntry.facing[k + 1] - newDistEntry.facing[k]);
                        float curDiff = LimitAng(curDistEntry.facing[k + 1] - curDistEntry.facing[k]);
                        facing += (1.0f - weight) * newDiff + weight * curDiff;
                        weight += 0.33333334f;
                    }
                    float lastDiff = facing - curDistEntry.facing[3];
                    facing = LimitAng(lastDiff);
                    if (fabsf(facing) > maxFacing) {
                        mDists(i, j) = sLargeFloat;
                        continue;
                    }
                }

                if (floatVec.empty()) {
                    FindWeights(transes, floatVec, mWeightData);
                }
                // Lane history (7 lines moved): docs/decomp/notes/system/char/ClipDistMap.md
                float dist = 0;
                // The bone count is materialised BEFORE the loop and only the
                // division below reads it; the loop condition keeps calling
                // size(). Both halves matter: using numBones as the loop bound
                // too lets MSVC form a `bdnz` counted loop (the target uses
                // cmplw/blt), and dropping it entirely makes MSVC spill the
                // count into the inline-call home slot, which rotates the
                // 0x50/0x54 temp pair and the r5/r6/r7 assignment.
                unsigned int numBones = newDistEntry.bones.size();
                for (int k = 0; k < newDistEntry.bones.size(); k++) {
                    unsigned int numWeights = floatVec.size();
                    float curFloat = floatVec[k % numWeights];
                    const Vector3 &curBone = curDistEntry.bones[k];
                    float dz = newDistEntry.bones[k].z - curBone.z;
                    float dy = newDistEntry.bones[k].y - curBone.y;
                    float dx = newDistEntry.bones[k].x - curBone.x;
                    dist += (dx * dx + dy * dy + dz * dz) * curFloat;
                }
                float meanSq = dist / (float)numBones;
                float err = std::sqrt(meanSq);
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

// Lane history (69 lines moved): docs/decomp/notes/system/char/ClipDistMap.md
// > Mechanism: the bound reference gives the member load its own IL leaf, so the division
// > and the comparison stop being scheduled off one shared base. (w27-f)
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
    rect.x = x - 1.0f;
    rect.h = 1.0f;
    Hmx::Color borderColor3(1.0f, 1.0f, 1.0f, 1.0f);
    rect.w = ((float)mDists.mWidth + 1.0f) * 2.0f;
    TheRnd.DrawRect(rect, borderColor3, nullptr, nullptr, nullptr);

    // Draw bottom border
    Hmx::Color borderColor4(1.0f, 1.0f, 1.0f, 1.0f);
    rect.y = (float)(mDists.mHeight * 2) + y;
    TheRnd.DrawRect(rect, borderColor4, nullptr, nullptr, nullptr);

    // Tick marks along the bottom border at integer beats of clip A (the rect
    // keeps the bottom border's y and height; only x and w change).
    for (float beat = (float)ceil(BeatA(0)); beat < BeatA(mDists.mWidth); beat = beat + 1.0f) {
        Hmx::Color gridColor1(0.0f, 0.0f, 0.0f, 1.0f);
        rect.x = (beat - mAStart) * (float)mSamplesPerBeat * 2.0f + x;
        rect.w = 2.0f;
        TheRnd.DrawRect(rect, gridColor1, nullptr, nullptr, nullptr);
    }

    // Tick marks along the left border at integer beats of clip B.
    for (float beat = (float)ceil(BeatB(0)); beat < BeatB(mDists.mHeight); beat = beat + 1.0f) {
        float scaled = (float)mSamplesPerBeat * (beat - mBStart);
        rect.x = x - 1.0f;
        rect.h = 2.0f;
        rect.w = 1.0f;
        rect.y = 2.0f * ((float)(mDists.mHeight - 1) - scaled) + y;
        Hmx::Color gridColor2(0.0f, 0.0f, 0.0f, 1.0f);
        TheRnd.DrawRect(rect, gridColor2, nullptr, nullptr, nullptr);
    }

    // Lane history (18 lines moved): docs/decomp/notes/system/char/ClipDistMap.md
    // > w28-b (2026-11-02): Draw's residual is 8 of 437 rows (canonical 98.15243, fuzzy
    // > 98.14088 -- the worklist's 97.691 predates a landing).
    Hmx::Rect cellRect(0, 0, 2.0f, 2.0f);
    Hmx::Color cellColor;
    for (int col = 0; col < mDists.mWidth; col++) {
        cellRect.x = (float)(col * 2) + x;
        for (int row = 0; row < mDists.mHeight; row++) {
            float err = mDists(col, row);
            if (err != sLargeFloat) {
                const float &worstErr = mWorstErr;
                const float &c = (worstErr - err) / worstErr;
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
        // One reference, not two mNodes[i] subscripts: the image computes the
        // node address once from the begin() its loop test already loaded
        // (w15-i1: two subscripts reload begin() in the body, 96.58 -> 97.69).
        Node &node = mNodes[i];
        DrawDot(x + 1.0f, y - 1.0f, node.curBeat, node.nextBeat, nodeColor);
    }

    // Draw current playback position if driver is provided
    if (driver && driver->First()) {
        float curBeatA = mClipA->StartBeat();
        for (CharClipDriver *cd = driver->First(); cd != nullptr; cd = cd->Next()) {
            if (cd->GetClip() == mClipA) {
                curBeatA = cd->mBeat;
            }
        }

        float curBeatB = mClipB->StartBeat();
        for (CharClipDriver *cd = driver->First(); cd != nullptr; cd = cd->Next()) {
            if (cd->GetClip() == mClipB) {
                curBeatB = cd->mBeat;
                break;
            }
        }

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
