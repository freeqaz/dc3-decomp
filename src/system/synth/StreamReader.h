#pragma once

class StreamReader {
public:
    StreamReader() {}
    virtual ~StreamReader() {}
    virtual void Poll(float) = 0;
    virtual void Seek(int) = 0;
    virtual void EnableReads(bool) = 0;
    virtual bool Done() = 0;
    virtual bool Fail() = 0;
    // StreamReader deliberately does NOT declare Init(). The target's vtable is
    // six slots -- ??_7StreamReader@@6B@ runs 0x82263F0C..0x82263F24, where
    // ??_7WavReader@@6B@ minus 4 (that vtable's own RTTI locator word) begins,
    // and config/373307D9/symbols.txt records size:0x18 after the 2026-08-23
    // locator-extent repair. We used to emit a seventh slot holding _purecall.
    // Each reader introduces its own Init instead (WavReader.h:36,
    // VorbisReader.h:38, BinkReader.h:39, all protected, all landing at +0x18 of
    // their own vtables either way), and nothing dispatches Init through a
    // StreamReader*: StandardStream uses only Poll/Seek/EnableReads/Done/Fail
    // and the destructor -- exactly the six the target has.
#ifdef HX_NATIVE
    // The shared engine's FFmpegAudioReader (milo-native-engine
    // src/platform/FFmpegAudioReader.h) marks Init `override`, so without this
    // the engine fails to compile. That header SHADOWS dc3's own
    // native/src/platform/FFmpegAudioReader.h -- the engine's -I comes first --
    // so editing dc3's copy does nothing. Remove this guard once the engine
    // drops the `override`; it exists only so the PPC vtable can be correct
    // today. Same shape as the AsyncFile::GetFileHandle guard in os/AsyncFile.h.
    virtual void Init() = 0;
#endif
};
