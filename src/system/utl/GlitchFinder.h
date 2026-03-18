#pragma once
#include "os/Timer.h"

DataNode GlitchFindScriptImpl(DataArray *, int);

class GlitchAverager {
public:
    GlitchAverager();
    ~GlitchAverager();

    void PushInstance(float, bool);

    float mAvg; // 0x0
    float mMax; // 0x4
    int mCount; // 0x8
    float mGlitchAvg; // 0xc
    int mGlitchCount; // 0x10
};

class AutoGlitchPoker {
public:
    ~AutoGlitchPoker();

protected:
    bool mActive;
};

class GlitchPoker {
public:
    GlitchPoker();
    ~GlitchPoker();

    bool OverBudget();
    void PollAveragesRecurse(bool);
    void Dump(TextStream &, int);
    void ClearData();

    static float smLastDumpTime;
    static bool smDumpLeaves;
    static float smThreshold;
    static float smTotalLeafTime;

private:
    friend class GlitchFinder;
    static std::vector<float> smNestedStartTimes;
    void PrintResult(TextStream &);
    void PrintNestedStartTimes(TextStream &, float);

    friend class GlitchFinder;

protected:
    char mName[64]; // 0x0
    float mTime; // 0x40
    float mTimeEnd; // 0x44
    std::vector<GlitchPoker *> mChildren; // 0x48
    GlitchPoker *mParent; // 0x54
    float mBudget; // 0x58
    GlitchAverager *mAvg; // 0x5c
};

class GlitchFinder {
public:
    GlitchFinder();
    ~GlitchFinder();

    void CheckDump();
    void PokeEnd(unsigned int);
    void PokeStart(const char *, unsigned int, float, float, GlitchAverager *);
    void Poke(const char *, unsigned int);
    static void Init();

private:
    GlitchPoker *NewPoker();
    static DataNode OnGlitchFind(DataArray *);
    static DataNode OnGlitchFindBudget(DataArray *);
    static DataNode OnGlitchFindLeaves(DataArray *);
    static DataNode OnGlitchFindPoke(DataArray *);

    friend DataNode GlitchFindScriptImpl(DataArray *, int);

protected:
    int mFrameCount; // 0x0
    int mGlitchCount; // 0x4
    bool mStop; // 0x8
    Timer mTime; // 0x10
    float mLastTime; // 0x40
    GlitchPoker mPokerPool[2048]; // 0x44
    int mPokerIndex; // 0x30044
    GlitchPoker *mStartPoker; // 0x30048
    GlitchPoker *mCurPoker; // 0x3004c
    bool mActive; // 0x30050
    bool mDumpLeavesOnly; // 0x30051
    float mLeafThreshold; // 0x30054
    unsigned int mOverheadCycles; // 0x30058
};

extern GlitchFinder TheGlitchFinder;