#include "gesture/SkeletonRecoverer.h"
#include "gesture/GestureMgr.h"
#include "gesture/Skeleton.h"
#include "obj/Task.h"
#include "utl/Std.h"
#include <cfloat>

SkeletonRecoverer::SkeletonRecoverer() {}

SkeletonRecoverer::~SkeletonRecoverer() {}

bool SkeletonRecoverer::IsSkeletonTracked(int id) const {
    for (int i = 0; i < 6; i++) {
        if (TheGestureMgr->GetSkeleton(i).TrackingID() == id) {
            if (TheGestureMgr->GetSkeleton(i).IsTracked())
                return true;
        }
    }
    return false;
}

int SkeletonRecoverer::GetTrackingIDWithRecovery(int id, int exclude) {
    Skeleton *skel = TheGestureMgr->GetSkeletonByTrackingID(id);
    if (skel && skel->IsTracked()) {
        return id;
    }

    TrackingIDHistory *found = nullptr;
    for (std::list<TrackingIDHistory>::iterator it = mIDHistory.begin(); it != mIDHistory.end();
         ++it) {
        if (it->mTrackingID == id) {
            found = &(*it);
            break;
        }
    }
    if (!found) {
        return 0;
    }

    int bestSkeleton = -1;
    int i = 0;
    float bestDist = FLT_MAX;
    do {
        Skeleton &candidate = TheGestureMgr->GetSkeleton(i);
        if (candidate.TrackingState() != kSkeletonNotTracked
            && candidate.TrackingID() != exclude) {
            float dy = candidate.GetUnkab0().y - found->unk8;
            float dz = candidate.GetUnkab0().z - found->unkC;
            float dx = candidate.GetUnkab0().x - found->unk4;
            float dist = dx * dx + (dz * dz + dy * dy);
            if (dist < bestDist) {
                bestDist = dist;
                bestSkeleton = i;
            }
        }
        i++;
    } while (i < 6);

    float maxRecoveryDistance = GestureMgr::MaxRecoveryDistance();
    if (bestSkeleton == -1 || bestDist > maxRecoveryDistance * maxRecoveryDistance
        || found->mUntrackedTime <= GestureMgr::MinRecoveryTime()) {
        return found->mTrackingID;
    }
    return TheGestureMgr->GetSkeleton(bestSkeleton).TrackingID();
}

bool SkeletonRecoverer::WaitingToRecover() {
    FOREACH (it, mIDHistory) {
        if (it->mUntrackedTime > 0.0f) {
            return true;
        }
    }
    return false;
}

void SkeletonRecoverer::Poll() {
    float deltaSeconds = TheTaskMgr.DeltaUISeconds();

    for (int i = 0; i < 6; i++) {
        Skeleton &skel = TheGestureMgr->GetSkeleton(i);
        if (!skel.IsTracked()) {
            continue;
        }

        TrackingIDHistory *found = nullptr;
        FOREACH (it, mIDHistory) {
            if (it->mTrackingID == skel.TrackingID()) {
                found = &(*it);
                break;
            }
        }

        if (!found) {
            TrackingIDHistory history;
            history.mTrackingID = skel.TrackingID();
            history.unk4 = skel.GetUnkab0().x;
            history.unk8 = skel.GetUnkab0().y;
            history.unkC = skel.GetUnkab0().z;
            history.unk10 = 0.0f;
            history.mUntrackedTime = 0.0f;
            mIDHistory.push_front(history);
        } else {
            found->mUntrackedTime = 0.0f;
            found->unk4 = skel.GetUnkab0().x;
            found->unk8 = skel.GetUnkab0().y;
            found->unkC = skel.GetUnkab0().z;
            found->unk10 = 0.0f;
        }
    }

    for (std::list<TrackingIDHistory>::iterator it = mIDHistory.begin();
         it != mIDHistory.end();) {
        if (it->mUntrackedTime > GestureMgr::MaxRecoveryTime()) {
            it = mIDHistory.erase(it);
            continue;
        }
        if (!IsSkeletonTracked(it->mTrackingID)) {
            it->mUntrackedTime += deltaSeconds;
        }
        ++it;
    }
}
