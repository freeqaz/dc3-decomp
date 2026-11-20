// Utf8: the framework's text is UTF-8 in std::string; these are the only
// functions that look inside it.
//
// Rules:
//  - Offsets are byte offsets that sit on code point boundaries.
//  - Malformed input never throws and never reads out of bounds: each bad byte
//    decodes as one U+FFFD.
//  - Upper-casing is the simple one-to-one mapping for ASCII, Latin-1 and
//    Latin Extended-A (song titles with accents), plus sharp s -> "SS".
//    Other scripts pass through unchanged.
#pragma once

#include <cstddef>
#include <string>
#include <string_view>

namespace nativeui::utf8 {

    inline constexpr char32_t kReplacement = 0xFFFD;

    struct Decoded {
        char32_t codePoint = kReplacement;
        std::size_t length = 0; // bytes consumed; 0 only at the end of the text
    };

    // Decodes the code point that starts at `offset`.
    Decoded Decode(std::string_view text, std::size_t offset);

    // Appends `codePoint` to `out`; an invalid scalar value appends U+FFFD.
    void Append(std::string &out, char32_t codePoint);

    // Number of code points in `text`.
    std::size_t Count(std::string_view text);

    // The boundary after / before the code point at `offset`, clamped to the text.
    std::size_t Next(std::string_view text, std::size_t offset);
    std::size_t Prev(std::string_view text, std::size_t offset);

    // Byte offset reached after `codePoints` code points (the text's size if fewer).
    std::size_t OffsetOf(std::string_view text, std::size_t codePoints);

    char32_t ToUpper(char32_t codePoint);
    std::string ToUpper(std::string_view text);

}
