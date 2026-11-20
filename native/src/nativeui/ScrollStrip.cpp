#include "nativeui/ScrollStrip.h"

#include <algorithm>
#include <cmath>

namespace nativeui {

    void ScrollStrip::SetItems(std::vector<StripItem> items, float gap) {
        items_ = std::move(items);
        starts_.resize(items_.size());
        float position = 0.0f;
        for (std::size_t i = 0; i < items_.size(); ++i) {
            starts_[i] = position;
            position += items_[i].extent + gap;
        }
        contentExtent_ = items_.empty() ? 0.0f : position - gap;
        if (!IsSelectable(selected_)) {
            selected_ = NextSelectable(-1, 1);
        }
        RetargetScroll();
    }

    void ScrollStrip::SetViewport(float extent, float margin) {
        viewport_ = extent;
        margin_ = margin;
        RetargetScroll();
    }

    bool ScrollStrip::IsSelectable(int index) const {
        return index >= 0 && index < Count() && items_[index].selectable;
    }

    int ScrollStrip::NextSelectable(int from, int direction) const {
        for (int i = from + direction; i >= 0 && i < Count(); i += direction) {
            if (items_[i].selectable) {
                return i;
            }
        }
        return -1;
    }

    bool ScrollStrip::Select(int index) {
        if (!IsSelectable(index) || index == selected_) {
            return false;
        }
        selected_ = index;
        RetargetScroll();
        return true;
    }

    bool ScrollStrip::Step(int direction) {
        if (selected_ < 0) {
            return false;
        }
        return Select(NextSelectable(selected_, direction < 0 ? -1 : 1));
    }

    bool ScrollStrip::SelectFirst() { return Select(NextSelectable(-1, 1)); }

    bool ScrollStrip::SelectLast() { return Select(NextSelectable(Count(), -1)); }

    bool ScrollStrip::Page(int direction) {
        if (selected_ < 0) {
            return false;
        }
        // The furthest selectable item whose start is within one viewport of the
        // current one; always at least one step, so a page never does nothing
        // while there is somewhere to go.
        const int step = direction < 0 ? -1 : 1;
        const float limit = Start(selected_) + static_cast<float>(step) * viewport_;
        int best = -1;
        for (int i = NextSelectable(selected_, step); i >= 0;
             i = NextSelectable(i, step)) {
            const bool within = step > 0 ? Start(i) <= limit : Start(i) >= limit;
            if (!within && best >= 0) {
                break;
            }
            best = i;
            if (!within) {
                break;
            }
        }
        return Select(best);
    }

    float ScrollStrip::MaxScroll() const {
        return std::max(0.0f, contentExtent_ - viewport_);
    }

    void ScrollStrip::SetScroll(float offset) {
        scroll_.Snap(offset);
        RetargetScroll();
        if (viewport_ > 0.0f) {
            scroll_.Snap(scroll_.Target());
        }
    }

    void ScrollStrip::Advance(float deltaSeconds, const SpringParams &spring) {
        scroll_.Advance(deltaSeconds, spring);
    }

    void ScrollStrip::RetargetScroll() {
        if (viewport_ <= 0.0f) {
            return; // not laid out yet; the first SetViewport retargets
        }
        float target = scroll_.Target();
        if (selected_ >= 0) {
            // The scroll must be at least `lowest` to show the item's end and at
            // most `highest` to show its start, margins included.
            const float lowest = End(selected_) + margin_ - viewport_;
            const float highest = Start(selected_) - margin_;
            if (lowest > highest) {
                // The item and its margins do not fit: centre it.
                target = Start(selected_) + Extent(selected_) * 0.5f - viewport_ * 0.5f;
            } else {
                target = std::min(highest, std::max(lowest, target));
            }
            if (NextSelectable(selected_, -1) < 0) {
                target = 0.0f;
            } else if (NextSelectable(selected_, 1) < 0) {
                target = MaxScroll();
            }
        }
        target = std::min(MaxScroll(), std::max(0.0f, target));

        // A long jump starts close to where it is going, so it takes no longer
        // than a short one and nothing in between is ever shown or built.
        const float longestGlide = kLongestGlideInViewports * viewport_;
        const float distance = target - scroll_.Value();
        if (std::fabs(distance) > longestGlide) {
            scroll_.Snap(target - (distance > 0.0f ? longestGlide : -longestGlide));
        }
        scroll_.SetTarget(target);
    }

    StripRange ScrollStrip::VisibleRange(float overscan) const {
        const float low = Scroll() - overscan;
        const float high = Scroll() + viewport_ + overscan;
        const int count = Count();

        // The last item starting at or before `low`, then past it if it ends first.
        int first = static_cast<int>(
            std::upper_bound(starts_.begin(), starts_.end(), low) - starts_.begin()
        );
        first = std::max(0, first - 1);
        while (first < count && End(first) <= low) {
            ++first;
        }
        const int last = static_cast<int>(
            std::lower_bound(starts_.begin(), starts_.end(), high) - starts_.begin()
        );
        return { first, std::max(first, last) };
    }

}
