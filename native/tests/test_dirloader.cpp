// DirLoader integration tests — requires full engine initialization
#include "test_helpers.h"
#include "obj/Dir.h"
#include "obj/DirLoader.h"
#include "utl/ChunkStream.h"
#include "utl/FilePath.h"
#include "utl/Loader.h"

extern void ReadDead(BinStream &);

// ============================================================================
// Fixture: ensures engine is initialized once for all tests in this suite
// ============================================================================

class DirLoaderTest : public EngineTestFixture {};

// ============================================================================
// StreamPositionTracking — manually read the DirLoader header fields
// from a known .milo file and verify Tell() stays coherent.
//
// DirLoader header format (rev >= 28):
//   int mRev
//   Symbol dirClass (length-prefixed string)
//   Symbol dirName
//   int numEntries
//   for each entry: Symbol className, Symbol objName
//   ... then dir data + object data
// ============================================================================

TEST_F(DirLoaderTest, StreamPositionTracking) {
    // Find a test .milo file — we need game data for this
    // Try a small, known-working file first
    const char *testFiles[] = {
        "world/shared/props/gen/discoball.milo_xbox",
        "world/shared/lighting/gen/shared_lights.milo_xbox",
        nullptr
    };

    // Resolve via the engine's file system
    const char *found = nullptr;
    for (int i = 0; testFiles[i]; i++) {
        FilePath fp(testFiles[i]);
        const char *resolved = fp.c_str();
        if (resolved && resolved[0]) {
            // Try to open it
            ChunkStream *cs = new ChunkStream(resolved, ChunkStream::kRead, 0x8000, false, kPlatformNone, false);
            if (!cs->Fail()) {
                delete cs;
                found = testFiles[i];
                break;
            }
            delete cs;
        }
    }

    if (!found) {
        GTEST_SKIP() << "No test .milo files found (need game data)";
    }

    printf("DirLoaderTest: using %s\n", found);

    ChunkStream cs(found, ChunkStream::kRead, 0x8000, false, kPlatformNone, false);
    ASSERT_FALSE(cs.Fail());

    // Process chunk header
    EofType eof = cs.Eof();
    ASSERT_EQ(eof, NotEof);

    // Read mRev
    int mRev;
    cs >> mRev;
    printf("  mRev = %d (tell=%d)\n", mRev, cs.Tell());
    EXPECT_GE(mRev, 25) << "mRev should be >= 25 for DC3 files";
    EXPECT_LE(mRev, 35) << "mRev suspiciously high";
    EXPECT_EQ(cs.Tell(), 4);

    // Read dirClass
    Symbol dirClass;
    cs >> dirClass;
    printf("  dirClass = '%s' (tell=%d)\n", dirClass.Str(), cs.Tell());
    EXPECT_NE(strlen(dirClass.Str()), 0u) << "dirClass should not be empty";

    int afterClass = cs.Tell();

    // Read dirName (only if mRev >= some version — usually present)
    if (mRev > 1) {
        Symbol dirName;
        cs >> dirName;
        printf("  dirName = '%s' (tell=%d)\n", dirName.Str(), cs.Tell());
    }

    // Read number of entries
    int numEntries;
    cs >> numEntries;
    printf("  numEntries = %d (tell=%d)\n", numEntries, cs.Tell());
    EXPECT_GE(numEntries, 0);
    EXPECT_LT(numEntries, 10000) << "Suspiciously many entries";

    // Read each entry's class+name
    for (int i = 0; i < numEntries && i < 10; i++) {
        Symbol className, objName;
        cs >> className;
        cs >> objName;
        printf("  entry[%d]: class='%s' name='%s' (tell=%d)\n",
               i, className.Str(), objName.Str(), cs.Tell());
    }

    printf("  Header parsed successfully. Final tell=%d\n", cs.Tell());
}

// ============================================================================
// LoadSimpleMilo — use DirLoader::LoadObjects to load a known-working file
// ============================================================================

TEST_F(DirLoaderTest, LoadSimpleMilo) {
    const char *testFiles[] = {
        "world/shared/props/gen/discoball.milo_xbox",
        nullptr
    };

    const char *found = nullptr;
    for (int i = 0; testFiles[i]; i++) {
        FilePath fp(testFiles[i]);
        // Check if file can be opened
        ChunkStream *cs = new ChunkStream(fp.c_str(), ChunkStream::kRead, 0x8000, false, kPlatformNone, false);
        if (!cs->Fail()) {
            delete cs;
            found = testFiles[i];
            break;
        }
        delete cs;
    }

    if (!found) {
        GTEST_SKIP() << "No test .milo files found (need game data)";
    }

    printf("DirLoaderTest::LoadSimpleMilo: loading %s\n", found);

    FilePath fp(found);
    ObjectDir *dir = DirLoader::LoadObjects(fp, nullptr, nullptr);

    ASSERT_NE(dir, nullptr) << "DirLoader::LoadObjects returned null for " << found;
    printf("  Loaded: '%s' class='%s'\n", dir->Name(), dir->ClassName().Str());

    // Basic sanity checks
    EXPECT_NE(strlen(dir->Name()), 0u);
}

// ============================================================================
// LoadWithoutDesync — parameterized test over multiple .milo files
// Each file should load without crash, ASan error, or desync.
// ============================================================================

class LoadMiloParam : public EngineTestFixture,
                      public ::testing::WithParamInterface<const char *> {};

TEST_P(LoadMiloParam, LoadWithoutDesync) {
    const char *miloFile = GetParam();

    FilePath fp(miloFile);
    ChunkStream *probe = new ChunkStream(fp.c_str(), ChunkStream::kRead, 0x8000, false, kPlatformNone, false);
    if (probe->Fail()) {
        delete probe;
        GTEST_SKIP() << "File not found: " << miloFile;
    }
    delete probe;

    printf("LoadWithoutDesync: %s\n", miloFile);
    ObjectDir *dir = DirLoader::LoadObjects(fp, nullptr, nullptr);
    ASSERT_NE(dir, nullptr) << "Failed to load " << miloFile;
    printf("  OK: '%s' class='%s'\n", dir->Name(), dir->ClassName().Str());
}

// Progressively more complex .milo files
INSTANTIATE_TEST_SUITE_P(
    MiloFiles,
    LoadMiloParam,
    ::testing::Values(
        "world/shared/props/gen/discoball.milo_xbox",
        "world/shared/lighting/gen/shared_lights.milo_xbox"
        // Add more files here as they are verified to work
        // "char/main/gen/skeleton.milo_xbox"  // <-- the problem file
    )
);

// ============================================================================
// DeadMarkerInRealFile — verify that after parsing the header, the stream
// is at a coherent position for reading object data.
// ============================================================================

TEST_F(DirLoaderTest, DeadMarkerInRealFile) {
    const char *testFiles[] = {
        "world/shared/props/gen/discoball.milo_xbox",
        nullptr
    };

    const char *found = nullptr;
    for (int i = 0; testFiles[i]; i++) {
        FilePath fp(testFiles[i]);
        ChunkStream *cs = new ChunkStream(fp.c_str(), ChunkStream::kRead, 0x8000, false, kPlatformNone, false);
        if (!cs->Fail()) {
            delete cs;
            found = testFiles[i];
            break;
        }
        delete cs;
    }

    if (!found) {
        GTEST_SKIP() << "No test .milo files found";
    }

    ChunkStream cs(found, ChunkStream::kRead, 0x8000, false, kPlatformNone, false);
    ASSERT_FALSE(cs.Fail());
    ASSERT_EQ(cs.Eof(), NotEof);

    // Read header: mRev, dirClass, dirName (if rev>1), numEntries, entries
    int mRev;
    cs >> mRev;

    Symbol dirClass;
    cs >> dirClass;

    if (mRev > 1) {
        Symbol dirName;
        cs >> dirName;
    }

    int numEntries;
    cs >> numEntries;

    for (int i = 0; i < numEntries; i++) {
        Symbol cn, on;
        cs >> cn;
        cs >> on;
    }

    int headerEnd = cs.Tell();
    printf("DeadMarkerInRealFile: header ends at tell=%d, mRev=%d, %d entries\n",
           headerEnd, mRev, numEntries);

    // The stream should now be positioned at the dir's PreLoad data.
    // We can't easily verify what's there without knowing the format,
    // but we can check that reading doesn't immediately fail.
    EXPECT_FALSE(cs.Fail()) << "Stream in failed state after header parse";
    EXPECT_NE(cs.Eof(), RealEof) << "Unexpected EOF right after header";
}
