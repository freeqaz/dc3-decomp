// FlowAnimate stream desync diagnostic test
//
// Isolates the desync in timey_wimey_elements.milo by:
// 1. Loading the file with DirLoader and tracking per-object byte consumption
// 2. Comparing tell() before/after each PreLoad+PostLoad to find which object
//    consumes the wrong number of bytes
// 3. For FlowAnimate objects specifically, instrumenting each field read
//
// Usage:
//   ./milo-tests --gtest_filter=FlowDesync.*
//
// Or to test a specific file:
//   MILO_DIAG_FILE=ui/visualizer/timey_wimey_elements.milo_xbox \
//     ./milo-tests --gtest_filter=FlowDesync.TrackObjectBytes

#include "test_helpers.h"
#include "obj/Dir.h"
#include "obj/DirLoader.h"
#include "utl/ChunkStream.h"
#include "utl/FilePath.h"
#include "utl/Loader.h"

extern void ReadDead(BinStream &);

class FlowDesync : public EngineTestFixture {};

// ============================================================================
// TrackObjectBytes — manually parse the DirLoader header, then call
// PreLoad/PostLoad on each object while logging tell() around every call.
// This finds exactly which object consumes wrong byte count.
// ============================================================================

TEST_F(FlowDesync, TrackObjectBytes) {
    const char *diagFile = getenv("MILO_DIAG_FILE");
    if (!diagFile || !diagFile[0]) {
        diagFile = "ui/visualizer/gen/timey_wimey_elements.milo_xbox";
    }

    printf("\n=== FlowDesync::TrackObjectBytes ===\n");
    printf("File: %s\n\n", diagFile);

    ChunkStream cs(diagFile, ChunkStream::kRead, 0x8000, false, kPlatformNone, false);
    if (cs.Fail()) {
        GTEST_SKIP() << "Cannot open " << diagFile;
    }

    EofType eof = cs.Eof();
    ASSERT_EQ(eof, NotEof);

    // --- Parse DirLoader header (must match DirLoader::LoadHeader exactly) ---
    int mRev;
    cs >> mRev;
    printf("mRev = %d (0x%x)\n", mRev, mRev);

    Symbol dirClass;
    char dirNameBuf[0x80] = {};

    if (mRev > 0xD) {
        // mRev > 13: Symbol dirClass + ReadString dirName + size1,size2
        cs >> dirClass;
        printf("dirClass = '%s' (tell=%d)\n", dirClass.Str(), cs.Tell());

        // DirLoader uses ReadString(buf, 0x80) — same as length-prefixed string
        cs.ReadString(dirNameBuf, 0x80);
        printf("dirName = '%s' (tell=%d)\n", dirNameBuf, cs.Tell());

        int size1, size2;
        cs >> size1 >> size2;
        printf("size1=%d size2=%d (tell=%d)\n", size1, size2, cs.Tell());

        // mRev > 0x1c: extra bool field
        if (mRev > 0x1c) {
            bool unk9a;
            cs >> unk9a;
            printf("unk9a=%d (tell=%d)\n", (int)unk9a, cs.Tell());
        }
    } else if (mRev > 0xC) {
        // mRev 13: just a symbol, then ObjectDir::Load
        cs >> dirClass;
        printf("dirClass = '%s' (tell=%d)\n", dirClass.Str(), cs.Tell());
        printf("WARNING: mRev=0xD format — dir load not replicated here\n");
    } else {
        printf("WARNING: mRev < 0xD not supported by this test\n");
        GTEST_SKIP() << "mRev " << mRev << " too old for this test";
    }

    int numEntries;
    cs >> numEntries;
    printf("numEntries = %d (tell=%d)\n\n", numEntries, cs.Tell());

    // Read entry list (matches DirLoader::CreateObjects format)
    struct ObjEntry {
        Symbol className;
        char objName[0x80];
    };
    std::vector<ObjEntry> entries;
    entries.reserve(numEntries);

    for (int i = 0; i < numEntries; i++) {
        ObjEntry e;
        cs >> e.className;
        cs.ReadString(e.objName, 0x80);
        entries.push_back(e);
    }

    printf("--- %d entries parsed, tell=%d ---\n\n", numEntries, cs.Tell());

    // Print first 30 and last 5
    for (int i = 0; i < numEntries; i++) {
        if (i < 30 || i >= numEntries - 5) {
            printf("  [%3d] class='%-20s' name='%s'\n",
                   i, entries[i].className.Str(), entries[i].objName);
        } else if (i == 30) {
            printf("  ... (%d more) ...\n", numEntries - 35);
        }
    }

    // --- Create objects ---
    printf("\n--- Creating objects ---\n");
    std::vector<Hmx::Object *> objects;
    objects.reserve(numEntries);

    ObjectDir *tempDir = dynamic_cast<ObjectDir *>(Hmx::Object::NewObject(Symbol("ObjectDir")));

    for (int i = 0; i < numEntries; i++) {
        Symbol cls = entries[i].className;
        Hmx::Object *obj = nullptr;

        if (Hmx::Object::RegisteredFactory(cls)) {
            obj = Hmx::Object::NewObject(cls);
            if (obj) {
                obj->SetName(entries[i].objName, tempDir);
            }
        }

        if (!obj) {
            printf("  [%3d] FAILED to create class='%s' — not registered\n",
                   i, cls.Str());
            // Create a dummy Object so indices stay aligned
            obj = new Hmx::Object();
            obj->SetName(entries[i].objName, tempDir);
        } else {
            // Verify factory created the right type
            if (obj->ClassName() != cls) {
                printf("  [%3d] WARNING: requested '%s' got '%s' (wrong NEW_OBJ?)\n",
                       i, cls.Str(), obj->ClassName().Str());
            }
        }
        objects.push_back(obj);
    }
    printf("  Created %d objects\n\n", (int)objects.size());

    // --- Load dir PreLoad/PostLoad ---
    printf("--- Loading dir ---\n");
    int dirPreTell = cs.Tell();
    tempDir->PreLoad(cs);
    int dirPostTell = cs.Tell();
    printf("  Dir PreLoad: tell %d → %d (consumed %d bytes)\n",
           dirPreTell, dirPostTell, dirPostTell - dirPreTell);

    tempDir->PostLoad(cs);
    int dirPostLoadTell = cs.Tell();
    printf("  Dir PostLoad: tell %d → %d (consumed %d bytes)\n",
           dirPostTell, dirPostLoadTell, dirPostLoadTell - dirPostTell);

    // ReadDead after dir
    ReadDead(cs);
    printf("  ReadDead: tell → %d\n\n", cs.Tell());

    // --- Load each object: PreLoad + PostLoad + ReadDead ---
    printf("--- Loading objects (PreLoad + PostLoad + ReadDead) ---\n\n");

    int desyncCount = 0;
    int lastGoodTell = cs.Tell();

    for (int i = 0; i < (int)objects.size(); i++) {
        Hmx::Object *obj = objects[i];
        if (!obj) continue;

        int preTell = cs.Tell();

        if (cs.Eof() == RealEof) {
            printf("  [%3d] '%s' (%s): RealEof at tell=%d — stopping\n",
                   i, obj->Name(), obj->ClassName().Str(), preTell);
            break;
        }

        // Check for nested ObjectDir (DirLoader format)
        ObjectDir *dirObj = dynamic_cast<ObjectDir *>(obj);
        if (dirObj && obj->ClassName() != Symbol("Object")) {
            int peekVal;
            cs >> peekVal;
            cs.Unreread(4);

            bool isDirLoaderFormat = (peekVal & 0xFFFF0000) == 0
                                  && (peekVal & 0xFFFF) > 28;
            if (isDirLoaderFormat) {
                printf("  [%3d] '%s' (%s): NESTED DIR (peek=0x%x) tell=%d — skipping via LoadObjects\n",
                       i, obj->Name(), obj->ClassName().Str(), peekVal, preTell);
                ObjectDir *subDir = DirLoader::LoadObjects(FilePath(diagFile), nullptr, &cs);
                if (subDir) delete subDir;
                ReadDead(cs);
                printf("         → tell=%d (consumed %d bytes)\n", cs.Tell(), cs.Tell() - preTell);
                lastGoodTell = cs.Tell();
                continue;
            }
        }

        // Normal PreLoad + PostLoad
        obj->PreLoad(cs);
        int midTell = cs.Tell();
        obj->PostLoad(cs);
        int postTell = cs.Tell();

        // ReadDead
        ReadDead(cs);
        int afterDead = cs.Tell();

        int totalConsumed = afterDead - preTell;

        // Check for desync indicators
        bool suspicious = false;
        if (cs.Fail()) {
            suspicious = true;
        }

        // Print every object for now (can filter later)
        printf("  [%3d] %-20s %-15s tell: %6d → pre=%6d post=%6d dead=%6d  (%d bytes)",
               i, obj->Name(), obj->ClassName().Str(),
               preTell, midTell, postTell, afterDead, totalConsumed);

        if (suspicious) {
            printf(" *** STREAM FAIL ***");
            desyncCount++;
        }
        if (obj->ClassName() == Symbol("Object") && entries[i].className != Symbol("Object")) {
            printf(" *** WRONG TYPE (wanted %s) ***", entries[i].className.Str());
            desyncCount++;
        }
        printf("\n");

        lastGoodTell = afterDead;

        // If we've seen too many suspicious reads, stop
        if (desyncCount > 5) {
            printf("\n  !!! Too many desync indicators, stopping at object %d !!!\n", i);
            break;
        }
    }

    printf("\n=== Summary ===\n");
    printf("  Objects: %d\n", (int)objects.size());
    printf("  Desync indicators: %d\n", desyncCount);
    printf("  Last good tell: %d\n", lastGoodTell);
    printf("  Stream fail: %s\n", cs.Fail() ? "YES" : "no");
    printf("=== Done ===\n");
}

// ============================================================================
// FlowAnimateFieldTrace — load a single FlowAnimate from the real stream
// with per-field tell() logging. Uses MILO_DIAG_OBJECT env var to select
// which object index to trace.
//
// Usage:
//   MILO_DIAG_OBJECT=42 ./milo-tests --gtest_filter=FlowDesync.FlowAnimateFieldTrace
// ============================================================================

TEST_F(FlowDesync, FlowAnimateFieldTrace) {
    const char *diagFile = getenv("MILO_DIAG_FILE");
    if (!diagFile || !diagFile[0]) {
        diagFile = "ui/visualizer/gen/timey_wimey_elements.milo_xbox";
    }

    const char *objIdxStr = getenv("MILO_DIAG_OBJECT");
    int targetIdx = objIdxStr ? atoi(objIdxStr) : -1;

    printf("\n=== FlowDesync::FlowAnimateFieldTrace ===\n");
    printf("File: %s\n", diagFile);
    printf("Target object index: %d%s\n\n",
           targetIdx, targetIdx < 0 ? " (will trace first FlowAnimate)" : "");

    // Use DirLoader to get the full context, but hook into the loading
    // Let's just use DirLoader::LoadObjects and rely on our instrumented
    // printfs to see what happens
    FilePath fp(diagFile);
    printf("Loading via DirLoader::LoadObjects...\n\n");
    ObjectDir *dir = DirLoader::LoadObjects(fp, nullptr, nullptr);

    if (dir) {
        printf("\nLoaded '%s' class='%s'\n", dir->Name(), dir->ClassName().Str());

        int total = 0;
        ObjDirItr<Hmx::Object> it(dir, true);
        while (it) { total++; ++it; }
        printf("Total objects: %d\n", total);
    } else {
        printf("LoadObjects returned null\n");
    }
}
