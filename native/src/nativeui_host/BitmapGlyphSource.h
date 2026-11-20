// BitmapGlyphSource: a GlyphSource over a fixed-cell bitmap atlas -- the shape
// of the game's RndFont (src/system/rndobj/Font.h). The font's numbers are
// copied into a plain BitmapFontData once (GameFonts.cpp does it from a loaded
// RndFont), so layout here touches no game object and tests can build a font
// by hand.
//
// How the numbers map (the same arithmetic RndText uses, Text.cpp ~3015-3060):
//  - A glyph's cell is cellWidth x cellHeight texels; drawn at `size` it is
//    `size` tall, so one texel is size / cellHeight logical units.
//  - RndFont stores a glyph's width and advance in CELL WIDTHS. Drawn width =
//    width * cellWidth * texel; pen advance = advance * cellWidth * texel.
//    (RndText: charW * mSize and advW * mSize, with glyph height
//    AspectRatio() * mSize -- its mSize is our size / aspect.)
//  - Kerning is added BEFORE each glyph, the first included (RndText adds
//    Kerning(prev, c) with prev = 0 for the first), in cell widths: the
//    font's base kerning plus the pair's table entry.
//  - The glyph's quad is the cell's left `width` cell-widths, its full height:
//    top at baseline - baselineFromTop * size.
//  - A monospace font advances one cell width per glyph and centres the glyph.
//
// Missing glyphs: FoldToBase() names the base letter of an accented Latin
// letter and the plain form of typographic punctuation; a code point with no
// glyph is drawn as its fold, then as its fold's other case, then as the
// placeholder: a hollow box 0.5 x 0.7 of the line (page -1, no texture).
#pragma once

#include "nativeui_host/GlyphSource.h"

#include <cstdint>
#include <unordered_map>

namespace nativeui_host {

    struct BitmapGlyph {
        int page = 0;
        float u0 = 0.0f, v0 = 0.0f, u1 = 0.0f, v1 = 0.0f; // the glyph's part of its cell
        float width = 0.0f; // in cell widths; 0 = no ink (space)
        float advance = 0.0f; // in cell widths
    };

    struct BitmapFontData {
        float cellWidth = 1.0f; // texels
        float cellHeight = 1.0f;
        // Where the ink sits in the cell, as fractions of the cell height from
        // its top (measured from the atlas by GameFonts.cpp): the top of a
        // capital, the baseline, and the bottom of a descender.
        float capTop = 0.2f;
        float baseline = 0.8f;
        float descenderBottom = 1.0f;
        bool monospace = false;
        bool pagesCarryColour = false;
        float baseKerning = 0.0f; // cell widths, added before every glyph
        std::unordered_map<char32_t, BitmapGlyph> glyphs;
        // (first | second << 16) -> the pair's EXTRA kerning on top of
        // baseKerning, in cell widths. BMP code points only, as in the game.
        std::unordered_map<std::uint32_t, float> pairKerning;
        std::vector<RndTex *> pages;
    };

    // The base form of `codePoint`: an accented Latin letter's plain letter, a
    // typographic quote's ASCII quote, a dash's hyphen, a no-break space's
    // space. Returns `codePoint` itself when there is none.
    char32_t FoldToBase(char32_t codePoint);

    class BitmapGlyphSource final : public GlyphSource {
    public:
        explicit BitmapGlyphSource(BitmapFontData data);

        float Advance(float size, std::string_view utf8) const override;
        nativeui::FontMetrics Metrics(float size) const override;
        float Layout(
            float size, float letterSpacing, std::string_view utf8,
            std::vector<GlyphQuad> &out
        ) const override;
        RndTex *PageTexture(int page) const override;
        bool PagesCarryColour() const override { return data_.pagesCarryColour; }
        bool HasGlyph(char32_t codePoint) const override;

        const BitmapFontData &Data() const { return data_; }
        // The glyph a code point is drawn with after fallback, or null for the
        // placeholder box.
        const BitmapGlyph *Resolve(char32_t codePoint) const;

        // The placeholder box, as fractions of the size.
        static constexpr float kPlaceholderAdvance = 0.5f;
        static constexpr float kPlaceholderHeight = 0.7f;

    private:
        float Walk(
            float size, float letterSpacing, std::string_view utf8,
            std::vector<GlyphQuad> *out
        ) const;

        BitmapFontData data_;
    };

}
