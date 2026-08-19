#pragma once
#include "..\win_types.h"
#include "vectorintrinsics.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum _NUI_SKELETON_POSITION_TRACKING_STATE {
    NUI_SKELETON_POSITION_NOT_TRACKED = 0x0000,
    NUI_SKELETON_POSITION_INFERRED = 0x0001,
    NUI_SKELETON_POSITION_TRACKED = 0x0002,
} NUI_SKELETON_POSITION_TRACKING_STATE;

typedef enum _NUI_SKELETON_TRACKING_STATE {
    NUI_SKELETON_NOT_TRACKED = 0x0000,
    NUI_SKELETON_POSITION_ONLY = 0x0001,
    NUI_SKELETON_TRACKED = 0x0002,
} NUI_SKELETON_TRACKING_STATE;

typedef struct _NUI_SKELETON_DATA { /* Size=0x1c0 */
    /* 0x0000 */ NUI_SKELETON_TRACKING_STATE eTrackingState;
    /* 0x0004 */ DWORD dwTrackingID;
    /* 0x0008 */ DWORD dwEnrollmentIndex;
    /* 0x000c */ DWORD dwUserIndex;
    /* 0x0010 */ XMVECTOR Position;
    /* 0x0020 */ XMVECTOR SkeletonPositions[20];
    /* 0x0160 */ NUI_SKELETON_POSITION_TRACKING_STATE eSkeletonPositionTrackingState[20];
    /* 0x01b0 */ DWORD dwQualityFlags;
} NUI_SKELETON_DATA;

typedef struct _NUI_SKELETON_FRAME { /* Size=0xab0 */
    /* 0x0000 */ LARGE_INTEGER liTimeStamp;
    /* 0x0008 */ DWORD dwFrameNumber;
    /* 0x000c */ DWORD dwFlags;
    /* 0x0010 */ XMVECTOR vFloorClipPlane;
    /* 0x0020 */ XMVECTOR vNormalToGravity;
    /* 0x0030 */ NUI_SKELETON_DATA SkeletonData[6];
} NUI_SKELETON_FRAME;

HRESULT NuiSkeletonTrackingEnable(HANDLE hNextFrameEvent, DWORD dwFlags);
HRESULT NuiSkeletonTrackingDisable();
HRESULT NuiSkeletonGetNextFrame(DWORD dwMillisecondsToWait, NUI_SKELETON_FRAME *pSkeletonFrame);
HRESULT NuiSkeletonSetTrackedSkeletons(DWORD *TrackingIDs);

#ifdef __cplusplus
}
#endif

// these have C++ definitions
XMMATRIX NuiTransformMatrixLevel(XMVECTOR vNormalToGravity);

// Focal length of the 320x240 depth camera, in pixels. The SDK header spells
// this NUI_CAMERA_SKELETON_TO_DEPTH_IMAGE_MULTIPLIER_320x240; the target's
// constant pool holds it as the raw float 0x438ed0a4.
#define NUI_CAMERA_SKELETON_TO_DEPTH_IMAGE_MULTIPLIER_320x240 285.63f

// FLT_EPSILON, spelled as a bare literal. Two reasons it is not the macro:
// this tree's src/xdk/LIBCMT/float.h defines FLT_EPSILON with a C99 hex-float
// literal (0x1.000000P-23F) that the 2008-era Xenon cl.exe rejects, and a
// named `static const float` makes the compiler emit a NAMED constant where the
// target emits the anonymous pool entry __real@34000000 (100% normalized but
// only 98.8% raw).
#define NUI_SKELETON_DEPTH_EPSILON 1.192092896e-07F

// Both overloads are `inline` HERE, not out-of-line in any .cpp: ham_xbox_r.map
// flags each as `f i` (function, inline) with its single folded copy parked in
// gesture:JointUtl.obj -- JointUtl.cpp's two JointScreenPos() overloads are the
// only odr-uses in the binary. We had them declared and never defined at all, so
// JointUtl.obj carried no body and objdiff scored both 0%.
//
// Reconstructed from the target assembly, which is the stock Kinect SDK body:
// centre of the depth sensor is (0,0,0) in skeleton space and (160,120) in depth
// image coordinates, and +Y is up in skeleton space but down in image space,
// hence the sign flip on the Y term (target: `fnmsubs`). The single `fdivs` +
// two `fmuls` rather than two divisions is /fp:fast folding both divisions by
// vPoint.z into one reciprocal -- see docs/decomp/patterns.
inline void
NuiTransformSkeletonToDepthImage(XMVECTOR vPoint, FLOAT *pfDepthX, FLOAT *pfDepthY) {
    if (pfDepthX && pfDepthY) {
        if (vPoint.z > NUI_SKELETON_DEPTH_EPSILON) {
            *pfDepthX = 160.0f
                + vPoint.x / vPoint.z
                    * NUI_CAMERA_SKELETON_TO_DEPTH_IMAGE_MULTIPLIER_320x240;
            *pfDepthY = 120.0f
                - vPoint.y / vPoint.z
                    * NUI_CAMERA_SKELETON_TO_DEPTH_IMAGE_MULTIPLIER_320x240;
        } else {
            *pfDepthX = 0.0f;
            *pfDepthY = 0.0f;
        }
    }
}

// Depth is metres in skeleton space; the depth image pixel format is millimetres
// shifted left by 3.
inline void NuiTransformSkeletonToDepthImage(
    XMVECTOR vPoint, LONG *plDepthX, LONG *plDepthY, USHORT *pusDepthValue
) {
    if (plDepthX && plDepthY && pusDepthValue) {
        if (vPoint.z > NUI_SKELETON_DEPTH_EPSILON) {
            *plDepthX = (LONG
            )(160.0f
              + vPoint.x / vPoint.z
                  * NUI_CAMERA_SKELETON_TO_DEPTH_IMAGE_MULTIPLIER_320x240);
            *plDepthY = (LONG
            )(120.0f
              - vPoint.y / vPoint.z
                  * NUI_CAMERA_SKELETON_TO_DEPTH_IMAGE_MULTIPLIER_320x240);
            *pusDepthValue = (USHORT)(vPoint.z * 1000) << 3;
        } else {
            *plDepthX = 0;
            *plDepthY = 0;
            *pusDepthValue = 0;
        }
    }
}
