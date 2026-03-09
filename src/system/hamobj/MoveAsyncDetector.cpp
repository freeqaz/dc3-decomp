#include "MoveDir.h"
#include "hamobj/DancerSequence.h"
#include "hamobj/Difficulty.h"
#include "hamobj/FilterVersion.h"
#include "hamobj/HamDirector.h"
#include "hamobj/HamGameData.h"
#include "hamobj/HamMove.h"
#include "hamobj/MoveDetector.h"
#include "os/Debug.h"
#include "stl/_pair.h"

MoveDetector::MoveDetector(
    const FilterVersion *fv, const HamMove *move, const DancerFrame *&dancer_frame
)
    : mMove(move), mActive(false), mLastDetectFrameIdx(-1), mDetectFrameOffset(-1) {
    MILO_ASSERT(mMove, 0x18);
    unsigned char mirrored = (unsigned char)(mMove->Mirrored());
    const std::vector<MoveFrame> &moveFrames = mMove->GetMoveFrames();
    mDancerFrames.resize(moveFrames.size());
    for (int i = 0; i < 2; i++) {
        mLastDetectFracs[i] = 0;
        mPlayerDetectFrames[i].resize(moveFrames.size());
    }
    for (int mf = 0; mf < moveFrames.size(); mf++) {
        if (dancer_frame->mMoveFrameIdx != mf) {
            const char *path = move ? PathName(move) : "NULL";
            MILO_WARN("HamMove '%s': dancer_frame->mMoveFrameIdx != mf", path);
            DancerFrame &cur = mDancerFrames[mf];
            cur.mMoveIdx = -1;
            cur.mMoveFrameIdx = mf;
            cur.mSkeleton = dancer_frame->mSkeleton;
            for (int i = 0; i < 2; i++) {
                mPlayerDetectFrames[i][mf].Reset(
                    fv, -1, &moveFrames[i], &cur, (MoveMirrored)mirrored
                );
            }
        }
    }
    for (int i = 0; i < 2; i++) {
        for (int j = 0; j < 4; j++) {
            mDetectThresholds[i][j] = 0;
        }
    }
}

MoveDetector::~MoveDetector() {}

float MoveDetector::ActiveDetectFrac(int player, MoveDir *dir) {
    MILO_ASSERT(mActive, 0x42);
    MILO_ASSERT((0) <= (player) && (player) < (2), 0x43);
    std::vector<DetectFrame> &frames = mPlayerDetectFrames[player];
    return dir->DetectFrac(
        player,
        mMove,
        std::make_pair(frames.begin(), frames.end())
    );
}

float MoveDetector::LastDetectFrac(int player) const {
    MILO_ASSERT(mActive, 0x4D);
    MILO_ASSERT((0) <= (player) && (player) < (2), 0x4E);
    return mLastDetectFracs[player];
}

std::vector<DetectFrame> &MoveDetector::PlayerDetectFrames(int player) {
    MILO_ASSERT((0) <= (player) && (player) < (2), 0x3C);
    return mPlayerDetectFrames[player];
}

MoveAsyncDetector::MoveAsyncDetector(MoveDir *md) : mDir(md) {
    if (!TheGameData->GetSong().Null()) {
        MILO_ASSERT(md, 0xE9);
        DancerSequence *perfSeq = md->PerformanceSequence(kDifficultyExpert);
        if (!perfSeq) {
            MILO_NOTIFY("MoveAsyncDetector could not find expert performance sequence");
        } else {
            const std::vector<DancerFrame> &frames = perfSeq->GetDancerFrames();
            MILO_ASSERT(TheHamDirector, 0xF5);
            std::vector<HamMoveKey> keys;
            TheHamDirector->MoveKeys(kDifficultyExpert, md, keys);
            for (ObjDirItr<HamMove> it(md, true); nullptr != it; ++it) {
                if (it->Scored()) {
                    for (int i = 0; i < keys.size(); i++) {
                        if (keys[i].move == it) {
                            // ...
                        }
                    }
                }
                DancerSequence *seq = it->GetDancerSequence();
                if (!(seq)) {
                    MILO_NOTIFY("Could not find %s in expert keys", PathName(it));
                } else {
                    const FilterVersion *curFv = it->FilterVer();
                    const DancerFrame *curFrame = seq->GetDancerFrames().begin();
                    mDetectors.push_back(new MoveDetector(curFv, it, curFrame));
                }
            }
            std::sort(mDetectors.begin(), mDetectors.end(), MoveDetectorCmp());
        }
    }
}

MoveAsyncDetector::~MoveAsyncDetector() {
    mActiveDetectors.clear();
    DeleteAll(mDetectors);
}

void MoveAsyncDetector::EnqueueDetectFrames(int i1, int i2, float f3, int i4) {
    for (std::set<MoveDetector *>::iterator it = mActiveDetectors.begin(); it != mActiveDetectors.end(); ++it) {
        MoveDetector *cur = *it;
        cur->Poll(i1, i2, mDir);
        const HamMove *move = cur->Move();
        std::vector<DetectFrame> &frames = cur->PlayerDetectFrames(i4);
        mDir->EnqueueDetectFrames(f3, i4, frames, move->FilterVer());
    }
}

void MoveAsyncDetector::DisableAllDetectors() {
    mActiveDetectors.clear();
    FOREACH (it, mDetectors) {
        (*it)->Reset();
    }
}

MoveDetector *MoveAsyncDetector::FindDetector(const HamMove *move) {
    std::pair<std::vector<MoveDetector *>::iterator, std::vector<MoveDetector *>::iterator>
        range = std::equal_range(mDetectors.begin(), mDetectors.end(), move, MoveDetectorCmp());
    if (range.first == range.second) {
        if (move->GetDancerSequence()) {
            const DancerFrame *frame = &move->GetDancerSequence()->GetDancerFrames().front();
            MoveDetector *detector =
                new MoveDetector(move->FilterVer(), move, frame);
            mDetectors.push_back(detector);
            std::sort(mDetectors.begin(), mDetectors.end(), MoveDetectorCmp());
            return detector;
        }
        return 0;
    }
    return *range.first;
}

void MoveAsyncDetector::EnableDetector(HamMove *move) {
    if (nullptr == move)
        return;
    MoveDetector *detector = FindDetector(move);
    if (detector != nullptr)
        goto activate;
    auto _tmp0 = MakeString("Could not enable detector for %s", move->Name());
    TheDebug.Notify(_tmp0);
    return;
activate:
    int active = detector->mActive;
    if (!(!(active == 1))) {
        // skip
    } else {
        detector->mDetectFrameOffset = -1;
        *(int *)&detector->mLastDetectFracs[0] = 0;
        *(int *)&detector->mLastDetectFracs[1] = 0;
        detector->mLastDetectFrameIdx = -1;
        detector->mActive = true;
    }
    mActiveDetectors.insert(detector);
}

float MoveDetector::Last4BeatsDetectFrac(int player) const {
    MILO_ASSERT(mActive, 0x54);
    MILO_ASSERT((0) <= (player) && (player) < (2), 0x55);
    int lowCount = 0;
    float sum = 0.0f;
    for (int i = 0; i < 4; i++) {
        float val = mDetectThresholds[player][i];
        sum += val;
        if (val < 0.13f) {
            lowCount++;
        }
    }
    if (lowCount > 1)
        return 0.0f;
    float avg = sum * 0.25f;
    avg = avg * 1.15f;
    float clampedSum = 0.0f;
    for (int i = 0; i < 4; i++) {
        float val = mDetectThresholds[player][i];
        float absVal = -val < 0.0f ? val : 0.0f;
        float clamped = absVal - avg < 0.0f ? absVal : avg;
        clampedSum += clamped;
    }
    float result = -clampedSum < 0.0f ? clampedSum : 0.0f;
    return result - 1.0f < 0.0f ? result : 1.0f;
}

void MoveAsyncDetector::ClearLoopedRatingFrac(const HamMove *move) {
    MoveDetector *det = FindDetector(move);
    if (det) {
        for (int i = 0; i < 2; i++) {
            for (int j = 0; j < 4; j++) {
                det->mDetectThresholds[i][j] = 0;
            }
        }
    }
}

void MoveAsyncDetector::DisableDetector(HamMove *move) {
    if (move != 0) {
        MoveDetector *detector = FindDetector(move);
        if (detector != 0) {
            detector->Reset();
            mActiveDetectors.erase(detector);
        } else {
            const char *path = PathName(move);
            auto _tmp0 = MakeString("Could not disable detector for %s", path);
            TheDebug.Notify(_tmp0);
        }
    }
}
