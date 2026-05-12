#include "Instarank.h"
#include "os/Debug.h"
#include "utl/Locale.h"
#include "utl/LocaleOrdinal.h"
#include "utl/Symbol.h"

const char *Instarank::Str() const {
    static Symbol ir_still_rank("ir_still_rank");
    static Symbol ir_at_rank("ir_at_rank");
    static Symbol ir_unranked("ir_unranked");
    static Symbol ir_number_one("ir_number_one");
    static Symbol ir_not_best("ir_not_best");
    static Symbol ir_among_friends("ir_among_friends");
    static Symbol ir_among_all("ir_among_all");

    const char *among_group =
        Localize(mFriendsOnly ? ir_among_friends : ir_among_all, nullptr, TheLocale);
    const char *ord = LocalizeOrdinal(
        unk_0x108, LocaleGenderMasculine, LocaleSingular, false, gNullStr, TheLocale
    );
    const char *score = LocalizeSeparatedInt(unk_0x10C, TheLocale);
    const char *ret;
    if (mToken == 'd') {
        ret = MakeString(Localize(ir_unranked, nullptr, TheLocale), mStr, score);
    } else if (mToken == 'c') {
        ret = MakeString(
            Localize(ir_number_one, nullptr, TheLocale),
            LocalizeOrdinal(
                1, LocaleGenderMasculine, LocaleSingular, false, gNullStr, TheLocale
            ),
            among_group
        );
    } else if (mToken == 'b') {
        ret = MakeString(
            Localize(ir_not_best, nullptr, TheLocale), ord, among_group, mStr, score
        );
    } else if (mToken == 'a') {
        ret = MakeString(
            Localize(ir_at_rank, nullptr, TheLocale), ord, among_group, mStr, score
        );
    } else if (mToken == 'e') {
        ret = MakeString(
            Localize(ir_still_rank, nullptr, TheLocale), ord, among_group, mStr, score
        );
    } else {
        ret = "";
        MILO_NOTIFY("unrecognized instarank token: %c\n", mToken);
    }
    return ret;
}
