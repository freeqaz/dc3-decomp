// test_outfit_remap_cost.cpp -- GetEntriesForOutfit searches OUTFIT_REMAP once
// a call, not once per character (np-gfx spotperf round 7, branch
// np-framecost7, PLAN H-118; src/system/hamobj/HamGameData.cpp, HX_NATIVE).
//
// At song end HamSongMgr::GetCrewStars runs GetOutfitCharacter for every
// available song, and the image's GetEntriesForOutfit searches the whole
// OUTFIT_REMAP table again for each character it tries: 5.5 ms of the 8 ms
// MetaPerformer::CompleteSong frame. The remap depends only on the outfit, so
// the native body searches once. This pins:
//  - every outfit resolves to the same character and outfit entry as an
//    independent reference walk of the two tables (the result is unchanged);
//  - a call searches OUTFIT_REMAP once, wherever the outfit's character is in
//    CHARACTERS, including outfits found under no character and outfits with
//    no remap entry (the image searches once per character with outfits);
//  - characters without an outfits array are skipped before the search, as
//    in the image: a CHARACTERS table with none searches zero times.
//
// Sabotage (dc3-decomp archive/np-gfx-2026-11-14/lanes/spotperf/round7):
//  - the image's per-character search restored -> searches == characters, red;
//  - the remap kept across calls (a memo keyed only on the first outfit asked)
//    -> the second outfit resolves to the first one's character, red;
//  - the search moved before the character loop -> the no-outfits table
//    searches once, red.

#include "test_helpers.h"

#include "hamobj/HamGameData.h"
#include "obj/Data.h"
#include "obj/DataFile.h"
#include "obj/DataUtl.h"

#include <string>

extern int gNativeOutfitRemapSearches;

namespace {

class OutfitRemapCostTest : public SymbolTestFixture {
protected:
    void SetUp() override {
        mOldRemap = Keep(DataGetMacro(Symbol("OUTFIT_REMAP")));
        mOldChars = Keep(DataGetMacro(Symbol("CHARACTERS")));
    }
    void TearDown() override {
        Restore("OUTFIT_REMAP", mOldRemap);
        Restore("CHARACTERS", mOldChars);
    }
    static DataArray *Keep(DataArray *a) {
        if (a)
            a->AddRef();
        return a;
    }
    static void Restore(const char *name, DataArray *old) {
        DataSetMacro(Symbol(name), old);
        if (old)
            old->Release();
    }
    static void SetMacro(const char *name, const char *text) {
        DataArray *a = DataReadString(text);
        ASSERT_NE(a, nullptr);
        DataSetMacro(Symbol(name), a);
        a->Release();
    }
    // Six characters; nina has no outfits array. Outfits remap to themselves
    // except the *_alt ones, which remap to another outfit of the character.
    void SetUpTables() {
        SetMacro(
            "OUTFIT_REMAP",
            "(emilia01 emilia01) (emilia02 emilia02) (emilia_alt emilia02)"
            "(bodie01 bodie01) (taye01 taye01) (mo01 mo01) (mo02 mo02)"
            "(glitch01 glitch01) (glitch_alt glitch01) (ghost01 ghost01)"
        );
        SetMacro(
            "CHARACTERS",
            "(emilia (gender female) (outfits (emilia01 a) (emilia02 b)))"
            "(bodie (gender male) (outfits (bodie01 a)))"
            "(nina (gender female))"
            "(taye (gender female) (outfits (taye01 a)))"
            "(mo (gender male) (outfits (mo01 a) (mo02 b)))"
            "(glitch (gender male) (outfits (glitch01 a)))"
        );
    }
    DataArray *mOldRemap = nullptr;
    DataArray *mOldChars = nullptr;
};

// The reference: the remap row's second symbol, then the first character
// whose outfits array has a row named by it. Written against the tables, not
// against HamGameData.
bool Reference(Symbol outfit, std::string &charName, DataArray **outfitEntry) {
    DataArray *remapRow = DataGetMacro(Symbol("OUTFIT_REMAP"))->FindArray(outfit, false);
    if (!remapRow)
        return false;
    Symbol remap = remapRow->Sym(1);
    DataArray *chars = DataGetMacro(Symbol("CHARACTERS"));
    for (int i = 0; i < chars->Size(); i++) {
        DataArray *c = chars->Array(i);
        DataArray *outfits = c->FindArray(Symbol("outfits"), false);
        if (!outfits)
            continue;
        if (DataArray *e = outfits->FindArray(remap, false)) {
            charName = c->Sym(0).Str();
            *outfitEntry = e;
            return true;
        }
    }
    return false;
}

TEST_F(OutfitRemapCostTest, EveryOutfitResolvesAsTheReferenceWithOneSearch) {
    SetUpTables();
    const char *outfits[] = { "emilia01", "emilia02",  "emilia_alt", "bodie01",
                              "taye01",   "mo01",      "mo02",       "glitch01",
                              "glitch_alt", "ghost01", "nobody01" };
    for (const char *name : outfits) {
        SCOPED_TRACE(name);
        std::string refChar;
        DataArray *refOutfit = nullptr;
        bool refFound = Reference(Symbol(name), refChar, &refOutfit);

        DataArray *charEntry = reinterpret_cast<DataArray *>(1);
        DataArray *outfitEntry = reinterpret_cast<DataArray *>(1);
        int before = gNativeOutfitRemapSearches;
        bool found = GetEntriesForOutfit(Symbol(name), &charEntry, &outfitEntry, false);
        EXPECT_EQ(gNativeOutfitRemapSearches - before, 1);
        ASSERT_EQ(found, refFound);
        if (found) {
            ASSERT_NE(charEntry, nullptr);
            EXPECT_EQ(std::string(charEntry->Sym(0).Str()), refChar);
            EXPECT_EQ(outfitEntry, refOutfit);
            EXPECT_EQ(std::string(GetOutfitCharacter(Symbol(name), false).Str()), refChar);
        } else {
            EXPECT_EQ(charEntry, nullptr);
            EXPECT_EQ(outfitEntry, nullptr);
        }
    }
}

// glitch is the last of six characters: the image searched OUTFIT_REMAP five
// times (nina has no outfits) to find glitch01.
TEST_F(OutfitRemapCostTest, LastCharacterSearchesOnce) {
    SetUpTables();
    int before = gNativeOutfitRemapSearches;
    EXPECT_EQ(std::string(GetOutfitCharacter(Symbol("glitch_alt"), false).Str()), "glitch");
    EXPECT_EQ(gNativeOutfitRemapSearches - before, 1);
    before = gNativeOutfitRemapSearches;
    EXPECT_EQ(GetOutfitEntry(Symbol("ghost01"), false), nullptr);
    EXPECT_EQ(gNativeOutfitRemapSearches - before, 1);
}

TEST_F(OutfitRemapCostTest, NoCharacterWithOutfitsSearchesNothing) {
    SetUpTables();
    SetMacro("CHARACTERS", "(nina (gender female)) (rasa (gender female))");
    int before = gNativeOutfitRemapSearches;
    EXPECT_FALSE(GetEntriesForOutfit(Symbol("emilia01"), nullptr, nullptr, false));
    EXPECT_EQ(gNativeOutfitRemapSearches - before, 0);
}

} // namespace
