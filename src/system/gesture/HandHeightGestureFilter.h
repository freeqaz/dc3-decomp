#pragma once
#include "gesture/BaseSkeleton.h"
#include "gesture/Skeleton.h"

class HandHeightGestureFilter {
public:
    HandHeightGestureFilter(SkeletonSide);
    virtual ~HandHeightGestureFilter() {}

    void Update(const Skeleton &, int);
    void Clear();

protected:
    SkeletonSide mSide; // 0x4
    float mHeightOffset; // 0x8
    int mTrackingState; // 0xc
    float mHandHeight; // 0x10
};
