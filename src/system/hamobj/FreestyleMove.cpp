#include "hamobj/FreestyleMove.h"
#include "gesture/BaseSkeleton.h"
#include "hamobj/DancerSkeleton.h"

FreestyleMove::FreestyleMove() : mDepthFrames(0), mNumFrames(0), unk10(0), unk14(0), mFrames(0) {}

FreestyleMove::~FreestyleMove() {
    delete[] mDepthFrames;
    delete[] mFrames;
}

void FreestyleMove::Clear() { mNumFrames = 0; }

void FreestyleMove::Free() {
    mNumFrames = 0;
    delete[] mDepthFrames;
    delete[] mFrames;
    mDepthFrames = nullptr;
    mFrames = nullptr;
}

void FreestyleMove::Init(int i1) {
    mNumFrames = 0;
    if (!mDepthFrames) {
        mDepthFrames = new DepthFrame[i1];
    }
    if (!mFrames) {
        mFrames = new FreestyleMoveFrame[i1];
    }
}

void FreestyleMove::RecordSkeletonFrame(BaseSkeleton *skeleton, int i2, float f3) {
    FreestyleMoveFrame frame;
    frame.skeleton.Init();
    frame.mBeat = f3;
    if (skeleton && skeleton->IsTracked()) {
        frame.skeleton.Set(*skeleton);
    }
    mFrames[i2] = frame;
}
