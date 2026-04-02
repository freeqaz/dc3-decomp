#include "meta_ham/SaveLoadManager.h"
#include "meta/FixedSizeSaveable.h"
#include "meta/FixedSizeSaveableStream.h"
#include "meta/MemcardMgr.h"
#include "meta/SongMgr.h"
#include "meta_ham/HamMemcardAction.h"
#include "meta_ham/HamProfile.h"
#include "meta_ham/HamUI.h"
#include "meta_ham/ProfileMgr.h"
#include "meta_ham/UIEventMgr.h"
#include "obj/Data.h"
#include "obj/Dir.h"
#include "obj/Object.h"
#include "os/Debug.h"
#include "os/Memcard.h"
#include "os/PlatformMgr.h"
#include "ui/UIPanel.h"
#include "utl/BufStream.h"
#include "utl/CacheMgr.h"
#include "utl/Locale.h"
#include "utl/MemMgr.h"
#include "utl/Symbol.h"

namespace {
    const char *kStrGlobalCacheName = "global";
}

SaveLoadManager *TheSaveLoadMgr;

SaveLoadManager::SaveLoadManager()
    : mActivated(0), mInitialLoadPending(1), mState(), mStateAtSelectStart(), mPadNum(-1), mActiveProfile(0), mCacheFileSize(0),
      mSigninMask(0), mCacheID(0), mCache(0), mData(0), mSongCacheWriteDisabled(0), mWaiting(0),
      unk64(0), unk68(), mNeedsSave(0), mNeedsLoad(0), mLastChosenDeviceID(0),
      mDeviceIDState(0), mAction(0) {
    SetName("saveload_mgr", ObjectDir::Main());
    ThePlatformMgr.AddSink(this, SigninChangedMsg::Type());
}

SaveLoadManager::~SaveLoadManager() {
    ThePlatformMgr.RemoveSink(this, SigninChangedMsg::Type());
    TheUIEventMgr->RemoveSink(this);
    RELEASE(mAction);
}

BEGIN_HANDLERS(SaveLoadManager)
    HANDLE_ACTION(autosave, AutoSave())
    HANDLE_ACTION(autoload, AutoLoad())
    HANDLE_ACTION(manual_save, ManualSave(_msg->Obj<HamProfile>(2)))
    HANDLE_EXPR(is_autosave_enabled, IsAutosaveEnabled(_msg->Obj<HamProfile>(2)))
    HANDLE_ACTION(enable_autosave, EnableAutosave(_msg->Obj<HamProfile>(2)))
    HANDLE_ACTION(disable_autosave, DisableAutosave(_msg->Obj<HamProfile>(2)))
    HANDLE_ACTION(handle_eventresponse_start, HandleEventResponseStart(_msg->Int(2)))
    HANDLE_ACTION(
        handle_eventresponse, HandleEventResponse(_msg->Obj<HamProfile>(2), _msg->Int(3))
    )
    HANDLE_EXPR(get_dialog_msg, GetDialogMsg())
    HANDLE_EXPR(get_dialog_opt1, GetDialogOpt1())
    HANDLE_EXPR(get_dialog_opt2, GetDialogOpt2())
    {
        _NEW_STATIC_SYMBOL(get_dialog_focus_option)
        if (sym == _s) {
            int focus = 0;
            if (mState == kS_ManualLoadConfirm)
                focus = 1;
            return DataNode(focus);
        }
    }
    HANDLE_EXPR(is_initial_load_done, IsInitialLoadDone())
    HANDLE_EXPR(is_idle, IsIdle())
    HANDLE_ACTION(activate, Activate())
    HANDLE_ACTION(printout_savesize_info, PrintoutSaveSizeInfo())
    HANDLE_MESSAGE(DeviceChosenMsg)
    HANDLE_MESSAGE(NoDeviceChosenMsg)
    HANDLE_MESSAGE(MCResultMsg)
    HANDLE_MESSAGE(SigninChangedMsg)
    HANDLE_MESSAGE(EventDialogDismissMsg)
    HANDLE_SUPERCLASS(Hmx::Object)
END_HANDLERS

void SaveLoadManager::AutoSave() {
    if (IsReasonToAutosave()) {
        mNeedsSave = true;
    }
}

void SaveLoadManager::AutoLoad() {
    if (IsReasonToAutoload()) {
        mNeedsLoad = true;
    }
}

void SaveLoadManager::HandleEventResponseStart(int) { mStateAtSelectStart = mState; }

__forceinline bool SaveLoadManager::IsIdle() const {
    return mState == 0 && (!mActivated || (!mNeedsSave && !mNeedsLoad));
}

void SaveLoadManager::PrintoutSaveSizeInfo() {
    FixedSizeSaveable::EnablePrintouts(true);
    MILO_LOG("SAVESIZE\n");
    int profileSize = HamProfile::SaveSize(0x5C);
    int symTableSize = FixedSizeSaveableStream::GetSymbolTableSize(0x5C);
    MILO_LOG("Symbol Table Size = %i\n", symTableSize);
    MILO_LOG("SAVESIZE TOTAL = %i \n", symTableSize + profileSize);
}

bool SaveLoadManager::IsReasonToAutosave() {
    HamProfile *p = GetAutosavableProfile();
    return p || TheProfileMgr.GlobalOptionsNeedsSave() || SongCacheNeedsWrite();
}

bool SaveLoadManager::IsReasonToAutoload() {
    HamProfile *p = GetNewSigninProfile();
    return p || mInitialLoadPending;
}

void SaveLoadManager::EnableAutosave(HamProfile *p) {
    if (!p) {
        MILO_NOTIFY("Tried to enable autosave without a valid profile.");
    } else {
        ManualSave(p);
    }
}

void SaveLoadManager::ManualSave(HamProfile *pProfile) {
    if (mState != 0) {
        State cur = mState;
        MILO_NOTIFY(
            "Attempted to perform a manual save, but saveloadmgr is not idle (state = %d).",
            cur
        );
    } else {
        MILO_ASSERT(pProfile, 0x364);
        mActiveProfile = pProfile;
        mPadNum = pProfile->GetPadNum();
        TheMemcardMgr.AddSink(this);
        SetState((State)0x56);
    }
}
void SaveLoadManager::Start() {
    mPadNum = -1;
    TheMemcardMgr.AddSink(this);
    SetState((State)1);
    if (mMode == 0) {
        UpdateStatus((SaveLoadMgrStatus)3);
    }
}

void SaveLoadManager::Finish() {
    if (mMode == 0) {
        UpdateStatus((SaveLoadMgrStatus)4);
    }
    TheMemcardMgr.RemoveSink(this);
    SetState(kS_Finish);
}

void SaveLoadManager::UpdateStatus(SaveLoadMgrStatus status) {
    static SaveLoadMgrStatusUpdateMsg msg(-1);
    msg[0] = status;
    Export(msg, true);
}

bool SaveLoadManager::SongCacheNeedsWrite() {
    return TheSongMgr.SongCacheNeedsWrite() && !mSongCacheWriteDisabled;
}

void SaveLoadManager::DisableAutosave(HamProfile *pProfile) {
    if (!pProfile) {
        MILO_NOTIFY("Tried to disable autosave without a valid profile.");
        return;
    }

    if (!IsIdle()) {
        MILO_NOTIFY("Tried to disable autosave while saveloadmgr is not idle.");
        return;
    }

    pProfile->SetSaveState(kMetaProfileError);
}

bool SaveLoadManager::IsSafePlaceToSave() const {
    if (!TheUIEventMgr->HasActiveDialogEvent()
        && !TheUIEventMgr->HasActiveTransitionEvent()) {
        return true;
    } else
        return false;
}

bool SaveLoadManager::IsSafePlaceToLoad() const {
    if (TheUIEventMgr->HasActiveDialogEvent()
        || TheUIEventMgr->HasActiveTransitionEvent()) {
        return false;
    } else {
        bool ret = true;
        UIPanel *panel = TheHamUI.FocusPanel();
        if (panel) {
            static Symbol allow_load("allow_load");
            const DataNode *n = panel->Property(allow_load, false);
            if (n) {
                ret = n->Int();
            }
        }
        return ret;
    }
}

void SaveLoadManager::Activate() {
    if (!mActivated) {
        mActivated = true;
        mNeedsLoad = true;
        TheUIEventMgr->AddSink(this, EventDialogDismissMsg::Type());
    }
}

bool SaveLoadManager::IsAutosaveEnabled(HamProfile *p) {
    if (!p) {
        MILO_NOTIFY("Tried to get autosave enabled status without a valid profile.");
        return false;
    } else {
        return p->IsAutosaveEnabled();
    }
}

Symbol SaveLoadManager::GetDialogOpt1() {
    static Symbol mc_button_create_data("mc_button_create_data");
    static Symbol mc_button_choose_device("mc_button_choose_device");
    static Symbol mc_button_continue("mc_button_continue");
    static Symbol mc_button_overwrite("mc_button_overwrite");
    static Symbol song_info_cache_button_create("song_info_cache_button_create");
    static Symbol song_info_cache_button_corrupt_overwrite(
        "song_info_cache_button_corrupt_overwrite"
    );
    static Symbol global_options_button_create("global_options_button_create");
    static Symbol global_options_button_corrupt_overwrite(
        "global_options_button_corrupt_overwrite"
    );
    static Symbol mc_button_delete_saves("mc_button_delete_saves");
    static Symbol mc_button_yes("mc_button_yes");
    Symbol out = gNullStr;
    switch (mState) {
    case kS_AutoloadNoSaveFound_Msg:
        out = mc_button_create_data;
        break;
    case kS_AutoloadCorrupt:
    case kS_AutoloadNotOwner:
    case kS_AutoloadObsolete:
    case kS_AutoloadFuture:
    case kS_SaveConfirmOverwrite:
        out = mc_button_overwrite;
        break;
    case kS_SongCacheCreateNotFound_Msg:
    case kS_SongCacheCreateMissing_Msg:
        out = song_info_cache_button_create;
        break;
    case kS_SongCacheCreateCorrupt:
        out = song_info_cache_button_corrupt_overwrite;
        break;
    case kS_GlobalCreateCorrupt:
        out = global_options_button_corrupt_overwrite;
        break;
    case kS_GlobalCreateNotFound_Msg:
    case kS_GlobalCreateMissing_Msg:
    case kS_GlobalOptionsMissing_Msg:
        out = global_options_button_create;
        break;
    case kS_SaveNotEnoughSpacePS3:
        out = mc_button_delete_saves;
        break;
    case kS_AutoloadMultipleSavesFound:
    case kS_AutoloadDeviceMissing:
    case kS_SaveDeviceInvalid:
    case kS_ManualSaveNoDevice:
    case kS_ManualLoadNoDevice:
        out = mc_button_choose_device;
        break;
    case kS_ManualLoadConfirmUnsaved:
        out = mc_button_continue;
        break;
    case kS_ManualLoadConfirm:
        out = mc_button_yes;
        break;
    default:
        break;
    }
    return out;
}

Symbol SaveLoadManager::GetDialogOpt2() {
    static Symbol mc_button_cancel("mc_button_cancel");
    static Symbol mc_button_continue_no_save("mc_button_continue_no_save");
    static Symbol song_info_cache_button_cancel("song_info_cache_button_cancel");
    static Symbol global_options_button_cancel("global_options_button_cancel");
    static Symbol mc_button_retry("mc_button_retry");
    static Symbol mc_button_disable_autosave("mc_button_disable_autosave");
    static Symbol mc_button_no("mc_button_no");
    Symbol out = gNullStr;
    switch (mState) {
    case kS_AutoloadCorrupt:
    case kS_AutoloadNotOwner:
    case kS_AutoloadObsolete:
    case kS_AutoloadFuture:
        out = mc_button_continue_no_save;
        break;
    case kS_SongCacheCreateNotFound_Msg:
    case kS_SongCacheCreateMissing_Msg:
    case kS_SongCacheCreateCorrupt:
        out = song_info_cache_button_cancel;
        break;
    case kS_GlobalCreateNotFound_Msg:
    case kS_GlobalCreateMissing_Msg:
    case kS_GlobalCreateCorrupt:
    case kS_GlobalOptionsMissing_Msg:
        out = global_options_button_cancel;
        break;
    case kS_SaveNotEnoughSpacePS3:
        out = mc_button_retry;
        break;
    case kS_SaveDeviceInvalid:
        out = mc_button_disable_autosave;
        break;
    case kS_ManualLoadConfirm:
        out = mc_button_no;
        break;
    case kS_AutoloadNoSaveFound_Msg:
    case kS_AutoloadMultipleSavesFound:
    case kS_AutoloadDeviceMissing:
    case kS_SaveConfirmOverwrite:
    case kS_ManualSaveNoDevice:
    case kS_ManualLoadConfirmUnsaved:
    case kS_ManualLoadNoDevice:
        out = mc_button_cancel;
        break;
    default:
        break;
    }
    return out;
}

void SaveLoadManager::Init() {
    MILO_ASSERT(!TheSaveLoadMgr, 0x47);
    TheSaveLoadMgr = new SaveLoadManager();
}

HamProfile *SaveLoadManager::GetAutosavableProfile() {
    std::vector<HamProfile *> shouldAutosaves = TheProfileMgr.GetShouldAutosave();
    if (!shouldAutosaves.empty()) {
        HamProfile *pProfile = shouldAutosaves[0];
        MILO_ASSERT(pProfile, 0x401);
        return pProfile;
    } else {
        return nullptr;
    }
}

HamProfile *SaveLoadManager::GetNewSigninProfile() {
    std::vector<HamProfile *> signIns = TheProfileMgr.GetNewlySignedIn();
    if (!signIns.empty()) {
        HamProfile *pProfile = signIns[0];
        MILO_ASSERT(pProfile, 0x3F2);
        return pProfile;
    } else {
        return nullptr;
    }
}

DataNode SaveLoadManager::OnMsg(const DeviceChosenMsg &msg) {
    MILO_ASSERT(mWaiting, 0x887);
    mWaiting = false;
    switch (mState) {
    case kS_ManualSaveChooseDevice:
        mLastChosenDeviceID = msg.Device();
        SetState(kS_SaveLookForFile);
        break;
    case 8:
    case 9:
    case 0xA:
    case 0xD:
        mLastChosenDeviceID = msg.Device();
        SetState(kS_AutoloadStartLoad);
        break;
    case kS_SaveChooseDeviceInvalid:
        SetState(kS_SaveNoOverwrite);
        break;
    case kS_ManualLoadChooseDevice:
        SetState(kS_ManualLoadStartLoad);
        break;
    case kS_Abort:
    case kS_Done:
    case kS_Finish:
        break;
    default:
        State state = mState;
        SaveLoadMode mode = mMode;
        MILO_FAIL("Unhandled DeviceChosenMsg in state %d and mode %d", state, mode);
        break;
    }
    return 0;
}

DataNode SaveLoadManager::OnMsg(const NoDeviceChosenMsg &msg) {
    MILO_ASSERT(mWaiting, 0x8b9);
    mWaiting = false;
    switch (mState) {
    case kS_SaveChooseDeviceInvalid:
        SetState(kS_SaveDeviceInvalid);
        break;
    case kS_AutoloadSetDevice:
        SetState(kS_AutoloadNoSaveFound_Msg);
        break;
    case kS_AutoloadSelectDevice2:
        SetState(kS_AutoloadMultipleSavesFound);
        break;
    case kS_AutoloadSelectDevice3:
        SetState(kS_AutoloadDeviceMissing);
        break;
    case kS_ManualSaveChooseDevice:
        SetState(kS_ManualSaveNoDevice);
        break;
    case kS_ManualLoadChooseDevice:
        SetState(kS_ManualLoadNoDevice);
        break;
    case kS_Abort:
    case kS_Done:
    case kS_Finish:
        break;
    default:
        State state = mState;
        SaveLoadMode mode = mMode;
        MILO_FAIL("Unhandled NoDeviceChosenMsg in state %d and mode %d", state, mode);
        break;
    }
    return 0;
}

DataNode SaveLoadManager::OnMsg(const EventDialogDismissMsg &msg) {
    static Symbol saveload_dialog_event("saveload_dialog_event");
    Symbol s2 = msg->ForceSym(2);
    Symbol s3 = msg->ForceSym(3);
    if (s3 != gNullStr && s2 == saveload_dialog_event && s3 != saveload_dialog_event) {
        SetState(kS_Abort);
    }
    return DATA_UNHANDLED;
}

DataNode SaveLoadManager::GetDialogMsg() {
    String profileName = gNullStr;
    int playerNum = -1;
    if (mActiveProfile) {
        profileName = mActiveProfile->GetName();
        playerNum = mActiveProfile->GetPadNum() + 1;
    }
    switch (mState) {
    case kS_AutoloadNoSaveFound_Msg: {
        static Symbol mc_auto_load_no_save_found_fmt("mc_auto_load_no_save_found_fmt");
        return DataArrayPtr(mc_auto_load_no_save_found_fmt, DataArrayPtr(), profileName);
    }
    case kS_AutoloadMultipleSavesFound: {
        static Symbol mc_auto_load_multiple_saves_found_fmt(
            "mc_auto_load_multiple_saves_found_fmt"
        );
        return DataArrayPtr(
            mc_auto_load_multiple_saves_found_fmt, DataArrayPtr(), profileName
        );
    }
    case kS_AutoloadDeviceMissing: {
        static Symbol mc_load_device_missing_fmt("mc_load_device_missing_fmt");
        return DataArrayPtr(mc_load_device_missing_fmt, DataArrayPtr(), profileName);
    }
    case kS_AutoloadCorrupt: {
        static Symbol mc_auto_load_corrupt("mc_auto_load_corrupt");
        HamProfile *pProfile = mActiveProfile;
        MILO_ASSERT(pProfile, 0xAD6);
        return DataArrayPtr(
            mc_auto_load_corrupt,
            DataArrayPtr(),
            ThePlatformMgr.GetName(pProfile->GetPadNum())
        );
    }
    case kS_AutoloadNotOwner: {
        static Symbol mc_auto_load_not_owner("mc_auto_load_not_owner");
        return DataArrayPtr(mc_auto_load_not_owner, DataArrayPtr());
    }
    case kS_AutoloadObsolete: {
        if (playerNum != -1) {
            static Symbol mc_auto_load_obsolete_version_fmt(
                "mc_auto_load_obsolete_version_fmt"
            );
            return DataArrayPtr(
                mc_auto_load_obsolete_version_fmt, DataArrayPtr(), profileName
            );
        } else {
            static Symbol mc_auto_load_obsolete_version("mc_auto_load_obsolete_version");
            return DataArrayPtr(mc_auto_load_obsolete_version, DataArrayPtr());
        }
    }
    case kS_AutoloadFuture: {
        if (playerNum != -1) {
            static Symbol mc_auto_load_newer_version_fmt("mc_auto_load_newer_version_fmt");
            return DataArrayPtr(
                mc_auto_load_newer_version_fmt, DataArrayPtr(), profileName
            );
        } else {
            static Symbol mc_auto_load_newer_version("mc_auto_load_newer_version");
            return DataArrayPtr(mc_auto_load_newer_version, DataArrayPtr());
        }
    }
    case kS_SongCacheCreateNotFound_Msg: {
        static Symbol song_info_cache_create("song_info_cache_create");
        return DataArrayPtr(song_info_cache_create, DataArrayPtr());
    }
    case kS_SongCacheCreateMissing_Msg: {
        static Symbol song_info_cache_missing("song_info_cache_missing");
        return DataArrayPtr(song_info_cache_missing, DataArrayPtr());
    }
    case kS_SongCacheCreateCorrupt: {
        static Symbol song_info_cache_corrupt("song_info_cache_corrupt");
        return DataArrayPtr(song_info_cache_corrupt, DataArrayPtr());
    }
    case kS_GlobalCreateNotFound_Msg: {
        static Symbol global_options_create("global_options_create");
        return DataArrayPtr(global_options_create, DataArrayPtr());
    }
    case kS_GlobalCreateMissing_Msg:
    case kS_GlobalOptionsMissing_Msg: {
        static Symbol global_options_missing("global_options_missing");
        return DataArrayPtr(global_options_missing, DataArrayPtr());
    }
    case kS_GlobalCreateCorrupt: {
        static Symbol global_options_corrupt("global_options_corrupt");
        return DataArrayPtr(global_options_corrupt, DataArrayPtr());
    }
    case kS_SaveLoadError: {
        static Symbol mc_autosave_disabled("mc_autosave_disabled");
        return DataArrayPtr(mc_autosave_disabled, DataArrayPtr());
    }
    case kS_SaveConfirmOverwrite: {
        static Symbol mc_save_confirm_overwrite("mc_save_confirm_overwrite");
        return DataArrayPtr(mc_save_confirm_overwrite, DataArrayPtr());
    }
    case kS_SaveNotEnoughSpace: {
        static Symbol mc_save_not_enough_space("mc_save_not_enough_space");
        return DataArrayPtr(mc_save_not_enough_space, DataArrayPtr());
    }
    case kS_SaveNotEnoughSpacePS3: {
        static Symbol mc_save_not_enough_space("mc_save_not_enough_space");
        return DataArrayPtr(
            mc_save_not_enough_space, DataArrayPtr(), -TheMemcardMgr.GetSizeNeeded()
        );
    }
    case kS_SaveDeviceInvalid: {
        static Symbol mc_save_device_missing_fmt("mc_save_device_missing_fmt");
        return DataArrayPtr(mc_save_device_missing_fmt, DataArrayPtr(), profileName);
    }
    case kS_SaveFailed: {
        static Symbol mc_save_failed("mc_save_failed");
        return DataArrayPtr(mc_save_failed, DataArrayPtr());
    }
    case kS_SaveDisabledByCheat: {
        static Symbol mc_save_disabled_by_cheat("mc_save_disabled_by_cheat");
        return DataArrayPtr(mc_save_disabled_by_cheat, DataArrayPtr());
    }
    case kS_LoadFailed: {
        static Symbol mc_load_failed("mc_load_failed");
        return DataArrayPtr(mc_load_failed, DataArrayPtr());
    }
    case kS_ManualSaveNoDevice: {
        static Symbol mc_manual_save_no_selection("mc_manual_save_no_selection");
        return DataArrayPtr(mc_manual_save_no_selection, DataArrayPtr());
    }
    case kS_ManualLoadConfirmUnsaved: {
        if (playerNum != -1) {
            static Symbol mc_manual_load_confirm_unsaved_fmt(
                "mc_manual_load_confirm_unsaved_fmt"
            );
            return DataArrayPtr(
                mc_manual_load_confirm_unsaved_fmt, DataArrayPtr(), profileName
            );
        } else {
            static Symbol mc_manual_load_confirm_unsaved("mc_manual_load_confirm_unsaved");
            return DataArrayPtr(mc_manual_load_confirm_unsaved, DataArrayPtr());
        }
    }
    case kS_ManualLoadConfirm: {
        static Symbol mc_manual_load_confirm("mc_manual_load_confirm");
        return DataArrayPtr(mc_manual_load_confirm, DataArrayPtr());
    }
    case kS_ManualLoadNoDevice: {
        static Symbol mc_manual_load_no_selection("mc_manual_load_no_selection");
        return DataArrayPtr(mc_manual_load_no_selection, DataArrayPtr());
    }
    case kS_ManualLoadMissing: {
        static Symbol mc_manual_load_storage_missing("mc_manual_load_storage_missing");
        return DataArrayPtr(mc_manual_load_storage_missing, DataArrayPtr());
    }
    case kS_ManualLoadNoFile: {
        static Symbol mc_manual_load_no_file("mc_manual_load_no_file");
        return DataArrayPtr(mc_manual_load_no_file, DataArrayPtr());
    }
    case kS_ManualLoadCorrupt: {
        static Symbol mc_manual_load_corrupt("mc_manual_load_corrupt");
        return DataArrayPtr(mc_manual_load_corrupt, DataArrayPtr());
    }
    case kS_ManualLoadNotOwner: {
        static Symbol mc_manual_load_not_owner("mc_manual_load_not_owner");
        return DataArrayPtr(mc_manual_load_not_owner, DataArrayPtr());
    }
    default: {
        MILO_ASSERT(false, 0xB73);
        return 0;
    }
    }
}

void SaveLoadManager::Poll() {
    if (!mActivated) {
        return;
    }

    if (kS_Idle == mState) {
        if (mNeedsSave && IsSafePlaceToSave()) {
            mMode = kAutoSave;
            Start();
            mNeedsSave = false;
            return;
        }

        if (mNeedsLoad && IsSafePlaceToLoad()) {
            mMode = kAutoLoad;
            Start();
            mNeedsLoad = false;
            return;
        }

        bool hasActiveDialog = TheUIEventMgr->HasActiveDialogEvent();
        if (hasActiveDialog) {
            return;
        }

        TheProfileMgr.PurgeOldData();

        if (IsReasonToAutoload()) {
            mNeedsLoad = true;
        }
        return;
    }

    State nextState;
    switch (mState) {
    case kS_Start: {
        unsigned int mode = mMode;
        if (mode < kAutoSave) {
            nextState = kS_AutoloadInit;
        } else if (mode == kAutoSave) {
            nextState = kS_SaveDone;
        } else if (mode >= kManualDelete) {
            MILO_NOTIFY("SaveLoadManager startup bad mode: %d", mMode);
            nextState = kS_Done;
        } else {
            nextState = kS_SaveLoadError;
        }
        break;
    }
    case kS_AutoloadSearchDevice:
        if (mWaiting) {
            return;
        }
        {
            int deviceState = unk64;
                        switch (deviceState) {
                case 7:
                nextState = kS_AutoloadStartLoad;
                break;
                case 8:
                nextState = kS_AutoloadDeviceFound;
                break;
                case 9:
                nextState = kS_AutoloadMultipleSavesFound;
                break;
                default:
                nextState = kS_SaveLoadError;
                break;
            }
        }
        break;
    case kS_SongCacheSearch: {
        if (!TheCacheMgr->IsDone()) {
            return;
        }
        unk68 = TheCacheMgr->GetLastResult();
        if (unk68 != 0) {
            if (unk68 != 6) {
                MILO_NOTIFY("SaveLoadManager - CacheMgr search returned error %d", unk68);
                nextState = kS_GlobalOptionsSearch;
            } else {
                nextState = kS_SongCacheSearchResult;
            }
        } else {
            const char *cacheName = mCacheName.c_str();
            TheCacheMgr->AddCacheID(mCacheID, cacheName);
            nextState = kS_SongCacheRead;
        }
        break;
    }
    case kS_SongCacheMount: {
        if (!TheCacheMgr->IsDone()) {
            return;
        }
        CacheResult result = TheCacheMgr->GetLastResult();
        if (result != 0) {
            if (result != 4) {
                MILO_FAIL("SaveLoadManager - CacheMgr choose returned error %d", result);
                nextState = kS_SongCacheFailed;
            } else {
                nextState = kS_SongCacheCreateNotFound_Msg;
                mDeviceIDState = 1;
            }
        } else {
            mDeviceIDState = 2;
            mLastChosenDeviceID = mCacheID->GetDeviceID();
            Symbol sym(mCacheName.c_str());
            TheCacheMgr->AddCacheID(mCacheID, sym);
            nextState = kS_SongCacheUnmount;
        }
        break;
    }
    case kS_SongCacheMountStart: {
        if (ThePlatformMgr.GuideShowing()) {
            return;
        }
        nextState = kS_SongCacheMount;
        break;
    }
    case kS_SongCacheRead: {
        if (!TheCacheMgr->IsDone()) {
            return;
        }
        CacheResult result = TheCacheMgr->GetLastResult();
                switch (result) {
            case 0:
            nextState = kS_SongCacheAllocRead;
            break;
            case 7:
            nextState = kS_SongCacheCreateCorrupt;
            break;
            case 8:
            nextState = kS_SongCacheCreate;
            break;
            default:
            MILO_FAIL("SaveLoadManager - kS_SongCacheCreateMountRead unhandled error %d", result);
            nextState = kS_SongCacheFailed;
            break;
        }
        break;
    }
    case kS_SongCacheGetSize: {
        if (!TheCacheMgr->IsDone()) {
            return;
        }
        UpdateStatus((SaveLoadMgrStatus)2);
        nextState = kS_SongCacheUnmount;
        break;
    }
    case kS_SongCacheAllocRead: {
        if (!mCache->IsDone()) {
            return;
        }
        CacheResult result = mCache->GetLastResult();
        if (result == 0) {
            nextState = kS_SongCacheWrite;
        } else {
            if (result == 6) {
                nextState = kS_SongCacheCreateCorrupt;
            } else if (result == 8) {
                nextState = kS_SongCacheCreate;
            } else {
                nextState = kS_GlobalOptionsSearch;
            }
        }
        break;
    }
    case kS_SongCacheWrite: {
        if (!mCache->IsDone()) {
            return;
        }
        CacheResult result = mCache->GetLastResult();
        if (result == 0) {
            BufStream bs(mData, mCacheFileSize, true);
            TheSongMgr.LoadCachedSongInfo(bs);
            SetState(kS_SongCacheFailed);
            return;
        }
        nextState = kS_GlobalOptionsSearch;
        break;
    }
    case kS_SongCacheUnmount: {
        if (!TheCacheMgr->IsDone()) {
            return;
        }
        CacheResult result = TheCacheMgr->GetLastResult();
                switch (result) {
            case 0:
            nextState = kS_SongCacheDone;
            break;
            case 7:
            UpdateStatus((SaveLoadMgrStatus)2);
            nextState = kS_SongCacheCreateCorrupt;
            break;
            case 8:
            UpdateStatus((SaveLoadMgrStatus)2);
            nextState = kS_SongCacheCreate;
            break;
            default:
            UpdateStatus((SaveLoadMgrStatus)2);
            MILO_FAIL("SaveLoadManager - kS_SongCacheCreateMountWrite unhandled error %d", result);
            nextState = kS_GlobalCacheLookup;
            break;
        }
        break;
    }
    case kS_SongCacheDone:
    case kS_GlobalDoneWrite:
    case kS_GlobalOptionsWrite: {
        if (!mCache->IsDone()) {
            return;
        }
        unk68 = mCache->GetLastResult();
                switch (mState) {
            case kS_SongCacheDone:
            nextState = kS_SongCacheLookup;
            break;
            case kS_GlobalDoneWrite:
            nextState = kS_GlobalDone;
            break;
            case kS_GlobalOptionsWrite:
            nextState = kS_GlobalOptionsUnmount;
            break;
            default:
            FormatString fs("Impossible state.");
            TheDebug.Fail(fs.Str(), 0);
            break;
        }
        break;
    }
    case kS_SongCacheFailed: {
        if (!TheCacheMgr->IsDone()) {
            return;
        }
        CacheResult result = TheCacheMgr->GetLastResult();
        if (result == 0) {
            nextState = kS_GlobalOptionsSearchResult;
        } else {
            nextState = kS_GlobalOptionsSearch;
        }
        break;
    }
    case kS_SongCacheLookup: {
        if (!TheCacheMgr->IsDone()) {
            return;
        }
        UpdateStatus((SaveLoadMgrStatus)2);
        if (unk68 == 0) {
            CacheResult result = TheCacheMgr->GetLastResult();
            unk68 = result;
        }
        if (unk68 == 0) {
            nextState = kS_GlobalOptionsSearchResult;
        } else {
            nextState = kS_GlobalOptionsSearch;
        }
        break;
    }
    case kS_GlobalOptionsCreate: {
        if (!TheCacheMgr->IsDone()) {
            return;
        }
        unk68 = TheCacheMgr->GetLastResult();
        if (unk68 == 0) {
            Symbol sym(kStrGlobalCacheName);
            TheCacheMgr->AddCacheID(mCacheID, sym);
            nextState = kS_GlobalMount2;
        } else if (unk68 == 6) {
            if (mDeviceIDState == 0) {
                nextState = kS_GlobalMount;
            } else if (mDeviceIDState != 2) {
                nextState = kS_GlobalCreateNotFound_Msg;
            } else {
                nextState = kS_GlobalMountStart;
            }
        } else {
            MILO_FAIL("SaveLoadManager - CacheMgr search returned error %d", unk68);
            nextState = kS_GlobalCacheLookup;
        }
        break;
    }
    case kS_GlobalMount:
    case kS_GlobalMountStart: {
        if (!TheCacheMgr->IsDone()) {
            return;
        }
        CacheResult result = TheCacheMgr->GetLastResult();
        if (result == 0) {
            mDeviceIDState = 2;
            mLastChosenDeviceID = mCacheID->GetDeviceID();
            Symbol sym(kStrGlobalCacheName);
            TheCacheMgr->AddCacheID(mCacheID, sym);
            nextState = kS_GlobalDoneRead;
        } else if (result == 4) {
            nextState = kS_GlobalCreateNotFound_Msg;
            mDeviceIDState = 1;
        } else {
            MILO_FAIL("SaveLoadManager - CacheMgr choose returned error %d", result);
            nextState = kS_GlobalCacheLookup;
        }
        break;
    }
    case kS_GlobalCreate2: {
        if (ThePlatformMgr.GuideShowing()) {
            return;
        }
        nextState = kS_GlobalMount;
        break;
    }
    case kS_GlobalMount2: {
        if (!TheCacheMgr->IsDone()) {
            return;
        }
        CacheResult result = TheCacheMgr->GetLastResult();
                switch (result) {
            case 0:
            nextState = kS_GlobalWrite;
            break;
            case 7:
            nextState = kS_GlobalCreateCorrupt;
            break;
            case 8:
            nextState = kS_GlobalOptionsLookup;
            break;
            default:
            int errCode = (int)result;
            State errState = mState;
            MILO_NOTIFY("SaveLoadManager - unknown error %d during state %d.", errCode, errState);
            nextState = kS_GlobalCacheLookup;
            break;
        }
        break;
    }
    case kS_GlobalRead: {
        if (!TheCacheMgr->IsDone()) {
            return;
        }
        UpdateStatus((SaveLoadMgrStatus)2);
        nextState = kS_GlobalDoneRead;
        break;
    }
    case kS_GlobalDoneRead: {
        if (!TheCacheMgr->IsDone()) {
            return;
        }
        CacheResult result = TheCacheMgr->GetLastResult();
                switch (result) {
            case 0:
            nextState = kS_GlobalDoneWrite;
            break;
            case 7:
            UpdateStatus((SaveLoadMgrStatus)2);
            nextState = kS_GlobalCreateCorrupt;
            break;
            case 8:
            UpdateStatus((SaveLoadMgrStatus)2);
            nextState = kS_GlobalOptionsLookup;
            break;
            default:
            UpdateStatus((SaveLoadMgrStatus)2);
            int errCode = (int)result;
            State errState = mState;
            MILO_NOTIFY("SaveLoadManager - unknown error %d during state %d.", errCode, errState);
            nextState = kS_GlobalCacheLookup;
            break;
        }
        break;
    }
    case kS_GlobalWrite: {
        if (!mCache->IsDone()) {
            return;
        }
        CacheResult result = mCache->GetLastResult();
        if (result == 0) {
            unsigned long saveSize = TheProfileMgr.GlobalOptionsSaveSize();
            FixedSizeSaveableStream fs(mData, saveSize, true);
            TheProfileMgr.LoadGlobalOptions(fs);
            TheProfileMgr.SetGlobalOptionsSaveState((ProfileSaveState)1);
        } else {
            TheProfileMgr.SetGlobalOptionsSaveState((ProfileSaveState)2);
        }
        nextState = kS_GlobalUnmount;
        break;
    }
    case kS_GlobalUnmount: {
        if (!TheCacheMgr->IsDone()) {
            return;
        }
        CacheResult result = TheCacheMgr->GetLastResult();
        if (result != 0) {
            TheProfileMgr.SetGlobalOptionsSaveState((ProfileSaveState)2);
        } else {
            TheProfileMgr.SetGlobalOptionsSaveState((ProfileSaveState)1);
        }
        nextState = kS_GlobalNewSignIns;
        break;
    }
    case kS_GlobalDone: {
        if (!TheCacheMgr->IsDone()) {
            return;
        }
        UpdateStatus((SaveLoadMgrStatus)2);
        if (unk68 == 0) {
            CacheResult result = TheCacheMgr->GetLastResult();
            unk68 = result;
        }
        if (unk68 == 0) {
            TheProfileMgr.SetGlobalOptionsSaveState((ProfileSaveState)1);
        } else {
            TheProfileMgr.SetGlobalOptionsSaveState((ProfileSaveState)2);
        }
        nextState = kS_GlobalNewSignIns;
        break;
    }
    case kS_GlobalOptionsCreate2: {
        if (!TheCacheMgr->IsDone()) {
            return;
        }
        CacheResult result = TheCacheMgr->GetLastResult();
        if (result != 0) {
            if (result != 4) {
                MILO_NOTIFY("SaveLoadManager - CacheMgr choose returned error %d", result);
                nextState = kS_GlobalOptionsFailed;
            } else {
                nextState = kS_GlobalOptionsMissing_Msg;
                mDeviceIDState = 1;
            }
        } else {
            mDeviceIDState = 2;
            mLastChosenDeviceID = mCacheID->GetDeviceID();
            Symbol sym(kStrGlobalCacheName);
            TheCacheMgr->AddCacheID(mCacheID, sym);
            nextState = kS_GlobalOptionsAllocRead;
        }
        break;
    }
    case kS_GlobalOptionsRead: {
        if (ThePlatformMgr.GuideShowing()) {
            return;
        }
        nextState = kS_GlobalOptionsCreate2;
        break;
    }
    case kS_GlobalOptionsAllocRead: {
        if (!TheCacheMgr->IsDone()) {
            return;
        }
        CacheResult result = TheCacheMgr->GetLastResult();
        if (result != 0) {
            if (result != 8) {
                UpdateStatus((SaveLoadMgrStatus)2);
                MILO_FAIL("SaveLoadManager - CacheMgr choose returned error %d", result);
                nextState = kS_GlobalOptionsFailed;
            } else {
                UpdateStatus((SaveLoadMgrStatus)2);
                nextState = kS_GlobalOptionsMissing_Msg;
            }
        } else {
            nextState = kS_GlobalOptionsWrite;
        }
        break;
    }
    case kS_GlobalOptionsUnmount: {
        if (!TheCacheMgr->IsDone()) {
            return;
        }
        UpdateStatus((SaveLoadMgrStatus)2);
        if (unk68 == 0) {
            CacheResult result = TheCacheMgr->GetLastResult();
            unk68 = result;
        }
        if (unk68 != 0) {
            TheProfileMgr.SetGlobalOptionsSaveState((ProfileSaveState)2);
        } else {
            TheProfileMgr.SetGlobalOptionsSaveState((ProfileSaveState)1);
        }
        nextState = kS_GlobalOptionsDone;
        break;
    }
    case kS_SaveOverwrite:
    case kS_SaveNoOverwrite:
        if (mWaiting) {
            return;
        }
        UpdateStatus((SaveLoadMgrStatus)2);
        {
            unsigned int deviceState = unk64;
            switch (deviceState) {
            case 0:
                nextState = kS_SaveLoadError2;
                break;
            case 1:
                nextState = kS_SaveDeviceInvalid;
                break;
            case 6:
                nextState = kS_SaveNotEnoughSpace;
                mDeviceIDState = 0;
                mLastChosenDeviceID = 0;
                break;
            case 7:
                MILO_ASSERT(mState != kS_SaveOverwrite, 0x2ED);
                nextState = kS_SaveConfirmOverwrite;
                break;
            default:
                nextState = kS_SaveFailed;
                break;
            }
        }
        break;
    case kS_SaveSongCache:
        if (!TheSongMgr.IsSongCacheWriteDone()) {
            return;
        }
        if (TheProfileMgr.GlobalOptionsNeedsSave()) {
            nextState = kS_SaveGlobalOptions;
        } else {
            nextState = kS_SaveCheckProfile;
        }
        break;
    case kS_Abort:
    case kS_Finish:
        if (mWaiting) {
            return;
        }
        if (mCache) {
            if (!mCache->IsDone()) {
                return;
            }
            TheCacheMgr->UnmountAsync(&mCache, nullptr);
            return;
        }
        if (!TheCacheMgr->IsDone()) {
            return;
        }
        if (mState == kS_Abort) {
            nextState = kS_Done;
        } else {
            nextState = kS_Idle;
        }
        break;
    default:
        return;
    }
    SetState(nextState);
}

void SaveLoadManager::SetState(State newState) {
    auto& state = mState;
    if ((int)(int)state == newState)
        return;

    static Symbol saveload_dialog_event("saveload_dialog_event");

    bool wasIdle = false;

    // Cleanup resources based on current state before transition
    // WARNING: Control flow structure is critical for codegen - do not refactor
    if (state <= kS_GlobalOptionsWrite) {
        if ((kS_GlobalOptionsWrite == state)
            || ((state == kS_SongCacheWrite) || (state == kS_SongCacheDone))
            || ((state > kS_GlobalDoneRead) && (state < kS_GlobalUnmount))) {
            // 0x3E, 0x1F, 0x21, or 0x32-0x33: free mData unless going to Finish
            if ((newState != kS_Finish) && mData) {
                MemFree(mData, "SaveLoadManager.cpp", 0x424);
                mData = nullptr;
            }
        } else if (state == kS_Idle) {
            // 0: set wasIdle flag
            wasIdle = true;
        } else if (kS_AutoloadStartLoad == state) {
            // 0xB: release mAction unless going to Abort
            if (newState != kS_Abort) {
                RELEASE(mAction);
            }
        }
    } else if (state >= kS_SaveOverwrite) {  // >= 0x46
        if ((state <= kS_SaveNoOverwrite) || (state == kS_ManualLoadStartLoad)) {
            // (mState < 0x48) || (mState == 0x60): release mAction unless going to Abort
            if (newState != kS_Abort) {
                RELEASE(mAction);
            }
        } else {
            if (state == kS_Abort) {
                // 0x65: release mAction unconditionally
                RELEASE(mAction);
            } else if ((state == kS_Finish) && mData) {
                // 0x67: free mData
                MemFree(mData, "SaveLoadManager.cpp", 0x433);
                mData = nullptr;
            }
        }
    }

    state = newState;

    if (wasIdle) {
        UpdateStatus((SaveLoadMgrStatus)0);
    }

    // Handle state based on new state value
    switch (state) {
    case kS_Idle:
        UpdateStatus((SaveLoadMgrStatus)5);
        break;
    case kS_Start:
        mDeviceIDState = 0;
        break;
    case kS_AutoloadInit:
        if (mInitialLoadPending) {
            SetState(kS_SongCacheInit);
        } else {
            SetState(kS_AutoloadSelectProfile);
        }
        break;
    case kS_AutoloadSelectProfile:
        mActiveProfile = GetNewSigninProfile();
        if (!mActiveProfile) {
            SetState(kS_AutoloadDone);
        } else {
            SetState(kS_AutoloadSearchDevice);
        }
        break;
    case kS_AutoloadSearchDevice: {
        HamProfile *pProfile = mActiveProfile;
        MILO_ASSERT(pProfile, 0x48B);
        mWaiting = true;
        TheMemcardMgr.OnSearchForDevice(pProfile);
        break;
    }
    case kS_AutoloadDeviceFound:
        if (mDeviceIDState == 2) {
            SetState(kS_AutoloadSetDevice);
        } else {
            SetState(kS_AutoloadNoSaveFound_Msg);
        }
        break;
    case kS_AutoloadNoSaveFound_Msg:
        // Dialog state - wait for user response
        break;
    case kS_AutoloadMultipleSavesFound:
        // Dialog state - wait for user response
        break;
    case kS_AutoloadSetDevice:
        MILO_ASSERT(mDeviceIDState == 2, 0x4AB);
        TheMemcardMgr.SetDevice(mLastChosenDeviceID);
        SetState(kS_AutoloadStartLoad);
        break;
    case kS_AutoloadSelectDevice: {
        HamProfile *pProfile = mActiveProfile;
        MILO_ASSERT(pProfile, 0x4B6);
        mWaiting = true;
        TheMemcardMgr.SelectDevice(pProfile, this, mPadNum, false);
        break;
    }
    case kS_AutoloadSelectDevice2: {
        HamProfile *pProfile = mActiveProfile;
        MILO_ASSERT(pProfile, 0x4C7);
        mWaiting = true;
        TheMemcardMgr.SelectDevice(pProfile, this, mPadNum, false);
        break;
    }
    case kS_AutoloadStartLoad: {
        HamProfile *pProfile = mActiveProfile;
        MILO_ASSERT(pProfile, 0x4D6);
        mWaiting = true;
        RELEASE(mAction);
        mAction = new LoadMemcardAction(pProfile);
        pProfile->PreLoad();
        TheMemcardMgr.OnLoadGame(pProfile, mAction);
        break;
    }
    case kS_AutoloadDeviceMissing:
        // Dialog state
        break;
    case kS_AutoloadSelectDevice3: {
        HamProfile *pProfile = mActiveProfile;
        MILO_ASSERT(pProfile, 0x4B6);
        mWaiting = true;
        TheMemcardMgr.SelectDevice(pProfile, this, mPadNum, true);
        break;
    }
    case kS_AutoloadCorrupt:
    case kS_AutoloadNotOwner:
    case kS_AutoloadObsolete:
    case kS_AutoloadFuture:
        // Dialog states
        break;
    case kS_AutoloadDone:
        mInitialLoadPending = false;
        if (TheProfileMgr.GlobalOptionsNeedsSave()) {
            SetState(kS_SongCacheInit);
        } else {
            TheProfileMgr.HandleProfileLoadComplete();
            SetState(kS_Done);
        }
        break;
    case kS_SongCacheInit: {
        mCacheName = TheSongMgr.GetCachedSongInfoName();
        if (mCacheID) {
            TheCacheMgr->RemoveCacheID(mCacheID);
            RELEASE(mCacheID);
        }
        if (!TheCacheMgr->SearchAsync(mCacheName.c_str(), &mCacheID)) {
            MILO_FAIL(
                "TheCacheMgr->SearchAsync() failed. CacheResult = %d",
                TheCacheMgr->GetLastResult()
            );
        }
        break;
    }
    case kS_SongCacheSearch:
        // Waiting for search result
        break;
    case kS_SongCacheSearchResult:
        SetState(kS_SongCacheMount);
        break;
    case kS_SongCacheCreate: {
        if (mCacheID) {
            TheCacheMgr->RemoveCacheID(mCacheID);
            RELEASE(mCacheID);
        }
        static Symbol song_info_cache_name("song_info_cache_name");
        const char *cacheName = Localize(song_info_cache_name, nullptr, TheLocale);
        if (!TheCacheMgr->ShowUserSelectUIAsync(
                nullptr, 0x25800, mCacheName.c_str(), cacheName, &mCacheID
            )) {
            int result = TheCacheMgr->GetLastResult();
            if (result != 0) {
                SetState(kS_SongCacheMountStart);
            }
        }
        break;
    }
    case kS_SongCacheCreateNotFound_Msg:
    case kS_SongCacheCreateMissing_Msg:
        // Dialog states
        break;
    case kS_SongCacheMount: {
        if (!TheCacheMgr->MountAsync(mCacheID, &mCache, nullptr)) {
            MILO_FAIL(
                "TheCacheMgr->MountAsync failed with CacheResult %d",
                TheCacheMgr->GetLastResult()
            );
        }
        break;
    }
    case kS_SongCacheMountStart:
        UpdateStatus((SaveLoadMgrStatus)1);
        // Fall through to mount logic handled elsewhere
        {
            if (!TheCacheMgr->MountAsync(mCacheID, &mCache, nullptr)) {
                MILO_FAIL(
                    "TheCacheMgr->MountAsync failed with CacheResult %d",
                    TheCacheMgr->GetLastResult()
                );
            }
        }
        break;
    case kS_SongCacheRead:
        UpdateStatus((SaveLoadMgrStatus)1);
        if (!TheCacheMgr->DeleteAsync(mCacheID)) {
            MILO_FAIL(
                "TheCacheMgr->DeleteAsync failed with CacheResult %d",
                TheCacheMgr->GetLastResult()
            );
        }
        break;
    case kS_SongCacheCreateCorrupt:
        // Dialog state
        break;
    case kS_SongCacheGetSize:
        if (!mCache->GetFileSizeAsync(mCacheName.c_str(), (unsigned int *)&mCacheFileSize, nullptr)) {
            MILO_FAIL(
                "mCache->GetFileSizeAsync failed with CacheResult %d",
                TheCacheMgr->GetLastResult()
            );
        }
        break;
    case kS_SongCacheAllocRead:
        mData = _MemAllocTemp(mCacheFileSize, "SaveLoadManager.cpp", 0x578, "SaveLoadManager", 0);
        if (!mCache->ReadAsync(mCacheName.c_str(), mData, mCacheFileSize, nullptr)) {
            MILO_FAIL(
                "mCache->ReadAsync failed with CacheResult %d",
                TheCacheMgr->GetLastResult()
            );
        }
        break;
    case kS_SongCacheWrite: {
        int size = TheSongMgr.GetCachedSongInfoSize();
        mData = _MemAllocTemp(size, "SaveLoadManager.cpp", 0x595, "SaveLoadManager", 0);
        BufStream bs(mData, size, true);
        if (TheSongMgr.SaveCachedSongInfo(bs)) {
            if (!mCache->WriteAsync(mCacheName.c_str(), mData, size, nullptr)) {
                MILO_FAIL(
                    "mCache->WriteAsync failed with CacheResult %d",
                    TheCacheMgr->GetLastResult()
                );
            }
        }
        break;
    }
    case kS_SongCacheUnmount:
        if (!TheCacheMgr->UnmountAsync(&mCache, nullptr)) {
            MILO_FAIL(
                "TheCacheMgr->UnmountAsync failed with CacheResult %d",
                TheCacheMgr->GetLastResult()
            );
        }
        break;
    case kS_SongCacheDone:
        mDeviceIDState = 1;
        mLastChosenDeviceID = 0;
        mSongCacheWriteDisabled = true;
        if (mCache) {
            SetState(kS_SongCacheLookup);
        } else {
            SetState((State)(kS_SongCacheLookup - 4));
        }
        break;
    case kS_SongCacheFailed:
        mDeviceIDState = 0;
        mLastChosenDeviceID = 0;
        mSongCacheWriteDisabled = true;
        if (mCache) {
            SetState(kS_SongCacheLookup);
        } else {
            SetState((State)(kS_SongCacheLookup - 4));
        }
        break;
    case kS_SongCacheLookup:
        mCacheID = nullptr;
        SetState(kS_GlobalOptionsCreate);
        break;
    case kS_GlobalOptionsInit: {
        if (!mCacheID) {
            Symbol globalCacheName(kStrGlobalCacheName);
            mCacheID = TheCacheMgr->GetCacheID(globalCacheName);
        }
        if (!mCacheID) {
            SetState(kS_GlobalCacheLookup);
        } else {
            SetState(kS_GlobalDoneRead);
        }
        break;
    }
    case kS_GlobalOptionsSearch: {
        if (mCacheID) {
            TheCacheMgr->RemoveCacheID(mCacheID);
            RELEASE(mCacheID);
        }
        if (!TheCacheMgr->SearchAsync(kStrGlobalCacheName, &mCacheID)) {
            MILO_FAIL(
                "TheCacheMgr->SearchAsync failed with CacheResult %d",
                TheCacheMgr->GetLastResult()
            );
        }
        break;
    }
    case kS_GlobalOptionsSearchResult:
        if (mDeviceIDState == 0) {
            mDeviceIDState = 0;
            SetState(kS_GlobalCreateMissing_Msg);
        } else {
            SetState(kS_GlobalMount);
        }
        break;
    case kS_GlobalOptionsCreate: {
        if (mCacheID) {
            TheCacheMgr->RemoveCacheID(mCacheID);
            RELEASE(mCacheID);
        }
        static Symbol global_options_cache_name("global_options_cache_name");
        int saveSize = TheProfileMgr.GlobalOptionsSaveSize();
        const char *cacheName = Localize(global_options_cache_name, nullptr, TheLocale);
        if (!TheCacheMgr->ShowUserSelectUIAsync(
                nullptr, saveSize, kStrGlobalCacheName, cacheName, &mCacheID
            )) {
            int result = TheCacheMgr->GetLastResult();
            if (result != 0) {
                SetState(kS_GlobalCreate2);
            }
        }
        break;
    }
    case kS_GlobalOptionsLookup: {
        if (mCacheID) {
            TheCacheMgr->RemoveCacheID(mCacheID);
            RELEASE(mCacheID);
        }
        Symbol globalCacheName(kStrGlobalCacheName);
        mCacheID = TheCacheMgr->GetCacheID(globalCacheName);
        if (!mCacheID) {
            SetState(kS_GlobalOptionsMissing_Msg);
        } else {
            SetState(kS_GlobalMount2);
        }
        break;
    }
    case kS_GlobalCreateNotFound_Msg:
    case kS_GlobalCreateMissing_Msg:
        // Dialog states
        break;
    case kS_GlobalMount:
        // Wait for mount
        break;
    case kS_GlobalMountStart:
        // Start mount
        break;
    case kS_GlobalCreate2:
        MILO_ASSERT(mDeviceIDState == 2, 0x627);
        // fall through to mount
        {
            if (mCacheID) {
                TheCacheMgr->RemoveCacheID(mCacheID);
                RELEASE(mCacheID);
            }
            static Symbol global_options_cache_name("global_options_cache_name");
            const char *cacheName =
                Localize(global_options_cache_name, nullptr, TheLocale);
            TheCacheMgr->CreateCacheIDFromDeviceID(
                mLastChosenDeviceID, kStrGlobalCacheName, cacheName, &mCacheID
            );
        }
        break;
    case kS_GlobalMount2:
        break;
    case kS_GlobalCreateCorrupt:
        // Dialog state
        break;
    case kS_GlobalRead: {
        int saveSize = TheProfileMgr.GlobalOptionsSaveSize();
        mData =
            _MemAllocTemp(saveSize, "SaveLoadManager.cpp", 0x69B, "SaveLoadManager", 0);
        if (!mCache->ReadAsync(kStrGlobalCacheName, mData, saveSize, nullptr)) {
            MILO_FAIL(
                "TheCacheMgr->ReadAsync failed with CacheResult %d",
                TheCacheMgr->GetLastResult()
            );
        }
        break;
    }
    case kS_GlobalDoneRead:
        // Handle read completion
        break;
    case kS_GlobalWrite: {
        UpdateStatus((SaveLoadMgrStatus)1);
        int saveSize = TheProfileMgr.GlobalOptionsSaveSize();
        mData =
            _MemAllocTemp(saveSize, "SaveLoadManager.cpp", 0x6AD, "SaveLoadManager", 0);
        FixedSizeSaveableStream fs(mData, saveSize, true);
        TheProfileMgr.SaveGlobalOptions(fs);
        if (!mCache->WriteAsync(kStrGlobalCacheName, mData, saveSize, nullptr)) {
            MILO_FAIL(
                "mCache->WriteAsync failed with CacheResult %d",
                TheCacheMgr->GetLastResult()
            );
        }
        break;
    }
    case kS_GlobalDoneWrite:
        // Handle write completion
        break;
    case kS_GlobalUnmount:
        if (!TheCacheMgr->UnmountAsync(&mCache, nullptr)) {
            int result = TheCacheMgr->GetLastResult();
            if (result != kCache_ErrorStorageDeviceMissing) {
                MILO_NOTIFY(
                    "UnmountAsync failed with error %d", TheCacheMgr->GetLastResult()
                );
            }
        }
        break;
    case kS_GlobalDone:
        mDeviceIDState = 1;
        mLastChosenDeviceID = 0;
        TheProfileMgr.SetGlobalOptionsSaveState((ProfileSaveState)2);
        SetState(kS_GlobalNewSignIns);
        break;
    case kS_GlobalFailed:
        mDeviceIDState = 0;
        mLastChosenDeviceID = 0;
        TheProfileMgr.SetGlobalOptionsSaveState((ProfileSaveState)2);
        SetState(kS_GlobalNewSignIns);
        break;
    case kS_GlobalCacheLookup:
        // Look up cache
        break;
    case kS_GlobalNewSignIns: {
        std::vector<HamProfile *> newSignIns = TheProfileMgr.GetNewlySignedIn();
        bool hasMultiple = newSignIns.size() > 1;
        if (hasMultiple) {
            mDeviceIDState = 1;
        }
        SetState(kS_AutoloadSelectProfile);
        break;
    }
    case kS_GlobalOptionsSearchResult2:
        if (mDeviceIDState == 0 || mDeviceIDState == 2) {
            SetState(kS_GlobalOptionsCreate2);
        } else {
            SetState(kS_GlobalOptionsMissing_Msg);
        }
        break;
    case kS_GlobalOptionsMissing_Msg:
        // Dialog state
        break;
    case kS_GlobalOptionsCreate2: {
        if (mCacheID) {
            TheCacheMgr->RemoveCacheID(mCacheID);
            RELEASE(mCacheID);
        }
        static Symbol global_options_cache_name("global_options_cache_name");
        int saveSize = TheProfileMgr.GlobalOptionsSaveSize();
        const char *cacheName = Localize(global_options_cache_name, nullptr, TheLocale);
        if (!TheCacheMgr->ShowUserSelectUIAsync(
                nullptr, saveSize, kStrGlobalCacheName, cacheName, &mCacheID
            )) {
            int result = TheCacheMgr->GetLastResult();
            if (result != 0) {
                SetState(kS_GlobalOptionsRead);
            }
        }
        break;
    }
    case kS_GlobalOptionsRead:
        // Read options
        break;
    case kS_GlobalOptionsAllocRead: {
        int saveSize = TheProfileMgr.GlobalOptionsSaveSize();
        mData =
            _MemAllocTemp(saveSize, "SaveLoadManager.cpp", 0x69B, "SaveLoadManager", 0);
        if (!mCache->ReadAsync(kStrGlobalCacheName, mData, saveSize, nullptr)) {
            MILO_FAIL(
                "TheCacheMgr->ReadAsync failed with CacheResult %d",
                TheCacheMgr->GetLastResult()
            );
        }
        break;
    }
    case kS_GlobalOptionsWrite: {
        UpdateStatus((SaveLoadMgrStatus)1);
        int saveSize = TheProfileMgr.GlobalOptionsSaveSize();
        mData =
            _MemAllocTemp(saveSize, "SaveLoadManager.cpp", 0x6AD, "SaveLoadManager", 0);
        FixedSizeSaveableStream fs(mData, saveSize, true);
        TheProfileMgr.SaveGlobalOptions(fs);
        if (!mCache->WriteAsync(kStrGlobalCacheName, mData, saveSize, nullptr)) {
            MILO_FAIL(
                "mCache->WriteAsync failed with CacheResult %d",
                TheCacheMgr->GetLastResult()
            );
        }
        break;
    }
    case kS_GlobalOptionsUnmount:
        if (!TheCacheMgr->UnmountAsync(&mCache, nullptr)) {
            int result = TheCacheMgr->GetLastResult();
            if (result != kCache_ErrorStorageDeviceMissing) {
                MILO_NOTIFY(
                    "UnmountAsync failed with error %d", TheCacheMgr->GetLastResult()
                );
            }
        }
        break;
    case kS_GlobalOptionsFailed:
        mDeviceIDState = 0;
        mLastChosenDeviceID = 0;
        TheProfileMgr.SetGlobalOptionsSaveState((ProfileSaveState)2);
        SetState(kS_GlobalOptionsDone);
        break;
    case kS_GlobalOptionsDone:
        mDeviceIDState = 1;
        mLastChosenDeviceID = 0;
        TheProfileMgr.SetGlobalOptionsSaveState((ProfileSaveState)2);
        SetState(kS_GlobalNewSignIns);
        break;
    case kS_SaveLoadError: {
        HamProfile *pProfile = mActiveProfile;
        mDeviceIDState = 0;
        MILO_ASSERT(pProfile, 0x6FE);
        TheMemcardMgr.SaveLoadProfileComplete(pProfile, 2);
        TheUIEventMgr->TriggerEvent(saveload_dialog_event, nullptr);
        break;
    }
    case kS_SaveLoadError2: {
        int errorType = 1;
        mDeviceIDState = 0;
        if (state == kS_SaveLoadError2) {
            errorType = -1;
        }
        HamProfile *pProfile = mActiveProfile;
        MILO_ASSERT(pProfile, 0x713);
        TheMemcardMgr.SaveLoadProfileComplete(pProfile, errorType);
        if (mMode >= kAutoSave) {
            if (mMode == kAutoSave) {
                SetState(kS_SaveCheckProfile);
            }
        } else {
            SetState(kS_AutoloadSelectProfile);
        }
        break;
    }
    case kS_SaveLoadCheckForFile: {
        HamProfile *pProfile = mActiveProfile;
        MILO_ASSERT(pProfile, 0x72D);
        mWaiting = true;
        TheMemcardMgr.OnCheckForSaveContainer(pProfile);
        break;
    }
    case kS_SaveLookForFile: {
        UpdateStatus((SaveLoadMgrStatus)1);
        HamProfile *pProfile = mActiveProfile;
        MILO_ASSERT(pProfile, 0x739);
        mWaiting = true;
        RELEASE(mAction);
        mAction = new SaveMemcardAction(pProfile);
        TheMemcardMgr.OnSaveGame(pProfile, mAction, 1);
        break;
    }
    case kS_SaveOverwrite: {
        UpdateStatus((SaveLoadMgrStatus)1);
        HamProfile *pProfile = mActiveProfile;
        MILO_ASSERT(pProfile, 0x747);
        mWaiting = true;
        RELEASE(mAction);
        mAction = new SaveMemcardAction(pProfile);
        TheMemcardMgr.OnSaveGame(pProfile, mAction, 0);
        break;
    }
    case kS_SaveNoOverwrite:
        // Save without overwrite
        break;
    case kS_SaveConfirmOverwrite:
    case kS_SaveNotEnoughSpace:
    case kS_SaveNotEnoughSpacePS3:
        // Dialog states
        break;
    case kS_SaveDeleteSaves: {
        HamProfile *pProfile = mActiveProfile;
        MILO_ASSERT(pProfile, 0x76D);
        mWaiting = true;
        TheMemcardMgr.OnDeleteSaves(pProfile);
        break;
    }
    case kS_SaveDeviceInvalid:
    case kS_SaveChooseDeviceInvalid:
    case kS_SaveFailed:
    case kS_SaveDisabledByCheat:
    case kS_LoadFailed:
        // Dialog/error states
        break;
    case kS_SaveDone:
        if (SongCacheNeedsWrite()) {
            SetState(kS_SaveSongCache);
        } else if (TheProfileMgr.GlobalOptionsNeedsSave()) {
            SetState(kS_SaveGlobalOptions);
        } else {
            SetState(kS_SaveCheckProfile);
        }
        break;
    case kS_SaveSongCache:
        TheSongMgr.StartSongCacheWrite();
        break;
    case kS_SaveGlobalOptions: {
        if (!mCacheID) {
            Symbol globalCacheName(kStrGlobalCacheName);
            mCacheID = TheCacheMgr->GetCacheID(globalCacheName);
        }
        if (!mCacheID) {
            SetState(kS_GlobalOptionsFailed);
        } else {
            SetState(kS_GlobalOptionsAllocRead);
        }
        break;
    }
    case kS_SaveCheckProfile:
        mActiveProfile = GetAutosavableProfile();
        if (mActiveProfile) {
            auto isStorageValid = TheMemcardMgr.IsStorageDeviceValid(mActiveProfile);
            if (isStorageValid) {
                SetState(kS_SaveOverwrite);
            } else {
                SetState(kS_SaveDeviceInvalid);
            }
        } else {
            SetState(kS_SaveCheckAutosave);
        }
        break;
    case kS_SaveCheckAutosave:
        TheProfileMgr.HandleProfileSaveComplete();
        SetState(kS_Done);
        break;
    case kS_ManualSaveInit:
        SetState(kS_ManualSaveChooseDevice);
        break;
    case kS_ManualSaveChooseDevice: {
        HamProfile *pProfile = mActiveProfile;
        MILO_ASSERT(pProfile, 0x7D6);
        mWaiting = true;
        TheMemcardMgr.SelectDevice(pProfile, this, mPadNum, true);
        break;
    }
    case kS_ManualSaveNoDevice:
        // Dialog state
        break;
    case kS_ManualSaveDone:
        // Manual save complete
        break;
    case kS_ManualLoadInit: {
        int padNum = 0;
        if (mActiveProfile) {
            padNum = mActiveProfile->GetPadNum();
        }
        if (TheProfileMgr.HasUnsavedDataForPad(padNum)) {
            SetState(kS_ManualLoadConfirmUnsaved);
        } else {
            SetState(kS_ManualLoadConfirm);
        }
        break;
    }
    case kS_ManualLoadConfirmUnsaved:
    case kS_ManualLoadConfirm:
    case kS_ManualLoadNoDevice:
    case kS_ManualLoadMissing:
    case kS_ManualLoadNoFile:
    case kS_ManualLoadCorrupt:
    case kS_ManualLoadNotOwner:
        // Dialog states
        break;
    case kS_ManualLoadChooseDevice:
        // Choose device for manual load
        break;
    case kS_ManualLoadStartLoad: {
        HamProfile *pProfile = mActiveProfile;
        MILO_ASSERT(pProfile, 0x811);
        mWaiting = true;
        RELEASE(mAction);
        mAction = new LoadMemcardAction(pProfile);
        pProfile->PreLoad();
        TheMemcardMgr.OnLoadGame(pProfile, mAction);
        break;
    }
    case kS_ManualLoadDone:
        // Manual load complete
        break;
    case kS_Abort:
        // Abort state
        break;
    case kS_Done:
        TheMemcardMgr.SaveLoadAllComplete();
        Finish();
        break;
    default:
        break;
    }
}

DataNode SaveLoadManager::OnMsg(const MCResultMsg &msg) {
    MILO_ASSERT(mWaiting, 0x8E8);
    mWaiting = false;

    int result = msg->Int(2);
    State nextState;

    if (mState <= kS_SaveNoOverwrite) {
        if (mState >= kS_SaveOverwrite || mState == kS_AutoloadSearchDevice) {
            unk64 = result;
            return DataNode(0);
        }
        if (mState != kS_AutoloadStartLoad) {
            if (mState != kS_SaveLookForFile) {
                goto fail;
            }
            // SaveLookForFile result handling
            switch ((unsigned)result) {
            case 0:
            case 5:
            case 7:
            case 25:
                nextState = kS_SaveConfirmOverwrite;
                break;
            case 1:
                nextState = kS_SaveDeviceInvalid;
                break;
            case 6:
            case 8:
                nextState = kS_SaveOverwrite;
                break;
            default:
                nextState = kS_SaveFailed;
                break;
            }
        } else {
            // AutoloadStartLoad result handling
            switch (result) {
            case 6:
            case 8:
                nextState = kS_SaveLookForFile;
                break;
            case 5:
                nextState = kS_AutoloadCorrupt;
                break;
            case 1:
                nextState = kS_AutoloadDeviceMissing;
                break;
            case 0:
            result_zero:
                unk64 = 0;
                nextState = kS_SaveLoadError2;
                break;
            case 10:
            autoload_obsolete:
                nextState = kS_AutoloadObsolete;
                break;
            case 11:
            autoload_future:
                nextState = kS_AutoloadFuture;
                break;
            case 25:
                nextState = kS_AutoloadNotOwner;
                break;
            default:
                nextState = kS_LoadFailed;
                break;
            }
        }
    } else if (mState == kS_SaveDeleteSaves) {
        nextState = kS_SaveNoOverwrite;
    } else {
        if (mState != kS_ManualLoadStartLoad) {
            if (mState <= kS_ManualLoadDone) {
                goto fail;
            }
            if (mState <= kS_Finish) {
                return DataNode(0);
            }
            goto fail;
        }
        switch (result) {
        case 0:
            unk64 = 0;
            nextState = kS_SaveLoadError2;
            break;
        case 1:
            nextState = kS_ManualLoadMissing;
            break;
        case 5:
            nextState = kS_ManualLoadCorrupt;
            break;
        case 8:
            nextState = kS_ManualLoadNoFile;
            break;
        case 10:
            goto autoload_obsolete;
        case 11:
            goto autoload_future;
        case 25:
            nextState = kS_ManualLoadNotOwner;
            break;
        default:
            nextState = kS_LoadFailed;
            break;
        }
    }
set_state:
    SetState(nextState);
    return DataNode(0);
fail:
    MILO_FAIL(
        "Unhandled MCResultMsg in state %d and mode %d", (int)mState, (State)mMode
    );
    return DataNode(0);
}

DataNode SaveLoadManager::OnMsg(const SigninChangedMsg &msg) {
    static Symbol saveload_dialog_event("saveload_dialog_event");

    mSigninMask = 0;

    switch (mState) {
    case kS_Idle:
    case kS_Abort:
    case kS_Done:
    case kS_Finish:
        break;

    default:
        if (mActiveProfile == NULL)
            break;
        if (!ThePlatformMgr.HasPadNumsSigninChanged(mActiveProfile->GetPadNum()))
            break;
        TheDebug.Notify(
            MakeString(
                "SIGNOUT on pad %d not expected during state %d",
                mActiveProfile->GetPadNum(),
                mState
            )
        );
        // fall through
    case kS_AutoloadStartLoad:
    case kS_SaveOverwrite:
    case kS_SaveNoOverwrite:
    case kS_ManualLoadStartLoad:
        SetState(kS_Abort);
        break;

    case kS_AutoloadNoSaveFound_Msg:
    case kS_AutoloadMultipleSavesFound:
    case kS_AutoloadDeviceMissing:
    case kS_AutoloadCorrupt:
    case kS_AutoloadNotOwner:
    case kS_AutoloadObsolete:
    case kS_AutoloadFuture:
    case kS_SongCacheCreateNotFound_Msg:
    case kS_SongCacheCreateMissing_Msg:
    case kS_SongCacheCreateCorrupt:
    case kS_GlobalCreateNotFound_Msg:
    case kS_GlobalCreateMissing_Msg:
    case kS_GlobalCreateCorrupt:
    case kS_GlobalOptionsMissing_Msg:
    case kS_SaveLoadError:
    case kS_SaveConfirmOverwrite:
    case kS_SaveNotEnoughSpace:
    case kS_SaveNotEnoughSpacePS3:
    case kS_SaveDeviceInvalid:
    case kS_SaveFailed:
    case kS_SaveDisabledByCheat:
    case kS_LoadFailed:
    case kS_ManualSaveNoDevice:
    case kS_ManualLoadConfirmUnsaved:
    case kS_ManualLoadConfirm:
    case kS_ManualLoadNoDevice:
    case kS_ManualLoadMissing:
    case kS_ManualLoadNoFile:
    case kS_ManualLoadCorrupt:
    case kS_ManualLoadNotOwner: {
        HamProfile *critProfile = TheProfileMgr.CriticalProfile();

        bool activeSignedOut = mActiveProfile != NULL
            && ThePlatformMgr.HasPadNumsSigninChanged(mActiveProfile->GetPadNum());

        bool otherSignedOut = critProfile != NULL
            && ThePlatformMgr.HasPadNumsSigninChanged(critProfile->GetPadNum());

        if (activeSignedOut) {
            if (TheUIEventMgr->HasActiveDialogEvent()
                && TheUIEventMgr->CurrentEvent() == saveload_dialog_event) {
                TheUIEventMgr->DismissEvent(gNullStr);
            } else {
                TheDebug.Notify(
                    MakeString(
                        "Expected active dialog event during signin change on pad %d while in state %d.",
                        mActiveProfile->GetPadNum(),
                        mState
                    )
                );
            }
        } else if (!otherSignedOut) {
            break;
        }

        SetState(kS_Done);
        break;
    }
    }

    return DataNode(0);
}

void SaveLoadManager::HandleEventResponse(HamProfile *profile, int response) {
    State savedState = mStateAtSelectStart;
    mStateAtSelectStart = (State)0;

    if (savedState != mState) {
        MILO_NOTIFY(
            "States changed between UIComponentSelectMsg (%d) and UIComponentSelectDoneMsg (%d).",
            savedState, mState
        );
        return;
    }

    if (response < 1 || response > 3) {
        MILO_FAIL("Bad choice index %i", response);
    } else {
        if (profile != nullptr) {
            mPadNum = profile->GetPadNum();
        } else {
            mPadNum = -1;
        }

        bool r1 = response == 1;
        State next;
        switch (mState) {
        case kS_AutoloadNoSaveFound_Msg:
            if (response == 1) {
                if (mDeviceIDState == 2)
                    SetState(kS_AutoloadSelectDevice);
                else
                    SetState(kS_AutoloadSetDevice);
                return;
            }
            SetState(kS_SaveLoadError);
            return;
        case kS_AutoloadMultipleSavesFound:
            next = (State)(kS_SaveLoadError + (-(int)r1 & (kS_AutoloadSelectDevice2 - kS_SaveLoadError)));
            break;
        case kS_AutoloadDeviceMissing:
            next = (State)(kS_SaveLoadError + (-(int)r1 & (kS_AutoloadSelectDevice3 - kS_SaveLoadError)));
            break;
        case kS_AutoloadCorrupt:
        case kS_AutoloadNotOwner:
        case kS_AutoloadObsolete:
        case kS_AutoloadFuture:
        case kS_SaveConfirmOverwrite:
            next = (State)(kS_SaveLoadError + (-(int)r1 & (kS_SaveOverwrite - kS_SaveLoadError)));
            break;
        case kS_SongCacheCreateNotFound_Msg:
        case kS_SongCacheCreateMissing_Msg:
            next = (State)(kS_GlobalOptionsInit + (-(int)r1 & (kS_SongCacheMount - kS_GlobalOptionsInit)));
            break;
        case kS_SongCacheCreateCorrupt:
            next = (State)(kS_GlobalOptionsInit + (-(int)r1 & (kS_SongCacheGetSize - kS_GlobalOptionsInit)));
            break;
        case kS_GlobalCreateNotFound_Msg:
        case kS_GlobalCreateMissing_Msg:
            next = (State)(kS_GlobalFailed + (-(int)r1 & (kS_GlobalMount - kS_GlobalFailed)));
            break;
        case kS_GlobalCreateCorrupt:
            next = (State)(kS_GlobalFailed + (-(int)r1 & (kS_GlobalRead - kS_GlobalFailed)));
            break;
        case kS_GlobalOptionsMissing_Msg:
            next = (State)(kS_GlobalOptionsFailed + (-(int)r1 & (kS_GlobalOptionsCreate2 - kS_GlobalOptionsFailed)));
            break;
        case kS_SaveLoadError:
            if ((unsigned int)mMode >= 1) {
                if ((unsigned int)mMode != 1) {
                    if ((unsigned int)mMode >= 3)
                        return;
                    SetState(kS_ManualSaveInit);
                    return;
                }
                SetState(kS_SaveCheckProfile);
                return;
            }
            SetState(kS_AutoloadSelectProfile);
            return;
        case kS_SaveNotEnoughSpace:
        case kS_SaveFailed:
        case kS_SaveDisabledByCheat:
        case kS_LoadFailed:
        case kS_ManualLoadMissing:
        case kS_ManualLoadNoFile:
        case kS_ManualLoadCorrupt:
        case kS_ManualLoadNotOwner:
            next = kS_SaveLoadError;
            break;
        case kS_SaveDeviceInvalid:
            next = (State)(kS_SaveLoadError + (-(int)r1 & (kS_SaveChooseDeviceInvalid - kS_SaveLoadError)));
            break;
        case kS_SaveNotEnoughSpacePS3:
            if (response == 1)
                next = kS_SaveDeleteSaves;
            else if (response == 2)
                next = kS_SaveNoOverwrite;
            else
                next = kS_SaveLoadError;
            break;
        case kS_ManualSaveNoDevice:
            next = (State)(kS_SaveLoadError + (-(int)r1 & (kS_ManualSaveChooseDevice - kS_SaveLoadError)));
            break;
        case kS_ManualLoadConfirmUnsaved:
        case kS_ManualLoadConfirm:
            if (response == 1) {
                SetState(kS_ManualLoadChooseDevice);
                return;
            }
            next = kS_SaveLoadCheckForFile;
            break;
        case kS_ManualLoadNoDevice:
            next = (State)(kS_SaveLoadError + (-(int)r1 & (kS_ManualLoadChooseDevice - kS_SaveLoadError)));
            break;
        default:
            MILO_FAIL(
                "Unhandled UIComponentSelectDoneMsg from choice index %i in state %d and mode %d",
                response, (int)mState, (int)mMode
            );
            return;
        }

        SetState(next);
    }
}
