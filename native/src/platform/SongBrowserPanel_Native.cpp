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
//   command     {song_select_panel select_right_list_item ROW} on the classic
//               list's own Random Song or Playlists row: the game's select of
//               that row (the voice command's path), so what follows is
//               SongFunctionNode::OnSelect
//
// While the browser is shown the game is kept in controller mode: the hands
// do not drive the list underneath, and no press is spent on waking the pad.
//
// Main thread only.  One browser for the process, made on the first visit that
// may show it and never destroyed (it owns GPU textures).

#include "SongBrowserHooks_Native.h"

#include "MenuGesture_Native.h" // Dc3MenuGesture::HandInListPose
#include "NativeSettings.h" // Dc3EnvFlag
#include "SongSelectState_Native.h"
#include "SongSelect_Native.h"
#include "gesture/GestureMgr.h"
#include "hamobj/HamNavList.h"
#include "meta_ham/HamUI.h"
#include "meta/SongPreview.h"
#include "meta_ham/HelpBarPanel.h"
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
#include "platform/TextInput_Native.h"
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

#if !defined(__EMSCRIPTEN__)
#include <GLFW/glfw3.h>
extern GLFWwindow *gNativeWindow;
#endif

extern bool Dc3ControllerModeForced();
extern int gNativeSongPreviewStarts; // src/system/meta/SongPreview.cpp
extern bool gNativeSongPreviewHold; // src/system/meta/SongPreview.cpp

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

// Seconds (the UI clock's) a hand must stay where the classic list needs it,
// without a break and without a press, before the browser gives way to the
// classic list (the auto rule).
const double kHandTakeoverSeconds = 0.75;

// The preview of the selected song starts once the selection has rested this
// long (UI seconds).  The classic list asks for a preview on every highlight
// that does not scroll it, so five quick presses prepared five streams.
const double kPreviewRestSeconds = 0.3;
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

// The device last used, for the names in the hint bar.  Process-wide: presses
// on every screen count (HamUI::OnMsg(ButtonDownMsg) reports each one), so
// song select opens naming the device the player came with.  A run starts on
// the keyboard: until a real pad's button arrives there is no pad to name.
enum Device {
    kDeviceKeyboard,
    kDevicePad
};
Device sDevice = kDeviceKeyboard;
bool sNextPressIsInjectedPad = false; // POST /api/input/press "device":"pad"
unsigned sPadPresses = 0; // presses counted as a real pad's, this run

// Whether a real pad holds a button down right now.  A press made by a key
// reaches the game as the same ButtonDownMsg (the engine ORs the keyboard's
// buttons into pad 0's), so the only way to tell them apart from here is to
// ask the pad itself: a ButtonDownMsg that arrives while the real pad with
// that number has nothing down was a key's (or a script's, or an injected
// one).  Headless and on the web there is no pad to ask.
bool RealPadHasAButtonDown(int padNum) {
#if !defined(__EMSCRIPTEN__)
    if (!gNativeWindow || padNum < 0 || padNum > GLFW_JOYSTICK_LAST) {
        return false;
    }
    GLFWgamepadstate state;
    if (!glfwJoystickIsGamepad(padNum) || !glfwGetGamepadState(padNum, &state)) {
        return false;
    }
    for (int button = 0; button <= GLFW_GAMEPAD_BUTTON_LAST; button++) {
        if (state.buttons[button]) {
            return true;
        }
    }
    return state.axes[GLFW_GAMEPAD_AXIS_LEFT_TRIGGER] > 0.0f
        || state.axes[GLFW_GAMEPAD_AXIS_RIGHT_TRIGGER] > 0.0f;
#else
    (void)padNum;
    return false;
#endif
}

void NoteButtonDown(int padNum) {
    const bool pad = sNextPressIsInjectedPad || RealPadHasAButtonDown(padNum);
    sNextPressIsInjectedPad = false;
    sDevice = pad ? kDevicePad : kDeviceKeyboard;
    if (pad) {
        sPadPresses++;
    }
}

songbrowser_ui::HintGlyphs GlyphsFor(Device device) {
    songbrowser_ui::HintGlyphs glyphs; // the pad's names are the defaults
    if (device == kDeviceKeyboard) {
        glyphs.accept = "ENTER";
        glyphs.back = "ESC";
        glyphs.sort = "TAB";
        glyphs.section = "< >";
        glyphs.type = "ABC";
    }
    return glyphs;
}

// The classic list's row for a command: the function node's token.
const char *CommandToken(songbrowser_ui::Command command) {
    return command == songbrowser_ui::Command::RandomSong ? "random_song" : "playlists";
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
        pendingCommand_ = -1;
        ClearCues();
        leftList_ = nullptr;
        if (HelpBarPanel *bar = TheHamUI.GetHelpBarPanel()) {
            leftList_ =
                bar->DataDir() ? bar->DataDir()->Find<HamNavList>("left_hand.hnl", false) : nullptr;
        }
        handSince_ = -1.0;
        handArmed_ = false;
        handNow_ = false;
        startPressed_ = false;
        previewSong_.clear();
        // Arriving, the hands have the menu only when the choice that led
        // here was made by hand (the game's own record of the last list
        // select) and the pad has not been touched since.
        handsSteer_ = mode_ == kAuto && !HamNavList::sLastSelectInControllerMode
            && !InControllerMode() && TrackedBodies() > 0;
        handsWhy_ = "song select was reached by hand (the last menu choice was a gesture)";
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
        SetWakeCapture(false);
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
        leftList_ = nullptr;
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
            // The game's: Start hands the menu back to the hands
            // (ShellInput::OnMsg leaves controller mode).  Update() sees
            // whether it did.
            startPressed_ = true;
            return DATA_UNHANDLED;
        }
        NotePadActivity();
        if (!gate_.frozen) {
            nativeui_host::OnButtonDown(host_, press);
            RunPending();
        }
        // Every other press is the browser's while it is shown, used or not:
        // the hidden list and the help bar must not act on it.
        return DataNode(1);
    }

    bool HidesChrome() const { return shown_; }

    // POST /api/songbrowser/command: what the screen's onCommand runs.
    bool RunCommandNamed(const std::string &name, std::string &error) {
        songbrowser_ui::Command command;
        if (name == "random_song") {
            command = songbrowser_ui::Command::RandomSong;
        } else if (name == "playlists") {
            command = songbrowser_ui::Command::Playlists;
        } else {
            error = "unknown command '" + name + "' (random_song, playlists)";
            return false;
        }
        return RunCommand(command, error);
    }

    std::string StateJson() const {
        using songbrowser::JsonEscape;
        std::string json = "{\"available\":true";
        json += ",\"mode\":\"" + std::string(ModeName()) + "\"";
        json += std::string(",\"on_song_select\":") + (panel_ ? "true" : "false");
        json += std::string(",\"eligible\":") + (eligible_ ? "true" : "false");
        json += std::string(",\"shown\":") + (shown_ ? "true" : "false");
        json += ",\"why\":\"" + JsonEscape(Why()) + "\"";
        json += std::string(",\"hands_steering\":") + (handsSteer_ ? "true" : "false");
        json += std::string(",\"hand_in_pose\":") + (handNow_ ? "true" : "false");
        json += ",\"tracked_bodies\":" + std::to_string(TrackedBodies());
        json += ",\"switches\":" + std::to_string(switches_);
        json += std::string(",\"controller_mode\":")
            + (TheGestureMgr && TheGestureMgr->InControllerMode() ? "true" : "false");
        json += std::string(",\"text_capture\":") + (host_.CapturingText() ? "true" : "false");
        json += std::string(",\"leaving\":") + (gate_.frozen ? "true" : "false");
        json += std::string(",\"engine_text_capture\":") + (TextInputCapture() ? "true" : "false");
        json += std::string(",\"hints\":\"") + (sDevice == kDeviceKeyboard ? "keyboard" : "pad")
            + "\"";
        json += std::string(",\"hint_accept\":\"") + JsonEscape(GlyphsFor(shownDevice_).accept)
            + "\"";
        json += ",\"pad_presses\":" + std::to_string(sPadPresses);
        json += ",\"screen_builds\":" + std::to_string(screenBuilds_);
        json += ",\"preview_song\":\"" + JsonEscape(previewSong_) + "\"";
        json += ",\"preview_changes\":" + std::to_string(previewChanges_);
        json += ",\"preview_starts\":" + std::to_string(gNativeSongPreviewStarts);
        json += std::string(",\"preview_hold\":") + (gNativeSongPreviewHold ? "true" : "false");
        json += ",\"commands\":[";
        if (panel_ && eligible_) {
            bool first = true;
            for (songbrowser_ui::Command command :
                 { songbrowser_ui::Command::RandomSong, songbrowser_ui::Command::Playlists }) {
                if (CommandRow(command) >= 0) {
                    json += std::string(first ? "\"" : ",\"") + CommandToken(command) + "\"";
                    first = false;
                }
            }
        }
        json += "]";
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
            json += ",\"list_index\":" + std::to_string(screen_->List().SelectedIndex());
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
            return mode_ == kAlways ? "DC3_SONG_BROWSER=1"
                                    : "the pad or the keyboard has the menu (no hand is steering)";
        }
        return hiddenWhy_;
    }

    bool DecideEligible(std::string &why) {
        if (mode_ == kNever) {
            why = modeWhy_;
            return false;
        }
        // No test of the game mode here: song_select_panel is on one screen,
        // song_select_screen, and its own enter script has put a playlist
        // mode back to perform before SongSelectPanel::Enter calls this, so
        // every visit is a pick-one-song visit (Perform, Rehearse, Dance
        // Battle).  Playlist building and the campaign are other panels.
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
        delegate.onCommand = [this](songbrowser_ui::Command command) {
            pendingCommand_ = (int)command;
        };
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
        shownDevice_ = sDevice;
        environment.glyphs = GlyphsFor(shownDevice_);
        screenBuilds_++;
        host_.SetScreen(nullptr);
        gate_.screen = nullptr;
        screen_ = std::make_unique<SongBrowserScreen>(catalog_, delegate, environment, restore);
        gate_.screen = screen_.get();
        host_.SetScreen(&gate_);
    }

    // The per-frame step: decides shown or hidden, then runs the shown browser.
    //
    // The auto rule.  A body in the camera's view is not intent, and neither
    // is the game's controller-mode timeout: the browser stays until someone
    // steers by hand, and comes back on a press or a typed character.
    //   to the hands   a tracked skeleton's hand is where the song list (or
    //                  the help bar's left-hand list) needs it, by the list's
    //                  own test, for kHandTakeoverSeconds without a break --
    //                  counted only from a moment the hand was NOT there
    //                  since the last press or typed character; or Start,
    //                  when the game then leaves controller mode
    //   to the pad     the game is in controller mode again (a press:
    //                  ShellInput::OnMsg), or a character was typed
    void Update() {
        const double now = nativeui_host::GameUiSeconds();
        const bool controllerMode = InControllerMode();

        if (mode_ == kAuto) {
            handNow_ = HandInListPose();
            if (handsSteer_) {
                if (controllerMode) {
                    TakeForThePad(); // a press woke the pad
                } else if (wakeCapture_ && TextInputPending() > 0) {
                    sDevice = kDeviceKeyboard;
                    TakeForThePad(); // typed: Show() below, and the text is typed into it
                }
            } else if (startPressed_ && !controllerMode && TrackedBodies() > 0) {
                handsSteer_ = true;
                handsWhy_ = "Start handed the menu to the hands";
            } else if (!handNow_) {
                handArmed_ = true;
                handSince_ = -1.0;
            } else if (handArmed_) {
                if (handSince_ < 0.0) {
                    handSince_ = now;
                }
                if (now - handSince_ >= kHandTakeoverSeconds) {
                    GiveToTheHands();
                }
            }
        }
        startPressed_ = false;

        bool want = mode_ == kAlways || !handsSteer_;
        bool forTheHands = !want;
        if (!want) {
            hiddenWhy_ = handsWhy_;
        }
        if (!panel_->Showing() || TheHamUI.GetOverlayPanel() != nullptr
            || (TheUIEventMgr && TheUIEventMgr->HasActiveDialogEvent())) {
            if (want) {
                hiddenWhy_ = "an overlay or a dialog is up";
            }
            want = false;
            forTheHands = false;
        }
        if (want != shown_) {
            if (want) {
                Show(now);
            } else {
                Hide();
            }
        }
        // The classic list is up for the hands: a typed character must still
        // be heard, to bring the browser back.
        SetWakeCapture(forTheHands);
        if (!shown_) {
            return;
        }
        KeepControllerMode();

        if (TheSongSortMgr && (int)TheSongSortMgr->MapSize() != catalogStamp_) {
            FillCatalog(); // content came or went
            screen_->SetCatalog(catalog_);
        }

        gate_.typed = false;
        host_.Tick(now); // typed text, held directions, art
        if (gate_.typed) {
            sDevice = kDeviceKeyboard;
            NotePadActivity();
        }
        if (sDevice != shownDevice_) {
            shownDevice_ = sDevice; // the hint bar names the device last used
            screen_->SetHintGlyphs(GlyphsFor(shownDevice_));
        }
        RunPending();

        if (UIPanel *info = SongInfoPanel()) {
            if (info->Showing()) {
                info->SetShowing(false);
            }
        }
        if (gNativeSongPreviewHold
            && (gate_.frozen || now - selectionMovedAt_ >= kPreviewRestSeconds)) {
            gNativeSongPreviewHold = false; // the selection has rested: play it
        }
        NotePreview();
        if (gate_.frozen && leavingSince_ >= 0.0 && !TheUI->InTransition()
            && now - leavingSince_ >= kLeaveGiveUpSeconds) {
            gate_.frozen = false; // the pick went nowhere (a locked or bad song)
            leavingSince_ = -1.0;
        }
    }

    void Show(double now) {
        shown_ = true;
        switches_++;
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
        switches_++;
        gNativeSongPreviewHold = false;
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
        if (pendingCommand_ >= 0) {
            const songbrowser_ui::Command command = (songbrowser_ui::Command)pendingCommand_;
            pendingCommand_ = -1;
            ClearCues();
            std::string error;
            if (!RunCommand(command, error)) {
                TheSynth->RunFlow("invalid_select.flow");
            }
            return;
        }
        if (cueTab_) {
            const songbrowser_ui::State state = screen_->SaveState();
            TheNativeSongSelectState().SetSort(SortFileName(state.arrangement));
        }
        // The selected song moved: the classic list follows, and plays the
        // list's highlight sound itself.  Any other cue plays here.
        const bool moved = SyncClassicList();
        if (moved) {
            selectionMovedAt_ = nativeui_host::GameUiSeconds();
            gNativeSongPreviewHold = true;
        }
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

    // The classic list's row for `command`, or -1 when this visit's list has
    // none (Rehearse has neither; Dance Battle has no Playlists).
    int CommandRow(songbrowser_ui::Command command) const {
        if (!TheSongSortMgr) {
            return -1;
        }
        static Symbol first_data_index("first_data_index");
        const DataNode row = TheSongSortMgr->Handle(
            Message(first_data_index, Symbol(CommandToken(command))), false
        );
        return row.Type() == kDataInt ? row.Int() : -1;
    }

    // A command: the classic list's own row for it, selected by the game's
    // own select-a-row (what the "playlists" voice command runs).  What
    // happens next is that row's: SongFunctionNode::OnSelect.
    bool RunCommand(songbrowser_ui::Command command, std::string &error) {
        if (!screen_ || !panel_ || !list_ || !shown_) {
            error = "the song browser is not shown (" + Why() + ")";
            return false;
        }
        if (gate_.frozen || TheUI->InTransition()) {
            error = "the song browser is on its way out";
            return false;
        }
        Uncollapse();
        const int row = CommandRow(command);
        if (row < 0) {
            error = std::string("the classic list has no ") + CommandToken(command)
                + " row in this mode";
            return false;
        }
        gate_.frozen = true;
        leavingSince_ = nativeui_host::GameUiSeconds();
        static Symbol select_right_list_item("select_right_list_item");
        panel_->Handle(Message(select_right_list_item, row), false);
        return true;
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

    // A press or typed text reached the browser: a hand that happens to be
    // out does not take the menu from it.
    void NotePadActivity() {
        handSince_ = -1.0;
        handArmed_ = false;
    }

    void TakeForThePad() {
        handsSteer_ = false;
        handSince_ = -1.0;
        handArmed_ = false;
    }

    void GiveToTheHands() {
        handSince_ = -1.0;
        handArmed_ = false;
        ShellInput *shell = TheHamUI.GetShellInput();
        if (!shell || Dc3ControllerModeForced()) {
            return; // controller mode is pinned: the hands cannot drive a list
        }
        if (InControllerMode()) {
            shell->ExitControllerMode(true); // the lists take the hands now
        }
        if (InControllerMode()) {
            return;
        }
        handsSteer_ = true;
        handsWhy_ = "a hand is steering (held where the list takes it; controller mode is off)";
    }

    // While the browser is shown the game stays in controller mode, whatever
    // its idle timeout says: the hands do not drive the list underneath, and
    // the next press is the browser's, not the one that wakes the pad.
    void KeepControllerMode() {
        ShellInput *shell = TheHamUI.GetShellInput();
        if (!shell || !TheGestureMgr) {
            return;
        }
        if (!TheGestureMgr->InControllerMode()) {
            shell->EnterControllerMode(true);
        }
        shell->unk_0x68.Restart(); // the controller-mode timeout starts over
    }

    // Text capture while the browser is hidden for the hands (the host holds
    // it while the browser is shown).
    void SetWakeCapture(bool on) {
        if (on == wakeCapture_) {
            return;
        }
        wakeCapture_ = on;
        if (on) {
            TextInputSetCapture(true);
        } else if (!shown_ && !host_.CapturingText()) {
            TextInputSetCapture(false); // shown: the host holds it from here
        }
    }

    // Whether any tracked skeleton holds a hand where the song list, or the
    // help bar's left-hand list, takes it.
    bool HandInListPose() const {
        if (!TheGestureMgr) {
            return false;
        }
        for (int i = 0; i < 6; i++) {
            const Skeleton &skeleton = TheGestureMgr->GetSkeleton(i);
            if (!skeleton.IsTracked()) {
                continue;
            }
            if (Dc3MenuGesture::HandInListPose(list_, skeleton)
                || Dc3MenuGesture::HandInListPose(leftList_, skeleton)) {
                return true;
            }
        }
        return false;
    }

    // Counts the songs the preview was started on (for the state endpoint).
    void NotePreview() {
        SongPreview *preview = TheSongSortMgr ? TheSongSortMgr->GetSongPreview() : nullptr;
        if (!preview) {
            return;
        }
        static Symbol get_song("get_song");
        const DataNode song = preview->Handle(Message(get_song), false);
        const std::string name = song.Type() == kDataSymbol ? song.Sym().Str() : "";
        if (name != previewSong_) {
            previewSong_ = name;
            if (!name.empty()) {
                previewChanges_++;
            }
        }
    }

    static bool InControllerMode() {
        return TheGestureMgr && TheGestureMgr->InControllerMode();
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
    HamNavList *leftList_ = nullptr; // the help bar's left_hand.hnl
    bool eligible_ = false; // this visit may show the browser
    bool shown_ = false;
    // The auto rule.
    bool handsSteer_ = false; // the latch: the classic list is up for the hands
    std::string handsWhy_;
    bool handNow_ = false; // a hand is where a list takes it, this frame
    bool handArmed_ = false; // the hand was seen away since the last press
    double handSince_ = -1.0; // when the armed hand got there
    bool startPressed_ = false;
    bool wakeCapture_ = false;
    unsigned switches_ = 0; // times the browser was shown or hidden, this run
    int catalogStamp_ = 0;

    songbrowser_ui::State saved_;
    bool hasSaved_ = false;
    std::string syncedSong_; // the song the classic list was last put on
    Device shownDevice_ = kDeviceKeyboard; // what the screen's hint bar names
    unsigned screenBuilds_ = 0;
    std::string previewSong_;
    unsigned previewChanges_ = 0;
    double selectionMovedAt_ = -1.0;

    std::string pendingPick_;
    bool pendingBack_ = false;
    int pendingCommand_ = -1; // a songbrowser_ui::Command, or -1
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

void HookInjectedPadPress() { sNextPressIsInjectedPad = true; }
bool HookCommand(const std::string &name, std::string &error) {
    if (sBrowserMade) return TheBrowser().RunCommandNamed(name, error);
    error = "the song browser is not shown (not on song select)";
    return false;
}

const NativeSongBrowser::Hooks kHooks = {
    HookEnter,       HookExit,      HookPoll,             HookDraw,   HookHandle,
    HookHidesChrome, HookStateJson, NoteButtonDown, HookInjectedPadPress, HookCommand,
};

struct Installer {
    Installer() { NativeSongBrowser::Install(&kHooks); }
} sInstaller;

} // namespace
