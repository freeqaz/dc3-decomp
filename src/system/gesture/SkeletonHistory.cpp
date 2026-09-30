#include "gesture\SkeletonHistory.h"
#include "ArchiveSkeleton.h"
#include "os\Debug.h"

SkeletonHistoryArchive::SkeletonHistoryArchive() {
    for (int i = 0; i < 6; i++) {
        mHistories[i].reserve(0xA0);
    }
}

const std::vector<ArchiveSkeleton> &SkeletonHistoryArchive::GetArchive(int skel_idx) const {
    MILO_ASSERT((0) <= (skel_idx) && (skel_idx) < (6), 0x36);
#ifdef HX_NATIVE
    if (skel_idx < 0 || skel_idx >= 6) {
        static const std::vector<ArchiveSkeleton> sEmpty;
        return sEmpty;
    }
#endif
    return mHistories[skel_idx];
}

bool SkeletonHistory::PrevFromArchive(
    const SkeletonHistoryArchive &archives,
    const Skeleton &skeleton,
    int targetMs,
    ArchiveSkeleton &archiveSkeleton,
    int &elapsedMs
) const {
    int skel_idx = skeleton.SkeletonIndex();
    MILO_ASSERT((0) <= (skel_idx) && (skel_idx) < (6), 0x13);
#ifdef HX_NATIVE
    if (skel_idx < 0 || skel_idx >= 6) return false;
#endif
    const std::vector<ArchiveSkeleton> &archive = archives.GetArchive(skel_idx);
    std::vector<ArchiveSkeleton>::const_iterator it = archive.begin();
    elapsedMs = skeleton.ElapsedMs();
    // Spell end() at each test and let MSVC do the caching: it rotates the loop
    // so the `it != end()` test IS the latch (0x8243EFB8) and the entry branches
    // straight to it (`b .L_8243EFB8` at 0x8243EF84), with the elapsedMs test at
    // the top of the body.  The old `(itEnd = archive.end(), it != itEnd)` form,
    // which hoisted end() into a named local inside the condition, made MSVC peel
    // the `it != end` test into an extra pre-header guard as well as keeping it at
    // the latch -- two surplus instructions (a `cmplw`/`beq` pair) and no entry
    // branch, 93.831%.  The trailing `it != archive.end()` is CSE'd onto the
    // latch's own load, which is what 0x8243EFC4 reusing r11 shows.
    while (it != archive.end() && elapsedMs < targetMs) {
        elapsedMs += it->ElapsedMs();
        ++it;
    }
    if (it != archive.end()) {
        archiveSkeleton = *it;
        return true;
    } else
        return false;
}

void SkeletonHistoryArchive::ClearHistory(int skel_idx) {
    MILO_ASSERT((0) <= (skel_idx) && (skel_idx) < (6), 0x4C);
    mHistories[skel_idx].clear();
}

void SkeletonHistoryArchive::AddToHistory(int skel_idx, const Skeleton &skeleton) {
    MILO_ASSERT((0) <= (skel_idx) && (skel_idx) < (6), 0x3F);

    while (mHistories[skel_idx].size() >= 0xA0) {
        mHistories[skel_idx].pop_back();
    }

    ArchiveSkeleton archiveSkeleton;
    archiveSkeleton.Set(skeleton);
    mHistories[skel_idx].insert(mHistories[skel_idx].begin(), archiveSkeleton);
}
