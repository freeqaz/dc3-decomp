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
    // Validates HTTP header by searching for double newline (end of headers)
    // Returns true when "\n\n" is found (detects both CRLF and LF line endings)
    bool ValidateHeader(char *buf, int len, int *outPos, int *outLines) {
        unsigned char sawNewline = 0;
        int lineCount = 0;
        char *start = buf;

        if (len > 0) {
            do {
                signed char c = *buf;
                if (c == '\n') {
                    if (sawNewline) {
                        // Found double newline - end of headers
                        if (outPos != 0) {
                            *outPos = buf - start;
                        }
                        if (outLines != 0) {
                            *outLines = lineCount;
                        }
                        return true;
                    }
                    lineCount++;
                    sawNewline = 1;
                } else {
                    // Keep sawNewline set only if we just saw '\r' (CRLF handling)
                    // Bitwise AND ensures sawNewline stays set through '\r' but clears on other chars
                    sawNewline = (c == '\r') & sawNewline;
                }
                len--;
                buf++;
            } while (len > 0);
        }
        return false;
    }
    char *GetNextLine(char *, int *) { return 0; }
    int LineLength(char *, int) { return 1; }
    bool StrIStartsWith(String const &, const char *) { return false; }
    char *ParseHeader(char *p, int lineLen, std::vector<String> *pHeader) {
        MILO_ASSERT(pHeader, 0x83);
        int count = (((int **)pHeader)[1] - ((int **)pHeader)[0]) >> 3;
        if (count > 0) {
            int idx = 0;
            while (count != 0) {
                int len = LineLength(p, lineLen);
                MILO_ASSERT(len > 0, 0x8C);
                (*pHeader)[idx].resize(len + 1);
                strncpy((char *)(*pHeader)[idx].c_str(), p, len);
                (*pHeader)[idx].erase(len);
                count = count - 1;
                p = GetNextLine(p, &lineLen);
                idx = idx + 1;
            }
        }
        return p;
    }

    unsigned int ParseStatusCode(std::vector<String> const &lines) {
        String status;

        if ((StrIStartsWith(lines[0], "HTTP/1.0") == 0) && (StrIStartsWith(lines[0], "HTTP/1.1") == 0)) {
            return 0;
        }

        const char *ptr = lines[0].c_str();
        ptr += 8;

        char c = *ptr;
        while (((c < '0') || (c > '9')) && (c != '\0') && (c != '\n')) {
            ptr++;
            c = *ptr;
        }

        if (c >= '0') {
            do {
                status += c;
                ptr++;
                c = *ptr;
            } while ((c >= '0') && (c <= '9'));
        }

        if (status.c_str()[0] == '\0') {
            return 0;
        }

        return atoi(status.c_str());
    }

    int GetContentLength(std::vector<String> const &) { return 1; }
};

HttpGet::HttpGet(unsigned int ip, unsigned short port, const char *c1, const char *c2)
    : mSocket(nullptr), mPath(c1), mPort(port), mState(-1), unk1c(false),
      mTimeoutMs(kDefaultTimeoutMs), mIP(ip), mHeaders(c2), mRecvBuf(nullptr), mRecvBufPos(0),
      mFileBuf(nullptr), mFileBufSize(0), mFileBufRecvPos(0), mRetryCount(0), mFailType(),
      mPrevState(kHttpGet_Nil) {
    SetState(kHttpGet_Connecting);
    AddRequiredHeaders();
}

HttpGet::HttpGet(
    unsigned int ip, unsigned short port, const char *c1, unsigned char uc, const char *c2
)
    : mSocket(nullptr), mPath(c1), mPort(port), mState(-1), unk1c(uc & 3),
      mTimeoutMs(kDefaultTimeoutMs), mIP(ip), mHeaders(c2), mRecvBuf(nullptr), mRecvBufPos(0),
      mFileBuf(nullptr), mFileBufSize(0), mFileBufRecvPos(0), mRetryCount(0), mFailType() {
    SetState((uc & 4) == 0 ? kHttpGet_Pending : kHttpGet_Connecting);
    AddRequiredHeaders();
}

HttpGet::~HttpGet() { SafeShutdown(); }

void HttpGet::StartSending() {
    MILO_ASSERT(mSocket, 0x311);
    if (!mSocket->CanSend()) {
        mFailType = kHttpFail_Send;
        SetState(kHttpGet_FailedSend);
        return;
    }
    String str = "GET ";
    str += mPath;
    str += " ";
    str += "HTTP/1.1";
    if (!mHeaders.empty()) {
        str += "\r\n";
        str += mHeaders;
    }
    str += "\r\n\r\n";
    int len = (int)str.length();
    if (mSocket->Send(str.c_str(), len) != len) {
        mFailType = kHttpFail_Send;
        SetState(kHttpGet_FailedSend);
    } else {
        SetState(kHttpGet_ReceivingHeaders);
    }
}

// Cleanup and free resources. Match: 99.2% (limited by __FILE__ path difference)
void HttpGet::SafeShutdown() {
    SafeDisconnect();
    if (mFileBuf) {
        MemFree(mFileBuf, __FILE__, 0x359);
        mFileBuf = nullptr;
    }
    mFileBufSize = 0;
    mFileBufRecvPos = 0;
}

void HttpGet::Send() {
    if (mState == kHttpGet_Pending) {
        SetState(kHttpGet_Connecting);
    }
}

bool HttpGet::IsDownloaded() { return mState == kHttpGet_Downloaded; }
bool HttpGet::HasFailed() { return mState == kHttpGet_Failed; }

char *HttpGet::DetachBuffer() {
    if (mState != kHttpGet_Downloaded) {
        return nullptr;
    }
    char *buffer = mFileBuf;
    mFileBuf = nullptr;
    return buffer;
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
    MILO_ASSERT(!mSocket, 0x2FF);
    mSocket = NetworkSocket::Create(true);
    if (mSocket->Fail()) {
        mFailType = kHttpFail_Send;
        SetState(kHttpGet_Failed);
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
    unk94 = post.c_str();
}

HttpPost::~HttpPost() {}

void HttpPost::SetContentLength(unsigned int len) {
    MILO_ASSERT(mContent, 0x3C1);
    mContentLength = len;
    unk90 = len;
    unk94 += "Content-Length: ";
    unk94 += MakeString("%d\r\n", mContentLength);
    unk94 += MakeString("\r\n");
}

bool HttpPost::CanRetry() {
    if (mRetryCount < 3) {
        unk90 = mContentLength;
        return true;
    }
    return false;
}

void HttpPost::StartSending() {
    MILO_ASSERT(mSocket, 0x3CD);
    if (mSocket->CanSend()) {
        unk9c = unk94.length();
        if (mSocket->Send(unk94.c_str(), unk9c) == unk9c) {
            SetState(kHttpGet_SendingBody);
            return;
        }
    }
    mFailType = kHttpFail_Send;
    SetState(kHttpGet_FailedSend);
}
