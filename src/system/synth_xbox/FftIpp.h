#pragma once

#include "utl\MemMgr.h"
#include <vector>

template <class T>
class XboxAllocator {
public:
    typedef std::size_t size_type;
    typedef std::ptrdiff_t difference_type;
    typedef T value_type;
    typedef T *pointer;
    typedef T &reference;
    typedef const T *const_pointer;
    typedef const T &const_reference;

    template <class T2>
    struct rebind {
        typedef XboxAllocator<T2> other;
    };

    // NEGATIVE RESULT (w7-as, 2026-09-14): the sibling
    // vector<float,XboxAllocator<float> >::_M_fill_assign sits at 94.4 canonical
    // on exactly one fact -- the image does NOT call get_allocator() there.  It
    // passes an UNINITIALISED r1+0x50 straight to _Vector_base(n, const alloc&)
    // (target `addi r5, r1, 0x50` / `addi r3, r1, 0x58` / `bl _Vector_base`),
    // i.e. get_allocator() was inlined to nothing; our build emits
    // `mr r4, r3` / `bl get_allocator` / `mr r5, r3` / `mr r4, r30`.  Stripping
    // the user-provided copy ctor here (so the empty class copies trivially) is
    // byte-for-byte inert -- 94.4 with the same four insert rows.  Stripping the
    // default ctor and dtor as well does not compile: stlport/stl/_vector.h:204
    // needs `allocator_type()` for the 3-arg vector ctor's default argument.
    // The remaining levers are both outside this header: stlport's _vector.c
    // spells `__tmp(__n, __val, get_allocator())` faithfully to upstream STLport
    // (and StlNodeAlloc -- structurally identical to this class, also empty with
    // the same three ctors -- DOES get an out-of-line get_allocator in the image,
    // 63 ICF-folded copies at 0x829526b0), so the divergence is an inlining
    // decision made in synth360's own TU, not a spelling this class controls.
    XboxAllocator() {}
    XboxAllocator(const XboxAllocator &) {}
    template <class T2>
    XboxAllocator(const XboxAllocator<T2> &) {}
    ~XboxAllocator() {}

    template <class T2>
    XboxAllocator &operator=(const XboxAllocator<T2> &) { return *this; }

    template <class T2>
    bool operator==(const XboxAllocator<T2> &) const { return true; }
    template <class T2>
    bool operator!=(const XboxAllocator<T2> &) const { return false; }

    pointer address(reference value) const { return &value; }
    const_pointer address(const_reference value) const { return &value; }
    size_type max_size() const { return size_type(-1) / sizeof(T); }

    // BUG FIX (w7-as, 2026-09-14): three things wrong here, all visible at the
    // inlined call site in vector<float,XboxAllocator<float> >::
    // _M_insert_overflow (0x828C6C5C..0x828C6C7C):
    //   * a zero-count guard -- `add. r26, r10, r11` / `beq` / `li r28, 0x0`;
    //     we called MemAlloc(0, ...) instead of returning NULL,
    //   * the ALIGNMENT argument is 0x10, not 0 (`li r7, 0x10`).  A float vector
    //     handed to the VMX/IPP spectral code was being 4-byte aligned,
    //   * the line number is 0x2f = 47 (`li r5, 0x2f`), which is where this call
    //     sits in the real common_vector.h; __LINE__ here is this file's line.
    pointer allocate(size_type count, const void *hint = 0) {
        if (count != 0)
            return (pointer)MemAlloc(count * sizeof(T), "e:\\lazer_build_gmc1\\system\\src\\synth360\\synapse_apo\\common_vector.h", 47, "synapse", 0x10);
        return 0;
    }

    void deallocate(pointer ptr, size_type) {
        // The image null-checks before freeing: `lwz r3, 0x0(r30)` /
        // `cmplwi cr6, r3, 0x0` / `beq` at 0x828C6CB0 in _M_insert_overflow.
        if (ptr)
            MemFree(ptr);
    }

    void construct(pointer ptr, const_reference value) { new (ptr) T(value); }
    void destroy(pointer ptr) { ptr->~T(); }
};

// `aligned_vector<T>` is the image's own name for this container, not a
// std::vector spelling: ham_xbox_r.map defines ??1?$aligned_vector@M@@QAA@XZ at
// 0x82e4a160 (synth_xbox:PitchDetector.obj) alongside the ordinary
// ?$vector@MV?$XboxAllocator@M@@@stlpmtx_std@@ members in SpectralAnalysis.obj,
// so both types are live in the build.  The allocator's own file literal names
// the header it came from -- common_vector.h.
template <class T>
class aligned_vector : public std::vector<T, XboxAllocator<T> > {};

class FftIpp {
public:
    void FftRealCcs(const float *__restrict, float *__restrict);
    void FftReal(const float *__restrict, float *__restrict, float *__restrict);
    ~FftIpp();
    FftIpp();
    void SetMode(int);

    int mSize;
    int mOrder;
    aligned_vector<float> mBuf1;   // 0x08
    aligned_vector<float> mBuf2;   // 0x14
    aligned_vector<float> mBuf3;   // 0x20
    aligned_vector<float> mBuf4;   // 0x2C
    aligned_vector<float> mSinCos; // 0x38
};
