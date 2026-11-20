// Containers: the three small widgets the song browser needs that are not
// about songs.
//
//   EventScope  one child; events nothing below it consumed are offered to a
//               callback (ancestor routing, nativeui/Focus.h). The browser's
//               navigation between its regions lives in one of these.
//   Reveal      one child that can be shown or hidden; its height glides
//               between the child's own height and nothing, and the child
//               fades as it goes. Hidden, it takes no room at all.
//   Notice      two centred lines of text ("NO SONGS MATCH" and why), shown
//               or not; it takes whatever room it is given and no focus.
#pragma once

#include "nativeui/Animation.h"
#include "nativeui/Widget.h"

#include <functional>
#include <memory>
#include <string>

namespace songbrowser_ui {

    class EventScope final : public nativeui::Widget {
    public:
        explicit EventScope(std::unique_ptr<nativeui::Widget> child);

        std::function<bool(const nativeui::Event &)> onEvent;
        bool HandleEvent(const nativeui::Event &event) override {
            return onEvent && onEvent(event);
        }

    protected:
        nativeui::Size OnMeasure(
            const nativeui::Context &ctx, const nativeui::Constraints &constraints
        ) override;
        void OnArrange(const nativeui::Context &ctx, const nativeui::Rect &bounds) override;
    };

    class Reveal final : public nativeui::Widget {
    public:
        explicit Reveal(std::unique_ptr<nativeui::Widget> child);

        // Glides to shown or hidden. The first layout jumps.
        void SetShown(bool shown) { shown_ = shown; }
        bool Shown() const { return shown_; }
        // 0 = hidden, 1 = fully shown.
        float Amount() const { return amount_.Value(); }

    protected:
        nativeui::Size OnMeasure(
            const nativeui::Context &ctx, const nativeui::Constraints &constraints
        ) override;
        void OnArrange(const nativeui::Context &ctx, const nativeui::Rect &bounds) override;
        void OnAdvance(const nativeui::Context &ctx, float deltaSeconds) override;
        void OnPaint(const nativeui::Context &ctx, nativeui::Painter &painter) const override;
        bool IsAtRest() const override { return !pending_ && amount_.AtRest(); }

    private:
        bool shown_ = true;
        bool pending_ = true; // the first Advance snaps instead of gliding
        nativeui::Tween amount_ { 1.0f };
        float childHeight_ = 0.0f;
    };

    class Notice final : public nativeui::Widget {
    public:
        void Show(std::string headline, std::string detail);
        void Hide() { shown_ = false; }
        bool Shown() const { return shown_; }
        const std::string &Headline() const { return headline_; }

    protected:
        nativeui::Size OnMeasure(
            const nativeui::Context &ctx, const nativeui::Constraints &constraints
        ) override;
        void OnPaint(const nativeui::Context &ctx, nativeui::Painter &painter) const override;

    private:
        bool shown_ = false;
        std::string headline_;
        std::string detail_;
    };

}
