#include "rndobj\wordwrap.h"

unsigned int g_uOption = 1;
// Restored from the shipped image: lbl_82F16AD0, .data, 145 entries of 4 bytes
// (ch:u16, cantBreakBefore:u8, cantBreakAfter:u8), strictly ascending by ch
// because both helpers binary-search it.
//
// The array is [146] while the image holds 145 entries. That is deliberate and
// matches the image's own behaviour: the search bound is `hi = 0x91` (145), and an
// exhaustive walk of the midpoint recurrence shows index 145 IS reachable -- for a
// char greater than every entry, the image reads four bytes past its own table.
// Declaring [145] here would reproduce that out-of-bounds read for real (and trip
// ASAN in the native port); the trailing zeroed entry has ch == 0, which no real
// character equals, so the probe falls through to `result = 0` exactly as before.
LineBreakEntry g_LineBreakTable[146] = {
    { 0x0021, 1, 0 },  // '!'
    { 0x0024, 0, 1 },  // '$'
    { 0x0025, 1, 0 },  // '%'
    { 0x0027, 1, 1 },  // "'"
    { 0x0028, 0, 1 },  // '('
    { 0x0029, 1, 0 },  // ')'
    { 0x002C, 1, 0 },  // ','
    { 0x002E, 1, 0 },  // '.'
    { 0x002F, 1, 1 },  // '/'
    { 0x003A, 1, 0 },  // ':'
    { 0x003B, 1, 0 },  // ';'
    { 0x003F, 1, 0 },  // '?'
    { 0x005B, 0, 1 },  // '['
    { 0x005C, 0, 1 },  // '\\'
    { 0x005D, 1, 0 },  // ']'
    { 0x007B, 0, 1 },  // '{'
    { 0x007D, 1, 0 },  // '}'
    { 0x00A2, 1, 0 },
    { 0x00A3, 0, 1 },
    { 0x00A5, 0, 1 },
    { 0x00A7, 0, 1 },
    { 0x00A8, 1, 0 },
    { 0x00A9, 1, 0 },
    { 0x00AE, 1, 0 },
    { 0x00B0, 1, 0 },
    { 0x00B7, 1, 1 },
    { 0x02C7, 1, 0 },
    { 0x02C9, 1, 0 },
    { 0x2013, 1, 0 },
    { 0x2014, 1, 0 },
    { 0x2015, 1, 0 },
    { 0x2016, 1, 0 },
    { 0x2018, 0, 1 },
    { 0x2019, 1, 0 },
    { 0x201C, 0, 1 },
    { 0x201D, 1, 0 },
    { 0x2022, 1, 0 },
    { 0x2025, 1, 0 },
    { 0x2026, 1, 0 },
    { 0x2027, 1, 0 },
    { 0x2032, 1, 0 },
    { 0x2033, 1, 0 },
    { 0x2035, 0, 1 },
    { 0x2103, 1, 0 },
    { 0x2122, 1, 0 },
    { 0x2236, 1, 0 },
    { 0x2574, 1, 0 },
    { 0x266F, 0, 1 },
    { 0x3001, 1, 0 },
    { 0x3002, 1, 0 },
    { 0x3003, 1, 0 },
    { 0x3005, 1, 0 },
    { 0x3008, 0, 1 },
    { 0x3009, 1, 0 },
    { 0x300A, 0, 1 },
    { 0x300B, 1, 0 },
    { 0x300C, 0, 1 },
    { 0x300D, 1, 0 },
    { 0x300E, 0, 1 },
    { 0x300F, 1, 0 },
    { 0x3010, 0, 1 },
    { 0x3011, 1, 0 },
    { 0x3012, 0, 1 },
    { 0x3014, 0, 1 },
    { 0x3015, 1, 0 },
    { 0x3016, 0, 1 },
    { 0x3017, 1, 0 },
    { 0x301D, 0, 1 },
    { 0x301E, 1, 0 },
    { 0x301F, 1, 0 },
    { 0x3041, 1, 0 },
    { 0x3043, 1, 0 },
    { 0x3045, 1, 0 },
    { 0x3047, 1, 0 },
    { 0x3049, 1, 0 },
    { 0x3063, 1, 0 },
    { 0x3083, 1, 0 },
    { 0x3085, 1, 0 },
    { 0x3087, 1, 0 },
    { 0x308E, 1, 0 },
    { 0x3099, 1, 0 },
    { 0x309A, 1, 0 },
    { 0x309B, 1, 0 },
    { 0x309C, 1, 0 },
    { 0x309D, 1, 0 },
    { 0x309E, 1, 0 },
    { 0x30A1, 1, 0 },
    { 0x30A3, 1, 0 },
    { 0x30A5, 1, 0 },
    { 0x30A7, 1, 0 },
    { 0x30A9, 1, 0 },
    { 0x30C3, 1, 0 },
    { 0x30E3, 1, 0 },
    { 0x30E5, 1, 0 },
    { 0x30E7, 1, 0 },
    { 0x30EE, 1, 0 },
    { 0x30F5, 1, 0 },
    { 0x30F6, 1, 0 },
    { 0x30FB, 1, 0 },
    { 0x30FC, 1, 0 },
    { 0x30FD, 1, 0 },
    { 0x30FE, 1, 0 },
    { 0xFE30, 1, 0 },
    { 0xFE50, 1, 0 },
    { 0xFE51, 1, 0 },
    { 0xFE52, 1, 0 },
    { 0xFE54, 1, 0 },
    { 0xFE55, 1, 0 },
    { 0xFE56, 1, 0 },
    { 0xFE57, 1, 0 },
    { 0xFE59, 0, 1 },
    { 0xFE5A, 1, 0 },
    { 0xFE5B, 0, 1 },
    { 0xFE5C, 1, 0 },
    { 0xFE5D, 0, 1 },
    { 0xFE5E, 1, 0 },
    { 0xFF01, 1, 0 },
    { 0xFF02, 1, 0 },
    { 0xFF04, 0, 1 },
    { 0xFF05, 1, 0 },
    { 0xFF07, 1, 0 },
    { 0xFF08, 0, 1 },
    { 0xFF09, 1, 0 },
    { 0xFF0C, 1, 0 },
    { 0xFF0E, 1, 0 },
    { 0xFF1A, 1, 0 },
    { 0xFF1B, 1, 0 },
    { 0xFF1F, 1, 0 },
    { 0xFF20, 0, 1 },
    { 0xFF3B, 0, 1 },
    { 0xFF3D, 1, 0 },
    { 0xFF40, 1, 0 },
    { 0xFF5B, 0, 1 },
    { 0xFF5C, 1, 0 },
    { 0xFF5D, 1, 0 },
    { 0xFF5E, 1, 0 },
    { 0xFF61, 1, 0 },
    { 0xFF64, 1, 0 },
    { 0xFF70, 1, 0 },
    { 0xFF9E, 1, 0 },
    { 0xFF9F, 1, 0 },
    { 0xFFE0, 1, 1 },
    { 0xFFE1, 0, 1 },
    { 0xFFE5, 0, 1 },
    { 0xFFE6, 0, 1 },
};

void WordWrap_SetOption(unsigned int option) { g_uOption = option; }

bool IsEastAsianChar(wchar_t ch) {
    if (g_uOption & 4) {
        if ((ch >= 0x1100 && ch <= 0x11FF)
            || (ch >= 0x3130 && ch <= 0x318F)
            || (ch >= 0xAC00 && ch <= 0xD7A3)) {
            return false;
        }
    }
        return (ch >= 0x1100 && ch <= 0x11FF)
        || (ch >= 0x3000 && ch <= 0xD7AF)
        || (ch >= 0xF900 && ch <= 0xFAFF)
        || (ch >= 0xFF00 && ch <= 0xFFDC);
}

// Hypothesis under test (wave 7, lane w7-y): the three binary searches are one
// pair of inlined bool-returning helpers, which is what gives the image its
// masked tail (`li r11, 0/1` + `clrlwi r3, r11, 24`) while plain `return false;`
// stays an unmasked `li r3, 0` in its own block.
static bool CantStartLine(wchar_t c, unsigned int option) {
    unsigned char result;
    if (option & 1) {
        int lo = 0, hi = 0x91;
        do {
            int mid = (hi - lo) / 2 + lo;
            if (c == g_LineBreakTable[mid].ch) {
                result = g_LineBreakTable[mid].cantBreakBefore;
                goto done;
            }
            if ((unsigned short)c < (unsigned short)g_LineBreakTable[mid].ch)
                hi = mid - 1;
            else
                lo = mid + 1;
        } while (lo <= hi);
    }
    result = 0;
done:
    return result != 0;
}

static bool CantEndLine(wchar_t c, unsigned int option) {
    unsigned char result;
    if (option & 1) {
        int lo = 0, hi = 0x91;
        do {
            int mid = (hi - lo) / 2 + lo;
            if (c == g_LineBreakTable[mid].ch) {
                result = g_LineBreakTable[mid].cantBreakAfter;
                goto done;
            }
            if ((unsigned short)c < (unsigned short)g_LineBreakTable[mid].ch)
                hi = mid - 1;
            else
                lo = mid + 1;
        } while (lo <= hi);
    }
    result = 0;
done:
    return result != 0;
}

bool WordWrap_CanBreakLineAt(const wchar_t *cur, const wchar_t *start) {
    if (cur == start)
        return false;

    // Attempt 2, INERT (wave 7, lane w7-y): reading g_uOption before the
    // character -- the image's `lis r10, g_uOption@h` sits one slot ahead of its
    // `lhz r31, 0x0(r3)` -- changes nothing, 97.60 either way.  The residual is
    // that the image loads the character STRAIGHT into its callee-saved register
    // and leaves `cur` in r3 for the function's whole life, while we copy `cur`
    // into r5 first (`mr r5, r3`, our only insert) and stage the character
    // through r3.  Nothing in the statement order reaches that choice.
    wchar_t ch = *cur;
    unsigned int option = g_uOption;

    // If current char is whitespace, check if next char can't start a line
    if (ch == 0x9 || ch == 0xD || ch == 0x20 || ch == 0x3000) {
        if (CantStartLine(cur[1], option))
            return false;
    }

    // Quote handling — guard against cur[-2] when too close to start.
    // Original check uses byte distance <= 2, assuming 2-byte wchar_t (PPC/Xbox).
    // On Linux wchar_t is 4 bytes, so use element distance instead.
#ifdef HX_NATIVE
    if ((cur - start) <= 1
#else
    if ((int)(((unsigned int)((char *)cur - (char *)start)) & 0xFFFFFFFEu) <= 2
#endif
        || (cur[-2] != 0x9 && cur[-2] != 0xD && cur[-2] != 0x20 && cur[-2] != 0x3000)
        || cur[-1] != 0x22
        || ch == 0x9 || ch == 0xD || ch == 0x20 || ch == 0x3000) {
        // ok
    } else {
        return false;
    }

    wchar_t prev = cur[-1];
    if (prev == 0x9 || prev == 0xD || prev == 0x20 || prev == 0x3000
        || ch != 0x22
        || (cur[1] != 0x9 && cur[1] != 0xD && cur[1] != 0x20 && cur[1] != 0x3000)) {
        // ok
    } else {
        return false;
    }

    // Check if this is a valid break position
    if (ch == 0x9 || ch == 0xD || ch == 0x20 || ch == 0x3000
        || IsEastAsianChar(ch) || IsEastAsianChar(prev) || prev == 0x2D) {
        return !CantStartLine(ch, option) && !CantEndLine(prev, option);
    }

    return false;
}
