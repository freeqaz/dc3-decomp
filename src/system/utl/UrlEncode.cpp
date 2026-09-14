#include "UrlEncode.h"
#include <utl/Str.h>

namespace {
    bool IsCharInString(char c, char const *str) {
        int length = strlen(str);
        for (int i = 0; i < length; ++i) {
            if (c == str[i]) {
                return true;
            }
        }
        return false;
    }
}

// These three are FILE-SCOPE const pointers, not locals: the target's .rdata
// carries their three initialiser words immediately after the hexmap literal
// (0x820DCA98..0x820DCAA0, in the order hexmap / unsafe / reserved), and that
// is also why `hexmap[2]` survives as `lbz r4, 0x2(r30)` instead of being
// constant-folded to `li r4, 0x32` -- MSVC propagates the pointer value but not
// the pointee through a static.
static char const *const hexmap = "0123456789ABCDEF";
static char const *const unsafe = " \"<>#%{}|\\^~[]`";
static char const *const reserved = "$&+,/:;=?@";

void URLEncode(char const *input, String &output, bool escapeUnsafe) {
    int length = strlen(input);

    for (int i = 0; i < length; ++i) {
        char c = input[i];
        // `unsafe` is tested FIRST: the first `bl IsCharInString` at 0x827EF50C
        // takes r28, which is the `" \"<>#%{}|\\^~[]`"` literal; the
        // `$&+,/:;=?@` literal only reaches r25 for the second call.
        if (IsCharInString(c, unsafe) || IsCharInString(c, reserved) || c < ' '
            || c > '~') {
            output += "%";

            // Nothing is assigned back to `c` here -- each arm APPENDS its
            // second character, and all three paths cross-jump into the one
            // `mr r3, r29 / bl String::operator+=(char)` tail at 0x827EF59C.
            // NEGATIVE RESULT (w7-al, 2026-09-14): collapsing the two arms into
            // `if (escapeUnsafe && ...) c = ' ';` plus one unconditional pair of
            // appends scores 93.4 -- MSVC then materialises `li r31, 0x20` where
            // the image has `lbz r4, 0x2(r30)`.
            if (escapeUnsafe && (c < ' ' || c > '~')) {
                output += hexmap[2];
                output += hexmap[0];
            } else {
                output += hexmap[(c >> 4) & 0xf];
                output += hexmap[c & 0xf];
            }
        } else {
            output += c;
        }
    }
}
