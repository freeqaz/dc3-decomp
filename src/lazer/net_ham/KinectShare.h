#pragma once
#include "net/HttpGet.h"
#include "net\XLSPConnection.h"
#include "utl\HxGuid.h"

// size 0x140
#pragma pack(push, 1)
class KinectShare : public HttpPost {
public:
    enum EContentType {
    };
    KinectShare(unsigned int, int, const char *, int, EContentType, u64, u64, u64, u64);

protected:
    virtual bool CanRetry();
    virtual void Sending();

    unsigned char unka0; // 0xa0
    unsigned short unka1; // 0xa1
    unsigned short unka3; // 0xa3
    u64 unka5; // 0xa5
    u64 unkad; // 0xad
    u64 unkb5; // 0xb5
    u64 unkbd; // 0xbd
    u64 unkc5; // 0xc5
    bool unkcd; // 0xcd
    int unkce; // 0xce
    bool unkd2; // 0xd2
    int unkd3; // 0xd3
    int unkd7; // 0xd7
    unsigned short unkdb;
    unsigned char unkdd;
    unsigned short unkde;
    unsigned char mContentType;
    int unke1[4];
    int unkf1;
    unsigned short unkf5;
    unsigned short unkf7;
    int unkf9[4];
    int unk109;
    bool unk10d;
    bool unk10e;
    int unk10f;
    u64 unk113;
    int unk11b[4];
    int unk12b;
    int unk12f;
    unsigned short mLanguage;
    unsigned short mLocale;
    bool unk137;
    int mHeaderSize;
    int mHeaderBytesRemaining;
};
#pragma pack(pop)

class KinectShareConnection {
public:
    ~KinectShareConnection();
    void Poll();
    int GetState() const { return mState; }

private:
    XLSPConnection mConnection; // 0x0
    int mState; // 0x78 - connection state machine (0=connecting, 1=uploading, 2=success, 3=failed)
    KinectShare *mKinectShare; // 0x7c
    const char *mContent;
    int mContentLength;
    KinectShare::EContentType mContentType;
    u64 unk90;
    u64 unk98;
    u64 unka0;
    u64 unka8;
};
