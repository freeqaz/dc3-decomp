#include "nativeui/Utf8.h"

#include <algorithm>

namespace nativeui::utf8 {

    namespace {

        bool IsContinuation(char c) {
            return (static_cast<unsigned char>(c) & 0xC0) == 0x80;
        }

        bool IsScalarValue(char32_t cp) {
            return cp <= 0x10FFFF && !(cp >= 0xD800 && cp <= 0xDFFF);
        }

    }

    Decoded Decode(std::string_view text, std::size_t offset) {
        if (offset >= text.size()) {
            return { kReplacement, 0 };
        }
        const unsigned char lead = static_cast<unsigned char>(text[offset]);
        if (lead < 0x80) {
            return { lead, 1 };
        }
        std::size_t length = 0;
        char32_t codePoint = 0;
        char32_t smallest = 0; // rejects overlong encodings
        if ((lead & 0xE0) == 0xC0) {
            length = 2;
            codePoint = lead & 0x1F;
            smallest = 0x80;
        } else if ((lead & 0xF0) == 0xE0) {
            length = 3;
            codePoint = lead & 0x0F;
            smallest = 0x800;
        } else if ((lead & 0xF8) == 0xF0) {
            length = 4;
            codePoint = lead & 0x07;
            smallest = 0x10000;
        } else {
            return { kReplacement, 1 };
        }
        if (offset + length > text.size()) {
            return { kReplacement, 1 };
        }
        for (std::size_t i = 1; i < length; ++i) {
            const char c = text[offset + i];
            if (!IsContinuation(c)) {
                return { kReplacement, 1 };
            }
            codePoint = (codePoint << 6) | (static_cast<unsigned char>(c) & 0x3F);
        }
        if (codePoint < smallest || !IsScalarValue(codePoint)) {
            return { kReplacement, 1 };
        }
        return { codePoint, length };
    }

    void Append(std::string &out, char32_t codePoint) {
        if (!IsScalarValue(codePoint)) {
            codePoint = kReplacement;
        }
        if (codePoint < 0x80) {
            out.push_back(static_cast<char>(codePoint));
        } else if (codePoint < 0x800) {
            out.push_back(static_cast<char>(0xC0 | (codePoint >> 6)));
            out.push_back(static_cast<char>(0x80 | (codePoint & 0x3F)));
        } else if (codePoint < 0x10000) {
            out.push_back(static_cast<char>(0xE0 | (codePoint >> 12)));
            out.push_back(static_cast<char>(0x80 | ((codePoint >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (codePoint & 0x3F)));
        } else {
            out.push_back(static_cast<char>(0xF0 | (codePoint >> 18)));
            out.push_back(static_cast<char>(0x80 | ((codePoint >> 12) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | ((codePoint >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (codePoint & 0x3F)));
        }
    }

    std::size_t Count(std::string_view text) {
        std::size_t count = 0;
        for (std::size_t offset = 0; offset < text.size(); offset = Next(text, offset)) {
            ++count;
        }
        return count;
    }

    std::size_t Next(std::string_view text, std::size_t offset) {
        if (offset >= text.size()) {
            return text.size();
        }
        return offset + Decode(text, offset).length;
    }

    std::size_t Prev(std::string_view text, std::size_t offset) {
        offset = std::min(offset, text.size());
        if (offset == 0) {
            return 0;
        }
        // Walk back over at most three continuation bytes to a candidate lead
        // byte, and accept it only if the sequence it starts ends exactly here.
        std::size_t start = offset - 1;
        const std::size_t earliest = offset >= 4 ? offset - 4 : 0;
        while (start > earliest && IsContinuation(text[start])) {
            --start;
        }
        if (start + Decode(text, start).length == offset) {
            return start;
        }
        return offset - 1;
    }

    std::size_t OffsetOf(std::string_view text, std::size_t codePoints) {
        std::size_t offset = 0;
        for (std::size_t i = 0; i < codePoints && offset < text.size(); ++i) {
            offset = Next(text, offset);
        }
        return offset;
    }

    char32_t ToUpper(char32_t cp) {
        if (cp >= U'a' && cp <= U'z') {
            return cp - 0x20;
        }
        if (cp < 0xE0) {
            return cp;
        }
        // Latin-1: a-grave .. thorn, except the division sign.
        if (cp <= 0xFE) {
            return cp == 0xF7 ? cp : cp - 0x20;
        }
        if (cp == 0xFF) {
            return 0x178; // y-diaeresis: its capital lives in Latin Extended-A
        }
        if (cp == 0x131) {
            return U'I'; // dotless i
        }
        // Latin Extended-A: capital/small pairs, the capital first. The pairs
        // start on an even code point in two runs and on an odd one in two.
        const bool evenPairs =
            (cp >= 0x100 && cp <= 0x137) || (cp >= 0x14A && cp <= 0x177);
        const bool oddPairs =
            (cp >= 0x139 && cp <= 0x148) || (cp >= 0x179 && cp <= 0x17E);
        if (evenPairs && (cp & 1) == 1) {
            return cp - 1;
        }
        if (oddPairs && (cp & 1) == 0) {
            return cp - 1;
        }
        return cp;
    }

    std::string ToUpper(std::string_view text) {
        std::string out;
        out.reserve(text.size());
        for (std::size_t offset = 0; offset < text.size();) {
            const Decoded d = Decode(text, offset);
            if (d.codePoint == 0xDF) {
                out += "SS"; // sharp s has no single capital the game's font carries
            } else {
                Append(out, ToUpper(d.codePoint));
            }
            offset += d.length;
        }
        return out;
    }

}
