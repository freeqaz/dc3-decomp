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
    /* PROVEN EXTRA VIRTUAL, deliberately left in place for now -- the last
     * remaining vtable-LENGTH finding in the whole binary.
     * ??_7StreamReader@@6B@ is 6 slots in the target (0x82263F0C..0x82263F24,
     * where ??_7WavReader@@6B@ - 4 begins; config/373307D9/symbols.txt now
     * records size:0x18 after the 2026-08-23 locator-extent repair).  We emit
     * a 7th slot holding _purecall, which is this Init.  Nothing dispatches it
     * through a StreamReader* -- StandardStream uses only
     * Poll/Seek/EnableReads/Done/Fail and the destructor, exactly the six the
     * target has -- and WavReader.h:36, VorbisReader.h:38 and BinkReader.h:39
     * each declare their own Init at slot +0x18 either way.
     * Removing it breaks the native build: FFmpegAudioReader in the SHARED
     * ../milo-native-engine repo (src/platform/FFmpegAudioReader.h:29) marks
     * Init `override`.  That header SHADOWS dc3's own
     * native/src/platform/FFmpegAudioReader.h -- the engine include path comes
     * first -- so editing dc3's copy does nothing.  Landing this needs the
     * `override` dropped in that repo plus a MILO_ENGINE_PIN bump, the same
     * cross-repo dependency as AsyncFile::GetFileHandle in os/AsyncFile.h. */
    virtual void Init() = 0;
};
