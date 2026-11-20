#include "songbrowser_ui/SongWidgets.h"

#include "nativeui/Icon.h"
#include "nativeui/IconTile.h"

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
            const bool earned = i < stars_;
            if (earned && gold) {
                // Gold stars shine: a soft pool of the star's colour under each.
                painter.FillRoundRect(
                    mark,
                    size * 0.5f,
                    CornerColors::Solid(theme.color.star.Faded(theme.color.accentGlow.a)),
                    size
                );
            }
            // An earned star is filled; one not earned is its outline.
            painter.DrawImage(
                mark,
                IconImage(earned ? Icon::Star : Icon::StarOutline),
                kWholeImage,
                earned ? theme.color.star : theme.color.starEmpty,
                theme.radius.none
            );
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
        const TextStyle &style = theme.type.label;
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
    // CommandRow
    // ---------------------------------------------------------------------------

    CommandRow::CommandRow(
        nativeui::Icon icon, std::string title, std::string tagline, std::function<bool()> lit
    )
        : title_(std::move(title)), tagline_(std::move(tagline)), isLit_(std::move(lit)) {
        Adopt(std::make_unique<IconTile>(icon, &Radii::small));
    }

    Size CommandRow::OnMeasure(const Context &ctx, const Constraints &constraints) {
        return { constraints.Biggest().width, ctx.theme.metric.listRowHeight };
    }

    void CommandRow::OnArrange(const Context &ctx, const Rect &bounds) {
        const Theme &theme = ctx.theme;
        const Spacing &space = theme.space;
        // The same insets and the same square as a song row's art, so the tile
        // and the title line up with the songs under them.
        const Rect inner = bounds.Deflated({ space.sm, space.xs, space.md, space.xs });
        const float tile = inner.height;
        Widget &icon = *Children().front();
        icon.Measure(ctx, Constraints::Tight({ tile, tile }));
        icon.Arrange(ctx, { inner.x, inner.y, tile, tile });

        const float left = inner.x + tile + space.md;
        if (fittedWidth_ != bounds.width) {
            fittedWidth_ = bounds.width;
            const float room = std::max(0.0f, inner.Right() - left);
            titleLine_ =
                FitLine(ctx.text, theme.type.body, title_, room, theme.type.ellipsis);
            taglineLine_ = FitLine(
                ctx.text,
                theme.type.caption,
                tagline_,
                std::max(0.0f, room - titleLine_.width - space.lg),
                theme.type.ellipsis
            );
        }
        textLeft_ = left;
        textRight_ = inner.Right();
    }

    void CommandRow::OnAdvance(const Context &ctx, float deltaSeconds) {
        lit_.SetTarget(isLit_ && isLit_() ? 1.0f : 0.0f);
        lit_.Advance(deltaSeconds, ctx.theme.motion.fadeSpring);
    }

    void CommandRow::OnPaint(const Context &ctx, Painter &painter) const {
        PaintChildren(ctx, painter);

        const Theme &theme = ctx.theme;
        const Rect &bounds = Bounds();
        const TextStyle &titleStyle = theme.type.body;
        painter.DrawText(
            { textLeft_,
              CenteredBaseline(
                  ctx.text.Metrics(titleStyle.font, titleStyle.size), bounds.y, bounds.height
              ) },
            titleStyle,
            theme.color.textPrimary,
            titleLine_.text
        );
        // What the command does, at the row's right end; brighter on the bar.
        const TextStyle &taglineStyle = theme.type.caption;
        painter.DrawText(
            { textRight_ - taglineLine_.width,
              CenteredBaseline(
                  ctx.text.Metrics(taglineStyle.font, taglineStyle.size),
                  bounds.y,
                  bounds.height
              ) },
            taglineStyle,
            Mix(theme.color.textSecondary, theme.color.textPrimary, lit_.Value()),
            taglineLine_.text
        );
    }

    // ---------------------------------------------------------------------------
    // RecentCard
    // ---------------------------------------------------------------------------

    namespace {

        // A card's caption: up to this many lines of title, then one of artist.
        constexpr int kCardTitleLines = 2;

    }

    RecentCard::RecentCard(std::string title, std::string artist, ArtSource art)
        : title_(std::move(title)), artist_(std::move(artist)) {
        Adopt(std::make_unique<ArtImage>(std::move(art), &Radii::medium));
    }

    Size RecentCard::OnMeasure(const Context &, const Constraints &constraints) {
        return constraints.Biggest();
    }

    void RecentCard::OnArrange(const Context &ctx, const Rect &bounds) {
        // The art is the square across the top; the caption has the rest.
        const float side = std::min(bounds.width, bounds.height);
        Widget &art = *Children().front();
        art.Measure(ctx, Constraints::Tight({ side, side }));
        art.Arrange(ctx, { bounds.x, bounds.y, side, side });
        if (fittedWidth_ != bounds.width) {
            fittedWidth_ = bounds.width;
            const Theme &theme = ctx.theme;
            const float room = std::max(0.0f, bounds.width - 2.0f * theme.space.xs);
            titleLines_ = WrapLines(
                ctx.text, theme.type.label, title_, room, kCardTitleLines, theme.type.ellipsis
            );
            artistLine_ = FitLine(ctx.text, theme.type.label, artist_, room, theme.type.ellipsis);
        }
    }

    void RecentCard::OnPaint(const Context &ctx, Painter &painter) const {
        PaintChildren(ctx, painter);

        const Theme &theme = ctx.theme;
        const TextStyle &style = theme.type.label;
        const Rect &bounds = Bounds();
        const FontMetrics metrics = ctx.text.Metrics(style.font, style.size);
        const float left = bounds.x + theme.space.xs;
        float top = bounds.y + std::min(bounds.width, bounds.height) + theme.space.sm;
        for (const FittedLine &line : titleLines_) {
            painter.DrawText(
                { left, CenteredBaseline(metrics, top, metrics.lineHeight) },
                style,
                theme.color.textPrimary,
                line.text
            );
            top += metrics.lineHeight;
        }
        // The artist follows the title directly: a one-line title leaves the
        // space under the artist, not a hole between the two.
        painter.DrawText(
            { left, CenteredBaseline(metrics, top, metrics.lineHeight) },
            style,
            theme.color.textSecondary,
            artistLine_.text
        );
    }

}
