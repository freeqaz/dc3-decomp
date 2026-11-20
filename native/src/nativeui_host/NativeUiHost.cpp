#include "nativeui_host/NativeUiHost.h"

#include "gfx/Canvas2D.h"
#include "platform/TextInput_Native.h"

#include <chrono>
#include <vector>

namespace nativeui_host {

    namespace {

        std::uint32_t MicrosSince(std::chrono::steady_clock::time_point start) {
            return static_cast<std::uint32_t>(
                std::chrono::duration_cast<std::chrono::microseconds>(
                    std::chrono::steady_clock::now() - start
                )
                    .count()
            );
        }

        // A step of the game's UI clock larger than this is a pause, a load or
        // a reset, not a frame: animations do not jump across it.
        constexpr double kLongestStep = 0.25;

    }

    NativeUiHost::NativeUiHost(const FontSet &fonts, ImageCache *images)
        : fonts_(fonts), images_(images), imageSource_(images) {}

    NativeUiHost::~NativeUiHost() { SetCapture(false); }

    void NativeUiHost::SetScreen(nativeui::Screen *screen) {
        plain_.screen = screen;
        SetScreen(screen ? static_cast<HostedScreen *>(&plain_) : nullptr);
    }

    void NativeUiHost::SetScreen(HostedScreen *screen) {
        if (screen != &plain_) {
            plain_.screen = nullptr;
        }
        screen_ = screen;
        repeater_.ReleaseAll();
        UpdateCapture();
    }

    void NativeUiHost::SetOutputSize(int width, int height) {
        (void)width;
        deviceScaleY_ = height > 0 ? static_cast<float>(height) / kLogicalHeight : 0.0f;
    }

    void NativeUiHost::AdvanceClock(double gameSeconds) {
        if (hasGameTime_) {
            double step = gameSeconds - lastGameSeconds_;
            if (step < 0.0) {
                step = 0.0; // the UI clock was reset: time does not run backwards
            } else if (step > kLongestStep) {
                step = kLongestStep;
            }
            clock_.now += step;
        }
        hasGameTime_ = true;
        lastGameSeconds_ = gameSeconds;
    }

    void NativeUiHost::Enter(double gameSeconds) {
        hasGameTime_ = false;
        AdvanceClock(gameSeconds);
        entered_ = true;
        repeater_.ReleaseAll();
        if (screen_) {
            screen_->PlayEnter();
        }
        UpdateCapture();
    }

    void NativeUiHost::Exit() {
        entered_ = false;
        repeater_.ReleaseAll();
        SetCapture(false);
    }

    void NativeUiHost::SetCapture(bool on) {
        if (on == capturing_) {
            return;
        }
        capturing_ = on;
        TextInputSetCapture(on);
    }

    void NativeUiHost::UpdateCapture() {
        bool want = false;
        if (screen_ && entered_) {
            switch (capturePolicy_) {
            case TextCapture::WhenTextFocused:
                want = screen_->WantsTextInput();
                break;
            case TextCapture::Always:
                want = true;
                break;
            case TextCapture::Never:
                want = false;
                break;
            }
        }
        SetCapture(want);
    }

    bool NativeUiHost::Send(const nativeui::Event &event) {
        if (!screen_) {
            return false;
        }
        eventsSinceDraw_++;
        const bool consumed = screen_->HandleEvent(event);
        UpdateCapture(); // the event may have moved focus onto or off a text field
        return consumed;
    }

    void NativeUiHost::Tick(double gameSeconds) {
        AdvanceClock(gameSeconds);
        if (images_) {
            images_->Poll();
        }
        if (!screen_ || !entered_) {
            return;
        }

        // Typed text and edit keys. Events are drained even when nothing is
        // focused on a text field: the screen's own hook may forward them
        // (type-to-search), and an injected event is queued regardless of
        // capture.
        TextInputEvent events[32];
        for (int count; (count = TextInputDrain(events, 32)) > 0;) {
            for (int i = 0; i < count; i++) {
                if (const std::optional<nativeui::Event> event = MapText(events[i])) {
                    Send(*event);
                }
            }
        }

        std::vector<nativeui::Action> repeats;
        repeater_.Tick(clock_.now, repeats);
        for (nativeui::Action action : repeats) {
            Send(nativeui::ActionEvent { action });
        }
        UpdateCapture();
    }

    bool NativeUiHost::OnButtonDown(int padButton, int padAction) {
        if (!screen_ || !entered_) {
            return false;
        }
        const std::optional<nativeui::Action> action = MapPad(padButton, padAction);
        if (!action) {
            return false;
        }
        if (IsDirection(*action)) {
            if (!repeater_.Press(*action, clock_.now)) {
                return true; // the game's own repeat of a held direction
            }
        }
        Send(nativeui::ActionEvent { *action });
        // A mapped press is the UI's whether or not a widget used it: a Down
        // that ran off the end of a list must not also move the game's menu.
        return true;
    }

    void NativeUiHost::OnButtonUp(int padButton, int padAction) {
        const std::optional<nativeui::Action> action = MapPad(padButton, padAction);
        if (action && IsDirection(*action)) {
            repeater_.Release(*action);
        }
    }

    void NativeUiHost::DrawTo(QuadSink &sink) {
        stats_ = FrameStats();
        stats_.eventsHandled = eventsSinceDraw_;
        eventsSinceDraw_ = 0;
        if (!screen_) {
            return;
        }
        const auto buildStart = std::chrono::steady_clock::now();
        const nativeui::DisplayList &list = screen_->BuildFrame();
        stats_.buildMicros = MicrosSince(buildStart);
        stats_.commands = static_cast<std::uint32_t>(list.commands.size());

        const auto replayStart = std::chrono::steady_clock::now();
        CanvasRenderer renderer(sink, fonts_, imageSource_);
        renderer.SetDeviceScale(deviceScaleY_);
        nativeui::Replay(list, renderer);
        stats_.replayMicros = MicrosSince(replayStart);
        const CanvasRenderer::Counts &counts = renderer.GetCounts();
        stats_.quads = counts.quads;
        stats_.glyphs = counts.glyphs;
        stats_.placeholderGlyphs = counts.placeholders;
        stats_.imagesMissing = counts.imagesMissing;
        stats_.drew = true;
    }

    void NativeUiHost::Draw() {
        Canvas2D *canvas = TheCanvas2D();
        if (!screen_ || !canvas || !canvas->Begin(kLogicalWidth, kLogicalHeight)) {
            stats_ = FrameStats();
            stats_.eventsHandled = eventsSinceDraw_;
            eventsSinceDraw_ = 0;
            return;
        }
        const Canvas2D::FrameStats before = canvas->Stats();
        Canvas2DSink sink(*canvas);
        const auto start = std::chrono::steady_clock::now();
        DrawTo(sink);
        const std::uint32_t build = stats_.buildMicros;
        canvas->End(); // flushes: the draw calls are recorded here
        stats_.replayMicros = MicrosSince(start) - build;
        const Canvas2D::FrameStats &after = canvas->Stats();
        stats_.quads = after.quads - before.quads;
        stats_.drawCalls = after.drawCalls - before.drawCalls;
    }

}
