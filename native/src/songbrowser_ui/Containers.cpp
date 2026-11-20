#include "songbrowser_ui/Containers.h"

#include <algorithm>

namespace songbrowser_ui {

    using namespace nativeui;

    // ---------------------------------------------------------------------------
    // EventScope
    // ---------------------------------------------------------------------------

    EventScope::EventScope(std::unique_ptr<Widget> child) { Adopt(std::move(child)); }

    Size EventScope::OnMeasure(const Context &ctx, const Constraints &constraints) {
        return Children().front()->Measure(ctx, constraints);
    }

    void EventScope::OnArrange(const Context &ctx, const Rect &bounds) {
        Children().front()->Arrange(ctx, bounds);
    }

    // ---------------------------------------------------------------------------
    // Reveal
    // ---------------------------------------------------------------------------

    Reveal::Reveal(std::unique_ptr<Widget> child) { Adopt(std::move(child)); }

    Size Reveal::OnMeasure(const Context &ctx, const Constraints &constraints) {
        Constraints loose = constraints.Loosened();
        loose.maxHeight = kUnbounded;
        const Size wanted = Children().front()->Measure(ctx, loose);
        childHeight_ = wanted.height;
        return { constraints.HasBoundedWidth() ? constraints.maxWidth : wanted.width,
                 childHeight_ * amount_.Value() };
    }

    void Reveal::OnArrange(const Context &ctx, const Rect &bounds) {
        // The child keeps its own height and its place; the reveal's bottom edge
        // moves over it.
        Children().front()->Arrange(
            ctx, { bounds.x, bounds.y, bounds.width, childHeight_ }
        );
    }

    void Reveal::OnAdvance(const Context &ctx, float deltaSeconds) {
        const float target = shown_ ? 1.0f : 0.0f;
        if (pending_) {
            amount_.Snap(target);
            pending_ = false;
        } else if (amount_.Target() != target) {
            amount_.Start(target, ctx.theme.motion.normal, ctx.theme.motion.easing);
        }
        amount_.Advance(deltaSeconds);
    }

    void Reveal::OnPaint(const Context &ctx, Painter &painter) const {
        const float amount = amount_.Value();
        if (amount <= 0.0f) {
            return;
        }
        if (amount >= 1.0f) {
            PaintChildren(ctx, painter);
            return;
        }
        // Part way: cut off at the moving edge. The cut is as wide as the view
        // so that a child drawing past its sides (a shelf's glow) still can.
        const Rect &bounds = Bounds();
        const float reach = ctx.theme.metric.glowSoftness;
        painter.PushClip(
            { bounds.x - reach, bounds.y - reach, bounds.width + 2.0f * reach,
              bounds.height + reach }
        );
        painter.PushLayer({ {}, 1.0f, amount });
        PaintChildren(ctx, painter);
        painter.PopLayer();
        painter.PopClip();
    }

    // ---------------------------------------------------------------------------
    // Notice
    // ---------------------------------------------------------------------------

    void Notice::Show(std::string headline, std::string detail) {
        shown_ = true;
        headline_ = std::move(headline);
        detail_ = std::move(detail);
    }

    Size Notice::OnMeasure(const Context &, const Constraints &constraints) {
        return constraints.Biggest();
    }

    void Notice::OnPaint(const Context &ctx, Painter &painter) const {
        if (!shown_) {
            return;
        }
        const Theme &theme = ctx.theme;
        const Rect &bounds = Bounds();
        const TextStyle &big = theme.type.title;
        const TextStyle &small = theme.type.caption;
        const FontMetrics bigMetrics = ctx.text.Metrics(big.font, big.size);
        const FontMetrics smallMetrics = ctx.text.Metrics(small.font, small.size);
        const float block =
            bigMetrics.lineHeight + theme.space.sm + smallMetrics.lineHeight;
        const float top = bounds.y + std::max(0.0f, (bounds.height - block) * 0.5f);

        const FittedLine headline =
            FitLine(ctx.text, big, headline_, bounds.width, theme.type.ellipsis);
        painter.DrawText(
            { bounds.x + AlignOffset(TextAlign::Center, bounds.width, headline.width),
              CenteredBaseline(bigMetrics, top, bigMetrics.lineHeight) },
            big,
            theme.color.textPrimary,
            headline.text
        );
        const FittedLine detail =
            FitLine(ctx.text, small, detail_, bounds.width, theme.type.ellipsis);
        painter.DrawText(
            { bounds.x + AlignOffset(TextAlign::Center, bounds.width, detail.width),
              CenteredBaseline(
                  smallMetrics,
                  top + bigMetrics.lineHeight + theme.space.sm,
                  smallMetrics.lineHeight
              ) },
            small,
            theme.color.textSecondary,
            detail.text
        );
    }

}
