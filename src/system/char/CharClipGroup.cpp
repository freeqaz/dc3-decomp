#include "char\CharClipGroup.h"
#include "CharClipGroup.h"
#include "char\CharClip.h"
#include "obj\ObjPtrVec_impl.h"
#include "math/Rand.h"
#include "math\Utl.h"
#include "obj/Object.h"
#include "os\Debug.h"
#include "rndobj\Mat.h"
#include "utl\Str.h"
#include <cstring>

#ifndef HX_NATIVE
// Explicit template instantiation (STLport only)
namespace stlpmtx_std {
    template class vector<ObjPtrVec<CharClip, ObjectDir>::Node, StlNodeAlloc<ObjPtrVec<CharClip, ObjectDir>::Node>>;
}
#endif

CharClipGroup::CharClipGroup()
    : mClips(this, (EraseMode)1), mWhich(0), unk24(0), mFlags(0) {}

BEGIN_HANDLERS(CharClipGroup)
    HANDLE_EXPR(get_clip, GetClip(0))
    HANDLE_ACTION(delete_remaining, DeleteRemaining(_msg->Int(2)))
    HANDLE_EXPR(get_size, mClips.size())
    HANDLE_EXPR(has_clip, HasClip(_msg->Obj<CharClip>(2)))
    HANDLE_EXPR(find_clip, GetClip(_msg->Int(2)))
    HANDLE_ACTION(add_clip, AddClip(_msg->Obj<CharClip>(2)))
    HANDLE_ACTION(set_clip_flags, SetClipFlags(_msg->Int(2)))
    HANDLE_SUPERCLASS(Hmx::Object)
END_HANDLERS

BEGIN_PROPSYNCS(CharClipGroup)
    SYNC_PROP(clips, mClips)
    SYNC_PROP(flags, mFlags)
    SYNC_SUPERCLASS(Hmx::Object)
END_PROPSYNCS

BEGIN_SAVES(CharClipGroup)
    SAVE_REVS(2, 0)
    SAVE_SUPERCLASS(Hmx::Object)
    bs << mClips;
    bs << mWhich;
    bs << mFlags;
END_SAVES

BEGIN_COPYS(CharClipGroup)
    COPY_SUPERCLASS(Hmx::Object)
    CREATE_COPY(CharClipGroup)
    BEGIN_COPYING_MEMBERS
        if (ty == kCopyFromMax) {
            for (int i = 0; i < c->mClips.size(); i++) {
                CharClip *curClip = (CharClip *)c->mClips[i];
                if (!FindClip(curClip->Name())) {
                    mClips.push_back(ObjOwnerPtr<CharClip>(this, curClip));
                }
            }
        } else
            COPY_MEMBER(mClips)
        COPY_MEMBER(mWhich)
        COPY_MEMBER(mFlags)
    END_COPYING_MEMBERS
END_COPYS

INIT_REVS(2, 0)

BEGIN_LOADS(CharClipGroup)
    LOAD_REVS(bs)
    ASSERT_REVS(2, 0)
    LOAD_SUPERCLASS(Hmx::Object)
    mClips.Load(d.stream, true, nullptr);
    d >> mWhich;
    mWhich = Max(mWhich, 0);
    if (d.rev > 1) {
        d >> mFlags;
    } else {
        mFlags = 0;
    }
END_LOADS

void CharClipGroup::AddClip(CharClip *clip) {
    if (!HasClip(clip)) {
        mClips.push_back(ObjOwnerPtr<CharClip>(this, clip));
    }
}

// RESIDUAL (w8-i, 56.81 canonical / 55.41 fuzzy): the only structural difference is
// WHERE `end() = begin() + size()` is finished.  The image completes it before the
// call and holds the single pointer in r31 across `bl find` (one callee-save,
// __savegprlr_31); we keep `begin` in r31 and `size` in r30 and sink the
// `mulli`/`add` past the call (two callee-saves).  Every one of the 23 charged rows
// is downstream of that one scheduling choice.  Measured here, all at 56.81 canonical:
//   - `const_iterator e = mClips.end(); return e != mClips.find(clip);`  56.81 / 55.41
//   - `return mClips.find(clip) != mClips.end();` (operand swap)          56.81 / 55.19
// Binding end() to a named local does not materialise it -- the value is a bare
// pointer, so MSVC re-sinks the arithmetic regardless of statement boundaries.
bool CharClipGroup::HasClip(CharClip *clip) const {
    return mClips.end() != mClips.find(clip);
}

int CharClipGroup::QueueRandom(int pos, int end) const {
    int diff = end - pos;
    int range = (diff < 0 ? mClips.size() : 0) + diff;
    int result = Rand::sRand.FastInt(0, range) + pos;
    int size = mClips.size();
    return result - ((result >= size) ? size : 0);
}

// w19-c: ClampTo is OUR name -- the image only shows that both clamps below
// went through an inlined by-reference helper: each stores its Min()
// unconditionally (`blt`/`bge` over one `mr`, then the `stw`) and the member is
// RELOADED afterwards instead of forwarded.  Written as plain
// `mWhich = Min(...)` MSVC forwards the value into a callee-saved register and
// renumbers r24-r31 (93.2); through the reference it does not (100).
static inline void ClampTo(int &x, int max) { x = Min(x, max); }

// w19-c (94.87 -> 100): three changes, each measured.  (1) the two scans are
// plain `while` loops, not hand-rotated `if (c) do {} while (c)` (-> 96.1, the
// r30/r31 `this`/`pos` swap is a rotation artefact); (2) the clamps go through
// ClampTo above (-> 97.5); (3) each scan advances through a fresh `next`
// local, wrapped and then assigned back, which is the image's xoris-before-subf
// order for the branchless wrap (-> 100); `pos++; pos -= ...` schedules the
// subf first.
CharClip *CharClipGroup::GetClip(int flags) {
    if (!mClips.size()) {
        return nullptr;
    }

    ClampTo(mWhich, (int)mClips.size() - 1);
    ClampTo(unk24, (int)mClips.size() - 1);

    int origWhich = mWhich;

    int pos = mWhich + 1;
    pos -= (pos >= mClips.size()) ? mClips.size() : 0;
    mWhich = pos;

    while (pos != unk24) {
        int swapIdx = QueueRandom(pos, unk24);
        mClips.swap(pos, swapIdx);
        CharClip *clip = mClips[pos];
        if ((clip->Flags() & flags) == flags) {
            mClips.swap(pos, mWhich);
            return clip;
        }
        int next = pos + 1;
        next -= (next >= mClips.size()) ? mClips.size() : 0;
        pos = next;
    }

    CharClip *clip = nullptr;
    while (pos != origWhich) {
        int swapIdx = QueueRandom(pos, origWhich);
        mClips.swap(pos, swapIdx);
        clip = mClips[pos];
        if ((clip->Flags() & flags) == flags) {
            mClips.swap(pos, mWhich);
            mClips.swap(pos, unk24);
            int newUnk24 = unk24 + 1;
            newUnk24 -= (newUnk24 >= mClips.size()) ? mClips.size() : 0;
            unk24 = newUnk24;
            return clip;
        }
        int next = pos + 1;
        next -= (next >= mClips.size()) ? mClips.size() : 0;
        pos = next;
    }

    clip = mClips[pos];
    if ((clip->Flags() & flags) == flags) {
        mClips.swap(pos, mWhich);
        mClips.swap(pos, unk24);
        int newUnk24 = unk24 + 1;
        newUnk24 -= (newUnk24 >= mClips.size()) ? mClips.size() : 0;
        unk24 = newUnk24;
        return clip;
    }

    return nullptr;
}

struct Alphabetically {
    bool operator()(Hmx::Object *c1, Hmx::Object *c2) const {
        return strcmp(c1->Name(), c2->Name()) < 0;
    }
};

void CharClipGroup::Sort() { mClips.sort(Alphabetically()); }

void CharClipGroup::DeleteRemaining(int i1) {
    CharClip *clips[256];
    MILO_ASSERT(mClips.size() < 256, 0x88);
    for (int i = 0; i < mClips.size(); i++) {
        clips[i] = mClips[i];
    }
    CharClip::LockAndDelete(clips, mClips.size(), i1);
}

CharClip *CharClipGroup::FindClip(const char *clipName) const {
    for (int i = 0; i < mClips.size(); i++) {
        if (streq(clipName, mClips[i]->Name())) {
            return (CharClip *)mClips[i];
        }
    }
    return nullptr;
}

void CharClipGroup::SetClipFlags(int flags) {
    for (int i = 0; i < mClips.size(); i++) {
        CharClip *cur = mClips[i];
        cur->SetFlags(cur->Flags() | flags);
    }
}

// Explicit instantiation of ObjPtr_p.h's generic operator<<, not a
// specialisation of it.  This used to be a hand-written body under a
// #line 391 "...\\obj\\ObjPtr_p.h" directive -- the #line got the __FILE__ the
// image carries, but the body was not the image's body (59.3%).  Instantiating
// the real template gets both, and the #line goes away with it.  The
// instantiation is needed because nothing in this TU streams an
// ObjPtrVec<RndMat> and MSVC emits no COMDAT for an uninstantiated template.
template BinStream &
operator<< <RndMat>(BinStream &, const ObjPtrVec<RndMat, ObjectDir> &);
