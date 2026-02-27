#include "gesture/IdentityInfo.h"
#include "gesture/GestureMgr.h"
#include "meta_ham/SkeletonIdentifier.h"

SkeletonIdentifiedMsg::SkeletonIdentifiedMsg(int arg1, int arg2)
    : Message(Type(), arg1, arg2) {}

void IdentityInfo::Identified(unsigned int enrollmentIdx) {
    GestureMgr::sIdentityOpInProgress = false;
    if ((int)enrollmentIdx == -5 || (int)enrollmentIdx == -4) {
        enrollmentIdx = -2;
    } else if ((int)enrollmentIdx == -2) {
        enrollmentIdx = -1;
    } else if ((int)enrollmentIdx == -1) {
        enrollmentIdx = -2;
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
