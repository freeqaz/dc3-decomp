// Foot Bone Invariant Tests — detect the "merged characters" bug where all
// characters' limb bones collapse to a shared point, creating stretched meshes.
//
// The "merged characters" symptom is: after IK, multiple characters' ankle/knee
// bones end up at the same world position (or at the origin), and foot meshes
// appear stretched or deformed. This test loads a real character .milo_xbox and
// validates spatial invariants that MUST hold for a correctly posed skeleton.
//
// These are regression tests: today's behavior is broken. The invariants here
// describe what CORRECT bones look like, derived from first principles:
//   - Left and right ankles must be spatially separated (bilateral symmetry)
//   - No limb bone should sit at the origin (0,0,0)
//   - Toe bones must be below their ankle bones (Z-up coordinate system)
//   - Pelvis-to-ankle distance must exceed a minimum (legs aren't collapsed)
//   - No bone should have garbage coordinate values
//
// Requires: MILO_LIB or ~/code/milohax/milo-engine-libs/harmonix-repos/milo-rnd-library/dc3
//
// Run: cd native/build && ctest -R FootBone --output-on-failure

#include "test_helpers.h"

#include "char/CharUtl.h"
#include "char/Character.h"
#include "hamobj/HamCharacter.h"
#include "hamobj/HamIKEffector.h"
#include "math/Mtx.h"
#include "math/Vec.h"
#include "obj/Dir.h"
#include "obj/DirLoader.h"
#include "obj/Object.h"
#include "rndobj/Trans.h"
#include "utl/ChunkStream.h"
#include "utl/FilePath.h"

#include <cmath>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

// ============================================================================
// Helpers
// ============================================================================

static std::string GetMiloLibRoot() {
    const char *env = getenv("MILO_LIB");
    if (env && env[0])
        return env;
    const char *home = getenv("HOME");
    if (home && home[0])
        return std::string(home)
            + "/code/milohax/milo-engine-libs/harmonix-repos/milo-rnd-library/dc3";
    return "";
}

static bool FileExists(const std::string &path) {
    struct stat st;
    return stat(path.c_str(), &st) == 0;
}

static float BoneDistance(const Vector3 &a, const Vector3 &b) {
    float dx = a.x - b.x;
    float dy = a.y - b.y;
    float dz = a.z - b.z;
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

static float BoneLength(const Vector3 &v) {
    return std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
}

// ============================================================================
// Fixture: loads main.milo_xbox character once per test suite
// ============================================================================

class FootBoneInvariants : public EngineTestFixture {
protected:
    static ObjectDir *sDir;

    static void SetUpTestSuite() {
        EngineTestFixture::SetUpTestSuite();

        std::string root = GetMiloLibRoot();
        if (root.empty()) {
            printf("FootBoneInvariants: MILO_LIB not set, no home dir\n");
            return;
        }

        std::string path = root + "/char/main/gen/main.milo_xbox";
        if (!FileExists(path)) {
            printf("FootBoneInvariants: %s not found\n", path.c_str());
            return;
        }

        printf("FootBoneInvariants: loading %s\n", path.c_str());
        sDir = DirLoader::LoadObjects(FilePath(path.c_str()), nullptr, nullptr);
        if (sDir) {
            printf("FootBoneInvariants: loaded '%s' class='%s'\n",
                   sDir->Name(), sDir->ClassName().Str());
        }
    }

    void SetUp() override {
        if (!sDir) {
            GTEST_SKIP() << "Character asset not loaded (need MILO_LIB or "
                         << "~/code/milohax/milo-engine-libs/.../dc3)";
        }
    }

    // Find a bone by name using the engine's CharUtlFindBoneTrans, which
    // searches for .cb, .trans, and .mesh suffixed objects in the dir.
    RndTransformable *FindBone(const char *name) {
        return CharUtlFindBoneTrans(name, sDir);
    }

    // Also allow direct .mesh lookup for bones that CharUtlFindBoneTrans
    // might not find (e.g., if CharBone is not set up).
    RndTransformable *FindBoneMesh(const char *name) {
        return sDir->Find<RndTransformable>(name, false);
    }
};

ObjectDir *FootBoneInvariants::sDir = nullptr;

// ============================================================================
// Invariant 1: Left and right ankles are NOT at the same position
// ============================================================================
// In the merged-characters bug, all characters' ankle bones collapse to the
// same world position. Even for a single character in rest pose, the left and
// right ankles must be separated by at least 5 units (bilateral separation).

TEST_F(FootBoneInvariants, AnklesBilaterallySeparated) {
    RndTransformable *lAnkle = FindBone("bone_L-ankle");
    RndTransformable *rAnkle = FindBone("bone_R-ankle");

    // Fall back to .mesh suffix if CharUtlFindBoneTrans fails
    if (!lAnkle) lAnkle = FindBoneMesh("bone_L-ankle.mesh");
    if (!rAnkle) rAnkle = FindBoneMesh("bone_R-ankle.mesh");

    ASSERT_NE(lAnkle, nullptr) << "bone_L-ankle not found in character dir";
    ASSERT_NE(rAnkle, nullptr) << "bone_R-ankle not found in character dir";

    const Vector3 &lPos = lAnkle->WorldXfm().v;
    const Vector3 &rPos = rAnkle->WorldXfm().v;

    float dist = BoneDistance(lPos, rPos);
    printf("  L-ankle: (%.2f, %.2f, %.2f)\n", lPos.x, lPos.y, lPos.z);
    printf("  R-ankle: (%.2f, %.2f, %.2f)\n", rPos.x, rPos.y, rPos.z);
    printf("  Bilateral separation: %.2f units\n", dist);

    EXPECT_GT(dist, 5.0f)
        << "Left and right ankles are too close together (dist=" << dist
        << "). This is the 'merged characters' bug — bones have collapsed "
        << "to a shared point.";
}

// ============================================================================
// Invariant 2: Ankle bones are NOT at the origin (0,0,0)
// ============================================================================
// A bone at exactly the origin means the world transform was never computed
// or was zeroed out by a bug. Real character bones are positioned relative
// to the skeleton root and should never be at the world origin.

TEST_F(FootBoneInvariants, AnklesNotAtOrigin) {
    const char *ankleNames[] = {"bone_L-ankle", "bone_R-ankle", nullptr};

    for (int i = 0; ankleNames[i]; i++) {
        RndTransformable *ankle = FindBone(ankleNames[i]);
        if (!ankle) ankle = FindBoneMesh(
            (std::string(ankleNames[i]) + ".mesh").c_str()
        );
        ASSERT_NE(ankle, nullptr) << ankleNames[i] << " not found";

        const Vector3 &pos = ankle->WorldXfm().v;
        float distFromOrigin = BoneLength(pos);

        printf("  %s: (%.2f, %.2f, %.2f) dist_from_origin=%.2f\n",
               ankleNames[i], pos.x, pos.y, pos.z, distFromOrigin);

        EXPECT_GT(distFromOrigin, 1.0f)
            << ankleNames[i] << " is at or near the origin (dist="
            << distFromOrigin << "). World transform was not computed.";
    }
}

// ============================================================================
// Invariant 3: Toe bones are below their respective ankle bones
// ============================================================================
// Milo uses Z-up coordinates. The toe bone should be closer to the ground
// (lower Z) than the ankle bone. If toeZ > ankleZ, the foot is inverted.

TEST_F(FootBoneInvariants, ToesBelowAnkles) {
    struct Side {
        const char *ankle;
        const char *toe;
        const char *label;
    };
    Side sides[] = {
        {"bone_L-ankle", "bone_L-toe", "Left"},
        {"bone_R-ankle", "bone_R-toe", "Right"},
    };

    for (auto &s : sides) {
        RndTransformable *ankle = FindBone(s.ankle);
        RndTransformable *toe = FindBone(s.toe);
        if (!ankle) ankle = FindBoneMesh(
            (std::string(s.ankle) + ".mesh").c_str()
        );
        if (!toe) toe = FindBoneMesh(
            (std::string(s.toe) + ".mesh").c_str()
        );

        if (!ankle || !toe) {
            printf("  SKIP %s: ankle=%p toe=%p\n",
                   s.label, (void *)ankle, (void *)toe);
            continue;
        }

        float ankleZ = ankle->WorldXfm().v.z;
        float toeZ = toe->WorldXfm().v.z;

        printf("  %s: ankle Z=%.3f, toe Z=%.3f (delta=%.3f)\n",
               s.label, ankleZ, toeZ, toeZ - ankleZ);

        // Toe should be at or below the ankle. Allow a small tolerance (2 units)
        // for animation blending artifacts, but anything more means inversion.
        EXPECT_LT(toeZ, ankleZ + 2.0f)
            << s.label << " toe is above ankle — foot is inverted. "
            << "toeZ=" << toeZ << " ankleZ=" << ankleZ;
    }
}

// ============================================================================
// Invariant 4: Pelvis-to-ankle distance exceeds a minimum threshold
// ============================================================================
// The distance from pelvis to each ankle represents the leg length. If this
// collapses below a minimum, the legs have been crushed to a point. A typical
// humanoid character has pelvis-to-ankle distance of ~40-80 units. We use a
// conservative threshold of 15 units to catch complete collapse while allowing
// for crouching/squatting poses.

TEST_F(FootBoneInvariants, LegsNotCollapsed) {
    RndTransformable *pelvis = FindBone("bone_pelvis");
    if (!pelvis) pelvis = FindBoneMesh("bone_pelvis.mesh");
    ASSERT_NE(pelvis, nullptr) << "bone_pelvis not found";

    const char *ankleNames[] = {"bone_L-ankle", "bone_R-ankle", nullptr};
    const char *labels[] = {"Left", "Right"};

    for (int i = 0; ankleNames[i]; i++) {
        RndTransformable *ankle = FindBone(ankleNames[i]);
        if (!ankle) ankle = FindBoneMesh(
            (std::string(ankleNames[i]) + ".mesh").c_str()
        );
        if (!ankle) {
            printf("  SKIP %s: ankle not found\n", labels[i]);
            continue;
        }

        float dist = BoneDistance(pelvis->WorldXfm().v, ankle->WorldXfm().v);
        printf("  %s pelvis-to-ankle: %.2f units\n", labels[i], dist);

        EXPECT_GT(dist, 15.0f)
            << labels[i] << " leg is collapsed (pelvis-to-ankle=" << dist
            << " units). Expected > 15 for a standing or crouching pose. "
            << "This indicates the merged-characters bug.";
    }
}

// ============================================================================
// Invariant 5: No limb bone has garbage values
// ============================================================================
// A bone with coordinates > 10000 in absolute value is almost certainly
// corrupted. Real DC3 character bones live in a space of roughly [-200, 200].

TEST_F(FootBoneInvariants, NoBoneGarbageValues) {
    const char *criticalBones[] = {
        "bone_pelvis", "bone_L-ankle", "bone_R-ankle",
        "bone_L-toe", "bone_R-toe", "bone_L-knee", "bone_R-knee",
        "bone_L-thigh", "bone_R-thigh", "bone_head",
        "bone_L-hand", "bone_R-hand",
        nullptr
    };

    const float kMaxAbsValue = 10000.0f;
    int checked = 0;

    for (int i = 0; criticalBones[i]; i++) {
        RndTransformable *bone = FindBone(criticalBones[i]);
        if (!bone) bone = FindBoneMesh(
            (std::string(criticalBones[i]) + ".mesh").c_str()
        );
        if (!bone) continue;
        checked++;

        const Vector3 &pos = bone->WorldXfm().v;
        bool xOk = std::fabs(pos.x) < kMaxAbsValue;
        bool yOk = std::fabs(pos.y) < kMaxAbsValue;
        bool zOk = std::fabs(pos.z) < kMaxAbsValue;
        bool nanCheck = (pos.x == pos.x) && (pos.y == pos.y) && (pos.z == pos.z);

        if (!xOk || !yOk || !zOk || !nanCheck) {
            printf("  GARBAGE: %s at (%.2f, %.2f, %.2f)\n",
                   criticalBones[i], pos.x, pos.y, pos.z);
        }

        EXPECT_TRUE(nanCheck)
            << criticalBones[i] << " has NaN coordinates";
        EXPECT_TRUE(xOk && yOk && zOk)
            << criticalBones[i] << " has garbage coordinates: ("
            << pos.x << ", " << pos.y << ", " << pos.z << ")";
    }

    printf("  Checked %d bones for garbage values\n", checked);
    EXPECT_GT(checked, 0) << "No critical bones found in character";
}

// ============================================================================
// Invariant 6: Knee bones are between pelvis and ankle in Z
// ============================================================================
// The knee should be vertically between the pelvis and ankle — its Z value
// should be less than the pelvis Z and greater than the ankle Z (or at least
// close). If the knee is at the same height as the ankle, the leg has
// collapsed. If above the pelvis, something is deeply wrong.

TEST_F(FootBoneInvariants, KneesBetweenPelvisAndAnkle) {
    RndTransformable *pelvis = FindBone("bone_pelvis");
    if (!pelvis) pelvis = FindBoneMesh("bone_pelvis.mesh");
    ASSERT_NE(pelvis, nullptr) << "bone_pelvis not found";

    struct LegSide {
        const char *knee;
        const char *ankle;
        const char *label;
    };
    LegSide legs[] = {
        {"bone_L-knee", "bone_L-ankle", "Left"},
        {"bone_R-knee", "bone_R-ankle", "Right"},
    };

    for (auto &leg : legs) {
        RndTransformable *knee = FindBone(leg.knee);
        RndTransformable *ankle = FindBone(leg.ankle);
        if (!knee) knee = FindBoneMesh(
            (std::string(leg.knee) + ".mesh").c_str()
        );
        if (!ankle) ankle = FindBoneMesh(
            (std::string(leg.ankle) + ".mesh").c_str()
        );
        if (!knee || !ankle) {
            printf("  SKIP %s: knee=%p ankle=%p\n",
                   leg.label, (void *)knee, (void *)ankle);
            continue;
        }

        float pelvisZ = pelvis->WorldXfm().v.z;
        float kneeZ = knee->WorldXfm().v.z;
        float ankleZ = ankle->WorldXfm().v.z;

        printf("  %s leg Z: pelvis=%.2f knee=%.2f ankle=%.2f\n",
               leg.label, pelvisZ, kneeZ, ankleZ);

        // Knee should not be above the pelvis
        EXPECT_LT(kneeZ, pelvisZ + 5.0f)
            << leg.label << " knee is above pelvis (kneeZ=" << kneeZ
            << " pelvisZ=" << pelvisZ << ")";

        // Knee-to-ankle distance should be nonzero (lower leg not collapsed)
        float kneeToAnkle = BoneDistance(knee->WorldXfm().v, ankle->WorldXfm().v);
        printf("  %s knee-to-ankle distance: %.2f\n", leg.label, kneeToAnkle);

        EXPECT_GT(kneeToAnkle, 5.0f)
            << leg.label << " lower leg collapsed (knee-to-ankle=" << kneeToAnkle
            << " units). Expected > 5.";
    }
}

// ============================================================================
// Invariant 7: Left and right limbs are roughly symmetric
// ============================================================================
// In rest pose, the skeleton should be roughly bilaterally symmetric about
// the Y=0 plane (or centered on the pelvis). The left ankle's Y offset from
// pelvis should be roughly the negative of the right ankle's Y offset.
// A large asymmetry suggests one side's IK was computed from garbage data.

TEST_F(FootBoneInvariants, BilateralSymmetry) {
    RndTransformable *pelvis = FindBone("bone_pelvis");
    if (!pelvis) pelvis = FindBoneMesh("bone_pelvis.mesh");
    ASSERT_NE(pelvis, nullptr);

    struct Pair {
        const char *left;
        const char *right;
        const char *label;
    };
    Pair pairs[] = {
        {"bone_L-ankle", "bone_R-ankle", "ankles"},
        {"bone_L-knee", "bone_R-knee", "knees"},
    };

    for (auto &p : pairs) {
        RndTransformable *lBone = FindBone(p.left);
        RndTransformable *rBone = FindBone(p.right);
        if (!lBone) lBone = FindBoneMesh(
            (std::string(p.left) + ".mesh").c_str()
        );
        if (!rBone) rBone = FindBoneMesh(
            (std::string(p.right) + ".mesh").c_str()
        );
        if (!lBone || !rBone) continue;

        float pelvisY = pelvis->WorldXfm().v.y;
        float lOffsetY = lBone->WorldXfm().v.y - pelvisY;
        float rOffsetY = rBone->WorldXfm().v.y - pelvisY;

        // For bilateral symmetry, one side should be positive and the other
        // negative (or both near zero). Their sum should be small relative
        // to their spread.
        float spread = std::fabs(lOffsetY) + std::fabs(rOffsetY);
        float asymmetry = std::fabs(lOffsetY + rOffsetY);

        printf("  %s: L offset_Y=%.2f, R offset_Y=%.2f, asymmetry=%.2f\n",
               p.label, lOffsetY, rOffsetY, asymmetry);

        if (spread > 1.0f) {
            // One side should not be wildly different from the other.
            // In rest pose both sides may have the same sign offset, so
            // only flag if one side is >10x larger than the other.
            float larger = std::max(std::fabs(lOffsetY), std::fabs(rOffsetY));
            float smaller = std::min(std::fabs(lOffsetY), std::fabs(rOffsetY));
            EXPECT_GT(smaller, larger * 0.1f)
                << p.label << " are heavily asymmetric. L_offset=" << lOffsetY
                << " R_offset=" << rOffsetY
                << ". One side may have IK computed from garbage.";
        }
    }
}

// ============================================================================
// Invariant 8: Ankle-to-toe vector points generally downward
// ============================================================================
// The normalized Z component of the ankle-to-toe vector should be negative
// or near zero (pointing downward toward the ground). A strongly positive
// value means the foot is flipped upside down.

TEST_F(FootBoneInvariants, AnkleToToeVectorPointsDown) {
    struct Side {
        const char *ankle;
        const char *toe;
        const char *label;
    };
    Side sides[] = {
        {"bone_L-ankle", "bone_L-toe", "Left"},
        {"bone_R-ankle", "bone_R-toe", "Right"},
    };

    for (auto &s : sides) {
        RndTransformable *ankle = FindBone(s.ankle);
        RndTransformable *toe = FindBone(s.toe);
        if (!ankle) ankle = FindBoneMesh(
            (std::string(s.ankle) + ".mesh").c_str()
        );
        if (!toe) toe = FindBoneMesh(
            (std::string(s.toe) + ".mesh").c_str()
        );
        if (!ankle || !toe) continue;

        float dx = toe->WorldXfm().v.x - ankle->WorldXfm().v.x;
        float dy = toe->WorldXfm().v.y - ankle->WorldXfm().v.y;
        float dz = toe->WorldXfm().v.z - ankle->WorldXfm().v.z;
        float len = std::sqrt(dx * dx + dy * dy + dz * dz);

        if (len < 0.01f) {
            printf("  %s: ankle and toe at same position (len=%.4f)\n",
                   s.label, len);
            ADD_FAILURE() << s.label << " ankle and toe bones are at the same "
                          << "position — foot has zero length.";
            continue;
        }

        float normZ = dz / len;
        printf("  %s ankle-to-toe: dir=(%.2f, %.2f, %.2f) normZ=%.3f\n",
               s.label, dx / len, dy / len, normZ, normZ);

        // Should not be strongly positive (pointing up through shin)
        EXPECT_LT(normZ, 0.5f)
            << s.label << " ankle-to-toe vector points upward (normZ="
            << normZ << "). The foot is inverted.";
    }
}

// ============================================================================
// Invariant 9: All critical bones are distinct positions
// ============================================================================
// In the merged-characters bug, multiple bones collapse to the same position.
// Even within a single character, pelvis/knees/ankles/toes should all be at
// distinct positions. No two of these should be closer than 3 units.

TEST_F(FootBoneInvariants, CriticalBonesAtDistinctPositions) {
    const char *boneNames[] = {
        "bone_pelvis",
        "bone_L-thigh", "bone_R-thigh",
        "bone_L-knee", "bone_R-knee",
        "bone_L-ankle", "bone_R-ankle",
        "bone_L-toe", "bone_R-toe",
        nullptr
    };

    struct BonePos {
        const char *name;
        Vector3 pos;
    };
    std::vector<BonePos> bones;

    for (int i = 0; boneNames[i]; i++) {
        RndTransformable *bone = FindBone(boneNames[i]);
        if (!bone) bone = FindBoneMesh(
            (std::string(boneNames[i]) + ".mesh").c_str()
        );
        if (!bone) continue;
        bones.push_back({boneNames[i], bone->WorldXfm().v});
    }

    printf("  Found %zu critical bones\n", bones.size());

    const float kMinDistinct = 3.0f;
    int collapseCount = 0;

    // Check all pairs. Bones on the SAME side of the body that are adjacent
    // in the chain (e.g., L-ankle and L-toe) might be close, but bones from
    // different chain levels (pelvis vs ankle) should be far apart.
    for (size_t i = 0; i < bones.size(); i++) {
        for (size_t j = i + 1; j < bones.size(); j++) {
            float dist = BoneDistance(bones[i].pos, bones[j].pos);
            if (dist < kMinDistinct) {
                // Allow same-side adjacent bones to be close (e.g., ankle/toe)
                bool sameChain =
                    (strstr(bones[i].name, "ankle") && strstr(bones[j].name, "toe"))
                    || (strstr(bones[i].name, "toe") && strstr(bones[j].name, "ankle"));
                if (!sameChain) {
                    printf("  COLLAPSED: %s and %s at distance %.2f\n",
                           bones[i].name, bones[j].name, dist);
                    collapseCount++;
                }
            }
        }
    }

    EXPECT_EQ(collapseCount, 0)
        << collapseCount << " bone pairs are collapsed to the same position. "
        << "This is the merged-characters bug — bones that should be distinct "
        << "are at the same world coordinates.";
}

// ============================================================================
// Invariant 10: Ankle rotation matrix Z-axis is not flipped
// ============================================================================
// The ankle bone's rotation matrix Z column should not point strongly upward.
// In a correct pose the Z-axis points roughly downward or laterally. A
// z.z component > 0.7 means the foot is rotated ~180 degrees.

TEST_F(FootBoneInvariants, AnkleRotationNotFlipped) {
    const char *ankleNames[] = {"bone_L-ankle", "bone_R-ankle", nullptr};

    for (int i = 0; ankleNames[i]; i++) {
        RndTransformable *ankle = FindBone(ankleNames[i]);
        if (!ankle) ankle = FindBoneMesh(
            (std::string(ankleNames[i]) + ".mesh").c_str()
        );
        if (!ankle) continue;

        float zAxisZ = ankle->WorldXfm().m.z.z;
        printf("  %s Z-axis z-component: %.3f\n", ankleNames[i], zAxisZ);

        EXPECT_LT(zAxisZ, 0.7f)
            << ankleNames[i] << " Z-axis points upward (z.z=" << zAxisZ
            << "), indicating the ankle rotation is flipped 180 degrees.";
    }
}
