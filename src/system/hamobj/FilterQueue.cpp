#include "hamobj/FilterQueue.h"
#include "hamobj/DetectFrame.h"
#include "hamobj/HamMove.h"
#include "os/Debug.h"
#include "utl/Loader.h"

FilterQueue::FilterQueue() : mJobFinished(0), mLastPollMs(0) {}

FilterQueue::~FilterQueue() {}

bool FilterQueue::GetResults(float &outValue, DetectFrame **frames, float unused) {
    mJobFinished = false;
    // Empty branch structure matches original control flow
    if (!mQueuedFrames.empty()) {
        // Queued frames exist; no action needed in this branch
    } else {
        // No queued frames; clear output frames
        mOutputFrames.clear();
    }
    outValue = unk0;
    MILO_ASSERT(mQueuedFrames.size() == mOutputFrames.size(), 0x42);
    frames[1] = nullptr;
    frames[0] = nullptr;
    for (int frameIdx = 0; frameIdx < (int)mQueuedFrames.size(); frameIdx++) {
        DetectFrame *pFrame = mQueuedFrames[frameIdx].unkc;
        pFrame->AddError(mOutputFrames[frameIdx].unk4, mQueuedFrames[frameIdx].unk4);
        // Access field at offset +8 in DetectFrame
        float* pFrameData = (float*)((char*)pFrame + 8);
        // Check if output value exceeds field data and input error exceeds threshold
        if ((unk0 > *pFrameData) && (mQueuedFrames[frameIdx].unk4 > unused)) {
            frames[mQueuedFrames[frameIdx].unk0] = pFrame;
        }
    }
    mQueuedFrames.clear();
    mOutputFrames.clear();
    return true;
}

void FilterQueue::EnqueueNewJob(float outValue, float duration, MoveMode mode) {
    if (!mQueuedFrames.empty()) {
        MILO_NOTIFY("Queuing new job, but there are already queued frames");
        mQueuedFrames.clear();
    }
    unk0 = outValue;
    unk4 = mode;
    unk8 = duration;
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
    mQueuedFrames.push_back(frame);
}

bool FilterQueue::IsJobFinished() const { return mJobFinished; }
float FilterQueue::LastPollMs() const { return mLastPollMs; }
bool FilterQueue::HasJob() const { return !mOutputFrames.empty(); }
void FilterQueue::CancelJob() { mQueuedFrames.clear(); }

void FilterQueue::StartJob() {
    if (!mOutputFrames.empty()) {
        if (!TheLoadMgr.EditMode()) {
            MILO_NOTIFY("Starting new job, but there are unprocessed output frames");
        }
        mOutputFrames.clear();
    }
    unk18 = unk8;
    mJobFinished = false;
    unk1c = unk4;
    int numQFrames = mQueuedFrames.size();
    mOutputFrames.resize(numQFrames);
    for (int frameIdx = 0; frameIdx < numQFrames; frameIdx++) {
        mOutputFrames[frameIdx].unk0 = mQueuedFrames[frameIdx].unk0;
    }
}
