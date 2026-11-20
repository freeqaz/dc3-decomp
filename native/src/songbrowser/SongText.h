#pragma once
// Song browser data - text rules (docs/native/reference/SONG_BROWSER_DATA.md).
//
// Two things every part of the browser needs to agree on: how a title or an
// artist is ordered (the game's rule, from src/system/meta/Sorting.cpp), and
// how a string is folded for search.  Standard C++ only.
//
// Strings are UTF-8, which is what the game's songs.dta files are.

#include <cstddef>
#include <string>
#include <vector>

namespace songbrowser {

// One code point of `text` at byte `at`.  Returns its length in bytes (1..4).
// A byte that does not start a well-formed sequence is U+FFFD, length 1, so a
// walk over any byte string terminates and never reads past the end.
int DecodeUtf8(const std::string &text, size_t at, unsigned &codePoint);

// The lower-case ASCII letters an accented Latin letter stands for: "e" for
// U+00E9, "ss" for U+00DF, "ae" for U+00E6.  Covers Latin-1 Supplement and
// Latin Extended-A (U+00C0..U+017F).  nullptr for anything else.
const char *FoldLatinLetter(unsigned codePoint);

// ---------------------------------------------------------------------------
// Ordering: the game's AlphaKey rules
// ---------------------------------------------------------------------------

// The game's AlphaKeySkip: the byte offset of the part of `text` that is
// sorted on.  With `skipArticles` a leading "The ", "A " or "An " is skipped
// (the artist sort passes true, every title sort false); a "(" that is then
// first is skipped either way.
size_t AlphaKeySkip(const std::string &text, bool skipArticles);

// The key `text` is ordered by: the part after AlphaKeySkip, ASCII letters
// lower-cased, accented Latin letters folded to their base letters, every
// other byte kept.  Comparing two keys as unsigned bytes is the game's
// AlphaKeyStrCmp (strcasecmp after AlphaKeySkip) for ASCII strings.
std::string SortKey(const std::string &text, bool skipArticles);

// < 0, 0, > 0: SortKey(a) against SortKey(b), as unsigned bytes.
int CompareSortKeys(const std::string &a, const std::string &b);

// 0 when the sort key does not start with a letter ("2NE1", "50 Cent",
// "1, 2 Step"), else 1..26 for A..Z.  ArtistCmp::SortCharRank's rule, with
// an accented first letter counted as its base letter.
int SortCharRank(const std::string &text, bool skipArticles);

// "123" for rank 0, else the letter: ArtistCmp::SortCharLabel.
const char *SortCharLabel(int rank);

// ---------------------------------------------------------------------------
// Folding for search
// ---------------------------------------------------------------------------

// A string folded for search: only [a-z0-9] and single spaces between words,
// no space at either end.  Every folded character remembers the bytes of the
// original it came from, so a match can be shown in the original string.
struct FoldedText {
    std::string text;
    std::vector<int> begin; // per folded char: first byte of its source
    std::vector<int> end;   // ... and one past its last byte

    // Whether a word starts at folded position `at`.
    bool WordStartsAt(size_t at) const {
        return at < text.size() && text[at] != ' ' && (at == 0 || text[at - 1] == ' ');
    }
};

// Punctuation between letters either separates words or is dropped, and a
// person may type it either way, so every searched string is folded both
// ways:
//   kFoldSpaced  every run of punctuation is a word break: "will.i.am" ->
//                "will i am", "Ne-Yo" -> "ne yo", "Ain't" -> "ain t"
//   kFoldTight   punctuation is dropped, only white space breaks words:
//                "will.i.am" -> "william", "Ne-Yo" -> "neyo", "Ain't" -> "aint"
// In both: letters are lower-cased; accented Latin letters become their base
// letters; "&" is the word "and"; "$" next to a letter is "s" ("Ke$ha" ->
// "kesha"); "!" followed by a letter is "i" ("P!nk" -> "pink").
enum FoldMode { kFoldSpaced, kFoldTight };

FoldedText FoldForSearch(const std::string &text, FoldMode mode);

// The length of a leading "the " in a folded string (4), or 0.  A search
// treats the word after it as the start of the string too.
size_t LeadingThe(const std::string &folded);

} // namespace songbrowser
