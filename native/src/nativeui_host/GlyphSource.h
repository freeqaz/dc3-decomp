// GlyphSource: one typeface as the host sees it -- "how wide is this text" and
// "which quads draw it". It is the seam between the widgets and whatever holds
// the glyphs: today the game's bitmap atlases (BitmapGlyphSource.h); a sharper
// source (a larger atlas, a distance field, a vector rasteriser) implements
// the same three calls and nothing above it changes.
//
// Rules:
//  - `size` is the height of the font's line box in logical units: one line of
//    this font is `size` tall, with the baseline inside it at Metrics().ascent
//    from the top of the tallest ordinary glyph.
//  - Advance() is the plain sum of advances (kerning included), with no letter
//    spacing. Layout() places exactly the same advances and adds
//    `letterSpacing` after every code point but the last, so
//        Layout(size, ls, text).penX == Advance(size, text) + ls * (codePoints - 1)
//    which is nativeui::MeasureWidth. A test holds this for every source.
//  - Every code point of the text is one glyph: one that the face lacks is
//    drawn as its base letter (e with acute -> e) and, failing that, as a
//    visible placeholder box. Nothing is ever dropped, so widths, the cursor
//    and the ellipsis all count the same characters.
//  - Quads are relative to the run's origin, the left end of the baseline: x
//    right, y DOWN (a glyph above the baseline has negative y).
#pragma once

#include "nativeui/Text.h"

#include <string_view>
#include <vector>

class RndTex;

namespace nativeui_host {

    struct GlyphQuad {
        float x = 0.0f, y = 0.0f, w = 0.0f, h = 0.0f; // relative to the baseline's left end
        float u0 = 0.0f, v0 = 0.0f, u1 = 0.0f, v1 = 0.0f;
        int page = -1; // PageTexture() index; -1: the placeholder box, no texture
    };

    class GlyphSource {
    public:
        virtual ~GlyphSource() = default;

        virtual float Advance(float size, std::string_view utf8) const = 0;
        virtual nativeui::FontMetrics Metrics(float size) const = 0;
        // Appends the quads of `utf8` to `out` and returns the pen position
        // after the last glyph (see the identity above). Glyphs with no ink
        // (a space) advance the pen and append nothing.
        virtual float Layout(
            float size, float letterSpacing, std::string_view utf8,
            std::vector<GlyphQuad> &out
        ) const = 0;

        // The texture of a page; it must stay alive while the source does.
        virtual RndTex *PageTexture(int page) const = 0;
        // False: the pages hold coverage only and are drawn as a flat tint
        // (Canvas2DTexMode::kCoverageA). True: their RGB is part of the glyph
        // (icons, shadowed faces) and is multiplied by the tint (kColor).
        virtual bool PagesCarryColour() const = 0;
        // Whether the face itself has this code point (before any fallback).
        virtual bool HasGlyph(char32_t codePoint) const = 0;
    };

}
