// Memory-safety defects found by running the native gameplay routes
// (scripts/native_assert_harvest.py) against an AddressSanitizer build of
// dc3-native.  Branch `native-asan`.  One defect per test.
//
// Several of these are READS OF UNINITIALISED MEMORY rather than classic
// use-after-free: ASan's allocator fills every new block with 0xbe
// (malloc_fill_byte), where glibc usually hands a young process zeroed pages.
// A member the image never initialises because retail code always assigns it
// later reads as null under glibc and as 0xbebebebe... under ASan.  The tests
// reproduce that by constructing the object in a buffer pre-filled with 0xbe.
#include "test_helpers.h"

#include "meta_ham/ShellInput.h"

#include <cstdlib>
#include <cstring>
#include <new>

namespace {

class NativeAsanTest : public EngineTestFixture {};

// Construct T in a heap block pre-filled with 0xbe, as ASan's allocator does.
template <class T> T *NewInGarbage() {
    void *mem = std::malloc(sizeof(T));
    std::memset(mem, 0xbe, sizeof(T));
    return ::new (mem) T;
}

template <class T> void DeleteInGarbage(T *obj) {
    obj->~T();
    std::free(obj);
}

// ShellInput::ShellInput (100% matched) stores 0xc8/0xcc/0xd0/0xdc and leaves
// mHandInvokeGestureFilter (0xd4) and mHandsUpGestureFilter (0xd8) alone,
// because retail Init always assigns both.  The native Init arm creates
// neither, and native Poll() tests them for null -- so without an explicit
// null in Init, Poll() read garbage.  ASan run, perform route: SIGSEGV in
// ShellInput::Poll (ShellInput.cpp:174, HandsUpGestureFilter::GetHandsUp) on
// the first frame of attract_screen.
TEST_F(NativeAsanTest, ShellInputInitNullsTheGestureFiltersItDoesNotCreate) {
    // Init's Find("cursor_panel") FAILs (non-fatally) here: the test engine
    // loads no UI.  The native arm null-checks the result.
    ShellInput *si = NewInGarbage<ShellInput>();
    si->Init();
    EXPECT_EQ(si->mHandsUpGestureFilter, nullptr);
    EXPECT_EQ(si->mHandInvokeGestureFilter, nullptr);
    DeleteInGarbage(si);
}

} // namespace
