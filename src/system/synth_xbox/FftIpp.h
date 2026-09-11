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

    pointer allocate(size_type count, const void *hint = 0) {
        return (pointer)MemAlloc(count * sizeof(T), "e:\\lazer_build_gmc1\\system\\src\\synth360\\synapse_apo\\common_vector.h", __LINE__, "synapse", 0);
    }

    void deallocate(pointer ptr, size_type) {
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
