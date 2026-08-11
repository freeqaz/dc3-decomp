#pragma once
#include "hamobj\HamNavList.h"
#include "meta_ham\HamPanel.h"
#include "meta_ham\SaveLoadManager.h"
#include "obj\Data.h"
#include "obj/Object.h"
#include "os\JoypadMsgs.h"
#include "rndobj\Draw.h"
#include "rndobj\Group.h"
#include "ui\UIPanel.h"

class HelpBarPanel : public HamPanel {
public:
    HelpBarPanel();
    // Hmx::Object
    virtual ~HelpBarPanel();
    OBJ_CLASSNAME(HelpBarPanel)
    OBJ_SET_TYPE(HelpBarPanel)
    virtual DataNode Handle(DataArray *, bool);
    virtual bool SyncProperty(DataNode &, DataArray *, int, PropOp);

    // UIPanel
    virtual void Draw();
    virtual void Enter();
    virtual void Exit();
    virtual void Poll();
    virtual void Unload();
    virtual void FinishLoad();

    static HelpBarPanel *sInstance;
    NEW_OBJ(HelpBarPanel)

    bool IsAnimating();
    bool UpdateBackButton(UIPanel *);
    bool UpdateTertiaryButton(UIPanel *);
    void EnterControllerMode();
    void ExitControllerMode(bool);
    bool IsWriteIconShowing();
    bool IsWriteIconUp() const;
    void SyncToPanel(UIPanel *);
    void SetTertiaryLabels(DataArray *);

    bool IsSaving() const { return mSaving; }
    bool AllowController() const { return mAllowController; }

    DataNode OnEnterBlacklightMode(const DataArray *);
    DataNode OnExitBlacklightMode(const DataArray *);

private:
    bool ShouldHideHelpbar() const;
    void ShowPhysicalWriteIcon();
    void HidePhysicalWriteIcon();
    void DeactivatePhysicalWriteIcon();
    void ShowWaveGestureIcon();
    void HideWaveGestureIcon();
    void PollSaveDeactivation();
    DataNode OnWaveGestureEnabled(const DataArray *);
    DataNode OnWaveGestureDisabled(const DataArray *);
    DataNode OnMsg(const ButtonDownMsg &);
    DataNode OnMsg(const SaveLoadMgrStatusUpdateMsg &);

    HamNavList *mLeftHandNavList; // 0x3c
    RndGroup *mAll; // 0x40
    bool mSaveDeactivationPending;
    Timer mSaveDeactivationTimer;
    bool mDisabled;
    bool mAllowController; // 0x79
    bool mSaving;
    bool mWriteIconShowing;
    bool mWaveGestureEnabled; // 0x7c
    Timer mWriteIconTimer;
    UIPanel *mSyncedPanel;
};

#ifdef HX_NATIVE
// Native never enters controller mode via gesture, so controller_mode.flow never
// gets activated and the EXIT CONTROLLER MODE band stays at its authored off-screen
// rest position. Activate it exactly once at boot, from the earliest hook where the
// helpbar dir is loaded and TheGestureMgr exists. Idempotent (function-local once
// guard); no-ops until both preconditions hold, so safe to call from multiple hooks.
void NativeBootControllerModeOnce();
#endif
