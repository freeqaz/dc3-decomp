#include "synth/SynthSample.h"
#include "SampleData.h"
#include "obj/Object.h"
#include "os/Debug.h"
#include "os/Platform.h"
#include "synth/SampleInst.h"
#include "utl/BinStream.h"
#include "utl/BufStream.h"
#include "utl/Loader.h"
#include "utl/MemMgr.h"

static void *SampleAlloc(int size, const char *file, int line, const char *name, int) {
    return MemAlloc(size, file, line, name, 0x20);
}

bool sDisabled;
SynthSample *SynthSample::sLoading;
FileLoader *SynthSample::sLoader;

void SynthSample::Init() {
    Register();
    SampleData::SetAllocator(SampleAlloc, MemFree);
}

void SynthSample::Disable() { sDisabled = true; }
int SynthSample::GetNumChannels() const { return mSampleData.NumChannels(); }
int SynthSample::GetSampleRate() const { return mSampleData.GetSampleRate(); }
std::vector<SampleMarker> &SynthSample::AccessMarkers() {
    return mSampleData.AccessMarkers();
}
int SynthSample::NumMarkers() const { return mSampleData.NumMarkers(); }
int SynthSample::GetPlatformSize(Platform) {
    return mSampleData.SizeAs(SampleData::kPCM);
}

void SynthSample::Sync(SyncType ty) {
    if (ty == sync0) {
        mSampleData.Reset();
        if (!sDisabled && !mFile.empty()) {
            FileLoader *fl = dynamic_cast<FileLoader *>(TheLoadMgr.ForceGetLoader(mFile));
            int i80;
            const char *cc;
            if (fl) {
                cc = fl->GetBuffer(&i80);
            } else
                cc = nullptr;
            delete fl;
            if (cc) {
                BufStream bs((void *)cc, i80, true);
                if (TheLoadMgr.GetPlatform() == kPlatformPC) {
                    mSampleData.LoadWAV(bs, mFile, false);
                } else {
                    mSampleData.Load(bs, mFile);
                }
                delete cc;
            }
        }
    }
}

BEGIN_SAVES(SynthSample)
    SAVE_REVS(6, 0)
    SAVE_SUPERCLASS(Hmx::Object)
    bs << mFile;
    if (bs.Cached()) {
        mSampleData.Save(bs);
    }
END_SAVES

void SynthSample::RegisterChild(SampleInst *inst) {
    mSampleInsts.push_back(inst);
}

void SynthSample::UnregisterChild(SampleInst *inst) {
    for (std::list<SampleInst *>::iterator it = mSampleInsts.begin();
         it != mSampleInsts.end(); ++it) {
        if (*it == inst) {
            mSampleInsts.erase(it);
            return;
        }
    }
    MILO_NOTIFY("Could not find child instance for unregister\n");
}

BEGIN_CUSTOM_PROPSYNC(SampleMarker)
    SYNC_PROP(sample, o.sample)
    SYNC_PROP(name, o.name)
END_CUSTOM_PROPSYNC

SynthSample::SynthSample() {}

SynthSample::~SynthSample() {
    while (!mSampleInsts.empty()) {
        SampleInst *inst = mSampleInsts.front();
        mSampleInsts.pop_front();
        delete inst;
    }

    if (sLoading == this) {
        RELEASE(sLoader);
        sLoading = nullptr;
    }
}

BEGIN_HANDLERS(SynthSample)
    HANDLE_EXPR(platform_size_kb, (mSampleData.SizeAs(SampleData::kPCM) >> 10) + 0)
    HANDLE_EXPR(num_markers, mSampleData.NumMarkers())
    HANDLE_EXPR(marker_name, mSampleData.GetMarker(_msg->Int(2)).Name())
    HANDLE_EXPR(marker_sample, mSampleData.GetMarker(_msg->Int(2)).Sample())
    HANDLE_EXPR(sample_length, (int)(LengthMs() * 0.001f))
    HANDLE_SUPERCLASS(Hmx::Object)
END_HANDLERS

BEGIN_PROPSYNCS(SynthSample)
    SYNC_PROP_MODIFY(file, mFile, Sync(sync0))
    SYNC_PROP_SET(
        sample_rate,
        mSampleData.GetSampleRate(),
        MILO_NOTIFY("can't set property %s", "sample_rate")
    )
    SYNC_PROP(markers, mSampleData.AccessMarkers())
    SYNC_SUPERCLASS(Hmx::Object)
END_PROPSYNCS
