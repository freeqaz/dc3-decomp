// milo2gltf — CLI tool to convert .milo_xbox scenes to glTF 2.0.
//
// Usage: milo2gltf <input.milo_xbox> -o <output.gltf> [--glb] [--no-animations] [--no-skins] [--verbose]

#include "os/Debug.h"
#include "os/System.h"
#include "obj/Dir.h"
#include "obj/DirLoader.h"
#include "rndobj/Rnd.h"
#include "rndobj/Cam.h"
#include "rndobj/Dir.h"
#include "char/Char.h"
#include "world/World.h"
#include "hamobj/Ham.h"
#include "flow/Flow.h"
#include "utl/FilePath.h"
#include "utl/MakeString.h"

#include "export/GltfExporter.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <climits>

extern Rnd& TheRnd;
extern void NativeDetectDataDir();
void SetFileChecksumData();

int main(int argc, char* argv[]) {
    const char* miloPath = nullptr;
    const char* outputPath = nullptr;
    bool binary = false;
    bool animations = true;
    bool skins = true;
    bool verbose = false;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            printf("milo2gltf — Convert .milo_xbox scenes to glTF 2.0\n\n");
            printf("Usage: milo2gltf <input.milo_xbox> -o <output.gltf> [options]\n\n");
            printf("Options:\n");
            printf("  -o <path>          Output glTF file path\n");
            printf("  --glb              Output binary glTF (.glb)\n");
            printf("  --no-animations    Skip animation export\n");
            printf("  --no-skins         Skip skinned mesh export\n");
            printf("  --verbose, -v      Print detailed info\n");
            return 0;
        } else if (strcmp(argv[i], "-o") == 0 && i + 1 < argc) {
            outputPath = argv[++i];
        } else if (strcmp(argv[i], "--glb") == 0) {
            binary = true;
        } else if (strcmp(argv[i], "--no-animations") == 0) {
            animations = false;
        } else if (strcmp(argv[i], "--no-skins") == 0) {
            skins = false;
        } else if (strcmp(argv[i], "--verbose") == 0 || strcmp(argv[i], "-v") == 0) {
            verbose = true;
        } else if (!miloPath) {
            miloPath = argv[i];
        }
    }

    if (!miloPath) {
        fprintf(stderr, "Error: no input .milo file specified\n");
        fprintf(stderr, "Usage: milo2gltf <input.milo_xbox> -o <output.gltf> [options]\n");
        return 1;
    }

    // Default output path: input name + .gltf
    char defaultOutput[PATH_MAX];
    if (!outputPath) {
        // Strip extension and add .gltf
        const char* base = strrchr(miloPath, '/');
        base = base ? base + 1 : miloPath;
        snprintf(defaultOutput, sizeof(defaultOutput), "%.*s.gltf",
                 (int)(strchr(base, '.') ? strchr(base, '.') - base : strlen(base)), base);
        outputPath = defaultOutput;
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

    // Sync if RndDir
    RndDir* rndScene = dynamic_cast<RndDir*>(scene);
    if (rndScene) rndScene->SyncObjects();

    if (verbose) printf("Loaded '%s' (class '%s')\n", scene->Name(), scene->ClassName().Str());

    // Export
    GltfExporter::Options opts;
    opts.binary = binary;
    opts.animations = animations;
    opts.skins = skins;
    opts.verbose = verbose;

    bool ok = GltfExporter::Export(scene, outputPath, opts);
    if (ok) {
        printf("Exported glTF to %s\n", outputPath);
    } else {
        fprintf(stderr, "Error: glTF export failed\n");
        return 1;
    }

    return 0;
}
