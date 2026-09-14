#include "utl\UTF8.h"
#include "utl\Str.h"
#include "os\Debug.h"
#include <cstring>

unsigned short WToLower(unsigned short us) {
    unsigned short ret = us;
    // Latin uppercase (A-Z) and Latin-1 Supplement (À-Þ)
    if ((us >= 0x41 && us <= 0x5A) || (us >= 0xC0 && us <= 0xDE)) {
        ret = us + 0x20;
    // Latin Extended-A (Ā-ž) odd/even patterns
    } else if ((us >= 0x100 && us <= 0x137 && (us & 1) == 0)
               || (us >= 0x139 && us <= 0x148 && (us & 1) == 1)
               || (us >= 0x14A && us <= 0x177 && (us & 1) == 0)
               || (us >= 0x179 && us <= 0x17E && (us & 1) == 1)) {
        ret = us + 1;
    } else if (us == 0x178) {
        ret = 0xFF;
    }
    return ret;
}

unsigned short WToUpper(unsigned short us) {
    unsigned short result;
    // Latin lowercase (a-z) and Latin-1 Supplement (à-þ)
    if ((us >= 0x61 && us <= 0x7A) || (us >= 0xE0 && us <= 0xFE)) {
        result = us - 0x20;
    // Latin Extended-A (Ā-ž) odd/even patterns
    } else if ((us >= 0x100 && us <= 0x137 && (us & 1) == 1)
               || (us >= 0x139 && us <= 0x148 && (us & 1) == 0)
               || (us >= 0x14A && us <= 0x177 && (us & 1) == 1)
               || (us >= 0x179 && us <= 0x17E && (us & 1) == 0)) {
        result = us - 1;
    } else {
        // Special case: ÿ (0xFF) -> Ÿ (0x178)
        if (us == 0xFF)
            return 0x178;
        result = us;
    }
    return result;
}

int WStrniCmp(const unsigned short *str1, const unsigned short *str2, int n) {
    // The p1/p2 copies are load-bearing, not redundant: mutating the parameters
    // directly makes MSVC evaluate WToLower(*str2) FIRST, while the image calls
    // it on *p1 first (`lhz r3, 0x0(r9)` / `bl` at 0x827E155C, before the
    // `lhzx` of the second string).  Measured: 84.1 vs 89.66 canonical.
    const unsigned short *p1 = str1;
    const unsigned short *p2 = str2;
    for (; n != 0; n--) {
        unsigned short char1 = WToLower(*p1);
        // RESIDUAL (w7-az, 89.66, 3 rows): the image loads *p2 into a scratch
        // (`lhzx r11, r8, r9`), saves char1 out of r3, and only then does
        // `mr r3, r11` -- 0x827E1564-0x827E156C.  We load straight into r3 and
        // skip both moves.  Interposing `unsigned short raw2 = *p2;` is
        // byte-inert: MSVC folds the temp.
        unsigned short char2 = WToLower(*p2);
        p1++;
        p2++;
        if (char1 != char2) {
            return char1 - char2;
        }
        if (char1 == 0)
            break;
    }
    return 0;
}

unsigned int DecodeUTF8(unsigned short &us, const char *str) {
    unsigned char uc = str[0];
    if (uc <= 0x7FU) {
        us = uc;
        return 1;
    }

    unsigned char uc1 = str[1];
    if (uc >= 0xC0 && uc <= 0xDF) {
        us = ((uc - 0xC0) * 0x40) + uc1 - 0x80;
        return 2;
    }

    unsigned char uc2 = str[2];
    if (uc >= 0xE0 && uc <= 0xEF) {
        us = ((uc - 0xE0) * 0x40 + (uc1 - 0x80)) * 0x40 + (uc2 - 0x80);
        return 3;
    }

    if (uc >= 0xF0 && uc <= 0xFD) {
        MILO_NOTIFY("HMX wide chars cannot exceed 16 bits: %s (0x%02x)", str, uc);
        us = 0x2A;
        return 1;
    }

    MILO_NOTIFY("Invalid UTF character: %s (0x%02x)", str, uc);
    us = 0x2A;
    return 1;
}

unsigned int EncodeUTF8(String &out, unsigned int in) {
    if (in < 0x80) {
        out.resize(2);
        out[0] = in & 0xFF;
        out[1] = 0;
        return 1;
    } else if (in <= 0x7FF) {
        out.resize(3);
        out[0] = (in >> 6) + 0xC0;
        out[1] = (in & 0x3F) + 0x80;
        out[2] = 0;
        return 2;
    } else if (in <= 0xFFFF) {
        out.resize(4);
        out[0] = (in >> 0xC) + 0xE0;
        out[1] = ((in >> 6) & 0x3F) + 0x80;
        out[2] = (in & 0x3F) + 0x80;
        out[3] = 0;
        return 3;
    } else if (in <= 0x7FFFFFFF) {
        MILO_NOTIFY("HMX wide chars cannot exceed 16 bits: %d (0x%02x)", in, in);
        out = "*";
        return 1;
    } else {
        MILO_NOTIFY("Invalid UTF character: %d (0x%02x)", in, in);
        out = "*";
        return 1;
    }
}

void UTF8toASCIIs(char *out, int len, const char *in, char sub) {
    unsigned short us;
    MILO_ASSERT(out, 0x5E);
    MILO_ASSERT(in, 0x5F);
    MILO_ASSERT(len > 0, 0x60);
    int i = 0;
    char *p = out;
    for (; *in != 0 && i < len - 1; i++) {
        in += DecodeUTF8(us, in);
        if (us < 0x100)
            *p++ = us;
        else
            *p++ = sub;
    }
    *p = '\0';
}

void ASCIItoUTF8(char *out, int len, const char *in) {
    MILO_ASSERT(out, 0x75);
    MILO_ASSERT(in, 0x76);
    MILO_ASSERT(len > 0, 0x77);
    memset(out, 0, len);
    String str;
    char *p = out;
    // A named `char` loop variable, not an index: the image loads the
    // character ONCE per iteration (`lbz r11, 0x0(r25)` on entry, `lbzu r11,
    // 0x1(r25)` at the bottom), homes it in its own stack slot at 0x50 -- which
    // is what pushes the MILO_ASSERT line slot to 0x54 -- and tests it with
    // `extsb. r10, r11`, a SIGNED test.  Indexing re-loaded `in[i]` for the
    // EncodeUTF8 argument and tested the zero-extended byte with `cmplwi`.
    // Residual (99.0%, 7 rows, 4 B): the image also HOMES that char in its own
    // stack slot -- `stb r11, 0x50(r31)` at 827E1B54 -- and never reads the
    // slot back anywhere in the function (grepped the whole .fn: 0x50 appears
    // once, as that store).  The dead home is what pushes the three MILO_ASSERT
    // scratch ints from 0x50 to 0x54, so all 6 remaining `off:-4` rows are
    // consequences of that one store, not independent faults.
    // REFUTED: hoisting the declaration to `char c;` before the loop and
    // assigning in the for-init is byte-for-byte inert (99.0%, same 7 rows).
    for (char c = *in; c != '\0'; c = *++in) {
        int utf8 = EncodeUTF8(str, (unsigned char)c);
        if ((p - out) + utf8 >= (unsigned int)len) {
            return;
        }
        for (int j = 0; j < str.length(); j++) {
            *p++ = str.c_str()[j];
        }
    }
}

const char *WideCharToChar(const unsigned short *us) {
    if (us == 0)
        return 0;
    else {
        static std::vector<char> cstring;
        cstring.clear();
        for (; *us != 0; us++) {
            if (*us > 0xFF)
                cstring.push_back('*');
            else
                cstring.push_back(*us);
        }
        cstring.push_back('\0');
        return &cstring[0];
    }
}

unsigned int UTF8StrLen(const char *str) {
    unsigned short us;
    const char *p = str;
    unsigned int len = 0;
    while (*p != '\0') {
        us = 0;
        len++;
        p += DecodeUTF8(us, p);
    }
    return len;
}

const char *UTF8strchr(const char *str, unsigned short us) {
    unsigned short us_loc;
    int decoded;

    while (*str) {
        us_loc = 0;
        decoded = DecodeUTF8(us_loc, str);
        if (us_loc == us)
            return str;
        str += decoded;
    }
    return 0;
}

void UTF8ToLower(unsigned short arg0, char *arg1) {
    int middleByte;
    int codepoint;

    codepoint = arg0;
    if (codepoint < 0x80U) {
        if ((unsigned short)(codepoint + 0xFFBF) <= 0x19U) {
            arg1[0] = (codepoint + 0x20);
        } else
            arg1[0] = codepoint;
    } else if (codepoint < 0x800U) {
        if ((unsigned short)(codepoint + 0xFF40) <= 0x1DU) {
            codepoint = (codepoint + 0x20) & 0xffff;
        }
        arg1[0] = (((codepoint >> 6) & 0x3FF) + 0xC0);
        arg1[1] = ((codepoint % 64) + 0x80);
    } else {
        middleByte = (codepoint >> 6) & 0x3FF;
        arg1[0] = (((codepoint >> 0xCU) & 0xF) + 0xE0);
        arg1[1] = ((middleByte % 64) + 0x80);
        arg1[2] = ((codepoint % 64) + 0x80);
    }
}

void UTF8ToUpper(unsigned short arg0, char *arg1) {
    int middleByte;
    int codepoint;

    codepoint = arg0;
    if (codepoint < 0x80U) {
        if ((unsigned short)(codepoint + 0xFF9F) <= 0x19U) {
            arg1[0] = (codepoint - 0x20);
        } else
            arg1[0] = codepoint;
    } else if (codepoint < 0x800U) {
        if ((unsigned short)(codepoint + 0xFF20) <= 0x1DU) {
            codepoint = (codepoint - 0x20) & 0xffff;
        }
        arg1[0] = (((codepoint >> 6) & 0x3FF) + 0xC0);
        arg1[1] = ((codepoint % 64) + 0x80);
    } else {
        middleByte = (codepoint >> 6) & 0x3FF;
        arg1[0] = (((codepoint >> 0xCU) & 0xF) + 0xE0);
        arg1[1] = ((middleByte % 64) + 0x80);
        arg1[2] = ((codepoint % 64) + 0x80);
    }
}

void UTF8FilterString(char *out, int len, const char *in, const char *allowed, char sub) {
    MILO_ASSERT(out, 0x18F);
    MILO_ASSERT(in, 0x190);
    MILO_ASSERT(len > 0, 0x191);
    MILO_ASSERT(allowed, 0x192);
    unsigned short us;
    int decoded;
    char *out_beg = out;

    while ((*in != 0) && (out - out_beg < len - 3)) {
        us = 0;
        decoded = DecodeUTF8(us, in);
        if (UTF8strchr(allowed, us) != 0) {
            for (unsigned int i = 0; i < decoded; i++)
                *out++ = *in++;
        } else {
            *out++ = sub;
            in += decoded;
        }
    }
    MILO_ASSERT((out - out_beg) < len, 0x1A5);
    *out = 0;
}

void UTF8RemoveSpaces(char *out, int len, const char *in) {
    MILO_ASSERT(out, 0x1AD);
    MILO_ASSERT(in, 0x1AE);
    MILO_ASSERT(len > 0, 0x1AF);
    unsigned short us;
    int unk;
    char *out_begin = out;
    while ((*in != 0) && (out - out_begin < len - 3)) {
        unk = DecodeUTF8(us, in);
        if (UTF8strchr(in, us) != (char *)32) {
            if (unk << 3)
                *out++ = *in++;
        } else {
            *out = '\0';
        }
    }
    *out = '\0';
}

void UTF8toWChar_t(wchar_t *wc, const char *c) {
    unsigned short *us = (unsigned short *)wc;
    while (*c != '\0') {
        c = c + DecodeUTF8(*us, c);
        us++;
    }
    *us = 0;
}

void UTF8toUTF16(unsigned short *us, const char *c) {
    while (*c != '\0') {
        c = c + DecodeUTF8(*us, c);
        us++;
    }
    *us = 0;
}

int UTF16toUTF8(char *c, const unsigned short *us) {
    int ctr = 0;
    while (*us != 0) {
        String sp8;
        int byteLen = EncodeUTF8(sp8, *us);
        ctr += byteLen;
        if (c != 0) {
            strncpy(c, sp8.c_str(), byteLen);
            c = &c[byteLen];
        }
        us++;
    }
    *c = 0;
    return ctr;
}

void ASCIItoWideVector(std::vector<unsigned short> &vec, const char *cc) {
    vec.clear();
    for (int i = 0; i < strlen(cc); i++) {
        String str;
        EncodeUTF8(str, (unsigned char)cc[i]);
        unsigned short us;
        DecodeUTF8(us, str.c_str());
        vec.push_back(us);
    }
}

String WideVectorToASCII(const std::vector<unsigned short> &wideVec) {
    String str;
    for (int i = 0; i < wideVec.size(); i++) {
        unsigned short curChar = wideVec[i];
        if (curChar > 0xFF)
            str += '*';
        else
            str += curChar;
    }
    return str;
}

unsigned int WideVectorToUTF8(const std::vector<unsigned short> &vec, String &str) {
    int totalBytes = 0;
    String thisStr;
    for (int i = 0; i < vec.size(); i++) {
        int curChar = vec[i];
        if (curChar < 0x80U)
            totalBytes += 1;
        else if (curChar <= 0x7FFU)
            totalBytes += 2;
        else if (curChar <= 0xFFFFU)
            totalBytes += 3;
        else {
            MILO_FAIL("HMX wide chars cannot exceed 16 bits");
            return 0;
        }
    }
    str.resize(totalBytes + 1);
    str.erase();
    for (int i = 0; i < vec.size(); i++) {
        EncodeUTF8(thisStr, vec[i]);
        str << thisStr;
    }
    return totalBytes;
}

void UTF8toWideVector(std::vector<unsigned short> &vec, const char *cc) {
    while (*cc != '\0') {
        unsigned short us;
        cc += DecodeUTF8(us, cc);
        vec.push_back(us);
    };
}

const unsigned short *CharToWideChar(const char *str) {
    if (!str)
        return 0;
    int len = strlen(str);
    static std::vector<unsigned short> wstring;
    wstring.clear();
    const char *p = str;
    if (len > 0) {
        do {
            unsigned short us = (unsigned char)*p++;
            wstring.push_back(us);
        } while (--len);
    }
    wstring.push_back(0);
    return &wstring[0];
}
