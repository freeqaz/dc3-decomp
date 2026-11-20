#include "nativeui/Layout.h"

#include <algorithm>

namespace nativeui {

    namespace {

        float Clamp(float value, float low, float high) {
            return std::min(high, std::max(low, value));
        }

    }

    // ---------------------------------------------------------------------------
    // Row / Column
    // ---------------------------------------------------------------------------

    float Flex::TotalGap() const {
        const std::size_t count = Children().size();
        return count > 1 ? gap_ * static_cast<float>(count - 1) : 0.0f;
    }

    Size Flex::OnMeasure(const Context &ctx, const Constraints &constraints) {
        const bool horizontal = axis_ == Axis::Horizontal;
        const float maxMain = horizontal ? constraints.maxWidth : constraints.maxHeight;
        const float maxCross = horizontal ? constraints.maxHeight : constraints.maxWidth;
        const bool bounded = maxMain < kUnbounded;
        const auto &children = Children();

        float used = TotalGap();
        float cross = 0.0f;
        // Measures one child within `mainLimit` and returns its main-axis size.
        const auto measure = [&](Widget &child, float mainLimit) {
            const Constraints limits = horizontal
                ? Constraints { 0.0f, mainLimit, 0.0f, maxCross }
                : Constraints { 0.0f, maxCross, 0.0f, mainLimit };
            const Size size = child.Measure(ctx, limits);
            cross = std::max(cross, horizontal ? size.height : size.width);
            return horizontal ? size.width : size.height;
        };

        // Fixed children first: they take what they ask for. With no limit along
        // the axis there is no leftover to share, so every child counts as fixed.
        float totalFlex = 0.0f;
        for (std::size_t i = 0; i < children.size(); ++i) {
            if (bounded && flex_[i] > 0.0f) {
                totalFlex += flex_[i];
            } else {
                used += measure(*children[i], std::max(0.0f, maxMain - used));
            }
        }
        if (totalFlex > 0.0f) {
            const float spare = std::max(0.0f, maxMain - used);
            for (std::size_t i = 0; i < children.size(); ++i) {
                if (flex_[i] > 0.0f) {
                    measure(*children[i], spare * flex_[i] / totalFlex);
                }
            }
            used = std::max(used, maxMain); // flexible children fill the axis
        }
        return horizontal ? Size { used, cross } : Size { cross, used };
    }

    void Flex::OnArrange(const Context &ctx, const Rect &bounds) {
        const bool horizontal = axis_ == Axis::Horizontal;
        const float mainSize = horizontal ? bounds.width : bounds.height;
        const float crossSize = horizontal ? bounds.height : bounds.width;
        const auto &children = Children();
        const auto mainOf = [&](const Widget &w) {
            return horizontal ? w.MeasuredSize().width : w.MeasuredSize().height;
        };
        const auto crossOf = [&](const Widget &w) {
            return horizontal ? w.MeasuredSize().height : w.MeasuredSize().width;
        };

        float fixed = TotalGap();
        float totalFlex = 0.0f;
        for (std::size_t i = 0; i < children.size(); ++i) {
            if (flex_[i] > 0.0f) {
                totalFlex += flex_[i];
            } else {
                fixed += mainOf(*children[i]);
            }
        }
        const float spare = std::max(0.0f, mainSize - fixed);

        float cursor = horizontal ? bounds.x : bounds.y;
        for (std::size_t i = 0; i < children.size(); ++i) {
            Widget &child = *children[i];
            const float main =
                flex_[i] > 0.0f ? spare * flex_[i] / totalFlex : mainOf(child);
            float cross = std::min(crossOf(child), crossSize);
            float crossOffset = 0.0f;
            switch (cross_) {
            case CrossAlign::Stretch:
                cross = crossSize;
                break;
            case CrossAlign::Center:
                crossOffset = (crossSize - cross) * 0.5f;
                break;
            case CrossAlign::End:
                crossOffset = crossSize - cross;
                break;
            case CrossAlign::Start:
                break;
            }
            child.Arrange(
                ctx,
                horizontal ? Rect { cursor, bounds.y + crossOffset, main, cross }
                           : Rect { bounds.x + crossOffset, cursor, cross, main }
            );
            cursor += main + gap_;
        }
    }

    // ---------------------------------------------------------------------------
    // Stack
    // ---------------------------------------------------------------------------

    Size Stack::OnMeasure(const Context &ctx, const Constraints &constraints) {
        Size biggest;
        for (const auto &child : Children()) {
            const Size size = child->Measure(ctx, constraints.Loosened());
            biggest.width = std::max(biggest.width, size.width);
            biggest.height = std::max(biggest.height, size.height);
        }
        return biggest;
    }

    void Stack::OnArrange(const Context &ctx, const Rect &bounds) {
        for (const auto &child : Children()) {
            child->Arrange(ctx, bounds);
        }
    }

    // ---------------------------------------------------------------------------
    // Padding
    // ---------------------------------------------------------------------------

    Padding::Padding(const Insets &insets, std::unique_ptr<Widget> child)
        : insets_(insets) {
        Adopt(std::move(child));
    }

    Size Padding::OnMeasure(const Context &ctx, const Constraints &constraints) {
        const Size inner =
            Children().front()->Measure(ctx, constraints.Deflated(insets_));
        return { inner.width + insets_.Horizontal(), inner.height + insets_.Vertical() };
    }

    void Padding::OnArrange(const Context &ctx, const Rect &bounds) {
        Children().front()->Arrange(ctx, bounds.Deflated(insets_));
    }

    // ---------------------------------------------------------------------------
    // Align
    // ---------------------------------------------------------------------------

    Align::Align(Alignment alignment, std::unique_ptr<Widget> child)
        : alignment_(alignment) {
        Adopt(std::move(child));
    }

    Size Align::OnMeasure(const Context &ctx, const Constraints &constraints) {
        return Children().front()->Measure(ctx, constraints.Loosened());
    }

    void Align::OnArrange(const Context &ctx, const Rect &bounds) {
        Widget &child = *Children().front();
        const float width = std::min(child.MeasuredSize().width, bounds.width);
        const float height = std::min(child.MeasuredSize().height, bounds.height);
        child.Arrange(
            ctx,
            { bounds.x + (bounds.width - width) * alignment_.x,
              bounds.y + (bounds.height - height) * alignment_.y,
              width,
              height }
        );
    }

    // ---------------------------------------------------------------------------
    // SizedBox
    // ---------------------------------------------------------------------------

    SizedBox::SizedBox(
        std::optional<float> width,
        std::optional<float> height,
        std::unique_ptr<Widget> child
    )
        : width_(width), height_(height) {
        if (child) {
            Adopt(std::move(child));
        }
    }

    Size SizedBox::OnMeasure(const Context &ctx, const Constraints &constraints) {
        Constraints limits = constraints;
        if (width_) {
            limits.minWidth = limits.maxWidth =
                Clamp(*width_, constraints.minWidth, constraints.maxWidth);
        }
        if (height_) {
            limits.minHeight = limits.maxHeight =
                Clamp(*height_, constraints.minHeight, constraints.maxHeight);
        }
        if (Children().empty()) {
            return { limits.minWidth, limits.minHeight };
        }
        return Children().front()->Measure(ctx, limits);
    }

    void SizedBox::OnArrange(const Context &ctx, const Rect &bounds) {
        if (!Children().empty()) {
            Children().front()->Arrange(ctx, bounds);
        }
    }

}
