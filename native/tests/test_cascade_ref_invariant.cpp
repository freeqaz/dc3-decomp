// Cascade reference-nullification invariant tests.
//
// The three-phase ObjectDir::DeleteObjects scheme (Dir.cpp) rests on ONE
// invariant:
//
//     When a cascade destroys an object, every ObjRef pointing at that
//     object must be nullified before the object's memory is freed.
//
// Phase 0 (NullifyAllRefs) enforces this for objects *reachable from the dir
// being deleted*.  Hmx::Object::~Object deliberately skips ReplaceRefs(nullptr)
// while InDeleteObjects() is true (Object.cpp), so an object destroyed DURING a
// cascade but NOT in the dir's iteration set gets neither path -- its ref ring
// is never walked and every holder is left dangling.
//
// That gap is what produced the TaskMgr::Poll crash (a queued ObjPtr<Task> in
// TheTaskMgr, which is not in any dir being deleted).  Site-specific guards fix
// the symptom one holder at a time; these tests pin the invariant itself.
//
// Each test states the invariant from the *holder's* point of view, so it fails
// deterministically at a named line rather than relying on a crash reproducing.

#include "test_helpers.h"

#include "obj/Dir.h"
#include "obj/Object.h"
#include "obj/Task.h"

namespace {

class CascadeRefInvariantTest : public EngineTestFixture {};

// A holder that lives OUTSIDE any ObjectDir, with a null ObjPtr owner --
// exactly the shape of TheTaskMgr::unk84's `ObjPtr<Task>(nullptr, task)`.
// Nothing about it is reachable from a dir, so Phase 0 will never visit it;
// it can only be nullified through the *referent's* own ring.
class ExternalHolder {
public:
    explicit ExternalHolder(Hmx::Object *obj) : mRef(nullptr, obj) {}
    Hmx::Object *Get() const { return mRef.Ptr(); }

private:
    ObjPtr<Hmx::Object> mRef;
};

// Lives INSIDE the dir under test.  Its destructor deletes an object that is
// NOT in the dir -- the "destroyed as a side effect of the cascade" case.
// Models AnimTask::~AnimTask deleting a blend task, Sequence::~Sequence
// deleting its instruments, HamCharacter::~HamCharacter deleting mWaypoint.
class CascadeKiller : public Hmx::Object {
public:
    explicit CascadeKiller(Hmx::Object *victim) : mVictim(victim) {}
    ~CascadeKiller() override {
        delete mVictim;
        mVictim = nullptr;
    }

private:
    Hmx::Object *mVictim;
};

} // namespace

// ---------------------------------------------------------------------------
// Control: the invariant already holds for objects that ARE in the dir.
// Phase 0's NullifyAllRefs walks their rings, so even a holder outside every
// dir gets nullified.  If this ever fails, Phase 0 itself has regressed.
// ---------------------------------------------------------------------------
TEST_F(CascadeRefInvariantTest, ExternalHolderOfDirResidentIsNullified) {
    ObjectDir *dir = Hmx::Object::New<ObjectDir>();
    dir->SetName("cascade_ctrl_dir", ObjectDir::Main());

    Hmx::Object *resident = Hmx::Object::New<Hmx::Object>();
    resident->SetName("resident", dir);

    ExternalHolder holder(resident);
    ASSERT_EQ(holder.Get(), resident);

    delete dir;

    EXPECT_EQ(holder.Get(), nullptr)
        << "Phase 0 NullifyAllRefs failed to nullify an external ref to a "
           "dir-resident object";
}

// ---------------------------------------------------------------------------
// THE INVARIANT UNDER TEST.
//
// `victim` is destroyed by the cascade (via a dir-resident destructor) but is
// not itself in the dir, so Phase 0 never walks its ring.  Hmx::Object::~Object
// skips ReplaceRefs while InDeleteObjects() is true.  Result: `holder` keeps a
// pointer into a block that Phase 2 hands back to free().
//
// This is the TaskMgr::Poll crash reduced to a single deterministic assertion.
// ---------------------------------------------------------------------------
TEST_F(CascadeRefInvariantTest, ExternalHolderOfCascadeCollateralIsNullified) {
    ObjectDir *dir = Hmx::Object::New<ObjectDir>();
    dir->SetName("cascade_collateral_dir", ObjectDir::Main());

    // Deliberately NOT named into `dir` -- invisible to Phase 0's iteration.
    Hmx::Object *victim = Hmx::Object::New<Hmx::Object>();

    ExternalHolder holder(victim);
    ASSERT_EQ(holder.Get(), victim);

    CascadeKiller *killer = new CascadeKiller(victim);
    killer->SetName("killer", dir);

    delete dir; // cascade: killer's dtor deletes victim

    EXPECT_EQ(holder.Get(), nullptr)
        << "DANGLING: an object destroyed during a cascade left a live ObjPtr "
           "pointing into freed memory. Phase 0 does not see it (not in the "
           "dir) and ~Object skips ReplaceRefs during a cascade.";
}

// ---------------------------------------------------------------------------
// Same invariant, stated through the real reporter: TheTaskMgr's delete queue.
//
// A task is queued for deletion BEFORE the cascade starts (QueueTaskDelete
// already refuses to enqueue during one), then destroyed by the cascade.  The
// queued ObjPtr<Task> must come back null; if it does not, the next
// TaskMgr::Poll would `delete` a freed block through a recycled vptr.
// ---------------------------------------------------------------------------
TEST_F(CascadeRefInvariantTest, QueuedTaskDestroyedByCascadeIsNullified) {
    // Drain anything already queued so we observe only our own entry.
    TheTaskMgr.Poll();

    ObjectDir *dir = Hmx::Object::New<ObjectDir>();
    dir->SetName("cascade_task_dir", ObjectDir::Main());

    Hmx::Object *target = Hmx::Object::New<Hmx::Object>();
    target->SetName("msg_target", dir);

    DataArray *msg = new DataArray(1);
    MessageTask *task = new MessageTask(target, msg);
    msg->Release();

    // Queued while alive and outside any cascade -- the exact precondition the
    // TaskMgr::Poll guard says it cannot defend against at enqueue time.
    TheTaskMgr.QueueTaskDelete(task);

    int skippedBefore = TaskMgr::DanglingQueuedTasksSkipped();

    delete dir; // cascade destroys `target`, which destroys `task`

    // Poll drains the queue. With the invariant held, the entry is already
    // null and nothing needs the dangling-task guard.
    TheTaskMgr.Poll();

    EXPECT_EQ(TaskMgr::DanglingQueuedTasksSkipped(), skippedBefore)
        << "TaskMgr::Poll had to fall back on its dangling-task guard, which "
           "means the cascade left a dangling ObjPtr<Task> in the delete "
           "queue. The guard masks the symptom; the invariant is violated.";
}
