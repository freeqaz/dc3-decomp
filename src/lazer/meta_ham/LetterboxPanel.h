#pragma once
#include "meta_ham/HamPanel.h"
#include "obj/Data.h"
#include "obj/Object.h"
#include "rndobj/Group.h"
#include "ui/UIPanel.h"

class LetterboxPanel : public HamPanel {
public:
    LetterboxPanel();
    // Hmx::Object
    virtual ~LetterboxPanel();
    OBJ_CLASSNAME(LetterboxPanel)
    OBJ_SET_TYPE(LetterboxPanel)
    virtual DataNode Handle(DataArray *, bool);
    virtual bool SyncProperty(DataNode &, DataArray *, int, PropOp);

    // UIPanel
    virtual void Draw();
    virtual void Enter();
    virtual void Poll();
    virtual void Unload();

    NEW_OBJ(LetterboxPanel)

    static LetterboxPanel *sInstance;

    bool ShouldHideLetterbox() const;
    bool ShouldShowHandHelp() const;
    bool IsBlacklightMode();
    bool IsLeavingBlacklightMode();
    bool IsEnteringBlacklightMode();
    bool InBlacklightTransition();
    void VoiceInput(int, bool);
    void SyncToPanel(UIPanel *);
    void EnterBlacklightMode();
    void ExitBlacklightMode(bool);
    void SetBlacklightMode(bool);
    void SetBlacklightModeImmediately(bool);
    void ToggleBlacklightMode(bool);

private:
    UIPanel *mSyncedPanel;
    RndGroup *mLetterboxGroup;
    bool mIsBlacklightMode; // 0x44
    bool mBlacklightActive;
    Timer mBlacklightTimer;
    int mBlacklightTimeout;
    int mBlacklightPhase;
    float mBlacklightTimeoutMs;
    float unk84;
};
