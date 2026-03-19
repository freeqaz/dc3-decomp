// MSVC STL Compatibility Shims for Native Port
//
// Iterator fix: patched libc++ __wrap_iter.h in native/include/ adds
// implicit conversion from __wrap_iter<T*> to T*.
//
// This file provides additional compat shims for C++ features that
// differ between MSVC's old STL and modern libc++.

#pragma once

#if defined(HX_NATIVE) && defined(__cplusplus)

// std::random_shuffle was removed in C++17. libc++ enforces this.
// The DC3 codebase uses it in several places. Provide a compat shim.
#include <algorithm>
#include <cstdlib>

// libc++ (macOS) removes random_shuffle/mem_fun in C++17 mode.
// libstdc++ keeps them as deprecated. Only define shims when missing.
#if defined(_LIBCPP_VERSION)
namespace std {
template <class RandomIt>
void random_shuffle(RandomIt first, RandomIt last) {
    for (auto i = last - first - 1; i > 0; --i) {
        auto j = std::rand() % (i + 1);
        std::swap(first[i], first[j]);
    }
}
} // namespace std

#include <functional>
namespace std {
template <class Ret, class T>
auto mem_fun(Ret (T::*f)()) { return std::mem_fn(f); }
template <class Ret, class T>
auto mem_fun(Ret (T::*f)() const) { return std::mem_fn(f); }
template <class Ret, class T, class Arg>
auto mem_fun(Ret (T::*f)(Arg)) { return std::mem_fn(f); }
} // namespace std
#endif

#endif // HX_NATIVE
