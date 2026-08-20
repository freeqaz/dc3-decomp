// Cascade reference-nullification invariant tests.
//
// The three-phase ObjectDir::DeleteObjects scheme (Dir.cpp) rests on ONE
// invariant:
//
//     When a cascade destroys an object, every ObjRef pointing at that
//     object must be nullified before the object's memory is freed.
//
// Phase 0 (NullifyAllRefs) enforces this only for objects *reachable from the
// dir being deleted*.  ~Object used to simply SKIP ref cleanup while
// InDeleteObjects() was true, so an object destroyed DURING a cascade but not
// in the dir's iteration set got neither path -- its ring was never walked and
// every holder was left dangling.  That gap produced the TaskMgr::Poll crash
// (a queued ObjPtr<Task> in TheTaskMgr, which is in no dir).
//
// Fixed 2026-08-20: ~Object now calls NullifyAllRefs() during a cascade instead
// of skipping.  Because the ring belongs to the REFERENT, that covers every
// ObjPtr / ObjPtrList / ObjPtrVec / ObjOwnerPtr / ObjDirPtr holder regardless
// of where the holder lives.  These tests are the regression gate on that.
//
// NOT covered, by construction: raw `Hmx::Object*` holders. They register no
// ObjRef, so no ring walk can ever reach them.  gDataVars, TheHamUI's panel
// pointers and the `static UIPanel* = Find<>(...)` caches are that separate
// class -- see docs/analysis/2026-08-20-objdir-cascade-class.md.
//
// Each test states the invariant from the *holder's* point of view, so a
// regression fails deterministically at a named line rather than as an
// intermittent SIGSEGV that a suite cannot gate on.

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

// ---------------------------------------------------------------------------
// Adversarial: Task::IsLive() is address-keyed, so it is ABA-unsound.
//
// LiveTasks() is an unordered_set<Task*> populated in Task::Task and erased in
// Task::~Task. It answers "is a Task alive at this ADDRESS", not "is THIS task
// alive".  Free a Task and allocate another of the same size and the allocator
// hands back the same block -- at which point IsLive(stalePointer) is true
// again and TaskMgr::Poll's guard waves the stale entry through to
// `delete unk84[i].Ptr()`, destroying a live, in-use Task.
//
// This is not currently reachable: with ~Object nullifying ref rings during a
// cascade, no dangling pointer survives to reach Poll(). The guard is defence
// in depth and this test records that its predicate cannot carry the load on
// its own -- so nobody generalises the pattern to a new site believing it can.
//
// Skips rather than fails if the allocator does not reuse the block; the claim
// is "reuse defeats the predicate", not "reuse always happens".
// ---------------------------------------------------------------------------
namespace {
class ProbeTask : public Task {
public:
    void Poll(float) override {}
};
} // namespace

TEST_F(CascadeRefInvariantTest, IsLiveIsAddressKeyedAndThereforeABAUnsound) {
    Task *first = new ProbeTask();
    Task *staleAddress = first;
    ASSERT_TRUE(Task::IsLive(first));

    delete first;
    ASSERT_FALSE(Task::IsLive(staleAddress))
        << "erase-on-destroy is the whole basis of the predicate";

    Task *second = new ProbeTask();
    if (second != staleAddress) {
        delete second;
        GTEST_SKIP() << "allocator did not reuse the block; ABA not observable "
                        "in this run (the unsoundness is unchanged)";
    }

    // Same address, different object. The predicate cannot tell them apart.
    EXPECT_TRUE(Task::IsLive(staleAddress));
    EXPECT_EQ(second, staleAddress)
        << "ABA CONFIRMED: a stale Task* now reports live because a new Task "
           "was allocated at the same address. TaskMgr::Poll's guard would "
           "delete the NEW task through the OLD pointer. Any future use of "
           "this predicate as a liveness oracle needs a generation counter, "
           "not an address set.";

    delete second;
}

// ---------------------------------------------------------------------------
// QueueTaskDelete must refuse a Task that has already been destroyed.
//
// This is the second, independent defect this lane found, and it is the one
// that actually produced the reported crash. Instrumented boot->gameplay runs
// showed QueueTaskDelete being handed an already-destroyed Task at cascade
// depth 0 -- no cascade running at all:
//
//   QueueTaskDelete on a task whose mRefs sentinel is already dead:
//       0x555bcb67c4c0 (depth=0)
//   TaskMgr::Poll dangling entry: queued=0x555bcb67c4c0
//
// Constructing ObjPtr<Task>(nullptr, task) on a dead object calls AddRef on it,
// splicing a live ObjRef into a ring nobody will ever walk again. Nothing can
// nullify it afterwards -- not Phase 0, not ~Object, not any ring walk. The
// only place it can be stopped is at the door.
//
// The test deliberately passes a pointer to freed memory, because that is
// precisely the contract violation being defended against.
// ---------------------------------------------------------------------------
TEST_F(CascadeRefInvariantTest, QueueTaskDeleteRefusesAnAlreadyDestroyedTask) {
    TheTaskMgr.Poll(); // drain

    Task *task = new ProbeTask();
    ASSERT_TRUE(Task::IsLive(task));

    delete task;
    ASSERT_FALSE(Task::IsLive(task));

    const int refusedBefore = TaskMgr::DeadTasksRefused();
    const int queuedBefore = TheTaskMgr.QueuedDeleteCountForTest();

    TheTaskMgr.QueueTaskDelete(task); // stale on purpose

    EXPECT_EQ(TaskMgr::DeadTasksRefused(), refusedBefore + 1)
        << "QueueTaskDelete accepted a destroyed Task. The resulting ObjPtr is "
           "unreachable by every nullification path in the engine.";
    EXPECT_EQ(TheTaskMgr.QueuedDeleteCountForTest(), queuedBefore)
        << "the dead task must not have been enqueued";

    // And the drain must not have to fall back on its own guard.
    const int skippedBefore = TaskMgr::DanglingQueuedTasksSkipped();
    TheTaskMgr.Poll();
    EXPECT_EQ(TaskMgr::DanglingQueuedTasksSkipped(), skippedBefore)
        << "Poll's dangling-task guard fired, meaning something dangling still "
           "reached the queue despite the entry-point check.";
}
