#include "utl/JobMgr.h"
#include "obj/Object.h"
#include "os/Debug.h"
#include "utl/DataPointMgr.h"

namespace {
    static int gJobIDCounter;
}

Job::Job() {
    mID = gJobIDCounter++;
}

void JobMgr::Poll() {
    if (!mJobQueue.empty()) {
        if (mJobQueue.front()->IsFinished()) {
            Job *job = mJobQueue.front();
            mJobQueue.pop_front();
            mPreventStart = true;
            job->OnCompletion(mCallback);
            delete job;
            mPreventStart = false;
            if (!mJobQueue.empty()) {
                mJobQueue.front()->Start();
            }
        }
    }
}

void JobMgr::CancelJob(int id) {
    std::list<Job *>::iterator it = mJobQueue.begin();
    while (it != mJobQueue.end()) {
        Job *job = *it;
        if (job->ID() == id) {
            int frontID = mJobQueue.front()->ID();
            it = mJobQueue.erase(it);
            bool oldstart = mPreventStart;
            mPreventStart = true;
            job->Cancel(mCallback);
            mPreventStart = oldstart;
            if (frontID == id && !oldstart && it != mJobQueue.end()) {
                (*it)->Start();
            }
            delete job;
            return;
        }
        ++it;
    }
    MILO_NOTIFY("This job is not in the queue %i", id);
}

JobMgr::JobMgr(Hmx::Object *o) : mCallback(o), mJobQueue(), mPreventStart(0) {}

void JobMgr::QueueJob(Job *j) {
    mJobQueue.push_back(j);
    if (mJobQueue.size() == 1 && !mPreventStart) {
        mJobQueue.front()->Start();
    }
}

void JobMgr::CancelAllJobs() {
    std::list<Job *> list = mJobQueue;
    mJobQueue.clear();
    for (std::list<Job *>::const_iterator it = list.begin(); it != list.end(); ++it) {
        (*it)->Cancel(mCallback);
        delete *it;
    }
}

JobMgr::~JobMgr() { CancelAllJobs(); }

PostPurchaseEnumJob::PostPurchaseEnumJob(Hmx::Object *obj, int unk, u64 u) : SingleItemEnumJob(obj, unk, u) {}

PostPurchaseEnumJob::~PostPurchaseEnumJob() {}

void PostPurchaseEnumJob::OnCompletion(Hmx::Object *obj) {
    if ((unk18 == 2) && (unk1c != 0)) {
        static int sInitFlags = 0;
        static Symbol sSourceSymbol;
        static Symbol sOfferSymbol;
        static Symbol sPurchaserSymbol;

        if (!(sInitFlags & 1)) {
            sInitFlags |= 1;
            sSourceSymbol = Symbol("source");
        }
        if (!(sInitFlags & 2)) {
            sInitFlags |= 2;
            sOfferSymbol = Symbol("offer");
        }
        if (!(sInitFlags & 4)) {
            sInitFlags |= 4;
            sPurchaserSymbol = Symbol("purchaser");
        }

        String dataStr(MakeString("%016llX", *(u64 *)(((char *)this) + 0x10)));
        SendDataPoint("store/purchase", sSourceSymbol, *(Symbol *)(((char *)this) + 0x48), sOfferSymbol, dataStr, sPurchaserSymbol, *(int *)(((char *)this) + 0x4C));
    }
    SingleItemEnumJob::OnCompletion(obj);
}

MultipleItemsEnumJob::MultipleItemsEnumJob(Hmx::Object *obj, int unk) : Job() {
    unk8 = obj;
    unkc = unk;
}

MultipleItemsEnumJob::~MultipleItemsEnumJob() {}

void MultipleItemsEnumJob::Start() {}

bool MultipleItemsEnumJob::IsFinished() { return true; }

void MultipleItemsEnumJob::Cancel(Hmx::Object *obj) {}

void MultipleItemsEnumJob::OnCompletion(Hmx::Object *obj) {}

MultipleItemsPostPurchaseEnumJob::MultipleItemsPostPurchaseEnumJob(Hmx::Object *obj, int unk) : MultipleItemsEnumJob(obj, unk) {}

MultipleItemsPostPurchaseEnumJob::~MultipleItemsPostPurchaseEnumJob() {}

void MultipleItemsPostPurchaseEnumJob::OnCompletion(Hmx::Object *obj) {
    if ((unk30 == 2) && (unk34 != 0)) {
        static int sInitFlags = 0;
        static Symbol sSourceSymbol;
        static Symbol sOfferSymbol;
        static Symbol sPurchaserSymbol;

        if (!(sInitFlags & 1)) {
            sInitFlags |= 1;
            sSourceSymbol = Symbol("source");
        }
        if (!(sInitFlags & 2)) {
            sInitFlags |= 2;
            sOfferSymbol = Symbol("offer");
        }
        if (!(sInitFlags & 4)) {
            sInitFlags |= 4;
            sPurchaserSymbol = Symbol("purchaser");
        }

        int count = ((int)((u64)unk14 - (u64)unk10)) >> 3;
        if (count != 0) {
            for (int i = 0; i < count; i++) {
                String itemStr(MakeString("0x%016llX", (u64)unk10 + (i << 3)));
                SendDataPoint("store/purchase", sSourceSymbol, *unk5c, sOfferSymbol, itemStr, sPurchaserSymbol, unk60);
            }
        }
    }
    MultipleItemsEnumJob::OnCompletion(obj);
}
