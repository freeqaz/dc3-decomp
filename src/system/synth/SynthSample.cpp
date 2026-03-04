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
    (void)file;
    (void)line;
    (void)name;
    return MemAlloc(size, "SynthSample.cpp", 0x1c, "Sample Data", 0);
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

void SynthSample::Load(BinStream &bs) {
    PreLoad(bs);
    PostLoad(bs);
}

void SynthSample::PreLoad(BinStream &bs) {
    int revData;
    bs.ReadEndian(&revData, 4);
    int rev = revData & 0xffff;
    int altRev = (unsigned int)revData >> 0x10;
    if (rev > 6) {
        MILO_FAIL(
            "%s can't load new %s version %d > %d",
            PathName(this),
            ClassName(),
            rev,
            (unsigned short)6
        );
    }
    if (altRev > 0) {
        MILO_FAIL(
            "%s can't load new %s alt version %d > %d",
            PathName(this),
            ClassName(),
            altRev,
            (unsigned short)0
        );
    }
    if (rev > 1) {
        Hmx::Object::Load(bs);
    }
    bs >> mFile;
    // Rev <= 5 had loop fields (isLooped bool, loopStartSamp int if rev >= 3)
    if (rev <= 5) {
        bool isLooped;
        bs >> isLooped;
        if (rev >= 3) {
            int loopStartSamp;
            bs >> loopStartSamp;
        }
    }
    if (!bs.Cached() || rev < 5) {
        if (rev > 3 && !sDisabled) {
            Loader *loader = TheLoadMgr.AddLoader(mFile, kLoadFront);
            sLoader = dynamic_cast<FileLoader *>(loader);
            sLoading = this;
        }
    } else {
        mSampleData.Load(bs, mFile);
    }
}

void SynthSample::PostLoad(BinStream &bs) {
    sLoader = nullptr;
    sLoading = nullptr;
    Sync(bs.Cached() ? sync1 : sync0);
}

BEGIN_COPYS(SynthSample)
    COPY_SUPERCLASS(Hmx::Object)
    CREATE_COPY(SynthSample)
    BEGIN_COPYING_MEMBERS
        if (ty != kCopyFromMax) {
            COPY_MEMBER(mFile)
        }
    END_COPYING_MEMBERS
    Sync(sync0);
END_COPYS

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
    auto& _ref0 = mSampleData;
    HANDLE_EXPR(platform_size_kb, (_ref0.SizeAs(SampleData::kPCM) >> 10) + 0)
    HANDLE_EXPR(num_markers, _ref0.NumMarkers())
    HANDLE_EXPR(marker_name, _ref0.GetMarker(_msg->Int(2)).Name())
    HANDLE_EXPR(marker_sample, _ref0.GetMarker(_msg->Int(2)).Sample())
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
