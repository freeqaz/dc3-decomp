#include "os/NetworkSocket.h"
#include "os/Debug.h"
#include "utl/MakeString.h"

struct XNDNS {
    int iStatus; // 0x0
    unsigned int cina; // 0x4
    unsigned int aina[8]; // 0x8
};

extern "C" {
int WSACreateEvent();
int XNetDnsLookup(const char *, HANDLE, XNDNS **);
int XNetDnsRelease(XNDNS *);
}

class WinSockSocket {
public:
    static void Init();
};

NetworkSocket::~NetworkSocket() {}

unsigned int NetworkSocket::ResolveHostName(String name) {
    WinSockSocket::Init();
    HANDLE event = (HANDLE)WSACreateEvent();
    XNDNS *pDns = 0;
    int status = XNetDnsLookup(name.c_str(), event, &pDns);
    if (status != 0 || pDns == 0) {
        TheDebug << MakeString("XNetDnsLookup returned %d %x for %s\n", status, pDns, name.c_str());
        return 0;
    }
    unsigned int result = 0;
    WaitForSingleObject(event, 10000);
    int dnsStatus = pDns->iStatus;
    if (dnsStatus == 0x2AF9) {
        char *hostStr = (char *)name.c_str();
        TheDebug << MakeString("Host %s not found.", hostStr);
    } else if (dnsStatus == 0x274C) {
        char *hostStr = (char *)name.c_str();
        TheDebug << MakeString("Host %s lookup timed out.", hostStr);
    } else if (dnsStatus == 0) {
        result = pDns->aina[0];
    }
    if (XNetDnsRelease(pDns) != 0) {
        FormatString fmt("could not release XNDNS");
        TheDebug << fmt.Str();
    }
    CloseHandle(event);
    return result;
}
