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
// A no-op under DC3_CONTROLLER_MODE=faithful (docs/debugging/native.md
// "Controller mode policy"), where the image's EnterControllerMode runs instead.
void NativeBootControllerModeOnce();

// Gameplay half of the controller mode policy (DC3_CONTROLLER_MODE=forced).
// The image leaves controller mode whenever the gameplay panel has focus
// (ShellInput::Poll's TheGamePanel arm calls ExitControllerMode(true)), so the
// helpbar -- whose Draw shows `all.grp` whenever InControllerMode() -- is gone
// for the whole song. `forced` pins GestureMgr in controller mode (gameplay
// reads it through HamGameData::IsSkeletonPresent), so the helpbar never left.
// These take the helpbar alone out of controller mode for gameplay and put it
// back when focus leaves the gameplay panel; GestureMgr is untouched. All are
// no-ops under `faithful`, where the image's ExitControllerMode runs instead.
void NativeForcedHelpbarExitForGameplay(bool immediate);
void NativeForcedHelpbarRestoreAfterGameplay();
// What HelpBarPanel::Draw reads in place of TheGestureMgr->InControllerMode().
bool NativeHelpbarInControllerMode();
// DC3_CONTROLLER_MODE=auto: re-latch the policy from NativePoseProviderRunning()
// and perform the forced<->faithful transition.  Called once per frame from
// ShellInput::Poll.
class ShellInput;
void NativeControllerModePolicyTick(ShellInput *shellInput);
#endif
