#pragma once
#include "math\\Vec.h"
#include <list>

class SkeletonRecoverer {
public:
    struct TrackingIDHistory {
        int mTrackingID; // 0x0
        PaddedJointPos mPos; // 0x4
        float mUntrackedTime; // 0x14
    };
    SkeletonRecoverer();
    virtual ~SkeletonRecoverer();

    void Poll();
    bool WaitingToRecover();
    int GetTrackingIDWithRecovery(int, int);

protected:
    std::list<TrackingIDHistory> mIDHistory; // 0x4

private:
    bool IsSkeletonTracked(int) const;
};
