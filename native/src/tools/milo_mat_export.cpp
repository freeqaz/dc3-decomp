// milo-mat-export — CLI tool to extract materials from .milo_xbox files as JSON.
//
// Usage: milo-mat-export <input.milo_xbox> <output-dir> [--verbose]

#include "os/Debug.h"
#include "os/System.h"
#include "obj/Dir.h"
#include "obj/DirLoader.h"
#include "rndobj/Rnd.h"
#include "rndobj/Cam.h"
#include "char/Char.h"
#include "world/World.h"
#include "hamobj/Ham.h"
#include "flow/Flow.h"
#include "utl/FilePath.h"
#include "utl/MakeString.h"

#include "export/MaterialExporter.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <climits>

extern Rnd& TheRnd;
extern void NativeDetectDataDir();
void SetFileChecksumData();

int main(int argc, char* argv[]) {
    const char* miloPath = nullptr;
    const char* outputDir = nullptr;
    bool verbose = false;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--verbose") == 0 || strcmp(argv[i], "-v") == 0) {
            verbose = true;
        } else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            printf("milo-mat-export — Extract materials from .milo_xbox as JSON\n\n");
            printf("Usage: milo-mat-export <input.milo_xbox> <output-dir> [--verbose]\n");
            return 0;
        } else if (!miloPath) {
            miloPath = argv[i];
        } else if (!outputDir) {
            outputDir = argv[i];
        }
    }

    if (!miloPath || !outputDir) {
        fprintf(stderr, "Usage: milo-mat-export <input.milo_xbox> <output-dir> [--verbose]\n");
        return 1;
    }

    char absPath[PATH_MAX];
    if (!realpath(miloPath, absPath)) {
        fprintf(stderr, "Error: cannot resolve path '%s'\n", miloPath);
        return 1;
    }

    // Engine init (headless — no GPU)
    setenv("MILO_RENDER", "0", 1);

    InitMakeString();
    SetFileChecksumData();
    SystemPreInit(argc, argv, "config/ham_preinit_keep.dta");
    TheRnd.PreInit();
    SystemInit("config/ham_keep.dta");
    TheRnd.Init();

    FlowInit();
    CharInit();
    WorldInit();
    HamInit();

    // Load .milo file
    if (verbose) printf("Loading %s...\n", absPath);

    ObjDirPtr<ObjectDir> baseDir;
    FilePath fp(absPath);
    baseDir.LoadFile(fp, false, false, kLoadFront, false);

    ObjectDir* scene = baseDir;
    if (!scene) {
        fprintf(stderr, "Error: failed to load '%s'\n", absPath);
        return 1;
    }

    if (verbose) printf("Loaded '%s' (class '%s')\n", scene->Name(), scene->ClassName().Str());

    // Export
    MaterialExporter::Options opts;
    opts.verbose = verbose;
    int count = MaterialExporter::ExportAll(scene, outputDir, opts);
    printf("Exported %d materials to %s\n", count, outputDir);

    return 0;
}
