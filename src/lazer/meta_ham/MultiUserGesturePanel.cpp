#include "meta_ham/MultiUserGesturePanel.h"
#include "HamPanel.h"
#include "HamSongMetadata.h"
#include "HamSongMgr.h"
#include "MultiUserGesturePanel.h"
#include "flow/PropertyEventProvider.h"
#include "game/GameMode.h"
#include "gesture/BaseSkeleton.h"
#include "gesture/GestureMgr.h"
#include "hamobj/HamGameData.h"
#include "hamobj/HamNavList.h"
#include "hamobj/HamPlayerData.h"
#include "meta_ham/CharacterProvider.h"
#include "meta_ham/CrewProvider.h"
#include "meta_ham/DifficultyProvider.h"
#include "meta_ham/HamSongMgr.h"
#include "meta_ham/HamUI.h"
#include "meta_ham/MetaPerformer.h"
#include "meta_ham/OutfitProvider.h"
#include "meta_ham/ProfileMgr.h"
#include "meta_ham/SkeletonChooser.h"
#include "meta_ham/TexLoadPanel.h"
#include "meta_ham/VenueProvider.h"
#include "obj/Data.h"
#include "obj/Dir.h"
#include "obj/Msg.h"
#include "obj/Object.h"
#include "os/Debug.h"
#include "os/Joypad.h"
#include "os/JoypadMsgs.h"
#include "rndobj/Draw.h"
#include "rndobj/Mat.h"
#include "rndobj/Mesh.h"
#include "rndobj/Tex.h"
#include "ui/UI.h"
#include "ui/UIComponent.h"
#include "ui/UIPicture.h"
#include "utl/FilePath.h"
#include "utl/MakeString.h"
#include "utl/Symbol.h"
#include <cstring>

MultiUserGesturePanel::MultiUserGesturePanel() {
    // Initialize UI components and providers for both players (left/right sides)
    // Note: (&mLeftNavList1)[i] pattern required for codegen - initializes nav list pairs
    for (int i = 0; i < 2; i++) {
        (&mLeftNavList1)[i] = NULL;
        (&mLeftNavList2)[i] = NULL;
        mCharacterProviders[i].SetPlayer(i);
        mVenueProviders[i].SetPlayer(i);
        mCrewProviders[i].SetPlayer(i);
        mOutfitProviders[i].SetPlayer(i);
        mDifficultyProviders[i].SetPlayer(i);
    }
#ifdef HX_NATIVE
    mNativeEnterPending = false;
#endif
}

void MultiUserGesturePanel::Enter() {
    UpdateProviderPlayerIndices();
    HamPanel::Enter();
    for (int i = 0; i < 2; i++) {
        (&mLeftNavList1)[i] = DataDir()->Find<HamNavList>(MakeString("right_hand_p%d.hnl", i + 1), false);
        (&mLeftNavList2)[i] = DataDir()->Find<HamNavList>(MakeString("left_hand_p%d.hnl", i + 1), false);
    }
    UpdateProviders();
#ifdef HX_NATIVE
    mNativeEnterPending = true;
#endif
}

void MultiUserGesturePanel::Poll() {
    if (!TheUI->InTransition()) {
#ifdef HX_NATIVE
        // No Kinect skeleton chooser on native — fire enter_gameplay directly
        // (on Xbox, this fires from DTA once skeleton assignment completes)
        if (mNativeEnterPending) {
            mNativeEnterPending = false;
            static Symbol enter_gameplay("enter_gameplay");
            static DataArrayPtr dataPtr(enter_gameplay);
            dataPtr->Execute();
        }
#else
        // UpdateNavLists manages Kinect skeleton tracking IDs for nav lists
        for (int i = 0; i < 2; i++)
            UpdateNavLists(i);
#endif
        UpdateProviderPlayerIndices();
    }
    TexLoadPanel::Poll();
}

void MultiUserGesturePanel::Unload() {
    TexLoadPanel::Unload();
    for (int i = 0; i < 2; i++) {
        mCharacterProviders[i].SetPanelDir(nullptr);
        mCrewProviders[i].SetPanelDir(nullptr);
    }
}

void MultiUserGesturePanel::FinishLoad() {
    TexLoadPanel::FinishLoad();
    for (int i = 0; i < 2; i++) {
        mCharacterProviders[i].SetPanelDir(LoadedDir());
        mCrewProviders[i].SetPanelDir(LoadedDir());
    }
}

bool MultiUserGesturePanel::Exiting() const { return HamPanel::Exiting(); }

void MultiUserGesturePanel::UpdateProviders() {
    for (int i = 0; i < 2; i++) {
        mCharacterProviders[i].UpdateList();
        mVenueProviders[i].UpdateList();
        mCrewProviders[i].UpdateList();
        mOutfitProviders[i].UpdateList();
    }
}

CharacterProvider const *MultiUserGesturePanel::GetCharProvider(int index) const {
    MILO_ASSERT_RANGE(index, 0, 2, 0xcf);
    return &mCharacterProviders[index];
}

VenueProvider const *MultiUserGesturePanel::GetVenueProvider(int index) const {
    MILO_ASSERT_RANGE(index, 0, 2, 0xd6);
    return &mVenueProviders[index];
}

CrewProvider const *MultiUserGesturePanel::GetCrewProvider(int index) const {
    MILO_ASSERT_RANGE(index, 0, 2, 0xdd);
    return &mCrewProviders[index];
}

OutfitProvider const *MultiUserGesturePanel::GetOutfitProvider(int index) const {
    MILO_ASSERT_RANGE(index, 0, 2, 0xe4);
    return &mOutfitProviders[index];
}

DifficultyProvider *MultiUserGesturePanel::GetDifficultyProvider(int index) {
    MILO_ASSERT_RANGE(index, 0, 2, 0xeb);
    return &mDifficultyProviders[index];
}

Symbol MultiUserGesturePanel::GetOutfit(int i1, int i2) const {
    const OutfitProvider *pProvider = GetOutfitProvider(i2);
    MILO_ASSERT(pProvider, 0xf9);
    return pProvider->DataSymbol(i1);
}

Symbol MultiUserGesturePanel::GetCharacter(int i1, int i2) {
    const CharacterProvider *pProvider = GetCharProvider(i2);
    MILO_ASSERT(pProvider, 0x2a1);
    return pProvider->DataSymbol(i1);
}

int MultiUserGesturePanel::GetCharacterIndex(int idx) const {
    const CharacterProvider *pProvider = GetCharProvider(idx);
    MILO_ASSERT(pProvider, 0x154);
    HamPlayerData *pPlayerData = TheGameData->Player(GetPlayerIndex(idx));
    return pProvider->DataIndex(pPlayerData->Char());
}

int MultiUserGesturePanel::GetOutfitIndex(int idx) const {
    const OutfitProvider *pProvider = GetOutfitProvider(idx);
    MILO_ASSERT(pProvider, 0x15f);
    HamPlayerData *pPlayerData = TheGameData->Player(GetPlayerIndex(idx));
    return pProvider->DataIndex(pPlayerData->Outfit());
}

int MultiUserGesturePanel::GetCrewIndex(int idx) const {
    const CrewProvider *pProvider = GetCrewProvider(idx);
    MILO_ASSERT(pProvider, 0x16a);
    HamPlayerData *pPlayerData = TheGameData->Player(GetPlayerIndex(idx));
    return pProvider->DataIndex(pPlayerData->Crew());
}

int MultiUserGesturePanel::GetVenueIndex(int idx, Symbol s) const {
    const VenueProvider *pProvider = GetVenueProvider(idx);
    MILO_ASSERT(pProvider, 0x175);
    return pProvider->DataIndex(s);
}

bool MultiUserGesturePanel::IsCrewAvailable(Symbol crew, int idx) {
    const CrewProvider *pProvider = GetCrewProvider(idx);
    MILO_ASSERT(pProvider, 0x2a9);
    return pProvider->IsCrewAvailable(crew);
}

bool MultiUserGesturePanel::IsCharacterAvailable(Symbol character, int idx) {
    const CharacterProvider *pProvider = GetCharProvider(idx);
    MILO_ASSERT(pProvider, 0x2b1);
    return pProvider->IsCharacterAvailable(character);
}

void MultiUserGesturePanel::SetOutfit(Symbol outfit, int idx) {
    HamPlayerData *pPlayerData = TheGameData->Player(idx);
    MILO_ASSERT(pPlayerData, 0x101);
    pPlayerData->SetOutfit(outfit);
    if (!TheGameMode->InMode("campaign", true))
        pPlayerData->SetPreferredOutfit(outfit);
}

void MultiUserGesturePanel::SetDefaultCharacter(int idx) {
    int index = GetPlayerIndex(idx);
    HamPlayerData *pPlayerData = TheGameData->Player(index);
    MILO_ASSERT(pPlayerData, 0x248);
    MetaPerformer *pPerformer = MetaPerformer::Current();
    MILO_ASSERT(pPerformer, 0x24b);
    pPerformer->SetDefaultSongCharacter(index);
    if (!TheGameMode->InMode("dance_battle", true)) {
        if (!TheGameMode->InMode("campaign", true)) {
            pPlayerData->SetMiniGameCharacter(gNullStr);
            pPlayerData->SetPreferredOutfit(gNullStr);
        }
    }
}

void MultiUserGesturePanel::SetRandomOutfit(int idx) {
    int index = GetPlayerIndex(idx);
    HamPlayerData *pPlayerData = TheGameData->Player(index);
    MILO_ASSERT(pPlayerData, 0x295);
    const OutfitProvider *pOutfitProvider = GetOutfitProvider(idx);
    MILO_ASSERT(pOutfitProvider, 0x298);
    Symbol randOutfit = pOutfitProvider->GetRandomAvailableOutfit();
    pPlayerData->SetOutfit(randOutfit);
}

void MultiUserGesturePanel::SetRandomCrew(int idx) {
    static Symbol random_crew("random_crew");
    int index = GetPlayerIndex(idx);
    int otherindex = index == 0;
    HamPlayerData *pPlayerData = TheGameData->Player(index);
    MILO_ASSERT(pPlayerData, 0x22e);
    HamPlayerData *pOtherPlayerData = TheGameData->Player(otherindex);
    MILO_ASSERT(pOtherPlayerData, 0x230);
    const CrewProvider *pCrewProvider = GetCrewProvider(idx);
    MILO_ASSERT(pCrewProvider, 0x233);
    Symbol symRandomCrew = pCrewProvider->GetRandomAvailableCrew();
    MILO_ASSERT(symRandomCrew != gNullStr, 0x236);
    pPlayerData->SetCrew(symRandomCrew);
    const CharacterProvider *pCharacterProvider = GetCharProvider(idx);
    MILO_ASSERT(pCharacterProvider, 0x23b);
    const_cast<CharacterProvider *>(pCharacterProvider)->UpdateList();
    SetRandomCharacter(idx);
}

void MultiUserGesturePanel::SetCrew(Symbol crew, int idx) {
    int index = GetPlayerIndex(idx);
    int otherindex = index == 0;
    HamPlayerData *pPlayerData = TheGameData->Player(index);
    MILO_ASSERT(pPlayerData, 0x131);
    HamPlayerData *pOtherPlayerData = TheGameData->Player(otherindex);
    MILO_ASSERT(pOtherPlayerData, 0x133);
    if (crew != Symbol("") && pOtherPlayerData->Crew() == crew) {
        pOtherPlayerData->SetCrew(pPlayerData->Crew());
        pOtherPlayerData->SetCharacter(pPlayerData->Char());
        pOtherPlayerData->SetOutfit(pPlayerData->Outfit());
        RefreshUI();
    }
    MetaPerformer *performer = MetaPerformer::Current();
    MILO_ASSERT(performer, 0x143);
    pPlayerData->SetCrew(crew);
    if (crew != Symbol("")) {
        const CharacterProvider *pCharacterProvider = GetCharProvider(idx);
        MILO_ASSERT(pCharacterProvider, 0x14a);
        const_cast<CharacterProvider *>(pCharacterProvider)->UpdateList();
        SetRandomCharacter(idx);
    }
}

void MultiUserGesturePanel::RefreshUI() {
    static Message cRefreshUIMsg("refresh_ui");
    TheUI->Handle(cRefreshUIMsg, false);
}

int MultiUserGesturePanel::GetPlayerIndex(int idx) const {
    SkeletonChooser *pSkeletonChooser = TheHamUI.GetShellInput()->GetSkeletonChooser();
#ifdef HX_NATIVE
    if (!pSkeletonChooser)
        return idx; // No Kinect on native — side maps directly to player index
#endif
    MILO_ASSERT(pSkeletonChooser, 0x68);
    SkeletonSide playerSide = pSkeletonChooser->GetPlayerSide(0);
    const DataNode *prop = TheHamProvider->Property("is_in_party_mode", true);
    if (prop->Int() != 0) {
        return idx;
    }
    if (idx == 0) {
        return playerSide != kSkeletonRight;
    } else
        return playerSide == kSkeletonRight;
}

void MultiUserGesturePanel::SetCharacter(Symbol s, int idx) {
    int otherindex = idx == 0;
    HamPlayerData *pPlayerData = TheGameData->Player(idx);
    MILO_ASSERT(pPlayerData, 0x110);
    HamPlayerData *pOtherPlayerData = TheGameData->Player(otherindex);
    MILO_ASSERT(pOtherPlayerData, 0x112);
    MetaPerformer *performer = MetaPerformer::Current();
    MILO_ASSERT(performer, 0x115);
    Symbol crewForChar = GetCrewForCharacter(s);
    pPlayerData->SetCharacter(s);
    pPlayerData->SetCrew(crewForChar);
    pPlayerData->SetOutfit(GetCharacterOutfit(s, false));
    if (!TheGameMode->InMode("campaign", true)) {
        pPlayerData->SetMiniGameCharacter(s);
        pPlayerData->SetPreferredOutfit(GetCharacterOutfit(s, false));
    }
    if (pOtherPlayerData->Char() == s) {
        performer->SetDefaultSongCharacter(otherindex);
        RefreshUI();
    }
}

void MultiUserGesturePanel::SetRandomCharacter(int idx) {
    int index = GetPlayerIndex(idx);
    int otherindex = index == 0;
    HamPlayerData *pPlayerData = TheGameData->Player(index);
    MILO_ASSERT(pPlayerData, 0x25d);
    HamPlayerData *pOtherPlayerData = TheGameData->Player(otherindex);
    MILO_ASSERT(pOtherPlayerData, 0x25f);
    const CharacterProvider *pCharProvider = GetCharProvider(idx);
    MILO_ASSERT(pCharProvider, 0x262);
    Symbol symRandomCharacter = pCharProvider->GetRandomAvailableCharacter();
    MILO_ASSERT(symRandomCharacter != gNullStr, 0x265);
    Symbol crewForChar = GetCrewForCharacter(symRandomCharacter);
    pPlayerData->SetCharacter(symRandomCharacter);
    pPlayerData->SetCrew(crewForChar);
    if (!TheGameMode->InMode("dance_battle", true)) {
        if (!TheGameMode->InMode("campaign", true)) {
            pPlayerData->SetMiniGameCharacter(gNullStr);
            pPlayerData->SetPreferredOutfit(gNullStr);
        }
    }

    if (TheGameMode->InMode("dance_battle", true)) {
        pPlayerData->SetOutfit(GetCharacterOutfit(symRandomCharacter, false));
    } else {
        const OutfitProvider *pOutfitProvider = GetOutfitProvider(idx);
        MILO_ASSERT(pOutfitProvider, 0x28a);
        const_cast<OutfitProvider *>(pOutfitProvider)->UpdateList();
        SetRandomOutfit(idx);
    }
}

void MultiUserGesturePanel::DropPlayerOnSide(int idx) {
    int index = GetPlayerIndex(idx);
    SkeletonChooser *pSkeletonChooser = TheHamUI.GetShellInput()->GetSkeletonChooser();
#ifdef HX_NATIVE
    if (!pSkeletonChooser)
        return; // No Kinect on native
#endif
    MILO_ASSERT(pSkeletonChooser, 0x3d);
    pSkeletonChooser->ClearPlayerSkeletonID(index);
}

void MultiUserGesturePanel::UpdateCrewPic(
    UIPicture *i_pPic, int i_iSide, int i_iPlayerIndex, Symbol s
) {
    MILO_ASSERT(i_pPic, 0x1e9);
    MILO_ASSERT_RANGE(i_iPlayerIndex, 0, 2, 0x1ea);
    MILO_ASSERT_RANGE(i_iSide, 0, 2, 0x1eb);
    const CrewProvider *pProvider = GetCrewProvider(i_iSide);
    MILO_ASSERT(pProvider, 0x1ee);
    String str;
    if (!TheProfileMgr.IsContentUnlocked(s)) {
        str = MakeString("%s_char_locked_keep.png", s.Str());
    } else if (!pProvider->IsCrewAvailable(s)) {
        str = MakeString("%s_char_locked_keep.png", s.Str());
    } else {
        str = MakeString("%s_char_keep.png", s.Str());
    }
    FilePath fp = FilePath("ui/image/crew/", str.c_str());
    i_pPic->SetTex(fp);
}

bool MultiUserGesturePanel::HasNavList() const {
    for (int i = 0; i < 2; i++) {
        if ((&mLeftNavList1)[i] != NULL) {
            return true;
        }
    }
    return false;
}

void MultiUserGesturePanel::UpdateProviderPlayerIndices() {
    for (int i = 0; i < 2; i++) {
        int playerIdx = GetPlayerIndex(i);
        mCharacterProviders[i].SetPlayer(playerIdx);
        mVenueProviders[i].SetPlayer(playerIdx);
        mDifficultyProviders[i].SetPlayer(playerIdx);
        mCrewProviders[i].SetPlayer(playerIdx);
        mOutfitProviders[i].SetPlayer(playerIdx);
    }
}

void MultiUserGesturePanel::UpdateCharPic(
    UIPicture *i_pPic, int i_iSide, int i_iPlayerIndex, Symbol charSym, Symbol outfitSym
) {
    MILO_ASSERT(i_pPic, 0x18e);
    MILO_ASSERT_RANGE(i_iPlayerIndex, 0, 2, 0x18f);
    MILO_ASSERT_RANGE(i_iSide, 0, 2, 0x190);
    static Symbol character_default("character_default");
    HamPlayerData *pPlayerData = TheGameData->Player(i_iPlayerIndex);
    if (charSym == character_default) {
        MetaPerformer *pPerformer = MetaPerformer::Current();
        Symbol primaryCrew;
        Symbol primaryChar;
        Symbol primaryOutfit;
        Symbol secondaryCrew;
        Symbol secondaryChar;
        Symbol secondaryOutfit;
        int songID = TheHamSongMgr.GetSongIDFromShortName(TheGameData->GetSong());
        const HamSongMetadata *pSongData = TheHamSongMgr.Data(songID);
        MILO_ASSERT(pSongData, 0x19d);

        bool check =
            TheGameMode->InMode("dance_battle") || TheGameMode->InMode("strike_a_pose");
        HamPlayerData *pPrimary;
        HamPlayerData *pSecondary;
        pPerformer->CalcCharacters(
            pSongData,
            check,
            (PlayerFlag)i_iPlayerIndex,
            pPrimary,
            primaryCrew,
            primaryChar,
            primaryOutfit,
            pSecondary,
            secondaryCrew,
            secondaryChar,
            secondaryOutfit
        );

        if (pPlayerData == pPrimary) {
            charSym = primaryChar;
            outfitSym = primaryOutfit;
        } else {
            MILO_ASSERT(pPlayerData == pSecondary, 0x1a8);
            charSym = secondaryChar;
            outfitSym = secondaryOutfit;
        }
    } else if (charSym == "") {
        MILO_ASSERT(pPlayerData, 0x1af);
        charSym = pPlayerData->Char();
        outfitSym = pPlayerData->Outfit();
    }

    if (charSym == "") {
        return;
    }

    const CharacterProvider *pProvider = GetCharProvider(i_iSide);
    MILO_ASSERT(pProvider, 0x1ba);

    String str;
    bool contentLocked = !TheProfileMgr.IsContentUnlocked(charSym)
        || !TheProfileMgr.IsContentUnlocked(outfitSym);
    if (contentLocked) {
        static Symbol is_in_party_mode("is_in_party_mode");
        if (TheHamProvider->Property(is_in_party_mode)->Int()) {
            contentLocked = !strstr(outfitSym.Str(), "01") ? contentLocked : false;
        }
    }

    if (contentLocked && !TheGameMode->InMode("campaign")) {
        str = MakeString("%s_locked_keep.png", outfitSym);
    } else if (!pProvider->IsCharacterAvailable(charSym)) {
        str = MakeString("%s_locked_keep.png", outfitSym);
    } else {
        str = MakeString("%s_keep.png", outfitSym);
    }
    FilePath fp = FilePath("ui/image/char/", str.c_str());
    i_pPic->SetTex(fp);
    i_pPic->GetMesh()->SetShowing(true);
}

void MultiUserGesturePanel::UpdateVenueMesh(
    RndMesh *i_pMesh, int i_iSide, int i_iPlayerIndex, Symbol venueSym, Symbol crewSym
) {
    MILO_ASSERT(i_pMesh, 0x208);
    MILO_ASSERT_RANGE(i_iPlayerIndex, 0, 2, 0x209);
    MILO_ASSERT_RANGE(i_iSide, 0, 2, 0x20a);

    const CrewProvider *pProvider = GetCrewProvider(i_iSide);
    MILO_ASSERT(pProvider, 0x20d);

    String texName;
    String matName(MakeString("venue_p%i.mat", i_iSide + 1));
    RndMat *pMat = mDir->Find<RndMat>(matName.c_str(), false);
    MILO_ASSERT(pMat, 0x212);

    if (!TheProfileMgr.IsContentUnlocked(venueSym)) {
        texName = MakeString("venue_%s_locked.tex", crewSym.Str());
    } else {
        texName = MakeString("venue_%s.tex", crewSym.Str());
    }

    RndTex *pTex = mDir->Find<RndTex>(texName.c_str(), false);
    if (pTex != NULL) {
        pMat->SetDiffuseTex(pTex);
        i_pMesh->SetMat(pMat);
    }
}

Symbol MultiUserGesturePanel::GetVoiceCommandOutfitTag(int playerIndex, Symbol screenName) {
    HamPlayerData *pPlayerData = TheGameData->Player(playerIndex);
    Symbol charSym = pPlayerData->Char();
    Symbol result;

    static Symbol screen_name("screen_name");

    int numOutfits = GetNumCharacterOutfits(charSym, false);
    for (int i = 0; i < numOutfits; i++) {
        DataArray *outfitEntry = GetCharacterOutfitEntry(charSym, i, true);
        DataArray *screenNameArr = outfitEntry->FindArray(screen_name, false);
        if (screenNameArr != NULL && screenNameArr->Sym(1) == screenName) {
            result = outfitEntry->Sym(0);
            break;
        }
    }
    return result;
}

void MultiUserGesturePanel::UpdateNavLists(int player) {
    MILO_ASSERT_RANGE(player, 0, 2, 0x9d);
    SkeletonChooser *skeletonChooser = TheHamUI.GetShellInput()->GetSkeletonChooser();
#ifdef HX_NATIVE
    if (!skeletonChooser)
        return; // No Kinect on native
#endif
    MILO_ASSERT(skeletonChooser, 0xa0);
    HamPlayerData *pPlayerData = TheGameData->Player(player);
    int trackingID = pPlayerData->GetSkeletonTrackingID();
    SkeletonSide side = skeletonChooser->GetPlayerSide(player);
    int idx = side != kSkeletonRight;
    if ((&mLeftNavList1)[idx]) {
        (&mLeftNavList1)[idx]->SetSkeletonTrackingID(trackingID);
        if ((trackingID <= 0 && !TheGestureMgr->InControllerMode())
            || TheHamUI.GetOverlayPanel()) {
            (&mLeftNavList1)[idx]->SetSkeletonTrackingID(0);
            (&mLeftNavList1)[idx]->Disengage();
        }
    }
    if ((&mLeftNavList2)[idx]) {
        (&mLeftNavList2)[idx]->SetShowing(true);
        (&mLeftNavList2)[idx]->SetSkeletonTrackingID(trackingID);
        if ((trackingID <= 0 && !TheGestureMgr->InControllerMode())
            || TheHamUI.GetOverlayPanel()) {
            (&mLeftNavList2)[idx]->SetSkeletonTrackingID(0);
            (&mLeftNavList2)[idx]->Disengage();
            if (TheHamUI.GetOverlayPanel()) {
                (&mLeftNavList2)[idx]->SetShowing(false);
            }
        }
    }
}

DataNode MultiUserGesturePanel::OnMsg(const ButtonDownMsg &msg) {
    static Symbol side("side");
    if (msg.GetButton() == kPad_LStickLeft || msg.GetButton() == kPad_DLeft) {
        if (TheUI->FocusPanel() && TheUI->FocusComponent() == mLeftNavList1) {
            TheUI->FocusPanel()->SetFocusComponent(mRightNavList1);
            PropertyEventProvider *multiUserProvider =
                DataDir()->Find<PropertyEventProvider>("multiuser.ep", false);
            if (multiUserProvider) {
                multiUserProvider->SetProperty(side, true);
            }
        }
    }

    if (msg.GetButton() == kPad_LStickRight || msg.GetButton() == kPad_DRight) {
        if (TheUI->FocusPanel() && TheUI->FocusComponent() == mRightNavList1) {
            TheUI->FocusPanel()->SetFocusComponent(mLeftNavList1);
            PropertyEventProvider *multiUserProvider =
                DataDir()->Find<PropertyEventProvider>("multiuser.ep", false);
            if (multiUserProvider) {
                multiUserProvider->SetProperty(side, false);
            }
        }
    }
    return DATA_UNHANDLED;
}

BEGIN_HANDLERS(MultiUserGesturePanel)
    HANDLE_EXPR(
        get_char_provider, const_cast<CharacterProvider *>(GetCharProvider(_msg->Int(2)))
    )
    HANDLE_EXPR(
        get_crew_provider, const_cast<CrewProvider *>(GetCrewProvider(_msg->Int(2)))
    )
    HANDLE_EXPR(
        get_outfit_provider, const_cast<OutfitProvider *>(GetOutfitProvider(_msg->Int(2)))
    )
    HANDLE_EXPR(
        get_difficulty_provider,
        dynamic_cast<DifficultyProvider *>(GetDifficultyProvider(_msg->Int(2)))
    )
    HANDLE_EXPR(
        get_venue_provider, const_cast<VenueProvider *>(GetVenueProvider(_msg->Int(2)))
    )
    HANDLE_EXPR(
        is_skeleton_present, TheGameData->IsSkeletonPresent(GetPlayerIndex(_msg->Int(2)))
    )
    HANDLE_EXPR(is_character_available, IsCharacterAvailable(_msg->Sym(2), _msg->Int(3)))
    HANDLE_EXPR(is_crew_available, IsCrewAvailable(_msg->Sym(2), _msg->Int(3)))
    HANDLE_EXPR(get_character, GetCharacter(_msg->Int(2), _msg->Int(3)))
    HANDLE_ACTION(set_character, SetCharacter(_msg->Sym(2), _msg->Int(3)))
    HANDLE_EXPR(get_outfit, GetOutfit(_msg->Int(2), _msg->Int(3)))
    HANDLE_ACTION(set_outfit, SetOutfit(_msg->Sym(2), _msg->Int(3)))
    HANDLE_ACTION(set_crew, SetCrew(_msg->Sym(2), _msg->Int(3)))
    HANDLE_ACTION(set_default_character, SetDefaultCharacter(_msg->Int(2)))
    HANDLE_ACTION(set_random_crew, SetRandomCrew(_msg->Int(2)))
    HANDLE_ACTION(
        update_char_pic,
        UpdateCharPic(
            _msg->Obj<UIPicture>(2), _msg->Int(3), _msg->Int(4), _msg->Sym(5), _msg->Sym(6)
        )
    )
    HANDLE_ACTION(
        update_crew_pic,
        UpdateCrewPic(_msg->Obj<UIPicture>(2), _msg->Int(3), _msg->Int(4), _msg->Sym(5))
    )
    HANDLE_ACTION(
        update_venue_mesh,
        UpdateVenueMesh(
            _msg->Obj<RndMesh>(2), _msg->Int(3), _msg->Int(4), _msg->Sym(5), _msg->Sym(6)
        )
    )
    HANDLE_EXPR(get_character_index, GetCharacterIndex(_msg->Int(2)))
    HANDLE_EXPR(get_outfit_index, GetOutfitIndex(_msg->Int(2)))
    HANDLE_EXPR(get_venue_index, GetVenueIndex(_msg->Int(2), _msg->Sym(3)))
    HANDLE_EXPR(get_crew_index, GetCrewIndex(_msg->Int(2)))
    HANDLE_EXPR(get_player_index, GetPlayerIndex(_msg->Int(2)))
    HANDLE_ACTION(update_provider_player_indices, UpdateProviderPlayerIndices())
    HANDLE_ACTION(drop_side, DropPlayerOnSide(_msg->Int(2)))
    HANDLE_EXPR(
        get_voice_command_outfit_tag, GetVoiceCommandOutfitTag(_msg->Int(2), _msg->Sym(3))
    )
    HANDLE_ACTION(update_providers, UpdateProviders())
    HANDLE_MESSAGE(ButtonDownMsg) HANDLE_SUPERCLASS(TexLoadPanel)
END_HANDLERS
