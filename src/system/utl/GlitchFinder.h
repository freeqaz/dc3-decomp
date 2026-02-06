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

protected:
    int unk0; // 0x0
    int unk4; // 0x0
    bool unk8; // 0x8
    Timer unk10; // 0x10
    //int unk34; // 0x34
    float unk40; // 0x40
    GlitchPoker mPokerPool[2048]; // 0x44
    int unk30044; // 0x30044
    GlitchPoker *unk30048; // 0x30048
    GlitchPoker *unk3004c; // 0x3004c
    bool unk30050; // 0x30050
    bool unk30051; // 0x30051
    float unk30054; // 0x30054
    double *unk30058; // 0x30058
};

extern GlitchFinder TheGlitchFinder;