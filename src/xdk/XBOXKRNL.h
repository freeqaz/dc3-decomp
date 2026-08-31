#pragma once

// size 0x8
struct _LIST_ENTRY {
    struct _LIST_ENTRY *Flink;
    struct _LIST_ENTRY *Blink;
};

// size 0x1C
struct _RTL_CRITICAL_SECTION {
    union {
        struct {
            unsigned char Type;
            unsigned char SpinCount;
            unsigned char Size;
            unsigned char Inserted;
            int SignalState;
            struct _LIST_ENTRY WaitListHead;
        } Event;
        struct {
            unsigned int SpinCount;
            void *Handle;
        } Usermode;
        unsigned int RawEvent[4];
    } Synchronization; // 0x0
    int LockCount; // 0x10
    int RecursionCount; // 0x14
    void *OwningThread; // 0x18
};

typedef struct _RTL_CRITICAL_SECTION RTL_CRITICAL_SECTION;

#ifdef __cplusplus
extern "C" {
#endif

void RtlInitializeCriticalSection(RTL_CRITICAL_SECTION *);
void RtlEnterCriticalSection(RTL_CRITICAL_SECTION *);
void RtlLeaveCriticalSection(RTL_CRITICAL_SECTION *);
int RtlTryEnterCriticalSection(RTL_CRITICAL_SECTION *);

#ifdef __cplusplus
}
#endif

/* RtlDeleteCriticalSection is the one member of this family that the shipped
   image does NOT contain: RtlInitialize/Enter/Leave/TryEnter all appear in
   ham_xbox_r.map, and RtlDeleteCriticalSection appears neither as a function
   nor as an __imp_ thunk.  ??1CriticalSection@@QAA@XZ *is* in the map, at
   0x823E3B70 -- the ICF address whose body is a bare `blr`.  So the original
   destructor emitted no call, which is what an inline no-op reproduces.
   (What we can prove is the absence of the call, not the XDK's exact spelling
   of it.)  Declared, not defined, it made CritSec.obj reference an external
   that nothing supplies, and link_glue.cpp bound it to __link_glue_noop. */
#ifdef HX_NATIVE
extern "C" void RtlDeleteCriticalSection(RTL_CRITICAL_SECTION *);
#else
#ifdef __cplusplus
inline void RtlDeleteCriticalSection(RTL_CRITICAL_SECTION *) {}
#else
/* C has no unnamed parameters in a definition, and a static definition in a
   header would warn in every TU that does not call it -- so, a macro. */
#define RtlDeleteCriticalSection(cs) ((void)(cs))
#endif
#endif
