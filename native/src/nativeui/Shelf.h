// Shelf: a horizontal, virtualised, selectable row of cards (album art for
// "recently played", say).
//
// The screen says how many cards there are and builds each on demand:
//   build(index) -> the widget for that card
// Every card has the theme's shelf card size.
//
// Behaviour (the same selection and scroll logic as ListView, sideways):
//  - Left/Right move the selection; Home/End go to the ends. No wrap: Left on
//    the first card and Right on the last are not consumed.
//  - Accept calls onActivated with the selected card.
//  - The selection scrolls into view on a spring. The selected card grows a
//    little and gets an accent ring; both glide to the next card.
//  - Only cards near the viewport exist.
//
// Rules:
//  - Cards live in the shelf's content coordinates and are not part of the
//    focus tree.
//  - The shelf is a little taller than a card, to leave room for the selected
//    card's growth.
#pragma once

#include "nativeui/Animation.h"
#include "nativeui/ScrollStrip.h"
#include "nativeui/Widget.h"

#include <functional>
#include <map>
#include <memory>

namespace nativeui {

    class Shelf final : public Widget {
    public:
        using BuildCard = std::function<std::unique_ptr<Widget>(int index)>;

        explicit Shelf(BuildCard build);

        void SetCardCount(int count);
        int CardCount() const { return strip_.Count(); }

        // -1 when there are no cards.
        int SelectedIndex() const { return strip_.Selected(); }
        // True if the selection changed. Calls onSelectionChanged.
        bool Select(int index);

        std::function<void(int index)> onSelectionChanged;
        std::function<void(int index)> onActivated;

        bool IsFocusable() const override { return true; }
        bool HandleEvent(const Event &event) override;

        // For tests and tools.
        float ScrollOffset() const { return strip_.Scroll(); }
        int BuiltCardCount() const { return static_cast<int>(cards_.size()); }

    protected:
        Size OnMeasure(const Context &ctx, const Constraints &constraints) override;
        void OnArrange(const Context &ctx, const Rect &bounds) override;
        void OnAdvance(const Context &ctx, float deltaSeconds) override;
        void OnPaint(const Context &ctx, Painter &painter) const override;
        bool IsAtRest() const override;

    private:
        bool Changed(bool selectionChanged);
        void SetItems(int count, float cardWidth, float gap);
        void PaintCard(
            const Context &ctx, Painter &painter, int index, const Widget &card
        ) const;
        // 1 for the card the selection is on, falling to 0 one card away.
        float Emphasis(int index) const;

        BuildCard build_;
        ScrollStrip strip_;
        std::map<int, std::unique_ptr<Widget> > cards_; // the cards that exist right now
        Spring selectionGlide_; // the selected index, as a moving number
        Spring focusAmount_; // 0 = another widget has focus, 1 = this shelf does
        float cardWidth_ = 0.0f;
        float gap_ = 0.0f;
        bool jumpOnNextLayout_ = true;
    };

}
