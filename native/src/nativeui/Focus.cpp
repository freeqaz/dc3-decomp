#include "nativeui/Focus.h"

#include <algorithm>
#include <limits>
#include <vector>

namespace nativeui {

    namespace {

        void CollectFocusable(Widget &widget, std::vector<Widget *> &out) {
            if (widget.IsFocusable()) {
                out.push_back(&widget);
            }
            for (const auto &child : widget.Children()) {
                CollectFocusable(*child, out);
            }
        }

        // Distance between two 1D spans; zero when they overlap.
        float SpanGap(float aStart, float aEnd, float bStart, float bEnd) {
            return std::max(0.0f, std::max(aStart, bStart) - std::min(aEnd, bEnd));
        }

        // Being off to the side counts double: a widget straight ahead beats a nearer
        // one that is diagonal.
        constexpr float kSidewaysPenalty = 2.0f;

    }

    Widget *FindFocusNeighbour(Widget &root, const Widget &from, Direction direction) {
        std::vector<Widget *> candidates;
        CollectFocusable(root, candidates);

        const Rect a = from.Bounds();
        Widget *best = nullptr;
        float bestScore = std::numeric_limits<float>::infinity();
        for (Widget *candidate : candidates) {
            const Rect b = candidate->Bounds();
            if (candidate == &from || b.IsEmpty()) {
                continue;
            }
            float ahead = 0.0f; // how far the candidate's centre is past our edge on that
                                // side
            float gap = 0.0f; // empty space between the two along the direction
            float sideways = 0.0f; // how far apart they are across it
            switch (direction) {
            case Direction::Up:
                ahead = a.Top() - b.Center().y;
                gap = a.Top() - b.Bottom();
                sideways = SpanGap(a.Left(), a.Right(), b.Left(), b.Right());
                break;
            case Direction::Down:
                ahead = b.Center().y - a.Bottom();
                gap = b.Top() - a.Bottom();
                sideways = SpanGap(a.Left(), a.Right(), b.Left(), b.Right());
                break;
            case Direction::Left:
                ahead = a.Left() - b.Center().x;
                gap = a.Left() - b.Right();
                sideways = SpanGap(a.Top(), a.Bottom(), b.Top(), b.Bottom());
                break;
            case Direction::Right:
                ahead = b.Center().x - a.Right();
                gap = b.Left() - a.Right();
                sideways = SpanGap(a.Top(), a.Bottom(), b.Top(), b.Bottom());
                break;
            }
            if (ahead <= 0.0f) {
                continue;
            }
            const float score = std::max(0.0f, gap) + kSidewaysPenalty * sideways;
            if (score < bestScore) {
                bestScore = score;
                best = candidate;
            }
        }
        return best;
    }

    Widget *FirstFocusable(Widget &root) {
        std::vector<Widget *> candidates;
        CollectFocusable(root, candidates);
        return candidates.empty() ? nullptr : candidates.front();
    }

    void FocusManager::SetFocus(Widget *widget) {
        if (widget && !widget->IsFocusable()) {
            return;
        }
        if (focused_) {
            focused_->hasFocus_ = false;
        }
        focused_ = widget;
        if (focused_) {
            focused_->hasFocus_ = true;
        }
    }

    bool FocusManager::Dispatch(const Event &event) {
        for (Widget *widget = focused_; widget; widget = widget->Parent()) {
            if (widget->HandleEvent(event)) {
                return true;
            }
        }
        return false;
    }

    bool FocusManager::MoveFocus(Widget &root, Direction direction) {
        if (!focused_) {
            return false;
        }
        Widget *next = FindFocusNeighbour(root, *focused_, direction);
        if (!next) {
            return false;
        }
        SetFocus(next);
        return true;
    }

}
