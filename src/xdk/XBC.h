#pragma once
#include "xdk/win_types.h"
#include "xdk/xapilibi/xbase.h"

struct HJSONWRITER__;
typedef HJSONWRITER__ HJSONWRITER;
struct HJSONREADER__;
typedef HJSONREADER__ HJSONREADER;

enum _JSONTokenType {
    kJSONTokenNone = 0,
    kJSONTokenBeginArray = 1,
    kJSONTokenEndArray = 2,
    kJSONTokenBeginMap = 3,
    kJSONTokenEndMap = 4,
    kJSONTokenString = 5,
    kJSONTokenNumber = 6,
    kJSONTokenTrue = 7,
    kJSONTokenFalse = 8,
    kJSONTokenNull = 9,
    kJSONTokenFieldName = 10,
    kJSONTokenEnd = 11,
    kJSONTokenComment = 12,
    kJSONTokenError = 13,
};

#define XBC_MAX_CLIENTS 4

typedef enum _XBC_EVENT_TYPE {
    XBC_EVENT_CLIENT_CONNECTED = 1,
    XBC_EVENT_CLIENT_DISCONNECTED = 2,
    XBC_EVENT_JSON_SEND_COMPLETE = 3,
    XBC_EVENT_JSON_RECEIVE_COMPLETE = 4
} XBC_EVENT_TYPE;

typedef enum _XBC_PROTOCOL_ID {
    XBC_PROTOCOL_NONE = 0,
    XBC_PROTOCOL_TCP = 1,
    XBC_PROTOCOL_CLOUD = 2
} XBC_PROTOCOL_ID;

typedef enum _XBC_DELIVERY_METHOD {
    XBC_DELIVERY_RELIABLE = 0,
    XBC_DELIVERY_UNRELIABLE = 1
} XBC_DELIVERY_METHOD;

typedef struct _XBC_EVENT_PARAMS {
    /* 0x0000 */ DWORD nClientId;
    /* 0x0004 */ DWORD nUserIndex;
    /* 0x0008 */ XBC_EVENT_TYPE Type;
    union {
        /* 0x000c */ XBC_PROTOCOL_ID ProtocolId;
        /* 0x000c */ HJSONREADER *hReader;
        /* 0x000c */ void *pvSendState;
    };
} XBC_EVENT_PARAMS;

typedef void (*XBC_EVENT_CALLBACK)(HRESULT hr, XBC_EVENT_PARAMS *pParams, void *pvState);

// C++ symbols
HRESULT XbcInitialize(XBC_EVENT_CALLBACK pCallback, void *pvState);
HRESULT XbcDoWork();
HRESULT XbcSendJSON(
    XBC_DELIVERY_METHOD eDeliveryMethod,
    DWORD nClientId,
    HJSONWRITER *hWriter,
    void *pvSendState
);

// XJSON Writer API
HJSONWRITER *XJSONCreateWriter();
long XJSONBeginArray(HJSONWRITER *writer);
long XJSONEndArray(HJSONWRITER *writer);
long XJSONWriteStringValue(HJSONWRITER *writer, const char *str, unsigned long len);
long XJSONWriteNumberValue(HJSONWRITER *writer, double value);
long XJSONWriteNullValue(HJSONWRITER *writer);
long XJSONGetBuffer(HJSONWRITER *writer, char *buffer, unsigned long *size);
long XJSONCloseWriter(HJSONWRITER *writer);

// XJSON Reader API
HJSONREADER *XJSONCreateReader();
long XJSONSetBuffer(HJSONREADER *reader, const char *buffer, unsigned long size, int flags);
long XJSONReadToken(HJSONREADER *reader, _JSONTokenType *tokenType, unsigned long *param1, unsigned long *param2);
long XJSONGetTokenValue(HJSONREADER *reader, wchar_t *buffer, unsigned long bufSize);
long XJSONCloseReader(HJSONREADER *reader);
