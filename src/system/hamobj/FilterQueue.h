#pragma once
#include "gesture/Skeleton.h"
#include "hamobj/DetectFrame.h"
#include "hamobj/ErrorNode.h"
#include "hamobj/FilterVersion.h"
#include "hamobj/HamMove.h"
#include <vector>

// size 0x34
class FilterQueue {
public:
    // size 0x14
    class FilterInputFrame {
    public:
        int unk0;
        float unk4;
        float unk8;
        DetectFrame *unkc;
        const FilterVersion *unk10;
    };

    // size 0x214
    class FilterOutputFrame {
    public:
        int unk0;
        Vector3 unk4[kMaxNumErrorNodes]; // 0x4
    };

    class QueuedJob {
    public:
    };

    FilterQueue();
    ~FilterQueue();

    bool GetResults(float &outValue, DetectFrame **frames, float unused);
    void EnqueueNewJob(float outValue, float duration, MoveMode mode);
    void EnqueueFrame(int frameNumber, float f2, float f3, DetectFrame *df, const FilterVersion *fv);
    bool IsJobFinished() const;
    float LastPollMs() const;
    bool HasJob() const;
    void CancelJob();
    void StartJob();
    void Poll(const SkeletonUpdateData &);

private:
    float unk0;
    MoveMode unk4;
    float unk8;
    std::vector<FilterInputFrame> mQueuedFrames; // 0xc
    float unk18;
    MoveMode unk1c;
    std::vector<FilterOutputFrame> mOutputFrames; // 0x20
    bool mJobFinished; // 0x2c
    float mLastPollMs; // 0x30
};
