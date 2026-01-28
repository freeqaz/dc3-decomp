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
        Job *job = mJobQueue.front();
        if (job->IsFinished()) {
            mJobQueue.erase(mJobQueue.begin());
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
    for (std::list<Job *>::iterator it = mJobQueue.begin(); it != mJobQueue.end(); ++it) {
        Job *job = *it;
        if (job->ID() == id) {
            int curID = job->ID();
            it = mJobQueue.erase(it);
            bool oldstart = mPreventStart;
            mPreventStart = true;
            job->Cancel(mCallback);
            mPreventStart = oldstart;
            if (curID == id && !oldstart) {
                for (std::list<Job *>::iterator it2 = mJobQueue.begin(); it2 != it; ++it2) {
                    (*it2)->Start();
                }
            }
            delete job;
            return;
        }
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
