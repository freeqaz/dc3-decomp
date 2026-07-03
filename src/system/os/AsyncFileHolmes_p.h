#pragma once
#include "os/AsyncFile.h"

class AsyncFileHolmes : public AsyncFile {
public:
    AsyncFileHolmes(const char *, int);
    virtual ~AsyncFileHolmes();

#ifdef HX_NATIVE
    static void *operator new(size_t s) {
#else
    static void *operator new(unsigned int s) {
#endif
        return _MemAllocTemp(s, __FILE__, 0x14, "AsyncFile", 0);
    }
#ifdef HX_NATIVE
    static void *operator new(size_t s, void *place) { return place; }
#else
    static void *operator new(unsigned int s, void *place) { return place; }
#endif
    static void operator delete(void *v) { MemFree(v, __FILE__, 0x14, "AsyncFile"); }

protected:
    virtual bool Truncate(int);
    virtual void _OpenAsync();
    virtual bool _OpenDone() { return true; }
    virtual void _WriteAsync(const void *, int);
    virtual bool _WriteDone() { return true; }
    virtual void _SeekToTell();
    virtual void _ReadAsync(void *, int);
    virtual bool _ReadDone();
    virtual void _Close();

    int mFd; // 0x34
};
