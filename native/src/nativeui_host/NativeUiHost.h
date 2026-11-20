// NativeUiHost: what a game panel owns to show a nativeui::Screen.
//
// It is a plain object, not a panel: it has no game base class and takes the
// game's clock and pad messages as numbers, so it is the same object in the
// game, in a test, and on the web. GameBindings.h has the three one-line
// adapters a panel uses (the UI clock, button_down / button_up messages, the
// output size).
//
// LIFECYCLE (what the owning panel does)
//
//   class SongBrowserPanel : public UIPanel {
//       nativeui_host::GameFonts mFonts;                 // Load(): mFonts.Load();
//       nativeui_host::GameImageLoader mLoader;
//       nativeui_host::ImageCache mImages { mLoader };
//       nativeui_host::NativeUiHost mHost { mFonts.Fonts(), &mImages };
//       std::unique_ptr<MyScreen> mScreen;               // a nativeui::Screen (or a HostedScreen)
//                                                        // built with mFonts.Fonts(), mHost.Clock()
//       void Enter() override  { UIPanel::Enter(); mHost.SetScreen(mScreen.get());
//                                mHost.Enter(nativeui_host::GameUiSeconds()); }
//       void Poll() override   { UIPanel::Poll(); mHost.Tick(nativeui_host::GameUiSeconds()); }
//       void Draw() override   { UIPanel::Draw(); nativeui_host::SetOutputSize(mHost); mHost.Draw(); }
//       void Exit() override   { mHost.Exit(); UIPanel::Exit(); }
//       DataNode OnMsg(const ButtonDownMsg &m) {         // from the panel's HANDLE_MESSAGE
//           return nativeui_host::OnButtonDown(mHost, m) ? DataNode(1) : DataNode(kDataUnhandled, 0);
//       }
//       DataNode OnMsg(const ButtonUpMsg &m) { nativeui_host::OnButtonUp(mHost, m); return DataNode(kDataUnhandled, 0); }
//   };
//
//   Enter(now)  starts the host's clock at `now`, plays the screen's entrance.
//   Tick(now)   once a frame, OUTSIDE any canvas session: advances the clock,
//               polls the image cache (which is when dropped textures die),
//               drains typed text into the screen, fires held-direction
//               repeats, and turns the engine's text capture on exactly while
//               the screen's focused widget wants text (a TextField).
//   Draw()      inside a render pass (a panel's Draw, the overlay hook): opens
//               the engine canvas at 1280x720 on the current viewport, builds
//               the screen's display list, replays it, closes the canvas.
//               Outside a pass, or with no GPU, it draws nothing and says so
//               in the stats.
//   OnButtonDown / OnButtonUp   pad messages; OnButtonDown returns whether the
//               UI used the press (false: the panel may handle it).
//   Exit()      releases text capture and held directions. The screen and the
//               fonts are untouched; Enter() again later is fine.
//
// The host borrows the screen, the fonts and the image cache; all three must
// outlive it (or SetScreen(nullptr) first).
#pragma once

#include "nativeui/Clock.h"
#include "nativeui/Screen.h"
#include "nativeui_host/CanvasRenderer.h"
#include "nativeui_host/FontSet.h"
#include "nativeui_host/HostedScreen.h"
#include "nativeui_host/IconTextures.h"
#include "nativeui_host/ImageCache.h"
#include "nativeui_host/InputBridge.h"

#include <cstddef>
#include <cstdint>

namespace nativeui_host {

    inline constexpr float kLogicalWidth = 1280.0f;
    inline constexpr float kLogicalHeight = 720.0f;

    // When the engine's text capture is on.
    enum class TextCapture {
        WhenTextFocused, // exactly while the focused widget wants text (default)
        Always, // while a screen is shown: type-to-search from anywhere, at the
                // cost of the text keys' pad meanings (X, Y, Q, E, Z, C, Space,
                // Backspace)
        Never
    };

    class NativeUiHost {
    public:
        // Per-frame numbers, as of the last Draw().
        struct FrameStats {
            bool drew = false; // false: no pass / no canvas / no screen
            std::uint32_t commands = 0; // display-list commands
            std::uint32_t quads = 0; // canvas quads this Draw() added
            std::uint32_t drawCalls = 0; // canvas draw calls this Draw() added
            std::uint32_t glyphs = 0;
            std::uint32_t placeholderGlyphs = 0; // drawn as the missing-glyph box
            std::uint32_t imagesMissing = 0; // image quads with no texture yet
            std::uint32_t buildMicros = 0; // Screen::BuildFrame
            std::uint32_t replayMicros = 0; // Replay through the canvas, End() included
            std::uint32_t eventsHandled = 0; // input events sent to the screen since the last Draw()
        };

        NativeUiHost(const FontSet &fonts, ImageCache *images);
        ~NativeUiHost();
        NativeUiHost(const NativeUiHost &) = delete;
        NativeUiHost &operator=(const NativeUiHost &) = delete;

        // The clock to build Screens with. It moves only in Enter() and Tick().
        const nativeui::Clock &Clock() const { return clock_; }
        const FontSet &Fonts() const { return fonts_; }
        ImageCache *Images() const { return images_; }

        // What the host runs: a nativeui::Screen, or any HostedScreen (see
        // HostedScreen.h: SongBrowserScreen goes through HostedScreenOf).
        // Null: nothing.  The screen is borrowed.
        void SetScreen(nativeui::Screen *screen);
        void SetScreen(HostedScreen *screen);
        void SetScreen(std::nullptr_t) { SetScreen(static_cast<HostedScreen *>(nullptr)); }
        HostedScreen *GetScreen() const { return screen_; }

        // Where image handles are resolved; the image cache by default. A
        // screen whose handles are not the cache's own gives its own source.
        void SetImageSource(ImageSource *source) { imageSource_ = source; }
        void SetTextCapture(TextCapture policy) { capturePolicy_ = policy; }
        void SetRepeat(RepeatConfig config) { repeater_.SetConfig(config); }
        // Device pixels of the pass Draw() runs in, for baseline snapping;
        // 0 = unknown (no snapping).
        void SetOutputSize(int width, int height);

        void Enter(double gameSeconds);
        void Exit();
        void Tick(double gameSeconds);
        void Draw();
        // The same frame into any sink (tests; no canvas session is opened).
        // Icons have no texture here and count as missing images: making one
        // needs the renderer, which only Draw() can rely on.
        void DrawTo(QuadSink &sink);

        bool OnButtonDown(int padButton, int padAction);
        void OnButtonUp(int padButton, int padAction);
        // Sends one event to the screen; true if it was consumed.
        bool Send(const nativeui::Event &event);

        // Whether the host currently holds the engine's text capture.
        bool CapturingText() const { return capturing_; }
        const FrameStats &Stats() const { return stats_; }

    private:
        class HostClock final : public nativeui::Clock {
        public:
            double NowSeconds() const override { return now; }
            double now = 0.0;
        };

        void AdvanceClock(double gameSeconds);
        // Builds the screen's frame and replays it into `sink`, resolving
        // image handles through `images` (may be null).
        void DrawFrame(QuadSink &sink, ImageSource *images);
        void UpdateCapture();
        void SetCapture(bool on);

        // A nativeui::Screen as a HostedScreen; the host's own, so the
        // common case needs no adapter object from the caller.
        class PlainScreen final : public HostedScreen {
        public:
            bool HandleEvent(const nativeui::Event &event) override {
                return screen->HandleEvent(event);
            }
            const nativeui::DisplayList &BuildFrame() override { return screen->BuildFrame(); }
            bool WantsTextInput() const override { return screen->WantsTextInput(); }
            void PlayEnter() override { screen->PlayEnter(); }
            nativeui::Screen *screen = nullptr;
        };

        const FontSet &fonts_;
        ImageCache *images_;
        ImageSource *imageSource_ = nullptr;
        // Draw()'s image source: nativeui's icons, then imageSource_ for the
        // rest.
        IconTextures icons_;
        PlainScreen plain_;
        HostedScreen *screen_ = nullptr;
        HostClock clock_;
        bool hasGameTime_ = false;
        double lastGameSeconds_ = 0.0;
        DirectionRepeater repeater_;
        TextCapture capturePolicy_ = TextCapture::WhenTextFocused;
        bool capturing_ = false;
        bool entered_ = false;
        float deviceScaleY_ = 0.0f;
        std::uint32_t eventsSinceDraw_ = 0;
        FrameStats stats_;
    };

}
