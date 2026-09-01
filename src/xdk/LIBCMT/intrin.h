#pragma once

// MSVC's Interlocked* compiler intrinsics.
//
// These are recognised by name by the Xenon front end -- `strings c1xx.dll`
// lists InterlockedIncrement/Decrement/ExchangeAdd/CompareExchange in its
// intrinsic table -- so a prototype plus `#pragma intrinsic` is all that is
// needed; there is no library body behind them.
//
// MEASURED: on this toolchain (X360/16.00.11886.00, /O1 /Oi) the lowering is
// NOT a bare lwarx/stwcx. loop. It is the interrupt-masked form the shipped
// image uses:
//
//     mfmsr  r10
//     mtmsrd r13, 1        ; r13 carries the EE-cleared MSR image
//     lwarx  r11, r0, r3
//     addi   r11, r11, 1
//     stwcx. r11, r0, r3
//     mtmsrd r10, 1        ; restore
//     bne    retry
//
// which is instruction-identical to CXAPOBase::Release @ 0x82E2D520 and
// CXAPOParametersBase::QueryInterface @ 0x82E2D598 modulo register allocation.
//
// Deliberately NOT placed in ppcintrinsics.h: that header is reached through
// the PCH (os/Timer.h includes it), so every declaration added there rebuilds
// 574 translation units. This one is included only where it is used.

#ifndef HX_NATIVE

extern "C" {
long _InterlockedIncrement(long volatile *);
long _InterlockedDecrement(long volatile *);
long _InterlockedExchange(long volatile *, long);
long _InterlockedExchangeAdd(long volatile *, long);
long _InterlockedCompareExchange(long volatile *, long, long);
}

#pragma intrinsic(_InterlockedIncrement)
#pragma intrinsic(_InterlockedDecrement)
#pragma intrinsic(_InterlockedExchange)
#pragma intrinsic(_InterlockedExchangeAdd)
#pragma intrinsic(_InterlockedCompareExchange)

#else

// Host build: the PPC reservation idiom has no meaning, but the semantics do.
inline long _InterlockedIncrement(long volatile *p) {
    return __sync_add_and_fetch(p, 1);
}
inline long _InterlockedDecrement(long volatile *p) {
    return __sync_sub_and_fetch(p, 1);
}
inline long _InterlockedExchange(long volatile *p, long v) {
    return __sync_lock_test_and_set(p, v);
}
inline long _InterlockedExchangeAdd(long volatile *p, long v) {
    return __sync_fetch_and_add(p, v);
}
inline long _InterlockedCompareExchange(long volatile *p, long xchg, long cmp) {
    return __sync_val_compare_and_swap(p, cmp, xchg);
}

#endif
