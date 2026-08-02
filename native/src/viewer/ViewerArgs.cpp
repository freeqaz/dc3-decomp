#include "ViewerArgs.h"

#include <cstdlib>
#include <cstring>

void ViewerConfig::PrintHelp(FILE* f) {
    fprintf(f, "milo-viewer — Dance Central 3 .milo scene viewer\n\n");
    fprintf(f, "Usage: milo-viewer <path.milo_xbox> [options]\n\n");
    fprintf(f, "Options:\n");
    fprintf(f, "  --help                     Show this help message\n");
    fprintf(f, "  --screenshot <file.png>    Render headlessly and save screenshot (PNG)\n");
    fprintf(f, "  --output <file.png>        Alias for --screenshot\n");
    fprintf(f, "  --frames <count>           Max frames to render (video/interactive), screenshot warmup count\n");
    fprintf(f, "  --subdir <path.milo_xbox>  Load additional .milo as subdirectory (repeatable)\n");
    fprintf(f, "  --clips <path.milo_xbox>   Load CharClip animation directory\n");
    fprintf(f, "  --char-setup <path.milo_xbox>  Load base HamCharacter (uses FileMerger for outfit/viseme)\n");
    fprintf(f, "  --visemes <path.milo_xbox>  Load facial viseme clip directory\n");
    fprintf(f, "  --clip <name>              Play a specific clip by name\n");
    fprintf(f, "  --bpm <number>             Beats per minute for clip playback (default: 120)\n");
    fprintf(f, "  --video <output.mp4>       Record video via ffmpeg (headless)\n");
    fprintf(f, "  --duration <seconds>       Video duration in seconds (default: 10)\n");
    fprintf(f, "  --fps <number>             Video frame rate (default: 30)\n");
    fprintf(f, "  --camera <mode>            Camera mode: orbit, auto-orbit (default: orbit)\n");
    fprintf(f, "  --hide <pattern>           Hide meshes matching substring (repeatable)\n");
    fprintf(f, "  --azimuth <degrees>        Camera azimuth angle (default: ~23)\n");
    fprintf(f, "  --elevation <degrees>      Camera elevation angle (default: ~17)\n");
    fprintf(f, "  --frame <number>           Start at specific animation frame\n");
    fprintf(f, "  --distance <units>         Camera distance from target\n");
    fprintf(f, "  --eye <X> <Y> <Z>          Camera eye position (direct placement)\n");
    fprintf(f, "  --lookat <X> <Y> <Z>       Camera look-at point (use with --eye)\n");
    fprintf(f, "  --test-bone <name> <angle> [axis]  Rotate a bone for testing (default axis: x)\n");
    fprintf(f, "  --pose-dump <file.json>    Dump final pose transforms (JSON)\n");
    fprintf(f, "  --pose-dump-bones <csv>    Restrict pose dump to named bones\n");
    fprintf(f, "  --pose-dump-beat <value>   Beat for pose dump (number | START | MID)\n");
    fprintf(f, "  --speed <multiplier>       Animation speed (default: 1.0)\n");
    fprintf(f, "  --paused                   Start with animation paused\n");
    fprintf(f, "  --width <pixels>           Render width (default: 1280)\n");
    fprintf(f, "  --height <pixels>          Render height (default: 720)\n");
    fprintf(f, "  --verbose, -v              Print detailed object/drawable info\n");
    fprintf(f, "  --show-all-lods            Draw every LOD variant, including ones a\n");
    fprintf(f, "                             higher-detail sibling already covers\n");
    fprintf(f, "  --no-fallback-material     Do not give material-less meshes a neutral\n");
    fprintf(f, "                             grey; leave them undrawn as the file ships them\n");
    fprintf(f, "  --export-textures <dir>    Export all textures as PNG and exit\n");
    fprintf(f, "  --export-materials <dir>   Export all materials as JSON and exit\n");
    fprintf(f, "  --export-gltf <path>       Export scene as glTF 2.0 and exit\n");
    fprintf(f, "  --light <type> <X> <Y> <Z> <R> <G> <B> [intensity]\n");
    fprintf(f, "                             Add synthetic light (type: dir, point). Repeatable.\n");
    fprintf(f, "  --ambient <R> <G> <B>      Set ambient light color (0.0-1.0)\n");
    fprintf(f, "  --movie <file>             Play a video file via TexMovie pipeline (tests FFmpeg→GPU)\n\n");
    fprintf(f, "Controls (windowed mode):\n");
    fprintf(f, "  Left drag     orbit\n");
    fprintf(f, "  Scroll        zoom\n");
    fprintf(f, "  Middle drag   pan\n");
    fprintf(f, "  R             reset camera\n");
    fprintf(f, "  Space         pause/resume animation\n");
    fprintf(f, "  .             step forward one frame\n");
    fprintf(f, "  ,             step backward one frame\n");
    fprintf(f, "  Up/Down       double/halve animation speed\n");
    fprintf(f, "  Home          reset animation to start\n");
    fprintf(f, "  Escape        quit\n\n");
    fprintf(f, "Examples:\n");
    fprintf(f, "  milo-viewer world/shared/props/gen/discoball.milo_xbox\n");
    fprintf(f, "  milo-viewer scene.milo_xbox --screenshot out.png\n");
    fprintf(f, "  milo-viewer aubrey01.milo_xbox --clips clips.milo_xbox --bpm 120\n");
    fprintf(f, "  milo-viewer aubrey01.milo_xbox --clips clips.milo_xbox --video dance.mp4 --duration 10\n");
    fprintf(f, "  milo-viewer parent.milo_xbox --subdir child.milo_xbox\n");
}

ViewerConfig ViewerConfig::Parse(int argc, char** argv) {
    ViewerConfig cfg;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            PrintHelp(stdout);
            exit(0);
        } else if (strcmp(argv[i], "--screenshot") == 0 && i + 1 < argc) {
            cfg.screenshotPath = argv[++i];
        } else if (strcmp(argv[i], "--output") == 0 && i + 1 < argc) {
            cfg.screenshotPath = argv[++i];
        } else if (strcmp(argv[i], "--frames") == 0 && i + 1 < argc) {
            cfg.maxFrames = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--subdir") == 0 && i + 1 < argc) {
            SubdirEntry e;
            e.path = argv[++i];
            // Check for optional modifiers after --subdir <path>
            while (i + 1 < argc) {
                if (strcmp(argv[i + 1], "--subdir-offset") == 0 && i + 4 < argc) {
                    i++;
                    e.offsetX = (float)atof(argv[++i]);
                    e.offsetY = (float)atof(argv[++i]);
                    e.offsetZ = (float)atof(argv[++i]);
                } else if (strcmp(argv[i + 1], "--subdir-rotate") == 0 && i + 2 < argc) {
                    i++;
                    e.rotateDeg = (float)atof(argv[++i]);
                } else {
                    break;
                }
            }
            cfg.subdirs.push_back(e);
        } else if (strcmp(argv[i], "--char-setup") == 0 && i + 1 < argc) {
            cfg.charSetupPath = argv[++i];
        } else if (strcmp(argv[i], "--clips") == 0 && i + 1 < argc) {
            cfg.clipsPath = argv[++i];
        } else if (strcmp(argv[i], "--visemes") == 0 && i + 1 < argc) {
            cfg.visemesPath = argv[++i];
        } else if (strcmp(argv[i], "--clip") == 0 && i + 1 < argc) {
            cfg.clipName = argv[++i];
        } else if (strcmp(argv[i], "--bpm") == 0 && i + 1 < argc) {
            cfg.bpm = (float)atof(argv[++i]);
        } else if (strcmp(argv[i], "--video") == 0 && i + 1 < argc) {
            cfg.videoPath = argv[++i];
        } else if (strcmp(argv[i], "--duration") == 0 && i + 1 < argc) {
            cfg.videoDuration = (float)atof(argv[++i]);
        } else if (strcmp(argv[i], "--fps") == 0 && i + 1 < argc) {
            cfg.videoFps = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--camera") == 0 && i + 1 < argc) {
            cfg.cameraMode = argv[++i];
        } else if (strcmp(argv[i], "--azimuth") == 0 && i + 1 < argc) {
            cfg.camAzimuthDeg = (float)atof(argv[++i]);
        } else if (strcmp(argv[i], "--elevation") == 0 && i + 1 < argc) {
            cfg.camElevationDeg = (float)atof(argv[++i]);
        } else if (strcmp(argv[i], "--distance") == 0 && i + 1 < argc) {
            cfg.camDistanceOverride = (float)atof(argv[++i]);
        } else if (strcmp(argv[i], "--eye") == 0 && i + 3 < argc) {
            cfg.eyeX = (float)atof(argv[++i]);
            cfg.eyeY = (float)atof(argv[++i]);
            cfg.eyeZ = (float)atof(argv[++i]);
            cfg.hasEye = true;
        } else if (strcmp(argv[i], "--lookat") == 0 && i + 3 < argc) {
            cfg.lookX = (float)atof(argv[++i]);
            cfg.lookY = (float)atof(argv[++i]);
            cfg.lookZ = (float)atof(argv[++i]);
            cfg.hasLookat = true;
        } else if (strcmp(argv[i], "--test-bone") == 0 && i + 2 < argc) {
            cfg.testBoneName = argv[++i];
            cfg.testBoneAngle = (float)atof(argv[++i]);
            if (i + 1 < argc && (strcmp(argv[i+1], "x") == 0 || strcmp(argv[i+1], "y") == 0 || strcmp(argv[i+1], "z") == 0)) {
                cfg.testBoneAxis = argv[++i];
            }
        } else if (strcmp(argv[i], "--frame") == 0 && i + 1 < argc) {
            cfg.startFrame = (float)atof(argv[++i]);
        } else if (strcmp(argv[i], "--speed") == 0 && i + 1 < argc) {
            cfg.animSpeed = (float)atof(argv[++i]);
        } else if (strcmp(argv[i], "--paused") == 0) {
            cfg.startPaused = true;
        } else if (strcmp(argv[i], "--width") == 0 && i + 1 < argc) {
            setenv("MILO_WIDTH", argv[++i], 1);
        } else if (strcmp(argv[i], "--height") == 0 && i + 1 < argc) {
            setenv("MILO_HEIGHT", argv[++i], 1);
        } else if (strcmp(argv[i], "--verbose") == 0 || strcmp(argv[i], "-v") == 0) {
            cfg.verbose = true;
        } else if (strcmp(argv[i], "--show-all-lods") == 0) {
            cfg.showAllLods = true;
        } else if (strcmp(argv[i], "--no-fallback-material") == 0) {
            cfg.fallbackMaterial = false;
        } else if (strcmp(argv[i], "--export-textures") == 0 && i + 1 < argc) {
            cfg.exportTexturesDir = argv[++i];
        } else if (strcmp(argv[i], "--export-materials") == 0 && i + 1 < argc) {
            cfg.exportMaterialsDir = argv[++i];
        } else if (strcmp(argv[i], "--export-gltf") == 0 && i + 1 < argc) {
            cfg.exportGltfPath = argv[++i];
        } else if (strcmp(argv[i], "--pose-dump") == 0 && i + 1 < argc) {
            cfg.poseDumpPath = argv[++i];
        } else if (strcmp(argv[i], "--pose-dump-bones") == 0 && i + 1 < argc) {
            cfg.poseDumpBonesCsv = argv[++i];
        } else if (strcmp(argv[i], "--pose-dump-beat") == 0 && i + 1 < argc) {
            cfg.poseDumpBeatArg = argv[++i];
        } else if (strcmp(argv[i], "--hide") == 0 && i + 1 < argc) {
            cfg.hidePatterns.push_back(argv[++i]);
        } else if (strcmp(argv[i], "--light") == 0 && i + 7 < argc) {
            LightDef ld;
            const char* typeStr = argv[++i];
            if (strcmp(typeStr, "dir") == 0) ld.type = 1; // kDirectional
            else if (strcmp(typeStr, "point") == 0) ld.type = 0; // kPoint
            else { fprintf(stderr, "Warning: unknown light type '%s', using directional\n", typeStr); ld.type = 1; }
            ld.x = (float)atof(argv[++i]);
            ld.y = (float)atof(argv[++i]);
            ld.z = (float)atof(argv[++i]);
            ld.r = (float)atof(argv[++i]);
            ld.g = (float)atof(argv[++i]);
            ld.b = (float)atof(argv[++i]);
            ld.intensity = 1.0f;
            if (i + 1 < argc && argv[i+1][0] != '-') {
                ld.intensity = (float)atof(argv[++i]);
            }
            cfg.lights.push_back(ld);
        } else if (strcmp(argv[i], "--ambient") == 0 && i + 3 < argc) {
            cfg.ambientR = (float)atof(argv[++i]);
            cfg.ambientG = (float)atof(argv[++i]);
            cfg.ambientB = (float)atof(argv[++i]);
        } else if (strcmp(argv[i], "--movie") == 0 && i + 1 < argc) {
            cfg.movieFilePath = argv[++i];
        } else if (strcmp(argv[i], "--dump-bones") == 0) {
            cfg.dumpBones = true;
        } else if (strcmp(argv[i], "--direct-pose") == 0) {
            cfg.directPose = true;
        } else if (!cfg.miloPath) {
            cfg.miloPath = argv[i];
        }
    }

    return cfg;
}
