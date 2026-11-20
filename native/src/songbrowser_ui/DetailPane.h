// DetailPane: everything about the selected thing, on a glassy panel.
//
// For a song, top to bottom: the art, framed, with where the song is from as a
// tag on its corner; the title; the artist; album and year; a rule; then
// difficulty (tier name and meter), best stars and score, length and BPM.
// For a command (a row that is not a song) the same pane shows the command's
// icon as a tile where the art goes, its name, and a few lines on what it does.
//
// It follows the selection with a short crossfade. What it shows is a
// DetailModel, a plain value taken whole: while fading, the old model and the
// new one are both drawn, each complete and each at its own opacity, so no
// frame mixes one song's title with another song's art. With nothing to show
// (nothing matches a search, an empty catalog) the whole pane fades away.
//
// Rules:
//  - The title is one line in the heading style when it fits. When it does
//    not, it wraps onto two lines in the body style; past that it is cut with
//    the ellipsis. The block it sits in is the same height either way, so
//    nothing else moves between songs.
//  - The text is laid out from the pane's bottom edge up, and the art is the
//    square that is left above it: the last line always sits one padding
//    above the pane's edge, whatever the fonts' metrics are.
#pragma once

#include "nativeui/Animation.h"
#include "nativeui/Icon.h"
#include "nativeui/Widget.h"
#include "songbrowser_ui/SongWidgets.h"

#include <memory>
#include <optional>
#include <string>

namespace songbrowser_ui {

    struct DetailModel {
        std::string key; // two models with one key are the same thing (a song's short name)
        std::string title;
        std::string artist;
        std::string album; // "Album (1999)", "1999", or empty
        std::string length; // "3:42", or empty
        std::string bpm; // "128 BPM", or empty
        std::string difficulty; // the tier's name
        int difficultyTier = -1;
        bool played = false;
        int stars = 0;
        std::string score; // "1,234,567"; empty when never played
        std::string source; // "Dance Central 2"
        ArtSource art;
        // A command instead of a song: its icon stands where the art would and
        // `description` is wrapped under the title; the song fields are unused.
        std::optional<nativeui::Icon> icon;
        std::string description;
    };

    bool SameContent(const DetailModel &a, const DetailModel &b);

    class DetailPane final : public nativeui::Widget {
    public:
        DetailPane();
        ~DetailPane() override;

        // Shows `model`, crossfading from whatever is shown. The same song with
        // new numbers (a new best score) changes in place.
        void Show(DetailModel model);
        // Nothing to show: the pane fades away.
        void Clear();

        // The song shown now (the one fading in, during a fade); empty for none.
        const std::string &ShownKey() const;
        // The song fading out, while a fade runs; empty otherwise.
        const std::string &FadingKey() const;
        // 0..1: how far the crossfade has got.
        float FadeProgress() const { return fade_.Value(); }

    protected:
        nativeui::Size OnMeasure(
            const nativeui::Context &ctx, const nativeui::Constraints &constraints
        ) override;
        void OnArrange(const nativeui::Context &ctx, const nativeui::Rect &bounds) override;
        void OnAdvance(const nativeui::Context &ctx, float deltaSeconds) override;
        void OnPaint(const nativeui::Context &ctx, nativeui::Painter &painter) const override;
        bool IsAtRest() const override;

    private:
        struct Face;
        std::unique_ptr<Face> MakeFace(DetailModel model) const;
        void ArrangeFace(const nativeui::Context &ctx, Face &face) const;
        void PaintFace(
            const nativeui::Context &ctx, nativeui::Painter &painter, const Face &face
        ) const;

        std::unique_ptr<Face> current_; // null: no song
        std::unique_ptr<Face> previous_; // fading out; null when no fade runs
        nativeui::Tween fade_ { 1.0f }; // 0 = previous_ only, 1 = current_ only
        float previousOpacity_ = 1.0f; // how visible previous_ was when it was replaced
        bool fadePending_ = false;
        nativeui::Spring presence_ { 0.0f }; // 0 = no pane, 1 = pane
        bool placed_ = false; // false until the first Advance, which jumps
    };

}
