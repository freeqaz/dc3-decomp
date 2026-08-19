#include "net\DingoAuthJob.h"
#include "DingoAuthJob.h"
#include "DingoSvr.h"
#include "net\DingoJob.h"
#include "net\JsonUtils.h"
#include "os\Debug.h"
#include "utl\MakeString.h"

// The target instantiates ??$MakeString@PBDPBDPBD@@ (@823a32f8) -- three
// `const char*` -- and takes the reference argument straight out of a static
// slot (`lis r11, lbl_8206643C@h; addi r4, r11, ...@l`). A string literal
// passed inline deduces `const char(&)[2]` and instantiates a different
// template; casting it deduces the right one but materialises a stack temp.
// A file-scope pointer reproduces both.
static const char *const kApiVersion = "1";

AuthenticateReqJob::AuthenticateReqJob(
    char const *url, const DataPoint &point, Hmx::Object *callback
)
    : DingoJob(url, callback) {
    DingoJob::SetDataPoint(point);
}

AuthenticateReqJob::~AuthenticateReqJob() {}

void AuthenticateReqJob::Start() {
    MILO_ASSERT(GetURL(), 0x24);
    MILO_ASSERT(strlen(GetURL()) != 0, 0x25);
    SetURL(MakeString("/%s/%s/%s", kApiVersion, TheServer.GetPlatform(), GetURL()));
    StartImpl();
}

bool AuthenticateReqJob::CheckReqResult() { return true; }

bool AuthenticateReqJob::MustFinishBeforeNext() { return true; }

bool AuthenticateReqJob::ParseResponse() {
    mSessionID = "";
    if (mJsonResponse) {
        if (mJsonResponseVersion == 1) {
            JsonObject *o = mJsonReader.GetByName(mJsonResponse, "session_id");
            if (o) {
                mSessionID = o->Str();
                return true;
            }
        } else {
            MILO_NOTIFY(
                "AuthenticateReqJob: New version of Authenticate response API!  Needs attention!"
            );
        }
    }
    return false;
}
