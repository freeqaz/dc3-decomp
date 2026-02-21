#include "hamobj/FilterQueue.h"
#include "os/Debug.h"
#include "utl/Loader.h"

FilterQueue::FilterQueue() : mJobFinished(false), mLastPollMs(0.0f) {}

bool FilterQueue::GetResults(float &outValue, DetectFrame **frames, float unused) {
    mJobFinished = false;
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
        FilterInputFrame &frame = qframes[frameIdx];
        frame.mDetectFrame->AddError(oframes[frameIdx].mErrors, frame.mSongBeats);
        if (mQueuedJob.songSeconds > frame.mDetectFrame->Seconds() && frame.mSongBeats > unused) {
            frames[frame.mSlot] = frame.mDetectFrame;
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
    frame.mSlot = frameNumber;
    frame.mSongBeats = f2;
    frame.mSongSpeed = f3;
    frame.mDetectFrame = df;
    frame.mFilterVersion = fv;
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
    mOutput.songSpeed = mQueuedJob.songSpeed;
    mJobFinished = false;
    mOutput.moveMode = mQueuedJob.moveMode;
    int frameCount = mQueuedJob.frames.size();
    mOutput.frames.resize(frameCount);
    for (int frameIdx = 0; frameIdx < frameCount; frameIdx++) {
        mOutput.frames[frameIdx].mInputFrame = &mQueuedJob.frames[frameIdx];
    }
}
