#include "char/FileMergerOrganizer.h"
#include "FileMerger.h"
#include "FileMergerOrganizer.h"
#include "math/Rand.h"
#include "obj/Object.h"
#include "os/Debug.h"
#include "os/System.h"

FileMergerOrganizer *TheFileMergerOrganizer;
std::map<Symbol, CatData> gCatPriority;
int gNextCatPriority = 1;
bool gOrganizing;
bool gGenderChirality;

void FileMergerOrganizerLoader::PollLoading() { TheFileMergerOrganizer->StartLoad(); }

FileMergerOrganizer::FileMergerOrganizer() : mActiveOrg(nullptr), mStartOrg(nullptr) {
    MILO_ASSERT(TheFileMergerOrganizer == NULL, 0x1BE);
    TheFileMergerOrganizer = this;
}

BEGIN_HANDLERS(FileMergerOrganizer)
    HANDLE_ACTION(add_file_merger, AddFileMerger(_msg->Obj<FileMerger>(2)))
    HANDLE_SUPERCLASS(Hmx::Object)
END_HANDLERS

BEGIN_PROPSYNCS(FileMergerOrganizer)
    SYNC_SUPERCLASS(Hmx::Object)
END_PROPSYNCS

BEGIN_SAVES(FileMergerOrganizer)
    MILO_FAIL("Can't save TheFileMergerOrganizer");
END_SAVES

BEGIN_COPYS(FileMergerOrganizer)
    MILO_FAIL("Can't copy TheFileMergerOrganizer");
END_COPYS

BEGIN_LOADS(FileMergerOrganizer)
    MILO_FAIL("Can't load TheFileMergerOrganizer");
END_LOADS

FileMerger::Merger *FileMergerOrganizer::FrontInactiveMerger(OrganizedFileMerger *ofm) {
    std::list<FileMerger::Merger *>::iterator it = ofm->merger->mFilesPending.begin();
    if (ofm->merger->mCurLoader)
        it++;
    if (it == ofm->merger->mFilesPending.end())
        return nullptr;
    else
        return *it;
}

void FileMergerOrganizer::FinishLoading(Loader *l) {
    OrganizedFileMerger *org = mActiveOrg;
    MILO_ASSERT(org->merger->mCurLoader == l, 0x144);
    MILO_ASSERT(org->merger->mFilesPending.front()->loading == l->LoaderFile(), 0x145);
    MILO_ASSERT(org->state == kPendingLoad, 0x146);
    org->state = kFinishLoad;
    mActiveOrg = nullptr;
    if (l->LoaderFile().empty()) {
        if (FrontInactiveMerger(org)) {
            Dispatch(org);
        } else
            RemoveFileMerger(org);
    } else
        CheckDone();
}

void FileMergerOrganizer::FailedLoading(Loader *l) {
    OrganizedFileMerger *org = nullptr;
    FOREACH (it, mOrganizedFileMergers) {
        org = &*it;
        if (org->merger->mCurLoader && org->merger->mCurLoader == l)
            break;
    }
    MILO_ASSERT(org, 0x173);
    MILO_ASSERT(org->merger->mFilesPending.front()->loading == l->LoaderFile(), 0x176);
    MILO_ASSERT(org->merger->mCurLoader == l, 0x177);
    MILO_ASSERT(org->state == kPendingLoad || (org->state == kFinishLoad && mActiveOrg != org), 0x179);
    org->state = kFailedLoad;
    OrganizedFileMerger *oldOrg = mActiveOrg;
    mActiveOrg = nullptr;
    Dispatch(org);
    MILO_ASSERT(org->merger->mCurLoader == NULL, 0x184);
    org->state = (OrganizedState)0;
    if (oldOrg == org)
        oldOrg = nullptr;
    mActiveOrg = oldOrg;
    if (!mActiveOrg && !mStartOrg) {
        FOREACH (it, mOrganizedFileMergers) {
            OrganizedFileMerger *curr = &*it;
            MILO_ASSERT(curr->state != kPendingLoad, 0x193);
        }
        mStartOrg = new FileMergerOrganizerLoader();
    }
}

void FileMergerOrganizer::Init() {
    MILO_ASSERT(gNextCatPriority == 1, 0x19E);
    REGISTER_OBJ_FACTORY(FileMergerOrganizer)
    DataArray *cfg = SystemConfig("file_merger_organizer");
    DataArray *catArr = cfg->FindArray("category_order", false);
    DataArray *genderArr = cfg->FindArray("gender_order", false);
    if (catArr) {
        for (; gNextCatPriority < catArr->Size(); gNextCatPriority++) {
            DataArray *curCatArr = catArr->Array(gNextCatPriority);
            for (int i = 0; i < curCatArr->Size(); i++) {
                Symbol key = curCatArr->Sym(i);
                CatData &val = gCatPriority[key];
                val.priority = gNextCatPriority;
                val.unk4 = genderArr && genderArr->Contains(key);
            }
            gOrganizing = true;
        }
    }
    FileMergerOrganizer::NewObject(); // ???
}

void FileMergerOrganizer::StartLoad() {
    gGenderChirality = RandomInt() & 1;
    RELEASE(mStartOrg);
    CheckDone();
}

void FileMergerOrganizer::Dispatch(FileMergerOrganizer::OrganizedFileMerger *ofm) {
    MILO_ASSERT(mActiveOrg == NULL, 0xC4);
    mActiveOrg = ofm;
    if (mActiveOrg->state != kFailedLoad) {
        FOREACH (it, mOrganizedFileMergers) {
            OrganizedFileMerger *curr = &*it;
            MILO_ASSERT(curr->state != kPendingLoad, 0xCD);
        }
    }
    if (ofm->state == kFinishLoad) {
        ofm->merger->FinishLoading(ofm->merger->mCurLoader);
    } else if (ofm->state == kFailedLoad) {
        ofm->merger->FailedLoading(ofm->merger->mCurLoader);
    } else if (ofm->state == 0) {
        ofm->merger->LaunchNextLoader();
    } else {
        MILO_FAIL("Unknown dispatch state %d\n", ofm->state);
    }
    ofm->state = kPendingLoad;
}

void FileMergerOrganizer::RemoveFileMerger(FileMergerOrganizer::OrganizedFileMerger *ofm
) {
    MILO_ASSERT(!mActiveOrg, 0x116);
    if (ofm->merger->mCurLoader) {
        Dispatch(ofm);
    }
    if (!ofm->merger->mCurLoader) {
        ofm->merger->mOrganizer = ofm->merger;
        FOREACH (it, mOrganizedFileMergers) {
            if (&*it == ofm) {
                mOrganizedFileMergers.erase(it);
                break;
            }
        }
        mActiveOrg = nullptr;
        CheckDone();
    }
}

void FileMergerOrganizer::CheckDone() {
    MILO_ASSERT(mActiveOrg == NULL, 0x97);
    MILO_ASSERT(mStartOrg == NULL, 0x98);
    OrganizedFileMerger *o5 = nullptr;
    FileMerger::Merger *m4 = nullptr;
    FOREACH (it, mOrganizedFileMergers) {
        OrganizedFileMerger *curr = &*it;
        MILO_ASSERT(curr->state != kPendingLoad, 0xA3);
        FileMerger::Merger *front = FrontInactiveMerger(curr);
        if (!front) {
            RemoveFileMerger(curr);
            return;
        }
        FileMergerSort sort;
        if (!o5 || !sort(m4, front)) {
            o5 = curr;
            m4 = front;
        }
    }
    if (o5)
        Dispatch(o5);
}

void FileMergerOrganizer::AddFileMerger(FileMerger *fm) {
    if (!gOrganizing)
        fm->LaunchNextLoader();
    else {
        MILO_ASSERT(fm->mOrganizer == fm, 0xFD);
        MILO_ASSERT(fm->mCurLoader == NULL, 0xFE);
        FOREACH (it, mOrganizedFileMergers) {
            OrganizedFileMerger *curr = &*it;
            MILO_ASSERT(curr->merger != fm, 0x102);
        }
        fm->mOrganizer = this;
        OrganizedFileMerger ofm;
        ofm.merger = fm;
        ofm.state = (OrganizedState)0;
        mOrganizedFileMergers.push_back(ofm);
        if (mActiveOrg == 0 && !mStartOrg) {
            mStartOrg = new FileMergerOrganizerLoader();
        }
    }
}
