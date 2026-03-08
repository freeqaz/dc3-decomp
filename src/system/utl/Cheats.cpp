#include "Cheats.h"

#include "obj/DataFunc.h"
#include "obj/Dir.h"
#include "obj/Msg.h"
#include "os/Debug.h"
#include "os/Joypad.h"
#include "os/JoypadMsgs.h"
#include "os/System.h"
#include "os/UserMgr.h"

CheatsManager *gCheatsManager = nullptr;
static bool gDisable = false;
static bool gKeyCheatsEnabled = false;

void InitQuickJoyCheats(const DataArray *a, CheatsManager::ShiftMode);
void InitKeyCheats(const DataArray *);
void InitLongJoyCheats(const DataArray *);

static DataNode SetKeyCheatsEnabled(DataArray *da);
static DataNode OnSetCheatMode(DataArray *da);
static DataNode OnGetCheatMode(DataArray *da);

bool CheatsInitialized() { return gCheatsManager != 0; }

BEGIN_HANDLERS(CheatsManager)
    HANDLE_ACTION(set_unsafe_cheat_used, gCheatsManager->mUnsafeCheatsUsed)
    HANDLE_MESSAGE(ButtonDownMsg)
    HANDLE_MESSAGE(KeyboardKeyMsg)
    HANDLE_MESSAGE(KeyboardKeyReleaseMsg)
END_HANDLERS

void CheatsInit() {
    SystemConfig()->FindData("disable_cheats", gDisable, true);
    if (!gDisable) {
        if (gCheatsManager != 0) {
            MILO_ASSERT(gCheatsManager == null, 0x2d8);
        }
        gCheatsManager = new CheatsManager();
        JoypadSubscribe(gCheatsManager);
        KeyboardSubscribe(gCheatsManager);

        DataArray *quickCheats = SystemConfig("quick_cheats");
        auto leftCheats = quickCheats->FindArray("left", true);
        InitQuickJoyCheats(
            leftCheats, CheatsManager::kLeftShift
        );
        InitQuickJoyCheats(
            quickCheats->FindArray("right", true), CheatsManager::kRightShift
        );

        InitKeyCheats(quickCheats->FindArray("keyboard", true));

        InitLongJoyCheats(SystemConfig("long_cheats"));

        DataRegisterFunc("set_key_cheats_enabled", SetKeyCheatsEnabled);
        DataRegisterFunc("set_cheat_mode", OnSetCheatMode);
        DataRegisterFunc("get_cheat_mode", OnGetCheatMode);
    }
}

DataNode OnGetCheatMode(DataArray *da) { return gCheatsManager->CheatMode(); }

void EnableKeyCheats(bool b) {
    gKeyCheatsEnabled = b;
    if (gCheatsManager) {
        gCheatsManager->SetKeyCheatsEnabled(b);
    }
}

DataNode OnSetCheatMode(DataArray *da) {
    Symbol sym;
    sym = da->Sym(1);
    if (gCheatsManager->GetSymMode() != sym) {
        gCheatsManager->SetSymMode(sym);
    }
    return 0;
}

DataNode SetKeyCheatsEnabled(DataArray *da) {
    bool result = da->Int(1) != 0;
    gKeyCheatsEnabled = result;
    if (gCheatsManager) {
        gCheatsManager->SetKeyCheatsEnabled(result);
    }
    return 0;
}

CheatsManager::CheatsManager()
    : mKeyCheatsEnabled(gKeyCheatsEnabled), mCtrlOverriddeMode(false),
      mIsOverridingKeyboard(false), mPreviousOverride(nullptr), mUnsafeCheatsUsed(false) {
    mLastButtonTime.Start();
    SystemConfig()->FindData("cheats_buffer", mMaxBuffer);
    DataArray *arr = SystemConfig()->FindArray("cheats_ctrl_mode", false);
    if (arr) {
        mCtrlOverriddeMode = arr->Int(1);
    }
    SetName("cheats_mgr", ObjectDir::Main());
}

void CheatsManager::AppendLog(FixedString &fs) {
    if (mBuffer.size() != 0) {
        fs += "\n\nCheats Used";
        char buf[16];
        strncpy(buf, "\n   %.30s", 10);
        for (std::list<CheatLog>::iterator it = mBuffer.begin(); it != mBuffer.end(); ++it) {
            String str;
            it->mScript.Print(str, 1, 0);
            fs += MakeString(buf, str);
        }
        if (mBuffer.size() == mMaxBuffer) {
            fs += "\n   ...";
        }
    }
}

void LogCheat(int i1, bool b, DataArray *da) {
    if (!gCheatsManager) {
        MILO_ASSERT(gCheatsManager, 0x303);
    }
    gCheatsManager->Log(i1, b, da);
}

void AppendCheatsLog(FixedString &fs) {
    if (gCheatsManager) {
        gCheatsManager->AppendLog(fs);
    }
}

bool GetEnabledKeyCheats() { return gKeyCheatsEnabled; }

void CheatsManager::Log(int padNum, bool quickCheat, DataArray *script) {
    CheatLog log;
    log.mPad = padNum;
    log.mQuick = quickCheat;
    log.mScript = script;
    mBuffer.push_front(log);

    if (mBuffer.size() > mMaxBuffer) {
        mBuffer.pop_back();
    }
}

void CheatsManager::CallCheatScript(bool b1, DataArray *da, LocalUser *lu, bool b2) {
    if (!lu && TheUserMgr) {
        std::vector<LocalUser *> users;
        TheUserMgr->GetLocalUsers(users);
        for (std::vector<LocalUser *>::iterator it = users.begin();
             it != users.end();
             ++it) {
            if ((*it)->GetPadNum() == -1)
                break;
            JoypadData *padData = JoypadGetPadData((*it)->GetPadNum());
            if (b1 && b2 && padData->mType - 1U > 2 && padData->mType - 0x13U > 2) {
                lu = *it;
                break;
            }
        }
    }
    if (lu) {
        switch (JoypadGetPadData(lu->GetPadNum())->mType) {
        case kJoypadDigital:
        case kJoypadAnalog:
        case kJoypadDualShock:
        case kJoypadWiiCore:
        case kJoypadWiiFS:
        case kJoypadWiiClassic:
            DataVariable("cheat_pad") = lu ? lu->GetPadNum() : 0;
            LogCheat(lu ? lu->GetPadNum() : -1, b1, da);
            if (b1) {
                int i = 2;
                for (; da->Node(i).Type() != kDataCommand && i < da->Size(); i++)
                    ;
                if (i < da->Size()) {
                    da->ExecuteScript(i, nullptr, nullptr, 1);
                }
            } else {
                da->Execute();
            }
            {
                Hmx::Object *uiObj = ObjectDir::Main()->Find<Hmx::Object>("ui", true);
                static Message msg("cheat_invoked", 0, 0);
                msg[0] = b1;
                msg[1] = DataNode(da, kDataArray);
                uiObj->Handle(msg, false);
            }
            break;
        default:
            break;
        }
    }
}

void CallQuickCheat(DataArray *da, LocalUser *lu) {
    if (!gCheatsManager) {
        MILO_ASSERT(gCheatsManager, 0x309);
    }
    gCheatsManager->CallCheatScript(true, da, lu, false);
}

void CheatsTerminate() {
    if (!gDisable) {
        if (!gCheatsManager) {
            MILO_ASSERT(gCheatsManager, 0x2fa);
        }
        JoypadUnsubscribe(gCheatsManager);
        KeyboardUnsubscribe(gCheatsManager);
        if (gCheatsManager) {
            delete gCheatsManager;
        }
        gCheatsManager = 0;
    }
}

__declspec(noinline) void CheatsManager::SetSymMode(Symbol sym) {
    mSymMode = sym;
    RebuildKeyCheatsForMode();
}

void CheatsManager::RebuildKeyCheatsForMode() {
    auto cheatsEnd = mKeyCheats.end();
    static Symbol modes("modes");
    mKeyCheatPtrsMode.clear();
    for (std::vector<KeyCheat>::iterator it = mKeyCheats.begin();
         it != cheatsEnd; ++it) {
        DataArray *modesArr = it->mScript->FindArray(modes, false);
        if (!modesArr || modesArr->Contains(mSymMode)) {
            mKeyCheatPtrsMode.push_back(&*it);
        }
    }
    for (int i = 0; i < 2; i++) {
        mJoyCheatPtrsMode[i].clear();
        for (std::vector<QuickJoyCheat>::iterator it = mQuickJoyCheats[i].begin();
             it != mQuickJoyCheats[i].end(); ++it) {
            DataArray *modesArr = it->mScript->FindArray(modes, false);
            if (!modesArr || modesArr->Contains(mSymMode)) {
                mJoyCheatPtrsMode[i].push_back(&*it);
            }
        }
    }
}


void InitLongJoyCheats(const DataArray *cheats) {
    for (int i = 1; i < cheats->Size(); i++) {
        DataArray *cheat = cheats->Array(i);
        DataArray *buttons = cheat->Array(0);
        if (buttons->Size() > 16) {
            MILO_LOG("Too many buttons in long cheat, max %d\n", 16);
        } else {
            LongJoyCheat longJoyCheat;
            bool good = true;
            for (int j = 0; j < buttons->Size(); j++) {
                int button = buttons->Int(j);
                if (button >= 0 && button < kPad_NumButtons) {
                    longJoyCheat.mSequence.push_back(button);
                } else {
                    MILO_LOG("Error in long_cheats: %s is not a valid button\n", button);
                    good = false;
                    break;
                }
            }
            if (good) {
                longJoyCheat.mScript = cheat->Command(1);
                gCheatsManager->AddLongJoyCheat(longJoyCheat);
            }
        }
    }
}

DataNode CheatsManager::OnMsg(KeyboardKeyReleaseMsg const &msg) {
    if (msg->Int(2) == 0x11 && mIsOverridingKeyboard)
        return 0;
    return DATA_UNHANDLED;
}