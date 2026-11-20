#include "songbrowser_ui/DetailPane.h"

#include "nativeui/IconTile.h"
#include "nativeui/Panel.h"

#include <algorithm>
#include <optional>
#include <vector>

namespace songbrowser_ui {

    using namespace nativeui;

    namespace {

        const std::string kNoKey;

        // A title that does not fit one line wraps onto this many.
        constexpr int kTitleLines = 2;
        // The most lines of a command's description.
        constexpr int kDescriptionLines = 5;
        // The difficulty meter's share of the pane's width; the tier's name
        // has the rest of the row.
        constexpr float kMeterShare = 0.42f;

    }

    bool SameContent(const DetailModel &a, const DetailModel &b) {
        return a.key == b.key && a.title == b.title && a.artist == b.artist
            && a.album == b.album && a.length == b.length && a.bpm == b.bpm
            && a.difficulty == b.difficulty && a.difficultyTier == b.difficultyTier
            && a.played == b.played && a.stars == b.stars && a.score == b.score
            && a.source == b.source && a.icon == b.icon && a.description == b.description;
    }

    // One selection's worth of pane: the model and the widgets that draw its
    // parts. The widgets are kept here, outside the widget tree, the way a list
    // keeps its rows: a face comes and goes with the selection.
    struct DetailPane::Face {
        DetailModel model;
        ArtImage art;
        std::optional<IconTile> tile; // instead of the art, for a command
        SegmentMeter meter;
        StarMarks stars;
        // Fitted for the last width arranged.
        float fittedWidth = -1.0f;
        TextStyleToken titleStyle = &Typography::title;
        std::vector<FittedLine> title; // one line, or two in the smaller style
        std::vector<FittedLine> description;
        FittedLine artist, album, length, bpm, difficulty, score, source;
        // Where each part goes, set by ArrangeFace.
        float titleBottom = 0.0f, artistTop = 0.0f, albumTop = 0.0f, ruleTop = 0.0f,
              difficultyTop = 0.0f, bestTop = 0.0f, statsTop = 0.0f;
        Rect inner;
        Rect picture; // the art's (or the tile's) square

        explicit Face(DetailModel m)
            : model(std::move(m)), art(model.art, &Radii::medium),
              meter(
                  kDifficultyTiers,
                  model.difficultyTier + 1,
                  &Palette::accent,
                  &Palette::fieldBorder
              ),
              stars(model.stars) {
            if (model.icon) {
                tile.emplace(*model.icon, &Radii::medium);
            }
        }
    };

    DetailPane::DetailPane() {
        PanelStyle glass;
        glass.border = &Palette::panelBorder;
        glass.borderEnd = &Palette::panelBorderEnd;
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
        const float meterWidth = inner.width * kMeterShare;

        if (face.fittedWidth != inner.width) {
            face.fittedWidth = inner.width;
            const std::string &ellipsis = theme.type.ellipsis;
            // One line in the heading style, or else two in the body style.
            face.titleStyle = &Typography::title;
            face.title = { FitLine(text, theme.type.title, model.title, inner.width, ellipsis) };
            if (face.title.front().truncated) {
                face.titleStyle = &Typography::body;
                face.title = WrapLines(
                    text, theme.type.body, model.title, inner.width, kTitleLines, ellipsis
                );
            }
            face.description = WrapLines(
                text,
                theme.type.caption,
                model.description,
                inner.width,
                kDescriptionLines,
                ellipsis
            );
            face.artist =
                FitLine(text, theme.type.caption, model.artist, inner.width, ellipsis);
            face.album = FitLine(text, theme.type.label, model.album, inner.width, ellipsis);
            const float half = (inner.width - space.md) * 0.5f;
            face.length = FitLine(text, theme.type.label, model.length, half, ellipsis);
            face.bpm = FitLine(text, theme.type.label, model.bpm, half, ellipsis);
            face.difficulty = FitLine(
                text,
                theme.type.label,
                model.difficulty,
                std::max(0.0f, inner.width - meterWidth - space.md),
                ellipsis
            );
            face.score = FitLine(
                text,
                theme.type.caption,
                model.score,
                std::max(0.0f, inner.width - StarMarks::WidthFor(theme) - space.md),
                ellipsis
            );
            // The tag sits on the picture with a margin and its own padding.
            face.source = FitLine(
                text,
                theme.type.label,
                model.source,
                std::max(0.0f, inner.width - 4.0f * space.sm),
                ellipsis
            );
        }

        // The text block is laid out from the bottom up; the picture takes the
        // square that is left above it. A command's face keeps the same places,
        // so the pane's shape does not jump between a song and a command.
        const float caption = lineHeight(&Typography::caption);
        const float label = lineHeight(&Typography::label);
        const float bestHeight = std::max(caption, theme.metric.starSize);
        const float meterHeight = theme.metric.meterCellHeight;
        float y = inner.Bottom();
        y -= label;
        face.statsTop = y;
        y -= space.sm + bestHeight;
        face.bestTop = y;
        y -= space.sm + label;
        face.difficultyTop = y;
        y -= space.sm + theme.metric.borderWidth;
        face.ruleTop = y;
        y -= space.sm + label;
        face.albumTop = y;
        y -= caption;
        face.artistTop = y;
        face.titleBottom = y;
        // Room for either shape of title, so nothing else moves between songs.
        y -= std::max(
            lineHeight(&Typography::title),
            static_cast<float>(kTitleLines) * lineHeight(&Typography::body)
        );
        const float side = std::max(0.0f, std::min(inner.width, y - space.sm - inner.y));
        face.picture = { inner.x + (inner.width - side) * 0.5f, inner.y, side, side };

        Widget &picture = face.tile ? static_cast<Widget &>(*face.tile) : face.art;
        picture.Measure(ctx, Constraints::Tight({ side, side }));
        picture.Arrange(ctx, face.picture);
        face.meter.Measure(ctx, Constraints::Tight({ meterWidth, meterHeight }));
        face.meter.Arrange(
            ctx,
            { inner.Right() - meterWidth,
              face.difficultyTop + (label - meterHeight) * 0.5f,
              meterWidth,
              meterHeight }
        );
        const float starsWidth = StarMarks::WidthFor(theme);
        face.stars.Measure(ctx, Constraints::Tight({ starsWidth, theme.metric.starSize }));
        face.stars.Arrange(
            ctx,
            { inner.x,
              face.bestTop + (bestHeight - theme.metric.starSize) * 0.5f,
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
        const Spacing &space = theme.space;
        const DetailModel &model = face.model;
        const Rect &inner = face.inner;
        const auto heightOf = [&](TextStyleToken token) {
            const TextStyle &style = theme.type.*token;
            return ctx.text.Metrics(style.font, style.size).lineHeight;
        };
        // One line in a box `boxHeight` high whose top is `top`.
        const auto draw = [&](const FittedLine &line,
                              TextStyleToken token,
                              const Color &tint,
                              float top,
                              TextAlign align) {
            const TextStyle &style = theme.type.*token;
            painter.DrawText(
                { inner.x + AlignOffset(align, inner.width, line.width),
                  CenteredBaseline(
                      ctx.text.Metrics(style.font, style.size), top, heightOf(token)
                  ) },
                style,
                tint,
                line.text
            );
        };

        // The picture, in a thin frame.
        if (face.tile) {
            face.tile->Paint(ctx, painter);
        } else {
            face.art.Paint(ctx, painter);
        }
        painter.StrokeRoundRect(
            face.picture,
            theme.radius.medium,
            CornerColors::Vertical(color.panelBorder, color.panelBorderEnd),
            theme.metric.borderWidth
        );

        // The title's lines stand on the line below them: a one-line title
        // leaves its spare room above it, next to the picture.
        const float titleLine = heightOf(face.titleStyle);
        float titleTop = face.titleBottom - titleLine * static_cast<float>(face.title.size());
        for (const FittedLine &line : face.title) {
            draw(line, face.titleStyle, color.textPrimary, titleTop, TextAlign::Left);
            titleTop += titleLine;
        }

        if (face.tile) {
            float top = face.artistTop;
            for (const FittedLine &line : face.description) {
                draw(line, &Typography::caption, color.textSecondary, top, TextAlign::Left);
                top += heightOf(&Typography::caption);
            }
            return;
        }

        // Where the song is from: a tag on the picture's lower left corner.
        if (!face.source.text.empty()) {
            const float label = heightOf(&Typography::label);
            const Rect tag { face.picture.x + space.sm,
                             face.picture.Bottom() - space.sm - label - space.xs,
                             face.source.width + 2.0f * space.sm,
                             label + space.xs };
            painter.FillRoundRect(tag, theme.radius.small, CornerColors::Solid(color.fieldFill));
            const TextStyle &style = theme.type.label;
            painter.DrawText(
                { tag.x + space.sm,
                  CenteredBaseline(
                      ctx.text.Metrics(style.font, style.size), tag.y, tag.height
                  ) },
                style,
                color.textPrimary,
                face.source.text
            );
        }

        draw(face.artist, &Typography::caption, color.accent, face.artistTop, TextAlign::Left);
        draw(face.album, &Typography::label, color.textSecondary, face.albumTop, TextAlign::Left);
        // The rule: the game's streak, blue running out to magenta.
        painter.FillRoundRect(
            { inner.x, face.ruleTop, inner.width, theme.metric.borderWidth },
            theme.radius.none,
            CornerColors::Horizontal(color.accent, color.streakMagenta)
        );
        draw(
            face.difficulty, &Typography::label, color.textPrimary, face.difficultyTop,
            TextAlign::Left
        );
        if (model.difficultyTier >= 0) {
            face.meter.Paint(ctx, painter);
        }
        if (model.played) {
            face.stars.Paint(ctx, painter);
        }
        {
            // The score shares its row with the stars and is centred on it.
            const TextStyle &style = theme.type.caption;
            const float rowHeight = std::max(heightOf(&Typography::caption), theme.metric.starSize);
            painter.DrawText(
                { inner.x + AlignOffset(TextAlign::Right, inner.width, face.score.width),
                  CenteredBaseline(
                      ctx.text.Metrics(style.font, style.size), face.bestTop, rowHeight
                  ) },
                style,
                model.played ? color.textPrimary : color.textDisabled,
                face.score.text
            );
        }
        draw(face.length, &Typography::label, color.textSecondary, face.statsTop, TextAlign::Left);
        draw(face.bpm, &Typography::label, color.textSecondary, face.statsTop, TextAlign::Right);
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
