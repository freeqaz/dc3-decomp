#include "char\CharPollGroup.h"
#include "char\CharPollable.h"
#include "char\CharWeightable.h"
#include "obj/Object.h"
#include "rndobj\Trans.h"
#include <algorithm>
#ifdef HX_NATIVE
#include <cstdio>
#include <cstring>
#endif

CharPollGroup::CharPollGroup() : mPolls(this), mChangedBy(this), mChanges(this) {}

CharPollGroup::~CharPollGroup() {}

BEGIN_HANDLERS(CharPollGroup)
    HANDLE_ACTION(sort_polls, SortPolls())
    HANDLE_SUPERCLASS(Hmx::Object)
END_HANDLERS

BEGIN_PROPSYNCS(CharPollGroup)
    SYNC_PROP(polls, mPolls)
    SYNC_PROP(changed_by, mChangedBy)
    SYNC_PROP(changes, mChanges)
    SYNC_SUPERCLASS(CharWeightable)
    SYNC_SUPERCLASS(Hmx::Object)
END_PROPSYNCS

BEGIN_SAVES(CharPollGroup)
    SAVE_REVS(3, 0)
    SAVE_SUPERCLASS(Hmx::Object)
    SAVE_SUPERCLASS(CharWeightable)
    bs << mPolls;
    bs << mChangedBy;
    bs << mChanges;
END_SAVES

BEGIN_COPYS(CharPollGroup)
    COPY_SUPERCLASS(Hmx : Object)
    COPY_SUPERCLASS(CharWeightable)
    CREATE_COPY(CharPollGroup)
    BEGIN_COPYING_MEMBERS
        if (ty == kCopyFromMax) {
            FOREACH (it, c->mPolls) {
                if (!mPolls.find(*it)) {
                    mPolls.push_back(*it);
                }
            }
        } else {
            COPY_MEMBER(mPolls)
            COPY_MEMBER(mChangedBy)
            COPY_MEMBER(mChanges)
        }
    END_COPYING_MEMBERS
END_COPYS

INIT_REVS(3, 0)

BEGIN_LOADS(CharPollGroup)
    LOAD_REVS(bs);
    ASSERT_REVS(3, 0);
    LOAD_SUPERCLASS(Hmx::Object)
    if (d.rev > 2) {
        LOAD_SUPERCLASS(CharWeightable)
    }
    d >> mPolls;
    if (d.rev > 1) {
        d >> mChangedBy;
        d >> mChanges;
    }
END_LOADS

void CharPollGroup::Poll() {
    if (Weight()) {
        FOREACH (it, mPolls) {
            (*it)->Poll();
        }
    }
}

void CharPollGroup::Enter() {
    FOREACH (it, mPolls) {
        (*it)->Enter();
    }
}

void CharPollGroup::Exit() {
    FOREACH (it, mPolls) {
        (*it)->Exit();
    }
}

void CharPollGroup::ListPollChildren(std::list<RndPollable *> &l) const {
    FOREACH (it, mPolls) {
        l.push_back(*it);
    }
}

void CharPollGroup::PollDeps(
    std::list<Hmx::Object *> &changedBy, std::list<Hmx::Object *> &change
) {
    if (mChangedBy || mChanges) {
        changedBy.push_back(mChangedBy);
        change.push_back(mChanges);
    } else {
        FOREACH (it, mPolls) {
            (*it)->PollDeps(changedBy, change);
        }
    }
}

void CharPollGroup::SortPolls() {
    CharPollableSorter sorter;
    std::vector<RndPollable *> polls;
    polls.reserve(mPolls.size());
    FOREACH (it, mPolls) {
        polls.push_back(*it);
    }
    sorter.Sort(polls);
    mPolls.clear();
    // RESIDUAL (w7-ak, 89.03 canonical): the whole 29-row residual is one MSVC
    // decision. The target RE-LOADS polls._M_start / _M_finish on every iteration
    // (`lwz r4, 0x60(r31)` + `lwz r11, 0x64(r31)` inside the loop) and indexes with
    // `lwzx r3, r30, r4` off a byte-offset induction variable, keeping i and i*4 as
    // two induction variables; our build proves the base loop-invariant, hoists it
    // into r23, caches the element count in r25 and strength-reduces to `lwzu r3,
    // 0x4(r29)`. That costs two extra callee-saved GPRs (`__savegprlr_23` vs `_25`)
    // and +0x10 of frame. NEGATIVE RESULT: binding the element to a reference
    // (`RndPollable *&poll = polls[i];`), which would explain the target's dead
    // `stw &polls[i], 0x54(r31)`, is inert to the digit -- MSVC elides the reference
    // while it can still strength-reduce, so the reference is a consequence of the
    // missing hoist, not its cause.
    // ADDENDUM (w7-bl, still 89.03): the target listing at 0x823A8B10-0x823A8B60
    // now reads completely, and it closes off the two remaining source-shaped
    // explanations. (1) The dead `stw r11, 0x54(r31)` is the HOMED reference that
    // `vector::operator[]` returns -- 0x54 is the very slot the FIRST loop uses for
    // `vector::push_back(const RndPollable*&)`'s temp (0x823A8AA4), reused -- so it
    // is emitted by the inliner, not by a named local, which is why w7-ak's explicit
    // reference could not conjure it. Spelling the element access as a materialised
    // iterator (`std::vector<RndPollable*>::iterator it = polls.begin() + i;` then
    // `*it`) is EXACTLY INERT: same 29 rows, same registers, same 89.03.
    // (2) The call in the loop is `ObjPtrList<CharPollable>::insert(iterator, T*)`
    // returning an iterator by value into 0x58(r31), with the iterator argument a
    // hoisted null (r26, `mr r26, r30` at 0x823A8B04) -- i.e. `insert(end(), x)`.
    // Writing that out (`mPolls.insert(mPolls.end(), dynamic_cast<CharPollable*>(
    // polls[i]))`) is ALSO exactly inert: our `push_back` already lowers to that
    // same `insert` call, and the Function Call Diff confirms `insert` on both sides.
    // What is left is purely that MSVC keeps `polls._M_start` and `polls.size()`
    // live in r23/r25 across the `insert` call where the image re-derives both from
    // 0x60/0x64(r31) at 0x823A8B44-0x823A8B58 and reuses the reloaded `_M_start` as
    // the next iteration's index base. Same compiler, same flags, no source spelling
    // found that makes MSVC decline the CSE; the frame and `__savegprlr_23` vs
    // `_25` follow from it.
    for (int i = 0; i < polls.size(); i++) {
        mPolls.push_back(dynamic_cast<CharPollable *>(polls[i]));
    }
#ifdef HX_NATIVE
    if (getenv("DC3_IK_DIAG")) {
        bool hasIK = false;
        for (int i = 0; i < (int)polls.size(); i++) {
            Hmx::Object *o = dynamic_cast<Hmx::Object *>(polls[i]);
            const char *n = o ? PathName(o) : nullptr;
            if (n && std::strstr(n, "ikfoot")) { hasIK = true; break; }
        }
        static int sSortLog = 0;
        if (hasIK && sSortLog < 4) {
            sSortLog++;
            std::fprintf(stderr, "DC3_IK_DIAG SortOrder[%d] (%d polls):", sSortLog, (int)polls.size());
            for (int i = 0; i < (int)polls.size(); i++) {
                Hmx::Object *o = dynamic_cast<Hmx::Object *>(polls[i]);
                const char *n = o ? PathName(o) : nullptr;
                if (n && (std::strstr(n, "ikfoot") || std::strstr(n, "servo") ||
                          std::strstr(n, "skeleton") || std::strstr(n, "driver")))
                    std::fprintf(stderr, " [%d]%s", i, n);
            }
            std::fprintf(stderr, "\n");
        }
    }
#endif
}

int CharPollableSorter::sSearchID = 0;

void CharPollableSorter::AddDeps(
    Dep *dep,
    const std::list<Hmx::Object *> &objs,
    std::list<Dep *> &deps,
    bool isChangedBy
) {
    for (std::list<Hmx::Object *>::const_iterator it = objs.begin(); it != objs.end();
         ++it) {
        Hmx::Object *cur = *it;
        if (cur) {
            Dep *mapDep = &mDeps[cur];
            if (!mapDep->obj) {
                mapDep->obj = cur;
                deps.push_back(mapDep);
            }
            if (isChangedBy) {
                dep->changedBy.push_back(mapDep);
            } else {
                mapDep->changedBy.push_back(dep);
            }
        }
    }
}

bool CharPollableSorter::ChangedByRecurse(Dep *dep) {
    if (!dep)
        return false;
    if (dep == mTarget)
        return true;
    if (dep->searchID == sSearchID)
        return false;
    dep->searchID = sSearchID;
    for (std::list<Dep *>::iterator it = dep->changedBy.begin();
         it != dep->changedBy.end();
         ++it) {
        if (ChangedByRecurse(*it))
            return true;
    }
    return false;
}

#ifdef HX_NATIVE
// Producer-first poll order (2026-07-02 feet-in-floor faithful root): DEFAULT
// ON native; opt out with DC3_POLL_ORDER_FIX=0. Producer-first polarity makes
// the per-character order song.hdrv (buffer) -> bone.servo (pose meshes) ->
// IK effectors — the order Xbox's rendered pose requires. With the PPC
// polarity below the order comes out reversed, so every effector write
// (pelvis retarget lift, ankle plants) is computed and then stomped by the
// servo's PoseMeshes in the same frame. RB3's matched sorter uses
// producer-first (rb3 Character.cpp ChangedBy: mTarget=d1, recurse(d2));
// DC3's Sort byte-matches the polarity below, so the PPC branch stays
// untouched. Was opt-in while the ankle solve diverged; with the
// HamIKEffector Interp mis-decomp + alias-unsafe Transform Multiply fixed
// (00c9b165) the faithful stack is Xbox-exact and gate-clean 48/48, so it is
// now the default. CharIKFoot.cpp keys its fallback clamps off this too.
bool Dc3PollOrderFixActive() {
    static int v = -1;
    if (v < 0) {
        const char *e = getenv("DC3_POLL_ORDER_FIX");
        v = (e && e[0] == '0') ? 0 : 1;
    }
    return v != 0;
}
#endif

bool CharPollableSorter::ChangedBy(Dep *a, Dep *b) {
#ifdef HX_NATIVE
    if (Dc3PollOrderFixActive()) {
        if (a == b)
            return false;
        sSearchID++;
        mTarget = a;
        return ChangedByRecurse(b);
    }
#endif
    mTarget = b;
    sSearchID++;
    return ChangedByRecurse(a);
}

void CharPollableSorter::Sort(std::vector<RndPollable *> &polls) {
    std::vector<Dep *> deps;
    deps.reserve(polls.size());
    for (int i = polls.size() - 1, last = i; i >= 0; i--) {
        CharPollable *c = dynamic_cast<CharPollable *>(polls[i]);
        if (c) {
            Dep &dep = mDeps[c];
            dep.obj = c;
            dep.poll = c;
            deps.push_back(&dep);
        } else {
            polls[last--] = polls[i];
        }
    }
    if (deps.empty())
        return;
    else {
        std::sort(deps.begin(), deps.end(), CharPollableSorter::AlphaSort());
        std::list<Dep *> depList;
        for (int i = 0; i < deps.size(); i++)
            depList.push_back(deps[i]);
        while (!depList.empty()) {
            Dep *curDep = depList.back();
            depList.pop_back();
            CharPollable *c = dynamic_cast<CharPollable *>(curDep->obj);
            if (c) {
                std::list<Hmx::Object *> depList1;
                std::list<Hmx::Object *> depList2;
                c->PollDeps(depList1, depList2);
                AddDeps(curDep, depList1, depList, true);
                AddDeps(curDep, depList2, depList, false);
            }
            RndTransformable *t = dynamic_cast<RndTransformable *>(curDep->obj);
            if (t) {
                std::list<Hmx::Object *> tDepList;
                tDepList.push_back(t->TransParent());
                AddDeps(curDep, tDepList, depList, true);
            }
        }

        std::list<Dep *> otherDepList;
        for (int i = 0; i < deps.size(); i++) {
            Dep *curDep = deps[i];
            std::list<Dep *>::iterator it = otherDepList.begin();
            for (; it != otherDepList.end(); ++it) {
                if (ChangedBy(curDep, *it))
                    break;
            }
            otherDepList.insert(it, curDep);
        }

        int idx = 0;
        for (std::list<Dep *>::iterator it = otherDepList.begin();
             it != otherDepList.end();
             ++it) {
            polls[idx++] = (*it)->poll;
        }
#ifdef HX_NATIVE
        if (getenv("DC3_IK_DIAG")) {
            bool hasIK = false;
            for (int i = 0; i < (int)polls.size(); i++) {
                Hmx::Object *o = dynamic_cast<Hmx::Object *>(polls[i]);
                const char *n = o ? PathName(o) : nullptr;
                if (n && std::strstr(n, "ikfoot")) { hasIK = true; break; }
            }
            static int sSortLog = 0;
            if (hasIK && sSortLog < 4) {
                sSortLog++;
                std::fprintf(stderr, "DC3_IK_DIAG SortOrder[%d] (%d):", sSortLog, (int)polls.size());
                for (int i = 0; i < (int)polls.size(); i++) {
                    Hmx::Object *o = dynamic_cast<Hmx::Object *>(polls[i]);
                    const char *n = o ? PathName(o) : nullptr;
                    if (n && (std::strstr(n, "ikfoot") || std::strstr(n, "servo") ||
                              std::strstr(n, "skeleton") || std::strstr(n, "driver") ||
                              std::strstr(n, "bone")))
                        std::fprintf(stderr, " [%d]%s", i, n);
                }
                std::fprintf(stderr, "\n");
            }
        }
#endif
    }
}
