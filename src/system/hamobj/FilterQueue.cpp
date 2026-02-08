#include "hamobj/FilterQueue.h"
#include "hamobj/DetectFrame.h"
#include "hamobj/HamMove.h"
#include "os/Debug.h"
#include "utl/Loader.h"

FilterQueue::FilterQueue() : jobFinished(0), lastPollMs(0) {}

bool FilterQueue::GetResults(float &outValue, DetectFrame **frames, float unused) {
    jobFinished = false;
    std::vector<FilterInputFrame> &qframes = mQueuedJob.frames;
    if (qframes.empty()) {
        mOutput.frames.clear();
    }
    outValue = mQueuedJob.songSeconds;
    std::vector<FilterOutputFrame> &oframes = mOutput.frames;
    MILO_ASSERT(qframes.size() == oframes.size(), 0x42);
    frames[1] = nullptr;
    frames[0] = nullptr;
    for (int frameIdx = 0; frameIdx < qframes.size(); frameIdx++) {
        DetectFrame *pFrame = qframes[frameIdx].unkc;
        pFrame->AddError(oframes[frameIdx].unk4, qframes[frameIdx].unk4);
        if (mQueuedJob.songSeconds > pFrame->Seconds() && qframes[frameIdx].unk4 > unused) {
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
    mQueuedJob.songSeconds = outValue;
    mQueuedJob.moveMode = mode;
    mQueuedJob.songSpeed = duration;
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

bool FilterQueue::IsJobFinished() const { return jobFinished; }
float FilterQueue::LastPollMs() const { return lastPollMs; }
bool FilterQueue::HasJob() const { return !mOutput.frames.empty(); }
void FilterQueue::CancelJob() { mQueuedJob.frames.clear(); }

void FilterQueue::StartJob() {
    if (!mOutput.frames.empty()) {
        if (!TheLoadMgr.EditMode()) {
            MILO_NOTIFY("Starting new job, but there are unprocessed output frames");
        }
        mOutput.frames.clear();
    }
    mOutput.songSpeed = mQueuedJob.songSpeed;
    jobFinished = false;
    mOutput.moveMode = mQueuedJob.moveMode;
    int frameCount = mQueuedJob.frames.size();
    mOutput.frames.resize(frameCount);
    for (int frameIdx = 0; frameIdx < frameCount; frameIdx++) {
        mOutput.frames[frameIdx].unk0 = &mQueuedJob.frames[frameIdx];
    }
}
