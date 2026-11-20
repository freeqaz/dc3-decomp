// Label: one line of text.
//
// It asks for the width of its text and the height of one line. Given less
// width than that, it cuts the text and ends it with the theme's ellipsis;
// given more, it places the line by its alignment. The line is centred
// vertically in whatever height it gets.
//
// The type style and colour are theme tokens (Theme.h); the style decides the
// case, so a title written "Déjà Vu" shows as "DÉJÀ VU" in the game's theme.
#pragma once

#include "nativeui/Widget.h"

#include <string>

namespace nativeui {

    class Label final : public Widget {
    public:
        explicit Label(
            std::string text,
            TextStyleToken style = &Typography::body,
            ColorToken color = &Palette::textPrimary,
            TextAlign align = TextAlign::Left
        );

        void SetText(std::string text) { text_ = std::move(text); }
        const std::string &Text() const { return text_; }

    protected:
        Size OnMeasure(const Context &ctx, const Constraints &constraints) override;
        void OnPaint(const Context &ctx, Painter &painter) const override;

    private:
        std::string text_;
        TextStyleToken style_;
        ColorToken color_;
        TextAlign align_;
    };

}
