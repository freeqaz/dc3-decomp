#include "nativeui/Shelf.h"

#include "nativeui/Decor.h"

#include <algorithm>
#include <cmath>

namespace nativeui {

    namespace {

        // Cards are kept alive this many viewport-widths beyond each edge.
        constexpr float kOverscanViewports = 0.5f;

    }

    Shelf::Shelf(BuildCard build) : build_(std::move(build)) {}

    void Shelf::SetCardCount(int count) {
        SetItems(count, cardWidth_, gap_);
        cards_.clear();
        jumpOnNextLayout_ = true;
    }

    void Shelf::SetItems(int count, float cardWidth, float gap) {
        cardWidth_ = cardWidth;
        gap_ = gap;
        strip_.SetItems(
            std::vector<StripItem>(
                static_cast<std::size_t>(std::max(0, count)), { cardWidth, true }
            ),
            gap
        );
        selectionGlide_.SetTarget(static_cast<float>(std::max(0, strip_.Selected())));
    }

    bool Shelf::Select(int index) { return Changed(strip_.Select(index)); }

    bool Shelf::Changed(bool selectionChanged) {
        if (selectionChanged) {
            selectionGlide_.SetTarget(static_cast<float>(strip_.Selected()));
            if (onSelectionChanged) {
                onSelectionChanged(strip_.Selected());
            }
        }
        return selectionChanged;
    }

    bool Shelf::HandleEvent(const Event &event) {
        const ActionEvent *action = std::get_if<ActionEvent>(&event);
        if (!action || strip_.Selected() < 0) {
            return false;
        }
        switch (action->action) {
        case Action::Left:
            return Changed(strip_.Step(-1));
        case Action::Right:
            return Changed(strip_.Step(1));
        case Action::Home:
            Changed(strip_.SelectFirst());
            return true;
        case Action::End:
            Changed(strip_.SelectLast());
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

    float Shelf::Emphasis(int index) const {
        if (strip_.Selected() < 0) {
            return 0.0f;
        }
        return std::max(
            0.0f, 1.0f - std::fabs(selectionGlide_.Value() - static_cast<float>(index))
        );
    }

    Size Shelf::OnMeasure(const Context &ctx, const Constraints &constraints) {
        const Metrics &metric = ctx.theme.metric;
        const float count = static_cast<float>(CardCount());
        const float content = count * metric.shelfCardWidth
            + std::max(0.0f, count - 1.0f) * ctx.theme.space.md;
        return { constraints.HasBoundedWidth() ? constraints.maxWidth : content,
                 metric.shelfCardHeight * metric.shelfFocusScale };
    }

    void Shelf::OnArrange(const Context &ctx, const Rect &bounds) {
        const Metrics &metric = ctx.theme.metric;
        if (cardWidth_ != metric.shelfCardWidth || gap_ != ctx.theme.space.md) {
            SetItems(CardCount(), metric.shelfCardWidth, ctx.theme.space.md);
        }
        strip_.SetViewport(bounds.width, ctx.theme.space.lg);
        if (jumpOnNextLayout_) {
            strip_.SnapScroll();
            selectionGlide_.Snap(selectionGlide_.Target());
            jumpOnNextLayout_ = false;
        }

        const StripRange wanted = strip_.VisibleRange(bounds.width * kOverscanViewports);
        for (auto it = cards_.begin(); it != cards_.end();) {
            if (it->first < wanted.first || it->first >= wanted.last) {
                it = cards_.erase(it);
            } else {
                ++it;
            }
        }
        const Size cardSize { metric.shelfCardWidth, metric.shelfCardHeight };
        for (int i = wanted.first; i < wanted.last; ++i) {
            auto found = cards_.find(i);
            if (found == cards_.end()) {
                found = cards_.emplace(i, build_(i)).first;
            }
            if (Widget *card = found->second.get()) {
                card->Measure(ctx, Constraints::Tight(cardSize));
                card->Arrange(
                    ctx, { strip_.Start(i), 0.0f, cardSize.width, cardSize.height }
                );
            }
        }
    }

    void Shelf::OnAdvance(const Context &ctx, float deltaSeconds) {
        const Motion &motion = ctx.theme.motion;
        strip_.Advance(deltaSeconds, motion.scrollSpring);
        selectionGlide_.Advance(deltaSeconds, motion.fadeSpring);
        focusAmount_.SetTarget(HasFocus() ? 1.0f : 0.0f);
        focusAmount_.Advance(deltaSeconds, motion.fadeSpring);
        for (const auto &entry : cards_) {
            if (entry.second) {
                entry.second->Advance(ctx, deltaSeconds);
            }
        }
    }

    void Shelf::PaintCard(
        const Context &ctx, Painter &painter, int index, const Widget &card
    ) const {
        const Theme &theme = ctx.theme;
        const Metrics &metric = theme.metric;
        const float emphasis = Emphasis(index);
        const float focus = focusAmount_.Value();
        // The selected card grows only while the shelf has focus.
        const float lit = emphasis * focus;
        const float scale = 1.0f + (metric.shelfFocusScale - 1.0f) * lit;
        const Rect rect {
            strip_.Start(index), 0.0f, metric.shelfCardWidth, metric.shelfCardHeight
        };
        const Vec2 centre = rect.Center();

        // Scale about the card's own centre.
        painter.PushLayer({ centre * (1.0f - scale), scale, 1.0f });
        // A glow and an accent ring while the shelf has focus; a quiet outline
        // while it only remembers its selection (the selection rule, Decor.h).
        PaintFocusGlow(painter, theme, rect, theme.radius.medium, lit);
        card.Paint(ctx, painter);
        PaintSelectionRing(painter, theme, rect, theme.radius.medium, focus, emphasis);
        painter.PopLayer();
    }

    namespace {
        // Less than this much of the row beyond an edge counts as none.
        constexpr float kScrolledOff = 0.5f;
    }

    void Shelf::OnPaint(const Context &ctx, Painter &painter) const {
        const Rect &bounds = Bounds();
        const Metrics &metric = ctx.theme.metric;
        const float scroll = strip_.Scroll();
        const float headroom = (bounds.height - metric.shelfCardHeight) * 0.5f;

        // Clip to the shelf plus the glow's reach above and below, and to the
        // sides only at an end of the row: there the first or last card sits at
        // the very edge and still has to grow and glow. Where the row goes on,
        // cards stop at the shelf's own edge, under the fade, and never reach
        // into whatever frames the shelf.
        const float glow = metric.glowSoftness;
        const float bleedLeft = scroll > kScrolledOff ? 0.0f : glow;
        const float bleedRight = strip_.MaxScroll() - scroll > kScrolledOff ? 0.0f : glow;
        const Rect lane { bounds.x - bleedLeft,
                          bounds.y - glow,
                          bounds.width + bleedLeft + bleedRight,
                          bounds.height + 2.0f * glow };
        painter.PushClip(lane);
        painter.PushLayer({ { bounds.x - scroll, bounds.y + headroom }, 1.0f, 1.0f });

        // The most emphasised card is painted last so that it overlaps its neighbours.
        int front = -1;
        float frontEmphasis = 0.0f;
        for (const auto &entry : cards_) {
            const float emphasis = Emphasis(entry.first);
            if (emphasis > frontEmphasis) {
                frontEmphasis = emphasis;
                front = entry.first;
            }
        }
        for (const auto &entry : cards_) {
            const int index = entry.first;
            const bool inView =
                strip_.End(index) > scroll && strip_.Start(index) < scroll + bounds.width;
            if (entry.second && inView && index != front) {
                PaintCard(ctx, painter, index, *entry.second);
            }
        }
        const auto frontCard = cards_.find(front);
        if (frontCard != cards_.end() && frontCard->second) {
            PaintCard(ctx, painter, front, *frontCard->second);
        }

        painter.PopLayer();

        // Cards fade out toward an edge the shelf goes on past, so a row that
        // holds more than fits says so.
        const float reach = ctx.theme.metric.edgeFade;
        if (reach > 0.0f) {
            PaintEdgeFade(painter, ctx.theme, lane, Edge::Left, scroll / reach);
            PaintEdgeFade(
                painter, ctx.theme, lane, Edge::Right, (strip_.MaxScroll() - scroll) / reach
            );
        }
        painter.PopClip();
    }

    bool Shelf::IsAtRest() const {
        if (!strip_.AtRest() || !selectionGlide_.AtRest() || !focusAmount_.AtRest()) {
            return false;
        }
        return std::all_of(cards_.begin(), cards_.end(), [](const auto &entry) {
            return !entry.second || entry.second->AtRest();
        });
    }

}
