#include "xdk\LIBCMT\vectorintrinsics.h"
#include "xdk\nui\nuidetroit.h"
#include "xdk\xapilibi\sysinfoapi.h"

#ifdef __cplusplus
extern "C" {
#endif

void XMScalarSinCos(float *pSin, float *pCos, float Value);

XMMATRIX XMMatrixRotationX(float Angle) {
    float fSin, fCos;
    XMMATRIX M;

    XMScalarSinCos(&fSin, &fCos, Angle);

    M.m[0][0] = 1.0f;
    M.m[0][1] = 0.0f;
    M.m[0][2] = 0.0f;
    M.m[0][3] = 0.0f;

    M.m[1][0] = 0.0f;
    M.m[1][1] = fCos;
    M.m[1][2] = fSin;
    M.m[1][3] = 0.0f;

    M.m[2][0] = 0.0f;
    M.m[2][1] = -fSin;
    M.m[2][2] = fCos;
    M.m[2][3] = 0.0f;

    M.m[3][0] = 0.0f;
    M.m[3][1] = 0.0f;
    M.m[3][2] = 0.0f;
    M.m[3][3] = 1.0f;

    return M;
}

#ifdef __cplusplus
}
#endif

// ---------------------------------------------------------------------------
// Camera elevation / tilt entry points.
//
// These are the public (extern "C") NUI entry points; each one validates its
// arguments and the Detroit runtime state before forwarding to the internal
// C++-mangled Nuip* worker in the same library.
// ---------------------------------------------------------------------------

// Internal runtime state blocks. Only the fields these three entry points
// touch are named; the rest is padding so the offsets line up with the
// original 0x200 / 0xe9f0 byte objects.
struct NUIP_DETROIT_RUNTIME_STATE { /* Size=0x200 */
    /* 0x0000 */ BYTE Reserved0[0x2c];
    /* 0x002c */ int CalibrationValid;
    /* 0x0030 */ BYTE Reserved30[0x50 - 0x30];
    /* 0x0050 */ DWORD TiltInProgress;
    /* 0x0054 */ BYTE Reserved54[0x190 - 0x54];
    /* 0x0190 */ DWORD ElevationFlags;
    /* 0x0194 */ BYTE Reserved194[0x1a0 - 0x194];
    /* 0x01a0 */ DWORD TiltStatus;
    /* 0x01a4 */ BYTE Reserved1a4[0x1a8 - 0x1a4];
    /* 0x01a8 */ DWORD LastTiltTime;
    /* 0x01ac */ DWORD TiltCount;
    /* 0x01b0 */ BYTE Reserved1b0[0x1d0 - 0x1b0];
    /* 0x01d0 */ NUI_TILT_FLAGS LastTiltFlags;
    /* 0x01d4 */ BYTE Reserved1d4[0x200 - 0x1d4];
};

struct NUIP_RUNTIME_STATE { /* Size=0xe9f0 */
    /* 0x0000 */ BYTE Reserved0[0x84];
    /* 0x0084 */ int DeviceState;
    /* 0x0088 */ BYTE Reserved88[0xe9f0 - 0x88];
};

extern NUIP_DETROIT_RUNTIME_STATE NuipDetroitRuntimeState;
extern NUIP_RUNTIME_STATE NuipRuntimeState;

LONG NuipCameraElevationSetAngle(LONG lAngleDegrees);
void NuipDetroitGetXConfigSettings();
DWORD NuipCameraAdjustTilt(
    NUI_TILT_FLAGS TiltFlags,
    FLOAT SpaceAboveHeadMeters,
    FLOAT FarSpaceDistanceMeters,
    FLOAT PreferredPlayspaceDistanceMeters,
    NUI_TILT_OBJECTS *pTiltObjects,
    XOVERLAPPED *pOverlapped
);

#ifdef __cplusplus
extern "C" {
#endif

HRESULT XamNuiCameraElevationGetAngle(LONG *plAngleDegrees, DWORD *pMovingFlags);
DWORD XexCheckExecutablePrivilege(DWORD dwPrivilege);

HRESULT NuiCameraElevationGetAngle(LONG *plAngleDegrees, DWORD *pMovingFlags) {
    if (plAngleDegrees == 0)
        return 0x80070057; // E_INVALIDARG
    if (pMovingFlags == 0)
        return 0x80070057; // E_INVALIDARG
    HRESULT hr = XamNuiCameraElevationGetAngle(plAngleDegrees, pMovingFlags);
    // The Xam layer reports "no elevation hardware present" as 0x10000000,
    // which the public API surfaces as success.
    return hr == 0x10000000 ? 0 : hr;
}

HRESULT NuiCameraElevationSetAngle(LONG lAngleDegrees) {
    if (XexCheckExecutablePrivilege(0x25) != 0) {
        if ((NuipDetroitRuntimeState.ElevationFlags & 1) == 0) {
            if (NuipRuntimeState.DeviceState != 0)
                return 0x8301000b;
            return NuipCameraElevationSetAngle(lAngleDegrees);
        }
    }
    return 0x80070005; // E_ACCESSDENIED
}

DWORD NuiCameraAdjustTilt(
    NUI_TILT_FLAGS TiltFlags,
    FLOAT SpaceAboveHeadMeters,
    FLOAT FarSpaceDistanceMeters,
    FLOAT PreferredPlayspaceDistanceMeters,
    NUI_TILT_OBJECTS *pTiltObjects,
    XOVERLAPPED *pOverlapped
) {
    float space;
    DWORD tracked;
    DWORD i;
    DWORD elapsed;
    DWORD requests;

    DWORD now = GetTickCount();

    if ((TiltFlags & 0x18) == 0x18)
        goto InvalidParameter;

    space = SpaceAboveHeadMeters * 0.001f;
    if (space > 0.5f)
        goto InvalidParameter;
    if (space < -0.15)
        goto InvalidParameter;

    if (pTiltObjects != 0) {
        if (pTiltObjects->Count > 6) {
        InvalidParameter:
            return 0x57; // ERROR_INVALID_PARAMETER
        }
        if (pTiltObjects->Count != 0) {
            // At most one object may be flagged as the preferred playspace.
            tracked = 0;
            for (i = 0; i < pTiltObjects->Count; i++) {
                if (pTiltObjects->Objects[i].Flags & 0x40000000) {
                    tracked++;
                    if (tracked > 1)
                        goto InvalidParameter;
                }
            }
        }
    }

    if (NuipDetroitRuntimeState.TiltInProgress == 0) {
        // Throttle: at most 16 tilt requests inside any 20 second window,
        // unless the caller asks to bypass the throttle.
        elapsed = now - NuipDetroitRuntimeState.LastTiltTime;
        if (elapsed > 20000) {
            requests = 0;
            NuipDetroitRuntimeState.TiltCount = requests;
        } else {
            requests = NuipDetroitRuntimeState.TiltCount;
        }

        if (NuipRuntimeState.DeviceState == 0) {
            if (NuipDetroitRuntimeState.CalibrationValid != 0) {
                if (elapsed >= 1000 || (TiltFlags & 0x20) != 0) {
                    if (requests > 15 && (TiltFlags & 0x20) == 0) {
                        NuipDetroitRuntimeState.LastTiltTime = now;
                        return 0x38; // ERROR_TOO_MANY_CMDS
                    }
                    NuipDetroitRuntimeState.LastTiltFlags = TiltFlags;
                    NuipDetroitGetXConfigSettings();
                    NuipDetroitRuntimeState.TiltStatus = 0;
                    return NuipCameraAdjustTilt(
                        TiltFlags,
                        SpaceAboveHeadMeters,
                        FarSpaceDistanceMeters,
                        PreferredPlayspaceDistanceMeters,
                        pTiltObjects,
                        pOverlapped
                    );
                }
            }
        }
        return 0x4d5; // ERROR_RETRY
    }
    return 0xaa; // ERROR_BUSY
}

#ifdef __cplusplus
}
#endif
