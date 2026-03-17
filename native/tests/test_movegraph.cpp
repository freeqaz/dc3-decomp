// test_movegraph.cpp — MoveGraph/MoveVariant/MoveCandidate deserialization tests
//
// Exercises the binary Load → CacheLinks pipeline to reproduce and investigate
// the SIGSEGV at 0x3f800000 reported in LOADING_ARCHITECTURE.md. Tests cover:
//   - MoveCandidate binary format and adjacency flag bit 0 handling
//   - MoveVariant Load + CacheLinks with linked/unlinked variants
//   - MoveParent Load with multiple variants
//   - Struct layout verification (sizeof, offsetof)

// Include STL/gtest/engine headers first (before private→public hack)
#include "test_helpers.h"
#include "hamobj/Difficulty.h"
#include "obj/Object.h"
#include "utl/Symbol.h"
#include <cstring>

// Allow test access to private members of MoveVariant/MoveParent
#define private public
#define protected public
#include "hamobj/MoveGraph.h"
#undef private
#undef protected

// ============================================================================
// Binary format builders — construct Xbox-format (big-endian) test data
// ============================================================================

// BinStream reads bool as 1 byte (unsigned char)
static void PutBEBool(std::vector<uint8_t> &buf, bool val) {
    buf.push_back(val ? 1 : 0);
}

// Build a MoveCandidate binary blob (matches MoveCandidate::Load format)
static void BuildMoveCandidate(std::vector<uint8_t> &buf,
                               int rev,
                               unsigned int adjacencyFlag,
                               const char *s1,    // unused in current code
                               const char *s2,    // variant name → mValue.mVariantName
                               const char *s3) {  // source type (rev < 1 only)
    PutBE32(buf, rev);               // rev
    PutBE32(buf, adjacencyFlag);     // mAdjacencyFlag
    PutBEString(buf, s1);            // s1 (read but unused)
    PutBEString(buf, s2);            // s2 → mValue.mVariantName
    PutBEString(buf, s3);            // s3 (Adjacency lookup if rev < 1)
}

// Build a MoveVariant binary blob (matches MoveVariant::Load format)
static void BuildMoveVariant(std::vector<uint8_t> &buf,
                             int rev,
                             float posX, float posY, float posZ,
                             const char *variantName,
                             const char *hamMoveName,
                             const char *hamMoveMiloName,
                             const char *genre,
                             const char *era,
                             const char *songName,
                             float avgBeatsPerSec,
                             unsigned int flags,
                             const char *linkedTo,     // nullptr = no link
                             const char *linkedFrom,   // nullptr = no link
                             const std::vector<std::vector<uint8_t>> &prevCandidates,
                             const std::vector<std::vector<uint8_t>> &nextCandidates) {
    PutBE32(buf, rev);

    // Vector3 mPositionOffset (3 floats)
    PutBEFloat(buf, posX);
    PutBEFloat(buf, posY);
    PutBEFloat(buf, posZ);

    PutBEString(buf, variantName);
    PutBEString(buf, hamMoveName);
    PutBEString(buf, hamMoveMiloName);
    PutBEString(buf, genre);
    PutBEString(buf, era);
    PutBEString(buf, songName);
    PutBEFloat(buf, avgBeatsPerSec);
    PutBE32(buf, flags);

    // mLinkedTo: bool isSym + optional Symbol
    if (linkedTo) {
        PutBEBool(buf, true);
        PutBEString(buf, linkedTo);
    } else {
        PutBEBool(buf, false);
    }

    // mLinkedFrom (rev >= 1 only)
    if (rev >= 1) {
        if (linkedFrom) {
            PutBEBool(buf, true);
            PutBEString(buf, linkedFrom);
        } else {
            PutBEBool(buf, false);
        }
    }

    // prevCandidates
    PutBE32(buf, (uint32_t)prevCandidates.size());
    for (auto &cand : prevCandidates) {
        buf.insert(buf.end(), cand.begin(), cand.end());
    }

    // nextCandidates
    PutBE32(buf, (uint32_t)nextCandidates.size());
    for (auto &cand : nextCandidates) {
        buf.insert(buf.end(), cand.begin(), cand.end());
    }
}

// Build a MoveParent binary blob (matches MoveParent::Load format)
static void BuildMoveParent(std::vector<uint8_t> &buf,
                            int rev,
                            const char *name,
                            int difficulty,
                            const std::vector<const char *> &genres,
                            const std::vector<const char *> &eras,
                            bool isSuperEasy,
                            const std::vector<std::vector<uint8_t>> &variants) {
    PutBE32(buf, rev);
    PutBEString(buf, name);
    PutBE32(buf, difficulty);

    // Genre flags
    PutBE32(buf, (uint32_t)genres.size());
    for (auto g : genres) PutBEString(buf, g);

    // Era flags
    PutBE32(buf, (uint32_t)eras.size());
    for (auto e : eras) PutBEString(buf, e);

    // isSuperEasy (1 byte bool)
    PutBEBool(buf, isSuperEasy);

    // Extra string field (String read in MoveParent::Load)
    PutBEString(buf, "");

    // Variants
    PutBE32(buf, (uint32_t)variants.size());
    for (auto &v : variants) {
        buf.insert(buf.end(), v.begin(), v.end());
    }
}

// ============================================================================
// Test fixture — needs full engine init for Object::New<MoveGraph>()
// ============================================================================

class MoveGraphTest : public EngineTestFixture {};

// ============================================================================
// MoveCandidate::Load — basic deserialization
// ============================================================================

TEST_F(MoveGraphTest, MoveCandidateLoadBasic) {
    std::vector<uint8_t> buf;
    BuildMoveCandidate(buf, /*rev=*/1, /*adjacencyFlag=*/0x04,
                       "unused_s1", "variant_A", "unused_s3");

    MemBinStream ms(buf.data(), buf.size(), /*littleEndian=*/false);
    MoveCandidate cand;
    cand.Load(ms);

    EXPECT_FALSE(ms.Fail()) << "Stream should not fail";
    // After Load, bit 0 should NOT be set (name mode, not pointer mode)
    EXPECT_EQ(cand.mAdjacencyFlag & 1, 0u) << "Bit 0 should be clear after Load";
    EXPECT_EQ(cand.mAdjacencyFlag & 0x04, 0x04u) << "original_adjacent bit preserved";
}

TEST_F(MoveGraphTest, MoveCandidateLoadClearsBit0) {
    // Simulate binary data where bit 0 is set (as if saved after CacheLinks)
    std::vector<uint8_t> buf;
    BuildMoveCandidate(buf, /*rev=*/1, /*adjacencyFlag=*/0x05, // bit 0 + original_adjacent
                       "unused", "variant_B", "unused");

    MemBinStream ms(buf.data(), buf.size(), /*littleEndian=*/false);
    MoveCandidate cand;
    cand.Load(ms);

    // Bit 0 must be cleared — union is in name mode after Load
    EXPECT_EQ(cand.mAdjacencyFlag & 1, 0u)
        << "Bit 0 must be cleared after Load (union is name, not pointer)";
    EXPECT_EQ(cand.mAdjacencyFlag & 0x04, 0x04u) << "Other flags preserved";
}

TEST_F(MoveGraphTest, MoveCandidateLoadRev0AddsAdjacency) {
    std::vector<uint8_t> buf;
    // rev=0, adjacencyFlag=2, s3="original_adjacent" → should OR in 4
    BuildMoveCandidate(buf, /*rev=*/0, /*adjacencyFlag=*/2,
                       "unused", "variant_C", "original_adjacent");

    MemBinStream ms(buf.data(), buf.size(), /*littleEndian=*/false);
    MoveCandidate cand;
    cand.Load(ms);

    EXPECT_EQ(cand.mAdjacencyFlag & 0x06, 0x06u)
        << "Rev 0: adjacency flag should be 2 | 4 = 6";
    EXPECT_EQ(cand.mAdjacencyFlag & 1, 0u) << "Bit 0 still clear";
}

// ============================================================================
// MoveVariant::Load — deserialization
// ============================================================================

TEST_F(MoveGraphTest, MoveVariantLoadBasic) {
    MoveGraph *graph = Hmx::Object::New<MoveGraph>();

    std::vector<uint8_t> buf;
    BuildMoveVariant(buf,
        /*rev=*/1,
        /*pos=*/0.0f, 0.0f, 0.0f,
        /*variantName=*/"test_var",
        /*hamMoveName=*/"idle.move",
        /*hamMoveMiloName=*/"idle",
        /*genre=*/"pop",
        /*era=*/"modern",
        /*songName=*/"test_song",
        /*avgBeatsPerSec=*/2.5f,
        /*flags=*/0x02,
        /*linkedTo=*/nullptr,
        /*linkedFrom=*/nullptr,
        /*prevCandidates=*/{},
        /*nextCandidates=*/{});

    MemBinStream ms(buf.data(), buf.size(), /*littleEndian=*/false);
    MoveVariant var;
    MoveParent parent;
    var.Load(ms, graph, &parent);

    EXPECT_FALSE(ms.Fail()) << "Stream should not fail";
    EXPECT_EQ(var.Name(), Symbol("test_var"));
    EXPECT_EQ(var.HamMoveName(), Symbol("idle.move"));
    EXPECT_EQ(var.Genre(), Symbol("pop"));
    EXPECT_EQ(var.Era(), Symbol("modern"));
    EXPECT_EQ(var.Song(), Symbol("test_song"));
    EXPECT_EQ(var.Parent(), &parent);

    // Verify variant was registered in graph
    EXPECT_EQ(graph->FindMoveByVariantName(Symbol("test_var")), &var);

    delete graph;
}

TEST_F(MoveGraphTest, MoveVariantLoadWithLinkedTo) {
    MoveGraph *graph = Hmx::Object::New<MoveGraph>();

    std::vector<uint8_t> buf;
    BuildMoveVariant(buf, 1, 0, 0, 0,
        "var_with_link", "idle.move", "idle",
        "pop", "modern", "song",
        2.0f, 0,
        /*linkedTo=*/"target_variant",
        /*linkedFrom=*/nullptr,
        {}, {});

    MemBinStream ms(buf.data(), buf.size(), /*littleEndian=*/false);
    MoveVariant var;
    MoveParent parent;
    var.Load(ms, graph, &parent);

    EXPECT_FALSE(ms.Fail());
    // After Load, mLinkedTo should hold the name string "target_variant"
    // (not a valid MoveVariant pointer — CacheLinks hasn't run yet)
    EXPECT_STREQ(var.mLinkedTo.mVariantName, "target_variant");

    delete graph;
}

TEST_F(MoveGraphTest, MoveVariantLoadWithNullLinks) {
    MoveGraph *graph = Hmx::Object::New<MoveGraph>();

    std::vector<uint8_t> buf;
    BuildMoveVariant(buf, 1, 0, 0, 0,
        "var_no_links", "idle.move", "idle",
        "pop", "modern", "song",
        2.0f, 0,
        /*linkedTo=*/nullptr,
        /*linkedFrom=*/nullptr,
        {}, {});

    MemBinStream ms(buf.data(), buf.size(), /*littleEndian=*/false);
    MoveVariant var;
    MoveParent parent;
    var.Load(ms, graph, &parent);

    EXPECT_FALSE(ms.Fail());
    // With null links, union is set to nullptr
    EXPECT_EQ(var.mLinkedTo.mVariant, nullptr);
    EXPECT_EQ(var.mLinkedFrom.mVariant, nullptr);

    delete graph;
}

// ============================================================================
// MoveVariant::CacheLinks with null links
// ============================================================================

TEST_F(MoveGraphTest, MoveVariantCacheLinksHandlesNullLinks) {
    MoveGraph *graph = Hmx::Object::New<MoveGraph>();

    std::vector<uint8_t> buf;
    BuildMoveVariant(buf, 1, 0, 0, 0,
        "var_null_links", "idle.move", "idle",
        "pop", "modern", "song",
        2.0f, 0, nullptr, nullptr, {}, {});

    MemBinStream ms(buf.data(), buf.size(), /*littleEndian=*/false);
    MoveVariant var;
    MoveParent parent;
    var.Load(ms, graph, &parent);

    // CacheLinks with null mLinkedTo/mLinkedFrom should NOT crash.
    // Symbol(nullptr) maps to gNullStr, FindMoveByVariantName returns nullptr.
    var.CacheLinks(graph);

    EXPECT_EQ(var.mLinkedTo.mVariant, nullptr);
    EXPECT_EQ(var.mLinkedFrom.mVariant, nullptr);

    delete graph;
}

// ============================================================================
// Full MoveParent::Load + CacheLinks with cross-referencing candidates
// ============================================================================

TEST_F(MoveGraphTest, MoveParentLoadWithCandidates) {
    MoveGraph *graph = Hmx::Object::New<MoveGraph>();

    // Build two variants that reference each other via candidates
    std::vector<uint8_t> candBuf;
    BuildMoveCandidate(candBuf, 1, 0x04, "unused", "var_B", "unused");

    std::vector<uint8_t> var1Buf, var2Buf;
    BuildMoveVariant(var1Buf, 1, 0, 0, 0,
        "var_A", "idle.move", "idle", "pop", "modern", "song_a",
        2.0f, 0x02, nullptr, nullptr,
        /*prevCandidates=*/{},
        /*nextCandidates=*/{candBuf});

    std::vector<uint8_t> cand2Buf;
    BuildMoveCandidate(cand2Buf, 1, 0x04, "unused", "var_A", "unused");

    BuildMoveVariant(var2Buf, 1, 0, 0, 0,
        "var_B", "step.move", "step", "hiphop", "classic", "song_b",
        3.0f, 0x02, nullptr, nullptr,
        /*prevCandidates=*/{cand2Buf},
        /*nextCandidates=*/{});

    // Build a MoveParent containing both variants
    std::vector<uint8_t> parentBuf;
    BuildMoveParent(parentBuf, 0, "test_parent", /*difficulty=*/3,
        {"pop", "hiphop"}, {"modern", "classic"},
        false, {var1Buf, var2Buf});

    MemBinStream ms(parentBuf.data(), parentBuf.size(), /*littleEndian=*/false);
    MoveParent *parent = new MoveParent();
    parent->Load(ms, graph);

    EXPECT_FALSE(ms.Fail()) << "Stream should not fail during MoveParent::Load";
    EXPECT_EQ(parent->Name(), Symbol("test_parent"));
    EXPECT_EQ(parent->Variants().size(), 2u);

    // CacheLinks should resolve candidate names to variant pointers
    parent->CacheLinks(graph);

    // Verify candidates were resolved
    const MoveVariant *varA = graph->FindMoveByVariantName(Symbol("var_A"));
    const MoveVariant *varB = graph->FindMoveByVariantName(Symbol("var_B"));
    ASSERT_NE(varA, nullptr);
    ASSERT_NE(varB, nullptr);

    // var_A's nextCandidate should point to var_B
    EXPECT_EQ(varA->mNextCandidates[0].mValue.mVariant, varB);
    EXPECT_EQ(varA->mNextCandidates[0].mAdjacencyFlag & 1, 1u);

    // var_B's prevCandidate should point to var_A
    EXPECT_EQ(varB->mPrevCandidates[0].mValue.mVariant, varA);
    EXPECT_EQ(varB->mPrevCandidates[0].mAdjacencyFlag & 1, 1u);

    delete parent;
    delete graph;
}

// ============================================================================
// Struct layout verification — catch 32-bit vs 64-bit mismatches
// ============================================================================

TEST_F(MoveGraphTest, MoveVariantValueUnionSize) {
    // On 32-bit Xbox: sizeof(union) = 4 (pointer = 4 bytes)
    // On 64-bit native: sizeof(union) = 8 (pointer = 8 bytes)
    EXPECT_EQ(sizeof(MoveVariantValue), sizeof(void *));
    printf("  sizeof(MoveVariantValue) = %zu (Xbox: 4)\n", sizeof(MoveVariantValue));
}

TEST_F(MoveGraphTest, MoveCandidateLayout) {
    // MoveCandidate: { MoveVariantValue mValue; unsigned int mAdjacencyFlag; }
    // On 32-bit: 4 + 4 = 8 bytes
    // On 64-bit: 8 + 4 + padding = 16 bytes (pointer alignment)
    MoveCandidate cand;
    ptrdiff_t flagOffset = (char *)&cand.mAdjacencyFlag - (char *)&cand.mValue;
    EXPECT_EQ(flagOffset, (ptrdiff_t)sizeof(MoveVariantValue))
        << "mAdjacencyFlag should immediately follow mValue in memory";
    printf("  sizeof(MoveCandidate) = %zu (Xbox: 8)\n", sizeof(MoveCandidate));
    printf("  mAdjacencyFlag offset = %td (Xbox: 4)\n", flagOffset);
}

// ============================================================================
// SIGSEGV 0x3f800000 investigation: what data produces this address?
// ============================================================================

TEST_F(MoveGraphTest, Float1_0fBitPattern) {
    // 0x3f800000 is IEEE 754 for 1.0f.
    // If this address appears as a crash target, something read float data
    // and used it as a pointer.
    float one = 1.0f;
    uint32_t bits;
    memcpy(&bits, &one, 4);
    EXPECT_EQ(bits, 0x3f800000u)
        << "Confirming 0x3f800000 = 1.0f — the SIGSEGV crash address";
}

TEST_F(MoveGraphTest, MoveVariantFieldLayout) {
    // Document actual field layout on this platform.
    // On Xbox (32-bit), offsets are documented in MoveGraph.h comments.
    // On native (64-bit), pointer fields are 8 bytes → offsets shift.
    MoveVariant var;
    auto base = (uintptr_t)&var;

    printf("  === MoveVariant Field Layout (64-bit native) ===\n");
    printf("  sizeof(MoveVariant) = %zu (Xbox: 0x54 = 84)\n", sizeof(MoveVariant));
    printf("  mPositionOffset   @ +0x%02tx (Xbox: 0x00)\n", (uintptr_t)&var.mPositionOffset - base);
    printf("  mVariantName      @ +0x%02tx (Xbox: 0x10)\n", (uintptr_t)&var.mVariantName - base);
    printf("  mMoveParent       @ +0x%02tx (Xbox: 0x14)\n", (uintptr_t)&var.mMoveParent - base);
    printf("  mPrevCandidates   @ +0x%02tx (Xbox: 0x18)\n", (uintptr_t)&var.mPrevCandidates - base);
    printf("  mNextCandidates   @ +0x%02tx (Xbox: 0x24)\n", (uintptr_t)&var.mNextCandidates - base);
    printf("  mHamMoveName      @ +0x%02tx (Xbox: 0x30)\n", (uintptr_t)&var.mHamMoveName - base);
    printf("  mHamMoveMiloName  @ +0x%02tx (Xbox: 0x34)\n", (uintptr_t)&var.mHamMoveMiloName - base);
    printf("  mLinkedTo         @ +0x%02tx (Xbox: 0x38)\n", (uintptr_t)&var.mLinkedTo - base);
    printf("  mLinkedFrom       @ +0x%02tx (Xbox: 0x3c)\n", (uintptr_t)&var.mLinkedFrom - base);
    printf("  mGenre            @ +0x%02tx (Xbox: 0x40)\n", (uintptr_t)&var.mGenre - base);
    printf("  mEra              @ +0x%02tx (Xbox: 0x44)\n", (uintptr_t)&var.mEra - base);
    printf("  mSongName         @ +0x%02tx (Xbox: 0x48)\n", (uintptr_t)&var.mSongName - base);
    printf("  mAvgBeatsPerSec   @ +0x%02tx (Xbox: 0x4c)\n", (uintptr_t)&var.mAvgBeatsPerSec - base);
    printf("  mFlags            @ +0x%02tx (Xbox: 0x50)\n", (uintptr_t)&var.mFlags - base);

    // Verify relative ordering is preserved (offsets will differ on 64-bit)
    EXPECT_LT((uintptr_t)&var.mPositionOffset, (uintptr_t)&var.mVariantName);
    EXPECT_LT((uintptr_t)&var.mVariantName, (uintptr_t)&var.mMoveParent);
    EXPECT_LT((uintptr_t)&var.mLinkedTo, (uintptr_t)&var.mLinkedFrom);
    EXPECT_LT((uintptr_t)&var.mAvgBeatsPerSec, (uintptr_t)&var.mFlags);

    printf("  === std::vector<MoveCandidate> ===\n");
    printf("  sizeof(std::vector<MoveCandidate>) = %zu (Xbox: 0xC = 12)\n",
           sizeof(std::vector<MoveCandidate>));

    // The 0x3f800000 crash: if mAvgBeatsPerSec (float, at Xbox 0x4c) were
    // somehow read as a pointer, its IEEE754 bit pattern 0x3f800000 would
    // be the crash address. Check if any struct size expansion could cause
    // a field to be misread.
    printf("  === Pointer size impact ===\n");
    printf("  sizeof(void*) = %zu (Xbox: 4)\n", sizeof(void *));
    printf("  sizeof(Symbol) = %zu (Xbox: 4)\n", sizeof(Symbol));
}

// ============================================================================
// MoveCandidate::CacheLinks — the critical path
// ============================================================================

TEST_F(MoveGraphTest, MoveCandidateCacheLinksResolvesName) {
    // Build two variants via binary Load, then CacheLinks candidates between them
    MoveGraph *graph = Hmx::Object::New<MoveGraph>();

    // Load variant "test_variant_1" into graph
    std::vector<uint8_t> v1Buf;
    BuildMoveVariant(v1Buf, 1, 0, 0, 0,
        "test_variant_1", "idle.move", "idle",
        "pop", "modern", "test_song",
        2.0f, 0x02, nullptr, nullptr, {}, {});

    MemBinStream ms1(v1Buf.data(), v1Buf.size(), false);
    MoveVariant *var1 = new MoveVariant();
    MoveParent parent;
    var1->Load(ms1, graph, &parent);
    ASSERT_FALSE(ms1.Fail());

    // Now create a MoveCandidate from binary with name = "test_variant_1"
    std::vector<uint8_t> candBuf;
    BuildMoveCandidate(candBuf, 1, 0x04, "unused", "test_variant_1", "unused");

    MemBinStream ms2(candBuf.data(), candBuf.size(), false);
    MoveCandidate cand;
    cand.Load(ms2);

    // CacheLinks should resolve the name to the variant pointer
    cand.CacheLinks(graph);

    EXPECT_EQ(cand.mAdjacencyFlag & 1, 1u) << "Bit 0 should be set after CacheLinks";
    EXPECT_EQ(cand.mValue.mVariant, var1) << "Should resolve to the graph's variant";

    delete var1;
    delete graph;
}

TEST_F(MoveGraphTest, MoveCandidateCacheLinksWithBit0SetWouldCrash) {
    // This test documents the crash scenario: if bit 0 is set after Load,
    // CacheLinks dereferences mValue.mVariantName (a const char*) as a
    // MoveVariant*. Without the bit-clearing fix, this would SIGSEGV.
    std::vector<uint8_t> buf;
    // adjacencyFlag=0x05 has bit 0 set — dangerous without the fix
    BuildMoveCandidate(buf, 1, 0x05, "unused", "some_variant", "unused");

    MemBinStream ms(buf.data(), buf.size(), false);
    MoveCandidate cand;
    cand.Load(ms);

    // With the fix, bit 0 is cleared. Verify:
    EXPECT_EQ(cand.mAdjacencyFlag & 1, 0u)
        << "Load must clear bit 0 to prevent CacheLinks from misinterpreting "
           "const char* as MoveVariant*";

    // The union should hold a valid const char* (from Symbol interning)
    EXPECT_NE(cand.mValue.mVariantName, nullptr);
    EXPECT_STREQ(cand.mValue.mVariantName, "some_variant");
}
