#include "rndobj\wordwrap.h"

unsigned int g_uOption = 1;
LineBreakEntry g_LineBreakTable[146];

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
