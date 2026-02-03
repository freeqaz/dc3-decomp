#pragma once
#include "CharCameraInput.h"
#include "gesture/Skeleton.h"
#include "gesture/SkeletonHistory.h"
#include "hamobj/HamCharacter.h"
#include "math/Mtx.h"

class MocapSkeletonIterator : public SkeletonHistoryArchive, public SkeletonHistory {
public:
    MocapSkeletonIterator(int, int);
    ~MocapSkeletonIterator();
    virtual bool PrevSkeleton(const Skeleton &, int, ArchiveSkeleton &, int &) const;

    operator bool();
    void operator++();

    int Unk24b8() const { return unk24b8; }

private:
    void Update();

    HamCharacter *mDancer; // 0x4c
    CharCameraInput mInput; // 0x50
    int unk24b0; // 0x24b0
    int unk24b4; // 0x24b4
    int unk24b8;
    float unk24bc;
    int unk24c0;
    Skeleton unk24c4; // 0x24c4
    float unk2f98; // 0x2f98
    Transform unk2f9c; // 0x2f9c
};
