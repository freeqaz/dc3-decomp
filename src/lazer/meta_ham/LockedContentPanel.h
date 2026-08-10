#pragma once
#include "HamPanel.h"
#include "hamobj\Difficulty.h"
#include "obj\Data.h"
#include "obj\Object.h"
#include "synth\Sound.h"
#include "utl\Symbol.h"

class HamLabel;

class LockedContentPanel : public HamPanel {
public:
    // Hmx::Object
    virtual ~LockedContentPanel();
    OBJ_CLASSNAME(LockedContentPanel)
    OBJ_SET_TYPE(LockedContentPanel)
    virtual DataNode Handle(DataArray *, bool);

    // UIPanel
    virtual void Enter();
    virtual void Exit();
    virtual void Poll();

    NEW_OBJ(LockedContentPanel)

    LockedContentPanel();
    void SetUpCampaignSong(Symbol);
    void SetUpCampaignMasterQuestHeader(Symbol);
    void SetUp(Symbol);
    void SetUpNoFlashcards(Symbol, Difficulty);
    void SetUpDifficultyLocked(Symbol, Symbol);
    void SetVoiceOver(Sound *, bool);

protected:
    virtual void FinishLoad();

    void TriggerTeaserText();

    // Array of object pointers accessed via offsets 0x3c-0x7b
    // First 8 slots (0x3c-0x5b) and second 8 slots (0x5c-0x7b) are paired
    // Used by SetUpDifficultyLocked, SetUpNoFlashcards for showing/hiding UI elements
    // Slots contain AppLabel*, HamLabel*, HamStarsDisplay* etc. (all UIComponent-derived)
    HamLabel *mLabels[16];
    Sound *mSound; // 0x7c
    Timer *mTimer; // 0x80
    bool mIsTeaserTextShowing;
};

extern LockedContentPanel *TheLockedContentPanel;
