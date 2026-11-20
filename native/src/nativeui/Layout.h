// Layout: the containers that position other widgets. They draw nothing.
//
//   Row / Column   children one after another along an axis, with a gap; a
//                  child added with a flex factor shares the leftover space
//   Stack          children on top of each other, each given the whole area
//   Padding        one child, inset on each side
//   Align          one child at its own size, placed within the area given
//   SizedBox       a fixed width and/or height, around a child or empty
//
// Rules:
//  - Two passes (Widget.h): Measure asks, Arrange tells.
//  - Sizes here are plain numbers. The screen that builds the tree reads them
//    from its theme's spacing scale; containers never invent one.
//  - This is not a CSS engine: no wrapping, no baselines, no min/max content
//    negotiation. A flexible child takes its share; a fixed child takes what it
//    measured.
#pragma once

#include "nativeui/Widget.h"

#include <memory>
#include <optional>
#include <vector>

namespace nativeui {

    enum class Axis {
        Horizontal,
        Vertical
    };

    // Where children sit across the container's axis.
    enum class CrossAlign {
        Start,
        Center,
        End,
        Stretch
    };

    // Row and Column share this; use those two names.
    class Flex : public Widget {
    public:
        // Adds a child at the end. With `flex` > 0 the child gets that share of
        // the space left after the fixed children and gaps; with 0 it keeps the
        // size it measured.
        template <class T>
        T &Add(std::unique_ptr<T> child, float flex = 0.0f) {
            flex_.push_back(flex);
            return Adopt(std::move(child));
        }

    protected:
        Flex(Axis axis, float gap, CrossAlign cross)
            : axis_(axis), gap_(gap), cross_(cross) {}

        Size OnMeasure(const Context &ctx, const Constraints &constraints) override;
        void OnArrange(const Context &ctx, const Rect &bounds) override;

    private:
        float TotalGap() const;

        Axis axis_;
        float gap_;
        CrossAlign cross_;
        std::vector<float> flex_; // one per child
    };

    class Row final : public Flex {
    public:
        explicit Row(float gap = 0.0f, CrossAlign cross = CrossAlign::Center)
            : Flex(Axis::Horizontal, gap, cross) {}
    };

    class Column final : public Flex {
    public:
        explicit Column(float gap = 0.0f, CrossAlign cross = CrossAlign::Stretch)
            : Flex(Axis::Vertical, gap, cross) {}
    };

    class Stack final : public Widget {
    public:
        template <class T>
        T &Add(std::unique_ptr<T> child) {
            return Adopt(std::move(child));
        }

    protected:
        Size OnMeasure(const Context &ctx, const Constraints &constraints) override;
        void OnArrange(const Context &ctx, const Rect &bounds) override;
    };

    class Padding final : public Widget {
    public:
        Padding(const Insets &insets, std::unique_ptr<Widget> child);

    protected:
        Size OnMeasure(const Context &ctx, const Constraints &constraints) override;
        void OnArrange(const Context &ctx, const Rect &bounds) override;

    private:
        Insets insets_;
    };

    // Where a child sits in spare space: 0 = left/top, 0.5 = centre, 1 = right/bottom.
    struct Alignment {
        float x = 0.5f;
        float y = 0.5f;
    };

    class Align final : public Widget {
    public:
        Align(Alignment alignment, std::unique_ptr<Widget> child);

    protected:
        Size OnMeasure(const Context &ctx, const Constraints &constraints) override;
        void OnArrange(const Context &ctx, const Rect &bounds) override;

    private:
        Alignment alignment_;
    };

    class SizedBox final : public Widget {
    public:
        // An unset dimension is left to the child (or is zero with no child).
        SizedBox(
            std::optional<float> width,
            std::optional<float> height,
            std::unique_ptr<Widget> child = nullptr
        );

    protected:
        Size OnMeasure(const Context &ctx, const Constraints &constraints) override;
        void OnArrange(const Context &ctx, const Rect &bounds) override;

    private:
        std::optional<float> width_;
        std::optional<float> height_;
    };

}
