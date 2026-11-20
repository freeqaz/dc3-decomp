#include "songbrowser_ui/SongWidgets.h"

#include <algorithm>

namespace songbrowser_ui {

    using namespace nativeui;

    // ---------------------------------------------------------------------------
    // ArtImage
    // ---------------------------------------------------------------------------

    ArtImage::ArtImage(ArtSource source, RadiusToken radius)
        : source_(std::move(source)) {
        image_ = &Adopt(
            std::make_unique<Image>(
                source_ ? source_() : ImageHandle {}, &Palette::imagePlaceholder, radius
            )
        );
    }

    Size ArtImage::OnMeasure(const Context &ctx, const Constraints &constraints) {
        return image_->Measure(ctx, constraints);
    }

    void ArtImage::OnArrange(const Context &ctx, const Rect &bounds) {
        image_->Arrange(ctx, bounds);
    }

    void ArtImage::OnAdvance(const Context &, float) {
        if (source_) {
            const ImageHandle now = source_();
            if (now != image_->GetImage()) {
                image_->SetImage(now);
            }
        }
    }

    // ---------------------------------------------------------------------------
    // SegmentMeter
    // ---------------------------------------------------------------------------

    SegmentMeter::SegmentMeter(
        int cells, int lit, ColorToken litColor, ColorToken unlitColor
    )
        : cells_(std::max(1, cells)), lit_(lit), litColor_(litColor),
          unlitColor_(unlitColor) {}

    Size SegmentMeter::OnMeasure(const Context &ctx, const Constraints &constraints) {
        const Metrics &metric = ctx.theme.metric;
        const float natural = static_cast<float>(cells_) * metric.meterCellWidth
            + static_cast<float>(cells_ - 1) * metric.meterCellGap;
        return { constraints.HasBoundedWidth() ? constraints.maxWidth : natural,
                 metric.meterCellHeight };
    }

    void SegmentMeter::OnPaint(const Context &ctx, Painter &painter) const {
        const Theme &theme = ctx.theme;
        const Rect &bounds = Bounds();
        const float gap = theme.metric.meterCellGap;
        const float cell =
            (bounds.width - gap * static_cast<float>(cells_ - 1)) / static_cast<float>(cells_);
        const float height = std::min(bounds.height, theme.metric.meterCellHeight);
        const float top = bounds.y + (bounds.height - height) * 0.5f;
        for (int i = 0; i < cells_; ++i) {
            painter.FillRoundRect(
                { bounds.x + static_cast<float>(i) * (cell + gap), top, cell, height },
                theme.radius.none,
                CornerColors::Solid(theme.color.*(i < lit_ ? litColor_ : unlitColor_))
            );
        }
    }

    // ---------------------------------------------------------------------------
    // StarMarks
    // ---------------------------------------------------------------------------

    StarMarks::StarMarks(int stars) : stars_(stars) {}

    float StarMarks::WidthFor(const Theme &theme) {
        return static_cast<float>(kMaxStars) * theme.metric.starSize
            + static_cast<float>(kMaxStars - 1) * theme.metric.starGap;
    }

    Size StarMarks::OnMeasure(const Context &ctx, const Constraints &) {
        return { WidthFor(ctx.theme), ctx.theme.metric.starSize };
    }

    void StarMarks::OnPaint(const Context &ctx, Painter &painter) const {
        const Theme &theme = ctx.theme;
        const Rect &bounds = Bounds();
        const float size = theme.metric.starSize;
        const float top = bounds.y + (bounds.height - size) * 0.5f;
        const bool gold = stars_ > kMaxStars;
        for (int i = 0; i < kMaxStars; ++i) {
            const Rect mark { bounds.x + static_cast<float>(i) * (size + theme.metric.starGap),
                              top,
                              size,
                              size };
            if (i < stars_) {
                if (gold) {
                    painter.FillRoundRect(
                        mark.Inflated(size * 0.5f),
                        size,
                        CornerColors::Solid(theme.color.star.Faded(theme.color.accentGlow.a)),
                        size
                    );
                }
                painter.FillRoundRect(mark, size * 0.5f, CornerColors::Solid(theme.color.star));
            } else {
                painter.StrokeRoundRect(
                    mark,
                    size * 0.5f,
                    CornerColors::Solid(theme.color.textDisabled),
                    theme.metric.borderWidth
                );
            }
        }
    }

    // ---------------------------------------------------------------------------
    // SongRow
    // ---------------------------------------------------------------------------

    SongRow::SongRow(SongRowModel model, ArtSource art, std::function<bool()> lit)
        : model_(std::move(model)), isLit_(std::move(lit)) {
        art_ = &Adopt(std::make_unique<ArtImage>(std::move(art), &Radii::small));
        if (model_.difficultyTier >= 0) {
            meter_ = &Adopt(
                std::make_unique<SegmentMeter>(
                    kDifficultyTiers,
                    model_.difficultyTier + 1,
                    &Palette::textPrimary,
                    &Palette::textDisabled
                )
            );
        }
        if (model_.played) {
            stars_ = &Adopt(std::make_unique<StarMarks>(model_.stars));
        }
    }

    Size SongRow::OnMeasure(const Context &ctx, const Constraints &constraints) {
        return { constraints.Biggest().width, ctx.theme.metric.listRowHeight };
    }

    void SongRow::OnArrange(const Context &ctx, const Rect &bounds) {
        const Theme &theme = ctx.theme;
        const Spacing &space = theme.space;
        const Metrics &metric = theme.metric;
        const Rect inner = bounds.Deflated({ space.sm, space.xs, space.md, space.xs });

        // Left: the art, a square as tall as the row allows.
        const float thumb = inner.height;
        art_->Measure(ctx, Constraints::Tight({ thumb, thumb }));
        art_->Arrange(ctx, { inner.x, inner.y, thumb, thumb });

        // Right, from the edge inwards: stars, meter, the second text column.
        // Every column keeps its place whether or not this row has the thing.
        float right = inner.Right();
        const float starsWidth = StarMarks::WidthFor(theme);
        if (stars_) {
            stars_->Measure(ctx, Constraints::Tight({ starsWidth, metric.starSize }));
            stars_->Arrange(
                ctx,
                { right - starsWidth,
                  inner.y + (inner.height - metric.starSize) * 0.5f,
                  starsWidth,
                  metric.starSize }
            );
        }
        right -= starsWidth + space.md;
        const float meterWidth = static_cast<float>(kDifficultyTiers) * metric.meterCellWidth
            + static_cast<float>(kDifficultyTiers - 1) * metric.meterCellGap;
        if (meter_) {
            meter_->Measure(ctx, Constraints::Tight({ meterWidth, metric.meterCellHeight }));
            meter_->Arrange(
                ctx,
                { right - meterWidth,
                  inner.y + (inner.height - metric.meterCellHeight) * 0.5f,
                  meterWidth,
                  metric.meterCellHeight }
            );
        }
        right -= meterWidth + space.md;
        const float secondaryWidth = bounds.width * metric.listSecondaryShare;
        secondaryBox_ = { right - secondaryWidth, inner.y, secondaryWidth, inner.height };
        right -= secondaryWidth + space.md;
        const float titleLeft = inner.x + thumb + space.md;
        titleBox_ = { titleLeft, inner.y, std::max(0.0f, right - titleLeft), inner.height };

        if (fittedWidth_ != bounds.width) {
            fittedWidth_ = bounds.width;
            title_ = FitMatchLine(
                ctx.text,
                theme.type.body,
                model_.title,
                model_.titleMatch,
                titleBox_.width,
                theme.type.ellipsis
            );
            secondary_ = FitMatchLine(
                ctx.text,
                theme.type.caption,
                model_.secondary,
                model_.secondaryMatch,
                secondaryBox_.width,
                theme.type.ellipsis
            );
        }
    }

    void SongRow::OnAdvance(const Context &ctx, float deltaSeconds) {
        lit_.SetTarget(isLit_ && isLit_() ? 1.0f : 0.0f);
        lit_.Advance(deltaSeconds, ctx.theme.motion.fadeSpring);
    }

    void SongRow::OnPaint(const Context &ctx, Painter &painter) const {
        PaintChildren(ctx, painter);

        const Theme &theme = ctx.theme;
        const Palette &color = theme.color;
        const float lit = lit_.Value();
        const bool matched = !model_.titleMatch.empty() || !model_.secondaryMatch.empty();
        // On the accent bar the match is the primary colour and the rest steps back.
        const Color highlight = Mix(color.accent, color.textPrimary, lit);
        const Color titleColor =
            matched ? Mix(color.textPrimary, color.textSecondary, lit) : color.textPrimary;

        const TextStyle &titleStyle = theme.type.body;
        PaintMatchLine(
            painter,
            { titleBox_.x,
              CenteredBaseline(
                  ctx.text.Metrics(titleStyle.font, titleStyle.size),
                  titleBox_.y,
                  titleBox_.height
              ) },
            titleStyle,
            title_,
            titleColor,
            highlight
        );
        const TextStyle &secondaryStyle = theme.type.caption;
        PaintMatchLine(
            painter,
            { secondaryBox_.x,
              CenteredBaseline(
                  ctx.text.Metrics(secondaryStyle.font, secondaryStyle.size),
                  secondaryBox_.y,
                  secondaryBox_.height
              ) },
            secondaryStyle,
            secondary_,
            color.textSecondary,
            highlight
        );
    }

    // ---------------------------------------------------------------------------
    // SectionHeading
    // ---------------------------------------------------------------------------

    SectionHeading::SectionHeading(std::string title, std::string count)
        : title_(std::move(title)), count_(std::move(count)) {}

    Size SectionHeading::OnMeasure(const Context &ctx, const Constraints &constraints) {
        return { constraints.Biggest().width, ctx.theme.metric.listHeaderHeight };
    }

    void SectionHeading::OnPaint(const Context &ctx, Painter &painter) const {
        const Theme &theme = ctx.theme;
        const TextStyle &style = theme.type.caption;
        const Rect inner = Bounds().Deflated(Insets::Symmetric(theme.space.sm, 0.0f));
        const float rule = theme.metric.borderWidth;
        const float baseline = CenteredBaseline(
            ctx.text.Metrics(style.font, style.size), inner.y, inner.height - rule
        );

        const std::string count = ApplyCase(style.textCase, count_);
        const float countWidth = MeasureWidth(ctx.text, style, count);
        const FittedLine title = FitLine(
            ctx.text,
            style,
            title_,
            std::max(0.0f, inner.width - countWidth - theme.space.md),
            theme.type.ellipsis
        );
        painter.DrawText({ inner.x, baseline }, style, theme.color.accent, title.text);
        painter.DrawText(
            { inner.Right() - countWidth, baseline }, style, theme.color.textSecondary, count
        );
        // The rule under it: the game's streak, blue running out to magenta.
        painter.FillRoundRect(
            { inner.x, Bounds().Bottom() - rule - theme.space.xs, inner.width, rule },
            theme.radius.none,
            CornerColors::Horizontal(theme.color.accent, theme.color.streakMagenta)
        );
    }

    // ---------------------------------------------------------------------------
    // RecentCard
    // ---------------------------------------------------------------------------

    RecentCard::RecentCard(std::string title, std::string artist, ArtSource art)
        : title_(std::move(title)), artist_(std::move(artist)) {
        Adopt(std::make_unique<ArtImage>(std::move(art), &Radii::medium));
    }

    Size RecentCard::OnMeasure(const Context &ctx, const Constraints &constraints) {
        const Size size = constraints.Biggest();
        Children().front()->Measure(ctx, Constraints::Tight(size));
        return size;
    }

    void RecentCard::OnArrange(const Context &ctx, const Rect &bounds) {
        Children().front()->Arrange(ctx, bounds);
        if (fittedWidth_ != bounds.width) {
            fittedWidth_ = bounds.width;
            const Theme &theme = ctx.theme;
            const float room = std::max(0.0f, bounds.width - 2.0f * theme.space.sm);
            titleLine_ =
                FitLine(ctx.text, theme.type.caption, title_, room, theme.type.ellipsis);
            artistLine_ =
                FitLine(ctx.text, theme.type.caption, artist_, room, theme.type.ellipsis);
        }
    }

    void RecentCard::OnPaint(const Context &ctx, Painter &painter) const {
        PaintChildren(ctx, painter);

        const Theme &theme = ctx.theme;
        const TextStyle &style = theme.type.caption;
        const Rect &bounds = Bounds();
        const FontMetrics metrics = ctx.text.Metrics(style.font, style.size);
        // A foot that darkens toward the bottom, so the two lines read over any art.
        const float foot = 2.0f * metrics.lineHeight + 2.0f * theme.space.sm;
        const Rect shade { bounds.x, bounds.Bottom() - 2.0f * foot, bounds.width, 2.0f * foot };
        painter.PushClip(bounds);
        painter.FillRoundRect(
            shade,
            theme.radius.medium,
            CornerColors::Vertical(
                theme.color.background.WithAlpha(0.0f), theme.color.background
            )
        );
        painter.PopClip();

        const float titleTop = bounds.Bottom() - theme.space.sm - 2.0f * metrics.lineHeight;
        painter.DrawText(
            { bounds.x + theme.space.sm,
              CenteredBaseline(metrics, titleTop, metrics.lineHeight) },
            style,
            theme.color.textPrimary,
            titleLine_.text
        );
        painter.DrawText(
            { bounds.x + theme.space.sm,
              CenteredBaseline(metrics, titleTop + metrics.lineHeight, metrics.lineHeight) },
            style,
            theme.color.textSecondary,
            artistLine_.text
        );
    }

}
