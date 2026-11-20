// DetailPane: everything about the selected song, on a glassy panel.
//
// Top to bottom: large art, title, artist, album and year, length and BPM,
// difficulty (tier name and meter), best stars and score, where the song is
// from.
//
// It follows the selection with a short crossfade. What it shows is a
// DetailModel, a plain value taken whole: while fading, the old model and the
// new one are both drawn, each complete and each at its own opacity, so no
// frame mixes one song's title with another song's art. With no song to show
// (nothing matches a search, an empty catalog) the whole pane fades away.
//
// Rules:
//  - The title uses the heading style when it fits and steps down to the body
//    style when it does not; past that it is cut with the ellipsis.
//  - The art is a square as large as the pane's width and the text under it
//    allow.
#pragma once

#include "nativeui/Animation.h"
#include "nativeui/Widget.h"
#include "songbrowser_ui/SongWidgets.h"

#include <memory>
#include <string>

namespace songbrowser_ui {

    struct DetailModel {
        std::string key; // the song's short name: two models with one key are the same song
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
