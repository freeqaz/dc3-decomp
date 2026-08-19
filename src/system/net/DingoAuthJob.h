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

public:

    friend class DingoServer;

private:
    String mSessionID; // 0xb0
};
