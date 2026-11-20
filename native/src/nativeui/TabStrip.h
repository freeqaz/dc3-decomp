// TabStrip: a row of labelled tabs, one of them selected (the sort tabs).
//
// Behaviour:
//  - NextTab/PrevTab step through the tabs and wrap around: they are the
//    shoulder buttons, which cycle.
//  - Left/Right step and stop at the ends, where they are not consumed, so
//    focus can leave sideways.
//  - onChanged fires whenever the selected tab changes, however it changed.
//  - An accent bar under the selected tab glides to the new tab on a spring.
//
// A screen that wants NextTab/PrevTab to work wherever focus is forwards those
// events to the strip from its unhandled-event hook.
#pragma once

#include "nativeui/Animation.h"
#include "nativeui/Widget.h"

#include <functional>
#include <string>
#include <vector>

namespace nativeui {

    class TabStrip final : public Widget {
    public:
        explicit TabStrip(std::vector<std::string> labels);

        int TabCount() const { return static_cast<int>(labels_.size()); }
        int SelectedIndex() const { return selected_; }
        // True if the selection changed. Calls onChanged.
        bool Select(int index);

        std::function<void(int index)> onChanged;

        bool IsFocusable() const override { return true; }
        bool HandleEvent(const Event &event) override;

    protected:
        Size OnMeasure(const Context &ctx, const Constraints &constraints) override;
        void OnArrange(const Context &ctx, const Rect &bounds) override;
        void OnAdvance(const Context &ctx, float deltaSeconds) override;
        void OnPaint(const Context &ctx, Painter &painter) const override;
        bool IsAtRest() const override;

    private:
        // Width of each tab: its label plus padding on both sides.
        std::vector<float> TabWidths(const Context &ctx) const;

        std::vector<std::string> labels_;
        int selected_ = 0;
        std::vector<Rect> tabRects_; // set by Arrange
        Spring indicatorLeft_;
        Spring indicatorWidth_;
        Spring focusAmount_;
        bool jumpOnNextLayout_ = true;
    };

}
