// ListView: a vertical, virtualised, selectable list.
//
// The screen describes the rows and builds them on demand:
//   describe(index) -> height and whether the row can be selected
//   build(index)    -> the widget for that row (any widget tree)
// Row kinds are the screen's business: a section header is simply a row that
// is not selectable and has its own height and widget.
//
// Behaviour:
//  - Only rows near the viewport exist. Rows are built as they come within
//    half a viewport of it and destroyed when they leave, so a list of a
//    thousand rows holds a couple of dozen widgets.
//  - Up/Down move the selection, skipping rows that are not selectable.
//    Home/End go to the first/last selectable row, PageUp/PageDown move about
//    one viewport. There is no wrap: Up on the first row and Down on the last
//    are not consumed, so the screen can move focus.
//  - Accept calls onActivated with the selected row.
//  - The selection scrolls into view on a spring, and a highlight bar glides
//    from the old row to the new one. The first layout and a Reload jump
//    instead of animating.
//
// Rules:
//  - Rows live in the list's content coordinates (y = 0 at the top of row 0),
//    not screen coordinates, and are not part of the focus tree.
//  - Rows do not know they are selected; the highlight is drawn behind them.
//  - After the data changes, call SetRowCount (or Reload): every row is
//    described again and rebuilt.
#pragma once

#include "nativeui/Animation.h"
#include "nativeui/ScrollStrip.h"
#include "nativeui/Widget.h"

#include <functional>
#include <map>
#include <memory>

namespace nativeui {

    struct ListRowInfo {
        float height = 0.0f;
        bool selectable = true;
    };

    class ListView final : public Widget {
    public:
        using DescribeRow = std::function<ListRowInfo(int index)>;
        using BuildRow = std::function<std::unique_ptr<Widget>(int index)>;

        ListView(DescribeRow describe, BuildRow build);

        void SetRowCount(int count);
        void Reload() { SetRowCount(RowCount()); }
        int RowCount() const { return strip_.Count(); }

        // -1 when no row can be selected.
        int SelectedIndex() const { return strip_.Selected(); }
        // True if the selection changed. Calls onSelectionChanged.
        bool Select(int index);
        bool SelectFirst();
        // Puts the list at a saved scroll position without animating. Call it
        // after SetRowCount and Select; the selection is still kept in view.
        void SetScrollOffset(float offset) { strip_.SetScroll(offset); }

        std::function<void(int index)> onSelectionChanged;
        std::function<void(int index)> onActivated;

        bool IsFocusable() const override { return true; }
        bool HandleEvent(const Event &event) override;

        // For tests and tools.
        float ScrollOffset() const { return strip_.Scroll(); }
        // Where the scroll is going; equal to ScrollOffset() at rest.
        float ScrollTarget() const { return strip_.ScrollTarget(); }
        int BuiltRowCount() const { return static_cast<int>(rows_.size()); }
        int TotalRowsBuilt() const { return totalRowsBuilt_; }
        // Where the highlight bar is now, in the same space as Bounds().
        Rect HighlightRect() const;

    protected:
        Size OnMeasure(const Context &ctx, const Constraints &constraints) override;
        void OnArrange(const Context &ctx, const Rect &bounds) override;
        void OnAdvance(const Context &ctx, float deltaSeconds) override;
        void OnPaint(const Context &ctx, Painter &painter) const override;
        bool IsAtRest() const override;

    private:
        bool Changed(bool selectionChanged);
        void AimHighlight();
        void RealizeRows(const Context &ctx, const Rect &bounds);

        DescribeRow describe_;
        BuildRow build_;
        ScrollStrip strip_;
        std::map<int, std::unique_ptr<Widget> > rows_; // the rows that exist right now
        Spring highlightTop_;
        Spring highlightHeight_;
        Spring focusAmount_; // 0 = another widget has focus, 1 = this list does
        bool jumpOnNextLayout_ = true;
        int totalRowsBuilt_ = 0;
    };

}
