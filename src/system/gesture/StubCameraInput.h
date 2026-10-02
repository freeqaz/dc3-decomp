#pragma once
#include "CameraInput.h"

class StubCameraInput : public CameraInput {
public:
    StubCameraInput();
    static void StubSkeletonFrame(SkeletonFrame &);
    static void StubSkeletonData(SkeletonData &, const Vector3 &);

protected:
    const SkeletonFrame *PollNewFrame();
    SkeletonFrame unk11d4; // 0x11d4
    // 6 per-skeleton extra entries, zeroed in ctor
    struct StubSkeletonExtra {
        bool unk0; // 0x0
        Vector3 unk4; // 0x4 (Vector3 carries its own pad word at 0x10)
    }; // size: 0x14 = 20
    StubSkeletonExtra unk239c[6]; // 0x239c, total 120 bytes
};
