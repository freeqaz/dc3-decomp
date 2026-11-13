// MSVC STL Compatibility Shims for Native Port
//
// Iterator fix: native/cmake/HostStlShim.cmake generates a copy of the host
// library's iterator header (libstdc++ __normal_iterator / libc++ __wrap_iter)
// that adds an implicit conversion to T*.
//
// This file provides additional compat shims for C++ features that
// differ between MSVC's old STL and modern libc++.

#pragma once

#if defined(HX_NATIVE) && defined(__cplusplus)

// std::random_shuffle was removed in C++17. libc++ enforces this.
// The DC3 codebase uses it in several places. Provide a compat shim.
#include <algorithm>
#include <cstdlib>

// Shim for std::random_shuffle, removed in C++17.
// Only needed when the standard library actually removed it. libc++ does (in
// C++17 mode). libstdc++ never has: every version keeps it, and mem_fun, as
// deprecated declarations unless _GLIBCXX_USE_DEPRECATED is 0. (The old test,
// __GLIBCXX__ < 20250101, assumed GCC 15 "re-added" it; on GCC 13/14 it
// defined a second random_shuffle and broke every TU -- the ubuntu-24.04 CI.)
#if defined(_LIBCPP_VERSION) ||                                                \
    (defined(__GLIBCXX__) && defined(_GLIBCXX_USE_DEPRECATED) && !_GLIBCXX_USE_DEPRECATED)
namespace std {
template <class RandomIt>
void random_shuffle(RandomIt first, RandomIt last) {
    for (auto i = last - first - 1; i > 0; --i) {
        auto j = std::rand() % (i + 1);
        std::swap(first[i], first[j]);
    }
}
} // namespace std
#endif

// Shim for std::mem_fun, removed in C++17. Same situation as random_shuffle.
#include <functional>
#if defined(_LIBCPP_VERSION) ||                                                \
    (defined(__GLIBCXX__) && defined(_GLIBCXX_USE_DEPRECATED) && !_GLIBCXX_USE_DEPRECATED)
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
