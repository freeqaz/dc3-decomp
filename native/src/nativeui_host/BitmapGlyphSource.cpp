#include "nativeui_host/BitmapGlyphSource.h"

#include "nativeui/Utf8.h"

#include <algorithm>

namespace nativeui_host {

    namespace {

        // Base letters of Latin-1 Supplement U+00C0..U+00FF ('\0' = no base).
        const char kLatin1Base[64] = {
            'A', 'A', 'A', 'A', 'A', 'A', 'A', 'C', 'E', 'E', 'E', 'E', 'I', 'I', 'I', 'I',
            'D', 'N', 'O', 'O', 'O', 'O', 'O', 0,   'O', 'U', 'U', 'U', 'U', 'Y', 0,   's',
            'a', 'a', 'a', 'a', 'a', 'a', 'a', 'c', 'e', 'e', 'e', 'e', 'i', 'i', 'i', 'i',
            'd', 'n', 'o', 'o', 'o', 'o', 'o', 0,   'o', 'u', 'u', 'u', 'u', 'y', 0,   'y',
        };

        // Base letters of Latin Extended-A U+0100..U+017F.
        const char kLatinExtABase[128] = {
            'A', 'a', 'A', 'a', 'A', 'a', 'C', 'c', 'C', 'c', 'C', 'c', 'C', 'c', 'D', 'd',
            'D', 'd', 'E', 'e', 'E', 'e', 'E', 'e', 'E', 'e', 'E', 'e', 'G', 'g', 'G', 'g',
            'G', 'g', 'G', 'g', 'H', 'h', 'H', 'h', 'I', 'i', 'I', 'i', 'I', 'i', 'I', 'i',
            'I', 'i', 'I', 'i', 'J', 'j', 'K', 'k', 'k', 'L', 'l', 'L', 'l', 'L', 'l', 'L',
            'l', 'L', 'l', 'N', 'n', 'N', 'n', 'N', 'n', 'n', 'N', 'n', 'O', 'o', 'O', 'o',
            'O', 'o', 'O', 'o', 'R', 'r', 'R', 'r', 'R', 'r', 'S', 's', 'S', 's', 'S', 's',
            'S', 's', 'T', 't', 'T', 't', 'T', 't', 'U', 'u', 'U', 'u', 'U', 'u', 'U', 'u',
            'U', 'u', 'U', 'u', 'W', 'w', 'Y', 'y', 'Y', 'Z', 'z', 'Z', 'z', 'Z', 'z', 's',
        };

        char32_t OtherCase(char32_t c) {
            if (c >= U'a' && c <= U'z') {
                return c - 32;
            }
            if (c >= U'A' && c <= U'Z') {
                return c + 32;
            }
            return c;
        }

    }

    char32_t FoldToBase(char32_t c) {
        if (c >= 0xC0 && c <= 0xFF) {
            const char base = kLatin1Base[c - 0xC0];
            return base ? static_cast<char32_t>(base) : c;
        }
        if (c >= 0x100 && c <= 0x17F) {
            return static_cast<char32_t>(kLatinExtABase[c - 0x100]);
        }
        switch (c) {
        case 0x00A0: // no-break space
        case 0x2009: // thin space
        case 0x202F: // narrow no-break space
            return U' ';
        case 0x2018: // single quotes, prime
        case 0x2019:
        case 0x201A:
        case 0x2032:
        case 0x00B4:
            return U'\'';
        case 0x201C: // double quotes
        case 0x201D:
        case 0x201E:
        case 0x2033:
        case 0x00AB:
        case 0x00BB:
            return U'"';
        case 0x2010: // hyphens and dashes
        case 0x2011:
        case 0x2012:
        case 0x2013:
        case 0x2014:
        case 0x2212:
            return U'-';
        case 0x2026: // ellipsis
            return U'.';
        default:
            return c;
        }
    }

    BitmapGlyphSource::BitmapGlyphSource(BitmapFontData data) : data_(std::move(data)) {
        if (data_.cellWidth <= 0.0f) {
            data_.cellWidth = 1.0f;
        }
        if (data_.cellHeight <= 0.0f) {
            data_.cellHeight = 1.0f;
        }
    }

    bool BitmapGlyphSource::HasGlyph(char32_t codePoint) const {
        return data_.glyphs.find(codePoint) != data_.glyphs.end();
    }

    const BitmapGlyph *BitmapGlyphSource::Resolve(char32_t codePoint) const {
        const char32_t candidates[4] = { codePoint, FoldToBase(codePoint),
                                         OtherCase(FoldToBase(codePoint)),
                                         OtherCase(codePoint) };
        for (char32_t candidate : candidates) {
            const auto found = data_.glyphs.find(candidate);
            if (found != data_.glyphs.end()) {
                return &found->second;
            }
        }
        return nullptr;
    }

    RndTex *BitmapGlyphSource::PageTexture(int page) const {
        if (page < 0 || page >= static_cast<int>(data_.pages.size())) {
            return nullptr;
        }
        return data_.pages[static_cast<std::size_t>(page)];
    }

    nativeui::FontMetrics BitmapGlyphSource::Metrics(float size) const {
        nativeui::FontMetrics metrics;
        metrics.ascent = (data_.baseline - data_.capTop) * size;
        metrics.descent = (data_.descenderBottom - data_.baseline) * size;
        metrics.lineHeight = size;
        return metrics;
    }

    float BitmapGlyphSource::Advance(float size, std::string_view utf8) const {
        return Walk(size, 0.0f, utf8, nullptr);
    }

    float BitmapGlyphSource::Layout(
        float size, float letterSpacing, std::string_view utf8, std::vector<GlyphQuad> &out
    ) const {
        return Walk(size, letterSpacing, utf8, &out);
    }

    // The one walk both Advance and Layout use, so they cannot disagree.
    float BitmapGlyphSource::Walk(
        float size, float letterSpacing, std::string_view utf8, std::vector<GlyphQuad> *out
    ) const {
        const float texel = size / data_.cellHeight; // logical units per atlas texel
        const float cell = data_.cellWidth * texel; // one cell width, the font's unit
        const float top = -data_.baseline * size; // cell top relative to the baseline

        float pen = 0.0f;
        char32_t previous = 0;
        bool first = true;
        for (std::size_t offset = 0; offset < utf8.size();) {
            const nativeui::utf8::Decoded decoded = nativeui::utf8::Decode(utf8, offset);
            if (decoded.length == 0) {
                break;
            }
            offset += decoded.length;
            const char32_t codePoint = decoded.codePoint;

            if (!first) {
                pen += letterSpacing;
            }
            first = false;

            const BitmapGlyph *glyph = Resolve(codePoint);
            if (!glyph) {
                // The placeholder: a hollow box standing on the baseline.
                const float advance = kPlaceholderAdvance * size;
                if (out) {
                    GlyphQuad quad;
                    const float inset = advance * 0.12f;
                    quad.x = pen + inset;
                    quad.w = advance - 2.0f * inset;
                    quad.h = std::min(kPlaceholderHeight, data_.baseline) * size;
                    quad.y = -quad.h;
                    quad.page = -1;
                    out->push_back(quad);
                }
                pen += advance;
                previous = 0;
                continue;
            }

            float kerning = data_.baseKerning;
            if (previous <= 0xFFFF && codePoint <= 0xFFFF && !data_.pairKerning.empty()) {
                const auto pair = data_.pairKerning.find(
                    static_cast<std::uint32_t>(previous)
                    | (static_cast<std::uint32_t>(codePoint) << 16)
                );
                if (pair != data_.pairKerning.end()) {
                    kerning += pair->second;
                }
            }
            pen += kerning * cell;

            const float advance = data_.monospace ? 1.0f : glyph->advance;
            const float width = glyph->width > 0.0f ? glyph->width : 0.0f;
            if (out && width > 0.0f) {
                GlyphQuad quad;
                const float centre =
                    data_.monospace ? std::max((advance - width) * 0.5f, 0.0f) : 0.0f;
                quad.x = pen + centre * cell;
                quad.y = top;
                quad.w = width * cell;
                quad.h = size;
                quad.u0 = glyph->u0;
                quad.v0 = glyph->v0;
                quad.u1 = glyph->u1;
                quad.v1 = glyph->v1;
                quad.page = glyph->page;
                out->push_back(quad);
            }
            pen += advance * cell;
            previous = codePoint;
        }
        return pen;
    }

}
