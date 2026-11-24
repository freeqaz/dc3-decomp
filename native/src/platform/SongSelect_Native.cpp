// DC3 Native Port - song select additions.
// See SongSelect_Native.h and docs/native/SONG_SELECT.md.

#include "SongSelect_Native.h"

#include "NativeSettings.h" // dc3's, same directory: Dc3EnvFlag
#include "PgSongs_Native.h"
#include "SongBrowserHooks_Native.h"
#include "SongSelectState_Native.h"
#include "game/GameMode.h"
#include "hamobj/HamListRibbon.h"
#include "hamobj/HamNavList.h"
#include "math/Utl.h"
#include "meta/Sorting.h"
#include "meta_ham/AppLabel.h"
#include "meta_ham/HamSongMgr.h"
#include "meta_ham/Playlist.h"
#include "meta_ham/SongSortMgr.h"
#include "obj/Data.h"
#include "os/Joypad.h"
#include "ui/UILabel.h"
#include "ui/UIListLabel.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <utility>
#include <vector>

bool Dc3ClassicSongSelect() {
    static const bool sClassic = Dc3EnvFlag("DC3_CLASSIC_SONG_SELECT", false);
    return sClassic;
}

bool NativePgSongs::Enabled() {
    static const bool enabled = Dc3EnvFlag("DC3_PG_SONGS", true);
    return enabled;
}

bool NativePgSongs::AllowsSong(Symbol shortName) {
    if (!Enabled()) return true;

    // The disc and official import packs use the same rating for every song.
    // Curate by the game's short names and consider the art shown in the browser.
    static const char *const songs[] = {
        "aroundtheworld",
        "betteroffalone",
        "bewareoftheboys",
        "cupidshuffle",
        "discoinferno",
        "electricboogie",
        "hello",
        "iamthebest",
        "iwillsurvive",
        "letthemusicplay",
        "macarena",
        "sambadejaneiro",
        "supersonic",
        "thehustle",
        "turnthebeataround",
        "whatdoesntkillyou",
        "ymca",
        "yougotittherightstuff",
        // Official imports and DLC.
        "bulletproof",
        "control",
        "dance",
        "daysgoby",
        "dragosteadintei",
        "escapade",
        "funkytown",
        "gonnamakeyousweat",
        "iwishforyou",
        "jungleboogie",
        "lalaland",
        "lefreak",
        "neversaynever",
        "nothinonyou",
        "planetrock",
        "pondereplay",
        "pumpupthejam",
        "replay",
        "roundandround",
        "sandstorm",
        "satellite",
        "sayhey",
        "somebodytolove",
        "straightup",
        "technologic",
        "venus",
        "whatislove",
        "whipmyhair",
        "wordup",
        // Additional mounted packs.
        "aroundtheworldlalala",
        "beautifullife",
        "chericherilady",
        "clocks",
        "dancingqueen",
        "faded",
        "feelthelight",
        "finalcountdown",
        "flames",
        "gotmymindsetonyou",
        "halo",
        "heroes",
        "ilikechopin",
        "illbethereforyou",
        "jumpvanhalen",
        "lalalashakira",
        "lights",
        "thefox",
        "thesafetydance",
        "tremor",
        "tsunami",
        "ucanttouchthis",
        "wakawaka",
        "whatsup",
    };
    for (const char *song : songs) {
        if (std::strcmp(shortName.Str(), song) == 0) return true;
    }
    return false;
}

bool NativePgSongs::AllowsPlaylist(const Playlist &playlist) {
    if (!Enabled()) return true;
    for (int i = 0; i < playlist.GetNumSongs(); ++i) {
        int id = playlist.GetSong(i);
        if (id <= 0 || !AllowsSong(TheHamSongMgr.GetShortNameFromSongID(id, false))) {
            return false;
        }
    }
    return true;
}

// ---------------------------------------------------------------------------
// Play history
// ---------------------------------------------------------------------------

void NativeSongSelect::NotePlayed(Symbol song) {
    if (song.Null()) return;
    // Kept under the classic switch too: the switch is about what song select
    // shows, and the list should be right the day it is turned back off.
    TheNativeSongSelectState().NotePlayed(song.Str());
}

bool NativeSongSelect::ShowsRecentlyPlayed() {
    if (Dc3ClassicSongSelect()) return false;
    static Symbol song_select_mode("song_select_mode");
    static Symbol song_select_quickplay("song_select_quickplay");
    return TheGameMode->Property(song_select_mode, true)->Sym() == song_select_quickplay;
}

bool NativeSongSelect::IsRecentRow(const NavListNode *node) {
    return node && dynamic_cast<const RecentlyPlayedHeaderNode *>(node->Parent()) != nullptr;
}

// ---------------------------------------------------------------------------
// Recently Played
// ---------------------------------------------------------------------------

Symbol RecentlyPlayedHeaderNode::Token() {
    static Symbol recently_played("recently_played");
    return recently_played;
}

RecentlyPlayedHeaderNode::RecentlyPlayedHeaderNode()
    : SongHeaderNode(new RecentlyPlayedCmp(), Token(), false) {}

void RecentlyPlayedHeaderNode::Text(UIListLabel *listLabel, UILabel *label) const {
    // The token is a name for scripts; the title is not in the image's locale.
    if (listLabel->Matches("sort_header") || listLabel->Matches("group")
        || listLabel->Matches("famousby_group")) {
        label->SetPrelocalizedString(String("Recently Played"));
        return;
    }
    SongHeaderNode::Text(listLabel, label);
}

// The rows are plain SongSortNodes over the same SongRecord as the song's own
// row, so everything a row does -- text, stars, album art, preview, select --
// is that row's code.  Only their place in the tree is different.
void SongSort::AddRecentlyPlayedSection() {
    if (!NativeSongSelect::ShowsRecentlyPlayed()) return;
    std::map<Symbol, SongRecord> &records = TheSongSortMgr->mSongRecordMap;
    const std::vector<std::string> &history = TheNativeSongSelectState().History();
    NavListShortcutNode *shortcut = nullptr;
    RecentlyPlayedHeaderNode *header = nullptr;
    int rows = 0;
    for (size_t i = 0; i < history.size() && rows < NativeSongSelect::kRecentRows; i++) {
        // Not in this list: a pack that is no longer installed, a locked song.
        std::map<Symbol, SongRecord>::iterator record =
            records.find(Symbol(history[i].c_str()));
        if (record == records.end()) continue;
        if (!header) {
            shortcut = new NavListShortcutNode(
                new RecentlyPlayedCmp(), RecentlyPlayedHeaderNode::Token(), false
            );
            header = new RecentlyPlayedHeaderNode();
            header->SetShortcut(shortcut);
            header->SetParent(shortcut);
            shortcut->InsertNode(header);
        }
        SongSortNode *row = new SongSortNode(new RecentlyPlayedCmp(), &record->second);
        row->SetShortcut(shortcut);
        row->SetParent(header);
        header->Children().push_back(row);
        header->UpdateItemCount(row);
        rows++;
    }
    if (shortcut) mShortcutNodes.insert(mShortcutNodes.begin(), shortcut);
}

// ---------------------------------------------------------------------------
// Sort by artist
// ---------------------------------------------------------------------------

ArtistCmp::ArtistCmp(const char *artist, const char *title)
    : mArtist(artist), mTitle(title), mFirstRank(SortCharRank(artist)),
      mLastRank(SortCharRank(artist)) {}

ArtistCmp::ArtistCmp(const char *firstArtist, const char *lastArtist, const char *title)
    : mArtist(firstArtist), mTitle(title), mFirstRank(SortCharRank(firstArtist)),
      mLastRank(SortCharRank(lastArtist)) {}

int ArtistCmp::SortCharRank(const char *artist) {
    unsigned char first = (unsigned char)*AlphaKeySkip(artist, true);
    if (first < 0x80 && isalpha(first)) return toupper(first) - 'A' + 1;
    return 0;
}

const char *ArtistCmp::SortCharLabel(int rank) {
    static const char *const kLetters[] = { "123", "A", "B", "C", "D", "E", "F", "G", "H",
                                            "I",   "J", "K", "L", "M", "N", "O", "P", "Q",
                                            "R",   "S", "T", "U", "V", "W", "X", "Y", "Z" };
    return kLetters[Clamp(0, 26, rank)];
}

int ArtistCmp::Compare(const NavListItemSortCmp *cmp, NavListNodeType type) const {
    switch (type) {
    case kNodeShortcut:
        return 0;
    case kNodeHeader: {
        // Ranges of sort characters: equal when they overlap, which is how an
        // item finds the section it belongs to.
        const ArtistCmp *other = cmp->GetArtistCmp();
        if (mLastRank < other->mFirstRank) return -1;
        if (mFirstRank > other->mLastRank) return 1;
        return 0;
    }
    case kNodeItem: {
        const ArtistCmp *other = cmp->GetArtistCmp();
        if (mFirstRank != other->mFirstRank) return mFirstRank - other->mFirstRank;
        int byArtist = AlphaKeyStrCmp(mArtist, other->mArtist, true);
        if (byArtist != 0) return byArtist;
        return AlphaKeyStrCmp(mTitle, other->mTitle, false);
    }
    default:
        MILO_FAIL("invalid type of node comparison.\n");
    }
    return 0;
}

namespace {

const char *ArtistOf(NavListItemNode *item) {
    return static_cast<SongSortNode *>(item)->Record()->Metadata()->Artist();
}

// "M" for one sort character, "A - C" for several.
Symbol ArtistSectionLabel(NavListItemNode *first, NavListItemNode *last) {
    int firstRank = ArtistCmp::SortCharRank(ArtistOf(first));
    int lastRank = ArtistCmp::SortCharRank(ArtistOf(last));
    if (firstRank == lastRank) return Symbol(ArtistCmp::SortCharLabel(firstRank));
    char label[16];
    snprintf(
        label,
        sizeof(label),
        "%s - %s",
        ArtistCmp::SortCharLabel(firstRank),
        ArtistCmp::SortCharLabel(lastRank)
    );
    return Symbol(label);
}

} // namespace

SongSortByArtist::SongSortByArtist() {
    static Symbol by_artist("by_artist");
    SetSortName(by_artist);
}

NavListItemNode *SongSortByArtist::NewItemNode(void *data) const {
    SongRecord *record = static_cast<SongRecord *>(data);
    return new SongSortNode(
        new ArtistCmp(record->Metadata()->Artist(), record->Metadata()->Title()), record
    );
}

NavListShortcutNode *SongSortByArtist::NewShortcutNode(NavListItemNode *item) const {
    return new NavListShortcutNode(
        new ArtistCmp(ArtistOf(item), nullptr), ArtistSectionLabel(item, item), false
    );
}

NavListHeaderNode *SongSortByArtist::NewHeaderNode(NavListItemNode *item) const {
    return NewHeaderNode(item, item);
}

NavListHeaderNode *
SongSortByArtist::NewHeaderNode(NavListItemNode *first, NavListItemNode *last) const {
    return new SongHeaderNode(
        new ArtistCmp(ArtistOf(first), ArtistOf(last), nullptr),
        ArtistSectionLabel(first, last),
        false
    );
}

void SongSortByArtist::BuildTree() {
    DeleteTree();
    Init();

    std::vector<NavListItemNode *> songs;
    std::map<Symbol, SongRecord> &records = TheSongSortMgr->GetUnk78();
    FOREACH (it, records) {
        songs.push_back(NewItemNode(&it->second));
    }
    std::stable_sort(songs.begin(), songs.end(), CompareItems());

    // Sections are whole sort characters, joined with the next until there
    // are kMinSectionSongs of them; a short tail joins the section before it.
    std::vector<std::pair<int, int> > sections;
    int numSongs = songs.size();
    int begin = 0;
    while (begin < numSongs) {
        int end = begin;
        while (end < numSongs && end - begin < kMinSectionSongs) {
            int rank = ArtistCmp::SortCharRank(ArtistOf(songs[end]));
            while (end < numSongs && ArtistCmp::SortCharRank(ArtistOf(songs[end])) == rank) {
                end++;
            }
        }
        if (end - begin < kMinSectionSongs && !sections.empty()) {
            sections.back().second = end;
        } else {
            sections.push_back(std::make_pair(begin, end));
        }
        begin = end;
    }

    for (size_t i = 0; i < sections.size(); i++) {
        NavListItemNode *first = songs[sections[i].first];
        NavListItemNode *last = songs[sections[i].second - 1];
        NavListShortcutNode *shortcut = new NavListShortcutNode(
            new ArtistCmp(ArtistOf(first), ArtistOf(last), nullptr),
            ArtistSectionLabel(first, last),
            false
        );
        NavListHeaderNode *header = NewHeaderNode(first, last);
        header->SetShortcut(shortcut);
        header->SetParent(shortcut);
        shortcut->InsertNode(header);
        for (int song = sections[i].first; song < sections[i].second; song++) {
            songs[song]->SetShortcut(shortcut);
            songs[song]->SetParent(header);
            header->Children().push_back(songs[song]);
            header->UpdateItemCount(songs[song]);
        }
        mShortcutNodes.push_back(shortcut);
    }

    AddRecentlyPlayedSection();

    FOREACH (it, mShortcutNodes) {
        (*it)->FinishSort(this);
    }
}

NavListSort *NativeSongSelect::NewSongSortByArtist() { return new SongSortByArtist(); }

bool NativeSongSelect::ArtistRowText(
    const SongSortNode *row, UIListLabel *slot, UILabel *label
) {
    if (Dc3ClassicSongSelect()) return false;
    static Symbol by_artist("by_artist");
    if (TheSongSortMgr->GetCurrentSortName() != by_artist) return false;
    if (slot->Matches("header_text")) {
        AppLabel *appLabel = dynamic_cast<AppLabel *>(label);
        if (appLabel) {
            appLabel->SetArtistName(row->Record()->ShortName(), true);
        } else {
            label->SetPrelocalizedString(String(row->GetArtist()));
        }
        return true;
    }
    // "As made famous by" is drawn where the artist now is.
    if (slot->Matches("asmadefamous")) {
        label->SetTextToken(gNullStr);
        return true;
    }
    return false;
}

// ---------------------------------------------------------------------------
// SongSortMgr: the messages song_select.dta and HamNavList send
// ---------------------------------------------------------------------------

namespace {

// The row song select was left on, as the path NavListNode::GetID writes
// ([shortcut, header, song]).  The image's script restores the highlight by
// list index, which is another row as soon as Recently Played has changed.
DataArray *sFocusID = nullptr;

} // namespace

DataNode SongSortMgr::NativeHandle(DataArray *msg) {
    static Symbol nav_jump_row("nav_jump_row");
    static Symbol sort_labels("sort_labels");
    static Symbol saved_sort_index("saved_sort_index");
    static Symbol save_sort("save_sort");
    static Symbol save_focus("save_focus");
    static Symbol restore_focus("restore_focus");
    static Symbol visible_text("visible_text");
    Symbol sym = msg->Sym(1);
    if (sym == nav_jump_row) {
        // Unhandled under the classic switch: the buttons are the image's.
        if (Dc3ClassicSongSelect()) return DATA_UNHANDLED;
        return NavJumpRow(msg->Int(2), msg->Int(3), msg->Int(4));
    }
    if (sym == sort_labels) {
        DataArray *labels = SortLabels();
        DataNode node(labels, kDataArray);
        labels->Release();
        return node;
    }
    if (sym == saved_sort_index) return SavedSortIndex();
    if (sym == save_sort) {
        SaveSort();
        return 0;
    }
    if (sym == save_focus) {
        SaveFocus(msg->Int(2));
        return 0;
    }
    if (sym == restore_focus) {
        RestoreFocus(msg->Obj<Hmx::Object>(2));
        return 0;
    }
    if (sym == visible_text) return msg->Obj<HamNavList>(2)->NativeVisibleText();
    return DATA_UNHANDLED;
}

// Where Left/Right (a section) or LB/RB (a screenful) take the highlight from
// row `selected`; -1 to stay.  A section is the rows from one header to the
// next, and the function rows above the first header are one too -- so in
// header mode, where the songs are folded away, every row is a section.
int SongSortMgr::NavJumpRow(int action, int selected, int pageRows) {
    int numRows = GetCurrentSort()->GetDataCount();
    if (selected < 0 || selected >= numRows) return -1;

    if (action == kAction_PageUp || action == kAction_PageDown) {
        int direction = action == kAction_PageDown ? 1 : -1;
        int target = Clamp(0, numRows - 1, selected + direction * pageRows);
        // A row that cannot be picked: the next one that can, still going the
        // same way; else the nearest one on the way back.
        int row = target;
        while (row >= 0 && row < numRows && !IsActive(row)) {
            row += direction;
        }
        if (row < 0 || row >= numRows) {
            row = target;
            while (row != selected && !IsActive(row)) {
                row -= direction;
            }
        }
        return row == selected ? -1 : row;
    }

    if (action != kAction_Left && action != kAction_Right) return -1;

    std::vector<int> sectionStarts;
    if (mHeadersB.empty() || mHeadersB.front() > 0) sectionStarts.push_back(0);
    FOREACH (it, mHeadersB) {
        sectionStarts.push_back(*it);
    }
    int numSections = sectionStarts.size();
    int current = 0;
    for (int i = 0; i < numSections; i++) {
        if (sectionStarts[i] <= selected) current = i;
    }
    int wanted = action == kAction_Right ? current + 1 : current - 1;
    // Past the last section: stay.  Before the first: the top of the list.
    if (wanted >= numSections) return -1;
    if (wanted < 0) wanted = 0;
    int end = wanted + 1 < numSections ? sectionStarts[wanted + 1] : numRows;
    for (int row = sectionStarts[wanted]; row < end; row++) {
        if (IsActive(row)) return row == selected ? -1 : row;
    }
    return -1;
}

// The sort names from the current one on, for the help bar's carousel (the
// image's script hard-codes the three rotations of its three sorts).  The bar
// has room for three names -- a fourth is cut off by the edge of the screen --
// so with four sorts it shows the current one and the next two.
DataArray *SongSortMgr::SortLabels() {
    static const int kHelpBarSortNames = 3;
    int numSorts = mSorts.size();
    int numLabels = Min(numSorts, kHelpBarSortNames);
    DataArray *labels = new DataArray(numLabels);
    for (int i = 0; i < numLabels; i++) {
        labels->Node(i) = mSorts[(mCurrentSortIdx + i) % numSorts]->GetSortName();
    }
    return labels;
}

int SongSortMgr::SavedSortIndex() {
    if (Dc3ClassicSongSelect()) return 0;
    const std::string &saved = TheNativeSongSelectState().Sort();
    for (int i = 0; i < (int)mSorts.size(); i++) {
        if (saved == mSorts[i]->GetSortName().Str()) return i;
    }
    return 0;
}

void SongSortMgr::SaveSort() {
    if (Dc3ClassicSongSelect()) return;
    TheNativeSongSelectState().SetSort(GetCurrentSortName().Str());
}

// `row` is the list's selected row.  Not GetHighlightItem(): after a collapsed
// list is opened with Back the provider's highlight is the first row again
// while the list still shows the row it was on.
void SongSortMgr::SaveFocus(int row) {
    if (Dc3ClassicSongSelect()) return;
    if (!sFocusID) sFocusID = new DataArray(0);
    NavListSort *sort = GetCurrentSort();
    if (row < 0 || row >= sort->GetDataCount()) {
        sFocusID->Resize(0);
        return;
    }
    sort->GetListFromIdx(row)->GetID(sFocusID);
}

// Called by song select's enter, after the sort is set: moves the panel's
// focus_song_index/top_index to the row SaveFocus saw, wherever it is now,
// keeping it at the same height on screen.  A Recently Played row is found
// again as a Recently Played row; if the song has since dropped out of the
// section, its own row is used.  When neither is there the panel's index
// stands, as in the image.
void SongSortMgr::RestoreFocus(Hmx::Object *panel) {
    if (Dc3ClassicSongSelect() || !panel || !sFocusID || sFocusID->Size() == 0) return;
    NavListSort *sort = GetCurrentSort();
    NavListSortNode *highlight = sort->GetHighlightNode();
    NavListSortNode *prevHighlight = sort->GetPrevHighlightNode();
    NavListSortNode *row = nullptr;
    if (sort->SetHighlightID(sFocusID)) row = sort->GetHighlightNode();
    // SetHighlightID is a setter; this is only a lookup.
    sort->SetHighlightNode(highlight);
    sort->SetPrevHighlightNode(prevHighlight);
    if (!row) row = sort->GetNode(sFocusID->Sym(sFocusID->Size() - 1));
    if (!row) return;
    int index = row->StartIndex();
    if (index < 0 || index >= sort->GetDataCount() || sort->GetListFromIdx(index) != row) {
        return;
    }
    static Symbol focus_song_index("focus_song_index");
    static Symbol top_index("top_index");
    int heightOnScreen = Clamp(
        0,
        HamListRibbon::sNumListSelectable - 1,
        panel->Property(focus_song_index, true)->Int() - panel->Property(top_index, true)->Int()
    );
    panel->SetProperty(focus_song_index, index);
    panel->SetProperty(top_index, Max(0, index - heightOnScreen));
}

// ---------------------------------------------------------------------------
// HamNavList: Left/Right and LB/RB
// ---------------------------------------------------------------------------

// Asks the list's provider where the button goes (nav_jump_row ACTION ROW
// PAGE_ROWS -> row, or -1 to stay).  Only song select's provider answers, so
// every other list -- and song select under the classic switch -- leaves the
// button unhandled, as in the image.  True when the button was taken.
bool HamNavList::NativeNavJump(JoypadAction action) {
    if (action != kAction_Left && action != kAction_Right && action != kAction_PageUp
        && action != kAction_PageDown) {
        return false;
    }
    Hmx::Object *provider = dynamic_cast<Hmx::Object *>(mListState.Provider());
    if (!provider) return false;
    static Message nav_jump_row("nav_jump_row", 0, 0, 0);
    int selected = mListState.Selected();
    nav_jump_row[0] = (int)action;
    nav_jump_row[1] = selected;
    nav_jump_row[2] = HamListRibbon::sNumListSelectable;
    DataNode answer = provider->Handle(nav_jump_row, false);
    if (answer.Type() != kDataInt) return false;
    int row = answer.Int();
    if (row < 0 || row >= mListState.NumShowing() || row == selected) return true;
    // A section starts at the top of the window; a page keeps the highlight
    // at the height it was.
    bool paging = action == kAction_PageUp || action == kAction_PageDown;
    int heightOnScreen = paging ? selected - mListState.FirstShowing() : 0;
    ScrollToIndex(row, Max(0, row - Max(0, heightOnScreen)));
    return true;
}

// The walk the image's LocalePanel does over a UIList, over this list.
String HamNavList::NativeVisibleText() const {
    String text;
    for (int display = 0; display < mListState.NumDisplay(); display++) {
        int row = mListState.Display2Data(display);
        if (row < 0) continue;
        FOREACH (it, mListWidgets) {
            UIListLabel *slot = dynamic_cast<UIListLabel *>(*it);
            UILabel *label = slot ? slot->ElementLabel(display) : nullptr;
            if (!label || label->RawText().empty()) continue;
            if (!text.empty()) text += ";;";
            text += MakeString("%d|%s|%s", row, slot->MatchName(), label->RawText().c_str());
        }
    }
    return text;
}

// ---------------------------------------------------------------------------
// The natively drawn song browser's seat in song select
// (SongBrowserHooks_Native.h).  The browser itself is
// SongBrowserPanel_Native.cpp; a binary that does not list it has no hooks and
// keeps the classic list.
// ---------------------------------------------------------------------------

namespace {
const NativeSongBrowser::Hooks *sBrowserHooks = nullptr;
}

void NativeSongBrowser::Install(const Hooks *hooks) { sBrowserHooks = hooks; }

void NativeSongBrowser::OnPanelEnter(UIPanel *panel) {
    if (sBrowserHooks) sBrowserHooks->enter(panel);
}

void NativeSongBrowser::OnPanelExit(UIPanel *panel) {
    if (sBrowserHooks) sBrowserHooks->exit(panel);
}

void NativeSongBrowser::OnPanelPoll(UIPanel *panel) {
    if (sBrowserHooks) sBrowserHooks->poll(panel);
}

bool NativeSongBrowser::OnPanelDraw(UIPanel *panel) {
    return sBrowserHooks && sBrowserHooks->draw(panel);
}

DataNode NativeSongBrowser::OnPanelHandle(UIPanel *panel, DataArray *msg) {
    if (sBrowserHooks) return sBrowserHooks->handle(panel, msg);
    return DATA_UNHANDLED;
}

bool NativeSongBrowser::HidesGameChrome() {
    return sBrowserHooks && sBrowserHooks->hidesChrome();
}

std::string NativeSongBrowser::StateJson() {
    if (sBrowserHooks) return sBrowserHooks->stateJson();
    return "{\"available\":false,\"shown\":false,\"why\":\"this binary has no song browser\"}";
}

void NativeSongBrowser::OnAnyButtonDown(int padNum) {
    if (sBrowserHooks) sBrowserHooks->anyButtonDown(padNum);
}

void NativeSongBrowser::NoteInjectedPadPress() {
    if (sBrowserHooks) sBrowserHooks->injectedPadPress();
}

int NativeSongBrowser::PointerTap(float x, float y, bool firstHighlights) {
    return sBrowserHooks && sBrowserHooks->pointerTap
        ? sBrowserHooks->pointerTap(x, y, firstHighlights)
        : 0;
}

bool NativeSongBrowser::PointerHover(float x, float y) {
    return sBrowserHooks && sBrowserHooks->pointerHover && sBrowserHooks->pointerHover(x, y);
}

bool NativeSongBrowser::RunCommand(const std::string &name, std::string &error) {
    if (sBrowserHooks) return sBrowserHooks->command(name, error);
    error = "this binary has no song browser";
    return false;
}

// The list's own menu sounds, for the browser: the sound moving onto row
// `index` plays, or the one selecting it plays.
void HamNavList::NativePlayRibbonSound(int index, bool select) {
    if (!mListRibbonResource) return;
    if (select) {
        mListRibbonResource->PlaySelectSound(index);
    } else {
        mListRibbonResource->PlayHighlightSound(index);
    }
}
