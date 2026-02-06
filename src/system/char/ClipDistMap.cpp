#include "char/ClipDistMap.h"
#include "char/CharClip.h"
#include "math/Utl.h"
#include "rndobj/Rnd.h"
#include <cmath>

struct DistMapNodeSort {
    bool operator()(const ClipDistMap::Node &n1, const ClipDistMap::Node &n2) const {
        return n1.unk0 < n2.unk0;
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

ClipDistMap::ClipDistMap(
    CharClip *clip1, CharClip *clip2, float f1, float f2, int i, const DataArray *a
)
    : mClipA(clip1), mClipB(clip2), mWeightData(a), mSamplesPerBeat(8),
      mLastMinErr(kHugeFloat), mBeatAlign(f1), mBeatAlignOffset(0), mBlendWidth(f2),
      mNumSamples(i) {
    int h = CalcHeight();
    int w = CalcWidth();
    mDists.Resize(w, h);

    mBeatAlignPeriod = (int)(mBeatAlign * mSamplesPerBeat + 0.5f);

    int temp;
    if (mBeatAlignPeriod != 0) {
        temp = (int)(mAStart * mSamplesPerBeat) - (int)(mBStart * mSamplesPerBeat);
        mBeatAlignOffset = temp - (temp / mBeatAlignPeriod) * mBeatAlignPeriod;

        if (mBeatAlignOffset < 0) {
            mBeatAlignOffset += mBeatAlignPeriod;
        }
    }
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

bool ClipDistMap::FindBestNode(float f1, float f2, float f3, ClipDistMap::Node &node) {
    if (!(f2 < f3)) {
        return false;
    }

    node.unk8 = f1;

    int iVar1 = (int)((f3 - mAStart) * mSamplesPerBeat);
    int uVar6 = (int)((f2 - mAStart) * mSamplesPerBeat);
    float fVar2 = mAStart;

    // Unsigned masking
    uVar6 = 0xffffffffU - ((int)uVar6 >> 0x1f) & uVar6;
    int iVar8 = mDists.mWidth;

    if (iVar1 <= mDists.mWidth) {
        iVar8 = iVar1;
    }

    for (; (int)uVar6 < iVar8; uVar6 = uVar6 + 1) {
        iVar1 = mAStart;
        int uVar5 = mDists.mHeight;
        int lVar7 = uVar5 - 1;

        if (-1 < lVar7) {
            do {
                float fVar3 = node.unk8;
                float fVar4 = *(float *)((mDists.mWidth * (int)lVar7 + uVar6) * 4 + mDists.mData);

                if (fVar3 - fVar4 < 0.0) {
                    fVar4 = fVar3;
                }

                node.unk8 = fVar4;

                if (fVar4 != fVar3) {
                    node.unk0 = (float)(int)uVar6 / (float)iVar1 + fVar2;
                    node.unk4 = (float)(int)lVar7 / (float)mAStart + mBStart;
                }

                lVar7 = lVar7 - 1;
                uVar5 = uVar5 - 1;
            } while (uVar5 != 0);
        }
    }

    return node.unk8 < f1;
}

// Find transition nodes between clips based on error threshold and distance constraints.
// Nodes represent points where animation transitions can occur.
// Parameters:
//   maxError: Maximum acceptable error threshold for a valid node
//   maxDist: Maximum distance between adjacent nodes
//   endDist: Minimum distance from clip end where final node can be placed
void ClipDistMap::FindNodes(float maxError, float maxDist, float endDist) {
    mNodes.clear();
    mLastMinErr = maxError;

    // Calculate search bounds for recursive node finding
    // The 0.45 factor creates asymmetric search bounds around potential nodes
    float halfMaxDist = maxDist * 0.45f;
    if (maxDist == 0.0f) {
        // No distance constraints - allow nodes anywhere
        endDist = halfMaxDist = kHugeFloat;
    } else if (endDist == 0.0f) {
        // Use half-distance as default end constraint
        endDist = halfMaxDist;
    }

    // Recursively find all candidate nodes within the clip range
    FindBestNodeRecurse(maxError, halfMaxDist, maxDist - halfMaxDist * 2.0f, mAStart, mAEnd);

    // Sort nodes by position (unk0 field)
    std::sort(mNodes.begin(), mNodes.end(), DistMapNodeSort());

    // Ensure we have a node near the end of the clip if needed
    if (!mNodes.empty() && endDist > 0.0f) {
        float lastNodeDist = mAEnd - mNodes.back().unk0;
        if (lastNodeDist > endDist) {
            ClipDistMap::Node node;
            if (FindBestNode(maxError, mAEnd - endDist, mAEnd, node)) {
                mNodes.push_back(node);
                std::sort(mNodes.begin(), mNodes.end(), DistMapNodeSort());
            }
        }
    }

    // Remove nodes that are too close together (violate maxDist constraint)
    int limit = mNodes.size() - 1;
    if (limit > 1) {
        for (int i = 1; i < limit;) {
            float dist = mNodes[i + 1].unk0 - mNodes[i].unk0;
            if (dist < maxDist) {
                mNodes.erase(mNodes.begin() + (i + 1));
                i--;
            }
            i++;
            limit = mNodes.size() - 1;
        }
    }
}

int ClipDistMap::CalcWidth() {
    float clipAStartBeat = mClipA->StartBeat();
    float samplesDiv = (1.0 / mSamplesPerBeat);
    float clipASamplesMod = Mod(clipAStartBeat, 1.0 / mSamplesPerBeat);
    float f1 = clipAStartBeat - clipASamplesMod;
    mAStart = f1;

    if (f1 < mClipA->StartBeat()) {
        mAStart = f1 + samplesDiv;
    }

    f1 = mClipA->EndBeat();
    clipASamplesMod = Mod(f1, samplesDiv);
    mAEnd = f1 - clipASamplesMod;
    clipASamplesMod = (f1 - clipASamplesMod) + samplesDiv;

    if (clipASamplesMod <= mClipA->EndBeat()) {
        mAEnd = clipASamplesMod;
    }

    f1 = floor(mAEnd - mAStart * mSamplesPerBeat + 0.5);

    uint val = f1;

    return (((val != 0) - (val >> 0x1f) & val)) + 1;
}

int ClipDistMap::CalcHeight() {
    float clipBStartBeat = mClipB->StartBeat();
    float samplesDiv = 1.0f / mSamplesPerBeat;
    float clipBSamplesMod = Mod(clipBStartBeat, samplesDiv);
    float f1 = clipBStartBeat - clipBSamplesMod;
    mBStart = f1;

    if (mBStart < mClipB->StartBeat()) {
        mBStart += samplesDiv;
    }
    // fVar5 = mClipB->EndBeat();
    f1 = mClipB->EndBeat();
    clipBSamplesMod = Mod(mClipB->EndBeat(), samplesDiv);
    clipBStartBeat = (f1 - clipBSamplesMod) + samplesDiv;
    f1 -= clipBSamplesMod;

    if (clipBStartBeat <= mClipB->EndBeat()) {
        f1 = clipBStartBeat;
    }

    f1 = floor(((f1 - mBStart) * (float)mSamplesPerBeat) + 0.5f);
    uint val = f1;

    return (((val != 0) - (val >> 0x1f) & val)) + 1;
}

void ClipDistMap::Array2d::Resize(int w, int h) {
    delete this->mData;
    this->mWidth = w;
    this->mHeight = h;
    this->mData = (float *)new uint[h * w];
}

void ClipDistMap::SetNodes(ClipDistMap::Node *node1, ClipDistMap::Node *node2) {
    mClipB->GetTransitions().RemoveClip(mClipB);
    for (int i = 0; i < mNodes.size(); i++) {
        if (node1) {
            float currentBest = node1->unk8;
            float candidateErr = mNodes[i].unk8;
            // Use fsel pattern: if (currentBest - candidateErr >= 0.0f) use candidateErr, else use currentBest
            bool changed = 1;
            float newBest = (currentBest - candidateErr >= 0.0f) ? candidateErr : currentBest;
            node1->unk8 = newBest;
            if (newBest == currentBest) {
                changed = 0;
            }
            if (changed) {
                *node1 = mNodes[i];
            }
        }
        if (node2) {
            float currentBest = node2->unk8;
            float candidateErr = mNodes[i].unk8;
            // Use fsel pattern: if (currentBest - candidateErr >= 0.0f) use candidateErr, else use currentBest
            bool changed = 1;
            float newBest = (currentBest - candidateErr >= 0.0f) ? candidateErr : currentBest;
            node2->unk8 = newBest;
            if (newBest == currentBest) {
                changed = 0;
            }
            if (changed) {
                *node2 = mNodes[i];
            }
        }
        CharGraphNode graphNode;
        graphNode.nextBeat = mNodes[i].unk4;
        graphNode.curBeat = mNodes[i].unk0;
        mClipB->GetTransitions().AddNode(mClipB, graphNode);
    }
}

void ClipDistMap::DrawDot(float x, float y, float f3, float f4, Hmx::Color const &color) {
    Hmx::Rect rect;
    rect.w = 2.0;
    rect.h = 2.0;
    float scale = (float)mSamplesPerBeat;
    rect.x = (f3 - mAStart) * scale * 2.0f + (x - 1.0f);
    rect.y = ((f4 - mBStart) * scale - (float)(mDists.mHeight - 1)) * 2.0f + y + 1.0f;
    TheRnd.DrawRect(rect, color, nullptr, nullptr, nullptr);
}