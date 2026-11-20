// Widget: the base of the retained UI tree.
//
// A frame makes four passes over the tree, each a public method here that
// calls the matching protected hook and then recurses where that makes sense:
//
//   Advance(ctx, dt)        move animations forward by dt seconds
//   Measure(ctx, limits)    "how big do you want to be within these limits?"
//   Arrange(ctx, bounds)    "this is your rectangle; place your children"
//   Paint(ctx, painter)     emit display-list commands
//
// Rules:
//  - A parent owns its children (unique_ptr); a child knows its parent.
//    Widgets are not copyable or movable: pointers to them stay valid for as
//    long as their parent keeps them.
//  - Measure is a request, Arrange is the answer. A widget must cope with being
//    arranged at a size other than the one it asked for.
//  - Bounds are in the coordinate space of the nearest enclosing scroller's
//    content, which for most widgets is the root logical space.
//  - Paint is const: nothing changes while painting.
//  - Events change state only (HandleEvent has no Context); anything that
//    depends on the theme or on sizes is worked out in the passes above.
//  - AtRest() is true when nothing in the subtree is animating.
#pragma once

#include "nativeui/DisplayList.h"
#include "nativeui/Geometry.h"
#include "nativeui/Input.h"
#include "nativeui/Text.h"
#include "nativeui/Theme.h"

#include <limits>
#include <memory>
#include <vector>

namespace nativeui {

    // What every pass is given: the theme and the text seam. Passed down the tree
    // by reference; never stored by a widget.
    struct Context {
        const Theme &theme;
        const TextMeasurer &text;
    };

    inline constexpr float kUnbounded = std::numeric_limits<float>::infinity();

    // The limits a widget is measured within.
    struct Constraints {
        float minWidth = 0.0f;
        float maxWidth = kUnbounded;
        float minHeight = 0.0f;
        float maxHeight = kUnbounded;

        // Exactly this size.
        static Constraints Tight(Size size) {
            return { size.width, size.width, size.height, size.height };
        }
        // Anything up to this size.
        static Constraints Loose(Size size) {
            return { 0.0f, size.width, 0.0f, size.height };
        }

        bool HasBoundedWidth() const { return maxWidth < kUnbounded; }
        bool HasBoundedHeight() const { return maxHeight < kUnbounded; }
        // The same maximums with no minimums.
        Constraints Loosened() const { return { 0.0f, maxWidth, 0.0f, maxHeight }; }
        Constraints Deflated(const Insets &insets) const;
        // `size` pulled inside the limits.
        Size Constrain(Size size) const;
        // The largest allowed size; in an unbounded direction, the minimum.
        Size Biggest() const;
    };

    class Widget {
    public:
        Widget() = default;
        virtual ~Widget() = default;
        Widget(const Widget &) = delete;
        Widget &operator=(const Widget &) = delete;

        void Advance(const Context &ctx, float deltaSeconds);
        Size Measure(const Context &ctx, const Constraints &constraints);
        void Arrange(const Context &ctx, const Rect &bounds);
        void Paint(const Context &ctx, Painter &painter) const;
        bool AtRest() const;

        // Returns true if the event was consumed. Called on the focused widget
        // first, then on each ancestor in turn.
        virtual bool HandleEvent(const Event &) { return false; }
        // Whether focus may land here.
        virtual bool IsFocusable() const { return false; }
        // Whether this widget, when focused, takes typed text. The integration
        // layer turns keyboard text capture on exactly while the focused widget
        // says yes (Screen::WantsTextInput).
        virtual bool WantsTextInput() const { return false; }

        bool HasFocus() const { return hasFocus_; }
        const Rect &Bounds() const { return bounds_; }
        Size MeasuredSize() const { return measuredSize_; }
        Widget *Parent() const { return parent_; }
        const std::vector<std::unique_ptr<Widget> > &Children() const {
            return children_;
        }

    protected:
        // The size wanted within `constraints`; the result must satisfy them.
        virtual Size OnMeasure(const Context &ctx, const Constraints &constraints) = 0;
        // Bounds() is already set; arrange children here.
        virtual void OnArrange(const Context &, const Rect &) {}
        // This widget's own animations; children are advanced by the base.
        virtual void OnAdvance(const Context &, float) {}
        // Default: paint the children in order (later ones on top).
        virtual void OnPaint(const Context &ctx, Painter &painter) const;
        // Whether this widget's own animations have finished.
        virtual bool IsAtRest() const { return true; }

        // Takes ownership of `child` and returns it, still typed.
        template <class T>
        T &Adopt(std::unique_ptr<T> child) {
            T &ref = *child;
            ref.parent_ = this;
            children_.push_back(std::move(child));
            return ref;
        }
        void PaintChildren(const Context &ctx, Painter &painter) const;

    private:
        friend class FocusManager; // sets hasFocus_

        Widget *parent_ = nullptr;
        std::vector<std::unique_ptr<Widget> > children_;
        Rect bounds_;
        Size measuredSize_;
        bool hasFocus_ = false;
    };

}
