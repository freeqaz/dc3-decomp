// CameraInput_Web.cpp -- the web build's camera picture transport (PLAN P-4).
//
// Desktop gets the camera picture + person mask inside pose_server.py's
// protocol v3 packets (Skeleton_Native.cpp's reader thread).  The browser runs
// the camera and MediaPipe itself (native/web/pose/): with segmentation masks
// on, pose_worker.js scales each frame to a small RGB picture and turns the
// masks into one byte per pixel naming the person (1-based, in the order of
// the persons the same frame pushed through dc3_pose_push).  Both transports
// end in NativeCamera::Publish, so the Kinect streams, the LiveCameraInput
// stand-in and the native StreamRenderer are the same code.
//
// JS contract (native/web/pose/pose_input.js):
//   globalThis.dc3CameraPending = { rgb: Uint8Array(w*h*3), mask: Uint8Array(w*h),
//                                   trackIds: Int32Array(n), depthM: Float32Array(n) }
//   int  Module._dc3_camera_push(w, h, n, timestampSeconds)
//        copies the pending frame into the engine (EM_ASM, where HEAPU8 is in
//        scope -- the page cannot see the heap, and this keeps dc3_pre.js
//        untouched) and publishes it.  1 = taken, 0 = rejected (bad size, no
//        pending frame).  Call it right after the frame's dc3_pose_push, so the
//        ordinals and track ids describe the same persons.
//   void Module._dc3_camera_stop()   the picture went away (dc3_pose_stop also
//        stops it, through NativeSkeletonProvider::Stop).
#ifdef __EMSCRIPTEN__

#include <emscripten/emscripten.h>
#include <emscripten/em_asm.h>

#include "platform/CameraStream_Native.h"

#include <cstdio>
#include <utility>

namespace {
unsigned sCameraPushes = 0;
}

extern "C" {

EMSCRIPTEN_KEEPALIVE
int dc3_camera_push(int width, int height, int numPersons, double timestamp) {
    using namespace NativeCamera;
    if (width <= 0 || height <= 0 || width > kWireMaxDim || height > kWireMaxDim)
        return 0;
    if (numPersons < 0)
        numPersons = 0;
    if (numPersons > kMaxPersons)
        numPersons = kMaxPersons;
    Image img;
    img.width = width;
    img.height = height;
    img.rgb.resize((size_t)width * height * 3);
    img.mask.resize((size_t)width * height);
    img.numPersons = numPersons;
    img.timestamp = timestamp;
    int ok = EM_ASM_INT({
        var f = globalThis.dc3CameraPending;
        if (!f || !f.rgb || !f.mask || f.rgb.length < $2 || f.mask.length < $3) return 0;
        HEAPU8.set(f.rgb.subarray(0, $2), $0);
        HEAPU8.set(f.mask.subarray(0, $3), $1);
        for (var i = 0; i < $6; i++) {
            HEAP32[($4 >> 2) + i] = f.trackIds && i < f.trackIds.length ? f.trackIds[i] : -1;
            HEAPF32[($5 >> 2) + i] = f.depthM && i < f.depthM.length ? f.depthM[i] : 0;
        }
        globalThis.dc3CameraPending = null;
        return 1;
    }, img.rgb.data(), img.mask.data(), (int)img.rgb.size(), (int)img.mask.size(), img.trackIds,
       img.depthM, numPersons);
    if (!ok)
        return 0;
    if (sCameraPushes++ == 0)
        printf("DC3 CAMERA: web camera picture connected (%dx%d)\n", width, height);
    Publish(std::move(img));
    return 1;
}

EMSCRIPTEN_KEEPALIVE
void dc3_camera_stop() { NativeCamera::Stop(); }

} // extern "C"

#endif // __EMSCRIPTEN__
