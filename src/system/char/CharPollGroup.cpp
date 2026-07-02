#include "char/CharPollGroup.h"
#include "char/CharPollable.h"
#include "char/CharWeightable.h"
#include "obj/Object.h"
#include "rndobj/Trans.h"
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

bool CharPollableSorter::ChangedBy(Dep *a, Dep *b) {
#ifdef HX_NATIVE
    // Poll-order experiment (DC3_POLL_ORDER_FIX=1, 2026-07-02 feet-in-floor
    // faithful root): producer-first polarity makes the per-character order
    // song.hdrv (buffer) -> bone.servo (pose meshes) -> IK effectors — the
    // order Xbox's rendered pose requires. With the default polarity below the
    // order comes out reversed (effectors -> servo -> driver), so the
    // HamIKEffector pelvis retarget lift (31.7 -> 38.9 = the exact Xbox pelvis
    // height) is computed and then stomped by the servo's PoseMeshes in the
    // same frame (DC3_SEQ trace, docs/sessions/2026-07-02). RB3's matched
    // sorter uses producer-first (rb3 Character.cpp ChangedBy: mTarget=d1,
    // recurse(d2)); DC3's Sort byte-matches with the polarity below, so the
    // PPC-facing branch stays untouched. OPT-IN until the ankle IK solve is
    // stabilized: with the fixed order the effectors' output SURVIVES, which
    // exposes the native CharIKHand divergence (feet fling to +/-300 — see
    // Push 12/13, docs/sessions/2026-06-09-xenia-xbox-foot-truth.md).
    static int sOrderFix = -1;
    if (sOrderFix < 0)
        sOrderFix = getenv("DC3_POLL_ORDER_FIX") ? 1 : 0;
    if (sOrderFix) {
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
