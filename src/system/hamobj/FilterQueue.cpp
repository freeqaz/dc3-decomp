#include "hamobj/FilterQueue.h"
#include "hamobj/DetectFrame.h"
#include "hamobj/HamMove.h"
#include "os/Debug.h"
#include "utl/Loader.h"

FilterQueue::FilterQueue() : mJobFinished(0), mLastPollMs(0) {}

FilterQueue::~FilterQueue() {}

bool FilterQueue::GetResults(float &outValue, DetectFrame **frames, float unused) {
    mJobFinished = false;
    std::vector<FilterInputFrame> &qframes = mQueuedJob.frames;
    std::vector<FilterOutputFrame> &oframes = mOutput.frames;
    // Empty branch structure matches original control flow
    if (!qframes.empty()) {
        // Queued frames exist; no action needed in this branch
    } else {
        // No queued frames; clear output frames
        oframes.clear();
    }
    outValue = mQueuedJob.unk0;
    MILO_ASSERT(qframes.size() == oframes.size(), 0x42);
    frames[0] = nullptr;
    frames[1] = nullptr;
    for (int frameIdx = 0; frameIdx < qframes.size(); frameIdx++) {
        DetectFrame *pFrame = qframes[frameIdx].unkc;
        pFrame->AddError(oframes[frameIdx].unk4, qframes[frameIdx].unk4);
        // Access field at offset +8 in DetectFrame
        float* pFrameData = (float*)((char*)pFrame + 8);
        // Check if output value exceeds field data and input error exceeds threshold
        if ((mQueuedJob.unk0 > *pFrameData) && (qframes[frameIdx].unk4 > unused)) {
            frames[qframes[frameIdx].unk0] = pFrame;
        }
    }
    qframes.clear();
    oframes.clear();
    return true;
}

void FilterQueue::EnqueueNewJob(float outValue, float duration, MoveMode mode) {
    std::vector<FilterInputFrame> &qframes = mQueuedJob.frames;
    if (!qframes.empty()) {
        MILO_NOTIFY("Queuing new job, but there are already queued frames");
        qframes.clear();
    }
    mQueuedJob.unk0 = outValue;
    mQueuedJob.unk4 = mode;
    mQueuedJob.unk8 = duration;
}

void FilterQueue::EnqueueFrame(
    int frameNumber, float f2, float f3, DetectFrame *df, const FilterVersion *fv
) {
    FilterInputFrame frame;
    frame.unk0 = frameNumber;
    frame.unk4 = f2;
    frame.unk8 = f3;
    frame.unkc = df;
    frame.unk10 = fv;
    mQueuedJob.frames.push_back(frame);
}

bool FilterQueue::IsJobFinished() const { return mJobFinished; }
float FilterQueue::LastPollMs() const { return mLastPollMs; }
bool FilterQueue::HasJob() const { return !mOutput.frames.empty(); }
void FilterQueue::CancelJob() { mQueuedJob.frames.clear(); }

void FilterQueue::StartJob() {
    if (!mOutput.frames.empty()) {
        if (!TheLoadMgr.EditMode()) {
            MILO_NOTIFY("Starting new job, but there are unprocessed output frames");
        }
        mOutput.frames.clear();
    }
    mOutput.unk0 = mQueuedJob.unk8;
    mJobFinished = false;
    mOutput.unk4 = mQueuedJob.unk4;
    int numQFrames = mQueuedJob.frames.size();
    mOutput.frames.resize(numQFrames);
    for (int frameIdx = 0; frameIdx < numQFrames; frameIdx++) {
        mOutput.frames[frameIdx].unk0 = mQueuedJob.frames[frameIdx].unk0;
    }
}
