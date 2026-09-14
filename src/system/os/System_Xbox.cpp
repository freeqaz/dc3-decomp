#include "obj\Data.h"
#include "os\Debug.h"
#include "os\File.h"
#include "os\PlatformMgr.h"
#include "os\System.h"
#include "xdk\XAPILIB.h"
#include "xdk\XBDM.h"
#include "Memory.h"

namespace {
    DiscErrorCallbackFunc *gCallback;
}

unsigned long ULSystemLocale() { return XGetLocale(); }
unsigned long ULSystemLanguage() { return XTLGetLanguage(); }

DiscErrorCallbackFunc *SetDiskErrorCallback(DiscErrorCallbackFunc *func) {
    DiscErrorCallbackFunc *old = gCallback;
    gCallback = func;
    return old;
}

DiscErrorCallbackFunc *GetDiskErrorCallback() { return gCallback; }

namespace {
    void XDKCheck() {
        DM_SYSTEM_INFO info;
        info.SizeOfStruct = 0x20;
        int hr = DmGetSystemInfo(&info);
        MILO_ASSERT(SUCCEEDED(hr), 0x27);
        if (info.XDKVersion.Build < 21173) {
            MILO_NOTIFY(
                "Console firmware is out of date.  Console: %d  Binary: %d",
                info.XDKVersion.Build,
                21173
            );
        }
    }
}

// Open residual (w7-r, 2026-09-14): the image and we disagree about which of the
// two incoming pointers gets a home slot. The image spills the by-value param
// (`stw r4, 0x11c(r31)`) and keeps the hidden sret pointer in callee-saved r26,
// so every `s = <sym>` arm loads into scratch r11 and branches to one merge
// point; we spill the sret pointer (`stw r3, 0x114(r31)`) and keep `s` itself in
// callee-saved r17, so the arms load into r17 and branch to a different merge.
// Everything else in the 61 rows is a straight permutation of the callee-saved
// registers holding the 17 function-local `static Symbol`s (r17..r29), which the
// canonical ruler already forgives -- hence 98.48 with 61 raw rows.
Symbol GetSystemLanguage(Symbol s) {
    static Symbol eng("eng");
    static Symbol fre("fre");
    static Symbol ita("ita");
    static Symbol deu("deu");
    static Symbol esl("esl");
    static Symbol mex("mex");
    static Symbol swe("swe");
    static Symbol pol("pol");
    static Symbol nor("nor");
    static Symbol fin("fin");
    static Symbol dut("dut");
    static Symbol dan("dan");
    static Symbol ptb("ptb");
    static Symbol rus("rus");
    static Symbol cht("cht");
    static Symbol kor("kor");
    static Symbol jpn("jpn");

    unsigned long lang = ULSystemLanguage();
    unsigned long locale = ULSystemLocale();

    // The image RETURNS out of both switches; it never assigns to `s`.  It
    // spills the parameter once at entry (stw r4, 0x11c(r31), 0x825E0734) and
    // reads that slot back in exactly one place -- the default arm at
    // 0x825E0BCC -- while every other arm loads its Symbol and branches to the
    // common return store at 0x825E0BD0 (stw r11, 0x0(r26); mr r3, r26).
    //
    // Two behavioural bugs this fixes, both invisible to the old `s = ...;
    // break;` spelling:
    //  * The locale switch's five Nordic arms returned immediately in the image
    //    (0x825E0A74/78 swe, 0x825E0A90/94 nor, 0x825E0AAC/B0 dut,
    //    0x825E0AC8/CC fin, 0x825E0AE4/E8 dan -- each `lwz r11, 0x0(rN)` then
    //    `b .L_825E0BD0`).  Assigning and falling through let the language
    //    switch below overwrite the choice, so e.g. a Swedish console reporting
    //    XC_LANGUAGE_ENGLISH came out `eng` instead of `swe`.
    //  * The Spanish arm returns `esl` for every locale that is not Chile,
    //    Colombia or Mexico (0x825E0B74 `bne cr6, .L_825E0B94`, and
    //    .L_825E0B94 is `lwz r11, 0x0(r15)` = esl).  We left `s` untouched
    //    there, returning the caller's fallback instead of Spanish.
    switch (locale) {
    case XC_LOCALE_SWEDEN:
        if (IsSupportedLanguage(swe, false))
            return swe;
        break;
    case XC_LOCALE_NORWAY:
        if (IsSupportedLanguage(nor, false))
            return nor;
        break;
    case XC_LOCALE_NETHERLANDS:
        if (IsSupportedLanguage(dut, false))
            return dut;
        break;
    case XC_LOCALE_FINLAND:
        if (IsSupportedLanguage(fin, false))
            return fin;
        break;
    case XC_LOCALE_DENMARK:
        if (IsSupportedLanguage(dan, false))
            return dan;
    default:
        break;
    }

    switch (lang) {
    case XC_LANGUAGE_ENGLISH:
        if (locale == XC_LOCALE_BELGIUM && IsSupportedLanguage(dut, false))
            return dut;
    case XC_LANGUAGE_SCHINESE:
        return eng;
    case XC_LANGUAGE_JAPANESE:
        return jpn;
    case XC_LANGUAGE_GERMAN:
        return deu;
    case XC_LANGUAGE_FRENCH:
        return fre;
    case XC_LANGUAGE_SPANISH:
        if (locale == XC_LOCALE_CHILE || locale == XC_LOCALE_COLOMBIA
            || locale == XC_LOCALE_MEXICO) {
            if (IsSupportedLanguage(mex, false))
                return mex;
        }
        return esl;
    case XC_LANGUAGE_ITALIAN:
        return ita;
    case XC_LANGUAGE_KOREAN:
        return kor;
    case XC_LANGUAGE_TCHINESE:
        return cht;
    case XC_LANGUAGE_PORTUGUESE:
        return ptb;
    case XC_LANGUAGE_POLISH:
        return pol;
    case XC_LANGUAGE_RUSSIAN:
        return rus;
    default:
        break;
    }

    return s;
}

Symbol GetSystemLocale(Symbol s) {
    static Symbol aus("aus");
    static Symbol aut("aut");
    static Symbol bel("bel");
    static Symbol bra("bra");
    static Symbol can("can");
    static Symbol chi("chi");
    static Symbol chn("chn");
    static Symbol col("col");
    static Symbol cze("cze");
    static Symbol den("den");
    static Symbol esp("esp");
    static Symbol fin("fin");
    static Symbol fra("fra");
    static Symbol gbr("gbr");
    static Symbol ger("ger");
    static Symbol gre("gre");
    static Symbol hkg("hkg");
    static Symbol hun("hun");
    static Symbol ind("ind");
    static Symbol irl("irl");
    static Symbol ita("ita");
    static Symbol jpn("jpn");
    static Symbol kor("kor");
    static Symbol mex("mex");
    static Symbol ned("ned");
    static Symbol nor("nor");
    static Symbol nzl("nzl");
    static Symbol pol("pol");
    static Symbol por("por");
    static Symbol rsa("rsa");
    static Symbol rus("rus");
    static Symbol sin("sin");
    static Symbol svk("svk");
    static Symbol swe("swe");
    static Symbol sui("sui");
    static Symbol tpe("tpe");
    static Symbol usa("usa");
    switch (ULSystemLocale()) {
    case XC_LOCALE_AUSTRALIA:
        return aus;
    case XC_LOCALE_AUSTRIA:
        return aut;
    case XC_LOCALE_BELGIUM:
        return bel;
    case XC_LOCALE_BRAZIL:
        return bra;
    case XC_LOCALE_CANADA:
        return can;
    case XC_LOCALE_CHILE:
        return chi;
    case XC_LOCALE_CHINA:
        return chn;
    case XC_LOCALE_COLOMBIA:
        return col;
    case XC_LOCALE_CZECH_REPUBLIC:
        return cze;
    case XC_LOCALE_DENMARK:
        return den;
    case XC_LOCALE_FINLAND:
        return fin;
    case XC_LOCALE_FRANCE:
        return fra;
    case XC_LOCALE_GERMANY:
        return ger;
    case XC_LOCALE_GREECE:
        return gre;
    case XC_LOCALE_HONG_KONG:
        return hkg;
    case XC_LOCALE_HUNGARY:
        return hun;
    case XC_LOCALE_INDIA:
        return ind;
    case XC_LOCALE_IRELAND:
        return irl;
    case XC_LOCALE_ITALY:
        return ita;
    case XC_LOCALE_JAPAN:
        return jpn;
    case XC_LOCALE_KOREA:
        return kor;
    case XC_LOCALE_MEXICO:
        return mex;
    case XC_LOCALE_NETHERLANDS:
        return ned;
    case XC_LOCALE_NEW_ZEALAND:
        return nzl;
    case XC_LOCALE_NORWAY:
        return nor;
    case XC_LOCALE_POLAND:
        return pol;
    case XC_LOCALE_PORTUGAL:
        return por;
    case XC_LOCALE_SINGAPORE:
        return sin;
    case XC_LOCALE_SLOVAK_REPUBLIC:
        return svk;
    case XC_LOCALE_SOUTH_AFRICA:
        return rsa;
    case XC_LOCALE_SPAIN:
        return esp;
    case XC_LOCALE_SWEDEN:
        return swe;
    case XC_LOCALE_SWITZERLAND:
        return sui;
    case XC_LOCALE_TAIWAN:
        return tpe;
    case XC_LOCALE_GREAT_BRITAIN:
        return gbr;
    case XC_LOCALE_UNITED_STATES:
        return usa;
    case XC_LOCALE_RUSSIAN_FEDERATION:
        return rus;
    default:
        return s;
    }
}

bool HongKongExceptionMet() {
    if (ULSystemLanguage() == XC_LANGUAGE_TCHINESE
        && ULSystemLocale() == XC_LOCALE_HONG_KONG) {
        return true;
    } else
        return false;
}

void GetMapFileName(String &filename) {
    if (SystemConfig()) {
        const char *mapVersion = "r";
        DataArray *cfg = SystemConfig("system", "xbox_map_file");
        FileQualifiedFilename(
            filename, MakeString(cfg->Str(1), FileExecRoot(), mapVersion)
        );
    } else {
        char name[256];
        strcpy(name, FileGetName(TheSystemArgs.front()));
        char *rchar = strrchr(name, '.');
        if (rchar) {
            strcpy(rchar, ".map");
        }
        filename = name;
        FileQualifiedFilename(filename, name);
    }
}

void SystemPreInit(int, char **const, const char *c3) {
    SystemPreInit(GetCommandLineA(), c3);
    XDKCheck();
    ForceLinkXMemFuncs();
}

bool PlatformDebugBreak() {
    if (DmIsDebuggerPresent()) {
        DebugBreak();
        return true;
    }
    return false;
}

void ShowDirtyDiscError() {
    unsigned long ul;

    if (ThePlatformMgr.sXShowCallback(ul)) {
        XShowNuiDirtyDiscErrorUI(ul, 0);
    }

    XShowDirtyDiscErrorUI(0);
}

void CaptureStackTrace(int p1, struct StackData *stackData, void *p3) {
    stackData->mFailThreadStack[0] = 0;

    DmCaptureStackBackTrace(p1, stackData);

    memmove(stackData->mFailThreadStack, stackData->mFailThreadStack + 3, (p1 + -3) * 4);

    if (p3 != 0) {
        memmove(
            stackData->mFailThreadStack + 2,
            stackData->mFailThreadStack + 8,
            (p1 + -8) * 4
        );
    }
}
