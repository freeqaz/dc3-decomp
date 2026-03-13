#include "net/DingoSvr.h"
#include "net/DingoJob.h"
#include "net/DingoAuthJob.h"
#include "WebSvcReq.h"
#include "WebSvcMgr.h"
#include "meta/ConnectionStatusPanel.h"
#include "obj/Data.h"
#include "obj/Dir.h"
#include "obj/Object.h"
#include "os/Debug.h"
#include "os/PlatformMgr.h"
#include "os/System.h"
#include "utl/DataPointMgr.h"
#include "utl/Std.h"
#include "utl/Symbol.h"
#include <cstring>

DingoServer::DingoServer() : mAuthState(kServerUnauthed), mPort(0), mPendingPadNum(-1), mAuthedPadNum(-1) {
    for (int i = 0; i < DIM(mPadAuthed); i++) {
        mPadAuthed[i] = false;
    }
}

BEGIN_HANDLERS(DingoServer)
    HANDLE_MESSAGE(SigninChangedMsg)
    HANDLE_MESSAGE(ConnectionStatusChangedMsg)
    HANDLE_EXPR(is_authed, IsAuthenticated())
    HANDLE_SUPERCLASS(Hmx::Object)
END_HANDLERS

void DingoServer::Init() {
    SetName("server", ObjectDir::Main());
    mLocale = PlatformRegionToSymbol(ThePlatformMgr.GetRegion());
    ThePlatformMgr.AddSink(this, SigninChangedMsg::Type());
    ThePlatformMgr.AddSink(this, ConnectionStatusChangedMsg::Type());
    mLanguage = SystemLanguage();
}

void DingoServer::Logout() {
    unk40 = "";
    mAuthState = kServerUnauthed;
    mAuthedPadNum = -1;
    for (int i = 0; i < DIM(mPadAuthed); i++) {
        mPadAuthed[i] = false;
    }
    mOnlineId.Clear();
}

void DingoServer::ManageJob(DingoJob *job) {
    MILO_ASSERT(job, 0xd0);
    bool isUrlDisabled = false;
    FOREACH (it, mDisabledUrls) {
        String cur(*it);
        if (strncmp(job->GetBaseURL(), cur.c_str(), cur.length()) == 0) {
            isUrlDisabled = true;
            break;
        }
    }
    bool shouldSendFailureCallback = true;
    bool authSucceeded = true;
    bool justAuthenticated = false;
    if (!isUrlDisabled) {
        if (!IsAuthenticated()) {
            MILO_NOTIFY("ManageJob without authentication.");
            if (ThePlatformMgr.IsConnected()) {
                authSucceeded = TheServer.Authenticate(mAuthedPadNum);
                justAuthenticated = true;
            } else {
                authSucceeded = false;
            }
        }
        if (authSucceeded && !job->GetHttpReq()) {
            shouldSendFailureCallback = !InitAndAddJob(job, false, justAuthenticated);
        }
    }
    if (shouldSendFailureCallback) {
        job->SendCallback(false, false);
        delete job;
    }
}

void DingoServer::FillAuthParams(DataPoint &point) {
    static Symbol locale("locale");
    point.AddPair(locale, mLocale.c_str());
    static Symbol language("language");
    point.AddPair(language, mLanguage.c_str());
}

void DingoServer::DoAdditionalLogin() {
    MILO_ASSERT(mAuthUrl.length() > 0, 0xa9);
    MILO_ASSERT(mAuthState == kServerAuthed, 0xAA);
    if (mAuthState == kServerAuthed) {
        if (mAuthUrl.length() != 0) {
            for (int i = 0; i < 4; i++) {
                if (!mPadAuthed[i]) {
                    DataPoint pt;
                    if (FillAuthParamsFromPadNum(pt, i)
                        && SendAuthenticateMsg(mAuthUrl.c_str(), pt, nullptr)) {
                        mPadAuthed[i] = true;
                    }
                }
            }
        }
    }
}

void DingoServer::DelayJob(DingoJob *job) { mDelayedJobs.push_back(job); }

void DingoServer::CancelDelayedCalls() {
    FOREACH (it, mDelayedJobs) {
        DingoJob *cur = *it;
        cur->Cancel(true);
        delete cur;
    }
    mDelayedJobs.clear();
}

// TODO: implement
#ifdef HX_NATIVE
DataNode DingoServer::OnMsg(const SigninChangedMsg &) { return DataNode(0); }
DataNode DingoServer::OnMsg(const ConnectionStatusChangedMsg &) { return DataNode(0); }
DataNode DingoServer::OnMsg(const DingoJobCompleteMsg &) { return DataNode(0); }
bool DingoServer::InitAndAddJob(DingoJob *, bool, bool) { return false; }
void DingoServer::AddDelayedCalls() {}
#endif
bool DingoServer::Authenticate(int padnum, const char *url) {
    if (mAuthState != 0) {
        return true;
    }

    mAuthState = kServerAuthenticating;
    mAuthUrl = url;

    DataPoint pt;
    if (padnum < 0) {
        FillAuthParams(pt);
        return SendAuthenticateMsg(url, pt, this);
    }

    if (FillAuthParamsFromPadNum(pt, padnum)) {
        return SendAuthenticateMsg(url, pt, this);
    }
    return false;
}

bool DingoServer::SendAuthenticateMsg(const char *url, DataPoint &pt, Hmx::Object *callback) {
    AuthenticateReqJob *job = new AuthenticateReqJob(url, pt, callback);
    return InitAndAddJob(job, true, false);
}
