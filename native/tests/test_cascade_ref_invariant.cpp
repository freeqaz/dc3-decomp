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
#include "rndobj/Anim.h"
#include "rndobj/Tex.h"
#include <webgpu/webgpu_cpp.h>

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
// The ABA hole, and the predicate that does not have it.
//
// LiveTasks() is keyed on the ADDRESS, because a stale pointer is all a caller
// can offer. The one-argument Task::IsLive() therefore answers "is a Task alive
// at this address", not "is THIS task alive": free a Task, allocate another of
// the same size, and the allocator hands back the same block -- at which point
// IsLive(stalePointer) is true again, and any gate built on it waves the stale
// pointer through onto a DIFFERENT, live task.
//
// The fix is not a second registry (that would inherit the same hole). It is to
// stop keying identity on the address at all: every Task gets a monotonic
// serial at construction, and the ABA-sound predicate compares the serial the
// caller captured while the pointer was known good.
//
// This test keeps the unsoundness of the one-argument form as its own NEGATIVE
// CONTROL -- if the address were not reused, the sound form passing would prove
// nothing -- and skips (rather than passes vacuously) when the allocator
// declines to reuse.
// ---------------------------------------------------------------------------
namespace {
class ProbeTask : public Task {
public:
    void Poll(float) override {}
};
} // namespace

TEST_F(CascadeRefInvariantTest, TaskSerialIsABASoundWhereTheAddressIsNot) {
    Task *first = new ProbeTask();
    Task *staleAddress = first;
    const Task::Serial staleSerial = first->TaskSerial();
    ASSERT_TRUE(Task::IsLive(first));
    ASSERT_TRUE(Task::IsLive(first, staleSerial));

    delete first;
    ASSERT_FALSE(Task::IsLive(staleAddress))
        << "erase-on-destroy is the whole basis of the predicate";
    ASSERT_FALSE(Task::IsLive(staleAddress, staleSerial));

    Task *second = new ProbeTask();
    if (second != staleAddress) {
        delete second;
        GTEST_SKIP() << "allocator did not reuse the block; ABA not observable "
                        "in this run (neither claim is weakened)";
    }

    // NEGATIVE CONTROL: reuse really did happen, and the address-keyed form is
    // fooled by it. Without this assertion the next one is vacuous.
    EXPECT_TRUE(Task::IsLive(staleAddress))
        << "ABA not actually exercised -- the address-keyed predicate should be "
           "reporting the RECYCLED task as live for the stale pointer";
    ASSERT_NE(second->TaskSerial(), staleSerial)
        << "serials must never be reused";

    // THE CLAIM: the serial-keyed form tells the two apart.
    EXPECT_FALSE(Task::IsLive(staleAddress, staleSerial))
        << "ABA: a stale Task* was accepted because a new Task landed at the "
           "same address. A gate built on this would delete/poll the NEW task "
           "through the OLD pointer.";
    EXPECT_TRUE(Task::IsLive(second, second->TaskSerial()))
        << "the sound predicate must still accept a genuinely live task";

    delete second;
}

// ---------------------------------------------------------------------------
// DeathWatch: the mechanism for "the callback I just made destroyed me".
//
// An ObjPtr protects the REFERENT's holders. Nothing protects the `this` of a
// frame already on the stack when a DTA/message callback deletes that object --
// which is what FlowAnimate::OnAnimEvent does to the AnimTask whose Poll() sent
// it the event. DeathWatch is a stack flag ~Object trips.
//
// Unlike an address-keyed liveness registry it compares no addresses, so it has
// no ABA hole at all.
// ---------------------------------------------------------------------------
TEST_F(CascadeRefInvariantTest, DeathWatchNoticesDestructionAndNestsCorrectly) {
    Hmx::Object *obj = Hmx::Object::New<Hmx::Object>();
    {
        Hmx::DeathWatch outer(obj);
        EXPECT_FALSE(outer.Dead());
        {
            Hmx::DeathWatch inner(obj);
            EXPECT_FALSE(inner.Dead());
        }
        // Inner unwound without tripping; the object is still fine and `outer`
        // must still be armed -- i.e. ~DeathWatch restored the chain head.
        EXPECT_FALSE(outer.Dead());
        delete obj;
        EXPECT_TRUE(outer.Dead())
            << "~Object failed to trip an armed DeathWatch -- every guard built "
               "on it is now a silent no-op";
    }
    // Leaving the scope must not write back into the freed block. If ~DeathWatch
    // restored obj->mDeathWatch here, ASAN/poisoning would flag it.
}

TEST_F(CascadeRefInvariantTest, DeathWatchTripsEveryFrameInTheChain) {
    Hmx::Object *obj = Hmx::Object::New<Hmx::Object>();
    Hmx::DeathWatch *outer = new Hmx::DeathWatch(obj);
    Hmx::DeathWatch *inner = new Hmx::DeathWatch(obj);
    delete obj;
    EXPECT_TRUE(inner->Dead());
    EXPECT_TRUE(outer->Dead())
        << "only the innermost watch was tripped; an outer frame would resume "
           "on freed memory";
    delete inner;
    delete outer;
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

// ---------------------------------------------------------------------------
// THE REGRESSION GATE for the ref-loss thread left open on 2026-08-20.
//
// That lane recorded "AnimTask::mBlendTask is stale and we do not know why" as
// its highest-value open item, on the theory that a ref-loss mechanism was
// loose. Instrumentation (DC3_REFRING_AUDIT=1) refuted it: across 12/12
// boot->gameplay runs, ZERO refs were lost from a ring and ZERO Replace calls
// declined -- while QueueTaskDelete was still handed a dead task every time.
//
// The real shape, from the paired backtraces:
//
//     AnimTask::Poll  -> mListener->Handle("looped"/"ended")
//                     -> FlowAnimate::OnAnimEvent
//                     -> delete mAnimTask          <-- deletes the POLLING task
//     ...and AnimTask::Poll then keeps running on the freed block, ending in
//        TheTaskMgr.QueueTaskDelete(this).
//
// `this`, not mBlendTask. A listener, not a cascade. This test reproduces it in
// one deterministic call with no dir, no cascade and no timing.
// ---------------------------------------------------------------------------
namespace {

// RndAnimatable's constructor is protected; the concrete type is irrelevant.
class ProbeAnimatable : public RndAnimatable {
public:
    ProbeAnimatable() {}
};

// The FlowAnimate shape, reduced: a listener whose handler deletes the very
// task that is calling it.
class TaskKillingListener : public Hmx::Object {
public:
    Task *mTask;
    bool mHandled;
    TaskKillingListener() : mTask(nullptr), mHandled(false) {}
    DataNode Handle(DataArray *, bool) override {
        mHandled = true;
        if (mTask) {
            Task *doomed = mTask;
            mTask = nullptr;
            delete doomed; // exactly what FlowAnimate::OnAnimEvent does
        }
        return DataNode(0);
    }
};

} // namespace

TEST_F(CascadeRefInvariantTest, AnimTaskPollSurvivesAListenerThatDeletesIt) {
    TheTaskMgr.Poll(); // drain anything already queued

    ProbeAnimatable *anim = new ProbeAnimatable();
    TaskKillingListener *listener = new TaskKillingListener();

    // Non-looping, no blend, so one Poll well past the end reaches the
    // "ended" notification and then the QueueTaskDelete(this) tail.
    AnimTask *task = new AnimTask(
        anim, 0.0f, 1.0f, 30.0f, false, 0.0f, listener, kEaseLinear, 0.0f, false
    );
    listener->mTask = task;

    const int refusedBefore = TaskMgr::DeadTasksRefused();
    const int abaBefore = Task::AbaFalsePositives();

    task->Poll(100.0f);

    // CONTROLS. Without these the expectation below can pass vacuously -- if
    // the listener never ran, or never deleted anything, there was no bug to
    // survive in the first place.
    ASSERT_TRUE(listener->mHandled)
        << "vacuous: AnimTask::Poll never notified the listener, so the "
           "delete-under-us path was not exercised at all";
    ASSERT_FALSE(Task::IsLive(task))
        << "vacuous: the listener did not actually destroy the task";

    EXPECT_EQ(TaskMgr::DeadTasksRefused(), refusedBefore)
        << "AnimTask::Poll kept running on a destroyed `this` and handed the "
           "corpse to TheTaskMgr.QueueTaskDelete. Every member access between "
           "the listener callback and that call was a use-after-free.";
    EXPECT_EQ(Task::AbaFalsePositives(), abaBefore)
        << "a stale Task pointer reached a gate that only its address could "
           "vouch for";

    delete listener;
    delete anim;
}

// ---------------------------------------------------------------------------
// The RAW-HOLDER class, one confirmed live instance.
//
// The ring only protects ObjPtr-family holders. The native renderer keeps GPU
// resources in a side table keyed on the RndTex ADDRESS (sTexGpuData) -- raw,
// so unreachable by any ring walk, and the destructor is the only hook that can
// keep it honest. RndMesh has called CleanupGpuMesh from ~RndMesh since the
// cache was written. RndTex carried this instead:
//
//     // Note: RndTex destructor doesn't call us directly yet.
//     // For Tier 1, leaked GPU textures are acceptable (cleaned up at shutdown).
//     // TODO: Hook into RndTex destructor or add ref-counting.
//
// The leak is the advertised cost and the lesser one. The real cost is that the
// next RndTex the allocator places at that address inherits the dead entry with
// uploaded=true and renders the PREVIOUS texture's image -- silently, and
// invisible to every metric this project has. This test asserts the outcome,
// not the bookkeeping, and uses address reuse as its own non-vacuity control.
// ---------------------------------------------------------------------------
extern wgpu::TextureView GetGpuTexView(RndTex *tex);

TEST_F(CascadeRefInvariantTest, ARecycledRndTexAddressDoesNotInheritGpuData) {
    RndTex *first = Hmx::Object::New<RndTex>();
    first->SetBitmap(4, 4, 32, RndTex::kRegular, false, nullptr);
    first->PresyncBitmap();

    if (!GetGpuTexView(first)) {
        delete first;
        GTEST_SKIP() << "no GPU entry was created for this RndTex in this "
                        "configuration, so there is nothing to inherit and the "
                        "test would be vacuous";
    }

    RndTex *staleAddress = first;
    delete first;

    RndTex *second = Hmx::Object::New<RndTex>();
    if (second != staleAddress) {
        delete second;
        GTEST_SKIP() << "allocator did not reuse the block; the stale-entry "
                        "hazard is unchanged, just not observable this run";
    }

    EXPECT_FALSE(GetGpuTexView(second))
        << "a brand-new RndTex at a recycled address inherited the destroyed "
           "texture's GPU entry. Everything drawn with it renders the previous "
           "texture's image, and ~RndTex is the only place that can prevent it.";

    delete second;
}
