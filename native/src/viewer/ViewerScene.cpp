#include "viewer/ViewerScene.h"
#include "viewer/ViewerArgs.h"
#include "viewer/ViewerCamera.h"

#include "obj/Dir.h"
#include "obj/DirLoader.h"
#include "rndobj/Dir.h"
#include "rndobj/Env.h"
#include "rndobj/Lit.h"
#include "rndobj/Cam.h"
#include "rndobj/Mesh.h"
#include "rndobj/Mat.h"
#include "rndobj/Tex.h"
#include "rndobj/Trans.h"
#include "rndobj/Rnd.h"
#include "rndobj/Draw.h"
#include "movie/TexMovie.h"
#include "movie/Movie.h"
#ifdef HX_FFMPEG
#include "platform/FFmpegMovieImpl.h"
#endif
#include "char/Character.h"
#include "char/FileMerger.h"
#include "math/Vec.h"
#include "utl/FilePath.h"
#include "platform/Rnd_Wgpu.h"
#include "platform/MeshGpuCache.h"
#include "platform/TexGpu.h"

#include <cstdio>
#include <cstring>
#include <cmath>
#include <climits>
#include <algorithm>
#include <vector>

bool ViewerScene::Load(const char* miloAbsPath, const ViewerConfig& cfg) {
    printf("Milo Viewer: loading milo file...\n");

    {
        Symbol dirClass = DirLoader::GetDirClass(miloAbsPath);
        printf("Milo Viewer: .milo dir class = '%s'\n", dirClass.Str());
    }

    FilePath fp(miloAbsPath);
    baseDir.LoadFile(fp, false, false, kLoadFront, false);

    baseScene = baseDir;
    if (!baseScene) {
        fprintf(stderr, "Error: failed to load '%s'\n", miloAbsPath);
        fprintf(stderr, "  (The file might not be a valid .milo file.)\n");
        return false;
    }

    printf("Milo Viewer: loaded ObjectDir '%s' (class '%s')\n",
           baseScene->Name(), baseScene->ClassName().Str());

    rndScene = dynamic_cast<RndDir*>(baseScene);
    if (!rndScene) {
        fprintf(stderr, "Warning: loaded dir is '%s', not RndDir — drawing may not work\n",
                baseScene->ClassName().Str());
    }

    if (rndScene) {
        rndScene->SyncObjects();
        printf("Milo Viewer: SyncObjects complete\n");
    }

    // Load subdirectories
    for (const auto& entry : cfg.subdirs) {
        char sdAbsPath[PATH_MAX];
        if (!realpath(entry.path.c_str(), sdAbsPath)) {
            fprintf(stderr, "Warning: cannot resolve subdir path '%s', skipping\n", entry.path.c_str());
            continue;
        }
        printf("Milo Viewer: loading subdir '%s'...\n", sdAbsPath);

        ObjDirPtr<ObjectDir> sd;
        FilePath sdFp(sdAbsPath);
        sd.LoadFile(sdFp, false, false, kLoadFront, false);

        ObjectDir* sdDir = sd;
        if (!sdDir) {
            fprintf(stderr, "Warning: failed to load subdir '%s'\n", sdAbsPath);
            continue;
        }

        if (entry.offsetX != 0 || entry.offsetY != 0 || entry.offsetZ != 0 || entry.rotateDeg != 0) {
            int moved = 0;
            float rad = entry.rotateDeg * (3.14159265f / 180.0f);
            float cosR = cosf(rad), sinR = sinf(rad);
            ObjDirItr<RndTransformable> xfmIt(sdDir, true);
            while (xfmIt) {
                RndTransformable* t = xfmIt;
                Transform wxfm = t->WorldXfm();
                if (entry.rotateDeg != 0) {
                    float ox = wxfm.v.x, oy = wxfm.v.y;
                    wxfm.v.x = ox * cosR - oy * sinR;
                    wxfm.v.y = ox * sinR + oy * cosR;
                    float xx = wxfm.m.x.x, xy = wxfm.m.x.y;
                    wxfm.m.x.x = xx * cosR - xy * sinR;
                    wxfm.m.x.y = xx * sinR + xy * cosR;
                    float yx = wxfm.m.y.x, yy = wxfm.m.y.y;
                    wxfm.m.y.x = yx * cosR - yy * sinR;
                    wxfm.m.y.y = yx * sinR + yy * cosR;
                    float zx = wxfm.m.z.x, zy = wxfm.m.z.y;
                    wxfm.m.z.x = zx * cosR - zy * sinR;
                    wxfm.m.z.y = zx * sinR + zy * cosR;
                }
                wxfm.v.x += entry.offsetX;
                wxfm.v.y += entry.offsetY;
                wxfm.v.z += entry.offsetZ;
                t->SetWorldXfm(wxfm);
                moved++;
                ++xfmIt;
            }
            printf("Milo Viewer: transformed subdir (offset=%.1f,%.1f,%.1f rot=%.1f°) — %d objects\n",
                   entry.offsetX, entry.offsetY, entry.offsetZ, entry.rotateDeg, moved);
        }

        baseScene->AppendSubDir(sd);
        subdirs.push_back(sd);
        printf("Milo Viewer: loaded subdir '%s' (class '%s')\n",
               sdDir->Name(), sdDir->ClassName().Str());
    }
    if (!subdirs.empty()) {
        if (rndScene) rndScene->SyncObjects();
        printf("Milo Viewer: %d subdirectories loaded\n", (int)subdirs.size());
    }

    // Find character in base scene or subdirectories
    {
        ObjDirItr<Character> charIt(baseScene, true);
        if (charIt) {
            character = charIt;
            printf("Milo Viewer: found Character '%s'\n", character->Name());
        }
    }
    if (!character) {
        for (auto& sd : subdirs) {
            Character* sdChar = dynamic_cast<Character*>((ObjectDir*)sd);
            if (sdChar) {
                character = sdChar;
                printf("Milo Viewer: found Character '%s' (in subdir)\n", character->Name());
                break;
            }
            ObjDirItr<Character> charIt((ObjectDir*)sd, true);
            if (charIt) {
                character = charIt;
                printf("Milo Viewer: found Character '%s' (child of subdir)\n", character->Name());
                break;
            }
        }
    }

    return true;
}

bool ViewerScene::LoadFileMerger(const ViewerConfig& cfg) {
    if (!cfg.charSetupPath || !character) return false;

    FileMerger* fm = baseScene->Find<FileMerger>("char.fm", false);
    if (!fm) {
        fprintf(stderr, "Warning: --char-setup specified but 'char.fm' FileMerger not found\n");
        return false;
    }
    printf("Milo Viewer: FileMerger 'char.fm' found\n");

    // Select outfit (the original miloPath = the outfit .milo)
    // Accepts either absolute filesystem paths or ark-relative paths
    if (cfg.miloPath) {
        char resolved[PATH_MAX];
        const char* outfitPath = cfg.miloPath;
        if (realpath(cfg.miloPath, resolved))
            outfitPath = resolved;
        FilePath outfitFp(outfitPath);
        fm->Select("outfit", outfitFp, false);
        printf("Milo Viewer: outfit selected: %s\n", outfitPath);
    }

    // Select visemes
    if (cfg.visemesPath) {
        char resolved[PATH_MAX];
        const char* visPath = cfg.visemesPath;
        if (realpath(cfg.visemesPath, resolved))
            visPath = resolved;
        FilePath visFp(visPath);
        fm->Select("viseme", visFp, false);
        printf("Milo Viewer: viseme selected: %s\n", visPath);
    }

    // Print merger config before load
    for (int i = 0; i < 3; i++) {
        FileMerger::Merger* m = fm->FindMerger(Symbol(), false);
    }
    // Count before merge
    {
        int before = 0;
        ObjDirItr<Hmx::Object> bIt(baseScene, true);
        while (bIt) { before++; ++bIt; }
        printf("Milo Viewer: objects BEFORE merge: %d\n", before);
    }

    // Synchronous merge
    bool loaded = fm->StartLoad(false);
    printf("Milo Viewer: FileMerger StartLoad returned %d, pending=%d\n",
           loaded, fm->HasPendingFiles());

    // Count objects in baseScene after merge
    {
        int meshes = 0, total = 0, itrTotal = 0;
        ObjDirItr<Hmx::Object> allIt(baseScene, true);
        while (allIt) {
            itrTotal++;
            const char* cn = allIt->ClassName().Str();
            if (strcmp(cn, "Mesh") == 0 || strcmp(cn, "RndMesh") == 0)
                meshes++;
            ++allIt;
        }
        // Also count via hash table directly
        int hashCap = baseScene->HashTableSize();
        int hashUsed = baseScene->HashTableUsedSize();
        printf("Milo Viewer: after merge: itr=%d hashUsed=%d hashCap=%d meshes=%d\n",
               itrTotal, hashUsed, hashCap, meshes);
        // Check if any mesh via Find
        RndMesh* testMesh = baseScene->Find<RndMesh>("aubrey01_head.mesh", false);
        printf("  Find<RndMesh>('aubrey01_head.mesh') = %p\n", testMesh);
        Hmx::Object* testObj = baseScene->Find<Hmx::Object>("aubrey01_head.mesh", false);
        printf("  Find<Object>('aubrey01_head.mesh') = %p class='%s'\n",
               testObj, testObj ? testObj->ClassName().Str() : "?");

        // Check subdirs
        int sdCount = 0;
        for (auto& sdPtr : baseScene->SubDirs()) {
            ObjectDir* sd = sdPtr;
            int sdMeshes = 0;
            ObjDirItr<RndMesh> mIt(sd, true);
            while (mIt) { sdMeshes++; ++mIt; }
            printf("  subdir '%s' class='%s': %d meshes\n",
                   sd->Name(), sd->ClassName().Str(), sdMeshes);
            sdCount++;
        }
        printf("  %d subdirs total\n", sdCount);
    }

    // SyncObjects wires CharFaceServo, CharEyes, CharLipSyncDriver
    if (rndScene) {
        rndScene->SyncObjects();
    }

    fileMergerActive = true;
    return true;
}

void ViewerScene::ReleaseResources() {
    clipsDir = nullptr;
    visemeDir = nullptr;
    for (auto& sd : subdirs) sd = nullptr;
    baseDir = nullptr;
    if (gWgpuRnd) gWgpuRnd->Terminate();
}

RndEnviron* ViewerScene::FindEnvironment() const {
    if (rndScene && rndScene->GetEnv())
        return rndScene->GetEnv();
    for (auto& sd : subdirs) {
        RndDir* sdScene = dynamic_cast<RndDir*>((ObjectDir*)sd);
        if (sdScene && sdScene->GetEnv())
            return sdScene->GetEnv();
    }
    return nullptr;
}

void ViewerScene::PrintSummary(bool verbose) const {
    int meshCount = 0, matCount = 0, texCount = 0, other = 0;
    ObjDirItr<Hmx::Object> it(baseScene, true);
    while (it) {
        const char* cn = it->ClassName().Str();
        if (strcmp(cn, "Mesh") == 0 || strcmp(cn, "RndMesh") == 0) meshCount++;
        else if (strcmp(cn, "Mat") == 0 || strcmp(cn, "RndMat") == 0) matCount++;
        else if (strcmp(cn, "Tex") == 0 || strcmp(cn, "RndTex") == 0) texCount++;
        else other++;
        ++it;
    }
    printf("Milo Viewer: %d meshes, %d materials, %d textures, %d other objects\n",
           meshCount, matCount, texCount, other);

    if (verbose) {
        int drawableCount = 0;
        ObjDirItr<RndDrawable> drawIt(baseScene, true);
        while (drawIt) {
            if (drawableCount < 20) {
                printf("  drawable[%d]: '%s' class='%s' showing=%d\n",
                       drawableCount, drawIt->Name(), drawIt->ClassName().Str(), drawIt->Showing());
            }
            drawableCount++;
            ++drawIt;
        }
        printf("Milo Viewer: %d total drawables\n", drawableCount);

        ObjDirItr<RndMesh> meshIt2(baseScene, true);
        int skinnedCount = 0;
        while (meshIt2) {
            bool skinned = meshIt2->IsSkinned();
            if (skinned) skinnedCount++;
            printf("  mesh '%s': showing=%d verts=%d faces=%d compressed=%d bones=%d mat=%s pos=%.1f,%.1f,%.1f\n",
                   meshIt2->Name(), meshIt2->Showing(),
                   meshIt2->NumVerts(), meshIt2->NumFaces(),
                   meshIt2->NumCompressedVerts(), meshIt2->NumBones(),
                   meshIt2->Mat() ? meshIt2->Mat()->Name() : "(none)",
                   meshIt2->WorldXfm().v.x, meshIt2->WorldXfm().v.y, meshIt2->WorldXfm().v.z);
            ++meshIt2;
        }
        if (skinnedCount > 0) {
            printf("Milo Viewer: %d skinned meshes detected\n", skinnedCount);
        }
    }

    {
        ObjDirItr<RndCam> camItr(baseScene, true);
        if (camItr) {
            printf("Milo Viewer: found scene camera '%s', using orbit cam anyway\n",
                   camItr->Name());
        }
    }
}

// Which meshes are redundant lower-detail copies is decided by the asset, not by
// its file names. Character::mLods is that authority: "drawables not in any lod
// group will be drawn at every LOD" (Character.h), and a drawable in group i is
// drawn only when the engine selects LOD i. The viewer wants the highest-detail
// view, so it hides exactly the drawables that appear in group 1..N-1 and in no
// earlier group.
//
// The name test this replaces (`strstr(name, "_lod")`) was wrong in both
// directions, and DC3's own content proves both:
//   * RB3 char/crowd/gen/crowd_female01 ships ONE body mesh,
//     female_crowd_body01_lod02.mesh, and it is in LOD group 0 -- the blanket
//     test hid the whole character and left four hand/prop meshes.
//   * DC3 char/main/dancer/gen/emilia01 names its LOD-1 meshes emilia01_lod1*
//     while the full-detail ones are emilia01_outfit* -- no name-derived sibling
//     lookup can pair those, but mLods pairs them exactly.
// Only assets with no Character (or with no LOD groups) fall back to the name
// test, where there is no authority to consult.
// The chosen LOD is the most detailed group that actually HAS geometry, not
// blindly group 0. RB3's crowd characters ship an empty group 0 and put their
// only body mesh in group 2 -- they are authored as the distant-crowd LOD and
// the engine would only ever select that group for them. Picking 0 unconditionally
// renders nothing but the hand props, which is the defect this replaced.
int ViewerScene::ChooseLod() const {
    if (!character) return -1;
    int n = character->NumLods();
    for (int l = 0; l < n; l++) {
        const Character::Lod& lod = character->GetLod(l);
        if (lod.mOpaque.size() > 0 || lod.mTranslucent.size() > 0) return l;
    }
    return -1;
}

void ViewerScene::CollectRedundantLodMeshes(std::set<const RndDrawable*>& out,
                                            bool verbose) const {
    int chosen = ChooseLod();
    if (chosen < 0) return;
    int n = character->NumLods();

    if (verbose) {
        for (int l = 0; l < n; l++) {
            const Character::Lod& lod = character->GetLod(l);
            printf("  LOD group %d%s: %d opaque, %d translucent\n", l,
                   l == chosen ? " (CHOSEN)" : "",
                   (int)lod.mOpaque.size(), (int)lod.mTranslucent.size());
        }
    }

    std::set<const RndDrawable*> kept;  // everything the chosen LOD draws
    const Character::Lod& best = character->GetLod(chosen);
    for (int i = 0; i < best.mOpaque.size(); i++) kept.insert(best.mOpaque[i]);
    for (int i = 0; i < best.mTranslucent.size(); i++) kept.insert(best.mTranslucent[i]);

    for (int l = 0; l < n; l++) {
        if (l == chosen) continue;
        const Character::Lod& lod = character->GetLod(l);
        for (int i = 0; i < lod.mOpaque.size(); i++)
            if (!kept.count(lod.mOpaque[i])) out.insert(lod.mOpaque[i]);
        for (int i = 0; i < lod.mTranslucent.size(); i++)
            if (!kept.count(lod.mTranslucent[i])) out.insert(lod.mTranslucent[i]);
    }
}

void ViewerScene::ResolveMeshVisibility(const ViewerConfig& cfg) {
    int hidCount = 0;
    int keptLod = 0;

    std::set<const RndDrawable*> lowerLods;
    if (!cfg.showAllLods) CollectRedundantLodMeshes(lowerLods, cfg.verbose);
    // Only trust the name test when the asset offers no LOD groups to read.
    bool haveLodAuthority = ChooseLod() >= 0;

    ObjDirItr<RndMesh> meshIt(baseScene, true);
    while (meshIt) {
        const char* name = meshIt->Name();
        size_t len = strlen(name);

        bool lodName = strstr(name, "_lod") != nullptr;
        // No LOD groups => nothing is demoted. Character.h is explicit that
        // "drawables not in any lod group will be drawn at every LOD", so an
        // asset that defines no groups is asking for all of its geometry. RB3's
        // crowd_female01 is exactly that: zero LOD groups, one lod-named body
        // mesh with Showing() set. Hiding it on the strength of its name alone
        // was overriding the asset with a guess.
        bool redundantLod = haveLodAuthority && !cfg.showAllLods
            && lowerLods.count(&(*meshIt)) != 0;

        if (redundantLod || strstr(name, "_wrinkle")) {
            meshIt->SetShowing(false);
            hidCount++;
            if (cfg.verbose) printf("  hide LOD/wrinkle mesh '%s'\n", name);
        }
        else if (lodName) {
            // Named like a LOD but the asset says it is drawn at this LOD.
            keptLod++;
            if (cfg.verbose)
                printf("  keep LOD mesh '%s' (%s)\n", name,
                       haveLodAuthority ? "in the Character's chosen LOD group"
                                        : "the asset defines no LOD groups");
        }
        else if (len > 5 && strcmp(name + len - 5, ".mesh") == 0) {
            bool isSplit = false;
            for (size_t i = 0; i < len - 5; i++) {
                if (name[i] == '.' && name[i+1] >= '1' && name[i+1] <= '9'
                    && (i + 2 >= len - 5 || name[i+2] == '.')) {
                    isSplit = true;
                    break;
                }
            }
            if (!isSplit) {
                char splitName[256];
                snprintf(splitName, sizeof(splitName), "%.*s.1.mesh", (int)(len - 5), name);
                RndMesh* split = baseScene->Find<RndMesh>(splitName, false);
                if (split) {
                    SetMeshDepthBias(&(*meshIt), 100);
                    if (cfg.verbose)
                        printf("  depth-bias combined mesh '%s'\n", name);
                }
            }
        }
        ++meshIt;
    }
    if (hidCount > 0) {
        printf("Milo Viewer: hid %d meshes (LOD/wrinkle/combined)\n", hidCount);
    }
    if (keptLod > 0) {
        printf("Milo Viewer: kept %d lod-named mesh(es) the asset does NOT demote — "
               "they are the geometry, not a redundant copy\n", keptLod);
    }
}

void ViewerScene::ApplyFallbackMaterial(const ViewerConfig& cfg) {
    // Some milos are geometry *libraries*: they ship meshes and no RndMat at all,
    // because the venue that instantiates them supplies the materials. RB3's
    // ui/track/gen/tracksystem_meshes is the canonical case — 130 meshes, zero
    // Mat objects. The engine's RndMesh::DrawShowing hard-skips a mesh whose
    // Mat() is null (Mesh_Wgpu.cpp, "no material"), and that skip is correct:
    // without a material there is nothing to bind. The result is a blank frame.
    //
    // That is an asset property, not a renderer bug — but a *viewer* exists to
    // show what is in the file, so a neutral prelit grey is attached and the
    // substitution is announced. The shape is the asset's; the grey is ours.
    // --no-fallback-material turns this off. Skipped entirely for export runs so
    // exported glTF/materials only ever contain what the file actually ships.
    if (!cfg.fallbackMaterial || cfg.IsExportOnly() || !baseScene) return;

    int matless = 0, total = 0;
    ObjDirItr<RndMesh> countIt(baseScene, true);
    while (countIt) {
        total++;
        if (!countIt->Mat()) matless++;
        ++countIt;
    }
    if (matless == 0) return;

    RndMat* fallback = Hmx::Object::New<RndMat>();
    fallback->SetName("viewer_fallback_mat", baseScene);
    fallback->SetPreLit(true);       // no RndEnviron dependency
    fallback->SetUseEnv(false);
    fallback->SetColor(0.68f, 0.68f, 0.70f);
    fallback->SetAlpha(1.0f);
    fallback->SetZMode(kZModeNormal);
    fallback->SetBlend(BaseMaterial::kBlendSrc);  // kBlendDest would draw nothing
    fallback->SetAlphaCut(false);
    fallbackMat = fallback;

    ObjDirItr<RndMesh> assignIt(baseScene, true);
    while (assignIt) {
        if (!assignIt->Mat()) {
            assignIt->SetMat(fallback);
            if (cfg.verbose) printf("  fallback material -> '%s'\n", assignIt->Name());
        }
        ++assignIt;
    }

    printf("Milo Viewer: %d of %d meshes ship NO material and were given a neutral "
           "prelit grey — their colour below is the VIEWER's, not the asset's "
           "(--no-fallback-material to disable)\n", matless, total);
}

void ViewerScene::SetupSyntheticLights(const ViewerConfig& cfg) {
    if (cfg.lights.empty() && cfg.ambientR < 0) return;

    RndEnviron* env = FindEnvironment();
    if (!env && rndScene) {
        env = Hmx::Object::New<RndEnviron>();
        env->SetName("synth_env", rndScene);
        rndScene->SetEnv(env);
        printf("Milo Viewer: created synthetic RndEnviron\n");
    }
    if (!env) {
        fprintf(stderr, "Warning: --light/--ambient specified but no RndEnviron found\n");
        return;
    }

    if (cfg.ambientR >= 0) {
        Hmx::Color amb;
        amb.Set(cfg.ambientR, cfg.ambientG, cfg.ambientB);
        env->SetAmbientColor(amb);
        printf("Milo Viewer: ambient color set to (%.2f, %.2f, %.2f)\n",
               cfg.ambientR, cfg.ambientG, cfg.ambientB);
    }

    for (size_t li = 0; li < cfg.lights.size(); li++) {
        const auto& ld = cfg.lights[li];
        RndLight* light = Hmx::Object::New<RndLight>();
        char lname[64];
        snprintf(lname, sizeof(lname), "synth_light_%zu", li);
        ObjectDir* owner = rndScene ? (ObjectDir*)rndScene : baseScene;
        light->SetName(lname, owner);
        light->SetLightType((RndLight::Type)ld.type);
        Hmx::Color col;
        col.Set(ld.r * ld.intensity, ld.g * ld.intensity, ld.b * ld.intensity);
        light->SetColor(col);
        light->SetShowing(true);
        if (ld.type == RndLight::kDirectional) {
            Transform xfm;
            xfm.Reset();
            Vector3 dir(ld.x, ld.y, ld.z);
            Normalize(dir, dir);
            xfm.m.z = dir;
            Vector3 up(0, 1, 0);
            if (fabsf(dir.y) > 0.9f) up.Set(1, 0, 0);
            Cross(dir, up, xfm.m.x);
            Normalize(xfm.m.x, xfm.m.x);
            Cross(xfm.m.x, dir, xfm.m.y);
            light->SetLocalXfm(xfm);
            printf("Milo Viewer: added directional light dir=(%.1f,%.1f,%.1f) col=(%.2f,%.2f,%.2f)\n",
                   ld.x, ld.y, ld.z, col.red, col.green, col.blue);
        } else {
            Transform xfm;
            xfm.Reset();
            xfm.v.Set(ld.x, ld.y, ld.z);
            light->SetLocalXfm(xfm);
            light->SetRange(500.0f);
            printf("Milo Viewer: added point light pos=(%.1f,%.1f,%.1f) col=(%.2f,%.2f,%.2f)\n",
                   ld.x, ld.y, ld.z, col.red, col.green, col.blue);
        }
        env->AddLight(light);
        syntheticLights.push_back(light);
    }
}

void ViewerScene::AutoFrameCamera(OrbitCamera& cam, RndCam* rndCam, const ViewerConfig& cfg) const {
    // Compute bounding box from mesh positions
    float minX = 1e10f, minY = 1e10f, minZ = 1e10f;
    float maxX = -1e10f, maxY = -1e10f, maxZ = -1e10f;
    int meshCount = 0;

    // Every world-space coordinate is also kept per axis so a robust (percentile)
    // bound can be computed as a fallback. A single garbage vertex otherwise
    // destroys the framing: RB3's ui/track/gen/tracksystem_meshes decodes one
    // vertex at Y=121458 (both this decoder and rb3-xenon's independent one
    // produce that same number, so the outlier is in the asset / a shared
    // vertex-format branch, not in the framing code), which parked the camera
    // 243180 units out and made the frame blank.
    std::vector<float> xs, ys, zs;

    bool charFraming = character && !cfg.subdirs.empty();
    ObjDirItr<RndMesh> bboxIt(baseScene, !charFraming);
    while (bboxIt) {
        RndMesh* m = bboxIt;
        if (!m) { ++bboxIt; continue; }
        if (!m->Showing()) { ++bboxIt; continue; }
        const Transform& xfm = m->WorldXfm();
        float px = xfm.v.x, py = xfm.v.y, pz = xfm.v.z;

        RndMesh* owner = m->GetGeomOwner();
        if (!owner) owner = m;

        int nv = owner->NumVerts();
        int ncv = owner->NumCompressedVerts();

        if (nv > 0) {
            for (int i = 0; i < nv; i++) {
                const RndMesh::Vert& v = owner->Verts(i);
                float wx = xfm.m.x.x * v.pos.x + xfm.m.y.x * v.pos.y + xfm.m.z.x * v.pos.z + xfm.v.x;
                float wy = xfm.m.x.y * v.pos.x + xfm.m.y.y * v.pos.y + xfm.m.z.y * v.pos.z + xfm.v.y;
                float wz = xfm.m.x.z * v.pos.x + xfm.m.y.z * v.pos.y + xfm.m.z.z * v.pos.z + xfm.v.z;
                if (wx < minX) minX = wx; if (wx > maxX) maxX = wx;
                if (wy < minY) minY = wy; if (wy > maxY) maxY = wy;
                if (wz < minZ) minZ = wz; if (wz > maxZ) maxZ = wz;
                xs.push_back(wx); ys.push_back(wy); zs.push_back(wz);
            }
        } else if (ncv > 0 && owner->CompressedVerts()) {
            const unsigned char* data = owner->CompressedVerts();
            struct CVert { int px, py, pz, n, c, t1, t2, b1, b2; };
            const CVert* cverts = (const CVert*)data;
            for (int i = 0; i < ncv; i++) {
                unsigned int bx = __builtin_bswap32((unsigned int)cverts[i].px);
                unsigned int by = __builtin_bswap32((unsigned int)cverts[i].py);
                unsigned int bz = __builtin_bswap32((unsigned int)cverts[i].pz);
                float fx, fy, fz;
                memcpy(&fx, &bx, 4);
                memcpy(&fy, &by, 4);
                memcpy(&fz, &bz, 4);
                float wx = xfm.m.x.x * fx + xfm.m.y.x * fy + xfm.m.z.x * fz + xfm.v.x;
                float wy = xfm.m.x.y * fx + xfm.m.y.y * fy + xfm.m.z.y * fz + xfm.v.y;
                float wz = xfm.m.x.z * fx + xfm.m.y.z * fy + xfm.m.z.z * fz + xfm.v.z;
                if (wx < minX) minX = wx; if (wx > maxX) maxX = wx;
                if (wy < minY) minY = wy; if (wy > maxY) maxY = wy;
                if (wz < minZ) minZ = wz; if (wz > maxZ) maxZ = wz;
                xs.push_back(wx); ys.push_back(wy); zs.push_back(wz);
            }
        } else {
            if (px < minX) minX = px; if (px > maxX) maxX = px;
            if (py < minY) minY = py; if (py > maxY) maxY = py;
            if (pz < minZ) minZ = pz; if (pz > maxZ) maxZ = pz;
            xs.push_back(px); ys.push_back(py); zs.push_back(pz);
        }
        meshCount++;
        ++bboxIt;
    }

    // Outlier guard. The percentile bound is used ONLY on an axis whose raw span
    // is more than kOutlierRatio times the robust span — i.e. only when a handful
    // of vertices are demonstrably lying. Well-formed assets keep their exact
    // historical framing, so this cannot silently crop anything.
    if (xs.size() >= 200) {
        const float kOutlierRatio = 4.0f;
        auto robustAxis = [&](std::vector<float>& v, float& lo, float& hi) {
            std::sort(v.begin(), v.end());
            size_t n = v.size();
            size_t k = n / 200;  // 0.5%
            if (k == 0) k = 1;
            float rlo = v[k], rhi = v[n - 1 - k];
            float rawSpan = hi - lo, robSpan = rhi - rlo;
            if (robSpan > 0.0f && rawSpan > robSpan * kOutlierRatio) {
                printf("Milo Viewer: WARNING outlier vertices — raw span %.2f vs robust "
                       "span %.2f; framing on the robust bound [%.2f, %.2f] (raw was "
                       "[%.2f, %.2f])\n", rawSpan, robSpan, rlo, rhi, lo, hi);
                lo = rlo; hi = rhi;
            }
        };
        robustAxis(xs, minX, maxX);
        robustAxis(ys, minY, maxY);
        robustAxis(zs, minZ, maxZ);
    }

    if (meshCount > 0 && maxX > minX - 1e6f) {
        float cx = (minX + maxX) * 0.5f;
        float cy = (minY + maxY) * 0.5f;
        float cz = (minZ + maxZ) * 0.5f;
        float sx = maxX - minX;
        float sy = maxY - minY;
        float sz = maxZ - minZ;
        float extent = sqrtf(sx * sx + sy * sy + sz * sz) * 0.5f;
        if (extent < 0.01f) extent = 1.0f;

        cam.targetX = cx;
        cam.targetY = cy;
        cam.targetZ = cz;
        float maxAxis = sx;
        if (sy > maxAxis) maxAxis = sy;
        if (sz > maxAxis) maxAxis = sz;
        cam.distance = maxAxis * 2.0f;
        if (cam.distance < extent * 1.5f) cam.distance = extent * 1.5f;
        if (cam.distance < 3.0f) cam.distance = 3.0f;
        cam.elevation = 0.3f;
        cam.azimuth = 0.4f;

        printf("Milo Viewer: auto-frame bbox (%.2f,%.2f,%.2f)-(%.2f,%.2f,%.2f) center=(%.2f,%.2f,%.2f) dist=%.2f\n",
               minX, minY, minZ, maxX, maxY, maxZ, cx, cy, cz, cam.distance);
    }

    // Re-center on character pelvis if present
    if (character) {
        RndTransformable* pelvis = character->Find<RndTransformable>("bone_pelvis.mesh", false);
        if (pelvis) {
            const Transform& bxfm = pelvis->WorldXfm();
            cam.targetX = bxfm.v.x;
            cam.targetY = bxfm.v.y;
            cam.targetZ = bxfm.v.z;
            cam.distance = 120.0f;
            cam.elevation = 0.12f;
            printf("Milo Viewer: re-centered on character pelvis (%.2f, %.2f, %.2f)\n",
                   bxfm.v.x, bxfm.v.y, bxfm.v.z);
        }
    }

    // Update frustum far plane
    {
        float farDist = cam.distance * 5.0f;
        if (farDist < 1000.0f) farDist = 1000.0f;
        float nearDist = farDist * 0.001f;
        if (nearDist < 0.1f) nearDist = 0.1f;
        rndCam->SetFrustum(nearDist, farDist, 0.6024f, 1.0f);
    }

    // Apply camera overrides from CLI args
    if (cfg.camAzimuthDeg > -900.0f) {
        cam.azimuth = cfg.camAzimuthDeg * (3.14159265f / 180.0f);
    }
    if (cfg.camElevationDeg > -900.0f) {
        cam.elevation = cfg.camElevationDeg * (3.14159265f / 180.0f);
    }
    if (cfg.camDistanceOverride > 0.0f) {
        cam.distance = cfg.camDistanceOverride;
    }

    // Direct camera placement (--eye / --lookat)
    if (cfg.hasEye) {
        if (cfg.hasLookat) {
            cam.targetX = cfg.lookX;
            cam.targetY = cfg.lookY;
            cam.targetZ = cfg.lookZ;
        }
        float dx = cfg.eyeX - cam.targetX;
        float dy = cfg.eyeY - cam.targetY;
        float dz = cfg.eyeZ - cam.targetZ;
        cam.distance = sqrtf(dx*dx + dy*dy + dz*dz);
        if (cam.distance < 0.01f) cam.distance = 1.0f;
        cam.azimuth = atan2f(dx, dy);
        cam.elevation = asinf(dz / cam.distance);
        printf("Milo Viewer: eye=(%.1f,%.1f,%.1f) lookat=(%.1f,%.1f,%.1f) dist=%.1f az=%.1f° el=%.1f°\n",
               cfg.eyeX, cfg.eyeY, cfg.eyeZ, cam.targetX, cam.targetY, cam.targetZ,
               cam.distance, cam.azimuth * 57.2958f, cam.elevation * 57.2958f);
    }
}

bool ViewerScene::ShouldHideMesh(const RndMesh* mesh, const ViewerConfig& cfg) {
    for (auto& pat : cfg.hidePatterns) {
        if (strstr(mesh->Name(), pat.c_str())) return true;
    }
    return false;
}

bool ViewerScene::HasUnresolvedTexture(const RndMesh* mesh) {
    RndMat* mat = const_cast<RndMesh*>(mesh)->Mat();
    if (mat) {
        RndTex* diffTex = mat->GetDiffuseTex();
        if (diffTex && !GetGpuTexView(diffTex)) {
            return true;
        }
    }
    return false;
}

// ============================================================================
// Movie support
// ============================================================================

void ViewerScene::EnterMovies(const ViewerConfig& cfg) {
    // Scan scene for existing TexMovie objects
    if (baseScene) {
        ObjDirItr<TexMovie> it(baseScene, true);
        while (it) {
            movies.push_back(it);
            printf("Milo Viewer: found TexMovie '%s' (showing=%d, empty=%d)\n",
                   it->Name(), it->Showing(), it->IsEmpty());
            ++it;
        }
    }

    // Override movie file paths if --movie was specified
    if (cfg.movieFilePath) {
        FilePath movieFp(cfg.movieFilePath);

        if (!movies.empty()) {
            // Override existing TexMovies' file paths
            for (TexMovie* tm : movies) {
                tm->SetFile(movieFp);
                tm->SetShowing(true);
                printf("Milo Viewer: overriding TexMovie '%s' with '%s'\n",
                       tm->Name(), cfg.movieFilePath);
            }
        } else {
            // No TexMovie in scene — create a synthetic one for testing
            printf("Milo Viewer: creating synthetic TexMovie for '%s'\n", cfg.movieFilePath);

            ObjectDir* owner = baseScene ? baseScene : ObjectDir::Main();

            // Create render target texture (512x512 RGBA)
            syntheticMovieTex = Hmx::Object::New<RndTex>();
            syntheticMovieTex->SetName("movie_test_tex", owner);
            syntheticMovieTex->SetBitmap(512, 512, 32, RndTex::kRendered, false, "");

            // Create TexMovie
            syntheticMovie = Hmx::Object::New<TexMovie>();
            syntheticMovie->SetName("movie_test", owner);

            // Wire output texture via SetProperty (string-based property set)
            syntheticMovie->SetProperty(Symbol("output_texture"), DataNode(syntheticMovieTex));
            syntheticMovie->SetFile(movieFp);
            syntheticMovie->SetShowing(true);
            movies.push_back(syntheticMovie);

            printf("Milo Viewer: synthetic TexMovie created (tex=%dx%d)\n",
                   syntheticMovieTex->Width(), syntheticMovieTex->Height());
        }
    }

    // Enter all movies
    for (TexMovie* tm : movies) {
        tm->Enter();
        printf("Milo Viewer: TexMovie '%s' entered (open=%d)\n",
               tm->Name(), tm->IsOpen());
    }

    if (!movies.empty()) {
        printf("Milo Viewer: %d TexMovie(s) active\n", (int)movies.size());
    }
}

void ViewerScene::PollMovies(float seconds) {
    for (TexMovie* tm : movies) {
#ifdef HX_FFMPEG
        if (seconds >= 0.0f) {
            // Virtual time mode: override wall-clock for headless/capture
            Movie& mov = tm->GetMovie();
            FFmpegMovieImpl* impl = dynamic_cast<FFmpegMovieImpl*>(mov.GetImpl());
            if (impl)
                impl->SetVirtualTime(seconds * 1000.0f);
        }
#endif
        tm->Poll();
    }
}

void ViewerScene::DrawMovieOverlay() {
    // Draw synthetic movie as a fullscreen quad if present
    if (!syntheticMovie || !syntheticMovieTex) return;

    // Trigger frame decode + texture upload
    syntheticMovie->DrawToTexture();

    // Only draw overlay if texture has GPU data
    if (!GetGpuTexView(syntheticMovieTex))
        return;

    // Create a material with the movie texture (lazy, reuse after first call)
    static RndMat* sMovieMat = nullptr;
    if (!sMovieMat) {
        sMovieMat = Hmx::Object::New<RndMat>();
        sMovieMat->SetName("movie_overlay_mat", baseScene ? baseScene : ObjectDir::Main());
        sMovieMat->SetDiffuseTex(syntheticMovieTex);
        sMovieMat->SetZMode(kZModeDisable);
        sMovieMat->SetUseEnv(false);
        sMovieMat->SetPreLit(true);
        sMovieMat->SetBlend(BaseMaterial::kBlendSrcAlpha);
        sMovieMat->SetAlphaCut(false);
    }

    // Draw fullscreen textured quad (DrawRect uses Rnd pixel coordinates)
    float rw = (float)TheRnd.Width();
    float rh = (float)TheRnd.Height();
    Hmx::Rect rect(0.0f, 0.0f, rw, rh);
    Hmx::Color white(1.0f, 1.0f, 1.0f, 1.0f);
    TheRnd.DrawRect(rect, white, sMovieMat, nullptr, nullptr);
}

void ViewerScene::DrawAllMeshes(const ViewerConfig& cfg) const {
    RndEnviron* env = FindEnvironment();
    Vector3 origin(0,0,0);
    RndEnvironTracker tracker(env, &origin);

    ObjDirItr<RndMesh> meshIt(baseScene, true);
    while (meshIt) {
        if (!ShouldHideMesh(meshIt, cfg)) {
            meshIt->DrawShowing();
        }
        ++meshIt;
    }

    for (auto& sd : subdirs) {
        ObjDirItr<RndMesh> sdMeshIt((ObjectDir*)sd, true);
        while (sdMeshIt) {
            if (!ShouldHideMesh(sdMeshIt, cfg) && !HasUnresolvedTexture(sdMeshIt)) {
                sdMeshIt->DrawShowing();
            }
            ++sdMeshIt;
        }
    }
}
