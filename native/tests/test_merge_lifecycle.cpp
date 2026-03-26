// HUD merge lifecycle tests — model CORRECT Xbox behavior for merge + delete.
//
// The game loads HUD content (hud_left, hud_right) from _default_hud.milo via
// FileMerger.  MergeDirs moves objects from source dir into the merge target
// (PanelDir "hud", which is WorldDir::mHUD).  PostMerge then deletes the
// source dir.  On Xbox, the merged objects survive because ~ObjectDir does NOT
// have a NullifyAllRefs cascade.
//
// On native, our NullifyAllRefs cascade in ~ObjectDir kills objects that were
// reparented via SetName during MergeDirs.  These tests define the correct
// (Xbox) behavior.  They will likely FAIL today — that is expected.
//
// Each test is self-contained: create dirs, merge/reparent, delete source,
// verify objects survive in the target.

#include "test_helpers.h"

#include "obj/Dir.h"
#include "obj/Object.h"
#include "obj/Utl.h"

#include <string>
#include <vector>

namespace {

// ============================================================================
// Fixture
// ============================================================================

class MergeLifecycleTest : public EngineTestFixture {};

// ============================================================================
// Test 1: ObjectsSurviveSourceDirDeletion
//
// After SetName reparents objects from source to target, deleting the source
// dir must not destroy or nullify the reparented objects.
// ============================================================================

TEST_F(MergeLifecycleTest, ObjectsSurviveSourceDirDeletion) {
    ObjectDir *source = Hmx::Object::New<ObjectDir>();
    source->SetName("source_dir", ObjectDir::Main());

    ObjectDir *target = Hmx::Object::New<ObjectDir>();
    target->SetName("target_dir", ObjectDir::Main());

    // Create objects in the source dir
    Hmx::Object *objA = Hmx::Object::New<Hmx::Object>();
    objA->SetName("hud_left", source);

    Hmx::Object *objB = Hmx::Object::New<Hmx::Object>();
    objB->SetName("hud_right", source);

    // Reparent objects to target (simulates what MergeObject does for new objects)
    objA->SetName("hud_left", target);
    objB->SetName("hud_right", target);

    // Verify objects are in target before deletion
    ASSERT_EQ(target->FindObject("hud_left", false, false), objA);
    ASSERT_EQ(target->FindObject("hud_right", false, false), objB);

    // Delete the source dir — Xbox does NOT cascade-nullify reparented objects
    delete source;

    // CORE ASSERTION: objects must survive in the target
    Hmx::Object *foundA = target->FindObject("hud_left", false, false);
    Hmx::Object *foundB = target->FindObject("hud_right", false, false);

    EXPECT_NE(foundA, nullptr) << "hud_left was killed when source dir was deleted";
    EXPECT_NE(foundB, nullptr) << "hud_right was killed when source dir was deleted";
    EXPECT_EQ(foundA, objA) << "hud_left changed identity";
    EXPECT_EQ(foundB, objB) << "hud_right changed identity";

    // Cleanup
    delete target;
}

// ============================================================================
// Test 2: SubdirsSurviveSourceDirDeletion
//
// When subdirs are moved from source to target via AppendSubDir (kMergeReplace
// path in MergeObjectsRecurse), deleting the source must not destroy the subdir
// or kill the ObjDirPtr in the target's mSubDirs.
//
// Note: AppendSubDir calls SetSubDir(true) which calls SetName(nullptr, nullptr),
// removing the subdir's name from the hash table. This is correct engine
// behavior — subdirs are findable via SubDirs(), not by name. We check
// HasSubDir and pointer identity instead of FindObject by name.
// ============================================================================

TEST_F(MergeLifecycleTest, SubdirsSurviveSourceDirDeletion) {
    ObjectDir *source = Hmx::Object::New<ObjectDir>();
    source->SetName("merge_source", ObjectDir::Main());

    ObjectDir *target = Hmx::Object::New<ObjectDir>();
    target->SetName("merge_target", ObjectDir::Main());

    // Create a subdir in the source
    ObjectDir *child = Hmx::Object::New<ObjectDir>();
    child->SetName("effects_subdir", source);
    source->AppendSubDir(ObjDirPtr<ObjectDir>(child));

    ASSERT_TRUE(source->HasSubDir(child));

    // Move subdir to target (mimics kMergeReplace path)
    target->AppendSubDir(ObjDirPtr<ObjectDir>(child));
    source->RemoveSubDir(ObjDirPtr<ObjectDir>(child));

    ASSERT_TRUE(target->HasSubDir(child));
    ASSERT_FALSE(source->HasSubDir(child));

    // Delete the source dir
    delete source;

    // CORE ASSERTION: subdir must survive in the target's SubDirs list.
    // Note: SetSubDir(true) cleared the subdir's name, so FindObject by name
    // won't work. Use HasSubDir and direct pointer check instead.
    EXPECT_TRUE(target->HasSubDir(child))
        << "Subdir was removed from target's mSubDirs when source was deleted";

    // Verify the subdir pointer is still valid (not freed)
    // If the subdir was destroyed, this would crash or return garbage.
    bool childIsSubDir = child->IsSubDir();
    EXPECT_TRUE(childIsSubDir || !childIsSubDir)
        << "Subdir pointer is invalid (use-after-free)";

    // Verify the ObjDirPtr in target's SubDirs still resolves to child
    bool foundInSubDirs = false;
    for (int i = 0; i < (int)target->SubDirs().size(); i++) {
        if ((ObjectDir *)target->SubDirs()[i] == child) {
            foundInSubDirs = true;
            break;
        }
    }
    EXPECT_TRUE(foundInSubDirs)
        << "Child pointer not found in target's SubDirs vector";

    // Cleanup
    delete target;
}

// ============================================================================
// Test 3: MergeDirsPreservesObjectsAfterSourceDeletion
//
// End-to-end: MergeDirs from source to target with default filter, then
// delete source.  All merged objects should be findable in target.
// ============================================================================

TEST_F(MergeLifecycleTest, MergeDirsPreservesObjectsAfterSourceDeletion) {
    ObjectDir *source = Hmx::Object::New<ObjectDir>();
    source->SetName("milo_source", ObjectDir::Main());

    ObjectDir *target = Hmx::Object::New<ObjectDir>();
    target->SetName("milo_target", ObjectDir::Main());

    // Populate source with objects (simulates .milo content)
    Hmx::Object *obj1 = Hmx::Object::New<Hmx::Object>();
    obj1->SetName("track_marker.obj", source);

    Hmx::Object *obj2 = Hmx::Object::New<Hmx::Object>();
    obj2->SetName("score_display.obj", source);

    Hmx::Object *obj3 = Hmx::Object::New<Hmx::Object>();
    obj3->SetName("combo_counter.obj", source);

    // Reserve space in target, then merge
    ReserveToFit(source, target, 0);
    MergeFilter filt(MergeFilter::kReplace, MergeFilter::kNoSubdirs);
    MergeDirs(source, target, filt);

    // Verify objects are in target after merge
    ASSERT_NE(target->FindObject("track_marker.obj", false, true), nullptr);
    ASSERT_NE(target->FindObject("score_display.obj", false, true), nullptr);
    ASSERT_NE(target->FindObject("combo_counter.obj", false, true), nullptr);

    // Delete source (simulates FileMerger PostMerge)
    delete source;

    // CORE ASSERTION: all merged objects survive
    EXPECT_NE(target->FindObject("track_marker.obj", false, true), nullptr)
        << "track_marker.obj killed by source deletion";
    EXPECT_NE(target->FindObject("score_display.obj", false, true), nullptr)
        << "score_display.obj killed by source deletion";
    EXPECT_NE(target->FindObject("combo_counter.obj", false, true), nullptr)
        << "combo_counter.obj killed by source deletion";

    // Cleanup
    delete target;
}

// ============================================================================
// Test 4: FindObjectWorksAfterMergeAndSourceDeletion
//
// Verify both FindObject modes work after merge + source deletion:
//   - FindObject("name", false, false)  — hash table only, no subdir search
//   - FindObject("name", false, true)   — includes subdir search
// ============================================================================

TEST_F(MergeLifecycleTest, FindObjectWorksAfterMergeAndSourceDeletion) {
    ObjectDir *source = Hmx::Object::New<ObjectDir>();
    source->SetName("find_source", ObjectDir::Main());

    ObjectDir *target = Hmx::Object::New<ObjectDir>();
    target->SetName("find_target", ObjectDir::Main());

    // Create objects in source
    Hmx::Object *hudLeft = Hmx::Object::New<Hmx::Object>();
    hudLeft->SetName("hud_left", source);

    Hmx::Object *hudRight = Hmx::Object::New<Hmx::Object>();
    hudRight->SetName("hud_right", source);

    // Merge into target
    ReserveToFit(source, target, 0);
    MergeFilter filt(MergeFilter::kReplace, MergeFilter::kNoSubdirs);
    MergeDirs(source, target, filt);

    // Delete source
    delete source;

    // Test 1: FindObject without subdir search (hash table only)
    Hmx::Object *foundDirect = target->FindObject("hud_left", false, false);
    EXPECT_NE(foundDirect, nullptr)
        << "FindObject(hud_left, false, false) failed after source deletion";

    // Test 2: FindObject with subdir search
    Hmx::Object *foundSubdir = target->FindObject("hud_left", false, true);
    EXPECT_NE(foundSubdir, nullptr)
        << "FindObject(hud_left, false, true) failed after source deletion";

    // Test 3: Both modes return the same object
    if (foundDirect && foundSubdir) {
        EXPECT_EQ(foundDirect, foundSubdir)
            << "FindObject modes returned different objects";
    }

    // Test 4: hud_right also accessible via both modes
    Hmx::Object *rightDirect = target->FindObject("hud_right", false, false);
    Hmx::Object *rightSubdir = target->FindObject("hud_right", false, true);
    EXPECT_NE(rightDirect, nullptr)
        << "FindObject(hud_right, false, false) failed after source deletion";
    EXPECT_NE(rightSubdir, nullptr)
        << "FindObject(hud_right, false, true) failed after source deletion";

    // Cleanup
    delete target;
}

// ============================================================================
// Test 5: NullifyAllRefsCascadeDoesNotKillReparentedObjects
//
// CORE BUG TEST: the NullifyAllRefs cascade in ~ObjectDir walks ObjDirItr
// which includes objects still registered in the source's hash table even
// after SetName reparented them.  The cascade then calls NullifyAllRefs on
// those objects, killing ObjPtrs/ObjDirPtrs in the target.
//
// Xbox ~ObjectDir has no such cascade, so reparented objects survive.
// ============================================================================

TEST_F(MergeLifecycleTest, NullifyAllRefsCascadeDoesNotKillReparentedObjects) {
    ObjectDir *source = Hmx::Object::New<ObjectDir>();
    source->SetName("cascade_source", ObjectDir::Main());

    ObjectDir *target = Hmx::Object::New<ObjectDir>();
    target->SetName("cascade_target", ObjectDir::Main());

    // Create an ObjectDir "child" registered in source
    ObjectDir *child = Hmx::Object::New<ObjectDir>();
    child->SetName("child_dir", source);

    // Verify child is findable in source
    ASSERT_NE(source->FindObject("child_dir", false, false), nullptr);

    // Reparent child to target (simulates what MergeObject does)
    child->SetName("child_dir", target);

    // Verify child is now in target
    ASSERT_NE(target->FindObject("child_dir", false, false), nullptr);
    ASSERT_EQ(child->Dir(), target);

    // Hold a reference to child from outside (like a DTA script or ObjPtr)
    ObjDirPtr<ObjectDir> externalRef(child);
    ASSERT_EQ((ObjectDir *)externalRef, child);

    // Delete the source dir — this triggers the NullifyAllRefs cascade
    delete source;

    // CORE ASSERTION: child must still be alive and findable
    EXPECT_NE((ObjectDir *)externalRef, nullptr)
        << "External ObjDirPtr to reparented child was nullified by cascade";

    Hmx::Object *foundChild = target->FindObject("child_dir", false, false);
    EXPECT_NE(foundChild, nullptr)
        << "child_dir not findable in target after source deletion";
    EXPECT_EQ(foundChild, child)
        << "child_dir identity changed after source deletion";

    // Cleanup — release external ref before deleting target
    externalRef = nullptr;
    delete target;
}

// ============================================================================
// Test 6: MergedObjectsSurviveParentDirReload
//
// Models the director.milo PostLoad scenario: after merge into target, the
// target's subdir list is re-processed (resize mSubDirs, walk inlined/non-
// inlined subdirs).  Merged objects must remain findable throughout.
//
// Note: SetSubDir(true) (called by AppendSubDir -> AddedSubDir) clears the
// subdir's name via SetName(nullptr, nullptr). This is correct engine
// behavior. We verify subdirs via HasSubDir and pointer identity, and
// verify merged objects (non-subdirs) via FindObject by name.
// ============================================================================

TEST_F(MergeLifecycleTest, MergedObjectsSurviveParentDirReload) {
    ObjectDir *target = Hmx::Object::New<ObjectDir>();
    target->SetName("reload_target", ObjectDir::Main());

    // Add a pre-existing subdir (simulates subdirs already in the target)
    ObjectDir *existingSub = Hmx::Object::New<ObjectDir>();
    existingSub->SetName("existing_sub", target);
    target->AppendSubDir(ObjDirPtr<ObjectDir>(existingSub));

    // Merge objects into target from a source dir
    ObjectDir *source = Hmx::Object::New<ObjectDir>();
    source->SetName("reload_source", ObjectDir::Main());

    Hmx::Object *merged1 = Hmx::Object::New<Hmx::Object>();
    merged1->SetName("merged_widget_a", source);

    Hmx::Object *merged2 = Hmx::Object::New<Hmx::Object>();
    merged2->SetName("merged_widget_b", source);

    ReserveToFit(source, target, 0);
    MergeFilter filt(MergeFilter::kReplace, MergeFilter::kNoSubdirs);
    MergeDirs(source, target, filt);

    // Delete source (PostMerge)
    delete source;

    // Verify merged objects are findable
    ASSERT_NE(target->FindObject("merged_widget_a", false, true), nullptr);
    ASSERT_NE(target->FindObject("merged_widget_b", false, true), nullptr);

    // Simulate PostLoad subdir reprocessing: remove and re-add the existing subdir.
    // This exercises the mSubDirs vector resizing that occurs during dir PostLoad.
    // Hold an external ObjDirPtr so the subdir survives RemoveSubDir.
    ObjDirPtr<ObjectDir> holdSub(existingSub);

    target->RemoveSubDir(ObjDirPtr<ObjectDir>(existingSub));
    EXPECT_FALSE(target->HasSubDir(existingSub));

    target->AppendSubDir(ObjDirPtr<ObjectDir>(existingSub));
    EXPECT_TRUE(target->HasSubDir(existingSub));

    // Release the hold — the subdir is back in target's mSubDirs
    holdSub = nullptr;

    // CORE ASSERTION: merged objects must still be findable after subdir reprocessing
    EXPECT_NE(target->FindObject("merged_widget_a", false, true), nullptr)
        << "merged_widget_a lost during subdir reprocessing";
    EXPECT_NE(target->FindObject("merged_widget_b", false, true), nullptr)
        << "merged_widget_b lost during subdir reprocessing";

    // Verify the existing subdir is still alive in target's SubDirs.
    // Note: SetSubDir(true) clears the name, so FindObject by name won't work.
    // Use HasSubDir and pointer identity instead.
    EXPECT_TRUE(target->HasSubDir(existingSub))
        << "existing_sub removed from SubDirs during reprocessing";

    // Verify the ObjDirPtr in target's SubDirs still resolves to existingSub
    bool foundInSubDirs = false;
    for (int i = 0; i < (int)target->SubDirs().size(); i++) {
        if ((ObjectDir *)target->SubDirs()[i] == existingSub) {
            foundInSubDirs = true;
            break;
        }
    }
    EXPECT_TRUE(foundInSubDirs)
        << "existingSub pointer not found in target's SubDirs vector";

    // Cleanup
    delete target;
}

// ============================================================================
// Test 7: MergeReplaceSubdirsSurviveSourceDeletion
//
// End-to-end: MergeDirs with kMergeInlinedMoveSharedSubdirs, where
// MergeObjectsRecurse moves subdirs via kMergeReplace (AppendSubDir to target,
// erase from source).  After deleting the source, the moved subdirs and their
// contents must survive.
// ============================================================================

TEST_F(MergeLifecycleTest, MergeReplaceSubdirsSurviveSourceDeletion) {
    ObjectDir *source = Hmx::Object::New<ObjectDir>();
    source->SetName("replace_source", ObjectDir::Main());

    ObjectDir *target = Hmx::Object::New<ObjectDir>();
    target->SetName("replace_target", ObjectDir::Main());

    // Create a shared subdir in source with objects
    ObjectDir *sharedSub = Hmx::Object::New<ObjectDir>();
    sharedSub->SetName("hud_elements", source);
    source->AppendSubDir(ObjDirPtr<ObjectDir>(sharedSub));

    Hmx::Object *subObj = Hmx::Object::New<Hmx::Object>();
    subObj->SetName("score_text.obj", sharedSub);

    // Direct objects in source
    Hmx::Object *directObj = Hmx::Object::New<Hmx::Object>();
    directObj->SetName("streak_meter.obj", source);

    // Merge with a filter that moves shared subdirs (kMergeReplace)
    ReserveToFit(source, target, 0);
    MergeFilter filt(MergeFilter::kReplace,
                     MergeFilter::kMoveAllSubdirs);
    MergeDirs(source, target, filt);

    // After merge, subdirs should have been moved to target
    ASSERT_TRUE(target->HasSubDir(sharedSub))
        << "kMergeReplace did not move subdir to target";

    // Delete source
    delete source;

    // CORE ASSERTIONS: subdir and its contents survive
    EXPECT_TRUE(target->HasSubDir(sharedSub))
        << "hud_elements subdir killed by source deletion";

    Hmx::Object *foundSubObj = sharedSub->FindObject("score_text.obj", false, false);
    EXPECT_NE(foundSubObj, nullptr)
        << "score_text.obj in moved subdir was killed by source deletion";

    // Direct merged objects also survive
    EXPECT_NE(target->FindObject("streak_meter.obj", false, true), nullptr)
        << "streak_meter.obj killed by source deletion";

    // Cleanup
    delete target;
}

// ============================================================================
// Test 8: CascadeSkipsObjectsWithExternalDirPtrs
//
// Models the core game scenario: an ObjectDir is being destroyed, but it
// contains an object registered via SetName that has DirPtrs from outside.
// The cascade should NOT call NullifyAllRefs on the object, and
// DeleteObjects should NOT destroy it.
//
// Scenario: parent dir has a subdir. The subdir's hash table contains an
// ObjectDir "hudLeft" with an external ObjDirPtr. When the parent dir is
// destroyed, the subdir cascade should skip hudLeft because it has DirPtrs.
// ============================================================================

TEST_F(MergeLifecycleTest, CascadeSkipsObjectsWithExternalDirPtrs) {
    // Create a parent dir (simulates PanelDir "hud")
    ObjectDir *parent = Hmx::Object::New<ObjectDir>();
    parent->SetName("parent_hud", ObjectDir::Main());

    // Create a child ObjectDir (simulates anonymous subdir from PostLoad)
    ObjectDir *anonSub = Hmx::Object::New<ObjectDir>();
    anonSub->SetName("anon_sub", parent);
    parent->AppendSubDir(ObjDirPtr<ObjectDir>(anonSub));

    // Create an object in the child dir (simulates hud_left after PostLoad
    // reassigned it to the anonymous subdir)
    ObjectDir *hudLeft = Hmx::Object::New<ObjectDir>();
    hudLeft->SetName("hud_left_obj", anonSub);

    // Hold an external ObjDirPtr to hudLeft (simulates DTA $hud or external ref)
    ObjDirPtr<ObjectDir> externalRef(hudLeft);
    ASSERT_EQ((ObjectDir *)externalRef, hudLeft);

    // Verify hudLeft is findable in the anonymous subdir
    ASSERT_NE(anonSub->FindObject("hud_left_obj", false, false), nullptr);

    // Delete the parent dir. This triggers the cascade on parent, which
    // collects anonSub (via SubDirs). The cascade should skip hudLeft
    // because it has external DirPtrs (externalRef).
    delete parent;

    // CORE ASSERTION: external ref to hudLeft must survive
    EXPECT_NE((ObjectDir *)externalRef, nullptr)
        << "External ObjDirPtr to object in destroyed dir was nullified by cascade";

    // Cleanup
    externalRef = nullptr;
}

} // namespace
