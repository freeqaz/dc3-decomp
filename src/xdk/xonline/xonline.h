#pragma once
#include "../win_types.h"
#include "xdk/xapilibi/xbase.h"
#include "xdk/xnet/winsockx.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct _XTITLE_SERVER_INFO { /* Size=0xd0 */
    /* 0x0000 */ IN_ADDR inaServer;
    /* 0x0004 */ char pad[0xcc];
} XTITLE_SERVER_INFO;

typedef struct _XSESSION_VIEW_PROPERTIES { /* Size=0xc */
    /* 0x0000 */ DWORD dwViewId;
    /* 0x0004 */ DWORD dwNumProperties;
    /* 0x0008 */ _XUSER_PROPERTY *pProperties;
} XSESSION_VIEW_PROPERTIES;

DWORD XTitleServerCreateEnumerator(
    LPCSTR pszServerInfo, DWORD cItem, DWORD *pcbBuffer, HANDLE *hEnum
);

DWORD XSessionStart(HANDLE hSession, DWORD dwFlags, XOVERLAPPED *pXOverlapped);
DWORD XSessionEnd(HANDLE hSession, XOVERLAPPED *pXOverlapped);

DWORD XSessionWriteStats(
    HANDLE hSession,
    XUID xuid,
    DWORD dwNumViews,
    XSESSION_VIEW_PROPERTIES *pViews,
    XOVERLAPPED *pXOverlapped
);
DWORD XSessionCreate(
    DWORD dwFlags,
    DWORD dwUserIndex,
    DWORD dwMaxPublicSlots,
    DWORD dwMaxPrivateSlots,
    ULONGLONG *pqwSessionNonce,
    XSESSION_INFO *pSessionInfo,
    XOVERLAPPED *pXOverlapped,
    HANDLE *ph
);
DWORD XSessionDelete(HANDLE hSession, XOVERLAPPED *pXOverlapped);
DWORD XSessionJoinLocal(
    HANDLE hSession,
    DWORD dwUserCount,
    const DWORD *pdwUserIndexes,
    const BOOL *pfPrivateSlots,
    XOVERLAPPED *pXOverlapped
);
DWORD XSessionLeaveLocal(
    HANDLE hSession,
    DWORD dwUserCount,
    const DWORD *pdwUserIndexes,
    XOVERLAPPED *pXOverlapped
);

#ifdef __cplusplus
}
#endif
