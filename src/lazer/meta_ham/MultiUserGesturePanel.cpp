#include "meta_ham/MultiUserGesturePanel.h"
#include "HamPanel.h"
#include "MultiUserGesturePanel.h"
#include "flow/PropertyEventProvider.h"
#include "game/GameMode.h"
#include "gesture/BaseSkeleton.h"
#include "hamobj/HamGameData.h"
#include "hamobj/HamPlayerData.h"
#include "meta_ham/CharacterProvider.h"
#include "meta_ham/CrewProvider.h"
#include "meta_ham/DifficultyProvider.h"
#include "meta_ham/HamUI.h"
#include "meta_ham/MetaPerformer.h"
#include "meta_ham/OutfitProvider.h"
#include "meta_ham/ProfileMgr.h"
#include "meta_ham/SkeletonChooser.h"
#include "meta_ham/TexLoadPanel.h"
#include "meta_ham/VenueProvider.h"
#include "obj/Data.h"
#include "obj/Msg.h"
#include "obj/Object.h"
#include "os/Debug.h"
#include "os/Joypad.h"
#include "os/JoypadMsgs.h"
#include "rndobj/Draw.h"
#include "rndobj/Mesh.h"
#include "ui/UI.h"
#include "ui/UIPicture.h"
#include "utl/FilePath.h"
#include "utl/MakeString.h"
#include "utl/Symbol.h"

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
    static Message refresh_ui("refresh_ui");
    TheUI->Handle(refresh_ui, false);
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
    int check = prop->Int();
    if (check == 0) {
        check = playerSide - 1;
        if ((unsigned int)idx == 0) {
            idx = check - ((playerSide - 2) + (playerSide - 1 == 0));
        } else {
            idx = playerSide == 0;
        }
    }
    return idx;
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
    } else if (pProvider->IsCrewAvailable(s)) {
        str = MakeString("%s_char_keep.png", s.Str());
    }
    FilePath fp = FilePath("ui/image/crew/", str.c_str());
    i_pPic->SetTex(fp);
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
    SkeletonSide playerSide = skeletonChooser->GetPlayerSide(player);

    int sideIdx = playerSide - 1;

    // Get nav lists for this player's side using array indexing
    // Array is [mLeftNavList1, mRightNavList1, mLeftNavList2, mRightNavList2]
    // For left side (sideIdx=-1): indices 20, 22 map to mLeftNavList1, mLeftNavList2
    // For right side (sideIdx=0): indices 21, 23 map to mRightNavList1, mRightNavList2
    int offset1 = (sideIdx + 0x15) * 4;
    auto basePtr = reinterpret_cast<u8 *>(this);
    HamNavList *navList1 = *reinterpret_cast<HamNavList **>(basePtr + offset1);

    if (navList1) {
        navList1->SetSkeletonTrackingID(trackingID);
        auto hamUIBytes = reinterpret_cast<u8 *>(&TheHamUI);
        if (((trackingID <= 0) && (*(char *)(reinterpret_cast<u8 *>(TheGestureMgr) + 0x426d) == '\0')) ||
            (*reinterpret_cast<u32 *>(hamUIBytes + 0xf0) != 0)) {
            navList1->SetSkeletonTrackingID(0);
            navList1->Disengage();
        }
    }

    auto thisBytes = reinterpret_cast<u8 *>(this);
    int offset2 = (sideIdx + 0x17) * 4;
    HamNavList *navList2 = *reinterpret_cast<HamNavList **>(thisBytes + offset2);

    if (navList2) {
        void **ptr = reinterpret_cast<void **>(reinterpret_cast<u8 *>(navList2) + 4);
        void *obj = *ptr;
        void *drawablePtr = *reinterpret_cast<void **>(reinterpret_cast<u8 *>(obj) + 0xc);
        drawablePtr = reinterpret_cast<u8 *>(drawablePtr) + 4;
        reinterpret_cast<RndDrawable *>(drawablePtr)->SetShowing(true);

        navList2->SetSkeletonTrackingID(trackingID);
        if (((trackingID <= 0) && (*(char *)(reinterpret_cast<u8 *>(TheGestureMgr) + 0x426d) == '\0')) ||
            (*reinterpret_cast<u32 *>(reinterpret_cast<u8 *>(&TheHamUI) + 0xf0) != 0)) {
            navList2->SetSkeletonTrackingID(0);
            navList2->Disengage();
            if (*reinterpret_cast<u32 *>(reinterpret_cast<u8 *>(&TheHamUI) + 0xf0) != 0) {
                void **ptr2 = reinterpret_cast<void **>(reinterpret_cast<u8 *>(navList2) + 4);
                void *obj2 = *ptr2;
                void *drawablePtr2 = *reinterpret_cast<void **>(reinterpret_cast<u8 *>(obj2) + 0xc);
                drawablePtr2 = reinterpret_cast<u8 *>(drawablePtr2) + 4;
                reinterpret_cast<RndDrawable *>(drawablePtr2)->SetShowing(false);
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
