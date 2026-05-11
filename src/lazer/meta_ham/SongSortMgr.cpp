#include "SongSortMgr.h"

#include "HamSongMgr.h"
#include "NavListSortMgr.h"
#include "SongSortBySong.h"
#include "game/GameMode.h"
#include "SongSort.h"
#include "SongSortByDiff.h"
#include "SongSortByLocation.h"
#include "SongSortNode.h"
#include "meta/SongPreview.h"
#include "meta_ham/HamSongMgr.h"
#include "meta_ham/MetaPerformer.h"
#include "obj/Data.h"
#include "obj/Dir.h"
#include "obj/Msg.h"
#include "obj/Object.h"
#include "os/Debug.h"
#include "stl/_vector.h"
#include "ui/UI.h"
#include "meta_ham/MetaPerformer.h"
#include "ProfileMgr.h"
#include "ui/UIListProvider.h"
#include "ui/UIPanel.h"
#include "utl/Std.h"
#include "utl/Symbol.h"
BEGIN_HANDLERS(SongSortMgr)
    HANDLE_ACTION(get_setlist_mode, 0)
    HANDLE_ACTION(set_setlist_mode, SetSetlistMode(_msg->Int(2) != 0))
    HANDLE_ACTION(
        mark_elements_provided, MarkElementsProvided(_msg->Obj<UIListProvider>(2))
    )
    HANDLE_ACTION(
        mark_element_in_playlist, MarkElementInPlaylist(_msg->Sym(2), _msg->Int(3) != 0)
    )
    HANDLE_ACTION(setup_quasi_random_songs, SetupQuasiRandomSongs())
    HANDLE_ACTION(set_quasi_random_song, SetQuasiRandomSong())
    HANDLE_EXPR(first_artist_song_index, FirstArtistSongIndex(_msg->Sym(2)))
    HANDLE_EXPR(
        determine_header_symbol_for_song, DetermineHeaderSymbolForSong(_msg->Sym(2))
    )
    HANDLE_SUPERCLASS(NavListSortMgr)
END_HANDLERS

#ifndef HX_NATIVE
SongSort::SongSort() {}
#endif

SongSortMgr::SongSortMgr(SongPreview &sp) : NavListSortMgr(sp) {
    SetName("song_offer_provider", ObjectDir::Main());
    mSorts.push_back(new SongSortByDiff());
    mSorts.push_back(new SongSortBySong());
    mSorts.push_back(new SongSortByLocation());
    mRankedSongCount = 0;
}

SongSortMgr::~SongSortMgr() {}

SongSortByDiff::SongSortByDiff() {
    static Symbol by_difficulty("by_difficulty");
    SetSortName(by_difficulty);
}

SongSortByLocation::SongSortByLocation() {
    static Symbol by_location("by_location");
    SetSortName(by_location);
}

void SongSortMgr::Init(SongPreview &preview) {
    MILO_ASSERT(!TheSongSortMgr, 0x21);
    TheSongSortMgr = new SongSortMgr(preview);
    TheContentMgr.RegisterCallback(TheSongSortMgr, false);
}

void SongSortMgr::Terminate() {
    TheContentMgr.UnregisterCallback(TheSongSortMgr, false);
    MILO_ASSERT(TheSongSortMgr, 0x2b);
    if (!TheSongSortMgr) {
        TheSongSortMgr = nullptr;
        return;
    }
    RELEASE(TheSongSortMgr);
}

bool SongSortMgr::SelectionIs(Symbol selection) {
    static Symbol song("song");
    static Symbol header("header");
    static Symbol function("function");
    if (selection == song) {
        return dynamic_cast<SongSortNode *>(GetHighlightItem()) != nullptr;
    } else if (selection == header) {
        return dynamic_cast<SongHeaderNode *>(GetHighlightItem()) != nullptr;
    } else if (selection == function) {
        return dynamic_cast<SongFunctionNode *>(GetHighlightItem()) != nullptr;
    }
    return false;
}

void SongSortMgr::OnHighlightChanged() {
    this->UnHighlightCurrent();
    MILO_ASSERT(GetHighlightItem(), 0x75);
    GetHighlightItem()->OnHighlight();
    if (!TheUI->GetTransitionState()) {
        static Symbol refresh_selected_song("refresh_selected_song");
        static Message msg(refresh_selected_song);
        TheUI->Handle(msg, false);
    }
}

void SongSortMgr::MarkElementsProvided(UIListProvider *prov) {
    if (!prov) return;
    int count = prov->NumData();
    int i = 0;
    if (count > 0) {
        do {
            Symbol sym = prov->DataSymbol(i);
            SongSortNode *ssn =
                dynamic_cast<SongSortNode *>(GetCurrentSort()->GetNode(sym));
            if (ssn) {
                ssn->SetInPlaylist(true);
            }
            i++;
        } while (i < count);
    }
}

void SongSortMgr::MarkElementInPlaylist(Symbol sym, bool b) {
    NavListSort *sort = GetCurrentSort();
    SongSortNode *ssn = dynamic_cast<SongSortNode *>(sort->GetNode(sym));
    if (ssn) {
        ssn->SetInPlaylist(b);
    }
}

int SongSortMgr::GetListIndexFromHeaderIndex(int idx) {
    int size = mHeadersB.size();
    if (idx < 0) {
        if (0 < size) {
            return mHeadersB.front();
        }
        return 1;
    }
    if (idx >= size) {
        if (0 < size) {
            return mHeadersB[size - 1];
        }
        return 1;
    }
    return mHeadersB[idx];
}

void SongSortMgr::OnSetlistChanged() {
    mSorts[mCurrentSortIdx]->BuildItemList();
    mSorts[mCurrentSortIdx]->UpdateHighlight();
    static Symbol refresh_setlist("refresh_setlist");
    static Message msg(refresh_setlist);
    TheUI->Handle(msg, false);
}

void SongSortMgr::OnSetlistModeChanged() {
    mSorts[mCurrentSortIdx]->BuildItemList();
    mSorts[mCurrentSortIdx]->UpdateHighlight();

    static Symbol on_change_setlist_mode("on_change_setlist_mode");
    static Message msg(on_change_setlist_mode);
    TheUI->Handle(msg, false);

    static Symbol update_held_buttons("update_held_buttons");
    static Message held_button_msg(update_held_buttons);
    TheUI->Handle(held_button_msg, false);
}

Symbol SongSortMgr::DetermineHeaderSymbolForSong(Symbol sym) {
    return static_cast<SongSort *>(GetCurrentSort())->DetermineHeaderSymbolFromSong(sym);
}

void SongSortMgr::SetQuasiRandomSong() {
    int numIndices = mQuasiRandomIndices.size();
    MILO_ASSERT(numIndices > 0, 0x175);

    int randVal = rand() % (numIndices / 2);
    int val = mQuasiRandomIndices[randVal];
    mQuasiRandomIndices.erase(mQuasiRandomIndices.begin() + randVal);
    mQuasiRandomIndices.push_back(val);

    NavListSortNode *node = mSorts[mCurrentSortIdx]->GetListFromIdx(val);
    MetaPerformer *performer = MetaPerformer::Current();
    performer->SetSong(node->GetToken());
}

bool SongSortMgr::HeadersSelectable() {
    static Symbol song_select_mode("song_select_mode");
    static Symbol song_select_story("song_select_story");
    static Symbol song_select_practice("song_select_practice");
    Symbol mode;
    mode = TheGameMode->Property(song_select_mode, true)->Sym();
    if (song_select_story != mode) {
        mode = TheGameMode->Property(song_select_mode, true)->Sym();
        if (song_select_practice != mode) {
            return true;
        }
    }
    return false;
}

bool SongSortMgr::DataIs(int i1, Symbol sym) {
    static Symbol song("song");
    static Symbol header("header");
    static Symbol function("function");
    NavListSortNode *node = mSorts[mCurrentSortIdx]->GetListFromIdx(i1);
    if (sym == song) {
        return dynamic_cast<SongSortNode *>(node) != 0;
    } else if (sym == header) {
        return dynamic_cast<SongHeaderNode *>(node) != 0;
    } else if (sym == function) {
        return dynamic_cast<SongFunctionNode *>(node) != 0;
    }
    return false;
}

int SongSortMgr::FirstArtistSongIndex(Symbol sym) {
    int result = 0;
    int dataCount = mSorts[mCurrentSortIdx]->GetDataCount();

    for (int i = 0; i < dataCount; i++) {
        SongSortNode *ssNode =
            dynamic_cast<SongSortNode *>(mSorts[mCurrentSortIdx]->GetListFromIdx(i));
        if (ssNode != 0) {
            const char *artist = ssNode->GetArtist();
            if (strcmp(artist, sym.Str()) == 0) {
                result = GetHeaderIndexFromChildListIndex(i);
                break;
            }
        }
    }
    return result;
}

void SongSortMgr::RebuildSongRecordMap() {
    mSongRecordMap.clear();
    std::vector<int> rankedSongs;
    TheHamSongMgr.GetRankedSongs(rankedSongs);
    mRankedSongCount = rankedSongs.size();
    FOREACH (it, rankedSongs) {
        const HamSongMetadata *metadata = TheHamSongMgr.Data(*it);
        if (metadata && !metadata->IsFake()
            && TheProfileMgr.IsContentUnlocked(metadata->ShortName())) {
            SongRecord second(metadata);
            mSongRecordMap.insert(std::pair<Symbol, SongRecord>(second.ShortName(), second));
        }
    }
}

Symbol SongSortMgr::MoveOn() {
    static Symbol song_select_quickplay("song_select_quickplay");
    static Symbol song_select_story("song_select_story");
    static Symbol song_select_practice("song_select_practice");
    static Symbol song_select_jukebox("song_select_jukebox");

    Symbol mode = TheGameMode->Property("song_select_mode", true)->Sym();
    if (song_select_quickplay == mode) {
        static Symbol move_on_quickplay("move_on_quickplay");
        UIPanel *songSelectPanel =
            ObjectDir::Main()->Find<UIPanel>("song_select_panel", true);
        static Message msg("move_on_quickplay");
        songSelectPanel->HandleType(msg);
        return gNullStr;
    } else if (song_select_story != mode && song_select_practice != mode) {
        if (mode != song_select_jukebox) {
        MILO_FAIL("Unknown song_select_mode\n");
        return gNullStr;
    } else {
        Symbol ready_screen("ready_screen");
        const DataNode *prop = TheGameMode->Property(ready_screen);
        return prop->Sym();
    }
    } else {
        Symbol ready_screen("ready_screen");
        const DataNode *prop = TheGameMode->Property(ready_screen);
        return prop->Sym();
    }
}

void SongSortMgr::OnEnter() {
    MetaPerformer::Current()->ResetSongs();
    static Symbol bass("bass");
    static Symbol guitar("guitar");
    static Symbol band("band");
    RebuildSongRecordMap();
    FOREACH (it, mSorts) {
        (*it)->BuildTree();
    }
    auto current = mSorts[mCurrentSortIdx];
    current->BuildItemList();
    if (mHighlightSaved) {
        current->SetHighlightID(mSavedHighlightID);
        mHighlightSaved = false;
    }
    current->UpdateHighlight();
}

void SongSortMgr::SetupQuasiRandomSongs() {
    int dataCount = mSorts[mCurrentSortIdx]->GetDataCount();
    unsigned int *tempIndices = new unsigned int[dataCount];
    mQuasiRandomIndices.erase(mQuasiRandomIndices.begin(), mQuasiRandomIndices.end());
    int i = 0;
    if (dataCount > 0) {
        unsigned int *ptr = tempIndices - 1;
        int count = dataCount;
        do {
            ptr++;
            *ptr = i;
            i++;
        } while (--count);
        unsigned int *endPtr = tempIndices + dataCount;
        do {
            int randIdx = rand() % dataCount;
            int value = tempIndices[randIdx];
            SongSortNode *ssn =
                dynamic_cast<SongSortNode *>(mSorts[mCurrentSortIdx]->GetListFromIdx(value));
            if (ssn != nullptr && !ssn->IsMedley() && !ssn->IsFake()
                && TheProfileMgr.IsContentUnlocked(ssn->Record()->ShortName())) {
                mQuasiRandomIndices.push_back(value);
            }
            endPtr--;
            dataCount--;
            tempIndices[randIdx] = *endPtr;
        } while (0 < dataCount);
    }
    delete[] tempIndices;
    SetQuasiRandomSong();
}

void SongSortMgr::SetSetlistMode(bool b) {
    static Symbol song_select_story("song_select_story");
    MILO_ASSERT(TheGameMode->Property("song_select_mode")->Sym() != song_select_story, 0xa7);
    if (b) {
        std::vector<Symbol> songs;
        TheHamSongMgr.GetValidSongs(*MetaPerformer::Current(), songs);
        static Symbol any("any");
        for (int i = 0; i < Min<int>(songs.size(), 100); i++) {
            MetaPerformer::Current()->SetSong(any);
        }
    } else {
        MetaPerformer::Current()->ResetSongs();
    }
    OnSetlistModeChanged();
}
