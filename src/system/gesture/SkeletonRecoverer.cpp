#include "gesture\SkeletonRecoverer.h"
#include "gesture\GestureMgr.h"
#include "gesture\Skeleton.h"
#include "obj/Task.h"
#include "utl\Std.h"
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
            float dy = candidate.GetUnkab0().y - found->mPos.y;
            float dz = candidate.GetUnkab0().z - found->mPos.z;
            float dx = candidate.GetUnkab0().x - found->mPos.x;
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

        int trackingID = skel.TrackingID();
        TrackingIDHistory *found;
        for (std::list<TrackingIDHistory>::iterator it = mIDHistory.begin();
             it != mIDHistory.end(); ++it) {
            if (it->mTrackingID == trackingID) {
                found = &(*it);
                goto check_found;
            }
        }
        found = nullptr;
        check_found:
        if (found) {
            found->mUntrackedTime = 0.0f;
            found->mPos = skel.GetUnkab0Padded();
        } else {
            TrackingIDHistory history;
            history.mUntrackedTime = 0.0f;
            history.mTrackingID = trackingID;
            history.mPos = skel.GetUnkab0Padded();
            mIDHistory.insert(mIDHistory.begin(), history);
        }
    }

    for (std::list<TrackingIDHistory>::iterator it = mIDHistory.begin();
         it != mIDHistory.end();) {
        TrackingIDHistory *data = &(*it);
        if (data->mUntrackedTime > GestureMgr::MaxRecoveryTime()) {
            it = mIDHistory.erase(it);
            continue;
        }
        ++it;
        if (!IsSkeletonTracked(data->mTrackingID)) {
            data->mUntrackedTime = deltaSeconds + data->mUntrackedTime;
        }
    }
}
