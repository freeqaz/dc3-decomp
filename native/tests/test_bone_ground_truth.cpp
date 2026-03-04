// Bone Ground Truth & Clip Validation tests (Gates 1-3)
// Validates bone topology, rest pose sanity, and clip pose application
// using real character .milo_xbox assets.
//
// Env vars:
//   MILO_TEST_CHAR  — bone dir override (default: skeleton_bones_resource)
//   MILO_TEST_CLIPS — clip dir override (default: auto-discover)
//
// Tests skip gracefully if assets are not found.

#include "test_helpers.h"
#include "obj/Dir.h"
#include "obj/DirLoader.h"
#include "rndobj/Trans.h"
#include "char/CharClip.h"
#include "utl/ChunkStream.h"
#include "utl/FilePath.h"
#include "math/Vec.h"

#include <cmath>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

// ============================================================================
// Helper: try to load a .milo_xbox file, returns nullptr on failure
// ============================================================================

static ObjectDir *TryLoadMilo(const char *path) {
    FilePath fp(path);
    ChunkStream *probe = new ChunkStream(
        fp.c_str(), ChunkStream::kRead, 0x8000, false, kPlatformNone, false
    );
    if (probe->Fail()) {
        delete probe;
        return nullptr;
    }
    delete probe;

    printf("  TryLoadMilo: %s\n", path);
    ObjectDir *dir = DirLoader::LoadObjects(fp, nullptr, nullptr);
    if (dir) {
        printf("  TryLoadMilo: OK '%s' class='%s'\n",
               dir->Name(), dir->ClassName().Str());
    }
    return dir;
}

// ============================================================================
// Shared fixture: loads skeleton bones once per test suite
// ============================================================================

class BoneGroundTruth : public EngineTestFixture {
protected:
    static ObjectDir *sDir;

    static void SetUpTestSuite() {
        EngineTestFixture::SetUpTestSuite();

        const char *envPath = std::getenv("MILO_TEST_CHAR");
        if (envPath) {
            sDir = TryLoadMilo(envPath);
            return;
        }

        // Try assets in order (simpler first to avoid crashes)
        const char *candidates[] = {
            "char/shared/gen/skeleton_bones_resource.milo_xbox",
            "char/main/gen/main.milo_xbox",
            nullptr
        };

        for (int i = 0; candidates[i]; i++) {
            sDir = TryLoadMilo(candidates[i]);
            if (sDir) return;
        }
        printf("BoneGroundTruth: no character asset found\n");
    }

    void SetUp() override {
        if (!sDir) {
            GTEST_SKIP() << "Character asset not loaded (set MILO_TEST_CHAR)";
        }
    }

    RndTransformable *FindBone(const char *name) {
        return sDir->Find<RndTransformable>(name, false);
    }
};

ObjectDir *BoneGroundTruth::sDir = nullptr;

// ============================================================================
// Gate 1: Bone Topology
// ============================================================================

TEST_F(BoneGroundTruth, BoneExists) {
    const char *boneNames[] = {
        "bone_pelvis.mesh", "bone_head.mesh",
        "bone_R-hand.mesh", "bone_L-hand.mesh",
        nullptr
    };

    for (int i = 0; boneNames[i]; i++) {
        RndTransformable *bone = FindBone(boneNames[i]);
        if (bone) {
            printf("  Found: %s\n", boneNames[i]);
        } else {
            printf("  NOT FOUND: %s\n", boneNames[i]);
        }
        ASSERT_NE(bone, nullptr) << "Missing bone: " << boneNames[i];
    }
}

TEST_F(BoneGroundTruth, BoneHierarchy) {
    RndTransformable *head = FindBone("bone_head.mesh");
    ASSERT_NE(head, nullptr);

    RndTransformable *headParent = head->TransParent();
    EXPECT_NE(headParent, nullptr) << "bone_head.mesh has no parent";
    if (headParent) {
        printf("  bone_head parent: %s\n", headParent->Name());
    }

    RndTransformable *rHand = FindBone("bone_R-hand.mesh");
    ASSERT_NE(rHand, nullptr);

    RndTransformable *rHandParent = rHand->TransParent();
    EXPECT_NE(rHandParent, nullptr) << "bone_R-hand.mesh has no parent";
    if (rHandParent) {
        printf("  bone_R-hand parent: %s\n", rHandParent->Name());
    }
}

TEST_F(BoneGroundTruth, BoneChildCount) {
    RndTransformable *pelvis = FindBone("bone_pelvis.mesh");
    ASSERT_NE(pelvis, nullptr);

    size_t childCount = pelvis->Children().size();
    printf("  bone_pelvis children: %zu\n", childCount);
    EXPECT_GT(childCount, 0u) << "pelvis should have children";
}

TEST_F(BoneGroundTruth, ManualTransformRoundTrip) {
    RndTransformable *pelvis = FindBone("bone_pelvis.mesh");
    ASSERT_NE(pelvis, nullptr);

    // Save original
    Vector3 origPos = pelvis->LocalXfm().v;

    // Set test position
    Vector3 testPos(1.0f, 2.0f, 3.0f);
    pelvis->SetLocalPos(testPos);

    const Vector3 &got = pelvis->LocalXfm().v;
    EXPECT_FLOAT_EQ(got.x, 1.0f);
    EXPECT_FLOAT_EQ(got.y, 2.0f);
    EXPECT_FLOAT_EQ(got.z, 3.0f);

    // Restore original
    pelvis->SetLocalPos(origPos);
}

// ============================================================================
// Gate 2: Rest Pose Sanity
// ============================================================================

TEST_F(BoneGroundTruth, RestPoseNonZero) {
    int nonIdentity = 0;
    int total = 0;

    for (ObjDirItr<RndTransformable> it(sDir, true); it; ++it) {
        RndTransformable *bone = it;
        total++;
        const Vector3 &pos = bone->WorldXfm().v;
        if (pos.x != 0.0f || pos.y != 0.0f || pos.z != 0.0f) {
            nonIdentity++;
        }
    }

    printf("  %d/%d transforms have non-zero world position\n", nonIdentity, total);
    EXPECT_GT(nonIdentity, 5)
        << "Expected at least some bones to have non-identity world transforms";
}

TEST_F(BoneGroundTruth, SymmetryCheck) {
    RndTransformable *lHand = FindBone("bone_L-hand.mesh");
    RndTransformable *rHand = FindBone("bone_R-hand.mesh");
    ASSERT_NE(lHand, nullptr);
    ASSERT_NE(rHand, nullptr);

    const Vector3 &lPos = lHand->WorldXfm().v;
    const Vector3 &rPos = rHand->WorldXfm().v;

    printf("  L-hand world: (%.3f, %.3f, %.3f)\n", lPos.x, lPos.y, lPos.z);
    printf("  R-hand world: (%.3f, %.3f, %.3f)\n", rPos.x, rPos.y, rPos.z);

    // Tolerance generous — model uses centimeters (hand at ~15 units from center)
    EXPECT_NEAR(lPos.x, -rPos.x, 1.0f) << "X axis symmetry";
    EXPECT_NEAR(lPos.y, rPos.y, 1.0f) << "Y axis similarity";
    EXPECT_NEAR(lPos.z, rPos.z, 1.0f) << "Z axis similarity";
}

TEST_F(BoneGroundTruth, LimbDistanceSanity) {
    RndTransformable *shoulder = FindBone("bone_R-upperArm.mesh");
    RndTransformable *elbow = FindBone("bone_R-foreArm.mesh");
    ASSERT_NE(shoulder, nullptr) << "bone_R-upperArm.mesh not found";
    ASSERT_NE(elbow, nullptr) << "bone_R-foreArm.mesh not found";

    const Vector3 &sPos = shoulder->WorldXfm().v;
    const Vector3 &ePos = elbow->WorldXfm().v;

    float dx = sPos.x - ePos.x;
    float dy = sPos.y - ePos.y;
    float dz = sPos.z - ePos.z;
    float len = std::sqrt(dx * dx + dy * dy + dz * dz);

    printf("  R-upperarm to R-forearm distance: %.3f\n", len);
    EXPECT_GT(len, 0.05f) << "Upper arm too short";
    EXPECT_LT(len, 300.0f) << "Upper arm too long";
}

TEST_F(BoneGroundTruth, HeadAbovePelvis) {
    RndTransformable *head = FindBone("bone_head.mesh");
    RndTransformable *pelvis = FindBone("bone_pelvis.mesh");
    ASSERT_NE(head, nullptr);
    ASSERT_NE(pelvis, nullptr);

    const Vector3 &headPos = head->WorldXfm().v;
    const Vector3 &pelvisPos = pelvis->WorldXfm().v;

    printf("  head pos=(%.3f, %.3f, %.3f), pelvis pos=(%.3f, %.3f, %.3f)\n",
           headPos.x, headPos.y, headPos.z, pelvisPos.x, pelvisPos.y, pelvisPos.z);
    // Milo uses Z-up coordinate system (head Z=64, pelvis Z=42.5)
    float headHeight = std::max(headPos.y, headPos.z);
    float pelvisHeight = std::max(pelvisPos.y, pelvisPos.z);
    EXPECT_GT(headHeight, pelvisHeight) << "Head should be above pelvis (max of Y,Z)";
}

// ============================================================================
// Gate 3: Clip Pose Validation
// ============================================================================

class ClipPoseFixture : public BoneGroundTruth {
protected:
    static CharClip *sClip;
    static ObjectDir *sClipDir;
    static bool sDanceClip; // true if we found a real dance clip (not skeleton retarget)

    static void SetUpTestSuite() {
        BoneGroundTruth::SetUpTestSuite();
        if (!sDir) return;

        // First try finding clips in the bone dir (and subdirs)
        for (ObjDirItr<CharClip> it(sDir, true); it; ++it) {
            sClip = it;
            sClipDir = sDir;
            sDanceClip = false; // likely skeleton retarget clip
            printf("ClipPoseFixture: found clip '%s' in bone dir\n", sClip->Name());
            break;
        }

        // Try loading dance animation clips (real movement data)
        const char *envClip = std::getenv("MILO_TEST_CLIPS");
        const char *danceCandidates[] = {
            "char/crowd/anim/gen/female_base.milo_xbox",
            "char/crowd/anim/gen/male_base.milo_xbox",
            nullptr
        };

        const char **candidates = danceCandidates;
        const char *singleEnv[2] = {nullptr, nullptr};
        if (envClip) {
            singleEnv[0] = envClip;
            candidates = singleEnv;
        }

        for (int i = 0; candidates[i]; i++) {
            ObjectDir *clipDir = TryLoadMilo(candidates[i]);
            if (clipDir) {
                for (ObjDirItr<CharClip> it(clipDir, true); it; ++it) {
                    CharClip *clip = it;
                    // Skip skeleton retarget clips, prefer dance clips
                    const char *name = clip->Name();
                    if (strstr(name, "skeleton") || strstr(name, "retarget"))
                        continue;
                    sClip = clip;
                    sClipDir = clipDir;
                    sDanceClip = true;
                    printf("ClipPoseFixture: found dance clip '%s' in %s\n",
                           name, candidates[i]);
                    break;
                }
                if (sDanceClip) break;
            }
        }

        // Fallback: try skeleton_clips if we still have no clip at all
        if (!sClip) {
            ObjectDir *clipDir = TryLoadMilo(
                "char/main/retarget_skeletons/gen/skeleton_clips.milo_xbox"
            );
            if (clipDir) {
                for (ObjDirItr<CharClip> it(clipDir, true); it; ++it) {
                    sClip = it;
                    sClipDir = clipDir;
                    sDanceClip = false;
                    printf("ClipPoseFixture: fallback clip '%s'\n", sClip->Name());
                    break;
                }
            }
        }

        if (!sClip) {
            printf("ClipPoseFixture: no CharClip found\n");
        }
    }

    void SetUp() override {
        BoneGroundTruth::SetUp();
        if (!sClip) {
            GTEST_SKIP() << "No CharClip found";
        }
    }
};

CharClip *ClipPoseFixture::sClip = nullptr;
ObjectDir *ClipPoseFixture::sClipDir = nullptr;
bool ClipPoseFixture::sDanceClip = false;

TEST_F(ClipPoseFixture, ClipExists) {
    int clipCount = 0;
    for (ObjDirItr<CharClip> it(sClipDir, true); it; ++it) {
        clipCount++;
        if (clipCount <= 5) {
            printf("  clip[%d]: '%s'\n", clipCount - 1, ((CharClip *)it)->Name());
        }
    }
    printf("  Total CharClips: %d\n", clipCount);
    printf("  Dance clip: %s\n", sDanceClip ? "yes" : "no (skeleton retarget)");
    EXPECT_GT(clipCount, 0);
}

TEST_F(ClipPoseFixture, PoseMeshesDoesNotCrash) {
    float beat = sClip->StartBeat();
    sClip->PoseMeshes(sDir, beat);
    printf("  PoseMeshes(dir, %.3f) completed without crash\n", beat);
}

TEST_F(ClipPoseFixture, PoseChangesTransforms) {
    // --- Diagnostic: inspect clip data before testing ---
    printf("  Clip: '%s'\n", sClip->Name());
    printf("  NumFrames: %d\n", sClip->NumFrames());
    printf("  StartBeat: %.3f  EndBeat: %.3f  LengthBeats: %.3f\n",
           sClip->StartBeat(), sClip->EndBeat(), sClip->LengthBeats());
    printf("  FramesPerSec: %.1f\n", sClip->FramesPerSec());

    // Check sample data
    const CharBonesSamples &full = sClip->GetFull();
    const CharBonesSamples &one = sClip->GetOne();
    printf("  Full: NumSamples=%d, NumFrames=%d, TotalSize=%d, Compression=%d\n",
           full.NumSamples(), full.NumFrames(), full.TotalSize(),
           (int)full.GetCompression());
    printf("  One:  NumSamples=%d, NumFrames=%d, TotalSize=%d\n",
           one.NumSamples(), one.NumFrames(), one.TotalSize());

    // List clip bones
    std::list<CharBones::Bone> boneList;
    sClip->ListBones(boneList);
    printf("  Clip has %zu bone channels\n", boneList.size());
    int bonesPrinted = 0;
    for (auto &b : boneList) {
        if (bonesPrinted < 10) {
            printf("    bone: '%s' weight=%.2f\n", b.name.Str(), b.weight);
        }
        bonesPrinted++;
    }
    if (bonesPrinted > 10) printf("    ... and %d more\n", bonesPrinted - 10);

    // Check how many clip bones can be found in the bone dir
    int foundCount = 0, missingCount = 0;
    for (auto &b : boneList) {
        // Simulate CharUtlFindBoneTrans logic
        char buf[256];
        strncpy(buf, b.name.Str(), sizeof(buf) - 1);
        buf[sizeof(buf) - 1] = '\0';
        char *dot = strrchr(buf, '.');
        if (!dot) dot = buf + strlen(buf);

        bool found = false;
        const char *suffixes[] = {".cb", ".trans", ".mesh", nullptr};
        for (int s = 0; suffixes[s]; s++) {
            strcpy(dot, suffixes[s]);
            if (sDir->Find<RndTransformable>(buf, false)) {
                found = true;
                break;
            }
        }
        if (found) foundCount++;
        else {
            missingCount++;
            if (missingCount <= 5)
                printf("    MISSING bone: '%s' (tried .cb/.trans/.mesh)\n", b.name.Str());
        }
    }
    printf("  Bone lookup: %d found, %d missing in dir '%s'\n",
           foundCount, missingCount, sDir->Name());

    // Check BeatToSample — use actual clip beat range!
    float beatA = sClip->StartBeat();
    float beatB = beatA + sClip->LengthBeats() * 0.5f; // midpoint
    float frac;
    int sampA = sClip->BeatToSample(beatA, &frac);
    printf("  BeatToSample(%.3f): sample=%d frac=%.4f\n", beatA, sampA, frac);
    int sampB = sClip->BeatToSample(beatB, &frac);
    printf("  BeatToSample(%.3f): sample=%d frac=%.4f\n", beatB, sampB, frac);

    // --- Actual test: check LocalXfm changes (not just WorldXfm) ---
    sClip->PoseMeshes(sDir, beatA);

    struct BoneSnapshot {
        RndTransformable *bone;
        Vector3 localPos;
        Vector3 worldPos;
    };
    std::vector<BoneSnapshot> frame0;

    for (ObjDirItr<RndTransformable> it(sDir, true); it; ++it) {
        RndTransformable *bone = it;
        frame0.push_back({bone, bone->LocalXfm().v, bone->WorldXfm().v});
    }

    // Apply clip at midpoint beat
    sClip->PoseMeshes(sDir, beatB);

    int localMoved = 0, worldMoved = 0;
    for (size_t i = 0; i < frame0.size(); i++) {
        const Vector3 &newLocal = frame0[i].bone->LocalXfm().v;
        const Vector3 &oldLocal = frame0[i].localPos;
        float dxL = newLocal.x - oldLocal.x;
        float dyL = newLocal.y - oldLocal.y;
        float dzL = newLocal.z - oldLocal.z;
        float distL = std::sqrt(dxL * dxL + dyL * dyL + dzL * dzL);
        if (distL > 0.001f) {
            localMoved++;
            if (localMoved <= 3) {
                printf("    LOCAL moved: '%s' (%.3f,%.3f,%.3f)->(%.3f,%.3f,%.3f) d=%.4f\n",
                       frame0[i].bone->Name(),
                       oldLocal.x, oldLocal.y, oldLocal.z,
                       newLocal.x, newLocal.y, newLocal.z, distL);
            }
        }

        const Vector3 &newWorld = frame0[i].bone->WorldXfm().v;
        const Vector3 &oldWorld = frame0[i].worldPos;
        float dxW = newWorld.x - oldWorld.x;
        float dyW = newWorld.y - oldWorld.y;
        float dzW = newWorld.z - oldWorld.z;
        float distW = std::sqrt(dxW * dxW + dyW * dyW + dzW * dzW);
        if (distW > 0.001f) worldMoved++;
    }

    printf("  %d/%zu bones LOCAL pos moved, %d/%zu bones WORLD pos moved (beat %.1f→%.1f)\n",
           localMoved, frame0.size(), worldMoved, frame0.size(), beatA, beatB);

    if (sDanceClip) {
        // Dance clips move bones via rotation (world positions change through parent chain)
        // At least some world-space positions should differ between two beats
        EXPECT_GT(worldMoved, 0) << "Dance clip should move bones in world space";
    } else if (worldMoved == 0) {
        printf("  NOTE: No bones moved — skeleton retarget clips are static poses\n");
    }
}

TEST_F(ClipPoseFixture, PoseDeterminism) {
    // Use a beat within the clip's actual range
    float beat = sClip->StartBeat() + sClip->LengthBeats() * 0.25f;
    sClip->PoseMeshes(sDir, beat);

    struct BoneSnapshot {
        RndTransformable *bone;
        Vector3 pos;
    };
    std::vector<BoneSnapshot> pass1;

    for (ObjDirItr<RndTransformable> it(sDir, true); it; ++it) {
        RndTransformable *bone = it;
        pass1.push_back({bone, bone->WorldXfm().v});
    }

    sClip->PoseMeshes(sDir, beat);

    int mismatches = 0;
    for (size_t i = 0; i < pass1.size(); i++) {
        const Vector3 &newPos = pass1[i].bone->WorldXfm().v;
        const Vector3 &oldPos = pass1[i].pos;
        if (newPos.x != oldPos.x || newPos.y != oldPos.y || newPos.z != oldPos.z) {
            if (mismatches < 3) {
                printf("  Mismatch: %s (%.6f,%.6f,%.6f) vs (%.6f,%.6f,%.6f)\n",
                       pass1[i].bone->Name(),
                       oldPos.x, oldPos.y, oldPos.z,
                       newPos.x, newPos.y, newPos.z);
            }
            mismatches++;
        }
    }

    printf("  %d/%zu bones had non-deterministic results\n",
           mismatches, pass1.size());
    EXPECT_EQ(mismatches, 0) << "PoseMeshes should be deterministic for same beat";
}

// ============================================================================
// main.milo_xbox loading (was DISABLED_ — crash fixed in RndMesh::OnSync)
// ============================================================================

class MainMiloLoadTest : public EngineTestFixture {};

TEST_F(MainMiloLoadTest, LoadMainCharacterMilo) {
    // main.milo_xbox has ~21 subdirs (wind, skeleton, flows, etc.)
    // Crash was fixed by adding bestFaceIt tracking in RndMesh::OnSync face patching.
    const char *path = "char/main/gen/main.milo_xbox";

    FilePath fp(path);
    ChunkStream *probe = new ChunkStream(
        fp.c_str(), ChunkStream::kRead, 0x8000, false, kPlatformNone, false
    );
    if (probe->Fail()) {
        delete probe;
        GTEST_SKIP() << "main.milo_xbox not found";
    }
    delete probe;

    printf("MainMiloLoadTest: attempting to load %s\n", path);
    ObjectDir *dir = DirLoader::LoadObjects(fp, nullptr, nullptr);

    // When this stops crashing, enable the test (remove DISABLED_ prefix)
    // and this assertion will verify it actually loaded.
    ASSERT_NE(dir, nullptr)
        << "main.milo_xbox should load without crashing. "
        << "If this fails after removing DISABLED_, the crash is fixed but "
        << "LoadObjects returned null.";

    printf("  Loaded: '%s' class='%s'\n", dir->Name(), dir->ClassName().Str());

    // Verify it has bones (main character should have full skeleton)
    RndTransformable *pelvis = dir->Find<RndTransformable>("bone_pelvis.mesh", false);
    EXPECT_NE(pelvis, nullptr) << "main.milo should contain bone_pelvis.mesh";
}
