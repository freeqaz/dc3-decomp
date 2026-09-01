#include "xdk\LIBCMT\math.h"
#include "xdk\LIBCMT\stdlib.h"
#include "xdk\LIBCMT\string.h"
#include "xdk\xapilibi\handleapi.h"
#include "xdk\xapilibi\synchapi.h"
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
// The 0x1c-byte blob XConfig category 7 / setting 9 fills in. Only the two
// fields the entry points below touch are named.
struct NUIP_DETROIT_TILT_XCONFIG { /* Size=0x1c */
    /* 0x0000 */ DWORD Flags;
    /* 0x0004 */ FLOAT FarSpaceMillimeters;
    /* 0x0008 */ int ElevationAngleDegrees;
    /* 0x000c */ DWORD Reservedc[4];
};

struct NUIP_DETROIT_RUNTIME_STATE { /* Size=0x200 */
    // Cleared as one 16-byte VMX store at the end of every tilt request; the
    // neighbouring workers take a __vector4* floor plane, hence the type.
    /* 0x0000 */ XMVECTOR FloorPlane;
    /* 0x0010 */ DWORD Unk10;
    // The three distances the caller asked for, converted from metres to
    // millimetres on the way in.
    /* 0x0014 */ FLOAT FarSpaceDistanceMillimeters;
    /* 0x0018 */ FLOAT PreferredPlayspaceMillimeters;
    /* 0x001c */ FLOAT SpaceAboveHeadMillimeters;
    /* 0x0020 */ FLOAT AppliedSpaceAboveHeadMillimeters;
    /* 0x0024 */ DWORD SpinLock;
    /* 0x0028 */ DWORD Unk28;
    /* 0x002c */ int CalibrationValid;
    /* 0x0030 */ DWORD ElevationMovePending;
    /* 0x0034 */ BYTE Reserved34[0x3c - 0x34];
    /* 0x003c */ int Unk3c;
    /* 0x0040 */ DWORD Unk40;
    /* 0x0044 */ BYTE Reserved44[0x50 - 0x44];
    /* 0x0050 */ DWORD TiltInProgress;
    /* 0x0054 */ DWORD FloorSearchFramesRemaining;
    /* 0x0058 */ DWORD Unk58;
    /* 0x005c */ DWORD Unk5c;
    /* 0x0060 */ DWORD Reserved60;
    /* 0x0064 */ int TargetElevationDegrees;
    /* 0x0068 */ FLOAT FarSpaceMillimeters;
    /* 0x006c */ BYTE Reserved6c[0x17c - 0x6c];
    /* 0x017c */ DWORD Unk17c;
    /* 0x0180 */ BYTE Reserved180[0x18c - 0x180];
    /* 0x018c */ FLOAT FloorHeightMillimeters;
    /* 0x0190 */ DWORD ElevationFlags;
    /* 0x0194 */ BYTE Reserved194[0x1a0 - 0x194];
    /* 0x01a0 */ DWORD TiltStatus;
    /* 0x01a4 */ DWORD LastElevationTime;
    /* 0x01a8 */ DWORD LastTiltTime;
    /* 0x01ac */ DWORD TiltCount;
    /* 0x01b0 */ DWORD Reserved1b0;
    /* 0x01b4 */ NUIP_DETROIT_TILT_XCONFIG TiltXConfig;
    /* 0x01d0 */ NUI_TILT_FLAGS LastTiltFlags;
    /* 0x01d4 */ BYTE Reserved1d4[0x1f0 - 0x1d4];
    /* 0x01f0 */ XOVERLAPPED *pOverlapped;
    /* 0x01f4 */ BYTE Reserved1f4[0x200 - 0x1f4];
};

struct NUIP_RUNTIME_STATE { /* Size=0xe9f0 */
    /* 0x0000 */ BYTE Reserved0[0x84];
    /* 0x0084 */ int DeviceState;
    /* 0x0088 */ BYTE Reserved88[0xe9f0 - 0x88];
};

// Zero-initialised .bss in the target (0x200 bytes at 0x8311C650, all zero),
// defined here rather than declared: nuidetroit.obj is where the target puts
// it. NuipRuntimeState lives in nuiruntime.obj and stays a declaration.
NUIP_DETROIT_RUNTIME_STATE NuipDetroitRuntimeState;
extern NUIP_RUNTIME_STATE NuipRuntimeState;

__declspec(noinline) void NuipDetroitCalculateFarSpace();
void NuipDetroitBeginFloorSearch();

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

extern "C" HRESULT XamNuiCameraElevationSetAngle(LONG lAngleDegrees);
// Returns a signed status, not a DWORD: the target compares the result with
// `cmpwi`, not `cmplwi`.
extern "C" LONG
ExGetXConfigSetting(WORD CategoryNum, WORD SettingNum, PVOID Buffer, WORD SizeOfBuffer, WORD *pSizeNeeded);

// The Xam layer reports "no elevation hardware present" as 0x10000000, which
// the internal worker -- like the public wrapper below -- surfaces as success.
LONG NuipCameraElevationSetAngle(LONG lAngleDegrees) {
    if (lAngleDegrees <= 27 && lAngleDegrees >= -27) {
        NuipDetroitRuntimeState.LastElevationTime = GetTickCount();
        HRESULT hr = XamNuiCameraElevationSetAngle(lAngleDegrees);
        return hr == 0x10000000 ? 0 : hr;
    }
    return 0x80070057; // E_INVALIDARG
}

// The sensor sits 2200mm back from the play space. Given a known floor height
// this solves for the elevation angle that puts the far edge of the requested
// play space at the bottom of frame; with the floor still unknown it works the
// other way and derives the far space from the calibrated angle.
//
// __declspec(noinline) because our /O2 inlines this 90-instruction body into
// NuipDetroitGetXConfigSettings and the original does not -- the target's
// GetXConfigSettings is a 40-instruction function ending in a `bl` to this one.
// Measured: without it GetXConfigSettings reads 1.5% (120 instructions, 80
// inserts), with it 100.0% with all 40 equal. Giving it a second real call site
// (NuipCameraAdjustTilt) did NOT stop the inlining, and neither did moving the
// definition above the caller to match the target's .text order; both were
// tried. This function's own codegen is unaffected either way (100.0%, 94/94).
__declspec(noinline) void NuipDetroitCalculateFarSpace() {
    LONG lAngleDegrees;

    if (NuipDetroitRuntimeState.FloorHeightMillimeters != 0.0f) {
        if (NuipDetroitRuntimeState.TiltStatus != 3) {
            if (NuipDetroitRuntimeState.TiltXConfig.FarSpaceMillimeters < 500.0f) {
                NuipDetroitRuntimeState.TiltXConfig.FarSpaceMillimeters = 1800.0f;
            }
            float drop = (NuipDetroitRuntimeState.SpaceAboveHeadMillimeters +
                          NuipDetroitRuntimeState.TiltXConfig.FarSpaceMillimeters) -
                NuipDetroitRuntimeState.FloorHeightMillimeters;
            float degrees = (float)atan(drop / 2200.0f) * 57.2957764f - 22.3654f;
            lAngleDegrees = (LONG)floor(degrees + 0.5);
        } else {
            // Floor known and calibration finished: report the far space the
            // current elevation angle actually reaches.
            float radians = (float)(NuipDetroitRuntimeState.TiltXConfig.ElevationAngleDegrees +
                                    22.3654f) *
                0.0174532924f;
            NuipDetroitRuntimeState.FarSpaceMillimeters =
                (float)tan(radians) * 2200.0f + NuipDetroitRuntimeState.FloorHeightMillimeters;
            goto Done;
        }
    } else {
        // Floor unknown: aim at the camera height alone, offset by whatever the
        // calibration blob already knows.
        float cameraHeight = NuipDetroitRuntimeState.SpaceAboveHeadMillimeters;
        int elevationOffset = NuipDetroitRuntimeState.TiltXConfig.ElevationAngleDegrees;
        float radians = (float)atan2(cameraHeight, 2200.0);
        lAngleDegrees = (LONG)floor(radians * 57.29577951308232 + 0.5) + elevationOffset;
    }

    if (lAngleDegrees >= 27) {
        lAngleDegrees = 27;
    } else if (lAngleDegrees <= -27) {
        lAngleDegrees = -27;
    }
    NuipDetroitRuntimeState.TargetElevationDegrees = lAngleDegrees;
Done:;
}

// Re-read the tilt tuning blob out of XConfig. A missing or opted-out setting,
// or a far-space distance below 500mm, falls back to the 1800mm default.
void NuipDetroitGetXConfigSettings() {
    WORD cbNeeded;
    DWORD *pXConfig;
    int i;

    pXConfig = (DWORD *)&NuipDetroitRuntimeState.TiltXConfig;
    for (i = 0; i < 7; i++) {
        pXConfig[i] = 0;
    }

    cbNeeded = 0;
    if (ExGetXConfigSetting(
            7, 9, &NuipDetroitRuntimeState.TiltXConfig,
            sizeof(NuipDetroitRuntimeState.TiltXConfig), &cbNeeded
        ) != 0
        || (NuipDetroitRuntimeState.LastTiltFlags & 0x40) != 0
        || NuipDetroitRuntimeState.TiltXConfig.FarSpaceMillimeters < 500.0f) {
        NuipDetroitRuntimeState.TiltXConfig.FarSpaceMillimeters = 1800.0f;
    }
    NuipDetroitRuntimeState.FarSpaceMillimeters =
        NuipDetroitRuntimeState.TiltXConfig.FarSpaceMillimeters;
    NuipDetroitCalculateFarSpace();
}

// Park the camera at its lowest angle and give the floor detector 60 frames to
// find a plane. Skipped when the sensor cannot search and the floor is already
// known.
void NuipDetroitBeginFloorSearch() {
    NuipDetroitRuntimeState.TiltInProgress = 1;
    NuipDetroitRuntimeState.Unk28 = 0;
    NuipDetroitRuntimeState.FloorSearchFramesRemaining = 60;
    if ((NuipDetroitRuntimeState.Unk17c & 0x10) != 0
        || (NuipDetroitRuntimeState.FloorHeightMillimeters == 0.0f
            && NuipDetroitRuntimeState.Unk3c == 0)) {
        NuipDetroitRuntimeState.LastElevationTime = GetTickCount();
        XamNuiCameraElevationSetAngle(-25);
        NuipDetroitRuntimeState.ElevationMovePending = 1;
    }
}

extern "C" HRESULT XamNuiCameraElevationGetAngle(LONG *plAngleDegrees, DWORD *pMovingFlags);
// KIRQL is a UCHAR in NT, but the target neither masks the value it gets back
// nor the one it hands to the release, so it travels as a full register here.
extern "C" DWORD KfAcquireSpinLock(DWORD *SpinLock);
extern "C" void KfReleaseSpinLock(DWORD *SpinLock, DWORD NewIrql);

// The tilt worker proper. Latches the request under the runtime spin lock and
// either starts a floor search or drives the camera straight to the angle
// NuipDetroitCalculateFarSpace worked out. With no caller-supplied XOVERLAPPED
// it synthesises one and blocks on it, so the call looks synchronous.
DWORD NuipCameraAdjustTilt(
    NUI_TILT_FLAGS TiltFlags,
    FLOAT SpaceAboveHeadMeters,
    FLOAT FarSpaceDistanceMeters,
    FLOAT PreferredPlayspaceDistanceMeters,
    NUI_TILT_OBJECTS *pTiltObjects,
    XOVERLAPPED *pOverlapped
) {
    XOVERLAPPED LocalOverlapped;
    LONG lCurrentAngle;
    DWORD dwMovingFlags;
    DWORD OldIrql;
    XOVERLAPPED *pRequest;
    DWORD dwResult;
    DWORD dwTiltState;

    FLOAT FarSpaceMillimeters = FarSpaceDistanceMeters * 1000.0f;
    DWORD dwNow = GetTickCount();
    LocalOverlapped.InternalLow = 0;
    for (int i = 1; i < 7; i++) {
        ((DWORD *)&LocalOverlapped)[i] = 0;
    }

    if ((NuipDetroitRuntimeState.ElevationFlags & 1) != 0) {
        return 5; // ERROR_ACCESS_DENIED
    }

    XamNuiCameraElevationGetAngle(&lCurrentAngle, &dwMovingFlags);
    OldIrql = KfAcquireSpinLock(&NuipDetroitRuntimeState.SpinLock);

    if (pOverlapped == 0) {
        LocalOverlapped.hEvent = CreateEventA(0, 1, 0, 0);
        pRequest = &LocalOverlapped;
        NuipDetroitRuntimeState.pOverlapped = pRequest;
    } else {
        NuipDetroitRuntimeState.pOverlapped = pOverlapped;
        pRequest = pOverlapped;
        if (pOverlapped->hEvent != 0 && pOverlapped->hEvent != INVALID_HANDLE_VALUE) {
            ResetEvent(pOverlapped->hEvent);
            pRequest = NuipDetroitRuntimeState.pOverlapped;
        }
    }

    NuipDetroitRuntimeState.PreferredPlayspaceMillimeters =
        PreferredPlayspaceDistanceMeters * 1000.0f;
    pRequest->InternalContext = (ULONG_PTR)pTiltObjects;
    NuipDetroitRuntimeState.FarSpaceDistanceMillimeters = FarSpaceMillimeters;
    NuipDetroitRuntimeState.Unk40 = 1;
    NuipDetroitRuntimeState.SpaceAboveHeadMillimeters = SpaceAboveHeadMeters * 1000.0f;
    dwResult = 0x3e5; // ERROR_IO_PENDING
    NuipDetroitRuntimeState.pOverlapped->InternalLow = 0x3e5;

    if ((TiltFlags & 0x20) != 0) {
        dwTiltState = 5;
    } else if ((NuipDetroitRuntimeState.Unk17c & 0x10) != 0
               && (NuipDetroitRuntimeState.TiltXConfig.Flags & 2) == 0) {
        NuipDetroitRuntimeState.LastTiltTime = dwNow;
        NuipDetroitRuntimeState.LastElevationTime = GetTickCount();
        NuipDetroitBeginFloorSearch();
        goto Unlock;
    } else if ((TiltFlags & 8) != 0) {
        NuipDetroitRuntimeState.LastTiltFlags = TiltFlags;
        NuipDetroitCalculateFarSpace();
        // Only actually drive the motor when the request moved, or when the
        // camera is more than two degrees off the angle we want.
        if (NuipDetroitRuntimeState.SpaceAboveHeadMillimeters !=
                NuipDetroitRuntimeState.AppliedSpaceAboveHeadMillimeters
            || (double)abs(NuipDetroitRuntimeState.TargetElevationDegrees - lCurrentAngle) > 2.0) {
            NuipCameraElevationSetAngle(NuipDetroitRuntimeState.TargetElevationDegrees);
        }
        dwTiltState = 5;
    } else if (NuipDetroitRuntimeState.Unk3c == 0
               && NuipDetroitRuntimeState.FloorHeightMillimeters == 0.0f) {
        NuipDetroitRuntimeState.LastTiltTime = dwNow;
        if ((NuipDetroitRuntimeState.TiltXConfig.Flags & 2) == 0) {
            NuipDetroitBeginFloorSearch();
            goto Unlock;
        }
        NuipCameraElevationSetAngle(NuipDetroitRuntimeState.TargetElevationDegrees);
        dwTiltState = 3;
    } else {
        NuipDetroitRuntimeState.LastTiltTime = dwNow;
        dwTiltState = 3;
    }
    NuipDetroitRuntimeState.TiltInProgress = dwTiltState;

Unlock:
    XMVECTOR Zero = { 0.0f, 0.0f, 0.0f, 0.0f };
    NuipDetroitRuntimeState.LastTiltFlags = TiltFlags;
    NuipDetroitRuntimeState.Unk5c = 0;
    NuipDetroitRuntimeState.Unk58 = 5;
    NuipDetroitRuntimeState.FloorPlane = Zero;
    KfReleaseSpinLock(&NuipDetroitRuntimeState.SpinLock, OldIrql);

    if (pOverlapped != 0) {
        return dwResult;
    }

    while (WaitForSingleObjectEx(LocalOverlapped.hEvent, INFINITE, 1) == 0xc0) {
    }
    dwResult = LocalOverlapped.InternalLow;
    if (LocalOverlapped.hEvent != 0 && LocalOverlapped.hEvent != INVALID_HANDLE_VALUE) {
        CloseHandle(LocalOverlapped.hEvent);
    }
    return dwResult;
}

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
