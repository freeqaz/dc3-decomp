#include "songbrowser_ui/DetailPane.h"

#include "nativeui/Panel.h"

#include <algorithm>

namespace songbrowser_ui {

    using namespace nativeui;

    namespace {

        const std::string kNoKey;

        struct Line {
            TextStyleToken style;
            ColorToken color;
        };

    }

    bool SameContent(const DetailModel &a, const DetailModel &b) {
        return a.key == b.key && a.title == b.title && a.artist == b.artist
            && a.album == b.album && a.length == b.length && a.bpm == b.bpm
            && a.difficulty == b.difficulty && a.difficultyTier == b.difficultyTier
            && a.played == b.played && a.stars == b.stars && a.score == b.score
            && a.source == b.source;
    }

    // One song's worth of pane: the model and the widgets that draw its parts.
    // The widgets are kept here, outside the widget tree, the way a list keeps
    // its rows: a face comes and goes with the selection.
    struct DetailPane::Face {
        DetailModel model;
        ArtImage art;
        SegmentMeter meter;
        StarMarks stars;
        // Fitted for the last width arranged.
        float fittedWidth = -1.0f;
        TextStyleToken titleStyle = &Typography::title;
        FittedLine title, artist, album, length, bpm, difficulty, score, source;
        // Where each part goes, set by ArrangeFace.
        float titleTop = 0.0f, artistTop = 0.0f, albumTop = 0.0f, statsTop = 0.0f,
              difficultyTop = 0.0f, bestTop = 0.0f, sourceTop = 0.0f;
        Rect inner;

        explicit Face(DetailModel m)
            : model(std::move(m)), art(model.art, &Radii::medium),
              meter(
                  kDifficultyTiers,
                  model.difficultyTier + 1,
                  &Palette::accent,
                  &Palette::fieldFill
              ),
              stars(model.stars) {}
    };

    DetailPane::DetailPane() {
        PanelStyle glass;
        glass.border = &Palette::panelBorder;
        glass.halo = PanelStyle::Halo::Shadow;
        Adopt(std::make_unique<Panel>(glass));
    }

    DetailPane::~DetailPane() = default;

    std::unique_ptr<DetailPane::Face> DetailPane::MakeFace(DetailModel model) const {
        return std::make_unique<Face>(std::move(model));
    }

    void DetailPane::Show(DetailModel model) {
        if (current_ && current_->model.key == model.key) {
            // The same song: new numbers go in without a fade.
            if (!SameContent(current_->model, model)) {
                current_ = MakeFace(std::move(model));
            }
            return;
        }
        // What is fading in becomes what fades out, from as visible as it had got.
        previousOpacity_ = current_ ? (previous_ ? fade_.Value() : 1.0f) : 0.0f;
        previous_ = std::move(current_);
        current_ = MakeFace(std::move(model));
        fade_.Snap(0.0f);
        fadePending_ = true;
    }

    void DetailPane::Clear() {
        if (!current_) {
            return;
        }
        previousOpacity_ = previous_ ? fade_.Value() : 1.0f;
        previous_ = std::move(current_);
        fade_.Snap(0.0f);
        fadePending_ = true;
    }

    const std::string &DetailPane::ShownKey() const {
        return current_ ? current_->model.key : kNoKey;
    }

    const std::string &DetailPane::FadingKey() const {
        return previous_ ? previous_->model.key : kNoKey;
    }

    Size DetailPane::OnMeasure(const Context &ctx, const Constraints &constraints) {
        return { ctx.theme.metric.sidePaneWidth, constraints.Biggest().height };
    }

    void DetailPane::OnArrange(const Context &ctx, const Rect &bounds) {
        Children().front()->Measure(ctx, Constraints::Tight({ bounds.width, bounds.height }));
        Children().front()->Arrange(ctx, bounds);
        if (current_) {
            ArrangeFace(ctx, *current_);
        }
        if (previous_) {
            ArrangeFace(ctx, *previous_);
        }
    }

    void DetailPane::ArrangeFace(const Context &ctx, Face &face) const {
        const Theme &theme = ctx.theme;
        const Spacing &space = theme.space;
        const TextMeasurer &text = ctx.text;
        const Rect inner = Bounds().Deflated(Insets::All(space.md));
        face.inner = inner;
        const auto lineHeight = [&](TextStyleToken token) {
            const TextStyle &style = theme.type.*token;
            return text.Metrics(style.font, style.size).lineHeight;
        };
        const DetailModel &model = face.model;

        if (face.fittedWidth != inner.width) {
            face.fittedWidth = inner.width;
            const std::string &ellipsis = theme.type.ellipsis;
            // The title steps down a size before it is cut.
            face.titleStyle = &Typography::title;
            face.title = FitLine(text, theme.type.title, model.title, inner.width, ellipsis);
            if (face.title.truncated) {
                face.titleStyle = &Typography::body;
                face.title =
                    FitLine(text, theme.type.body, model.title, inner.width, ellipsis);
            }
            face.artist = FitLine(text, theme.type.body, model.artist, inner.width, ellipsis);
            face.album = FitLine(text, theme.type.caption, model.album, inner.width, ellipsis);
            const float half = (inner.width - space.md) * 0.5f;
            face.length = FitLine(text, theme.type.caption, model.length, half, ellipsis);
            face.bpm = FitLine(text, theme.type.caption, model.bpm, half, ellipsis);
            face.difficulty =
                FitLine(text, theme.type.caption, model.difficulty, inner.width, ellipsis);
            face.score = FitLine(
                text,
                theme.type.caption,
                model.score,
                std::max(0.0f, inner.width - StarMarks::WidthFor(theme) - space.md),
                ellipsis
            );
            face.source = FitLine(text, theme.type.caption, model.source, inner.width, ellipsis);
        }

        // The text block is laid out from the bottom up; the art takes the
        // square that is left above it.
        const float caption = lineHeight(&Typography::caption);
        const float meterHeight = theme.metric.meterCellHeight;
        float y = inner.Bottom();
        y -= caption;
        face.sourceTop = y;
        y -= space.sm + caption;
        face.bestTop = y;
        y -= space.sm + meterHeight + space.xs + caption;
        face.difficultyTop = y;
        y -= space.sm + caption;
        face.statsTop = y;
        y -= space.xs + caption;
        face.albumTop = y;
        y -= lineHeight(&Typography::body);
        face.artistTop = y;
        // Room for the heading style whichever style the title ends up in, so
        // nothing below moves between songs.
        y -= lineHeight(&Typography::title);
        face.titleTop = y;
        const float artSide =
            std::max(0.0f, std::min(inner.width, y - space.sm - inner.y));

        face.art.Measure(ctx, Constraints::Tight({ artSide, artSide }));
        face.art.Arrange(
            ctx, { inner.x + (inner.width - artSide) * 0.5f, inner.y, artSide, artSide }
        );
        face.meter.Measure(ctx, Constraints::Tight({ inner.width, meterHeight }));
        face.meter.Arrange(
            ctx, { inner.x, face.difficultyTop + caption + space.xs, inner.width, meterHeight }
        );
        const float starsWidth = StarMarks::WidthFor(theme);
        face.stars.Measure(ctx, Constraints::Tight({ starsWidth, theme.metric.starSize }));
        face.stars.Arrange(
            ctx,
            { inner.x,
              face.bestTop + (caption - theme.metric.starSize) * 0.5f,
              starsWidth,
              theme.metric.starSize }
        );
    }

    void DetailPane::OnAdvance(const Context &ctx, float deltaSeconds) {
        const Motion &motion = ctx.theme.motion;
        if (fadePending_) {
            fade_.Start(1.0f, motion.normal, motion.easing);
            fadePending_ = false;
        }
        fade_.Advance(deltaSeconds);
        if (fade_.AtRest()) {
            previous_.reset();
        }
        presence_.SetTarget(current_ ? 1.0f : 0.0f);
        if (!placed_) {
            presence_.Snap(presence_.Target());
            fade_.Snap(1.0f);
            previous_.reset();
            placed_ = true;
        }
        presence_.Advance(deltaSeconds, motion.fadeSpring);
        if (current_) {
            current_->art.Advance(ctx, deltaSeconds);
        }
        if (previous_) {
            previous_->art.Advance(ctx, deltaSeconds);
        }
    }

    void DetailPane::PaintFace(const Context &ctx, Painter &painter, const Face &face) const {
        const Theme &theme = ctx.theme;
        const Palette &color = theme.color;
        const DetailModel &model = face.model;
        const Rect &inner = face.inner;
        const auto draw = [&](const FittedLine &line,
                              TextStyleToken token,
                              const Color &tint,
                              float top,
                              TextAlign align,
                              float boxHeight) {
            const TextStyle &style = theme.type.*token;
            painter.DrawText(
                { inner.x + AlignOffset(align, inner.width, line.width),
                  CenteredBaseline(ctx.text.Metrics(style.font, style.size), top, boxHeight) },
                style,
                tint,
                line.text
            );
        };
        const auto heightOf = [&](TextStyleToken token) {
            const TextStyle &style = theme.type.*token;
            return ctx.text.Metrics(style.font, style.size).lineHeight;
        };
        const float caption = heightOf(&Typography::caption);

        face.art.Paint(ctx, painter);
        draw(
            face.title,
            face.titleStyle,
            color.textPrimary,
            face.titleTop,
            TextAlign::Left,
            heightOf(&Typography::title)
        );
        draw(
            face.artist,
            &Typography::body,
            color.textSecondary,
            face.artistTop,
            TextAlign::Left,
            heightOf(&Typography::body)
        );
        draw(face.album, &Typography::caption, color.textSecondary, face.albumTop, TextAlign::Left, caption);
        draw(face.length, &Typography::caption, color.textPrimary, face.statsTop, TextAlign::Left, caption);
        draw(face.bpm, &Typography::caption, color.textPrimary, face.statsTop, TextAlign::Right, caption);
        draw(face.difficulty, &Typography::caption, color.accent, face.difficultyTop, TextAlign::Left, caption);
        if (model.difficultyTier >= 0) {
            face.meter.Paint(ctx, painter);
        }
        if (model.played) {
            face.stars.Paint(ctx, painter);
        }
        draw(
            face.score,
            &Typography::caption,
            model.played ? color.textPrimary : color.textDisabled,
            face.bestTop,
            TextAlign::Right,
            caption
        );
        draw(face.source, &Typography::caption, color.textSecondary, face.sourceTop, TextAlign::Left, caption);
    }

    void DetailPane::OnPaint(const Context &ctx, Painter &painter) const {
        if (!current_ && !previous_) {
            return;
        }
        const float fade = fade_.Value();
        const float leaving = previous_ ? (1.0f - fade) * previousOpacity_ : 0.0f;
        // A song arriving where there was none comes in with the pane; one
        // replacing another fades over it on a pane that stays.
        const float arriving = !current_ ? 0.0f : (previous_ ? fade : presence_.Value());

        painter.PushLayer({ {}, 1.0f, std::min(1.0f, std::max(leaving, arriving)) });
        PaintChildren(ctx, painter); // the panel
        painter.PopLayer();
        if (previous_) {
            painter.PushLayer({ {}, 1.0f, leaving });
            PaintFace(ctx, painter, *previous_);
            painter.PopLayer();
        }
        if (current_) {
            painter.PushLayer({ {}, 1.0f, arriving });
            PaintFace(ctx, painter, *current_);
            painter.PopLayer();
        }
    }

    bool DetailPane::IsAtRest() const {
        if (fadePending_ || !fade_.AtRest() || previous_ || !presence_.AtRest() || !placed_) {
            return false;
        }
        return !current_ || current_->art.AtRest();
    }

}
