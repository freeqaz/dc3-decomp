#include "net\DingoJob.h"
#include "JsonUtils.h"
#include "macros.h"
#include "net\HttpReq.h"
#include "net\WebSvcMgr.h"
#include "net\WebSvcReq.h"
#include "net\DingoSvr.h"
#include "os\Debug.h"
#include "os\OnlineID.h"
#include "utl\DataPointMgr.h"
#include "utl\MemMgr.h"
#include "utl\UrlEncode.h"

OnlineID::OnlineID(const OnlineID &other)
    : mXUID(other.mXUID), mPlayerName(other.mPlayerName), mValid(other.mValid) {}

extern const char *lbl_82066608;

#pragma region DingoJob

DingoJob::DingoJob(char const *url, Hmx::Object *callback)
    : WebSvcRequest(url, "", callback), mResult(0), mDataPoint(0), mJsonResponse(0),
      mJsonResponseVersion(0), mTimeoutMs(10000) {
    mContentBuffer = 0;
}

DingoJob::~DingoJob() { RELEASE(mDataPoint); }

void DingoJob::Start() {
    MILO_ASSERT(GetURL(), 0x49);
    MILO_ASSERT(strlen(GetURL()) != 0, 0x4A);

    // The retail instantiation is MakeString<const char*, const char*, const char*,
    // const char*>: four arguments, all char pointers, for the four %s in the
    // format. Ours passed three, and the second was a String object rather than
    // its c_str(). The target's asm evaluates them right to left --
    // GetURL() -> 0x50(r1), TheServer+0x44 (unk40's mStr) -> 0x54(r1), and the
    // vtable+0x80 call (GetPlatform) -> 0x58(r1) -- so the missing argument is
    // the platform string, and there is no TheServer.Poll() call here at all.
    SetURL(MakeString(
        "/%s/%s/%s/%s",
        lbl_82066608,
        TheServer.GetPlatform(),
        TheServer.unk40.c_str(),
        GetURL()
    ));
    StartImpl();
}

void DingoJob::SendCallback(bool success, bool cancelled) {
    // Validate the response if the request succeeded
    if (success) {
        ParseResponse();
        // Check for error result codes
        if (!mJsonResponse || mResult == -1 || mResult == -4 || mResult == -0xb
            || mResult == -0x138b) {
            success = false;
        }
    }

    // Send completion message if callback is set
    if (mCallback) {
        static DingoJobCompleteMsg msg(this, false);
        msg[0] = this;
        msg[1] = success;
        mCallback->Handle(msg, true);

        // On failure, report a debug data point and drop the session.
        if (!success && TheServer.IsAuthenticated() && !cancelled) {
            DataPoint pt("dingo_job_failed");
            pt.AddPair("location", "DingoJob::SendCallback");
            pt.AddPair("mResult", mResult);
            pt.AddPair("mJsonResponse", mJsonResponse ? "non-NULL" : "NULL");
            pt.AddPair("mResponseStr", mResponseStr.c_str());
            pt.AddPair("mBaseUrl", mBaseUrl.c_str());
            pt.AddPair("mResponseStatusCode", (int)GetResponseStatusCode());
            pt.AddPair("session_id", TheServer.unk40.c_str());
            OnlineID onlineId(TheServer.mOnlineId);
            pt.AddPair("player", onlineId.ToString());
            pt.AddPair("severity", "warn");
            pt.AddPair("project", "sync");
            TheDataPointMgr.RecordDebugDataPoint(pt);
            TheWebSvcMgr.CancelOutstandingCalls();
            TheServer.Logout();
            static ServerStatusChangedMsg msg(kServerStatusDisconnected);
            TheServer.Export(msg, true);
        }
    }
}

void DingoJob::CleanUp(bool success) {
    WebSvcRequest::CleanUp(success);
    if (success) {
        // Copy response data to string member for safe ownership
        char *src = mResponseData;
        int size = GetResponseDataLength();
        char *str_buffer =
            (char *)_MemAllocTemp(size + 1, __FILE__, 0x6D, "DingoJobTmp", 0);
        MILO_ASSERT(str_buffer, 0x6E);
        memcpy(str_buffer, src, size);
        str_buffer[size] = '\0';
        mResponseStr = str_buffer;
        MemFree(str_buffer);
    }
}

bool DingoJob::CheckReqResult() {
    JsonConverter converter;
    JsonObject *response = nullptr;
    ParseResponse(&converter, &response, nullptr);
    if (mResult == -3) {
        // Authenticate()'s return value gates the retry -- we were discarding it.
        // Image, 0x8255F430 onwards: `bne .L_8255F468` (already authenticating)
        // sets r3 = 1; otherwise r3 is the result of the vtable+0x64 call
        // (Authenticate). Both paths join at 0x8255F46C `clrlwi. r11, r3, 24;
        // beq .L_8255F498`, and .L_8255F498 sets r30 = 1 and skips DelayJob
        // entirely -- i.e. a FAILED re-authentication returns true.
        bool authOk;
        if (!TheServer.IsAuthenticating()) {
            int padnum = TheServer.mAuthedPadNum;
            TheServer.Logout();
            authOk = TheServer.Authenticate(padnum);
        } else {
            authOk = true;
        }
        if (authOk) {
            TheServer.DelayJob(this);
            return false;
        }
        return true;
    }
    return true;
}

void DingoJob::Reset() {
    mResponseStr.erase();
    mResult = 0;
    WebSvcRequest::Reset();
}

void DingoJob::StartImpl() {
    AddContent(mHttpReq);
    WebSvcRequest::Start();
}

void DingoJob::AddContent(HttpReq *httpReq) {
    MILO_ASSERT(mDataPoint, 0xf1);
    MILO_ASSERT(httpReq, 0xf2);
    String str1, str2;
    mDataPoint->ToJSON(str1);
    URLEncode(str1.c_str(), str2, false);

    // The scan POST-increments, so it stops one past the terminator
    // (lbz / addi / cmplwi / bne at 8255EFC4, then subf+subi at 8255EFD4).
    // A `for (; *scan; scan++)` form stops ON the terminator and makes the
    // size one byte short -- the image's arithmetic is (end - start - 1) + 7
    // with `end` already past the NUL, i.e. strlen + 7.
    const char *scan = str2.c_str();
    while (*scan++ != '\0') {
    }

    // Calculate total size: "params=" (7 bytes) + encoded string length.
    // Negative result: splitting this into `int size = scan - c_str() - 1;
    // size += 7;` to reproduce the image's 32-bit truncation (clrrwi r11, r11,
    // 0 at 8255EFDC) does NOT un-fold it -- MSVC still emits addi r29, r11, 0x6
    // -- and cost 2.2pp elsewhere.
    int size = (scan - str2.c_str() - 1) + 7;

    // Allocate buffer for the complete request body
    char *buf = new char[size + 1];
    mContentBuffer = buf;

    // Copy the "params=" prefix into the buffer. Residual: the image LOADS the
    // eight bytes out of the literal pool (ld r11, ??_C@_07MOHLFAJ@params...)
    // where we materialise them as lis/ori immediates; routing the literal
    // through a `const char *prefix` local does not stop the fold.
    *(s64 *)buf = *(s64 *)"params=";

    // Find the end of the prefix, then back up onto its terminator.
    // Negative result: the image reads str2's buffer pointer BEFORE this scan
    // (lwz r10, 0x5c(r31) at 8255F000); hoisting `data` above the loop to match
    // makes MSVC re-shape both loops onto lbzu/stbu update forms and costs
    // 2.2pp (86.2 -> 84.0), so the load stays where the scan leaves it.
    char *end = (char *)mContentBuffer;
    while (*end++ != '\0') {
    }
    end--;
    const char *data = str2.c_str();

    // Append the encoded data, terminator included: the image stores the
    // loaded byte BEFORE testing it (stb r9, 0x0(r11) at 8255F024 ahead of
    // the bne), so the NUL is copied and the buffer ends terminated.
    while ((*end++ = *data++) != '\0') {
    }

    httpReq->SetContent((const char *)mContentBuffer);
    httpReq->SetContentLength(size);
}

void DingoJob::SetDataPoint(const DataPoint &point) {
    MILO_ASSERT(mDataPoint == NULL, 0x27);
    mDataPoint = new DataPoint(point);
    MILO_ASSERT(mDataPoint, 0x29);
}

const char *DingoJob::GetResponseString() { return mResponseStr.c_str(); }

void DingoJob::ParseResponse() {
    ParseResponse(&mJsonReader, &mJsonResponse, &mJsonResponseVersion);
}

void DingoJob::ParseResponse(JsonConverter *json, JsonObject **response, int *iptr) {
    MILO_ASSERT(json, 0x123);
    MILO_ASSERT(response, 0x124);
    const char *strResult = mResponseStr.c_str();
    mResult = -1000;
    MILO_ASSERT(strResult, 0x12a);
    JsonObject *jObj = json->LoadFromString(strResult);
    if (!jObj) {
        mResult = -1001;
    } else {
        JsonObject *resultObj = json->GetByName(jObj, "result");
        if (resultObj) {
            mResult = resultObj->Int();
            *response = json->GetByName(jObj, "response");
            if (iptr) {
                JsonObject *versionObj = json->GetByName(jObj, "version");
                if (versionObj) {
                    *iptr = versionObj->Int();
                }
            }
        }
    }
}
