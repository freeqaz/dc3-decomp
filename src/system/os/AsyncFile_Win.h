#pragma once
#include "os/AsyncFile.h"
#include "utl/MemMgr.h"
#include "xdk/XAPILIB.h"

class AsyncFileWin : public AsyncFile {
public:
    AsyncFileWin(const char *, int);
    virtual ~AsyncFileWin();

#ifdef HX_NATIVE
    static void *operator new(size_t s) {
#else
    static void *operator new(unsigned int s) {
#endif
        return _MemAllocTemp(s, __FILE__, 0x17, "AsyncFile", 0);
    }
#ifdef HX_NATIVE
    static void *operator new(size_t s, void *place) { return place; }
#else
    static void *operator new(unsigned int s, void *place) { return place; }
#endif
    static void operator delete(void *v) { MemFree(v, __FILE__, 0x17, "AsyncFile"); }

protected:
    virtual bool Truncate(int);
    virtual void _OpenAsync();
    virtual bool _OpenDone() { return true; }
    virtual void _WriteAsync(const void *, int);
    virtual bool _WriteDone();
    virtual void _SeekToTell();
    virtual void _ReadAsync(void *, int);
    virtual bool _ReadDone();
    virtual void _Close();

    int mSectorBytes; // 0x34
    HANDLE mFile; // 0x38
    int mFd; // 0x3c
    bool mReadInProgress; // 0x40
    bool mWriteInProgress; // 0x41
    OVERLAPPED mOverlapped; // 0x44
    bool unk58;
    void *unk5c;
    void *unk60;
    int unk64;
    int unk68;
};
