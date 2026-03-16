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
#include "utl/TimeConversion.h"
#include <list>

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
                    int keyIdx = -1;
                    for (int i = 0; i < keys.size(); i++) {
                        if (keys[i].move == it) {
                            keyIdx = i;
                            break;
                        }
                    }
                    if (keyIdx != -1) {
                        const DancerFrame *curFrame = frames.end();
                        for (const DancerFrame *df = frames.begin(); df != frames.end(); ++df) {
                            if ((int)df->mMoveIdx == keyIdx) {
                                curFrame = df;
                                break;
                            }
                        }
                        if (curFrame != frames.end()) {
                            const FilterVersion *curFv = it->FilterVer();
                            mDetectors.push_back(new MoveDetector(curFv, it, curFrame));
                        }
                    } else {
                        DancerSequence *seq = it->GetDancerSequence();
                        if (!(seq)) {
                            MILO_NOTIFY("Could not find %s in expert keys", PathName(it));
                        } else {
                            const FilterVersion *curFv = it->FilterVer();
                            const DancerFrame *curFrame = seq->GetDancerFrames().begin();
                            mDetectors.push_back(new MoveDetector(curFv, it, curFrame));
                        }
                    }
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
    auto msg = MakeString("Could not enable detector for %s", move->Name());
    TheDebug.Notify(msg);
    return;
activate:
    int active = detector->mActive;
    if (!(!(active == 1))) {
        // skip
    } else {
        *(int *)&detector->mLastDetectFracs[0] = 0;
        *(int *)&detector->mLastDetectFracs[1] = 0;
        detector->mLastDetectFrameIdx = -1;
        detector->mDetectFrameOffset = -1;
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

void MoveDetector::Poll(int moveIdx, int moveBeat, MoveDir *dir) {
    if (!mActive)
        return;
    if (moveBeat != mDetectFrameOffset) {
        if (mDetectFrameOffset != -1) {
            for (int player = 0; player < 2; player++) {
                float frac = ActiveDetectFrac(player, dir);
                float prevLast = mDetectThresholds[player][3];
                if (prevLast < frac)
                    frac -= prevLast;
                // Shift thresholds[player][1..3] down to [0..2]
                for (int j = 1; j <= 3; j++) {
                    mDetectThresholds[player][j - 1] = mDetectThresholds[player][j];
                }
                mDetectThresholds[player][3] = frac;
            }
        }
        mDetectFrameOffset = moveBeat;
    }
    if (moveIdx != mLastDetectFrameIdx) {
        if (mLastDetectFrameIdx != -1) {
            for (int player = 0; player < 2; player++) {
                mLastDetectFracs[player] = ActiveDetectFrac(player, dir);
            }
        }
        mLastDetectFrameIdx = moveIdx;
        float beatOffset = (float)moveIdx * 4.0f;
        // Update mMoveIdx in all dancer frames
        for (auto it = mDancerFrames.begin(); it != mDancerFrames.end(); ++it) {
            it->mMoveIdx = (short)moveIdx;
        }
        // Update detect frame seconds for both player detect frame vectors
        int numFrames = (int)mPlayerDetectFrames[0].size();
        for (int j = 0; j < numFrames; j++) {
            float beat = mPlayerDetectFrames[0][j].GetMoveFrame()->GetBeat();
            float seconds = BeatToSeconds(beat + beatOffset);
            for (int p = 0; p < 2; p++) {
                mPlayerDetectFrames[p][j].SetSecondsAndReset(seconds);
            }
        }
    }
}

#ifdef HX_NATIVE
float MoveAsyncDetector::MoveRatingFrac(int, RatingBar, const HamMove *) { return 0.0f; }
void MoveAsyncDetector::DisableDetector(HamMove *) {}
#else
float MoveAsyncDetector::MoveRatingFrac(int player, RatingBar bar, const HamMove *move) {
    if (move != nullptr && move->Scored()) {
        MILO_ASSERT((0) <= (player) && (player) < (2), 0x144);
        MoveDetector *det = FindDetector(move);
        if (det != nullptr) {
            if (!det->mActive) {
                static stlpmtx_std::list<String> _dw;
                char *name = (char *)move->Name();
                const char *msg = MakeString("MoveRatingFrac for %s called, but it's disabled", name);
                if (!AddToStrings(msg, _dw)) {
                    return 0.0f;
                }
                TheDebug.Notify(msg);
            } else {
                MoveDir *dir = mDir;
                int beat = dir->MoveBeat();
                int idx = mDir->MoveIdx();
                det->Poll(idx, beat, dir);
                if (bar == 0) {
                    return det->ActiveDetectFrac(player, mDir);
                } else if (bar == 2) {
                    return det->Last4BeatsDetectFrac(player);
                } else {
                    return det->LastDetectFrac(player);
                }
            }
        } else {
            char *name2 = (char *)move->Name();
            TheDebug.Notify(MakeString("Could not find rating for %s", name2));
        }
    }
    return 0.0f;
}

void MoveAsyncDetector::DisableDetector(HamMove *move) {
    if ((unsigned long)0 != (int)move) {
        MoveDetector *detector = FindDetector(move);
        if (detector != (int)0) {
            detector->Reset();
            mActiveDetectors.erase(detector);
        } else {
            char *name = (char *)move->Name();
            auto msg = MakeString("Could not disable detector for %s", name);
            TheDebug.Notify(msg);
        }
    }
}
#endif // !HX_NATIVE
