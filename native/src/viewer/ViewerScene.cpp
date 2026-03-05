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
#include "rndobj/Trans.h"
#include "rndobj/Rnd.h"
#include "rndobj/Draw.h"
#include "char/Character.h"
#include "char/FileMerger.h"
#include "math/Vec.h"
#include "utl/FilePath.h"
#include "platform/Rnd_Wgpu.h"

#include <cstdio>
#include <cstring>
#include <cmath>
#include <climits>

// From Mesh_Wgpu.cpp
extern wgpu::TextureView GetGpuTexView(RndTex* tex);
extern void SetMeshDepthBias(RndMesh*, int32_t);

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
    if (cfg.miloPath) {
        char outfitAbsPath[PATH_MAX];
        if (realpath(cfg.miloPath, outfitAbsPath)) {
            FilePath outfitFp(outfitAbsPath);
            fm->Select("outfit", outfitFp, false);
            printf("Milo Viewer: outfit selected: %s\n", outfitAbsPath);
        } else {
            fprintf(stderr, "Warning: cannot resolve outfit path '%s'\n", cfg.miloPath);
        }
    }

    // Select visemes
    if (cfg.visemesPath) {
        char visAbsPath[PATH_MAX];
        if (realpath(cfg.visemesPath, visAbsPath)) {
            FilePath visFp(visAbsPath);
            fm->Select("viseme", visFp, false);
            printf("Milo Viewer: viseme selected: %s\n", visAbsPath);
        } else {
            fprintf(stderr, "Warning: cannot resolve visemes path '%s'\n", cfg.visemesPath);
        }
    }

    // Synchronous merge
    fm->StartLoad(false);
    printf("Milo Viewer: FileMerger StartLoad complete\n");

    // SyncObjects wires CharFaceServo, CharEyes, CharLipSyncDriver
    if (rndScene) {
        rndScene->SyncObjects();
        printf("Milo Viewer: SyncObjects after FileMerger complete\n");
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

void ViewerScene::ResolveMeshVisibility(const ViewerConfig& cfg) {
    int hidCount = 0;
    ObjDirItr<RndMesh> meshIt(baseScene, true);
    while (meshIt) {
        const char* name = meshIt->Name();
        size_t len = strlen(name);

        if (strstr(name, "_lod") || strstr(name, "_wrinkle")) {
            meshIt->SetShowing(false);
            hidCount++;
            if (cfg.verbose) printf("  hide LOD/wrinkle mesh '%s'\n", name);
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

    bool charFraming = character && !cfg.subdirs.empty();
    ObjDirItr<RndMesh> bboxIt(baseScene, !charFraming);
    while (bboxIt) {
        RndMesh* m = bboxIt;
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
            }
        } else {
            if (px < minX) minX = px; if (px > maxX) maxX = px;
            if (py < minY) minY = py; if (py > maxY) maxY = py;
            if (pz < minZ) minZ = pz; if (pz > maxZ) maxZ = pz;
        }
        meshCount++;
        ++bboxIt;
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
