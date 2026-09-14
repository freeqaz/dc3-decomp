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

    // The whole tail is three C library intrinsics, not hand-rolled loops.
    // strlen: 8255EFBC keeps str2's base in r10, scans with a POST-increment
    // (8255EFC4 lbz/addi/cmplwi cr6/bne -- an UNSIGNED compare into cr6),
    // then 8255EFD4 subf / 8255EFD8 subi 1 / 8255EFDC `clrrwi r11, r11, 0` /
    // 8255EFE0 `addi r29, r11, 0x7`.  The clrrwi is a zero-extend to size_t;
    // it is what sits between the -1 and the +7 and stops MSVC folding them
    // into the `addi 0x6` we used to emit.
    // strcpy of an 8-byte constant: 8255EFF4 `ld` out of the literal pool +
    // 8255EFF8 `std`, never lis/ori immediates.
    // strcat: 8255EFFC re-reads mContentBuffer, 8255F000 loads str2's pointer
    // BEFORE the destination scan (it is the second ARGUMENT), then a
    // post-increment scan and a post-increment copy loop with separate addis
    // -- not the lbzu/stbu update forms a hand-written loop produces.
    int size = strlen(str2.c_str()) + 7;
    mContentBuffer = new char[size + 1];
    strcpy((char *)mContentBuffer, "params=");
    strcat((char *)mContentBuffer, str2.c_str());

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
