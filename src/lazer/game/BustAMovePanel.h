#pragma once
#include "gesture\BaseSkeleton.h"
#include "gesture\Skeleton.h"
#include "hamobj\BustAMoveData.h"
#include "hamobj\DancerSkeleton.h"
#include "hamobj/FreestyleMoveRecorder.h"
#include "hamobj\HamLabel.h"
#include "hamobj\HamPhraseMeter.h"
#include "hamobj\ScoreUtl.h"
#include "meta_ham\HamPanel.h"
#include "obj\Dir.h"
#include "obj/Object.h"
#include "rndobj\Dir.h"

class BustAMovePanel : public HamPanel {
public:
    enum BAMState {
        kBAMState_CountIn = 0,
        kBAMState_Recording = 1,
        kBAMState_Playing = 2,
        kBAMState_ShowMove = 3,
        kBAMState_PlayCountIn = 4,
        kBAMState_RecordCountIn = 5,
        kBAMState_FailureToBust = 6,
        kBAMState_ShowMoveSequenceSetup = 7,
        kBAMState_ShowMoveSequence = 8,
        kBAMState_End = 9,
        kBAMState_None = 10,
    };
    BustAMovePanel();
    // Hmx::Object
    virtual ~BustAMovePanel();
    OBJ_CLASSNAME(BustAMovePanel);
    OBJ_SET_TYPE(BustAMovePanel);
    virtual DataNode Handle(DataArray *, bool);
    virtual bool SyncProperty(DataNode &, DataArray *, int, PropOp);
    // UIPanel
    virtual void Draw();
    virtual void Enter();
    virtual void Exit();
    virtual void Poll();

    NEW_OBJ(BustAMovePanel)

    void OnBeat();
    void SetUpSongStructure(Symbol);
    void PlayVO(Symbol);

private:
    bool InBustAMove();

    void CacheObjects();
    float GetMovePromptVOLength();
    void PlayIntroVO();
    void QueueMovePromptVO();
    void PlayMovePromptVO();
    DataArray *GetMoveNameData(int);
    Symbol GetPlayerColor(int);
    MoveRating GetMoveRating(float);
    void SetFlashcardText(int, int, Symbol);
    void SetMovePrompt();
    void IncreaseScore(int, int);
    void ResetScores();
    void SetFlashcardName(int, int, int);
    void CountIn(int);
    void ShowMoveRating(MoveRating, int);
    void SetRoundFailure();
    void ShowGetReadyCard(Symbol, SkeletonSide);
    void SetUpMoveNames();
    void PollCaptureFlashcard();
    __declspec(noinline) void AnimateFlashcard(int);
    __declspec(noinline) void AdvanceFlashcards();
    int RepsToNextPhrase();
    void SetFlashcardImage(int, int, int);

    BAMState mState; // 0x3c
    FreestyleMoveRecorder *mRecorder; // 0x40
    int mBeatCount; // 0x44 - beat counter within current state phase
    std::list<Symbol> mFlashcardLabels; // 0x48 - queue of flashcard text tokens
    std::list<int> mFlashcardSlots; // 0x50 - queue of flashcard image indices
    int mRecordSkelIdx; // 0x58
    float mMoveScore; // 0x5c - accumulated move accuracy score
    ObjectDir *mHUDPanel; // 0x60
    int mActivePlayer; // 0x64 - current player index (0 or 1)
    int mCountInLength; // 0x68 - number of beats for count-in
    int mMatchCount; // 0x6c - number of successfully matched moves this round
    int mPendingState; // 0x70 - queued next state override (BAMState or kBAMState_None)
    HamLabel *mStatusLabel; // 0x74
    HamLabel *mMovePromptLabel; // 0x78
    bool mRecordSuccess; // 0x7c - whether the last recording attempt passed
    float mRecordScore; // 0x80
    int mMoveIndex; // 0x84 - current freestyle move slot (0-3)
    int unk88; // 0x88
    int unk8c; // 0x8c
    int mPlayerScoreLeft; // 0x90 - reinterpreted as float for final sequence scoring
    int mPlayerScoreRight; // 0x94 - reinterpreted as float for final sequence scoring
    RndDir *mBAMColumns[kNumSkeletonSides]; // 0x98
    int mCreatorSide; // 0xa0 - side of the move creator (SkeletonSide)
    DancerSkeleton mCapturePoses[3]; // 0xa4 - skeleton poses for flashcard capture
    int mCaptureStep; // 0x92c - which capture frame to grab (1-3), 0=idle
    float mCaptureTimer; // 0x930 - delay timer before taking capture
    int mCaptureFrames; // 0x934 - frames left to render flashcard capture
    HamPanel *mBAMVisualizerPanel; // 0x938
    int mMoveNameIndices[4]; // 0x93c - indices into shuffled move names per slot
    int mRetryCount0; // 0x94c - retry count for player 0
    int mRetryCount1; // 0x950 - retry count for player 1
    int mMaxRetries; // 0x954 - maximum allowed retries
    float mLoopStartBeat; // 0x958 - audio loop start (-1 = none)
    float mLoopEndBeat; // 0x95c - audio loop end (-1 = none)
    HamPhraseMeter *mPhraseMeters[kNumSkeletonSides]; // 0x960
    int mFailureEndBeat; // 0x968 - beat at which failure loop ends
    int mFinalSequenceType; // 0x96c - final sequence complexity (1/2/3)
    bool mIsMulligan; // 0x970 - true if this is a retry attempt
    float mDancerTakeScore; // 0x974 - score from dancer take recording
    float mCurrentMoveScore; // 0x978 - score from current move recording
    std::vector<int> mSongStructure; // 0x97c
    int mRepsRemaining; // 0x988 - reps remaining until next phrase
    bool mStreamJumped; // 0x98c - set when audio stream jumps
    std::vector<int> mShuffledMoveNames; // 0x990
    int mMoveNameCursor; // 0x99c - wrapping index into mShuffledMoveNames
    float mNextVOTime; // 0x9a0 - next scheduled VO play time
    int mFlawlessFlags; // 0x9a4 - per-player flawless tracking (accessed as bool array)
    int mMoveCreators[4]; // 0x9a8 - which player created each move
    bool mPlayIntroVO; // 0x9b8
    bool mNoPosesDetected; // 0x9b9 - set when no skeleton poses found during capture
    int mDepthBufPlayer; // 0x9bc
};
