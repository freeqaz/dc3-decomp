// SongWidgets: the pieces of the song browser that show a song.
//
//   ArtImage         album art that may arrive late: asks its source every
//                    frame and fades the picture in when it turns up
//   SegmentMeter     the difficulty meter: a row of cells, the first few lit
//   StarMarks        stars earned, as a row of marks
//   SongRow          one row of the list: art, title, artist, meter, stars
//   SectionHeading   a section header row: name, song count, a rule
//   RecentCard       a shelf card: art with the title and artist over its foot
//
// Rules:
//  - Each takes a plain model (strings and numbers copied at build time) and
//    keeps no pointer into the catalog; art is fetched through an ArtSource.
//  - Text is fitted once per width, not once per frame.
//  - Columns have fixed places: a long title is cut with the ellipsis and can
//    never run under the column beside it.
#pragma once

#include "nativeui/Animation.h"
#include "nativeui/Icon.h"
#include "nativeui/Image.h"
#include "nativeui/Widget.h"
#include "songbrowser_ui/MatchText.h"

#include <functional>
#include <string>
#include <vector>

namespace songbrowser_ui {

    // Whatever art there is right now; the null handle until it has arrived.
    using ArtSource = std::function<nativeui::ImageHandle()>;

    // The game's difficulty tiers: warmup .. off the hook.
    inline constexpr int kDifficultyTiers = 7;
    // The most stars a song gives; a best of more than this is "gold stars".
    inline constexpr int kMaxStars = 5;

    class ArtImage final : public nativeui::Widget {
    public:
        ArtImage(ArtSource source, nativeui::RadiusToken radius);

        nativeui::ImageHandle Shown() const { return image_->GetImage(); }

    protected:
        nativeui::Size OnMeasure(
            const nativeui::Context &ctx, const nativeui::Constraints &constraints
        ) override;
        void OnArrange(const nativeui::Context &ctx, const nativeui::Rect &bounds) override;
        void OnAdvance(const nativeui::Context &ctx, float deltaSeconds) override;

    private:
        ArtSource source_;
        nativeui::Image *image_ = nullptr; // owned as a child
    };

    // Fills the width it is given with `cells` equal cells.
    class SegmentMeter final : public nativeui::Widget {
    public:
        SegmentMeter(
            int cells,
            int lit,
            nativeui::ColorToken litColor,
            nativeui::ColorToken unlitColor
        );

    protected:
        nativeui::Size OnMeasure(
            const nativeui::Context &ctx, const nativeui::Constraints &constraints
        ) override;
        void OnPaint(const nativeui::Context &ctx, nativeui::Painter &painter) const override;

    private:
        int cells_;
        int lit_;
        nativeui::ColorToken litColor_;
        nativeui::ColorToken unlitColor_;
    };

    // kMaxStars stars in a row: the first `stars` filled, the rest outlines.
    // More than kMaxStars is gold: all of them, with a glow.
    class StarMarks final : public nativeui::Widget {
    public:
        explicit StarMarks(int stars);

        static float WidthFor(const nativeui::Theme &theme);

    protected:
        nativeui::Size OnMeasure(
            const nativeui::Context &ctx, const nativeui::Constraints &constraints
        ) override;
        void OnPaint(const nativeui::Context &ctx, nativeui::Painter &painter) const override;

    private:
        int stars_;
    };

    struct SongRowModel {
        std::string title;
        std::string secondary; // the artist; the album when only the album matched
        std::vector<ByteSpan> titleMatch; // what a search matched, if anything
        std::vector<ByteSpan> secondaryMatch;
        int difficultyTier = -1; // -1: none
        int stars = 0;
        bool played = false;
    };

    class SongRow final : public nativeui::Widget {
    public:
        // `lit` answers whether this row is the selected row of a focused list;
        // a lit row sits on the accent bar, where accent text would vanish, so
        // its matched text turns to the primary colour instead.
        SongRow(SongRowModel model, ArtSource art, std::function<bool()> lit);

        const SongRowModel &Model() const { return model_; }

    protected:
        nativeui::Size OnMeasure(
            const nativeui::Context &ctx, const nativeui::Constraints &constraints
        ) override;
        void OnArrange(const nativeui::Context &ctx, const nativeui::Rect &bounds) override;
        void OnAdvance(const nativeui::Context &ctx, float deltaSeconds) override;
        void OnPaint(const nativeui::Context &ctx, nativeui::Painter &painter) const override;
        bool IsAtRest() const override { return lit_.AtRest(); }

    private:
        SongRowModel model_;
        std::function<bool()> isLit_;
        nativeui::Spring lit_; // 0 = an ordinary row, 1 = on the accent bar
        nativeui::Widget *art_ = nullptr;
        nativeui::Widget *meter_ = nullptr;
        nativeui::Widget *stars_ = nullptr;
        // Fitted for the last width arranged.
        float fittedWidth_ = -1.0f;
        nativeui::Rect titleBox_;
        nativeui::Rect secondaryBox_;
        MatchLine title_;
        MatchLine secondary_;
    };

    class SectionHeading final : public nativeui::Widget {
    public:
        SectionHeading(std::string title, std::string count);

    protected:
        nativeui::Size OnMeasure(
            const nativeui::Context &ctx, const nativeui::Constraints &constraints
        ) override;
        void OnPaint(const nativeui::Context &ctx, nativeui::Painter &painter) const override;

    private:
        std::string title_;
        std::string count_;
    };

    // A row of the list that is not a song but something to do ("Random
    // song"): an icon tile where a song has its art, the command's name, and at
    // the right end a few words on what it does.
    class CommandRow final : public nativeui::Widget {
    public:
        // `lit`: as SongRow's.
        CommandRow(
            nativeui::Icon icon, std::string title, std::string tagline, std::function<bool()> lit
        );

        const std::string &Title() const { return title_; }

    protected:
        nativeui::Size OnMeasure(
            const nativeui::Context &ctx, const nativeui::Constraints &constraints
        ) override;
        void OnArrange(const nativeui::Context &ctx, const nativeui::Rect &bounds) override;
        void OnAdvance(const nativeui::Context &ctx, float deltaSeconds) override;
        void OnPaint(const nativeui::Context &ctx, nativeui::Painter &painter) const override;
        bool IsAtRest() const override { return lit_.AtRest(); }

    private:
        std::string title_;
        std::string tagline_;
        std::function<bool()> isLit_;
        nativeui::Spring lit_; // 0 = an ordinary row, 1 = on the highlight bar
        float fittedWidth_ = -1.0f;
        float textLeft_ = 0.0f;
        float textRight_ = 0.0f;
        nativeui::FittedLine titleLine_;
        nativeui::FittedLine taglineLine_;
    };

    // A card of the Recently Played shelf: the art as a square across the top
    // and, under it where there is room for whole words, the title on up to
    // two lines and the artist on one.
    class RecentCard final : public nativeui::Widget {
    public:
        RecentCard(std::string title, std::string artist, ArtSource art);

        // The caption as fitted for the last width arranged.
        const std::vector<nativeui::FittedLine> &TitleLines() const { return titleLines_; }

    protected:
        nativeui::Size OnMeasure(
            const nativeui::Context &ctx, const nativeui::Constraints &constraints
        ) override;
        void OnArrange(const nativeui::Context &ctx, const nativeui::Rect &bounds) override;
        void OnPaint(const nativeui::Context &ctx, nativeui::Painter &painter) const override;

    private:
        std::string title_;
        std::string artist_;
        float fittedWidth_ = -1.0f;
        std::vector<nativeui::FittedLine> titleLines_;
        nativeui::FittedLine artistLine_;
    };

}
