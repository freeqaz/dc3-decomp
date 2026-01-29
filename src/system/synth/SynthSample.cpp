#include "synth/SynthSample.h"
#include "SampleData.h"
#include "obj/Object.h"
#include "os/Platform.h"
#include "utl/BinStream.h"
#include "utl/BufStream.h"
#include "utl/Loader.h"

bool sDisabled;

void SynthSample::Disable() { sDisabled = true; }
int SynthSample::GetNumChannels() const { return mSampleData.NumChannels(); }
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

BEGIN_CUSTOM_PROPSYNC(SampleMarker)
    SYNC_PROP(sample, o.sample)
    SYNC_PROP(name, o.name)
END_CUSTOM_PROPSYNC

SynthSample::SynthSample() {}

SynthSample::~SynthSample() {
    if (sLoading == this) {
        RELEASE(sLoader);
        sLoading = nullptr;
    }
}

DataNode SynthSample::Handle(DataArray *in, bool b) {
    Symbol sym = in->Sym(b);

    static Symbol sPlatformSizeKb("platform_size_kb");
    static Symbol sNumMarkers("num_markers");
    static Symbol sMarkerName("marker_name");
    static Symbol sMarkerSample("marker_sample");
    static Symbol sSampleLength("sample_length");
    static int sInitState = 0;

    Timer timer;
    if (MessageTimer::Active()) {
        timer.Restart();
    }

    if (!(sInitState & 1)) {
        sInitState |= 1;
        sPlatformSizeKb = Symbol("platform_size_kb");
    }
    if (sym == sPlatformSizeKb) {
        int size = mSampleData.SizeAs(SampleData::kPCM) >> 10;
        return DataNode(size + 0);
    }

    if (!(sInitState & 2)) {
        sInitState |= 2;
        sNumMarkers = Symbol("num_markers");
    }
    if (sym == sNumMarkers) {
        return mSampleData.NumMarkers();
    }

    if (!(sInitState & 4)) {
        sInitState |= 4;
        sMarkerName = Symbol("marker_name");
    }
    if (sym == sMarkerName) {
        int idx = in->Int(b);
        const SampleMarker &marker = mSampleData.GetMarker(idx);
        return DataNode(marker.Name());
    }

    if (!(sInitState & 8)) {
        sInitState |= 8;
        sMarkerSample = Symbol("marker_sample");
    }
    if (sym == sMarkerSample) {
        int idx = in->Int(b);
        const SampleMarker &marker = mSampleData.GetMarker(idx);
        return marker.Sample();
    }

    if (!(sInitState & 0x10)) {
        sInitState |= 0x10;
        sSampleLength = Symbol("sample_length");
    }
    if (sym == sSampleLength) {
        float len = LengthMs();
        return DataNode((int)(len * 0.001f));
    }

    DataNode ret = Hmx::Object::Handle(in, b);
    if (ret.Type() != kDataUnhandled) {
        return ret;
    }

    if (b) {
        const char *msg = MakeString("Unhandled msg: %s %s", PathName(this), sym);
        TheDebug.Notify(msg);
    }
    return DataNode(kDataUnhandled, 0);
}

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
