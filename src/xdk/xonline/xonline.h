#pragma once
#include "..\win_types.h"
#include "xdk\xapilibi\xbase.h"
#include "xdk\xnet\winsockx.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct _XTITLE_SERVER_INFO { /* Size=0xd0 */
    /* 0x0000 */ IN_ADDR inaServer;
    /* 0x0004 */ char pad[0xcc];
} XTITLE_SERVER_INFO;

/* The XN_LIVE_CONNECTIONCHANGED parameter that means "logged on". */
#define XONLINE_S_LOGON_CONNECTION_ESTABLISHED 0x001510F0L

enum XONLINE_NAT_TYPE {
    XONLINE_NAT_OPEN = 0x0001,
    XONLINE_NAT_MODERATE = 0x0002,
    XONLINE_NAT_STRICT = 0x0003,
};

typedef struct _XSESSION_VIEW_PROPERTIES { /* Size=0xc */
    /* 0x0000 */ DWORD dwViewId;
    /* 0x0004 */ DWORD dwNumProperties;
    /* 0x0008 */ _XUSER_PROPERTY *pProperties;
} XSESSION_VIEW_PROPERTIES;

/* Byte-packed: the declared offsets only add up to the recorded 0x41 with
   pack(1), and PlatformMgr::Poll reads pwszPathName at +0x39 rather than the
   naturally-aligned +0x40. */
#pragma pack(push, 1)
typedef struct _XSTORAGE_FILE_INFO { /* Size=0x41 */
    /* 0x0000 */ DWORD dwTitleID;
    /* 0x0004 */ DWORD dwTitleVersion;
    /* 0x0008 */ QWORD qwOwnerPUID;
    /* 0x0010 */ BYTE bCountryID;
    /* 0x0011 */ QWORD qwReserved;
    /* 0x0019 */ DWORD dwContentType;
    /* 0x001d */ DWORD dwStorageSize;
    /* 0x0021 */ DWORD dwInstalledSize;
    /* 0x0025 */ FILETIME ftCreated;
    /* 0x002d */ FILETIME ftLastModified;
    /* 0x0035 */ WORD wAttributesSize;
    /* 0x0037 */ WORD cchPathName;
    /* 0x0039 */ WCHAR *pwszPathName;
    /* 0x003d */ BYTE *pbAttributes;
} XSTORAGE_FILE_INFO;
#pragma pack(pop)

typedef struct _XSTORAGE_ENUMERATE_RESULTS { /* Size=0xc */
    /* 0x0000 */ DWORD dwTotalNumItems;
    /* 0x0004 */ DWORD dwNumItemsReturned;
    /* 0x0008 */ XSTORAGE_FILE_INFO *pItems;
} XSTORAGE_ENUMERATE_RESULTS;

/* Size 0x14, confirmed by PlatformMgr::Poll passing cbResults = 0x14 alongside
   `anonymous namespace'::mResults, whose symbols.txt size is 0x14. */
typedef struct _XSTORAGE_DOWNLOAD_TO_MEMORY_RESULTS { /* Size=0x14 */
    /* 0x0000 */ DWORD dwBytesTotal;
    /* 0x0004 */ XUID xuidOwner;
    /* 0x000c */ FILETIME ftCreated;
} XSTORAGE_DOWNLOAD_TO_MEMORY_RESULTS;

enum XSTORAGE_FACILITY {
    XSTORAGE_FACILITY_INVALID = 0,
    XSTORAGE_FACILITY_GAME_CLIP = 1,
    XSTORAGE_FACILITY_PER_TITLE = 2,
    XSTORAGE_FACILITY_PER_USER_TITLE = 3,
};

DWORD XStorageBuildServerPath(
    DWORD dwUserIndex,
    XSTORAGE_FACILITY StorageFacility,
    const void *pvStorageFacilityInfo,
    DWORD dwStorageFacilityInfoSize,
    const WCHAR *wszItemName,
    WCHAR *wszServerPath,
    DWORD *pdwServerPathLength
);

DWORD XStorageEnumerate(
    DWORD dwUserIndex,
    const WCHAR *wszServerPath,
    DWORD dwStartingIndex,
    DWORD dwMaxResults,
    DWORD cbResults,
    XSTORAGE_ENUMERATE_RESULTS *pResults,
    XOVERLAPPED *pOverlapped
);

DWORD XStorageDownloadToMemory(
    DWORD dwUserIndex,
    const WCHAR *wszServerPath,
    DWORD cbBuffer,
    void *pvBuffer,
    DWORD cbResults,
    XSTORAGE_DOWNLOAD_TO_MEMORY_RESULTS *pResults,
    XOVERLAPPED *pOverlapped
);

#define XONLINE_GAMERTAG_SIZE 16

/* A friend request we sent that is still pending. */
#define XONLINE_FRIENDSTATE_FLAG_SENTREQUEST 0x40000000
/* A friend request somebody sent us that we have not answered. */
#define XONLINE_FRIENDSTATE_FLAG_RECEIVEDREQUEST 0x80000000

/* Only the leading three fields are recovered from the shipped binary:
   PlatformMgr::Poll walks the XFriendsCreateEnumerator buffer with a stride of
   0xc4 and reads the XUID at +0x00, the gamertag at +0x08 and dwFriendState at
   +0x18.  The remainder (session/invite IDs, rich presence, ...) is never
   touched there, so it stays an unnamed tail rather than a guess. */
/* Byte-packed like the rest of the storage/friends structures: with natural
   alignment the trailing XUID would round the size up to 0xc8, but the Poll
   loop steps the buffer by 0xc4. */
#pragma pack(push, 4)
typedef struct _XONLINE_FRIEND { /* Size=0xc4 */
    /* 0x0000 */ XUID xuid;
    /* 0x0008 */ CHAR szGamertag[XONLINE_GAMERTAG_SIZE];
    /* 0x0018 */ DWORD dwFriendState;
    /* 0x001c */ BYTE pad[0xa8];
} XONLINE_FRIEND;
#pragma pack(pop)

DWORD XFriendsCreateEnumerator(
    DWORD dwUserIndex,
    DWORD dwStartingIndex,
    DWORD dwFriendsToReturn,
    DWORD *pcbBuffer,
    HANDLE *ph
);

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
DWORD XOnlineStartup();
DWORD XOnlineCleanup();

#ifdef __cplusplus
}
#endif
