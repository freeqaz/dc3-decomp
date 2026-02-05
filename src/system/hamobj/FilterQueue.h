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
    FilterQueue();

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

    struct QueuedJob {
        float unk0; // 0x0 - song seconds minus latency seconds
        MoveMode unk4; // 0x4 - current move mode
        float unk8; // 0x8 - song speed
        std::vector<FilterInputFrame> frames; // 0xc
    };

    struct Output {
        float unk0;
        MoveMode unk4;
        std::vector<FilterOutputFrame> frames; // 0x20
    };

    QueuedJob mQueuedJob; // 0x0
    Output mOutput; // 0x18
    bool jobFinished; // 0x2c
    float lastPollMs; // 0x30
};
