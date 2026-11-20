// Song browser data - text rules.  See SongText.h.

#include "SongText.h"

namespace songbrowser {

int DecodeUtf8(const std::string &text, size_t at, unsigned &codePoint) {
    const size_t size = text.size();
    unsigned char lead = (unsigned char)text[at];
    if (lead < 0x80) {
        codePoint = lead;
        return 1;
    }
    int length = 0;
    unsigned value = 0;
    if (lead >= 0xC2 && lead <= 0xDF) {
        length = 2;
        value = lead & 0x1F;
    } else if (lead >= 0xE0 && lead <= 0xEF) {
        length = 3;
        value = lead & 0x0F;
    } else if (lead >= 0xF0 && lead <= 0xF4) {
        length = 4;
        value = lead & 0x07;
    }
    if (length == 0 || at + length > size) {
        codePoint = 0xFFFD;
        return 1;
    }
    for (int i = 1; i < length; i++) {
        unsigned char next = (unsigned char)text[at + i];
        if ((next & 0xC0) != 0x80) {
            codePoint = 0xFFFD;
            return 1;
        }
        value = (value << 6) | (next & 0x3F);
    }
    codePoint = value;
    return length;
}

const char *FoldLatinLetter(unsigned cp) {
    // Latin-1 Supplement, U+00C0..U+00FF.  The two signs in the range (U+00D7
    // multiplication, U+00F7 division) are not letters.
    static const char *const kLatin1[64] = {
        "a", "a", "a", "a", "a", "a", "ae", "c", "e", "e", "e", "e", "i", "i", "i", "i",
        "d", "n", "o", "o", "o", "o", "o", nullptr, "o", "u", "u", "u", "u", "y", "th", "ss",
        "a", "a", "a", "a", "a", "a", "ae", "c", "e", "e", "e", "e", "i", "i", "i", "i",
        "d", "n", "o", "o", "o", "o", "o", nullptr, "o", "u", "u", "u", "u", "y", "th", "y",
    };
    if (cp >= 0xC0 && cp <= 0xFF) return kLatin1[cp - 0xC0];
    // Latin Extended-A, U+0100..U+017F.
    static const char *const kExtendedA[128] = {
        "a", "a", "a", "a", "a", "a", "c", "c", "c", "c", "c", "c", "c", "c", "d", "d",
        "d", "d", "e", "e", "e", "e", "e", "e", "e", "e", "e", "e", "g", "g", "g", "g",
        "g", "g", "g", "g", "h", "h", "h", "h", "i", "i", "i", "i", "i", "i", "i", "i",
        "i", "i", "ij", "ij", "j", "j", "k", "k", "k", "l", "l", "l", "l", "l", "l", "l",
        "l", "l", "l", "n", "n", "n", "n", "n", "n", "n", "n", "n", "o", "o", "o", "o",
        "o", "o", "oe", "oe", "r", "r", "r", "r", "r", "r", "s", "s", "s", "s", "s", "s",
        "s", "s", "t", "t", "t", "t", "t", "t", "u", "u", "u", "u", "u", "u", "u", "u",
        "u", "u", "u", "u", "w", "w", "y", "y", "y", "z", "z", "z", "z", "z", "z", "s",
    };
    if (cp >= 0x100 && cp <= 0x17F) return kExtendedA[cp - 0x100];
    return nullptr;
}

// ---------------------------------------------------------------------------
// Ordering
// ---------------------------------------------------------------------------

namespace {

bool IsAsciiLetter(unsigned char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }
bool IsAsciiDigit(unsigned char c) { return c >= '0' && c <= '9'; }
char AsciiLower(unsigned char c) { return c >= 'A' && c <= 'Z' ? (char)(c + 32) : (char)c; }

bool IsSpace(unsigned char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v';
}

} // namespace

// Sorting.cpp's AlphaKeySkip, including its length tests ("The " is only
// skipped from a string longer than four bytes, so a band called "The " is
// not an empty key).
size_t AlphaKeySkip(const std::string &text, bool skipArticles) {
    size_t at = 0;
    size_t length = text.size();
    if (skipArticles) {
        if (length > 4 && AsciiLower(text[0]) == 't' && AsciiLower(text[1]) == 'h'
            && AsciiLower(text[2]) == 'e' && text[3] == ' ') {
            at = 4;
        } else if (length > 2 && AsciiLower(text[0]) == 'a' && text[1] == ' ') {
            at = 2;
        } else if (length > 3 && AsciiLower(text[0]) == 'a' && AsciiLower(text[1]) == 'n'
                   && text[2] == ' ') {
            at = 3;
        }
    }
    if (at < length && text[at] == '(') at++;
    return at;
}

std::string SortKey(const std::string &text, bool skipArticles) {
    std::string key;
    size_t at = AlphaKeySkip(text, skipArticles);
    while (at < text.size()) {
        unsigned cp;
        int length = DecodeUtf8(text, at, cp);
        const char *folded = cp >= 0x80 ? FoldLatinLetter(cp) : nullptr;
        if (folded) {
            key += folded;
        } else if (cp < 0x80) {
            key += AsciiLower((unsigned char)cp);
        } else {
            key.append(text, at, length);
        }
        at += length;
    }
    return key;
}

int CompareSortKeys(const std::string &a, const std::string &b) {
    size_t common = a.size() < b.size() ? a.size() : b.size();
    for (size_t i = 0; i < common; i++) {
        unsigned char x = (unsigned char)a[i];
        unsigned char y = (unsigned char)b[i];
        if (x != y) return x < y ? -1 : 1;
    }
    if (a.size() == b.size()) return 0;
    return a.size() < b.size() ? -1 : 1;
}

int SortCharRank(const std::string &text, bool skipArticles) {
    std::string key = SortKey(text, skipArticles);
    if (key.empty()) return 0;
    unsigned char first = (unsigned char)key[0];
    if (first >= 'a' && first <= 'z') return first - 'a' + 1;
    return 0;
}

const char *SortCharLabel(int rank) {
    static const char *const kLabels[] = { "123", "A", "B", "C", "D", "E", "F", "G", "H",
                                           "I",   "J", "K", "L", "M", "N", "O", "P", "Q",
                                           "R",   "S", "T", "U", "V", "W", "X", "Y", "Z" };
    if (rank < 0) rank = 0;
    if (rank > 26) rank = 26;
    return kLabels[rank];
}

// ---------------------------------------------------------------------------
// Folding for search
// ---------------------------------------------------------------------------

namespace {

// What one code point of the original is, for folding.
enum CharKind {
    kCharText,  // contributes folded characters
    kCharSpace, // white space: a word break in both modes
    kCharPunct, // a word break when spaced, nothing when tight
    kCharAnd,   // "&": the word "and"
};

struct SourceChar {
    CharKind kind;
    const char *folded; // kCharText: one or more of [a-z0-9]
    char single[2];     // storage for a one-character fold
    int begin;
    int end;
};

void Append(FoldedText &out, char c, int begin, int end) {
    out.text += c;
    out.begin.push_back(begin);
    out.end.push_back(end);
}

// A pending word break becomes one space, but never at the start.
void Break(FoldedText &out, bool &pendingBreak, int at) {
    if (pendingBreak && !out.text.empty()) Append(out, ' ', at, at);
    pendingBreak = false;
}

} // namespace

namespace {

bool IsDot(const std::string &text, const std::vector<SourceChar> &chars, size_t i) {
    return i < chars.size() && chars[i].kind == kCharPunct && chars[i].end - chars[i].begin == 1
        && text[chars[i].begin] == '.';
}

// Whether the space at `i` stands between two initials: a single letter and
// its dot before it, a single letter and its dot after it.
bool BetweenInitials(const std::string &text, const std::vector<SourceChar> &chars, size_t i) {
    size_t before = i;
    while (before > 0 && chars[before].kind == kCharSpace) {
        before--;
    }
    if (!IsDot(text, chars, before) || before == 0 || chars[before - 1].kind != kCharText) {
        return false;
    }
    if (before >= 2 && chars[before - 2].kind == kCharText) return false; // a longer word
    size_t after = i;
    while (after < chars.size() && chars[after].kind == kCharSpace) {
        after++;
    }
    return after < chars.size() && chars[after].kind == kCharText && IsDot(text, chars, after + 1);
}

} // namespace

FoldedText FoldForSearch(const std::string &text, FoldMode mode) {
    // First pass: classify every code point.
    std::vector<SourceChar> chars;
    size_t at = 0;
    while (at < text.size()) {
        unsigned cp;
        int length = DecodeUtf8(text, at, cp);
        SourceChar c;
        c.kind = kCharPunct;
        c.folded = nullptr;
        c.single[0] = c.single[1] = 0;
        c.begin = (int)at;
        c.end = (int)at + length;
        if (cp < 0x80) {
            unsigned char byte = (unsigned char)cp;
            if (IsAsciiLetter(byte) || IsAsciiDigit(byte)) {
                c.kind = kCharText;
                c.single[0] = AsciiLower(byte);
            } else if (IsSpace(byte)) {
                c.kind = kCharSpace;
            } else if (byte == '&') {
                c.kind = kCharAnd;
            }
        } else if (const char *folded = FoldLatinLetter(cp)) {
            c.kind = kCharText;
            c.folded = folded;
        } else if (cp == 0xA0) {
            c.kind = kCharSpace; // no-break space
        }
        chars.push_back(c);
        at += length;
    }
    // "$" beside a letter is an s, "!" before a letter is an i.
    for (size_t i = 0; i < chars.size(); i++) {
        if (chars[i].kind != kCharPunct || chars[i].end - chars[i].begin != 1) continue;
        char byte = text[chars[i].begin];
        bool letterBefore = i > 0 && chars[i - 1].kind == kCharText
            && !IsAsciiDigit((unsigned char)chars[i - 1].single[0]);
        bool letterAfter = i + 1 < chars.size() && chars[i + 1].kind == kCharText
            && !IsAsciiDigit((unsigned char)chars[i + 1].single[0]);
        if (byte == '$' && (letterBefore || letterAfter)) {
            chars[i].kind = kCharText;
            chars[i].single[0] = 's';
        } else if (byte == '!' && letterAfter) {
            chars[i].kind = kCharText;
            chars[i].single[0] = 'i';
        }
    }

    FoldedText out;
    bool pendingBreak = false;
    for (size_t i = 0; i < chars.size(); i++) {
        const SourceChar &c = chars[i];
        switch (c.kind) {
        case kCharText: {
            Break(out, pendingBreak, c.begin);
            const char *folded = c.folded ? c.folded : c.single;
            for (const char *p = folded; *p; p++) {
                Append(out, *p, c.begin, c.end);
            }
            break;
        }
        case kCharSpace:
            // Initials written apart are one word when squeezed:
            // "J. J. Fad" is "jj fad", as "E.U." is "eu".
            if (mode == kFoldTight && BetweenInitials(text, chars, i)) break;
            pendingBreak = true;
            break;
        case kCharPunct:
            if (mode == kFoldSpaced) pendingBreak = true;
            break;
        case kCharAnd:
            pendingBreak = true;
            Break(out, pendingBreak, c.begin);
            Append(out, 'a', c.begin, c.end);
            Append(out, 'n', c.begin, c.end);
            Append(out, 'd', c.begin, c.end);
            pendingBreak = true;
            break;
        }
    }
    return out;
}

size_t LeadingThe(const std::string &folded) {
    return folded.size() > 4 && folded.compare(0, 4, "the ") == 0 ? 4 : 0;
}

} // namespace songbrowser
