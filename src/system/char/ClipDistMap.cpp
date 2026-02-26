#include "char/ClipDistMap.h"
#include "char/CharClip.h"
#include "math/Utl.h"
#include "rndobj/Rnd.h"
#include <cmath>

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

ClipDistMap::ClipDistMap(
    CharClip *clip1, CharClip *clip2, float f1, float f2, int i, const DataArray *a
)
    : mBeatAlign(f1), mBlendWidth(f2), mClipA(clip1), mClipB(clip2), mWeightData(a),
      mSamplesPerBeat(8), mLastMinErr(kHugeFloat), mBeatAlignOffset(0), mNumSamples(i) {
    mDists.Resize(CalcWidth(), CalcHeight());

    mBeatAlignPeriod = (int)((double)(mBeatAlign * mSamplesPerBeat) + 0.5);

    if (mBeatAlignPeriod != 0) {
        int diff = (int)(mBStart * mSamplesPerBeat) - (int)(mAStart * mSamplesPerBeat);
        mBeatAlignOffset = diff - (diff / mBeatAlignPeriod) * mBeatAlignPeriod;
        if (mBeatAlignOffset < 0)
            mBeatAlignOffset += mBeatAlignPeriod;
    }
}

ClipDistMap::~ClipDistMap() {
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

    float clipAStart = mAStart;
    int endCol = (int)((endBeat - mAStart) * mSamplesPerBeat);
    int startCol = (int)((startBeat - mAStart) * mSamplesPerBeat);

    // Clamp startCol to non-negative (unsigned masking pattern for codegen)
    startCol = 0xffffffffU - ((int)startCol >> 0x1f) & startCol;
    int maxCol = mDists.mWidth;

    if (mDists.mWidth >= endCol) {
        maxCol = endCol;
    }

    // Search columns in beat range
    while ((int)startCol < maxCol) {
        int samplesPerBeat = mAStart;
        int rowIdx = mDists.mHeight - 1;

        // Search all rows in this column (top to bottom)
        if (rowIdx >= 0) {
            int rowCount = rowIdx + 1;
            do {
                float currentError = node.err;
                u8 foundBetter = 1;
                // Access distance map: mData[(row * width) + col]
                float cellError = *(float *)((mDists.mWidth * rowIdx + startCol) * 4 + mDists.mData);
                float newError = (currentError - cellError >= 0.0f) ? cellError : currentError;

                node.err = newError;
                if (newError == currentError) {
                    foundBetter = 0;
                }

                // Update node position if we found a better match
                if (foundBetter != 0) {
                    node.curBeat = (float)startCol / (float)samplesPerBeat + clipAStart;
                    node.nextBeat = (float)rowIdx / (float)mAStart + mBStart;
                }

                rowIdx--;
                rowCount--;
            } while (rowCount != 0);
        }
        startCol++;
    }

    return node.err < maxError;
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

    // searchRadius is 45% of maxDist to create overlap regions for better transitions
    float searchRadius = maxDist * 0.45f;
    if (maxDist == 0.0f) {
        searchRadius = kHugeFloat;
        endDist = searchRadius;
    } else if (endDist == 0.0f) {
        endDist = maxDist;
    }

    // Recursively find all candidate nodes within the clip range
    FindBestNodeRecurse(maxError, searchRadius, maxDist - searchRadius * 2.0f, mAStart, mAEnd);

    // Sort nodes by position (curBeat field)
    std::sort(mNodes.begin(), mNodes.end(), DistMapNodeSort());

    // Ensure we have a node near the end of the clip if needed
    if (!mNodes.empty() && endDist > 0.0f) {
        float lastNodeDist = mAEnd - mNodes.back().curBeat;
        if (lastNodeDist > endDist) {
            ClipDistMap::Node node;
            if (FindBestNode(maxError, mAEnd - endDist, mAEnd, node)) {
                mNodes.push_back(node);
                std::sort(mNodes.begin(), mNodes.end(), DistMapNodeSort());
            }
        }
    }

    // Filter out nodes that are too close together
    // Maintains minimum spacing of maxDist between nodes
    int limit = mNodes.size() - 1;
    int i = 1;
    if (limit > 1) {
        for (; i < limit;) {
            float dist = mNodes[i + 1].curBeat - mNodes[i].curBeat;
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
    float start = mClipA->StartBeat();
    float inv = 1.0f / (float)mSamplesPerBeat;
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

    int res = (int)floor(((mAEnd - mAStart) * (float)mSamplesPerBeat) + 0.5f);
    return Max(0, res) + 1;
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

    int res = (int)floor(((fVar - mBStart) * (float)mSamplesPerBeat) + 0.5f);
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
    mClipB->GetTransitions().RemoveClip(mClipB);

    for (int i = 0; i < mNodes.size(); i++) {
        // Update node1 if this candidate has lower error
        // NOTE: Verbose pattern preserved for codegen - modern Min() would break PPC match
        if (node1) {
            float candidateErr = mNodes[i].err;
            float currentBest = node1->err;
            bool changed = 1;
            float newBest = (currentBest - candidateErr >= 0.0f) ? candidateErr : currentBest;
            node1->err = newBest;
            if (currentBest == newBest) {
                changed = 0;
            }
            if (changed) {
                *node1 = mNodes[i];
            }
        }

        // Update node2 if this candidate has lower error (same logic as node1)
        if (node2) {
            float candidateErr = mNodes[i].err;
            float currentBest = node2->err;
            bool changed = 1;
            float newBest = (currentBest - candidateErr >= 0.0f) ? candidateErr : currentBest;
            node2->err = newBest;
            if (currentBest == newBest) {
                changed = 0;
            }
            if (changed) {
                *node2 = mNodes[i];
            }
        }

        // Add transition node to graph regardless of node1/node2
        CharGraphNode graphNode;
        graphNode.nextBeat = mNodes[i].nextBeat;
        graphNode.curBeat = mNodes[i].curBeat;
        mClipB->GetTransitions().AddNode(mClipB, graphNode);
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