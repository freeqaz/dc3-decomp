// DingoServer native stub — prevents null dispatch crashes in RockCentral::Init/Poll.
// Xbox Live networking is not available on native; all auth/job operations are no-ops.

#ifdef HX_NATIVE

#include "net/DingoSvr.h"

class DingoServerNative : public DingoServer {
public:
    DingoServerNative() {}
    void CreateAccount() override {}
    bool Authenticate(int padnum) override { return false; }
    const char *GetPlatform() override { return "native"; }
    void Poll() override {}
    void ManageJob(DingoJob *job) override {
        // No network — just delete the job immediately
        delete job;
    }
};

static DingoServerNative gNativeServer;
DingoServer &TheServer = gNativeServer;

#endif // HX_NATIVE
