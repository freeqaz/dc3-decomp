#include "rndobj/wordwrap.h"

unsigned int g_uOption;
unsigned short g_LineBreakTable[290];

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
