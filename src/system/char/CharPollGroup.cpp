#include "char/CharPollGroup.h"
#include "char/CharPollable.h"
#include "char/CharWeightable.h"
#include "obj/Object.h"
#include <algorithm>

CharPollGroup::CharPollGroup() : mPolls(this), mChangedBy(this), mChanges(this) {}

CharPollGroup::~CharPollGroup() {}

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
            for (ObjPtrList<CharPollable>::iterator it = c->mPolls.begin();
                 it != c->mPolls.end();
                 ++it) {
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
    Hmx::Object::Load(bs);
    if (d.rev > 2)
        CharWeightable::Load(bs);
    bs >> mPolls;
    if (d.rev > 1) {
        bs >> mChangedBy;
        bs >> mChanges;
    }
END_LOADS

void CharPollGroup::Poll() {
    if (Weight() != 0.0f) {
        for (ObjPtrList<CharPollable>::iterator it = mPolls.begin(); it != mPolls.end();
             ++it) {
            (*it)->Poll();
        }
    }
}

void CharPollGroup::Enter() {
    for (ObjPtrList<CharPollable>::iterator it = mPolls.begin(); it != mPolls.end();
         ++it) {
        (*it)->Enter();
    }
}

void CharPollGroup::Exit() {
    for (ObjPtrList<CharPollable>::iterator it = mPolls.begin(); it != mPolls.end();
         ++it) {
        (*it)->Exit();
    }
}

void CharPollGroup::ListPollChildren(std::list<RndPollable *> &l) const {
    ObjPtrList<CharPollable>::iterator it = mPolls.begin();
    ObjPtrList<CharPollable>::iterator itEnd = mPolls.end();
    for (; it != itEnd; ++it) {
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
        for (ObjPtrList<CharPollable>::iterator it = mPolls.begin(); it != mPolls.end();
             ++it) {
            (*it)->PollDeps(changedBy, change);
        }
    }
}

void CharPollGroup::SortPolls() {
    CharPollableSorter sorter;
    std::vector<RndPollable *> polls;
    polls.reserve(mPolls.size());
    for (ObjPtrList<CharPollable>::iterator it = mPolls.begin(); it != mPolls.end();
         ++it) {
        polls.push_back(*it);
    }
    sorter.Sort(polls);
    mPolls.clear();
    for (int i = 0; i < polls.size(); i++) {
        mPolls.push_back(dynamic_cast<CharPollable *>(polls[i]));
    }
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
        Hmx::Object *obj = *it;
        if (!obj)
            continue;
        std::map<Hmx::Object *, Dep>::iterator found = mDeps.find(obj);
        if (found != mDeps.end()) {
            Dep *other = &found->second;
            if (isChangedBy) {
                dep->changedBy.push_back(other);
            }
        }
    }
}

bool CharPollableSorter::ChangedByRecurse(Dep *dep) {
    if (dep->searchID == sSearchID)
        return false;
    dep->searchID = sSearchID;
    for (std::list<Dep *>::iterator it = dep->changedBy.begin();
         it != dep->changedBy.end();
         ++it) {
        if (*it == mTarget)
            return true;
        if (ChangedByRecurse(*it))
            return true;
    }
    return false;
}

bool CharPollableSorter::ChangedBy(Dep *a, Dep *b) {
    mTarget = b;
    sSearchID++;
    return ChangedByRecurse(a);
}

void CharPollableSorter::Sort(std::vector<RndPollable *> &polls) {
    // Build dependency map for all CharPollables
    for (int i = 0; i < (int)polls.size(); i++) {
        CharPollable *cp = dynamic_cast<CharPollable *>(polls[i]);
        if (cp) {
            Dep &dep = mDeps[cp];
            dep.obj = cp;
            dep.poll = polls[i];
            dep.searchID = 0;
        }
    }

    // Build changedBy lists
    for (std::map<Hmx::Object *, Dep>::iterator it = mDeps.begin(); it != mDeps.end();
         ++it) {
        CharPollable *cp = dynamic_cast<CharPollable *>(it->first);
        if (!cp)
            continue;
        std::list<Hmx::Object *> changedBy, change;
        cp->PollDeps(changedBy, change);
        AddDeps(&it->second, changedBy, it->second.changedBy, true);
    }

    // Topological sort: sort so that if A changes B, B comes after A
    std::vector<Dep *> sorted;
    sorted.reserve(mDeps.size());
    for (std::map<Hmx::Object *, Dep>::iterator it = mDeps.begin(); it != mDeps.end();
         ++it) {
        sorted.push_back(&it->second);
    }

    // Bubble sort based on dependency
    for (int i = 0; i < (int)sorted.size(); i++) {
        for (int j = i + 1; j < (int)sorted.size(); j++) {
            if (ChangedBy(sorted[i], sorted[j])) {
                // sorted[i] is changed by sorted[j], so sorted[j] must come first
                std::swap(sorted[i], sorted[j]);
            }
        }
    }

    // Rebuild polls vector
    polls.clear();
    for (int i = 0; i < (int)sorted.size(); i++) {
        if (sorted[i]->poll)
            polls.push_back(sorted[i]->poll);
    }
}

BEGIN_HANDLERS(CharPollGroup)
    HANDLE_ACTION(sort_polls, SortPolls())
    HANDLE_SUPERCLASS(Hmx::Object)
END_HANDLERS
