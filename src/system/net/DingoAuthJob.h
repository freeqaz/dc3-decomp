#pragma once

#include "net\DingoJob.h"
#include "obj/Object.h"
#include "utl\DataPointMgr.h"
#include "utl\Str.h"

class AuthenticateReqJob : public DingoJob {
public:
    AuthenticateReqJob(const char *url, const DataPoint &pt, Hmx::Object *callback);
    virtual ~AuthenticateReqJob();
    virtual void Start();

    bool ParseResponse();

protected:
    /** Both at 0x82E2AB00 ('li r3,1; blr'), 'f'. */
    virtual bool CheckReqResult();
    virtual bool MustFinishBeforeNext();
    /** ??_7AuthenticateReqJob@@6B@ slot +0x90 (target 37 slots, we had 36).
     *  ?ShouldRemoveReq@AuthenticateReqJob@@MAA_NXZ is in ham_xbox_r.map at
     *  0x82E2AB00 ("li r3,1; blr").  It is declared here and not on DingoJob:
     *  ??_7DingoJob@@6B@ is 36 slots on both sides. */
    virtual bool ShouldRemoveReq() { return true; }

public:

    friend class DingoServer;

private:
    String mSessionID; // 0xb0
};
