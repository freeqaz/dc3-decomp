// Milo Diagnostic Harness — not pass/fail, a debugging tool
//
// Usage:
//   MILO_DIAG_FILE=char/main/gen/skeleton.milo_xbox ./milo-tests --gtest_filter=MiloDiagnostic.WalkFile
//
// Or for unreread boundary testing:
//   ./milo-tests --gtest_filter=MiloDiagnostic.UnrereadBoundarySafety

#include "test_helpers.h"
#include "obj/Dir.h"
#include "obj/DirLoader.h"
#include "utl/ChunkStream.h"
#include "utl/FilePath.h"

extern void ReadDead(BinStream &);

class MiloDiagnostic : public EngineTestFixture {};

// ============================================================================
// WalkFile — manually parse header and log every field with position,
// then attempt full DirLoader::LoadObjects load.
// ============================================================================

TEST_F(MiloDiagnostic, WalkFile) {
    const char *diagFile = getenv("MILO_DIAG_FILE");
    if (!diagFile || !diagFile[0]) {
        GTEST_SKIP() << "Set MILO_DIAG_FILE env var to run this diagnostic";
    }

    printf("\n========================================\n");
    printf("MILO DIAGNOSTIC: %s\n", diagFile);
    printf("========================================\n\n");

    // Phase 1: Manual header parse with position logging
    printf("--- Phase 1: Manual header parse ---\n\n");

    ChunkStream cs(diagFile, ChunkStream::kRead, 0x8000, false, kPlatformNone, false);
    if (cs.Fail()) {
        // Try with FilePath resolution
        FilePath fp(diagFile);
        printf("Direct open failed, trying FilePath: %s\n", fp.c_str());
        GTEST_SKIP() << "Cannot open " << diagFile;
    }

    EofType eof = cs.Eof();
    printf("After Eof() init: eof=%d platform=%d littleEndian=%d\n",
           eof, cs.GetPlatform(), cs.LittleEndian());

    if (eof != NotEof) {
        printf("ERROR: Unexpected eof=%d after init\n", eof);
        FAIL() << "Eof not NotEof after header read";
    }

    // mRev
    int mRev;
    cs >> mRev;
    printf("[tell=%6d] mRev = %d (0x%x)\n", cs.Tell(), mRev, mRev);

    // dirClass
    Symbol dirClass;
    cs >> dirClass;
    printf("[tell=%6d] dirClass = '%s'\n", cs.Tell(), dirClass.Str());

    // dirName (if mRev > 1)
    Symbol dirName;
    if (mRev > 1) {
        cs >> dirName;
        printf("[tell=%6d] dirName = '%s'\n", cs.Tell(), dirName.Str());
    }

    // numEntries
    int numEntries;
    cs >> numEntries;
    printf("[tell=%6d] numEntries = %d\n", cs.Tell(), numEntries);

    if (numEntries < 0 || numEntries > 10000) {
        printf("ERROR: numEntries=%d looks wrong, aborting manual parse\n", numEntries);
        FAIL() << "Suspicious numEntries";
    }

    // Entry list
    printf("\n--- Object entries ---\n");
    for (int i = 0; i < numEntries; i++) {
        int preTell = cs.Tell();
        Symbol className, objName;
        cs >> className;
        cs >> objName;
        if (i < 20 || i == numEntries - 1) {
            printf("[tell=%6d→%6d] entry[%3d]: class='%s' name='%s'\n",
                   preTell, cs.Tell(), i, className.Str(), objName.Str());
        } else if (i == 20) {
            printf("  ... (%d more entries) ...\n", numEntries - 21);
        }
    }

    int headerEnd = cs.Tell();
    printf("\n[tell=%6d] Header parse complete\n", headerEnd);
    printf("  mRev=%d dirClass='%s' dirName='%s' numEntries=%d\n",
           mRev, dirClass.Str(), dirName.Str(), numEntries);

    // Phase 2: Try reading a few more bytes to see what follows
    printf("\n--- Phase 2: Post-header data peek ---\n");

    if (cs.Eof() == NotEof) {
        // Read next 16 bytes as hex dump
        printf("[tell=%6d] Next 16 bytes:", cs.Tell());
        for (int i = 0; i < 16 && cs.Eof() == NotEof; i++) {
            unsigned char b;
            cs >> b;
            printf(" %02x", b);
        }
        printf("\n");
    }

    printf("\n--- Phase 3: Full DirLoader::LoadObjects ---\n\n");

    // Attempt full load
    FilePath fp(diagFile);
    ObjectDir *dir = DirLoader::LoadObjects(fp, nullptr, nullptr);

    if (dir) {
        printf("SUCCESS: Loaded '%s' class='%s'\n", dir->Name(), dir->ClassName().Str());

        // Count objects by type
        int totalObjects = 0;
        ObjDirItr<Hmx::Object> it(dir, true);
        while (it) {
            totalObjects++;
            ++it;
        }
        printf("  Total objects: %d\n", totalObjects);
    } else {
        printf("FAILED: DirLoader::LoadObjects returned null\n");
    }

    printf("\n========================================\n");
    printf("DIAGNOSTIC COMPLETE\n");
    printf("========================================\n");
}

// ============================================================================
// UnrereadBoundarySafety — synthetic 2-chunk file testing Unreread at
// exact chunk boundary, mimicking what DirLoader::LoadObjs does.
// ============================================================================

TEST_F(MiloDiagnostic, UnrereadBoundarySafety) {
    printf("\n--- UnrereadBoundarySafety ---\n");

    // Craft two chunks where we'll exercise the peek-unreread pattern
    // at various offsets relative to the chunk boundary.
    //
    // Chunk 0: 16 bytes (4 ints)
    // Chunk 1: 16 bytes (4 ints)

    std::vector<uint8_t> chunk0;
    PutBE32(chunk0, 0x11111111);
    PutBE32(chunk0, 0x22222222);
    PutBE32(chunk0, 0x33333333);
    PutBE32(chunk0, 0x44444444);

    std::vector<uint8_t> chunk1;
    PutBE32(chunk1, 0x55555555);
    PutBE32(chunk1, 0x66666666);
    PutBE32(chunk1, 0x77777777);
    PutBE32(chunk1, 0x88888888);

    std::string path = "/tmp/claude-1000/milo_tests/unreread_boundary_diag.milo_xbox";
    ASSERT_TRUE(WriteSyntheticMilo(path.c_str(), {chunk0, chunk1}));

    ChunkStream cs(path.c_str(), ChunkStream::kRead, 0x8000, false, kPlatformNone, false);
    ASSERT_FALSE(cs.Fail());
    ASSERT_EQ(cs.Eof(), NotEof);

    // Read 3 ints from chunk 0 (12 bytes consumed, 4 remaining)
    int v;
    cs >> v; EXPECT_EQ(v, 0x11111111);
    cs >> v; EXPECT_EQ(v, 0x22222222);
    cs >> v; EXPECT_EQ(v, 0x33333333);

    printf("After 3 reads: tell=%d\n", cs.Tell());

    // Read 4th int (last in chunk 0)
    cs >> v;
    EXPECT_EQ(v, 0x44444444);
    printf("After 4th read (end of chunk 0): tell=%d\n", cs.Tell());

    // Now at chunk boundary — advance to chunk 1
    EofType e = cs.Eof();
    printf("After Eof() call: eof=%d tell=%d\n", e, cs.Tell());
    ASSERT_EQ(e, NotEof);

    // Peek 4 bytes from chunk 1
    int peek;
    cs >> peek;
    printf("Peek from chunk 1: 0x%08X tell=%d\n", peek, cs.Tell());
    EXPECT_EQ(peek, 0x55555555);

    // Unreread — should be safe since peek was entirely within chunk 1
    cs.Unreread(4);
    printf("After Unreread(4): tell=%d\n", cs.Tell());

    // Re-read all 4 ints from chunk 1
    int vals[4];
    for (int i = 0; i < 4; i++) {
        cs >> vals[i];
        printf("Chunk 1 int[%d]: 0x%08X tell=%d\n", i, vals[i], cs.Tell());
    }

    EXPECT_EQ(vals[0], 0x55555555);
    EXPECT_EQ(vals[1], 0x66666666);
    EXPECT_EQ(vals[2], 0x77777777);
    EXPECT_EQ(vals[3], (int)0x88888888);

    printf("UnrereadBoundarySafety: all reads correct\n");
}
