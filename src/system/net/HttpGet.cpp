#include "net/HttpGet.h"
#include "os/Debug.h"
#include "os/NetworkSocket.h"
#include "stl/_vector.h"
#include "utl/MemMgr.h"
#include "utl/Str.h"

const float HttpGet::kDefaultTimeoutMs = 5000.0f;
const int HttpGet::kMaxRetries = 3;
const int HttpGet::kRecvBufSize = 0x1000;

namespace {
    bool ValidateHeader(char *, int, int *, int *) { return false; }
    char *GetNextLine(char *, int *) { return 0; }
    int LineLength(char *, int) { return 1; }
    bool StrIStartsWith(String const &, const char *) { return false; }
    char *ParseHeader(char *, int, std::vector<String> *) { return 0; }
    unsigned int ParseStatusCode(std::vector<String> const &) { return 1; }
    int GetContentLength(std::vector<String> const &) { return 1; }
};

HttpGet::HttpGet(unsigned int ip, unsigned short port, const char *c1, const char *c2)
    : mSocket(0), mPath(c1), mPort(port), mState(-1), mFlags(false),
      mTimeoutMs(kDefaultTimeoutMs), mIP(ip), mHeaders(c2), mRecvBuf(0), mRecvBufPos(0),
      mFileBuf(0), mFileBufSize(0), mFileBufRecvPos(0), mRetryCount(0), mFailType(),
      mPrevState(kHttpGet_Nil) {
    SetState((State)0);
    AddRequiredHeaders();
}

HttpGet::HttpGet(
    unsigned int ip, unsigned short port, const char *c1, unsigned char uc, const char *c2
)
    : mSocket(0), mPath(c1), mPort(port), mState(-1), mFlags(uc & 3),
      mTimeoutMs(kDefaultTimeoutMs), mIP(ip), mHeaders(c2), mRecvBuf(0), mRecvBufPos(0),
      mFileBuf(0), mFileBufSize(0), mFileBufRecvPos(0), mRetryCount(0), mFailType() {
    State s;
    if ((uc & 4) == 0) {
        s = (State)8;
    } else {
        s = (State)0;
    }
    SetState(s);
    AddRequiredHeaders();
}

HttpGet::~HttpGet() { SafeShutdown(); }

void HttpGet::StartSending() {
    MILO_ASSERT(mSocket, 0x311);
    if (!mSocket->CanSend()) {
        mFailType = (HttpGetFailType)1;
        SetState((State)7);
    } else {
        String str("GET ");
        str += mPath;
        str += " ";
        str += "HTTP/1.1";
        if (!mHeaders.empty()) {
            str += "\r\n";
            str += mHeaders;
        }
        str += "\r\n\r\n";
        int len = str.length();
        mSocket->Send(str.c_str(), len);
        if (len != 0) {
            mFailType = (HttpGetFailType)1;
            SetState((State)7);
        } else {
            SetState((State)3);
        }
    }
}

void HttpGet::SafeShutdown() {
    SafeDisconnect();
    if (mFileBuf != 0) {
        MemFree(mFileBuf);
        mFileBuf = 0;
    }
    mFileBufSize = 0;
    mFileBufRecvPos = 0;
}

void HttpGet::Send() {
    if (mState == 8) {
        SetState((State)0);
    }
}

bool HttpGet::IsDownloaded() { return mState == 5; }
bool HttpGet::HasFailed() { return mState == 6; }

char *HttpGet::DetachBuffer() {
    if (mState != 5) {
        return nullptr;
    } else {
        char *buffer = mFileBuf;
        mFileBuf = nullptr;
        return buffer;
    }
}

void HttpGet::StartReceiving() {
    if (mRecvBuf) {
        MemFree(mRecvBuf, __FILE__, 0x344);
        mRecvBuf = nullptr;
    }
    mRecvBuf = _MemAllocTemp(0x1000, __FILE__, 0x346, "HttpGet", 0);
}

void HttpGet::SafeDisconnect() {
    if (mSocket) {
        mSocket->Disconnect();
        RELEASE(mSocket);
    }
    if (mRecvBuf) {
        MemFree(mRecvBuf, __FILE__, 0x351);
        mRecvBuf = nullptr;
    }
    mRecvBufPos = 0;
}

void HttpGet::StartConnection() {
    MILO_ASSERT(mSocket == NULL, 0x2FF);
    mSocket = NetworkSocket::Create(true);
    if (mSocket->Fail()) {
        mFailType = (HttpGetFailType)1;
        SetState((State)6);
    } else {
        mSocket->Connect(mIP, mPort);
    }
}

bool HttpGet::HasTimedOut() {
    mTimer.Split();
    return mTimer.Ms() > mTimeoutMs;
}

void HttpGet::SetTimeout(float timeout) { mTimeoutMs = timeout; }

HttpPost::HttpPost(unsigned int ip, unsigned short port, const char *cc, unsigned char uc)
    : HttpGet(ip, port, cc, uc, nullptr) {
    String newLine;
    newLine = MakeString("\r\n");
    String post("POST ");
    post += mPath.c_str();
    post += " ";
    post += "HTTP/1.1";
    post += newLine;
    post += "Host: ";
    post += NetworkSocket::IPIntToString(ip);
    post += ":";
    post += MakeString("%d", mPort);
    post += newLine;
    post += "Content-Type: application/x-www-form-urlencoded";
    post += newLine;
    post += "Connection: close";
    post += newLine;
    mRequestHeaders = post.c_str();
}

HttpPost::~HttpPost() {}

void HttpPost::SetContentLength(unsigned int len) {
    MILO_ASSERT(mContent, 0x3C1);
    mContentLength = len;
    mBytesRemaining = len;
    mRequestHeaders += "Content-Length: ";
    mRequestHeaders += MakeString("%d\r\n", mContentLength);
    mRequestHeaders += MakeString("\r\n");
}

bool HttpPost::CanRetry() {
    if (mRetryCount < 3) {
        mBytesRemaining = mContentLength;
        return true;
    } else {
        return false;
    }
}

void HttpPost::StartSending() {
    MILO_ASSERT(mSocket, 0x3CD);
    if (mSocket->CanSend()) {
        mHeaderLength = mRequestHeaders.length();
        if (mSocket->Send(mRequestHeaders.c_str(), mHeaderLength) == mHeaderLength) {
            SetState((State)2);
            return;
        }
    }
    mFailType = (HttpGetFailType)1;
    SetState((State)7);
}
