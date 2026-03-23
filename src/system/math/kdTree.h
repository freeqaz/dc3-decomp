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
            SetTriList(0);
            mFlags = 0x8000;
            mData.real = 0;
            mData.index = 0;
        }
        ~kdTreeNode() {
            if (mFlags & 0x8000 && GetTriList()) {
                delete[] GetTriList();
                SetTriList(nullptr);
            }
        }

        // On PPC (ILP32), pointer/float/bitfield all fit in 4 bytes and share
        // a single union — the float's bottom 2 mantissa bits are repurposed as
        // the axis index. On LP64, the pointer is 8 bytes so it must be stored
        // separately from the 4-byte float+bitfield pack.
#ifdef HX_NATIVE
        kdTriList *mTriList; // LP64: separate 8-byte pointer
        union {
            float real;
            struct {
                unsigned int unused : 30;
                unsigned int index : 2;
            };
        } mData;
        kdTriList *GetTriList() const { return mTriList; }
        void SetTriList(kdTriList *p) { mTriList = p; }
#else
        union {
            kdTriList *triList;
            float real;
            // bitmask here? the bottom 2 bits are its own thing
            struct {
                unsigned int unused : 30;
                unsigned int index : 2;
            };
        } mData; // 0x0
        kdTriList *GetTriList() const { return mData.triList; }
        void SetTriList(kdTriList *p) { mData.triList = p; }
#endif
        short mFlags;

        bool GetIsLeaf() const { return mFlags & 0x8000; }

        float EvaluateSplit(
            const Box &box,
            const std::list<Triangle *> &triangles,
            unsigned char idx,
            float threshold
        ) const {
            unsigned int axis = (unsigned int)(unsigned char)idx;
            if (threshold > box.mMax[axis] || threshold < box.mMin[axis]) {
                return FLT_MAX;
            }

            // Split box at threshold on given axis
            Box leftBox(box.mMin, box.mMax);
            Box rightBox(box.mMin, box.mMax);
            leftBox.mMax[axis] = threshold;
            rightBox.mMin[axis] = threshold;

            float totalArea = box.SurfaceArea();
            float invTotalArea = 1.0f / totalArea;
            float leftAreaFrac = (float)(leftBox.SurfaceArea() * invTotalArea);
            float rightAreaFrac = (float)(rightBox.SurfaceArea() * invTotalArea);

            float leftCount = 0.0f;
            float rightCount = 0.0f;
            for (auto it = triangles.begin(); it != triangles.end(); ++it) {
                Triangle *tri = *it;
                if (leftBox.Contains(*tri)) {
                    leftCount = leftCount + 1.0f;
                } else if (rightBox.Contains(*tri)) {
                    rightCount = rightCount + 1.0f;
                } else {
                    if (::Intersect(*tri, leftBox)) {
                        leftCount = leftCount + 0.5f;
                    }
                    if (::Intersect(*tri, rightBox)) {
                        rightCount = rightCount + 0.5f;
                    }
                }
            }

            return (float)(rightCount * rightAreaFrac) + leftCount * leftAreaFrac + 0.3f;
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

            unsigned int numContains = 0;
            mData.real = idxDiff / 2.0f + box.mMin[mData.index];
            mData.index = 3;

            double fsum = 0.0;
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
        SetTriList(nullptr);
    } else {
        unsigned int uCount = items.size();
        SetTriList(kdTriList::Allocate(uCount));
        kdTriList *pCurr = GetTriList();
        while (!items.empty()) {
            MILO_ASSERT(pCurr->mIndex != -1, 0x1AE);
            pCurr->mIndex = reinterpret_cast<int>(items.front());
            items.pop_front();
            ++pCurr;
        }
    }
}
