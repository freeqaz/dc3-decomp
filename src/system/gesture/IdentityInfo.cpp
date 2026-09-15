#include "gesture\IdentityInfo.h"
#include "gesture\GestureMgr.h"
#include "meta_ham\SkeletonIdentifier.h"

SkeletonIdentifiedMsg::SkeletonIdentifiedMsg(int arg1, int arg2)
    : Message(Type(), arg1, arg2) {}

void IdentityInfo::Identified(unsigned int enrollmentIdx) {
    GestureMgr::sIdentityOpInProgress = false;
    // w8-i: -5 and -4 carry SEPARATE bodies rather than sharing a label group.
    // Stacked as `case -5: case -4: case -1:` the two contiguous labels reach one
    // basic block and MSVC range-merges them into `blt`/`ble`; the image tests all
    // four with `beq`.  Giving each its own body defeats the merge at lowering
    // time, and the identical blocks are tail-merged afterwards -- which is why the
    // image's two `beq`s both target the single `li r4, -0x2`.
    switch ((int)enrollmentIdx) {
    case -5:
        enrollmentIdx = (unsigned int)-2;
        break;
    case -4:
        enrollmentIdx = (unsigned int)-2;
        break;
    case -2:
        enrollmentIdx = (unsigned int)-1;
        break;
    case -1:
        enrollmentIdx = (unsigned int)-2;
        break;
    }
    SkeletonIdentifiedMsg msg(enrollmentIdx, unkc);
    TheGestureMgr->Export(msg, true);
}

void IdentityInfo::PostUpdate() {
    if (mIdentified) {
        Identified(mEnrollmentIdx);
        mIdentified = false;
    }
    if (unk9) {
        unk9 = false;
        static SkeletonEnrollmentChangedMsg msg;
        TheGestureMgr->Export(msg, true);
    }
}
