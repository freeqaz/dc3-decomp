#include "meta_ham/ChooseModeProvider.h"
#include "game/GameMode.h"
#include "obj/Object.h"
#include "os/Debug.h"
#include "rndobj/Mat.h"
#include "ui/UILabel.h"
#include "ui/UIListLabel.h"
#include "ui/UIListMesh.h"
#include "utl/Symbol.h"
#ifdef HX_NATIVE
#include <cstdio>
#include <cstdlib>
#include <cstring>
#endif

#ifdef HX_NATIVE
namespace {
bool DebugChooseMode() {
    static int enabled = -1;
    if (enabled == -1) {
        const char *env = getenv("MILO_DEBUG_CHOOSE_MODE");
        enabled = (env && env[0] && strcmp(env, "0") != 0) ? 1 : 0;
    }
    return enabled != 0;
}
}
#endif

BEGIN_HANDLERS(ChooseModeProvider)
    HANDLE_ACTION(update_list, UpdateList(_msg->Int(2)))
    HANDLE_SUPERCLASS(Hmx::Object)
END_HANDLERS

ChooseModeProvider::~ChooseModeProvider() {}

void ChooseModeProvider::Text(
    int, int i_iData, UIListLabel *listLabel, UILabel *uiLabel
) const {
    MILO_ASSERT(i_iData < NumData(), 0x4e);
    Symbol dataSym = DataSymbol(i_iData);
    if (listLabel->Matches("label")) {
        uiLabel->SetTextToken(dataSym);
    } else {
        uiLabel->SetTextToken(listLabel->GetDefaultText());
    }
}

Symbol ChooseModeProvider::DataSymbol(int i_iData) const {
    MILO_ASSERT_RANGE(i_iData, 0, NumData(), 0xaf);
    return mModes[i_iData];
}

void ChooseModeProvider::UpdateList(bool b) {
#ifdef HX_NATIVE
    if (DebugChooseMode()) {
        printf(
            "DC3 CHOOSE ChooseModeProvider::UpdateList provider=%s extra=%d before=%zu\n",
            PathName(this),
            b,
            mModes.size()
        );
    }
#endif
    mModes.clear();
    static Symbol perform("perform");
    static Symbol practice("practice");
    static Symbol dance_battle("dance_battle");
    static Symbol custom_party("custom_party");
    static Symbol crew_showdown("crew_showdown");
    static Symbol rtnbldrproto("rtnbldrproto");
    static Symbol namethatdance("namethatdance");
    static Symbol cascade("cascade");
    static Symbol concentration("concentration");
    static Symbol rhythm_battle("rhythm_battle");
    static Symbol holla_back("holla_back");
    static Symbol holla_back_70s_craze("holla_back_70s_craze");
    static Symbol perform_legacy("perform_legacy");
    static Symbol leaderboards("leaderboards");
    static Symbol bustamove("bustamove");
    static Symbol mind_control("mind_control");

    mModes.push_back(perform);
    mModes.push_back(practice);
    if (!TheGameMode->InMode("campaign", true)) {
        mModes.push_back(dance_battle);
        mModes.push_back(custom_party);
        mModes.push_back(crew_showdown);
        if (b) {
            mModes.push_back(perform_legacy);
            mModes.push_back(rtnbldrproto);
            mModes.push_back(namethatdance);
            mModes.push_back(concentration);
            mModes.push_back(rhythm_battle);
            mModes.push_back(holla_back_70s_craze);
            mModes.push_back(dance_battle);
            mModes.push_back(bustamove);
            mModes.push_back(mind_control);
        }
    }
#ifdef HX_NATIVE
    if (DebugChooseMode()) {
        printf(
            "DC3 CHOOSE ChooseModeProvider::UpdateList done provider=%s count=%zu first=%s\n",
            PathName(this),
            mModes.size(),
            mModes.empty() ? "<none>" : mModes[0].Str()
        );
    }
#endif
}

RndMat *ChooseModeProvider::Mat(int, int i_iData, UIListMesh *mesh) const {
    MILO_ASSERT_RANGE(i_iData, 0, NumData(), 0x5d);
    static Symbol practice("practice");
    static Symbol perform("perform");
    static Symbol holla_back("holla_back");
    static Symbol dance_battle("dance_battle");
    static Symbol namethatdance("namethatdance");
    static Symbol perform_legacy("perform_legacy");
    static Symbol start_the_party("start_the_party");
    static Symbol rhythm_battle("rhythm_battle");
    static Symbol bustamove("bustamove");
    static Symbol custom_party("custom_party");
    static Symbol crew_showdown("crew_showdown");
    static Symbol rtnbldrproto("rtnbldrproto");
    static Symbol concentration("concentration");

    Symbol dataSym = DataSymbol(i_iData);

    if (mesh->Matches("icon_1p")) {
        return nullptr;
    }
    bool isOnePlusIcon = mesh->Matches("icon_1p_plus");
    if (isOnePlusIcon) {
        if ((int)dataSym == custom_party) {
            return nullptr;
        }
        return mesh->DefaultMat();
    }
    if (mesh->Matches("icon_2p")) {
        if (dataSym == namethatdance || dataSym == dance_battle || dataSym == concentration || rhythm_battle == dataSym) {
            return nullptr;
        }
        return mesh->DefaultMat();
    }
    if (mesh->Matches("icon_1por2p")) {
        if (dataSym == perform || dataSym == perform_legacy || dataSym == rtnbldrproto || dataSym == holla_back) {
            return nullptr;
        }
        return mesh->DefaultMat();
    }
    if (mesh->Matches("icon_2p_plus")) {
        return nullptr;
    }

    return nullptr;
}
