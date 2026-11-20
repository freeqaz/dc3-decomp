// DC3 Native Port - the natively drawn song browser, in song select.
// docs/native/SONG_SELECT.md ("The song browser") has the behaviour;
// SongBrowserHooks_Native.h has the five places the image calls in here.
//
// The classic song_select_panel stays loaded, entered and polled exactly as
// without the browser.  While the browser is shown the panel does not draw
// (SongSelectPanel::Draw), song_info_panel is not shown, pad presses stop here
// (SongSelectPanel::Handle), and the browser is drawn in the panel's place,
// over background_panel.  Everything the browser does to the game it does
// through the classic panel's own handlers, so a pick, a back and the song the
// preview plays are the classic list's:
//
//   selection   {song_select_panel highlight_right_list_item ROW}: the classic
//               list's highlight is kept on the browser's song, so its
//               handle_highlight / handle_highlight_settled run (song info,
//               the preview) and the list's highlight sound plays
//   pick        {song_select_panel special_select_song SONG}: the game's own
//               select-by-name (the voice command's path), which runs the
//               list's select animation and sound and then on_select
//   back        {song_select_panel on_back}
//
// Main thread only.  One browser for the process, made on the first visit that
// may show it and never destroyed (it owns GPU textures).

#include "SongBrowserHooks_Native.h"

#include "NativeSettings.h" // Dc3EnvFlag
#include "SongSelectState_Native.h"
#include "SongSelect_Native.h"
#include "gesture/GestureMgr.h"
#include "hamobj/HamNavList.h"
#include "meta_ham/HamUI.h"
#include "meta_ham/ShellInput.h"
#include "meta_ham/SongSortMgr.h"
#include "meta_ham/UIEventMgr.h"
#include "nativeui_host/GameBindings.h"
#include "nativeui_host/GameFonts.h"
#include "nativeui_host/GameImages.h"
#include "nativeui_host/HostedScreen.h"
#include "nativeui_host/NativeUiHost.h"
#include "nativeui/ListView.h"
#include "obj/Data.h"
#include "obj/Dir.h"
#include "obj/Msg.h"
#include "os/JoypadMsgs.h"
#include "songbrowser/SongCatalog.h"
#include "songbrowser/SongCatalogJson.h"
#include "songbrowser/SongCatalogSource_Native.h"
#include "songbrowser/SongSections.h"
#include "songbrowser_ui/SongBrowserScreen.h"
#include "synth/Synth.h"
#include "ui/PanelDir.h"
#include "ui/UI.h"
#include "ui/UIPanel.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <strings.h>
#include <variant>

namespace {

using songbrowser_ui::Cue;
using songbrowser_ui::Region;
using songbrowser_ui::SongBrowserScreen;

// DC3_SONG_BROWSER.
enum Mode {
    kNever,
    kAlways,
    kAuto
};

// Seconds (the UI clock's) a body must stay tracked with controller mode off
// before the browser gives way to the classic list.  The game's own
// controller-mode timeout (5 s without a press) comes first.
const double kGestureTakeoverSeconds = 2.0;
// A pick or a back that started no screen change within this long is given
// up: the browser takes input again.
const double kLeaveGiveUpSeconds = 4.0;

// The sort as song select's state file names it.  The four the classic list
// has use its names, so both lists come back on the same sort; the other two
// are names the classic list does not know and reads as its first sort.
const char *SortFileName(songbrowser::Arrangement arrangement) {
    switch (arrangement) {
    case songbrowser::kByTitle:
        return "by_song";
    case songbrowser::kByArtist:
        return "by_artist";
    case songbrowser::kByDifficulty:
        return "by_difficulty";
    case songbrowser::kBySource:
        return "by_location";
    case songbrowser::kByDecade:
        return "by_decade";
    case songbrowser::kByBpm:
        return "by_bpm";
    }
    return "by_song";
}

bool ArrangementFromSortFile(const std::string &name, songbrowser::Arrangement &out) {
    static const songbrowser::Arrangement kAll[] = {
        songbrowser::kByTitle,  songbrowser::kByArtist, songbrowser::kByDifficulty,
        songbrowser::kBySource, songbrowser::kByDecade, songbrowser::kByBpm,
    };
    for (songbrowser::Arrangement arrangement : kAll) {
        if (name == SortFileName(arrangement)) {
            out = arrangement;
            return true;
        }
    }
    return false;
}

const char *RegionName(Region region) {
    switch (region) {
    case Region::Search:
        return "search";
    case Region::Tabs:
        return "tabs";
    case Region::Recents:
        return "recents";
    case Region::List:
        return "list";
    }
    return "list";
}

// What the host runs: the screen, behind a gate that drops input while a pick
// or a back is on its way, and that notices typed text.
class GatedScreen final : public nativeui_host::HostedScreen {
public:
    bool HandleEvent(const nativeui::Event &event) override {
        if (!screen || frozen) {
            return false;
        }
        if (!std::holds_alternative<nativeui::ActionEvent>(event)) {
            typed = true;
        }
        return screen->HandleEvent(event);
    }
    const nativeui::DisplayList &BuildFrame() override { return screen->BuildFrame(); }
    bool WantsTextInput() const override { return screen != nullptr; }

    SongBrowserScreen *screen = nullptr;
    bool frozen = false;
    bool typed = false; // text or an edit key arrived since this was cleared
};

class Browser {
public:
    Browser() : images_(loader_), host_(fonts_.Fonts(), &images_) {
        mode_ = ResolveMode(modeWhy_);
        host_.SetTextCapture(nativeui_host::TextCapture::Always);
        std::fprintf(
            stderr,
            "DC3 SongBrowser: mode=%s%s%s\n",
            ModeName(),
            modeWhy_.empty() ? "" : " - ",
            modeWhy_.c_str()
        );
    }

    void Enter(UIPanel *panel) {
        panel_ = panel;
        list_ = panel->DataDir() ? panel->DataDir()->Find<HamNavList>("right_hand.hnl", false)
                                 : nullptr;
        shown_ = false;
        gate_.frozen = false;
        leavingSince_ = -1.0;
        pendingPick_.clear();
        pendingBack_ = false;
        ClearCues();
        trackedSince_ = -1.0;
        padInUse_ = TheGestureMgr && TheGestureMgr->InControllerMode();
        eligible_ = DecideEligible(visitWhy_);
        if (!eligible_) {
            return;
        }
        FillCatalog();
        BuildScreen(hasSaved_ ? &saved_ : nullptr);
        // The sort both lists remember, when it is one the browser has.
        songbrowser::Arrangement arrangement;
        if (ArrangementFromSortFile(TheNativeSongSelectState().Sort(), arrangement)) {
            songbrowser_ui::State state = screen_->SaveState();
            if (state.arrangement != arrangement) {
                state.arrangement = arrangement;
                screen_->RestoreState(state);
            }
        }
        Update(); // the first frame drawn is already the right list
    }

    void Exit(UIPanel *panel) {
        if (panel != panel_) {
            return;
        }
        if (screen_) {
            if (shown_) {
                SyncClassicList(); // the classic list saves its focus next
            }
            saved_ = screen_->SaveState();
            hasSaved_ = true;
        }
        Hide();
        host_.SetScreen(nullptr);
        gate_.screen = nullptr;
        gate_.frozen = false;
        leavingSince_ = -1.0;
        screen_.reset();
        if (eligible_) {
            if (UIPanel *info = SongInfoPanel()) {
                info->SetShowing(true); // other screens use it too
            }
        }
        panel_ = nullptr;
        list_ = nullptr;
        eligible_ = false;
        visitWhy_ = "not on song select";
    }

    void Poll(UIPanel *panel) {
        if (panel != panel_ || !eligible_) {
            return;
        }
        Update();
    }

    bool Draw(UIPanel *panel) {
        if (panel != panel_ || !shown_) {
            return false;
        }
        if (!UIPanel::GetFinalDrawPass()) {
            nativeui_host::SetOutputSize(host_);
            host_.Draw();
            frames_++;
        }
        return true;
    }

    DataNode Handle(UIPanel *panel, DataArray *msg) {
        if (panel != panel_ || !shown_ || !msg || msg->Size() < 2) {
            return DATA_UNHANDLED;
        }
        Symbol type = msg->Sym(1);
        if (type == ButtonUpMsg::Type()) {
            nativeui_host::OnButtonUp(host_, ButtonUpMsg(msg));
            return DATA_UNHANDLED;
        }
        if (type != ButtonDownMsg::Type()) {
            return DATA_UNHANDLED;
        }
        ButtonDownMsg press(msg);
        if (press.GetButton() == kPad_Start) {
            return DATA_UNHANDLED; // the game's: Start hands the menu back to gestures
        }
        if (!gate_.frozen) {
            NotePadActivity();
            nativeui_host::OnButtonDown(host_, press);
            RunPending();
        }
        // Every other press is the browser's while it is shown, used or not:
        // the hidden list and the help bar must not act on it.
        return DataNode(1);
    }

    bool HidesChrome() const { return shown_; }

    std::string StateJson() const {
        using songbrowser::JsonEscape;
        std::string json = "{\"available\":true";
        json += ",\"mode\":\"" + std::string(ModeName()) + "\"";
        json += std::string(",\"on_song_select\":") + (panel_ ? "true" : "false");
        json += std::string(",\"eligible\":") + (eligible_ ? "true" : "false");
        json += std::string(",\"shown\":") + (shown_ ? "true" : "false");
        json += ",\"why\":\"" + JsonEscape(Why()) + "\"";
        json += std::string(",\"pad_in_use\":") + (padInUse_ ? "true" : "false");
        json += std::string(",\"controller_mode\":")
            + (TheGestureMgr && TheGestureMgr->InControllerMode() ? "true" : "false");
        json += std::string(",\"text_capture\":") + (host_.CapturingText() ? "true" : "false");
        json += std::string(",\"leaving\":") + (gate_.frozen ? "true" : "false");
        json += std::string(",\"hints\":\"") + (keyboardHints_ ? "keyboard" : "pad") + "\"";
        json += ",\"songs\":" + std::to_string(catalog_.Size());
        json += ",\"classic_song\":\"" + JsonEscape(syncedSong_) + "\"";
        if (screen_) {
            const songbrowser_ui::State state = screen_->SaveState();
            const nativeui::Rect bar = screen_->List().HighlightRect();
            char numbers[256];
            json += std::string(",\"focus\":\"") + RegionName(state.focus) + "\"";
            json += ",\"selected_song\":\"" + JsonEscape(screen_->DetailSong()) + "\"";
            json += ",\"list_song\":\"" + JsonEscape(state.selectedSong) + "\"";
            json += ",\"recent_song\":\"" + JsonEscape(state.recentSong) + "\"";
            json += ",\"query\":\"" + JsonEscape(state.query) + "\"";
            json += std::string(",\"sort\":\"") + songbrowser::ArrangementName(state.arrangement)
                + "\"";
            json += ",\"rows\":" + std::to_string(screen_->List().RowCount());
            json += std::string(",\"recents_shown\":")
                + (screen_->RecentsShown() ? "true" : "false");
            std::snprintf(
                numbers,
                sizeof(numbers),
                ",\"highlight_rect\":[%.1f,%.1f,%.1f,%.1f]",
                bar.x,
                bar.y,
                bar.width,
                bar.height
            );
            json += numbers;
        }
        const nativeui_host::NativeUiHost::FrameStats &stats = host_.Stats();
        json += ",\"frames\":" + std::to_string(frames_);
        json += std::string(",\"drew\":") + (stats.drew ? "true" : "false");
        json += ",\"glyphs\":" + std::to_string(stats.glyphs);
        json += ",\"placeholder_glyphs\":" + std::to_string(stats.placeholderGlyphs);
        json += ",\"quads\":" + std::to_string(stats.quads);
        json += ",\"draw_calls\":" + std::to_string(stats.drawCalls);
        json += ",\"images_missing\":" + std::to_string(stats.imagesMissing);
        json += "}";
        return json;
    }

private:
    static Mode ResolveMode(std::string &why) {
        if (Dc3ClassicSongSelect()) {
            why = "DC3_CLASSIC_SONG_SELECT=1";
            return kNever;
        }
        const char *value = std::getenv("DC3_SONG_BROWSER");
        if (value && *value) {
            if (strcasecmp(value, "auto") == 0) {
                return kAuto;
            }
            if (!Dc3EnvFlag("DC3_SONG_BROWSER", true)) {
                why = "DC3_SONG_BROWSER=0";
                return kNever;
            }
            why = "DC3_SONG_BROWSER=1";
            return kAlways;
        }
#ifdef __EMSCRIPTEN__
        why = "off by default on the web (DC3_SONG_BROWSER is not set)";
        return kNever;
#else
        // THE pin for everything written against the classic list: a run
        // driven by an input script gets the classic list unless it asks for
        // the browser by name.
        const char *script = std::getenv("MILO_INPUT_SCRIPT");
        if (script && *script) {
            why = "MILO_INPUT_SCRIPT is set and DC3_SONG_BROWSER is not: scripted input "
                  "drives the classic list";
            return kNever;
        }
        return kAuto;
#endif
    }

    const char *ModeName() const {
        return mode_ == kNever ? "off" : mode_ == kAlways ? "on" : "auto";
    }

    // Why the browser is or is not on screen right now.
    std::string Why() const {
        if (mode_ == kNever) {
            return modeWhy_;
        }
        if (!panel_) {
            return "not on song select";
        }
        if (!eligible_) {
            return visitWhy_;
        }
        if (shown_) {
            return mode_ == kAlways ? "DC3_SONG_BROWSER=1" : "pad or keyboard in use";
        }
        return hiddenWhy_;
    }

    bool DecideEligible(std::string &why) {
        if (mode_ == kNever) {
            why = modeWhy_;
            return false;
        }
        if (!NativeSongSelect::ShowsRecentlyPlayed()) {
            why = "this visit is not a pick-one-song list (playlist building, campaign)";
            return false;
        }
        if (!songbrowser::GameCatalogReady()) {
            why = "the song catalog is not ready";
            return false;
        }
        if (!list_) {
            why = "the panel has no right_hand.hnl";
            return false;
        }
        if (!fontsTried_) {
            fontsTried_ = true;
            fontsOk_ = fonts_.Load();
            images_.SetFallback(nativeui_host::kFallbackArtPath);
        }
        if (!fontsOk_) {
            why = "the menu font did not load";
            return false;
        }
        why.clear();
        return true;
    }

    void FillCatalog() {
        songbrowser::FillCatalogFromGame(catalog_);
        catalogStamp_ = TheSongSortMgr ? (int)TheSongSortMgr->MapSize() : 0;
    }

    void BuildScreen(const songbrowser_ui::State *restore) {
        songbrowser_ui::Delegate delegate;
        delegate.onPick = [this](const std::string &song) { pendingPick_ = song; };
        delegate.onBack = [this] { pendingBack_ = true; };
        delegate.onCue = [this](Cue cue) {
            switch (cue) {
            case Cue::Move:
                cueMove_ = true;
                break;
            case Cue::Tab:
                cueTab_ = true;
                break;
            case Cue::Back:
                cueBack_ = true;
                break;
            case Cue::Error:
                cueError_ = true;
                break;
            case Cue::Select:
                break; // the classic list plays its own select sound for the pick
            }
        };
        delegate.artFor = [this](const songbrowser::SongEntry &song) {
            return images_.Request(
                song.albumArtPath.empty() ? nativeui_host::kFallbackArtPath : song.albumArtPath
            );
        };
        songbrowser_ui::Environment environment { fonts_.Fonts(), host_.Clock() };
        if (keyboardHints_) {
            environment.glyphs.accept = "ENTER";
            environment.glyphs.back = "ESC";
            environment.glyphs.sort = "TAB";
            environment.glyphs.section = "< >";
            environment.glyphs.type = "ABC";
        }
        host_.SetScreen(nullptr);
        gate_.screen = nullptr;
        screen_ = std::make_unique<SongBrowserScreen>(catalog_, delegate, environment, restore);
        gate_.screen = screen_.get();
        host_.SetScreen(&gate_);
    }

    // The per-frame step: decides shown or hidden, then runs the shown browser.
    void Update() {
        const double now = nativeui_host::GameUiSeconds();
        const bool controllerMode = TheGestureMgr && TheGestureMgr->InControllerMode();

        // The auto rule's latch.
        if (controllerMode) {
            padInUse_ = true;
            trackedSince_ = -1.0;
        } else if (TrackedBodies() > 0) {
            if (trackedSince_ < 0.0) {
                trackedSince_ = now;
            }
            if (now - trackedSince_ >= kGestureTakeoverSeconds) {
                padInUse_ = false;
            }
        } else {
            trackedSince_ = -1.0; // nobody in view: nothing has taken over
        }

        bool want = mode_ == kAlways || padInUse_;
        if (!want) {
            hiddenWhy_ = "a tracked player is using gestures (controller mode is off)";
        }
        if (want && (!panel_->Showing() || TheHamUI.GetOverlayPanel() != nullptr
                || (TheUIEventMgr && TheUIEventMgr->HasActiveDialogEvent()))) {
            want = false;
            hiddenWhy_ = "an overlay or a dialog is up";
        }
        if (want != shown_) {
            if (want) {
                Show(now);
            } else {
                Hide();
            }
        }
        if (!shown_) {
            return;
        }

        if (TheSongSortMgr && (int)TheSongSortMgr->MapSize() != catalogStamp_) {
            FillCatalog(); // content came or went
            screen_->SetCatalog(catalog_);
        }

        gate_.typed = false;
        host_.Tick(now); // typed text, held directions, art
        if (gate_.typed) {
            NotePadActivity();
            if (!keyboardHints_) {
                keyboardHints_ = true;
                const songbrowser_ui::State state = screen_->SaveState();
                BuildScreen(&state); // the hint bar now names keys
            }
        }
        RunPending();

        if (UIPanel *info = SongInfoPanel()) {
            if (info->Showing()) {
                info->SetShowing(false);
            }
        }
        if (gate_.frozen && leavingSince_ >= 0.0 && !TheUI->InTransition()
            && now - leavingSince_ >= kLeaveGiveUpSeconds) {
            gate_.frozen = false; // the pick went nowhere (a locked or bad song)
            leavingSince_ = -1.0;
        }
    }

    void Show(double now) {
        shown_ = true;
        // Coming back from the classic list: stand on the song it was left on.
        const std::string classic = ClassicSong();
        if (!syncedSong_.empty() && !classic.empty() && classic != syncedSong_
            && classic != screen_->DetailSong() && catalog_.Find(classic)) {
            songbrowser_ui::State state = screen_->SaveState();
            state.focus = Region::List;
            state.selectedSong = classic;
            state.query.clear();
            screen_->RestoreState(state);
        }
        host_.Enter(now);
        ClearCues();
    }

    void Hide() {
        if (!shown_) {
            return;
        }
        if (screen_ && !gate_.frozen) {
            SyncClassicList(); // the classic list appears on the same song
        }
        shown_ = false;
        host_.Exit(); // text capture off, held directions released
        if (UIPanel *info = SongInfoPanel()) {
            info->SetShowing(panel_ && panel_->Showing());
        }
    }

    // What the screen's callbacks asked for, done once the screen has returned.
    void RunPending() {
        if (!screen_) {
            return;
        }
        if (!pendingPick_.empty()) {
            const std::string song = pendingPick_;
            pendingPick_.clear();
            ClearCues();
            Pick(song);
            return;
        }
        if (pendingBack_) {
            pendingBack_ = false;
            ClearCues();
            Back();
            return;
        }
        if (cueTab_) {
            const songbrowser_ui::State state = screen_->SaveState();
            TheNativeSongSelectState().SetSort(SortFileName(state.arrangement));
        }
        // The selected song moved: the classic list follows, and plays the
        // list's highlight sound itself.  Any other cue plays here.
        const bool moved = SyncClassicList();
        if (cueError_) {
            TheSynth->RunFlow("invalid_select.flow");
        } else if (cueTab_) {
            list_->NativePlayRibbonSound(0, true);
        } else if ((cueMove_ || cueBack_) && !moved) {
            list_->NativePlayRibbonSound(0, false);
        }
        ClearCues();
    }

    void Pick(const std::string &song) {
        Uncollapse();
        syncedSong_ = song;
        gate_.frozen = true;
        leavingSince_ = nativeui_host::GameUiSeconds();
        static Symbol special_select_song("special_select_song");
        panel_->Handle(Message(special_select_song, Symbol(song.c_str())), false);
    }

    void Back() {
        Uncollapse(); // on_back opens a collapsed list instead of leaving
        static Symbol on_back("on_back");
        panel_->Handle(Message(on_back), false);
        if (TheUI->InTransition()) {
            gate_.frozen = true;
            leavingSince_ = nativeui_host::GameUiSeconds();
        }
    }

    // Puts the classic list's highlight on the browser's song.  True when it
    // moved (the list then played its highlight sound).
    bool SyncClassicList() {
        if (!screen_ || !panel_ || !list_) {
            return false;
        }
        const std::string &song = screen_->DetailSong();
        if (song.empty() || song == syncedSong_) {
            return false;
        }
        Uncollapse();
        static Symbol first_data_index("first_data_index");
        static Symbol highlight_right_list_item("highlight_right_list_item");
        const DataNode row =
            TheSongSortMgr->Handle(Message(first_data_index, Symbol(song.c_str())), false);
        if (row.Type() != kDataInt || row.Int() < 0) {
            return false; // not in the classic list (it should always be)
        }
        syncedSong_ = song;
        panel_->Handle(Message(highlight_right_list_item, row.Int()), false);
        return true;
    }

    // The classic list shows sections only after its sort changes; the songs
    // have to be rows for a highlight or a select by name.
    void Uncollapse() {
        static Symbol get_header_mode("get_header_mode");
        static Symbol do_uncollapse("do_uncollapse");
        static Symbol refresh("refresh");
        const DataNode collapsed = TheSongSortMgr->Handle(Message(get_header_mode), false);
        if (collapsed.Type() == kDataInt && collapsed.Int() != 0) {
            TheSongSortMgr->Handle(Message(do_uncollapse), false);
            list_->Handle(Message(refresh), false);
        }
    }

    // The song the classic list's highlight is on; empty for a section row.
    std::string ClassicSong() {
        static Symbol get_selected_song_token("get_selected_song_token");
        const DataNode token = panel_->Handle(Message(get_selected_song_token), false);
        if (token.Type() != kDataSymbol) {
            return std::string();
        }
        return token.Sym().Str();
    }

    // A press or typed text reached the browser: the pad or the keyboard is
    // in use, whatever the game thought a moment ago.
    void NotePadActivity() {
        padInUse_ = true;
        trackedSince_ = -1.0;
        ShellInput *shell = TheHamUI.GetShellInput();
        if (!shell || !TheGestureMgr) {
            return;
        }
        if (!TheGestureMgr->InControllerMode()) {
            shell->EnterControllerMode(false);
        }
        shell->unk_0x68.Restart(); // the controller-mode timeout starts over
    }

    static int TrackedBodies() {
        ShellInput *shell = TheHamUI.GetShellInput();
        return shell ? shell->NumTrackedSkeletons() : 0;
    }

    static UIPanel *SongInfoPanel() {
        return ObjectDir::Main()->Find<UIPanel>("song_info_panel", false);
    }

    void ClearCues() { cueMove_ = cueTab_ = cueBack_ = cueError_ = false; }

    nativeui_host::GameFonts fonts_;
    nativeui_host::GameImageLoader loader_;
    nativeui_host::ImageCache images_;
    nativeui_host::NativeUiHost host_;
    songbrowser::SongCatalog catalog_;
    std::unique_ptr<SongBrowserScreen> screen_;
    GatedScreen gate_;

    Mode mode_ = kNever;
    std::string modeWhy_;
    std::string visitWhy_ = "not on song select";
    std::string hiddenWhy_;
    bool fontsTried_ = false;
    bool fontsOk_ = false;

    UIPanel *panel_ = nullptr; // song_select_panel, while it is entered
    HamNavList *list_ = nullptr; // its right_hand.hnl
    bool eligible_ = false; // this visit may show the browser
    bool shown_ = false;
    bool padInUse_ = false; // the auto rule's latch
    double trackedSince_ = -1.0;
    int catalogStamp_ = 0;

    songbrowser_ui::State saved_;
    bool hasSaved_ = false;
    std::string syncedSong_; // the song the classic list was last put on
    bool keyboardHints_ = false;

    std::string pendingPick_;
    bool pendingBack_ = false;
    bool cueMove_ = false;
    bool cueTab_ = false;
    bool cueBack_ = false;
    bool cueError_ = false;
    double leavingSince_ = -1.0;
    unsigned frames_ = 0;
};

Browser &TheBrowser() {
    static Browser *browser = new Browser; // never destroyed: see the top
    return *browser;
}

// The mode alone, for the state endpoint before any visit made the browser.
bool sBrowserMade = false;

void HookEnter(UIPanel *panel) {
    sBrowserMade = true;
    TheBrowser().Enter(panel);
}
void HookExit(UIPanel *panel) {
    if (sBrowserMade) TheBrowser().Exit(panel);
}
void HookPoll(UIPanel *panel) {
    if (sBrowserMade) TheBrowser().Poll(panel);
}
bool HookDraw(UIPanel *panel) { return sBrowserMade && TheBrowser().Draw(panel); }
DataNode HookHandle(UIPanel *panel, DataArray *msg) {
    if (sBrowserMade) return TheBrowser().Handle(panel, msg);
    return DATA_UNHANDLED;
}
bool HookHidesChrome() { return sBrowserMade && TheBrowser().HidesChrome(); }
std::string HookStateJson() {
    if (sBrowserMade) return TheBrowser().StateJson();
    return "{\"available\":true,\"on_song_select\":false,\"eligible\":false,\"shown\":false,"
           "\"why\":\"not on song select\"}";
}

const NativeSongBrowser::Hooks kHooks = {
    HookEnter, HookExit, HookPoll, HookDraw, HookHandle, HookHidesChrome, HookStateJson,
};

struct Installer {
    Installer() { NativeSongBrowser::Install(&kHooks); }
} sInstaller;

} // namespace
