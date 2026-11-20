#include "nativeui/TabStrip.h"

#include <algorithm>

namespace nativeui {

    TabStrip::TabStrip(std::vector<std::string> labels) : labels_(std::move(labels)) {}

    bool TabStrip::Select(int index) {
        if (index < 0 || index >= TabCount() || index == selected_) {
            return false;
        }
        selected_ = index;
        if (onChanged) {
            onChanged(selected_);
        }
        return true;
    }

    bool TabStrip::HandleEvent(const Event &event) {
        const ActionEvent *action = std::get_if<ActionEvent>(&event);
        const int count = TabCount();
        if (!action || count == 0) {
            return false;
        }
        switch (action->action) {
        case Action::NextTab:
            Select((selected_ + 1) % count);
            return true;
        case Action::PrevTab:
            Select((selected_ + count - 1) % count);
            return true;
        case Action::Left:
            return Select(selected_ - 1);
        case Action::Right:
            return Select(selected_ + 1);
        default:
            return false;
        }
    }

    std::vector<float> TabStrip::TabWidths(const Context &ctx) const {
        const TextStyle &style = ctx.theme.type.body;
        std::vector<float> widths;
        widths.reserve(labels_.size());
        for (const std::string &label : labels_) {
            widths.push_back(
                MeasureWidth(ctx.text, style, ApplyCase(style.textCase, label))
                + 2.0f * ctx.theme.space.md
            );
        }
        return widths;
    }

    Size TabStrip::OnMeasure(const Context &ctx, const Constraints &) {
        float width = 0.0f;
        for (const float tab : TabWidths(ctx)) {
            width += tab;
        }
        if (TabCount() > 1) {
            width += ctx.theme.space.sm * static_cast<float>(TabCount() - 1);
        }
        return { width, ctx.theme.metric.controlHeight };
    }

    void TabStrip::OnArrange(const Context &ctx, const Rect &bounds) {
        const std::vector<float> widths = TabWidths(ctx);
        tabRects_.clear();
        float x = bounds.x;
        for (const float width : widths) {
            tabRects_.push_back({ x, bounds.y, width, bounds.height });
            x += width + ctx.theme.space.sm;
        }
        if (selected_ < TabCount()) {
            indicatorLeft_.SetTarget(tabRects_[selected_].x - bounds.x);
            indicatorWidth_.SetTarget(tabRects_[selected_].width);
        }
        if (jumpOnNextLayout_) {
            indicatorLeft_.Snap(indicatorLeft_.Target());
            indicatorWidth_.Snap(indicatorWidth_.Target());
            jumpOnNextLayout_ = false;
        }
    }

    void TabStrip::OnAdvance(const Context &ctx, float deltaSeconds) {
        const Motion &motion = ctx.theme.motion;
        indicatorLeft_.Advance(deltaSeconds, motion.focusSpring);
        indicatorWidth_.Advance(deltaSeconds, motion.focusSpring);
        focusAmount_.SetTarget(HasFocus() ? 1.0f : 0.0f);
        focusAmount_.Advance(deltaSeconds, motion.fadeSpring);
    }

    void TabStrip::OnPaint(const Context &ctx, Painter &painter) const {
        const Theme &theme = ctx.theme;
        const TextStyle &style = theme.type.body;
        const Rect &bounds = Bounds();
        const FontMetrics metrics = ctx.text.Metrics(style.font, style.size);
        const float baseline = CenteredBaseline(metrics, bounds.y, bounds.height);

        for (std::size_t i = 0; i < tabRects_.size() && i < labels_.size(); ++i) {
            const Rect &tab = tabRects_[i];
            const FittedLine line =
                FitLine(ctx.text, style, labels_[i], tab.width, theme.type.ellipsis);
            const bool selected = static_cast<int>(i) == selected_;
            painter.DrawText(
                { tab.x + AlignOffset(TextAlign::Center, tab.width, line.width),
                  baseline },
                style,
                selected ? theme.color.textPrimary : theme.color.textSecondary,
                line.text
            );
        }

        if (TabCount() > 0) {
            const float focus = focusAmount_.Value();
            const float height = theme.metric.tabIndicatorHeight;
            const float glow = theme.metric.glowSoftness;
            const Rect bar { bounds.x + indicatorLeft_.Value(),
                             bounds.Bottom() - height,
                             indicatorWidth_.Value(),
                             height };
            painter.FillRoundRect(
                bar.Inflated(glow * 0.5f),
                (height + glow) * 0.5f,
                CornerColors::Solid(theme.color.accentGlow.Faded(focus)),
                glow
            );
            painter.FillRoundRect(
                bar,
                height * 0.5f,
                CornerColors::Solid(
                    Mix(theme.color.highlightIdle, theme.color.accent, focus)
                )
            );
        }
    }

    bool TabStrip::IsAtRest() const {
        return indicatorLeft_.AtRest() && indicatorWidth_.AtRest()
            && focusAmount_.AtRest();
    }

}
