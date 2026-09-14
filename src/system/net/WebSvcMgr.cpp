#include "net\WebSvcMgr.h"
#include "net\WebSvcReq.h"
#include "obj\Dir.h"
#include "os\Debug.h"
#include "os\NetworkSocket.h"

const char *kHMXDomain = "harmonixmusic.com";

WebSvcMgr::WebSvcMgr() {}

WebSvcMgr::~WebSvcMgr() { DeleteAll(mRequests); }

BEGIN_HANDLERS(WebSvcMgr)
END_HANDLERS

void WebSvcMgr::Init() { SetName("web_svc_mgr", ObjectDir::Main()); }

void WebSvcMgr::OnReqFinished(WebSvcRequest *req) {
    MILO_ASSERT(req, 0x181);
    MILO_ASSERT(req->IsDeleteReady(), 0x182);
    delete req;
}

int WebSvcMgr::NumRequestsStarted() {
    int numTotalReqs = mRequests.size();
    int numUnstartedReqs = 0;
    FOREACH (it, mRequests) {
        WebSvcRequest *cur = *it;
        if (cur->IsNotStarted()) {
            numUnstartedReqs++;
        }
    }
    return numTotalReqs - numUnstartedReqs;
}

int WebSvcMgr::NumRequests() { return mRequests.size(); }

void WebSvcMgr::Start(WebSvcRequest *req) { req->Start(); }

void WebSvcMgr::CancelOutstandingCalls() {
    FOREACH (it, mRequests) {
        WebSvcRequest *cur = *it;
        if (!cur->IsWebSvcRequest() && (cur->IsNotStarted() || cur->IsRunning())) {
            cur->Cancel(true);
        }
    }
}

bool WebSvcMgr::AddRequest(
    WebSvcRequest *req, unsigned int timeout_ms, bool immediate, bool resolve
) {
    MILO_ASSERT(req, 0xD7);
    if (resolve && !ResolveHostname(req)) {
        req->Cancel(true);
        return false;
    }
    req->SetTimeout(timeout_ms);
    if (immediate) {
        mRequests.push_front(req);
    } else {
        mRequests.push_back(req);
    }
    return true;
}

bool WebSvcMgr::ResolveHostname(WebSvcRequest *req) {
    unsigned int ip = req->GetIPAddr();
    if (ip != 0) {
        return true;
    }
    // Try resolving with HMX domain suffix first
    NetAddress addr = ResolveHostname(req->GetHostName(), kHMXDomain, 80);
    if (addr.mIP != 0) {
        req->UpdateIP(addr.mIP);
        return true;
    }
    // Fallback: try resolving hostname as-is (no domain suffix)
    addr = ResolveHostname(req->GetHostName(), NULL, 80);
    if (addr.mIP == 0) {
        return false;
    }
    req->UpdateIP(addr.mIP);
    return true;
}

NetAddress
WebSvcMgr::ResolveHostname(const char *hostname, const char *domain, unsigned short port) {
    NetAddress ret;
    bool notInCache = false;
    String str(hostname);
    if (domain) {
        str += ".";
        str += domain;
    }
    // The find and the end() comparison are ONE full-expression on purpose.
    // find() builds a temporary String from the char* (the map's key type), and
    // that temporary is destroyed at the end of the full expression.  The image
    // computes the comparison FIRST and carries it across the destructor as a
    // materialised bool:
    //   8255BA80  subf  r11, r3, r23     find result vs end
    //   8255BA88  subic r10, r11, 0x1
    //   8255BA8C  subfe r26, r10, r11    r26 = (result == end)
    //   8255BA94  bl    ??1String@@UAA@XZ   <- temp dies here
    //   8255BA98  clrlwi. r11, r26, 24
    // Split into `auto it = find(...); if (it != end())` the destructor runs
    // first and the comparison becomes a plain cmplw afterwards.
    std::map<String, NetAddress>::iterator it;
    if ((it = mHostCache.find(str.c_str())) != mHostCache.end()) {
        ret = it->second;
    } else {
        ret = NetworkSocket::SetIPPortFromHostPort(hostname, domain, port);
        notInCache = true;
    }
    if (ret.mIP != 0 && notInCache) {
        mHostCache.insert(std::make_pair(str, ret));
    }
    return ret;
}

void WebSvcMgr::Poll() {
    bool mustFinish = false;
    unsigned int runningCount = 0;
    std::list<WebSvcRequest *>::iterator it = mRequests.begin();
    while (it != mRequests.end()) {
        WebSvcRequest *req = *it;
        if (!mustFinish) {
            if (req->IsNotStarted()) {
                if (runningCount >= 5) {
                    return;
                }
                Start(req);
            }
            req->Poll();
        }
        if (req->IsDeleteReady() || req->GetState() == WebSvcRequest::kReadyForRemoval) {
            it = mRequests.erase(it);
            if (req->IsDeleteReady()) {
                OnReqFinished(req);
            } else if (req->GetState() == WebSvcRequest::kReadyForRemoval) {
                req->Reset();
            }
        } else {
            if (req->IsRunning() || req->IsFinished()) {
                runningCount++;
            }
            if (req->MustFinishBeforeNext()) {
                mustFinish = true;
            }
            ++it;
        }
    }
}
