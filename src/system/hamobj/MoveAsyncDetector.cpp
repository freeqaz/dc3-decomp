#include "MoveDir.h"
#include "hamobj\DancerSequence.h"
#include "hamobj\Difficulty.h"
#include "hamobj\FilterVersion.h"
#include "hamobj\HamDirector.h"
#include "hamobj\HamGameData.h"
#include "hamobj\HamMove.h"
#include "hamobj\MoveDetector.h"
#include "os\Debug.h"
#include "stl\_pair.h"
#include "utl\TimeConversion.h"

MoveDetector::MoveDetector(
    const FilterVersion *fv, const HamMove *move, const DancerFrame *&dancer_frame
)
    : mMove(move), mActive(false), mLastDetectFrameIdx(-1), mDetectFrameOffset(-1) {
    MILO_ASSERT(mMove, 0x18);
    MoveMirrored mirrored = move->Mirrored();
    const std::vector<MoveFrame> &moveFrames = mMove->GetMoveFrames();
    mDancerFrames.resize(moveFrames.size());
    for (int i = 0; i < 2; i++) {
        mLastDetectFracs[i] = 0;
        mPlayerDetectFrames[i].resize(moveFrames.size());
    }
    for (int mf = 0; mf < moveFrames.size(); mf++) {
        if (dancer_frame->mMoveFrameIdx != mf) {
            char *path = move ? (char *)PathName(move) : (char *)"NULL";
            MILO_FAIL("HamMove '%s': dancer_frame->mMoveFrameIdx != mf", path);
        }
        DancerFrame &cur = mDancerFrames[mf];
        cur.mMoveIdx = -1;
        cur.mMoveFrameIdx = mf;
        cur.mSkeleton = dancer_frame->mSkeleton;
        for (int i = 0; i < 2; i++) {
            DetectFrame &df = mPlayerDetectFrames[i][mf];
            df.Reset(fv, -1, &moveFrames[mf], &cur, (MoveMirrored)mirrored);
        }
        dancer_frame++;
    }
    for (int i = 0; i < 2; i++) {
        for (int j = 0; j < 4; j++) {
            mDetectThresholds[i][j] = 0;
        }
    }
}

MoveDetector::~MoveDetector() {}

void MoveDetector::Poll(int moveIdx, int beat, MoveDir *dir) {
    if (!mActive)
        return;

    // Beat changed - shift detect thresholds and record new value
    if (beat != mDetectFrameOffset) {
        if (mDetectFrameOffset != -1) {
            for (int p = 0; p < 2; p++) {
                float frac = ActiveDetectFrac(p, dir);
                float oldVal = mDetectThresholds[p][3];
                if (oldVal < frac) {
                    frac = frac - oldVal;
                }
                // Shift thresholds: [1] -> [0], [2] -> [1], [3] -> [2]
                for (int i = 1; i < 4; i++) {
                    mDetectThresholds[p][i - 1] = mDetectThresholds[p][i];
                }
                // Store new frac at end
                mDetectThresholds[p][3] = frac;
            }
        }
        mDetectFrameOffset = beat;
    }

    // Move index changed - update detect frames
    if (moveIdx != mLastDetectFrameIdx) {
        if (mLastDetectFrameIdx != -1) {
            for (int p = 0; p < 2; p++) {
                mLastDetectFracs[p] = ActiveDetectFrac(p, dir);
            }
        }

        mLastDetectFrameIdx = moveIdx;
        float beatOffset = (float)(long long)moveIdx * 4.0f;

        // Update all dancer frames' move index
        std::vector<DancerFrame>::iterator it = mDancerFrames.begin();
        if (it != mDancerFrames.end()) {
            do {
                it->mMoveIdx = (short)moveIdx;
                ++it;
            } while (it != mDancerFrames.end());
        }

        // Reset all detect frames with new timing
        int numFrames = (int)mPlayerDetectFrames[0].size();
        for (int i = 0; i < numFrames; i++) {
            float secs = BeatToSeconds(
                mPlayerDetectFrames[0][i].GetMoveFrame()->GetBeat() + beatOffset
            );
            for (int p = 0; p < 2; p++) {
                mPlayerDetectFrames[p][i].SetSecondsAndReset(secs);
            }
        }
    }
}

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
                    int foundIdx = -1;
                    for (int i = 0; i < keys.size(); i++) {
                        if (keys[i].move == it) {
                            foundIdx = i;
                            break;
                        }
                    }
                    if (foundIdx == -1) {
                        if (it->GetDancerSequence()) {
                            DancerSequence *seq = it->GetDancerSequence();
                            const DancerFrame *curFrame = seq->GetDancerFrames().begin();
                            const FilterVersion *curFv = it->FilterVer();
                            mDetectors.push_back(new MoveDetector(curFv, it, curFrame));
                        } else {
                            MILO_NOTIFY("Could not find %s in expert keys", PathName(it));
                        }
                    } else {
                        const DancerFrame *curFrame = frames.begin();
                        while (curFrame != frames.end()) {
                            if (curFrame->mMoveIdx == foundIdx)
                                break;
                            curFrame++;
                        }
                        if (curFrame != frames.end()) {
                            const FilterVersion *curFv = it->FilterVer();
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
        // NOTE (w7-av): 90.53 is a register-allocation floor, not a statement-order
        // one.  The image keeps `cur` in r25 across the PlayerDetectFrames call and
        // does the Move() load inline at 0x8252D3B8 (`lwz r3, 0x0(r25)`), putting
        // `frames` in r24; we kill `cur` early and put `frames` in r25.  Measured:
        // deferring Move() below `frames` (86.8), spelling it inline as
        // `cur->Move()->FilterVer()` in the call (86.8), and hoisting it to a
        // separate `ver` statement (86.8) all make MSVC take a NINTH callee-saved
        // GPR (`stfd f31, -0x48` / __savegprlr_23) and score worse.  Passing
        // PlayerDetectFrames inline too is 84.2 and proves the arg order: MSVC
        // evaluates this call right-to-left, so `frames` has to be a named local.
        // w14-b (still 90.53): also inert -- `inline` on every MoveDetector
        // method the map lists `f i` (Poll, PlayerDetectFrames, ctor/dtor, the
        // Frac getters); frames-first + cur->Move()->FilterVer() in the call,
        // a separate `ver` local, (*it)-> everywhere, a frames pointer: 86.0-86.7.
        // w21-b (still 90.53): the image loads mMove straight into r3 AFTER
        // `mr r24, r3` (so cur stays live in r25 and frames takes r24); every
        // spelling that loads Move() after the call lets MSVC's pre-RA
        // scheduler hoist the load above the frames copy, so frames reuses
        // cur's register (86.8, __savegprlr_25).  Re-measured: frames first +
        // cur->Move()->FilterVer() inline (86.8), `cur` declared at function
        // scope (86.8), PlayerDetectFrames inline with `move` named first
        // (84.2; confirms right-to-left argument evaluation).  Behaviour is
        // the image's: Poll(i1, i2, mDir), then EnqueueDetectFrames(f3, i4,
        // frames, mMove->FilterVer()).
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
    if (move != 0) {
        MoveDetector *detector = FindDetector(move);
        if (detector != 0) {
            // w17-b: 97.96 -> 100.  The activation is an INLINE MoveDetector
            // method (the mirror of the header's Reset()), not open code here.
            // Written inline in this body, MSVC scheduled the dead loop pointer
            // `addi r11, r3, 0x34` between the two -1 stores (loop form, 95.9)
            // or dropped it and swapped r10/r11 (straight-line form, 97.96, see
            // w7-av / w8-i history in git); through the inline call boundary the
            // stores land exactly where the image has them (0x8252EA90..AAC).
            detector->Activate();
            mActiveDetectors.insert(detector);
        } else {
            // w8-i: `const char *`, NOT a `(char *)` cast.  MakeString takes its
            // varargs by reference, so the cast instantiated
            // `??$MakeString@PAD@@YAPBDPBDABQAD@Z` where the image calls
            // `??$MakeString@PBD@@YAPBDPBDABQBD@Z` -- a genuinely different
            // callee that only the name_check ruler charges (Hmx::Object::Name()
            // has returned `const char *` all along).
            const char *name = move->Name();
            auto _tmp0 = MakeString("Could not enable detector for %s", name);
            TheDebug.Notify(_tmp0);
        }
    }
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
    sum = 0.0f;
    for (int i = 0; i < 4; i++) {
        float val = mDetectThresholds[player][i];
        float absVal = -val < 0.0f ? val : 0.0f;
        float clamped = absVal - avg < 0.0f ? absVal : avg;
        sum += clamped;
    }
    float result = -sum < 0.0f ? sum : 0.0f;
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

float MoveAsyncDetector::MoveRatingFrac(int player, RatingBar bar, const HamMove *move) {
    if (!move || !move->Scored()) {
        return 0.0f;
    }
    MILO_ASSERT((0) <= (player) && (player) < (2), 0x144);
    MoveDetector *detector = FindDetector(move);
    const char *msg;
    if (detector != 0) {
        if (!detector->mActive) {
            MILO_NOTIFY_ONCE("MoveRatingFrac for %s called, but it's disabled", move->Name());
            return 0.0f;
        }
        MoveDir *dir = mDir;
        int beat = dir->MoveBeat();
        int idx = mDir->MoveIdx();
        detector->Poll(idx, beat, dir);
        if (bar == kRatingActive) {
            return detector->ActiveDetectFrac(player, mDir);
        }
        if (bar == kRatingLast4Beats) {
            return detector->Last4BeatsDetectFrac(player);
        }
        return detector->LastDetectFrac(player);
    } else {
        msg = MakeString("Could not find rating for %s", move->Name());
    }
    TheDebug.Notify(msg);
    return 0.0f;
}

void MoveAsyncDetector::DisableDetector(HamMove *move) {
    if (move != 0) {
        MoveDetector *detector = FindDetector(move);
        if (detector != 0) {
            detector->Reset();
            mActiveDetectors.erase(detector);
        } else {
            const char *name = move->Name();
            auto _tmp0 = MakeString("Could not disable detector for %s", name);
            TheDebug.Notify(_tmp0);
        }
    }
}
