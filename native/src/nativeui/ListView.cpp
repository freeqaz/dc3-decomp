#include "nativeui/ListView.h"

#include "nativeui/Decor.h"

#include <algorithm>

namespace nativeui {

    namespace {

        // Rows are kept alive this many viewport-heights beyond each edge, so a row is
        // ready before a scroll brings it into view.
        constexpr float kOverscanViewports = 0.5f;

    }

    ListView::ListView(DescribeRow describe, BuildRow build)
        : describe_(std::move(describe)), build_(std::move(build)) {}

    void ListView::SetRowCount(int count) {
        std::vector<StripItem> items;
        items.reserve(static_cast<std::size_t>(std::max(0, count)));
        for (int i = 0; i < count; ++i) {
            const ListRowInfo info = describe_(i);
            items.push_back({ info.height, info.selectable });
        }
        strip_.SetItems(std::move(items), 0.0f);
        rows_.clear();
        jumpOnNextLayout_ = true;
        AimHighlight();
    }

    bool ListView::Select(int index) { return Changed(strip_.Select(index)); }

    bool ListView::SelectFirst() { return Changed(strip_.SelectFirst()); }

    bool ListView::Changed(bool selectionChanged) {
        if (selectionChanged) {
            AimHighlight();
            if (onSelectionChanged) {
                onSelectionChanged(strip_.Selected());
            }
        }
        return selectionChanged;
    }

    void ListView::AimHighlight() {
        const int selected = strip_.Selected();
        if (selected >= 0) {
            highlightTop_.SetTarget(strip_.Start(selected));
            highlightHeight_.SetTarget(strip_.Extent(selected));
        }
    }

    bool ListView::HandleEvent(const Event &event) {
        const ActionEvent *action = std::get_if<ActionEvent>(&event);
        if (!action || strip_.Selected() < 0) {
            return false;
        }
        switch (action->action) {
        case Action::Up:
            return Changed(strip_.Step(-1));
        case Action::Down:
            return Changed(strip_.Step(1));
        case Action::Home:
            Changed(strip_.SelectFirst());
            return true;
        case Action::End:
            Changed(strip_.SelectLast());
            return true;
        case Action::PageUp:
            Changed(strip_.Page(-1));
            return true;
        case Action::PageDown:
            Changed(strip_.Page(1));
            return true;
        case Action::Accept:
            if (onActivated) {
                onActivated(strip_.Selected());
            }
            return true;
        default:
            return false;
        }
    }

    Rect ListView::HighlightRect() const {
        const Rect &bounds = Bounds();
        return { bounds.x,
                 bounds.y + highlightTop_.Value() - strip_.Scroll(),
                 bounds.width,
                 highlightHeight_.Value() };
    }

    Size ListView::OnMeasure(const Context &, const Constraints &constraints) {
        // A list takes the room it is given; with no limit it is as tall as its rows.
        return { constraints.Biggest().width,
                 constraints.HasBoundedHeight() ? constraints.maxHeight
                                                : strip_.ContentExtent() };
    }

    void ListView::OnArrange(const Context &ctx, const Rect &bounds) {
        strip_.SetViewport(bounds.height, ctx.theme.metric.listScrollMargin);
        AimHighlight();
        if (jumpOnNextLayout_) {
            strip_.SnapScroll();
            highlightTop_.Snap(highlightTop_.Target());
            highlightHeight_.Snap(highlightHeight_.Target());
            jumpOnNextLayout_ = false;
        }
        RealizeRows(ctx, bounds);
    }

    void ListView::RealizeRows(const Context &ctx, const Rect &bounds) {
        const StripRange wanted = strip_.VisibleRange(bounds.height * kOverscanViewports);

        for (auto it = rows_.begin(); it != rows_.end();) {
            if (it->first < wanted.first || it->first >= wanted.last) {
                it = rows_.erase(it);
            } else {
                ++it;
            }
        }
        for (int i = wanted.first; i < wanted.last; ++i) {
            auto found = rows_.find(i);
            if (found == rows_.end()) {
                found = rows_.emplace(i, build_(i)).first;
                ++totalRowsBuilt_;
            }
            if (Widget *row = found->second.get()) {
                const Size size { bounds.width, strip_.Extent(i) };
                row->Measure(ctx, Constraints::Tight(size));
                row->Arrange(ctx, { 0.0f, strip_.Start(i), size.width, size.height });
            }
        }
    }

    void ListView::OnAdvance(const Context &ctx, float deltaSeconds) {
        const Motion &motion = ctx.theme.motion;
        strip_.Advance(deltaSeconds, motion.scrollSpring);
        highlightTop_.Advance(deltaSeconds, motion.focusSpring);
        highlightHeight_.Advance(deltaSeconds, motion.focusSpring);
        focusAmount_.SetTarget(HasFocus() ? 1.0f : 0.0f);
        focusAmount_.Advance(deltaSeconds, motion.fadeSpring);
        for (const auto &entry : rows_) {
            if (entry.second) {
                entry.second->Advance(ctx, deltaSeconds);
            }
        }
    }

    void ListView::OnPaint(const Context &ctx, Painter &painter) const {
        const Rect &bounds = Bounds();
        const Theme &theme = ctx.theme;
        const float scroll = strip_.Scroll();

        painter.PushClip(bounds);
        painter.PushLayer({ { bounds.x, bounds.y - scroll }, 1.0f, 1.0f });

        if (strip_.Selected() >= 0) {
            // Filled only while this list has focus; an outline otherwise (Decor.h).
            PaintSelectionBar(
                painter,
                theme,
                { 0.0f, highlightTop_.Value(), bounds.width, highlightHeight_.Value() },
                theme.radius.small,
                focusAmount_.Value()
            );
        }

        for (const auto &entry : rows_) {
            const int index = entry.first;
            const bool inView = strip_.End(index) > scroll
                && strip_.Start(index) < scroll + bounds.height;
            if (entry.second && inView) {
                entry.second->Paint(ctx, painter);
            }
        }

        painter.PopLayer();

        // Rows fade out toward an edge the list goes on past, by as much as it
        // goes on: nothing at the very top or the very end.
        const float reach = theme.metric.edgeFade;
        if (reach > 0.0f) {
            PaintEdgeFade(painter, theme, bounds, Edge::Top, scroll / reach);
            PaintEdgeFade(
                painter, theme, bounds, Edge::Bottom, (strip_.MaxScroll() - scroll) / reach
            );
        }
        painter.PopClip();
    }

    bool ListView::IsAtRest() const {
        if (!strip_.AtRest() || !highlightTop_.AtRest() || !highlightHeight_.AtRest()
            || !focusAmount_.AtRest()) {
            return false;
        }
        return std::all_of(rows_.begin(), rows_.end(), [](const auto &entry) {
            return !entry.second || entry.second->AtRest();
        });
    }

}
