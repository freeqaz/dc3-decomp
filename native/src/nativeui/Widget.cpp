#include "nativeui/Widget.h"

#include <algorithm>

namespace nativeui {

    Constraints Constraints::Deflated(const Insets &insets) const {
        const auto shrink = [](float value, float by) {
            return std::max(0.0f, value - by);
        };
        return { shrink(minWidth, insets.Horizontal()),
                 shrink(maxWidth, insets.Horizontal()),
                 shrink(minHeight, insets.Vertical()),
                 shrink(maxHeight, insets.Vertical()) };
    }

    Size Constraints::Constrain(Size size) const {
        return { std::min(maxWidth, std::max(minWidth, size.width)),
                 std::min(maxHeight, std::max(minHeight, size.height)) };
    }

    Size Constraints::Biggest() const {
        return { HasBoundedWidth() ? maxWidth : minWidth,
                 HasBoundedHeight() ? maxHeight : minHeight };
    }

    void Widget::Advance(const Context &ctx, float deltaSeconds) {
        OnAdvance(ctx, deltaSeconds);
        for (const auto &child : children_) {
            child->Advance(ctx, deltaSeconds);
        }
    }

    Size Widget::Measure(const Context &ctx, const Constraints &constraints) {
        measuredSize_ = constraints.Constrain(OnMeasure(ctx, constraints));
        return measuredSize_;
    }

    void Widget::Arrange(const Context &ctx, const Rect &bounds) {
        bounds_ = bounds;
        OnArrange(ctx, bounds);
    }

    void Widget::Paint(const Context &ctx, Painter &painter) const {
        OnPaint(ctx, painter);
    }

    bool Widget::AtRest() const {
        if (!IsAtRest()) {
            return false;
        }
        return std::all_of(children_.begin(), children_.end(), [](const auto &child) {
            return child->AtRest();
        });
    }

    void Widget::OnPaint(const Context &ctx, Painter &painter) const {
        PaintChildren(ctx, painter);
    }

    void Widget::PaintChildren(const Context &ctx, Painter &painter) const {
        for (const auto &child : children_) {
            child->Paint(ctx, painter);
        }
    }

}
