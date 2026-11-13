// PoseInput_Web.cpp -- the web build's pose transport.
//
// Desktop gets skeleton frames from pose_server.py over a Unix socket
// (Skeleton_Native.cpp's reader thread).  The browser has no child process and
// no socket: the page runs the camera and MediaPipe itself (native/web/pose/,
// a port of native/scripts/pose_mediapipe.py) and pushes each camera frame
// here.  Both transports end in NativeSkeletonProvider::PublishFrame, so
// everything after it -- Poll, the frame-id gate, FillSkeletonData's joint
// hold, AssignSlots, the image's Skeleton::Poll and scoring -- is the same code.
//
// JS contract (see native/web/pose/pose_input.js, and the dc3_pre.js helper
// Module.dc3PosePush which does the heap copy):
//   float *dc3_pose_buffer()      scratch area, kPoseMaxPersons * kPoseFloatsPerPerson
//   int dc3_pose_push(count, data, timestampSeconds)
//       data: per person [trackId, 20 x (x, y, z, conf)] -- DC3's 20 joints in
//       BaseSkeleton.h order, camera-space metres, conf in [0,1] (the desktop
//       protocol's layout 1, same thresholds via the same ConfidenceFromScore
//       rule).  count 0 is a real frame with nobody in view.  The first push
//       starts the provider (the sensor leaves the dummy skeleton).
//   void dc3_pose_stop()          camera gone: back to the dummy skeleton.
//
// Status: WebPoseStatusTick (main_web.cpp, once per engine frame) publishes
// window.dc3PoseGame -- what the GAME sees (player 0's bound skeleton, score,
// rating), not what the page sent -- so a test can show that joints arrived.
#ifdef __EMSCRIPTEN__

#include <emscripten/emscripten.h>
#include <emscripten/em_asm.h>

#include "platform/Skeleton_Native.h"
#include "gesture/GestureMgr.h"
#include "gesture/Skeleton.h"
#include "hamobj/HamGameData.h"
#include "hamobj/HamPlayerData.h"
#include "obj/Data.h"

#include <cstdio>
#include <cstring>

namespace {

const int kPoseMaxPersons = NativeSkeletonProvider::kMaxPersons;
const int kPoseFloatsPerPerson = 1 + kNumJoints * 4;

float sPushBuffer[kPoseMaxPersons * kPoseFloatsPerPerson];
uint32_t sPushFrameId = 0;
unsigned sPushCount = 0;
unsigned sPushPersons = 0; // persons in the last push

JointConfidence ConfidenceFromScore(float score) {
    // Identical to Skeleton_Native.cpp's socket path (and the COCO path).
    if (score < 0.3f)
        return kConfidenceNotTracked;
    if (score < 0.6f)
        return kConfidenceInferred;
    return kConfidenceTracked;
}

} // namespace

extern "C" {

EMSCRIPTEN_KEEPALIVE
float *dc3_pose_buffer() { return sPushBuffer; }

EMSCRIPTEN_KEEPALIVE
int dc3_pose_capacity() { return kPoseMaxPersons; }

EMSCRIPTEN_KEEPALIVE
int dc3_pose_push(int count, const float *data, double timestamp) {
    NativeSkeletonProvider *provider = TheSkeletonProvider;
    if (!provider || !data)
        return 0; // engine not initialised yet (or DC3_POSE=off): drop the frame
    if (count < 0)
        count = 0;
    if (count > kPoseMaxPersons)
        count = kPoseMaxPersons;

    NativeSkeletonProvider::PersonData persons[kPoseMaxPersons];
    memset(persons, 0, sizeof(persons));
    for (int i = 0; i < count; i++) {
        const float *p = data + i * kPoseFloatsPerPerson;
        persons[i].trackId = (int)p[0];
        for (int j = 0; j < kNumJoints; j++) {
            const float *v = p + 1 + j * 4;
            persons[i].joints[j] = Vector3(v[0], v[1], v[2]);
            persons[i].confidence[j] = ConfidenceFromScore(v[3]);
        }
        persons[i].valid = true;
    }
    provider->PublishFrame(persons, count, ++sPushFrameId, timestamp);
    if (!provider->IsRunning()) {
        provider->StartPushed();
        printf("DC3 POSE: web camera pipeline connected (first frame, %d person(s))\n", count);
    }
    sPushCount++;
    sPushPersons = (unsigned)count;
    return 1;
}

EMSCRIPTEN_KEEPALIVE
void dc3_pose_stop() {
    if (TheSkeletonProvider && TheSkeletonProvider->IsRunning()) {
        TheSkeletonProvider->Stop();
        printf("DC3 POSE: web camera pipeline stopped -> dummy skeleton\n");
    }
}

} // extern "C"

// Once per engine frame from main_web.cpp; publishes every 15 frames.
void WebPoseStatusTick(int engineFrame) {
    if (engineFrame % 15 != 0)
        return;
    char buf[1024];
    int n = 0;
    bool running = TheSkeletonProvider && TheSkeletonProvider->IsRunning();
    n += snprintf(buf + n, sizeof(buf) - n,
                  "{\"frame\":%d,\"providerRunning\":%s,\"pushes\":%u,\"lastPushPersons\":%u,"
                  "\"players\":[",
                  engineFrame, running ? "true" : "false", sPushCount, sPushPersons);
    for (int p = 0; p < 2 && n < (int)sizeof(buf) - 300; p++) {
        int score = 0;
        const char *rating = "";
        int trackingId = -1;
        bool tracked = false;
        Vector3 head(0, 0, 0), hip(0, 0, 0), handL(0, 0, 0), handR(0, 0, 0);
        HamPlayerData *pd = TheGameData ? TheGameData->Player(p) : nullptr;
        if (pd) {
            trackingId = pd->GetSkeletonTrackingID();
            static Symbol scoreSym("score");
            static Symbol ratingSym("rating");
            const DataNode *node =
                pd->Provider() ? pd->Provider()->Property(scoreSym, false) : nullptr;
            if (node && node->Type() == kDataInt)
                score = node->Int();
            node = pd->Provider() ? pd->Provider()->Property(ratingSym, false) : nullptr;
            if (node && node->Type() == kDataSymbol)
                rating = node->Sym().Str();
            const Skeleton *skel = pd->GetSkeleton();
            if (skel && skel->IsTracked()) {
                tracked = true;
                skel->JointPos(kCoordCamera, kJointHead, head);
                skel->JointPos(kCoordCamera, kJointHipCenter, hip);
                skel->JointPos(kCoordCamera, kJointHandLeft, handL);
                skel->JointPos(kCoordCamera, kJointHandRight, handR);
            }
        }
        n += snprintf(buf + n, sizeof(buf) - n,
                      "%s{\"player\":%d,\"trackingId\":%d,\"tracked\":%s,\"score\":%d,"
                      "\"rating\":\"%s\",\"head\":[%.3f,%.3f,%.3f],\"hip\":[%.3f,%.3f,%.3f],"
                      "\"handL\":[%.3f,%.3f,%.3f],\"handR\":[%.3f,%.3f,%.3f]}",
                      p ? "," : "", p, trackingId, tracked ? "true" : "false", score, rating,
                      head.x, head.y, head.z, hip.x, hip.y, hip.z, handL.x, handL.y, handL.z,
                      handR.x, handR.y, handR.z);
    }
    if (n < (int)sizeof(buf) - 3)
        snprintf(buf + n, sizeof(buf) - n, "]}");
    EM_ASM({ try { window.dc3PoseGame = JSON.parse(UTF8ToString($0)); } catch (e) {} }, buf);
}

#endif // __EMSCRIPTEN__
