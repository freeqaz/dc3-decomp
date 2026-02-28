#pragma once
#include "obj/Msg.h"
#include "obj/Object.h"
#include "stdlib.h"
#include "utl/MemMgr.h"
#include "utl/Symbol.h"
#include "xdk/xapilibi/xbase.h"
#include <vector>

class Job {
public:
    Job();
    virtual ~Job() {}
    virtual void Start() = 0;
    virtual bool IsFinished() = 0;
    virtual void Cancel(Hmx::Object *) = 0;
    virtual void OnCompletion(Hmx::Object *) {}

    int ID() const { return mID; }

    MEM_OVERLOAD(Job, 0x11);

private:
    int mID; // 0x4
};

class JobMgr {
public:
    void Poll();
    void CancelJob(int);
    JobMgr(Hmx::Object *);
    void QueueJob(Job *);
    ~JobMgr();

    MEM_OVERLOAD(JobMgr, 0x2A);

    Hmx::Object *mCallback; // 0x0
    std::list<Job *> mJobQueue; // 0x4
    bool mPreventStart; // 0xc

private:
    void CancelAllJobs();
};

class SingleItemEnumJob : public Job {
public:
    SingleItemEnumJob(Hmx::Object *, int, u64);
    virtual ~SingleItemEnumJob();
    virtual void Start();
    virtual bool IsFinished();
    virtual void Cancel(Hmx::Object *);
    virtual void OnCompletion(Hmx::Object *);

protected:
    Hmx::Object *mObject;           // 0x8
    int mUnkc;                      // 0xc
    u64 mItemID;                    // 0x10
    int mStatus;                    // 0x18
    bool mSuccess;                  // 0x1c
    int unk20;
    int unk24;
    XOVERLAPPED mOverlapped;        // 0x28
};

class PostPurchaseEnumJob : public SingleItemEnumJob {
public:
    PostPurchaseEnumJob(Hmx::Object *, int, u64, Symbol, unsigned int);
    virtual ~PostPurchaseEnumJob();
    virtual void OnCompletion(Hmx::Object *);

private:
    Symbol mOfferSymbol;            // 0x48
    int mPurchaserID;               // 0x4C
};

class MultipleItemsEnumJob : public Job {
public:
    MultipleItemsEnumJob(Hmx::Object *, int);
    virtual ~MultipleItemsEnumJob();
    virtual void Start();
    virtual bool IsFinished();
    virtual void Cancel(Hmx::Object *);
    virtual void OnCompletion(Hmx::Object *);

protected:
    Hmx::Object *mObject;           // 0x8
    int mUnkc;                      // 0xc
    u64 *mItemIDsBegin;
    u64 *mItemIDsEnd;
    int mStatus;                    // 0x18
    bool mSuccess;                  // 0x1c
    int unk20;
    int unk24;
    XOVERLAPPED mOverlapped;        // 0x28
    int mEnumStatus;
    bool mEnumSuccess;
    int unk38;
    int unk3c;
    int unk40;
    int unk44;
    int unk48;
    int unk4c;
    int unk50;
    int unk54;
    int unk58;
    Symbol *mOfferSymbol;
    int mPurchaserID;
};

class MultipleItemsPostPurchaseEnumJob : public MultipleItemsEnumJob {
public:
    MultipleItemsPostPurchaseEnumJob(Hmx::Object *, int, std::vector<unsigned long long> &, Symbol, unsigned int);
    virtual ~MultipleItemsPostPurchaseEnumJob();
    virtual void OnCompletion(Hmx::Object *);
};

DECLARE_MESSAGE(SingleItemEnumCompleteMsg, "single_item_enum_complete")
bool Success() const { return mData->Int(2); }
bool HasOfferID() const { return mData->Int(3); }
unsigned long long OfferID() const;
END_MESSAGE

