#include "MQSongSortMgr.h"

#include "Campaign.h"
#include "NavListSortMgr.h"
#include "hamobj/HamGameData.h"
#include "HamSongMgr.h"
#include "MQSongSortByCharacter.h"
#include "MQSongSortNode.h"
#include "ProfileMgr.h"
#include "obj/Dir.h"

MQSongSortMgr::MQSongSortMgr(SongPreview &sp) : NavListSortMgr(sp) {
    SetName("mq_song_provider", ObjectDir::Main());
    mSorts.push_back(new MQSongSortByCharacter());
}

MQSongSortMgr::~MQSongSortMgr() {};

MQSongSort::MQSongSort() {};

void MQSongSortMgr::Init(SongPreview &preview) {
    MILO_ASSERT(!TheMQSongSortMgr, 0x18);
    TheMQSongSortMgr = new MQSongSortMgr(preview);
    TheContentMgr.RegisterCallback(TheMQSongSortMgr, false);
}

void MQSongSortMgr::OnEnter() {
    mHeadersSelectable = true;
    UpdateList();
    FOREACH (it, mSorts) {
        (*it)->BuildTree();
    }
    NavListSort *sort = mSorts[mCurrentSortIdx];
    sort->BuildItemList();
    if (mHighlightSaved) {
        sort->SetHighlightID(mSavedHighlightID);
        mHighlightSaved = false;
    }
    sort->UpdateHighlight();
}

Symbol MQSongSortMgr::MoveOn() {
    MILO_ASSERT(0, 0x45);
    return gNullStr;
}

bool MQSongSortMgr::SelectionIs(Symbol sym) {
    static Symbol challenge("challenge");
    static Symbol header("header");
    if (sym == challenge) {
        return dynamic_cast<MQSongSortNode *>(GetHighlightItem()) != nullptr;
    } else if (sym == header) {
        return dynamic_cast<MQSongHeaderNode *>(GetHighlightItem()) != nullptr;
    }
    return false;
}

bool MQSongSortMgr::IsCharacter(Symbol sym) const {
    FOREACH (it, mCharacterSongs) {
        if (it->first == sym) {
            return true;
        }
    }
    return false;
}

void MQSongSortMgr::UpdateList() {
    MILO_ASSERT(TheCampaign, 0x6e);
    if (mFlatList.begin() != mFlatList.end()) {
        mFlatList.clear();
    }
    Symbol mqCrew = TheCampaign->GetMQCrew();
    mCharacterSongs.clear();
    const std::vector<int> &rankedSongs = TheHamSongMgr.RankedSongs((SongType)1);
    FOREACH (it, rankedSongs) {
        const HamSongMetadata *metadata = TheHamSongMgr.Data(*it);
        Symbol character = GetOutfitCharacter(metadata->Outfit(), true);
        Symbol crew = GetCrewForCharacter(character, true);
        Symbol charCopy = character;
        Symbol mqHeader = MakeString<char>("mqheader_%s", charCopy);
        if (metadata->IsFake() == false && crew == mqCrew
            && TheProfileMgr.IsContentUnlocked(metadata->ShortName())) {
            mCharacterSongs[mqHeader].push_back(TheHamSongMgr.GetShortNameFromSongID(*it));
        }
    }
    FOREACH (it, mCharacterSongs) {
        mFlatList.push_back(it->first);
        FOREACH (it2, it->second) {
            mFlatList.push_back(*it2);
        }
    }
}

bool MQSongSortMgr::IsSong(Symbol sym) const {
    for (auto it = mCharacterSongs.begin(); it != mCharacterSongs.end() && it->first != sym; ++it) {
        std::vector<Symbol> syms = it->second;
        FOREACH (it2, syms) {
            if (*it2 == sym) {
                return true;
            }
        }
    }
    return false;
}
