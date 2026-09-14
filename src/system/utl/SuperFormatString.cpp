#include "utl\SuperFormatString.h"
#include "os\Debug.h"
#include "os\System.h"
#include "utl\Locale.h"
#include "utl\LocaleOrdinal.h"
#include "obj\Data.h"
#include <string.h>

#define BUF_SIZE 0x800

SuperFormatString::SuperFormatString(
    const char *cc, const DataArray *da, bool b, Locale &locale, Symbol lang
) {
    char tempFmt[2048];
    char param[8];
    char phInfo[64];
    mTokensOnly = b;
    mHasPercentFormat = false;
    if (!da && !b) {
        InitializeWithFmt(cc, true);
        return;
    } else {
        // The image initialises the three walking pointers BEFORE the two
        // ints (827F7... `addi r25, r31, 0x100` / `addi r19, r31, 0xb0` /
        // `addi r21, r31, 0x70`, then `mr r22, r20` / `mr r23, r20` off the
        // zero register), and that order is what puts phInfoPos in r19 and the
        // zero in r20 rather than the other way round.
        char *tempFmtPos = tempFmt;
        char *phInfoPos = phInfo;
        char *paramPos = param;
        int phType = 0;
        int state = 0;
        char *tempFmtEnd = tempFmt + 2048;
        // The image zeroes 0x51 before 0x50 (and 0x50 is sawPercent -- it is
        // the byte tested against '%' at 827F7C24), so sawDouble is the one
        // initialised first.
        bool sawDouble = false;
        bool sawPercent = false;
        for (const char *p = cc; *p != 0; p++) {
            switch (state) {
            case 0:
                if (*p == '{') {
                    // `{{` is the fall-through arm and `state = 1` the branch
                    // target (`bne cr6, .L_827F7C14` at 827F7C00 skips the
                    // `li r10, 0x7b` / `stw` / `stb` of the literal brace).
                    if (p[1] == '{') {
                        *tempFmtPos++ = '{';
                        p++;
                    } else {
                        state = 1;
                    }
                } else {
                    if (*p == '%' && !sawPercent) {
                        if (p[1] == '%' && !sawDouble) {
                            sawPercent = true;
                            mHasPercentFormat = true;
                        } else {
                            sawDouble = true;
                            mHasPercentFormat = false;
                        }
                    } else {
                        sawPercent = false;
                    }
                    *tempFmtPos++ = *p;
                }
                break;
            case 1:
                if (*p == ':') {
                    MILO_ASSERT(phInfoPos - phInfo < 64, 0x5A);
                    *phInfoPos = '\0';
                    phInfoPos = phInfo;
                    state = 3;
                    // A flat `else if` chain with each arm's own `phType;
                    // state; *paramPos++ = '%'` and no `continue`: that is
                    // what makes MSVC keep the shared tail at the "float"
                    // arm (`li r22, 0x1` / `b .L_827F7B28` at 827F7AAC jumps
                    // FORWARD into `li r11, 0x25` / `li r23, 0x2` /
                    // `stb r11, 0x0(r21)`, and case 2's `*paramPos++ = *p`
                    // joins at .L_827F7B30). Refuted: separate
                    // `if (...) { ...; continue; }` statements (tail kept at
                    // "int", "float" jumping back, 8 rows); a named strcmp
                    // temp (homed to the stack, `stw r9, 0x60(r31)`, and it
                    // takes the slot the image gives the DataNode-lifetime
                    // flag word); ONE `state = 2; *paramPos++ = '%'` site
                    // after the chain reached by fall-through -- as nested
                    // if/else and as a flat else-if -- which re-homes
                    // paramPos to the stack and reshuffles r16..r30 (103-109
                    // rows).
                    if (strcmp(phInfoPos, "string") == 0) {
                        phType = 0;
                    } else if (strcmp(phInfoPos, "int") == 0) {
                        phType = 1;
                        state = 2;
                        *paramPos++ = '%';
                    } else if (strcmp(phInfoPos, "sep_int") == 0) {
                        phType = 2;
                    } else if (strcmp(phInfoPos, "float") == 0) {
                        phType = 3;
                        state = 2;
                        *paramPos++ = '%';
                    } else if (strcmp(phInfoPos, "token") == 0) {
                        phType = 4;
                    } else if (strcmp(phInfoPos, "ordinal") == 0) {
                        phType = 5;
                        state = 2;
                    } else {
                        MILO_FAIL("bad SuperFormatString placeholder type '%s'", phInfo);
                    }
                } else {
                    *phInfoPos++ = *p;
                }
                break;
            case 2:
                if (*p == ':') {
                    if (phType == 3) {
                        *paramPos++ = 'f';
                        *paramPos = '\0';
                    } else if (phType == 1) {
                        *paramPos++ = 'i';
                        *paramPos = '\0';
                    }
                    MILO_ASSERT(paramPos - param < 8, 0x8F);
                    if (phType == 5) {
                        MILO_ASSERT(param + 2 == paramPos, 0x95);
                    }
                    paramPos = param;
                    state = 3;
                } else {
                    *paramPos++ = *p;
                }
                break;
            case 3:
                if (*p == '}') {
                    MILO_ASSERT(phInfoPos - phInfo < 64, 0xA3);
                    *phInfoPos = '\0';
                    phInfoPos = phInfo;
                    state = 0;
                    bool isToken = phType == 4;
                    DataArray *theArr = 0;
                    if (!b && !isToken) {
                        theArr = da->FindArray(phInfoPos, false);
                    }
                    if (theArr || isToken) {
                        DataNode node((isToken) ? DataNode(0) : theArr->Evaluate(1));
                        bool nodeBad = false;
                        switch (phType) {
                        case 0:
                            // Cases 0 and 3 are `&&` bool values sharing one
                            // tail (`li r11, 0x1` / `bne` / `mr r11, r20` /
                            // `clrlwi` at 827F7774..827F7780); the subic/subfe
                            // boolean-isation at .L_827F7744 belongs to cases
                            // 1, 2 and 5 (the plain `!= kDataInt`), not to
                            // case 3.
                            nodeBad = node.Type() != kDataString
                                && node.Type() != kDataSymbol;
                            break;
                        case 1:
                            nodeBad = node.Type() != kDataInt;
                            break;
                        case 2:
                            nodeBad = node.Type() != kDataInt;
                            break;
                        case 3:
                            nodeBad = node.Type() != kDataFloat
                                && node.Type() != kDataInt;
                            break;
                        case 4:
                            nodeBad = false;
                            break;
                        case 5:
                            nodeBad = node.Type() != kDataInt;
                            break;
                        default:
                            break;
                        }

                        if (!nodeBad) {
                            int snResult = 0;
                            switch (phType) {
                            case 0:
                                if (node.Type() == kDataString) {
                                    snResult = Hx_snprintf(
                                        tempFmtPos,
                                        tempFmtEnd - tempFmtPos,
                                        "%s",
                                        node.Str()
                                    );
                                } else {
                                    snResult = Hx_snprintf(
                                        tempFmtPos,
                                        tempFmtEnd - tempFmtPos,
                                        "%s",
                                        Localize(node.Sym(), 0, locale)
                                    );
                                }
                                break;
                            case 1:
                                snResult = Hx_snprintf(
                                    tempFmtPos, tempFmtEnd - tempFmtPos, param, node.Int()
                                );
                                break;
                            case 2:
                                snResult = Hx_snprintf(
                                    tempFmtPos,
                                    tempFmtEnd - tempFmtPos,
                                    "%s",
                                    LocalizeSeparatedInt(node.Int(), locale)
                                );
                                break;
                            case 3:
                                snResult = Hx_snprintf(
                                    tempFmtPos,
                                    tempFmtEnd - tempFmtPos,
                                    param,
                                    node.Float()
                                );
                                break;
                            case 4:
                                snResult = Hx_snprintf(
                                    tempFmtPos,
                                    tempFmtEnd - tempFmtPos,
                                    "%s",
                                    Localize(Symbol(phInfo), 0, locale)
                                );
                                break;
                            case 5: {
                                // gender/num are NAMED (param[0] is read
                                // before param[1]: `lbz r11, 0x70(r31)` at
                                // 827F77B4, then 0x71), but the Int() call is
                                // written inline: a named `x` sets up the
                                // LocalizeOrdinal arguments r8..r4 in reverse
                                // (locale first), the image does r4 first.
                                LocaleGender gender = (LocaleGender)(param[0] != 'm');
                                LocaleNumber num = (LocaleNumber)(param[1] != 's');
                                snResult = Hx_snprintf(
                                    tempFmtPos,
                                    tempFmtEnd - tempFmtPos,
                                    "%s",
                                    LocalizeOrdinal(
                                        node.Int(), gender, num, false, lang, locale
                                    )
                                );
                                break;
                            }
                            }

                            tempFmtPos += snResult;
                            continue;
                        }
                        MILO_NOTIFY(
                            "parameter for placeholder '%s' was the wrong type", phInfo
                        );
                    } else {
                        MILO_NOTIFY(
                            "couldn't find parameter for placeholder '%s'", phInfo
                        );
                    }
                    tempFmtPos += Hx_snprintf(
                        tempFmtPos, tempFmtEnd - tempFmtPos, "{missing:%s}", phInfo
                    );
                } else {
                    *phInfoPos++ = *p;
                }
                break;
            default:
                break;
            }
        }

        if (state != 0) {
            *phInfoPos = '\0';
            MILO_NOTIFY("bad formatting for placeholder '%s'", phInfo);
            tempFmtPos +=
                Hx_snprintf(tempFmtPos, tempFmtEnd - tempFmtPos, "{badfmt:%s", phInfo);
        }
        *tempFmtPos = 0;
        MILO_ASSERT(tempFmtPos - tempFmt < BUF_SIZE, 0x10B);
        InitializeWithFmt(tempFmt, b == 0);
    }
}

const char *SuperFormatString::FinalStr() {
    if (!(!mTokensOnly)) {
        return mFmt;
    }
    const char *result = Str();
    if (!mHasPercentFormat) {
        return result;
    }
    String str(result);
    str += "%s";
    return MakeString(str.c_str(), "");
}
