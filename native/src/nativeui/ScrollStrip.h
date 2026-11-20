// ScrollStrip: the logic shared by ListView (vertical) and Shelf (horizontal):
// a line of items with extents, one of them selected, seen through a viewport
// that scrolls to keep the selection in view.
//
// It is a plain value with no widgets, drawing or theme in it. "Extent" is an
// item's size along the strip; "start" is where it begins.
//
// Rules:
//  - Items that are not selectable (section headers) are skipped by every
//    selection move.
//  - Selection does not wrap. A move that has nowhere to go returns false and
//    changes nothing, which is how focus gets to leave a list.
//  - The scroll target is the smallest move that shows the selected item with
//    `margin` to spare; the first and last selectable items pin the scroll to
//    its ends, so leading and trailing headers are shown too.
//  - The scroll position follows its target on a spring (Animation.h), but
//    never from further than kLongestGlideInViewports away: a longer jump
//    (End on a long list) moves most of the way at once and glides the rest.
#pragma once

#include "nativeui/Animation.h"

#include <vector>

namespace nativeui {

    struct StripItem {
        float extent = 0.0f;
        bool selectable = true;
    };

    // How far the scroll position may glide, in viewports.
    constexpr float kLongestGlideInViewports = 1.5f;

    // A half-open run of item indices: [first, last).
    struct StripRange {
        int first = 0;
        int last = 0;
    };

    class ScrollStrip {
    public:
        // Replaces the items. The selection keeps its index if that is still a
        // selectable item, and otherwise moves to the first selectable one.
        void SetItems(std::vector<StripItem> items, float gap);
        // The visible length and how much of it to keep clear around the selection.
        void SetViewport(float extent, float margin);

        int Count() const { return static_cast<int>(items_.size()); }
        float Start(int index) const { return starts_[index]; }
        float Extent(int index) const { return items_[index].extent; }
        float End(int index) const { return starts_[index] + items_[index].extent; }
        float ContentExtent() const { return contentExtent_; }
        float ViewportExtent() const { return viewport_; }
        bool IsSelectable(int index) const;

        // -1 when nothing is selectable.
        int Selected() const { return selected_; }
        // Each returns true if the selection changed.
        bool Select(int index);
        bool Step(int direction); // -1 or +1: the next selectable item that way
        bool SelectFirst();
        bool SelectLast();
        bool Page(int direction); // -1 or +1: about one viewport that way

        float Scroll() const { return scroll_.Value(); }
        float ScrollTarget() const { return scroll_.Target(); }
        float MaxScroll() const;
        void SnapScroll() { scroll_.Snap(scroll_.Target()); }
        void Advance(float deltaSeconds, const SpringParams &spring);
        bool AtRest() const { return scroll_.AtRest(); }

        // Items that intersect the viewport at the current scroll position,
        // widened by `overscan` at both ends.
        StripRange VisibleRange(float overscan) const;

    private:
        int NextSelectable(int from, int direction) const;
        void RetargetScroll();

        std::vector<StripItem> items_;
        std::vector<float> starts_;
        float contentExtent_ = 0.0f;
        float viewport_ = 0.0f;
        float margin_ = 0.0f;
        int selected_ = -1;
        Spring scroll_;
    };

}
