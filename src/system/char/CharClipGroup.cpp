#include "char/CharClipGroup.h"
#include "CharClipGroup.h"
#include "char/CharClip.h"
#include "math/Rand.h"
#include "math/Utl.h"
#include "obj/Object.h"
#include "os/Debug.h"
#include "utl/Str.h"
#include <cstring>

#ifndef HX_NATIVE
// Explicit template instantiation (STLport only)
namespace stlpmtx_std {
    template class vector<ObjPtrVec<CharClip, ObjectDir>::Node, StlNodeAlloc<ObjPtrVec<CharClip, ObjectDir>::Node>>;
}
#endif

CharClipGroup::CharClipGroup()
    : mClips(this, (EraseMode)1), mWhich(0), mLRUBoundary(0), mFlags(0) {}

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

void CharClipGroup::AddClip(CharClip *clip) {
    if (!HasClip(clip)) {
        mClips.push_back(ObjOwnerPtr<CharClip>(this, clip));
    }
}

bool CharClipGroup::HasClip(CharClip *clip) const {
    return mClips.find(clip) != mClips.end();
}

// Generates a random index for shuffling clips in the range [pos, end).
// When end < pos (wrapping case), the range extends from pos to array end, then wraps to 0..end.
// Returns the selected index, wrapped back into [0, size) bounds if needed.
int CharClipGroup::QueueRandom(int pos, int end) const {
    int diff = end - pos;
    // Handle wrap-around: if end < pos, range includes tail + head of array
    int range = (diff < 0 ? mClips.size() : 0) + diff;
    int result = Rand::sRand.FastInt(0, range) + pos;
    int size = mClips.size();
    // Modulo operation: wrap result back into valid array bounds
    return result - ((result >= size) ? size : 0);
}

CharClip *CharClipGroup::GetClip(int flags) {
    if (!mClips.size()) {
        return nullptr;
    }

    if (mWhich >= mClips.size()) {
        mWhich = mClips.size() - 1;
    }

    if (mLRUBoundary >= mClips.size()) {
        mLRUBoundary = mClips.size() - 1;
    }

    int origWhich = mWhich;
    int origUnk24 = mLRUBoundary;

    int pos = mWhich + 1;
    pos -= (pos >= mClips.size()) ? mClips.size() : 0;
    mWhich = pos;

    if (pos != origUnk24) {
        do {
            int swapIdx = QueueRandom(pos, origUnk24);
            mClips.swap(pos, swapIdx);
            CharClip *clip = mClips[pos];
            if ((clip->Flags() & flags) == flags) {
                mClips.swap(pos, mWhich);
                int newUnk24 = origUnk24 + 1;
                newUnk24 -= (newUnk24 >= mClips.size()) ? mClips.size() : 0;
                mLRUBoundary = newUnk24;
                return clip;
            }
            pos++;
            pos -= (pos >= mClips.size()) ? mClips.size() : 0;
        } while (pos != origUnk24);
    }

    CharClip *clip = nullptr;
    if (pos != origWhich) {
        do {
            int swapIdx = QueueRandom(pos, origWhich);
            mClips.swap(pos, swapIdx);
            clip = mClips[pos];
            if ((clip->Flags() & flags) == flags) {
                mClips.swap(pos, mWhich);
                mClips.swap(pos, mLRUBoundary);
                goto updateBoundary;
            }
            pos++;
            pos -= (pos >= mClips.size()) ? mClips.size() : 0;
        } while (pos != origWhich);
    }

    clip = mClips[pos];
    if ((clip->Flags() & flags) == flags) {
        mClips.swap(pos, mWhich);
        mClips.swap(pos, mLRUBoundary);
    updateBoundary:;
        int newUnk24 = mLRUBoundary + 1;
        newUnk24 -= (newUnk24 >= mClips.size()) ? mClips.size() : 0;
        mLRUBoundary = newUnk24;
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

void CharClipGroup::DeleteRemaining(int x) {
    CharClip *clips[256];
    MILO_ASSERT(mClips.size() < 256, 0x88);
    for (int i = 0; i < (int)mClips.size(); i++) {
        clips[i] = (CharClip *)mClips[i];
    }
    CharClip::LockAndDelete(clips, mClips.size(), x);
}

CharClip *CharClipGroup::FindClip(const char *name) const {
    for (int i = 0; i < (int)mClips.size(); i++) {
        CharClip *clip = (CharClip *)mClips[i];
        auto _tmp0 = streq(name, clip->Name());
        if (clip && _tmp0)
            return clip;
    }
    return nullptr;
}

void CharClipGroup::SetClipFlags(int flags) {
    for (int i = 0; i < (int)mClips.size(); i++) {
        CharClip *clip = (CharClip *)mClips[i];
        if (clip)
            clip->SetFlags(flags);
    }
}
