#pragma once
#include "math/Geo.h"
#include "math/Vec.h"
#include "os/Debug.h"
#include "utl/MemMgr.h"
#include "utl/Std.h"
#include <float.h>
#include <list>

// kdTree size: 0x2c
template <class T>
class kdTree {
public:
    enum SplitPlaneType {
        // mean = 0, mean = 1
        // SAH = 2
    };

    class kdTriList {
    public:
        MEM_ARRAY_OVERLOAD(kdTriList, 0xC6);

        kdTriList() : mIndex(0) {}

        static kdTriList *Allocate(unsigned int num) {
            kdTriList *list = new kdTriList[num + 1];
            list[num].mIndex = -1;
            return list;
        }

        int mIndex; // Triangle*?
    };

    class kdTreeNode {
    public:
        struct Stack {
            kdTreeNode *node;
            float tNear;
            float tFar;
        };

        kdTreeNode() {
            mData.triList = 0;
            mFlags = 0x8000;
            mData.real = 0;
            mData.index = 0;
        }
        ~kdTreeNode() {
            if (mFlags & 0x8000 && mData.triList) {
                delete[] mData.triList;
                mData.triList = nullptr;
            }
        }
        union {
            kdTriList *triList;
            float real;
            // bitmask here? the bottom 2 bits are its own thing
            struct {
                unsigned int unused : 30;
                unsigned int index : 2;
            };
        } mData; // 0x0
        short mFlags;

        bool GetIsLeaf() const { return mFlags & 0x8000; }

        float EvaluateSplit(
            const Box &box,
            const std::list<Triangle *> &triangles,
            unsigned char idx,
            float threshold
        ) const {
            if (box.mMax[idx] >= threshold && box.mMin[idx] <= threshold) {
                Box box100 = box;
                Box boxe0 = box;
            } else {
                return FLT_MAX;
            }
        }

        bool FindSplit_Mean(const Box &box, const std::list<Triangle *> &items) {
            float yDiff = box.mMax.y - box.mMin.y;
            float zDiff = box.mMax.z - box.mMin.z;
            if (box.mMax.x - box.mMin.x > yDiff) {
                mData.index = 0;
            } else {
                mData.index = 1;
            }
            if (zDiff > yDiff) {
                mData.index = 2;
            }
            unsigned int vecIdx = mData.index;
            float idxDiff = box.mMax[vecIdx] - box.mMin[vecIdx];
            long numContains = 0;
            mData.real = idxDiff / 2.0f + box.mMin[mData.index];
            mData.index = 3;
            double fsum = 0;
            if (!items.empty()) {
                FOREACH (it, items) {
                    Triangle *cur = *it;
                    Vector3 v[3];
                    v[0].Set(
                        cur->origin.x + cur->frame.x.x,
                        cur->origin.y + cur->frame.x.y,
                        cur->origin.z + cur->frame.x.z
                    );
                    v[1].Set(
                        cur->origin.x + cur->frame.y.x,
                        cur->origin.y + cur->frame.y.y,
                        cur->origin.z + cur->frame.y.z
                    );
                    v[2].Set(
                        cur->origin.x + cur->frame.z.x,
                        cur->origin.y + cur->frame.z.y,
                        cur->origin.z + cur->frame.z.z
                    );
                    for (int i = 0; i < 3; i++) {
                        if (box.Contains(v[i])) {
                            fsum += v[i][vecIdx];
                            numContains++;
                        }
                    }
                }
                if (numContains != 0) {
                    mData.real = (float)(fsum / numContains);
                    mData.index = 3;
                }
            }
            return true;
        }
        bool FindSplit_SAH(const Box &, const std::list<Triangle *> &);
        void Pack(
            SplitPlaneType s,
            const Box &inDimensions,
            std::list<Triangle *> &items,
            kdTreeNode *pBase,
            unsigned char uc
        );

        MEM_ARRAY_OVERLOAD(kdTreeNode, 0xEC);
    };

    kdTree(const Box &box) {
        mBounds.Set(box.mMin, box.mMax);
        mNodes = new kdTreeNode[0x8000];
        for (u16 i = 0; i < 0x8000; i++) {
            mNodes[i].mFlags |= i;
        }
    }
    ~kdTree() { delete[] mNodes; }

    void Add(T *item) { mItems.push_back(item); }
    void PackNodes(SplitPlaneType s, unsigned char uc) {
        mNodes->Pack(s, mBounds, mItems, mNodes, uc);
    }

    bool Intersect(const Vector3 &, const Vector3 &, float, float &) const;

private:
    std::list<T *> mItems; // 0x0 - objects?
    kdTreeNode *mNodes; // 0x8
    Box mBounds; // 0xc - bounding box of the tree?
};

template <class T>
void kdTree<T>::kdTreeNode::Pack(
    SplitPlaneType s,
    const Box &inDimensions,
    std::list<Triangle *> &items,
    kdTreeNode *pBase,
    unsigned char uc
) {
    if (uc < 0xF) {
        if (!items.empty()) {
            if (items.size() >= 10) {
                bool bFound = false;
                if (s == 0) {
                    bFound = FindSplit_Mean(inDimensions, items);
                } else if (s == 1) {
                    bFound = FindSplit_SAH(inDimensions, items);
                } else if (s == 2) {
                    bFound = FindSplit_Mean(inDimensions, items);
                } else {
                    MILO_FAIL("Invalid split plane type");
                }

                if (bFound) {
                    float fSplit = mData.real;
                    unsigned int iAxis = mData.index & 3;
                    float fMin = inDimensions.mMin[iAxis];
                    float fMax = inDimensions.mMax[iAxis];

                    if (fMin <= fSplit && fSplit <= fMax) {
                        Box minBox = inDimensions;
                        Box maxBox = inDimensions;
                        minBox.mMax[iAxis] = fSplit;
                        maxBox.mMin[iAxis] = fSplit;

                        std::list<Triangle *> leftList, rightList;
                        bool bContinue = true;
                        for (auto it = items.begin(); it != items.end(); ++it) {
                            Triangle *pTri = *it;
                            MILO_ASSERT(::Intersect(*pTri, inDimensions), 0x166);
                            bool bLeftIntersect = ::Intersect(*pTri, minBox);
                            bool bRightIntersect = ::Intersect(*pTri, maxBox);

                            if ((!bLeftIntersect) && (!bRightIntersect)) {
                                bContinue = false;
                                break;
                            }

                            if (bLeftIntersect) {
                                leftList.push_back(pTri);
                            }
                            if (bRightIntersect) {
                                rightList.push_back(pTri);
                            }
                        }

                        if (bContinue && !leftList.empty() && !rightList.empty() && (unsigned short)(mFlags & 0x7fff) <= 0x3ffe) {
                            items.clear();
                            unsigned char ucNext = uc + 1;
                            unsigned short uNodeIdx = mFlags & 0x7fff;
                            mFlags = (mFlags & 0x8000) | uNodeIdx;

                            kdTreeNode *pNode0 = pBase + uNodeIdx * 0x10 + 8;
                            kdTreeNode *pNode1 = pBase + (uNodeIdx + 1) * 0x10;

                            pNode0->Pack(s, minBox, leftList, pBase, ucNext);
                            pNode1->Pack(s, maxBox, rightList, pBase, ucNext);

                            leftList.clear();
                            rightList.clear();
                            return;
                        }
                        leftList.clear();
                        rightList.clear();
                    }
                }
            }
        }
    }

    MILO_ASSERT(GetIsLeaf(), 0x19F);
    if (items.empty()) {
        mData.triList = nullptr;
    } else {
        unsigned int uCount = items.size();
        mData.triList = kdTriList::Allocate(uCount);
        kdTriList *pCurr = mData.triList;
        while (!items.empty()) {
            MILO_ASSERT(pCurr->mIndex != -1, 0x1AE);
            pCurr->mIndex = reinterpret_cast<int>(items.front());
            items.pop_front();
            ++pCurr;
        }
    }
}
