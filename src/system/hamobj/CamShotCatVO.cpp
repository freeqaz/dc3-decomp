#include "hamobj\CamShotCatVO.h"
#include "obj\Data.h"
#include "obj\DataFunc.h"
#include "obj\DataUtl.h"
#include "os\Debug.h"
#include "utl\Str.h"
#include "utl\Symbol.h"

Symbol StrToCharacterSym(String str) {
    str.ToLower();
    Symbol charSym(str.c_str());
    static Symbol CHARACTERS("CHARACTERS");
    DataArray *charArr = DataGetMacro(CHARACTERS)->FindArray(charSym, false);
    if (charArr) {
        return charSym;
    } else {
        MILO_NOTIFY("%s is not a valid character symbol", charSym);
        return gNullStr;
    }
}

Symbol StrToCrewSym(String str) {
    str.ToLower();
    Symbol crewSym(str.c_str());
    static Symbol CREWS("CREWS");
    DataArray *charArr = DataGetMacro(CREWS)->FindArray(crewSym, false);
    if (charArr) {
        return crewSym;
    } else {
        MILO_NOTIFY("%s is not a valid crew symbol", crewSym);
        return gNullStr;
    }
}

void CamShotVOData(
    Symbol s, Symbol &s1, Symbol &charSym, Symbol &crewSym, Symbol &winLevelSym
) {
    s1 = charSym = crewSym = winLevelSym = gNullStr;
    static Symbol INTRO_CAM_CATS("INTRO_CAM_CATS");
    static Symbol OUTRO_CAM_CATS("OUTRO_CAM_CATS");
    static Symbol intro_quick("intro_quick");
    static Symbol intro_skills("intro_skills");
    static Symbol intro_playlist("intro_playlist");
    static Symbol battle_intro_crew("battle_intro_crew");
    static Symbol camp_intro_crew("camp_intro_crew");
    static Symbol win_dlg_char("win_dlg_char");
    static Symbol win_mov_char("win_mov_char");
    static Symbol win_hype_solo("win_hype_solo");
    static Symbol win_hype_crew("win_hype_crew");
    static Symbol win_hype_diff_crew("win_hype_diff_crew");
    // Declaration order is observable: each function-local static takes the
    // next bit of the `?$S3@` guard word and the next .data slot.  The target
    // constructs win_camp_crew (bit 0x1000, 0x82F61C68) before lose_camp_char
    // (bit 0x2000, 0x82F61C64) -- see build/373307D9/asm/system/hamobj/
    // CamShotCatVO.s @82518D4C/82518D78 -- so these two were declared the wrong
    // way round here.  The *uses* below were always correct; only the order was
    // wrong.
    static Symbol win_camp_crew("win_camp_crew");
    static Symbol lose_camp_char("lose_camp_char");
    static Symbol battle_outro_crew("battle_outro_crew");
    static Symbol all("all");
    static Symbol active("active");
    bool hasIntros = DataGetMacro(INTRO_CAM_CATS)->Contains(s);
    bool hasOutros = DataGetMacro(OUTRO_CAM_CATS)->Contains(s);
    if (hasIntros || hasOutros) {
        String str(s.Str());
        std::vector<String> subStrings;
        str.split("_", subStrings);
        if (hasIntros) {
            static Symbol INTRO_QUICK("INTRO_QUICK");
            static Symbol INTRO_SKILLS("INTRO_SKILLS");
            static Symbol INTRO_SKILLS_LONG("INTRO_SKILLS_LONG");
            static Symbol INTRO_PLAYLIST("INTRO_PLAYLIST");
            if ((unsigned long)(int)(unsigned long)s == INTRO_QUICK) {
                s1 = intro_quick;
                charSym = all;
            } else if (s == INTRO_SKILLS || s == INTRO_SKILLS_LONG) {
                s1 = intro_skills;
                charSym = all;
            } else if (s == INTRO_PLAYLIST) {
                s1 = intro_playlist;
                charSym = all;
            } else if (0 < subStrings.size()) {
                if (subStrings[0] == "BATTLE") {
                    s1 = battle_intro_crew;
                    charSym = all;
                    crewSym = StrToCrewSym(subStrings[2]);
                } else if (subStrings[0] == "CAMP") {
                    s1 = camp_intro_crew;
                    charSym = all;
                }
            }
            if (s1.Null()) {
                MILO_NOTIFY("Unknown intro category %s", s);
            }
        } else if (hasOutros) {
            auto numSubStrings = subStrings.size();
            if (numSubStrings > 0 && subStrings[0] == "BATTLE") {
                s1 = battle_outro_crew;
                charSym = all;
                crewSym = StrToCrewSym(subStrings[2]);
            } else if (subStrings.size() > 1 && subStrings[1] == "CAMP") {
                if (subStrings[0] == "WIN") {
                    s1 = win_camp_crew;
                    charSym = all;
                } else if (subStrings[0] == "LOSE") {
                    s1 = lose_camp_char;
                    if (subStrings.size() > 2) {
                        charSym = StrToCharacterSym(subStrings[2]);
                        // String(Symbol), NOT String(const char *) -- REFUTED
                        // 2026-09-14 (w7-q).  objdiff's "Function Call Diff"
                        // reports `??0String@@QAA@VSymbol@@@Z` as base-only and
                        // `??0String@@QAA@PBD@Z` as target-2/base-1, which reads
                        // exactly like a wrong-callee bug.  It is not: the two
                        // constructors are ICF-FOLDED.
                        //   build/373307D9/icf_aliases.map:1875-1876
                        //     ??0String@@QAA@PBD@Z        827CE9E8
                        //     ??0String@@QAA@VSymbol@@@Z  827CE9E8
                        // dtk names the single body after the PBD symbol, so the
                        // listing can never distinguish the two spellings here.
                        // Measured anyway, full ninja, report.json canonical:
                        //   String s2Str(charSym)        97.42986 (fuzzy 96.77376)
                        //   String s2Str(charSym.Str())  97.12820 (fuzzy 96.41177)
                        //   Symbol c = ...; String s2Str(c.Str())  97.0 -- the
                        //     named local takes a frame word and shifts every
                        //     stack slot in the function by 8.
                        // Keep the Symbol overload.
                        String s2Str(charSym);
                        if (s2Str.contains("robot")) {
                            charSym = all;
                        }
                    } else {
                        MILO_NOTIFY("Could not find character in %s", s);
                    }
                } else {
                    MILO_NOTIFY("Could not determine cam_type for %s", s);
                }
            } else if (subStrings.size() > 1 && subStrings[1] == "HYPE") {
                static Symbol WIN_HYPE_SOLO("WIN_HYPE_SOLO");
                static Symbol WIN_HYPE_DIFF_CREW("WIN_HYPE_DIFF_CREW");
                if (s == WIN_HYPE_SOLO) {
                    s1 = win_hype_solo;
                    charSym = active;
                } else if (s == WIN_HYPE_DIFF_CREW) {
                    s1 = win_hype_diff_crew;
                    charSym = all;
                } else {
                    s1 = win_hype_crew;
                    charSym = all;
                }
            } else {
                // BUG FIX (w7-bv): the win-level / character block below is
                // INSIDE this final else, not after the chain.  Every earlier
                // arm exits straight to the vector destructor at 0x82519524 in
                // the image -- BATTLE `b 0x825194FC` (at 0x82519118, via the
                // shared `lwz r11, 0(r3); stw r11, 0(r10)` tail), WIN
                // `b 0x82519500` (0x82519174), LOSE `b 0x82519524` (0x8251920C)
                // and HYPE (0x825192F0) -- and only the DLG/MOV arm and the
                // no-match case (`ble cr6, 0x8251937C` at 0x82519310) reach the
                // `subStrings.size() > 1` test at 0x82519378.  We used to fall
                // into it from every arm, which notified "Couldn't find win
                // level" / "Couldn't find character" for every BATTLE/CAMP/HYPE
                // category and overwrote charSym with subStrings[3] on any
                // four-part BATTLE name.  97.43 -> 100.0 canonical (99.95 raw);
                // the whole r23/r25 and TheDebug-in-r14 cascade that w7-q
                // recorded as a register-allocator floor was downstream of
                // this control flow.  Residual 6 rows: the low/med/high statics
                // mangle as scope `?FG@` in the image and `?FI@` here (MSVC's
                // per-function scope ordinal -- two more numbered scopes reach
                // this block in our spelling), a class the canonical ruler
                // folds.
                if (subStrings.size() > 2) {
                    // One address for the two comparisons: the target computes
                    // `addi r29, r3, 0x10` once and reuses it (`mr r3, r29`
                    // before each String::operator==), unlike subStrings[1]
                    // above which it recomputes at every use.
                    String &camType = subStrings[2];
                    if (camType == "DLG")
                        s1 = win_dlg_char;
                    else if (camType == "MOV")
                        s1 = win_mov_char;
                    else
                        MILO_NOTIFY("Could not find cam_type for %s", s);
                }

                if (subStrings.size() > 1) {
                    String strd0(subStrings[1]);
                    strd0.ToLower();
                    static Symbol low("low");
                    static Symbol med("med");
                    static Symbol high("high");
                    if (strd0 == low || strd0 == med || strd0 == high) {
                        winLevelSym = strd0.c_str();
                    }
                }
                if (winLevelSym.Null()) {
                    MILO_NOTIFY("Couldn't find win level for %s", s);
                }
                if (subStrings.size() > 3) {
                    charSym = StrToCharacterSym(subStrings[3]);
                } else {
                    MILO_NOTIFY("Couldn't find character for %s", s);
                }
            }
        }
    }
}

DataNode OnCamShotVOData(DataArray *a) {
    Symbol s = a->Sym(1);
    Symbol s1, s2, s3, s4;
    CamShotVOData(s, s1, s2, s3, s4);
    *a->Var(2) = s1;
    *a->Var(3) = s2;
    *a->Var(4) = s3;
    *a->Var(5) = s4;
    return 0;
}

void CamShotCatVOInit() { DataRegisterFunc("camshot_vo_data", OnCamShotVOData); }
