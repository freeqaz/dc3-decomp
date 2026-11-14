cmake_minimum_required(VERSION 3.22)

# This project requires Clang due to MSVC compatibility flags (-fms-extensions, etc.)
# (Emscripten provides its own Clang — skip manual detection)
if(NOT EMSCRIPTEN)
    if(NOT DEFINED CMAKE_C_COMPILER AND NOT DEFINED ENV{CC})
        find_program(CLANG_C_COMPILER clang)
        if(CLANG_C_COMPILER)
            set(CMAKE_C_COMPILER "${CLANG_C_COMPILER}" CACHE FILEPATH "C compiler" FORCE)
        endif()
    endif()
    if(NOT DEFINED CMAKE_CXX_COMPILER AND NOT DEFINED ENV{CXX})
        find_program(CLANG_CXX_COMPILER clang++)
        if(CLANG_CXX_COMPILER)
            set(CMAKE_CXX_COMPILER "${CLANG_CXX_COMPILER}" CACHE FILEPATH "C++ compiler" FORCE)
        endif()
    endif()
endif()

project(dc3-native LANGUAGES C CXX)

# Verify we're using Clang (the MSVC compat flags won't work with GCC)
# Emscripten's compiler identifies as Clang, so this check passes.
if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    message(FATAL_ERROR
        "This project requires Clang (found ${CMAKE_CXX_COMPILER_ID}).\n"
        "Configure with: CC=clang CXX=clang++ cmake ..")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# LP64 audit mode: enable pointer-to-int truncation warnings
# Usage: cmake -DLP64_AUDIT=ON .. && make 2>&1 | grep -E 'pointer-to-int|int-to-pointer'
option(LP64_AUDIT "Enable LP64 pointer truncation warnings for auditing" OFF)

# Address Sanitizer — catches heap corruption, use-after-free, buffer overflows
# Usage: cmake -DENABLE_ASAN=ON .. && make
# Run with: ASAN_OPTIONS=detect_leaks=0 ./dc3-native
option(ENABLE_ASAN "Enable AddressSanitizer for memory error detection" OFF)
if(ENABLE_ASAN)
    add_compile_options(-fsanitize=address -fno-omit-frame-pointer)
    add_link_options(-fsanitize=address)
endif()

# Compiler cache (ccache) — set by scripts/native_configure.sh as
# CMAKE_<LANG>_COMPILER_LAUNCHER; see docs/native/BUILD_CACHE.md. Clang stamps
# the mtime of every header into a .pch, which makes every PCH unique and every
# TU that uses one a guaranteed cache miss. -fno-pch-timestamp drops the stamp
# (ninja's depfiles, not clang's mtime check, are what keep a PCH current here).
# Directory scope, before add_subdirectory(milo-native-engine), so the engine's
# PCH gets it too.
if(CMAKE_CXX_COMPILER_LAUNCHER MATCHES "ccache")
    add_compile_options("$<$<COMPILE_LANGUAGE:CXX>:SHELL:-Xclang -fno-pch-timestamp>")
endif()

# MSVC compatibility flags — applied per-target, not globally.
set(DC3_MSVC_COMPAT_FLAGS
    -fms-extensions          # __declspec support (xdk headers)
    -fms-compatibility       # Full MSVC compat (dependent base class lookup)
    -fms-compatibility-version=19.29  # VS2019+ — char16_t/char32_t as builtins
    -fdelayed-template-parsing  # MSVC-style template parsing
    # GCC 15 headers: clang in MSVC mode drops __GNUC__ macros, breaking
    # glibc's sys/cdefs.h and libstdc++'s atomic_base.h. These defines restore
    # the compiler builtins that GCC 15 headers expect.
    # Not needed on macOS (uses libc++, not libstdc++).
    $<$<NOT:$<PLATFORM_ID:Darwin>>:-D__GNUC_STDC_INLINE__>
    $<$<NOT:$<PLATFORM_ID:Darwin>>:-D__GCC_ATOMIC_TEST_AND_SET_TRUEVAL=1>
    # Same class of bug as the two above, and it produced an undefined symbol
    # rather than a compile error. -fms-compatibility makes clang stop defining
    # __GXX_WEAK__; libstdc++'s <typeinfo> reads that macro to decide whether
    # std::type_info::operator== is inline, so in MSVC mode it becomes an
    # out-of-line call to _ZNKSt9type_infoeqERKS_ -- which libstdc++.so.6.0.36
    # does not export. Every MS-compat TU comparing type_info (std::regex
    # collation, shared_ptr's _M_get_deleter) emitted a reference nothing could
    # satisfy. <typeinfo> documents this macro as the supported override.
    # Reproduce: clang++ -std=gnu++17 -fms-compatibility on `a == b` over two
    # type_infos leaves an undefined reference; adding this define removes it.
    # This also makes MS-compat TUs agree with the rest of the build, which was
    # already using the inline form.
    $<$<NOT:$<PLATFORM_ID:Darwin>>:-D__GXX_TYPEINFO_EQUALITY_INLINE=1>
    # macOS: -fms-compatibility changes __builtin_va_list from void* to char*,
    # conflicting with the SDK's va_list typedef. Force-include <stdarg.h> so
    # clang's definition wins first, then suppress the SDK's redefinition.
    $<$<PLATFORM_ID:Darwin>:-include>
    $<$<PLATFORM_ID:Darwin>:stdarg.h>
    $<$<PLATFORM_ID:Darwin>:-D_VA_LIST_T>
)
# GCC 15 <atomic>/<thread>/<mutex> need __GCC_ATOMIC_*_LOCK_FREE builtins,
# which clang drops in -fms-compatibility mode. Define them all (=2 on x86_64).
# Not needed on macOS (uses libc++, not libstdc++).
if(NOT APPLE)
    set(DC3_ATOMIC_COMPAT_FLAGS
        -D__GCC_ATOMIC_BOOL_LOCK_FREE=2
        -D__GCC_ATOMIC_CHAR_LOCK_FREE=2
        -D__GCC_ATOMIC_WCHAR_T_LOCK_FREE=2
        -D__GCC_ATOMIC_CHAR16_T_LOCK_FREE=2
        -D__GCC_ATOMIC_CHAR32_T_LOCK_FREE=2
        -D__GCC_ATOMIC_SHORT_LOCK_FREE=2
        -D__GCC_ATOMIC_INT_LOCK_FREE=2
        -D__GCC_ATOMIC_LONG_LOCK_FREE=2
        -D__GCC_ATOMIC_LLONG_LOCK_FREE=2
        -D__GCC_ATOMIC_POINTER_LOCK_FREE=2
    )
else()
    set(DC3_ATOMIC_COMPAT_FLAGS "")
endif()
set(DC3_DECOMP_FLAGS
    -ferror-limit=0
    -fno-omit-frame-pointer
    "SHELL:-include ${CMAKE_SOURCE_DIR}/src/msvc_compat.h"
)
if(LP64_AUDIT)
    list(APPEND DC3_DECOMP_FLAGS -Wno-everything -Wpointer-to-int-cast -Wint-to-pointer-cast)
else()
    list(APPEND DC3_DECOMP_FLAGS -w)
endif()

# Vendored single-header libraries (httplib, cgltf, stb).
include_directories(BEFORE SYSTEM ${CMAKE_SOURCE_DIR}/include)

# MSVC STL compat: shadow the host library's vector iterator header
# (libstdc++ bits/stl_iterator.h or libc++ __iterator/wrap_iter.h) with a copy
# that adds an implicit conversion to the underlying pointer, matching Xbox 360
# MSVC's STL where vector::begin()/end() are T* (600+ call sites). The copy is
# GENERATED from this toolchain's own header -- a checked-in copy of one
# version's header is what broke CI on GCC 14 and libc++ 23; see
# cmake/HostStlShim.cmake. Non-SYSTEM and BEFORE so it precedes every other
# -I, including the engine's own include/ copy (re-prepended on milo-engine
# below, after add_subdirectory).
list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")
include(HostStlShim)
dc3_generate_host_stl_shim("${CMAKE_BINARY_DIR}/host_stl_shim" DC3_HOST_STL_SHIM_DIR)
include_directories(BEFORE ${DC3_HOST_STL_SHIM_DIR})

if(EMSCRIPTEN)
    # Emscripten provides its own threading, WebGPU (emdawnwebgpu), and GLFW shim
    set(ENABLE_FFMPEG OFF)
else()
    find_package(Threads REQUIRED)
    find_package(Dawn REQUIRED)
    find_package(glfw3 REQUIRED)
endif()

# Dear ImGui — debug UI overlay for native + web builds
include(FetchContent)
FetchContent_Declare(imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG        8314fc3e5a10f7c6b670225065fce1dc8cfd396b  # post-v1.91.8, supports Dawn WebGPU API renames
)
FetchContent_MakeAvailable(imgui)
if(NOT EMSCRIPTEN)
    # Desktop: GLFW + Dawn backends
    add_library(imgui STATIC
        ${imgui_SOURCE_DIR}/imgui.cpp
        ${imgui_SOURCE_DIR}/imgui_draw.cpp
        ${imgui_SOURCE_DIR}/imgui_tables.cpp
        ${imgui_SOURCE_DIR}/imgui_widgets.cpp
        ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
        ${imgui_SOURCE_DIR}/backends/imgui_impl_wgpu.cpp
    )
    target_include_directories(imgui PUBLIC
        ${imgui_SOURCE_DIR}
        ${imgui_SOURCE_DIR}/backends
    )
    target_link_libraries(imgui PUBLIC glfw dawn::webgpu_dawn)
    target_compile_definitions(imgui PUBLIC IMGUI_IMPL_WEBGPU_BACKEND_DAWN)
endif()
# Under EMSCRIPTEN the engine's MILO_BUILD_WEB block stands up the `imgui`
# target (WebGPU backend only, --use-port=emdawnwebgpu). DC3 just links it.

# FFmpeg — used for Bink video/audio decoding (replaces RAD Bink SDK)
option(ENABLE_FFMPEG "Enable FFmpeg for Bink video/audio decoding" ON)
if(ENABLE_FFMPEG AND NOT EMSCRIPTEN)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(FFMPEG REQUIRED libavformat libavcodec libswscale libavutil)
endif()

# The in-process ncnn pose path (native/src/pose/, ENABLE_NCNN) was removed
# 2026-11-13 (docs/native/PLAN.md P-6): its YOLO-Pose model was retired in
# 23f92cb78, so it failed on every boot before the MediaPipe pose server was
# tried. Pose input is native/scripts/pose_server.py over the socket, or
# PoseInput_Web.cpp on web. Drop the options an older configure cached, so an
# existing build dir configures exactly like a fresh one.
unset(ENABLE_NCNN CACHE)
unset(ncnn_DIR CACHE)

# Platform-specific linker flags and libraries (Linux vs macOS)
if(NOT EMSCRIPTEN)
    if(APPLE)
        # macOS: Apple's default linker (ld-prime, Xcode 15+), with no flags
        # that relax it.
        #
        # Until 2026-11-13 this was `-ld_classic -multiply_defined,suppress
        # -undefined,dynamic_lookup`. The first two existed only to tolerate
        # duplicate strong definitions; there are none now (PLAN M-4: dc3 and
        # engine 4e738fe, gated on Linux by ctest LinkHygiene.*), and
        # -ld_classic is deprecated and slated for removal from Xcode. The
        # third turned every undefined symbol into a runtime crash on first
        # call, which is exactly what Linux's --no-undefined was added to stop
        # (see the Linux branch below); the libc++ proxy build links with zero
        # undefined symbols. DC3_MAC_DYNAMIC_LOOKUP=ON brings it back as a
        # stopgap if a Mac link lists symbols nobody has defined yet.
        option(DC3_MAC_DYNAMIC_LOOKUP
            "macOS: link with -undefined dynamic_lookup (missing symbols crash at first call)" OFF)
        set(DC3_PLATFORM_LINK_OPTIONS "")
        if(DC3_MAC_DYNAMIC_LOOKUP)
            message(WARNING "DC3_MAC_DYNAMIC_LOOKUP=ON: undefined symbols will link and crash on first call")
            list(APPEND DC3_PLATFORM_LINK_OPTIONS -Wl,-undefined,dynamic_lookup)
        endif()
        # macOS needs ObjC++ for MetalSurface.mm
        enable_language(OBJCXX)
        # Homebrew library/include paths: $HOMEBREW_PREFIX (set by `brew
        # shellenv`), else the Apple Silicon default.
        if(DEFINED ENV{HOMEBREW_PREFIX})
            set(DC3_HOMEBREW_PREFIX "$ENV{HOMEBREW_PREFIX}")
        else()
            set(DC3_HOMEBREW_PREFIX /opt/homebrew)
        endif()
        include_directories(SYSTEM ${DC3_HOMEBREW_PREFIX}/include)
        link_directories(${DC3_HOMEBREW_PREFIX}/lib)
        set(DC3_PLATFORM_LIBS
            Threads::Threads
            dawn::webgpu_dawn
            glfw
            curl
            jpeg
            vorbis
            vorbisfile
            ogg
            z
            c++
            c++abi
            "-framework Cocoa"
            "-framework IOKit"
            "-framework Metal"
            "-framework QuartzCore"
        )
    else()
        # Linux: GNU ld flags
        #
        # --no-undefined (== -z defs) is load-bearing. It replaced
        # `--unresolved-symbols=ignore-all` + `--warn-unresolved-symbols`, a
        # pair that between them made the link incapable of reporting a missing
        # symbol at all:
        #
        #   * --unresolved-symbols=ignore-all SUPPRESSES
        #     --warn-unresolved-symbols, so the link was completely silent.
        #     Measured: 24 symbols were undefined in dc3-native with zero
        #     diagnostics.
        #   * --warn-unresolved-symbols DEMOTES --no-undefined's errors back to
        #     warnings. Adding --no-undefined while leaving the warn flag in
        #     place still links successfully and still produces a binary. Both
        #     had to go for the check to have any effect. Do not re-add it.
        #
        # Undefined symbols here are not a theoretical concern: they are lazily
        # bound, so the link succeeds, the binary boots, and the process dies
        # with `symbol lookup error` / exit 127 on the FIRST call. Verified on
        # the pre-fix binary with `LD_BIND_NOW=1 ./dc3-native` (exit 127 on
        # BinkRegisterFrameBuffers).
        #
        # If this fires, add a definition (native/src/xbox_link_stubs.cpp is the
        # home for Xbox-only entry points). Do not silence it — a green link is
        # the only thing standing between a missing symbol and a crash on a code
        # path nobody has walked yet.
        #
        # --allow-multiple-definition stays: the decomp has intentional
        # stub/real symbol pairs. Note it is why weak stubs can shadow real
        # bodies; scripts/native/check_stub_shadow.py is the gate for that
        # separate failure mode.
        set(DC3_PLATFORM_LINK_OPTIONS
            -rdynamic
            -Wl,--no-undefined
            -Wl,--allow-multiple-definition
        )
        set(DC3_PLATFORM_LIBS
            Threads::Threads
            dawn::webgpu_dawn
            glfw
            dl
            curl
            jpeg
            vorbis
            vorbisfile
            ogg
            z
        )
    endif()
endif()

# ============================================================================
# milo-native-engine (shared LP64 engine) — consumed by dc3-native via the
# engine-clean gfx/audio/platform source set compiled into libmilo-engine.a.
# Sibling repo + soft SHA pin (warn on mismatch). See
# rb3/docs/native/NATIVE_PORT_ROADMAP.md.
# ============================================================================
# MILO_ENGINE_PATH stays overridable (-DMILO_ENGINE_PATH=... must keep working),
# so no FORCE here -- but the default has to be right in a git worktree too. A
# worktree lives at <...>/wt/<name>, where the "sibling of the checkout" guess
# lands in the worktree pool rather than next to the main checkout.
set(_milo_engine_default "${CMAKE_SOURCE_DIR}/../../milo-native-engine")
if(NOT EXISTS "${_milo_engine_default}/.git")
    execute_process(COMMAND git rev-parse --path-format=absolute --git-common-dir
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        OUTPUT_VARIABLE _milo_git_common OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET)
    if(_milo_git_common)
        get_filename_component(_milo_main_checkout "${_milo_git_common}" DIRECTORY)
        get_filename_component(_milo_repos_root "${_milo_main_checkout}" DIRECTORY)
        if(EXISTS "${_milo_repos_root}/milo-native-engine/.git")
            set(_milo_engine_default "${_milo_repos_root}/milo-native-engine")
        endif()
    endif()
endif()
set(MILO_ENGINE_PATH "${_milo_engine_default}"
    CACHE PATH "Path to milo-native-engine checkout")
# MILO_ENGINE_PIN is a SOURCE-OF-TRUTH value, not a user setting: bumping the
# line below (by hand or via scripts/bump-engine.sh --apply) has to take effect.
# A plain `set(... CACHE STRING ...)` does the exact opposite -- once ANY value
# lands in CMakeCache.txt the source line can never be read again. The
# 2026-08-19 toolchain audit caught this: four different pin values were live at
# once (engine HEAD, this file, native/build's cache, CLAUDE.md), setting the
# source pin to the engine's actual HEAD still warned quoting the cached value,
# and `bump-engine.sh --apply` was a no-op against every existing build dir.
# FORCE makes the source win on every reconfigure, including over a stale cache.
# Consequence, deliberately accepted: `-DMILO_ENGINE_PIN=...` on the cmake
# command line no longer sticks. There is no legitimate reason to override a
# provenance pin from the command line; edit this line instead.
set(MILO_ENGINE_PIN "c8abfc0b9b377db695377cc9b4b57d9b1b0cefa2"
    CACHE STRING "Engine commit SHA verified against" FORCE)
if(EXISTS "${MILO_ENGINE_PATH}/.git")
    execute_process(COMMAND git rev-parse HEAD
        WORKING_DIRECTORY ${MILO_ENGINE_PATH}
        OUTPUT_VARIABLE _milo_engine_head OUTPUT_STRIP_TRAILING_WHITESPACE)
    # SOFT pin, by design: this warns and never fails. Do not promote it to
    # FATAL_ERROR -- the engine is a live sibling checkout and a mismatch is the
    # normal state mid-development.
    if(NOT _milo_engine_head MATCHES "^${MILO_ENGINE_PIN}")
        message(WARNING
            "milo-native-engine HEAD is ${_milo_engine_head}\n"
            "  but dc3-native pins ${MILO_ENGINE_PIN}\n"
            "  (update MILO_ENGINE_PIN once the new engine state is canonical:\n"
            "   scripts/bump-engine.sh --apply)")
    endif()
endif()

# Inject DC3's matched-fork context so the engine compiles against DC3's Milo
# headers with the MSVC-PPC compat flags + shared PCH.
set(MILO_ENGINE_DECOMP_INCLUDE_DIRS
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/../src
    ${CMAKE_SOURCE_DIR}/../src/system
    ${CMAKE_SOURCE_DIR}/../src/system/oggvorbis
    ${CMAKE_SOURCE_DIR}/../src/lazer
    ${CMAKE_SOURCE_DIR}/../include)
set(MILO_ENGINE_DECOMP_COMPAT_FLAGS
    ${DC3_MSVC_COMPAT_FLAGS} ${DC3_ATOMIC_COMPAT_FLAGS} ${DC3_DECOMP_FLAGS})
# The PCH header is named in ANGLE-BRACKET form, found through the targets'
# include path (../src/system), on purpose: CMake writes the header's name
# verbatim into the generated cmake_pch.hxx that every PCH user includes. A full
# path there is a different file in every worktree, so no TU that uses the PCH
# could ever be a ccache hit from another worktree (measured: the only
# difference between two trees' hash inputs was that 2-byte-longer line).
# $<ANGLE-R>, not '>': every use wraps this in $<$<COMPILE_LANGUAGE:CXX>:...>,
# and a bare '>' would close that genex early (C TUs then got `#include ">"`).
set(DC3_PCH_HEADER "<decomp_pch.h$<ANGLE-R>")
set(MILO_ENGINE_DECOMP_PCH "${DC3_PCH_HEADER}")
# GPU backend flavor: DC3's WgpuRnd : NgRnd + the rndobj-coupled gfx TUs. This is
# the engine's default when a consumer names no backend, but set it explicitly
# (the engine now also ships a `rb3` BandRnd flavor — see the engine CMakeLists
# MILO_ENGINE_GPU_BACKEND option).
set(MILO_ENGINE_GPU_BACKEND dc3 CACHE STRING "DC3: WgpuRnd : NgRnd GPU backend" FORCE)
# Web build: opt into the engine's MILO_BUILD_WEB platform layer (sources
# live in milo-native-engine/src/{platform,audio,gfx}/*_Web.cpp, plus the
# engine-side EMSCRIPTEN imgui target and audio-worklet.js / missing_stubs.js
# assets). dc3-web now consumes the engine helpers
# milo_engine_apply_web_target_options() + milo_engine_set_web_canvas_selector()
# instead of the inlined link/compile workaround from W0 (engine fixed the
# CMAKE_CURRENT_SOURCE_DIR-inside-function bug + added a canvas-selector
# header-routing helper so `#dc3-canvas` no longer needs a per-consumer
# configure_file).
#
# We exclude the *_Web.cpp sources from libmilo-engine.a because dc3-web
# compiles them DIRECTLY (via DC3_WEB_LIFTED_ENGINE_SOURCES) with the
# DC3-target compile flags + force-included header. Building them twice
# (once in the library, once in dc3-web) would emit duplicate symbols at
# link time, and the library version fails to compile under the engine's
# PCH because the PCH locks in <sys/stat.h>'s st_mtime macro state.
if(EMSCRIPTEN)
    set(MILO_BUILD_WEB ON CACHE BOOL "Engine web platform layer (lifted from DC3 in W0)" FORCE)
    list(APPEND MILO_ENGINE_DECOMP_PLATFORM_EXCLUDE
        File_Web.cpp
        CDReader_Web.cpp
        WebAssets.cpp
        GpuDevice_Web.cpp
        AudioDevice_Web.cpp
        ImGuiBackend_Web.cpp)
endif()
# FFmpegMovieImpl.cpp (H-5): the engine's copy links. DC3 used to compile its
# own and exclude the engine's, because only DC3's carried the image-faithful
# fixes (Ready() is true with no pending load; Poll() of a movie that never
# opened reports DONE, so a missing .bik no longer "plays" forever) and G-15's
# File-layer AVIO. The engine carries them now (engine-stack "FFmpegMovieImpl:
# dc3's movie fixes (H-5 + G-15)"), so DC3's copy is deleted and there is
# exactly one definition.
add_subdirectory(${MILO_ENGINE_PATH} ${CMAKE_BINARY_DIR}/milo-engine)
# The engine generates its own copy of the same shim (cmake/MiloHostStlShim.cmake,
# BEFORE PUBLIC) from the same toolchain header, so the two are identical.
# Prepending dc3's again keeps the engine's TUs on exactly the header dc3's own
# TUs see, whichever generator runs first. (Consumers already get it first from the
# directory property; this is PRIVATE so it does not duplicate there.)
target_include_directories(milo-engine BEFORE PRIVATE ${DC3_HOST_STL_SHIM_DIR})
if(EMSCRIPTEN)
    # dc3-web TUs compile with HX_WEB=1 and link libmilo-engine.a; the engine's
    # StreamReceiver_Native.h and audio/AudioDevice.h change class layout under
    # HX_WEB, so the archive must see the same define or the two sides disagree
    # on those objects' layout (and StreamReceiverNative::SetDebugLabel is
    # never compiled).
    target_compile_definitions(milo-engine PRIVATE HX_WEB=1)
endif()

# ============================================================================
# Source files from decomp (generated from config/373307D9/objects.json)
# Excludes: xdk/, lib/binkxenon, synth_xbox/, rnddx9/, *_Xbox.cpp, *_Win.cpp
# ============================================================================

set(DC3_ENGINE_SOURCES
    ${CMAKE_SOURCE_DIR}/../src/App.cpp
    ${CMAKE_SOURCE_DIR}/../src/lazer/game/BustAMovePanel.cpp
    ${CMAKE_SOURCE_DIR}/../src/lazer/game/Game.cpp
    ${CMAKE_SOURCE_DIR}/../src/lazer/game/GameMode.cpp
    ${CMAKE_SOURCE_DIR}/../src/lazer/game/GamePanel.cpp
    ${CMAKE_SOURCE_DIR}/../src/lazer/game/HamUser.cpp
    ${CMAKE_SOURCE_DIR}/../src/lazer/game/HamUserMgr.cpp
    ${CMAKE_SOURCE_DIR}/../src/lazer/game/LiveInput.cpp
    ${CMAKE_SOURCE_DIR}/../src/lazer/game/PartyModeMgr.cpp
    ${CMAKE_SOURCE_DIR}/../src/lazer/game/PresenceMgr.cpp
    ${CMAKE_SOURCE_DIR}/../src/lazer/game/Shuttle.cpp
    ${CMAKE_SOURCE_DIR}/../src/lazer/game/SongDB.cpp
    ${CMAKE_SOURCE_DIR}/../src/lazer/game/SongSequence.cpp
    ${CMAKE_SOURCE_DIR}/../src/lazer/meta_ham/Accomplishment.cpp
    ${CMAKE_SOURCE_DIR}/../src/lazer/meta_ham/AccomplishmentCampaignConditional.cpp
    ${CMAKE_SOURCE_DIR}/../src/lazer/meta_ham/AccomplishmentCategory.cpp
    ${CMAKE_SOURCE_DIR}/../src/lazer/meta_ham/AccomplishmentCharacterListConditional.cpp
    ${CMAKE_SOURCE_DIR}/../src/lazer/meta_ham/AccomplishmentConditional.cpp
    ${CMAKE_SOURCE_DIR}/../src/lazer/meta_ham/AccomplishmentCountConditional.cpp
    ${CMAKE_SOURCE_DIR}/../src/lazer/meta_ham/AccomplishmentDiscSongConditional.cpp
    ${CMAKE_SOURCE_DIR}/../src/lazer/meta_ham/AccomplishmentGroup.cpp
    ${CMAKE_SOURCE_DIR}/../src/lazer/meta_ham/AccomplishmentManager.cpp
    ${CMAKE_SOURCE_DIR}/../src/lazer/meta_ham/AccomplishmentOneShot.cpp
    ${CMAKE_SOURCE_DIR}/../src/lazer/meta_ham/AccomplishmentProgress.cpp
    ${CMAKE_SOURCE_DIR}/../src/lazer/meta_ham/AccomplishmentSongConditional.cpp
    ${CMAKE_SOURCE_DIR}/../src/lazer/meta_ham/AccomplishmentSongListConditional.cpp
    ${CMAKE_SOURCE_DIR}/../src/lazer/meta_ham/AppLabel.cpp
    ${CMAKE_SOURCE_DIR}/../src/lazer/meta_ham/AppMiniLeaderboardDisplay.cpp
    ${CMAKE_SOURCE_DIR}/../src/lazer/meta_ham/AppNavProvider.cpp
    ${CMAKE_SOURCE_DIR}/../src/lazer/meta_ham/Award.cpp
    ${CMAKE_SOURCE_DIR}/../src/lazer/meta_ham/BlacklightPanel.cpp
    ${CMAKE_SOURCE_DIR}/../src/lazer/meta_ham/CalibrationPanel.cpp
    ${CMAKE_SOURCE_DIR}/../src/lazer/meta_ham/Campaign.cpp
    ${CMAKE_SOURCE_DIR}/../src/lazer/meta_ham/CampaignDiffSelectPanel.cpp
    ${CMAKE_SOURCE_DIR}/../src/lazer/meta_ham/CampaignEra.cpp
    ${CMAKE_SOURCE_DIR}/../src/lazer/meta_ham/CampaignMasterQuestCrewSelectPanel.cpp
    ${CMAKE_SOURCE_DIR}/../src/lazer/meta_ham/CampaignMasterQuestSongSelectPanel.cpp
    ${CMAKE_SOURCE_DIR}/../src/lazer/meta_ham/CampaignPerformer.cpp
    ${CMAKE_SOURCE_DIR}/../src/lazer/meta_ham/CampaignProgress.cpp
    ${CMAKE_SOURCE_DIR}/../src/lazer/meta_ham/CampaignSongSelectPanel.cpp
    ${CMAKE_SOURCE_DIR}/../src/lazer/meta_ham/ChallengeRecord.cpp
    ${CMAKE_SOURCE_DIR}/../src/lazer/meta_ham/ChallengeResultPanel.cpp
    ${CMAKE_SOURCE_DIR}/../src/lazer/meta_ham/ChallengeSort.cpp
    ${CMAKE_SOURCE_DIR}/../src/lazer/meta_ham/ChallengeSortByScore.cpp
    ${CMAKE_SOURCE_DIR}/../src/lazer/meta_ham/ChallengeSortMgr.cpp
    ${CMAKE_SOURCE_DIR}/../src/lazer/meta_ham/ChallengeSortNode.cpp
    ${CMAKE_SOURCE_DIR}/../src/lazer/meta_ham/Challenges.cpp
    ${CMAKE_SOURCE_DIR}/../src/lazer/meta_ham/CharacterProvider.cpp
    ${CMAKE_SOURCE_DIR}/../src/lazer/meta_ham/ChooseModeProvider.cpp
    ${CMAKE_SOURCE_DIR}/../src/lazer/meta_ham/ChooseProfilePanel.cpp
    ${CMAKE_SOURCE_DIR}/../src/lazer/meta_ham/ContentLoadingPanel.cpp
    ${CMAKE_SOURCE_DIR}/../src/lazer/meta_ham/ContextChecker.cpp
    ${CMAKE_SOURCE_DIR}/../src/lazer/meta_ham/CorrectIdentityPanel.cpp
    ${CMAKE_SOURCE_DIR}/../src/lazer/meta_ham/CrewProvider.cpp
    ${CMAKE_SOURCE_DIR}/../src/lazer/meta_ham/CursorPanel.cpp
    ${CMAKE_SOURCE_DIR}/../src/lazer/meta_ham/DepthBuffer.cpp
    ${CMAKE_SOURCE_DIR}/../src/lazer/meta_ham/DifficultyProvider.cpp
    ${CMAKE_SOURCE_DIR}/../src/lazer/meta_ham/EventDialogPanel.cpp
    ${CMAKE_SOURCE_DIR}/../src/lazer/meta_ham/FitnessCalorieSort.cpp
    ${CMAKE_SOURCE_DIR}/../src/lazer/meta_ham/FitnessCalorieSortByCalorie.cpp
    ${CMAKE_SOURCE_DIR}/../src/lazer/meta_ham/FitnessCalorieSortMgr.cpp
    ${CMAKE_SOURCE_DIR}/../src/lazer/meta_ham/FitnessCalorieSortNode.cpp
    ${CMAKE_SOURCE_DIR}/../src/lazer/meta_ham/FitnessGoalMgr.cpp
    ${CMAKE_SOURCE_DIR}/../src/lazer/meta_ham/FitnessProvider.cpp
    ${CMAKE_SOURCE_DIR}/../src/lazer/meta_ham/HamMemcardAction.cpp
    ${CMAKE_SOURCE_DIR}/../src/lazer/meta_ham/HamPanel.cpp
    ${CMAKE_SOURCE_DIR}/../src/lazer/meta_ham/HamProfile.cpp
    ${CMAKE_SOURCE_DIR}/../src/lazer/meta_ham/HamScreen.cpp
    ${CMAKE_SOURCE_DIR}/../src/lazer/meta_ham/HamSongMetadata.cpp
    ${CMAKE_SOURCE_DIR}/../src/lazer/meta_ham/HamSongMgr.cpp
    ${CMAKE_SOURCE_DIR}/../src/lazer/meta_ham/HamStarsDisplay.cpp
    ${CMAKE_SOURCE_DIR}/../src/lazer/meta_ham/HamStoreFilterProvider.cpp
    ${CMAKE_SOURCE_DIR}/../src/lazer/meta_ham/HamStoreOffer.cpp
    ${CMAKE_SOURCE_DIR}/../src/lazer/meta_ham/HamStorePanel.cpp
    ${CMAKE_SOURCE_DIR}/../src/lazer/meta_ham/HamStoreProvider.cpp
    ${CMAKE_SOURCE_DIR}/../src/lazer/meta_ham/HamUI.cpp
    ${CMAKE_SOURCE_DIR}/../src/lazer/meta_ham/HelpBarPanel.cpp
    ${CMAKE_SOURCE_DIR}/../src/lazer/meta_ham/Instarank.cpp
    ${CMAKE_SOURCE_DIR}/../src/lazer/meta_ham/Leaderboards.cpp
    ${CMAKE_SOURCE_DIR}/../src/lazer/meta_ham/LetterboxPanel.cpp
    ${CMAKE_SOURCE_DIR}/../src/lazer/meta_ham/LoadingPanel.cpp
    ${CMAKE_SOURCE_DIR}/../src/lazer/meta_ham/LockedContentPanel.cpp
    ${CMAKE_SOURCE_DIR}/../src/lazer/meta_ham/MQSongSort.cpp
    ${CMAKE_SOURCE_DIR}/../src/lazer/meta_ham/MQSongSortByCharacter.cpp
    ${CMAKE_SOURCE_DIR}/../src/lazer/meta_ham/MQSongSortMgr.cpp
    ${CMAKE_SOURCE_DIR}/../src/lazer/meta_ham/MQSongSortNode.cpp
    ${CMAKE_SOURCE_DIR}/../src/lazer/meta_ham/MainMenuPanel.cpp
    ${CMAKE_SOURCE_DIR}/../src/lazer/meta_ham/MainMenuProvider.cpp
    ${CMAKE_SOURCE_DIR}/../src/lazer/meta_ham/MetaPanel.cpp
    ${CMAKE_SOURCE_DIR}/../src/lazer/meta_ham/MetaPerformer.cpp
    ${CMAKE_SOURCE_DIR}/../src/lazer/meta_ham/MetagameRank.cpp
    ${CMAKE_SOURCE_DIR}/../src/lazer/meta_ham/MetagameStats.cpp
    ${CMAKE_SOURCE_DIR}/../src/lazer/meta_ham/MoveRatingHistory.cpp
    ${CMAKE_SOURCE_DIR}/../src/lazer/meta_ham/MovieProvider.cpp
    ${CMAKE_SOURCE_DIR}/../src/lazer/meta_ham/MultiUserGesturePanel.cpp
    ${CMAKE_SOURCE_DIR}/../src/lazer/meta_ham/NavListNode.cpp
    ${CMAKE_SOURCE_DIR}/../src/lazer/meta_ham/NavListSort.cpp
    ${CMAKE_SOURCE_DIR}/../src/lazer/meta_ham/NavListSortMgr.cpp
    ${CMAKE_SOURCE_DIR}/../src/lazer/meta_ham/OptionsPanel.cpp
    ${CMAKE_SOURCE_DIR}/../src/lazer/meta_ham/OutfitProvider.cpp
    ${CMAKE_SOURCE_DIR}/../src/lazer/meta_ham/OverlayPanel.cpp
    ${CMAKE_SOURCE_DIR}/../src/lazer/meta_ham/Overshell.cpp
    ${CMAKE_SOURCE_DIR}/../src/lazer/meta_ham/PassiveMessagesPanel.cpp
    ${CMAKE_SOURCE_DIR}/../src/lazer/meta_ham/PassiveMessenger.cpp
    ${CMAKE_SOURCE_DIR}/../src/lazer/meta_ham/Playlist.cpp
    ${CMAKE_SOURCE_DIR}/../src/lazer/meta_ham/PlaylistSongProvider.cpp
    ${CMAKE_SOURCE_DIR}/../src/lazer/meta_ham/PlaylistSort.cpp
    ${CMAKE_SOURCE_DIR}/../src/lazer/meta_ham/PlaylistSortByTypeCmp.cpp
    ${CMAKE_SOURCE_DIR}/../src/lazer/meta_ham/PlaylistSortMgr.cpp
    ${CMAKE_SOURCE_DIR}/../src/lazer/meta_ham/PlaylistSortNode.cpp
    ${CMAKE_SOURCE_DIR}/../src/lazer/meta_ham/PracticeChoosePanel.cpp
    ${CMAKE_SOURCE_DIR}/../src/lazer/meta_ham/ProfileMgr.cpp
    ${CMAKE_SOURCE_DIR}/../src/lazer/meta_ham/SaveLoadManager.cpp
    ${CMAKE_SOURCE_DIR}/../src/lazer/meta_ham/ShellInput.cpp
    ${CMAKE_SOURCE_DIR}/../src/lazer/meta_ham/SigninScreen.cpp
    ${CMAKE_SOURCE_DIR}/../src/lazer/meta_ham/SingleUserCrewSelectPanel.cpp
    ${CMAKE_SOURCE_DIR}/../src/lazer/meta_ham/SkeletonChooser.cpp
    ${CMAKE_SOURCE_DIR}/../src/lazer/meta_ham/SkeletonIdentifier.cpp
    ${CMAKE_SOURCE_DIR}/../src/lazer/meta_ham/SkillsAwardList.cpp
    ${CMAKE_SOURCE_DIR}/../src/lazer/meta_ham/SongRecord.cpp
    ${CMAKE_SOURCE_DIR}/../src/lazer/meta_ham/SongSelectPanel.cpp
    ${CMAKE_SOURCE_DIR}/../src/lazer/meta_ham/SongSelectPlaylistCustomizePanel.cpp
    ${CMAKE_SOURCE_DIR}/../src/lazer/meta_ham/SongSelectPlaylistPanel.cpp
    ${CMAKE_SOURCE_DIR}/../src/lazer/meta_ham/SongSort.cpp
    ${CMAKE_SOURCE_DIR}/../src/lazer/meta_ham/SongSortByDiff.cpp
    ${CMAKE_SOURCE_DIR}/../src/lazer/meta_ham/SongSortByLocation.cpp
    ${CMAKE_SOURCE_DIR}/../src/lazer/meta_ham/SongSortBySong.cpp
    ${CMAKE_SOURCE_DIR}/../src/lazer/meta_ham/SongSortMgr.cpp
    ${CMAKE_SOURCE_DIR}/../src/lazer/meta_ham/SongSortNode.cpp
    ${CMAKE_SOURCE_DIR}/../src/lazer/meta_ham/SongStatusMgr.cpp
    ${CMAKE_SOURCE_DIR}/../src/lazer/meta_ham/TexLoadPanel.cpp
    ${CMAKE_SOURCE_DIR}/../src/lazer/meta_ham/TitleProvider.cpp
    ${CMAKE_SOURCE_DIR}/../src/lazer/meta_ham/UIEventMgr.cpp
    ${CMAKE_SOURCE_DIR}/../src/lazer/meta_ham/Utl.cpp
    ${CMAKE_SOURCE_DIR}/../src/lazer/meta_ham/VenueProvider.cpp
    ${CMAKE_SOURCE_DIR}/../src/lazer/meta_ham/VoiceControlPanel.cpp
    ${CMAKE_SOURCE_DIR}/../src/lazer/meta_ham/VoiceInputPanel.cpp
    ${CMAKE_SOURCE_DIR}/../src/lazer/meta_ham/WeightInput.cpp
    ${CMAKE_SOURCE_DIR}/../src/lazer/net_ham/ChallengeSystemJobs.cpp
    ${CMAKE_SOURCE_DIR}/../src/lazer/net_ham/DataMinerJobs.cpp
    ${CMAKE_SOURCE_DIR}/../src/lazer/net_ham/FitnessGoalJobs.cpp
    ${CMAKE_SOURCE_DIR}/../src/lazer/net_ham/FriendsListJobs.cpp
    ${CMAKE_SOURCE_DIR}/../src/lazer/net_ham/HamStoreCartJobs.cpp
    ${CMAKE_SOURCE_DIR}/../src/lazer/net_ham/LeaderboardJobs.cpp
    ${CMAKE_SOURCE_DIR}/../src/lazer/net_ham/MotdJobs.cpp
    ${CMAKE_SOURCE_DIR}/../src/lazer/net_ham/PartyModeJobs.cpp
    ${CMAKE_SOURCE_DIR}/../src/lazer/net_ham/PlaylistJobs.cpp
    ${CMAKE_SOURCE_DIR}/../src/lazer/net_ham/RCJobDingo.cpp
    ${CMAKE_SOURCE_DIR}/../src/lazer/net_ham/RockCentral.cpp
    ${CMAKE_SOURCE_DIR}/../src/lazer/net_ham/ScoreJobs.cpp
    ${CMAKE_SOURCE_DIR}/../src/lazer/net_ham/TokenJobs.cpp
    ${CMAKE_SOURCE_DIR}/../src/lazer/net_ham/WebLinkJobs.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/char/Char.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/char/CharBlendBone.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/char/CharBone.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/char/CharBoneDir.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/char/CharBoneOffset.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/char/CharBoneTwist.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/char/CharBones.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/char/CharBonesBlender.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/char/CharBonesMeshes.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/char/CharBonesSamples.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/char/CharClip.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/char/CharClipDisplay.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/char/CharClipDriver.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/char/CharClipGroup.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/char/CharClipSet.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/char/CharCollide.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/char/CharCuff.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/char/CharDriver.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/char/CharDriverMidi.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/char/CharEyeDartRuleset.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/char/CharEyes.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/char/CharFaceServo.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/char/CharForeTwist.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/char/CharGuitarString.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/char/CharHair.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/char/CharIKFingers.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/char/CharIKFoot.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/char/CharIKHand.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/char/CharIKHead.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/char/CharIKMidi.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/char/CharIKRod.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/char/CharIKScale.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/char/CharIKSliderMidi.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/char/CharInterest.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/char/CharLipSync.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/char/CharLipSyncDriver.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/char/CharLookAt.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/char/CharMeshHide.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/char/CharMirror.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/char/CharNeckTwist.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/char/CharPollGroup.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/char/CharPosConstraint.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/char/CharServoBone.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/char/CharSignalApplier.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/char/CharSleeve.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/char/CharTaskMgr.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/char/CharTransDraw.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/char/CharUpperTwist.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/char/CharUtl.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/char/CharWeightSetter.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/char/CharWeightable.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/char/Character.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/char/CharacterTest.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/char/ClipCollide.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/char/ClipDistMap.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/char/ClipGraphGen.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/char/FileMerger.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/char/FileMergerOrganizer.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/char/Waypoint.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/flow/DrivenPropertyEntry.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/flow/DrivenPropertyMathOps.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/flow/Flow.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/flow/FlowAnimate.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/flow/FlowCommand.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/flow/FlowDistance.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/flow/FlowEventListener.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/flow/FlowIf.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/flow/FlowLabel.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/flow/FlowManager.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/flow/FlowMultiSetProperty.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/flow/FlowNode.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/flow/FlowOnStop.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/flow/FlowOutPort.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/flow/FlowPickOne.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/flow/FlowPtr.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/flow/FlowQueueable.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/flow/FlowRun.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/flow/FlowSequence.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/flow/FlowSetProperty.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/flow/FlowSlider.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/flow/FlowSound.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/flow/FlowSwitch.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/flow/FlowSwitchCase.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/flow/FlowTimer.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/flow/FlowTrigger.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/flow/FlowValueCase.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/flow/FlowWhile.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/flow/PropertyEventListener.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/flow/PropertyEventProvider.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/gesture/ArcDetector.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/gesture/ArchiveSkeleton.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/gesture/BaseSkeleton.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/gesture/CameraInput.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/gesture/CameraTilt.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/gesture/DepthBuffer3D.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/gesture/DirectionGestureFilter.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/gesture/DrawUtl.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/gesture/FitnessFilter.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/gesture/FreestyleMotionFilter.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/gesture/Gesture.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/gesture/GestureMgr.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/gesture/HandHeightGestureFilter.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/gesture/HandInvokeGestureFilter.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/gesture/HandRaisedGestureFilter.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/gesture/HandsUpGestureFilter.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/gesture/HighFiveGestureFilter.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/gesture/IdentityInfo.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/gesture/JointUtl.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/gesture/LiveCameraInput.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/gesture/NavigationSkeletonDir.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/gesture/Skeleton.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/gesture/SkeletonClip.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/gesture/SkeletonDir.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/gesture/SkeletonExtentTracker.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/gesture/SkeletonHistory.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/gesture/SkeletonQualityFilter.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/gesture/SkeletonRecoverer.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/gesture/SkeletonUpdate.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/gesture/SkeletonViz.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/gesture/SpeechMgr.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/gesture/StandingStillGestureFilter.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/gesture/StreamRecorder.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/gesture/StreamRenderer.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/gesture/StubCameraInput.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/gesture/WaveToTurnOnLight.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/hamobj/BustAMoveData.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/hamobj/CamShotCatVO.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/hamobj/CharCameraInput.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/hamobj/CharFeedback.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/hamobj/ClipPlayer.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/hamobj/CrazeHollaback.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/hamobj/DanceRemixer.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/hamobj/DancerSequence.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/hamobj/DancerSkeleton.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/hamobj/DetectFrame.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/hamobj/Difficulty.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/hamobj/ErrorNode.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/hamobj/FilterQueue.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/hamobj/FilterVersion.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/hamobj/FreestyleMove.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/hamobj/FreestyleMoveRecorder.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/hamobj/Ham.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/hamobj/HamAudio.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/hamobj/HamBattleData.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/hamobj/HamCamShot.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/hamobj/HamCamTransform.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/hamobj/HamCharacter.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/hamobj/HamDirector.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/hamobj/HamDriver.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/hamobj/HamGameData.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/hamobj/HamIKEffector.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/hamobj/HamIKSkeleton.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/hamobj/HamIconMan.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/hamobj/HamLabel.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/hamobj/HamList.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/hamobj/HamListRibbon.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/hamobj/HamMaster.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/hamobj/HamMove.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/hamobj/HamNavList.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/hamobj/HamNavProvider.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/hamobj/HamPartyJumpData.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/hamobj/HamPhotoDisplay.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/hamobj/HamPhraseMeter.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/hamobj/HamPlayerData.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/hamobj/HamProviderPrinter.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/hamobj/HamRegulate.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/hamobj/HamRibbon.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/hamobj/HamScrollBehavior.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/hamobj/HamScrollSpeedIndicator.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/hamobj/HamSkeletonConverter.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/hamobj/HamSong.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/hamobj/HamSongData.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/hamobj/HamSupereasyData.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/hamobj/HamVisDir.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/hamobj/HamWardrobe.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/hamobj/HollaBackMinigame.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/hamobj/MeterDisplay.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/hamobj/MiniGameMgr.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/hamobj/MiniLeaderboardDisplay.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/hamobj/MocapSkeletonIterator.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/hamobj/MoveAsyncDetector.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/hamobj/MoveDir.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/hamobj/MoveGraph.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/hamobj/MoveMgr.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/hamobj/MoveParent.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/hamobj/MoveVariant.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/hamobj/OriginalChoreoRemixer.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/hamobj/PhotoSpotlightPositioner.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/hamobj/Pose.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/hamobj/PoseFatalities.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/hamobj/PracticeOptionsProvider.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/hamobj/PracticeSection.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/hamobj/RhythmBattle.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/hamobj/RhythmBattlePlayer.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/hamobj/RhythmDetector.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/hamobj/RhythmDetectorGroup.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/hamobj/ScoreUtl.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/hamobj/SongCollision.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/hamobj/SongDifficultyDisplay.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/hamobj/SongLayout.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/hamobj/SongUtl.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/hamobj/StarsDisplay.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/hamobj/SuperEasyRemixer.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/hamobj/TransConstraint.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/jpeg/Jpeg.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/math/Color.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/math/Decibels.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/math/DoubleExponentialSmoother.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/math/Easing.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/math/FileChecksum.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/math/Geo.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/math/Interp.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/math/Key.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/math/Primes.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/math/Rand.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/math/Rand2.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/math/Rot.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/math/SHA1.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/math/Sort.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/math/StreamChecksum.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/math/Trig.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/math/mtx.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/math/vec.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/meta/Achievements.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/meta/ButtonHolder.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/meta/ConnectionStatusPanel.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/meta/CreditsPanel.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/meta/DataArraySongInfo.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/meta/DeJitterPanel.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/meta/FixedSizeSaveable.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/meta/FixedSizeSaveableStream.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/meta/HAQManager.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/meta/HeldButtonPanel.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/meta/Jukebox.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/meta/MemcardAction.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/meta/MemcardMgr.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/meta/Meta.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/meta/MetaMusicManager.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/meta/MetaMusicScene.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/meta/MoviePanel.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/meta/PreloadPanel.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/meta/Profile.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/meta/SongMetadata.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/meta/SongMgr.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/meta/SongPreview.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/meta/Sorting.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/meta/StoreEnumeration.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/meta/StoreOffer.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/meta/StorePanel.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/meta/StorePreviewMgr.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/meta/StorePurchaser.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/meta/StreamPlayer.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/midi/DataEventList.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/midi/DisplayEvents.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/midi/MidiParser.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/midi/MidiParserMgr.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/midi/MidiReader.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/midi/MidiReceiver.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/midi/MidiVarLen.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/movie/Movie.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/movie/MovieImpl.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/movie/MovieSys.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/movie/Splash.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/movie/TexMovie.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/moviebink/BinkMovieImpl.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/moviebink/BinkMovieSys.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/net/DingoAuthJob.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/net/DingoJob.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/net/DingoSvr.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/net/HttpGet.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/net/HttpReq.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/net/HttpReqCurl.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/net/JsonMemory.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/net/JsonUtils.cpp
    # json-c (C) — real implementation so online JSON (RockCentral/leaderboards/
    # MOTD/store) parses instead of resolving to the return-0 stubs.
    ${CMAKE_SOURCE_DIR}/../src/system/net/json-c/arraylist.c
    ${CMAKE_SOURCE_DIR}/../src/system/net/json-c/debug.c
    ${CMAKE_SOURCE_DIR}/../src/system/net/json-c/json_object.c
    ${CMAKE_SOURCE_DIR}/../src/system/net/json-c/json_tokener.c
    # json_util.c (json_object_from_file/_to_file) is pure file I/O the game never
    # calls and uses the zero-arg MC_ERROR variadic form that trips standard C —
    # omitted from the native build.
    ${CMAKE_SOURCE_DIR}/../src/system/net/json-c/linkhash.c
    ${CMAKE_SOURCE_DIR}/../src/system/net/json-c/printbuf.c
    ${CMAKE_SOURCE_DIR}/../src/system/net/WebSvcMgr.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/net/WebSvcMgrCurl.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/net/WebSvcReq.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/net/XLSPConnection.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/obj/DataArray.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/obj/DataFile.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/obj/DataFunc.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/obj/DataNode.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/obj/DataUtl.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/obj/Dir.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/obj/DirLoader.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/obj/DirUnloader.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/obj/MessageTimer.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/obj/Msg.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/obj/Object.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/obj/PropSync.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/obj/Task.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/obj/TextFile.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/obj/TypeProps.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/obj/Utl.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/oggvorbis/bitwise.c
    ${CMAKE_SOURCE_DIR}/../src/system/oggvorbis/framing.c
    ${CMAKE_SOURCE_DIR}/../src/system/oggvorbis/VorbisMem.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/os/AppChild.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/os/Archive.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/os/ArkFile.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/os/AsyncFile.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/os/AsyncFileHolmes.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/os/AsyncTask.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/os/BlockMgr.cpp
    # CDReader.cpp excluded - uses Xbox CreateFileA/ReadFile/OVERLAPPED
    # Replaced by native/src/platform/CDReader_Native.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/os/ContentMgr.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/os/CritSec.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/os/DateTime.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/os/Debug.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/os/File.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/os/FileCache.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/os/HDCache.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/os/HolmesClient.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/os/HolmesClient_NetSocket.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/os/HolmesKeyboard.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/os/HolmesUtl.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/os/Joypad.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/os/JoypadClient.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/os/JoypadMsgs.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/os/Joypad_Xinput.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/os/Keyboard.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/os/KeyboardJoypadAdapter.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/os/Memcard.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/os/NetStream.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/os/NetworkSocket.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/os/OnlineID.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/os/PlatformMgr.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/os/SynchronizationEvent.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/os/System.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/os/Timer.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/os/UsbMidiGuitar.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/os/UsbMidiGuitarMsgs.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/os/UsbMidiKeyboard.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/os/UsbMidiKeyboardMsgs.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/os/User.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/os/UserMgr.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/os/VirtualKeyboard.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/rndobj/AmbientOcclusion.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/rndobj/Anim.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/rndobj/AnimFilter.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/rndobj/BaseMaterial.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/rndobj/Bitmap.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/rndobj/BoxMap.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/rndobj/Cam.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/rndobj/CamAnim.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/rndobj/ColorXfm.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/rndobj/Console.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/rndobj/CubeTex.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/rndobj/DOFProc.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/rndobj/DOFProc_NG.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/rndobj/Dir.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/rndobj/Draw.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/rndobj/Enter.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/rndobj/Env.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/rndobj/Env_NG.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/rndobj/EventTrigger.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/rndobj/Flare.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/rndobj/Font.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/rndobj/Font3d.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/rndobj/FontBase.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/rndobj/Fur.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/rndobj/Fur_NG.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/rndobj/Gen.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/rndobj/Graph.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/rndobj/Group.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/rndobj/HiResScreen.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/rndobj/Line.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/rndobj/Lit.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/rndobj/LitAnim.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/rndobj/Lit_NG.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/rndobj/Mat.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/rndobj/MatAnim.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/rndobj/Mat_NG.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/rndobj/Mesh.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/rndobj/MeshAnim.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/rndobj/MeshDeform.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/rndobj/MetaMaterial.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/rndobj/Morph.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/rndobj/MotionBlur.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/rndobj/Movie.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/rndobj/MultiMesh.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/rndobj/MultiMeshProxy.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/rndobj/Overlay.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/rndobj/Part.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/rndobj/PartAnim.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/rndobj/PartLauncher.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/rndobj/Poll.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/rndobj/PollAnim.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/rndobj/PostProc.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/rndobj/PostProcMgr.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/rndobj/PostProc_NG.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/rndobj/PropAnim.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/rndobj/PropKeys.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/rndobj/Ribbon.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/rndobj/Rnd.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/rndobj/Rnd_NG.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/rndobj/SIVideo.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/rndobj/ScreenMask.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/rndobj/Set.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/rndobj/Shader.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/rndobj/ShaderMgr.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/rndobj/ShaderOptions.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/rndobj/ShaderProgram.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/rndobj/ShadowMap.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/rndobj/Shockwave.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/rndobj/SoftParticleBuffer.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/rndobj/SoftParticles.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/rndobj/Spline.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/rndobj/Tex.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/rndobj/TexBlendController.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/rndobj/TexBlender.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/rndobj/TexProc.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/rndobj/TexRenderer.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/rndobj/Text.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/rndobj/Trans.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/rndobj/TransAnim.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/rndobj/TransProxy.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/rndobj/Utl.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/rndobj/VelocityBuffer.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/rndobj/Watcher.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/rndobj/Wind.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/rndobj/wordwrap.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/synth/ADSR.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/synth/AudioDucker.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/synth/BinkReader.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/dsp/BitCrushEffect.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/synth/ByteGrinder.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/synth/CompressionEffect.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/dsp/DelayEffect.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/dsp/DistortionEffect.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/dsp/EQEffect.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/synth/Emitter.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/synth/Faders.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/dsp/FlangerEffect.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/synth/FxSend.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/synth/FxSendBitCrush.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/synth/FxSendChorus.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/synth/FxSendCompress.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/synth/FxSendDelay.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/synth/FxSendDistortion.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/synth/FxSendEQ.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/synth/FxSendFlanger.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/synth/FxSendMeterEffect.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/synth/FxSendPitchShift.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/synth/FxSendReverb.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/synth/FxSendSynapse.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/synth/FxSendWah.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/synth/MetaMusic.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/synth/MeterEffectMonitor.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/synth/Mic.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/synth/MicClientMapper.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/synth/MicNull.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/synth/MidiChannel.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/synth/MidiInstrument.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/synth/MidiSynth.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/synth/MoggClip.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/synth/MoggClipMap.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/synth/OggMap.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/synth/Pollable.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/synth/SampleData.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/synth/SampleInst.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/synth/SampleZone.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/synth/Sequence.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/synth/Sfx.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/synth/SfxMap.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/synth/Sound.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/synth/StandardStream.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/synth/Stream.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/synth/StreamNull.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/synth/StreamReceiver.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/synth/StreamReceiverFile.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/synth/Synth.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/synth/SynthSample.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/synth/ThreeDSound.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/synth/Utl.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/synth/VorbisReader.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/dsp/WahEffect.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/synth/WavMgr.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/synth/WavReader.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/synth/complex.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/synth/filterdesign.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/synth/tomcrypt/aes.c
    ${CMAKE_SOURCE_DIR}/../src/system/synth/tomcrypt/crypt.c
    ${CMAKE_SOURCE_DIR}/../src/system/synth/tomcrypt/ctr.c
    ${CMAKE_SOURCE_DIR}/../src/system/synth/tomcrypt/TomCryptLicense.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/ui/CheatProvider.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/ui/InlineHelp.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/ui/LabelNumberTicker.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/ui/LabelShrinkWrapper.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/ui/LocalePanel.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/ui/PanelDir.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/ui/ResourceDirPtr.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/ui/Screenshot.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/ui/ScrollSelect.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/ui/UI.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/ui/UIButton.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/ui/UIColor.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/ui/UIComponent.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/ui/UIFontImporter.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/ui/UIGuide.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/ui/UILabel.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/ui/UILabelDir.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/ui/UIList.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/ui/UIListArrow.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/ui/UIListCustom.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/ui/UIListDir.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/ui/UIListHighlight.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/ui/UIListLabel.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/ui/UIListMesh.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/ui/UIListProvider.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/ui/UIListSlot.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/ui/UIListState.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/ui/UIListSubList.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/ui/UIListWidget.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/ui/UIPanel.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/ui/UIPicture.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/ui/UIScreen.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/ui/UISlider.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/ui/UITransitionHandler.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/ui/UITrigger.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/ui/Utl.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/utl/AllocInfo.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/utl/BeatMap.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/utl/BinStream.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/utl/BinkIntegration.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/utl/BufStream.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/utl/Cache.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/utl/CacheMgr.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/utl/Cheats.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/utl/ChunkIDs.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/utl/ChunkStream.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/utl/Chunks.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/utl/Compress.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/utl/Crc.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/utl/DataPointMgr.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/utl/DeJitter.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/utl/DebugGraph.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/utl/DebugMeter.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/utl/EncryptXTEA.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/utl/FakeSongMgr.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/utl/FilePath.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/utl/FileStream.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/utl/GlitchFinder.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/utl/HxGuid.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/utl/JobMgr.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/utl/JsonEncode.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/utl/KnownIssues.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/utl/Licenses.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/utl/Loader.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/utl/Locale.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/utl/LocaleOrdinal.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/utl/MBT.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/utl/Magnu.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/utl/MakeString.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/utl/MeasureMap.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/utl/MemHeap.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/utl/MemMgr.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/utl/MemPoint.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/utl/MemStats.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/utl/MemStream.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/utl/MemTrack.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/utl/MemTracker.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/utl/MultiTempoTempoMap.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/utl/NetCacheLoader.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/utl/NetCacheMgr.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/utl/NetCacheMgr_Xbox.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/utl/NetLoader.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/utl/OSCMessenger.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/utl/Option.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/utl/Pool.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/utl/PoolAlloc.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/utl/Profiler.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/utl/Spew.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/utl/Song.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/utl/SongInfoAudioType.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/utl/SongInfoCopy.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/utl/Str.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/utl/StringTable.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/utl/SuperFormatString.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/utl/Symbol.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/utl/TempoMap.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/utl/TextFileStream.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/utl/TextStream.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/utl/TimeConversion.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/utl/UTF8.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/utl/UrlEncode.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/utl/VarTimer.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/utl/WaveFile.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/utl/trie.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/world/BeatClock.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/world/CameraManager.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/world/CameraShot.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/world/ColorPalette.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/world/Crowd.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/world/Crowd3DCharHandle.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/world/DefaultPhysicsManager.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/world/Dir.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/world/FreeCamera.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/world/Instance.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/world/LightHue.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/world/LightPreset.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/world/LightPresetManager.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/world/PhysicsManager.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/world/PhysicsVolume.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/world/PostProcer.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/world/Reflection.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/world/Spotlight.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/world/SpotlightDrawer.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/world/SpotlightDrawer_NG.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/world/SpotlightEnder.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/world/ThreeDSoundManager.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/world/World.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/obj/DataFlex.c
)

# ============================================================================
# Native platform implementations (replace Xbox-specific files)
# ============================================================================

# Native platform sources (no entry point — shared by dc3-native and milo-viewer)
set(DC3_NATIVE_CORE_SOURCES
    src/xdk_shims.cpp
    src/xbox_link_stubs.cpp
    src/telemetry/GameplayTelemetry.cpp
    src/platform/Memory_Native.cpp
    src/platform/PlatformMgr_Native.cpp
    src/platform/DingoSvr_Native.cpp
    src/platform/System_Native.cpp
    src/platform/File_Native.cpp
    src/platform/AsyncFile_Native.cpp
    src/platform/CDReader_Native.cpp
    src/platform/ThreadCall_Native.cpp
    src/platform/Joypad_Native.cpp
    src/platform/Keyboard_Native.cpp
    src/platform/Synth_Stub.cpp
    src/platform/Skeleton_Native.cpp
    src/platform/ProcessSpawner_Native.cpp # pose_server.py launched from a pristine fork (RLIMIT_RTTIME)
    src/platform/NativeNotice.cpp
    src/platform/GestureMgr_Native.cpp
    src/platform/PoseLevel_Native.cpp # P-7: camera tilt -> vNormalToGravity, Create's level step
    src/platform/PoseFill_Native.cpp # DC3_POSE_FILL: frames between a slow camera's (np-pose-fill)
    src/platform/NuiImageSurface_Native.cpp
    src/platform/CameraStream_Native.cpp # PLAN P-4: webcam -> Kinect colour/depth streams
    src/platform/StreamCompose_Native.cpp # PLAN P-4: StreamRenderer's shaders on the CPU
    src/platform/StreamReceiver_Native.cpp
    src/platform/SampleInst_Native.cpp
    src/platform/StbVorbisImpl.cpp
    src/platform/FxSendNative.cpp
    src/platform/MeshFilter.cpp
    src/platform/BoneSetup.cpp
    src/platform/MeshGpuCache.cpp
    src/platform/MaterialSetup.cpp
    src/platform/TransparentQueue.cpp
    src/platform/Cam_Native.cpp
    src/platform/Net_Stub.cpp
    src/platform/Memcard_Stub.cpp
    src/platform/ContentMgr_Stub.cpp
    src/platform/MapFile_Stub.cpp
    src/platform/VirtualKeyboard_Stub.cpp
    src/platform/Achievements_Stub.cpp
    src/platform/MemcardMgr_Stub.cpp
    src/platform/BinkMovie_Stub.cpp
    $<$<BOOL:${ENABLE_FFMPEG}>:src/platform/XmaSampleDecoder.cpp>
    src/platform/NetXbox_Stub.cpp
    src/platform/KinectShare_Stub.cpp
    src/platform/Cache_Stub.cpp
    src/platform/ChecksumData_Stub.cpp
    src/platform/Keygen_Stub.cpp
    src/platform/NetworkSocket_Stub.cpp
    src/platform/SynthCommon_Stub.cpp
    src/platform/RndTex_Native.cpp
    src/platform/RenderState_Native.cpp
    src/platform/DataParser_Native.cpp
    $<$<NOT:$<BOOL:${EMSCRIPTEN}>>:src/platform/HttpServer.cpp>
    $<$<NOT:$<BOOL:${EMSCRIPTEN}>>:src/platform/PoseTarget_Native.cpp>
    $<$<NOT:$<BOOL:${EMSCRIPTEN}>>:src/platform/PosePerformer_Native.cpp>  # DC3_POSE_PERFORM
    $<$<NOT:$<BOOL:${EMSCRIPTEN}>>:src/platform/PoseSession_Native.cpp>  # DC3_POSE_RECORD, session: replay (PLAN L-1)
    src/platform/DtaEvalSupport.cpp   # DTA eval state guard + result serializer (unit-tested)
    $<$<NOT:$<BOOL:${EMSCRIPTEN}>>:src/platform/FramePacer_Native.cpp>  # H-29 frame cap (DC3_FPS_CAP)
    src/engine_stubs_generated.cpp
    src/StubTrace.cpp
    src/native_link_glue.cpp
    src/native_job_stubs.cpp
    src/thunk_stubs.cpp
)

# Engine-clean core files now provided by libmilo-engine.a. Targets that LINK
# the engine (dc3-native, milo-viewer, render-test, milo-tests) use this
# filtered core list and additionally drop the moved gfx set + CharTwistSolver
# + ImGuiBackend + DebugPanel + VideoEncoder (all in the engine). Only dc3-web
# is unchanged and still compiles everything locally -- the export tools
# (milo-tex-export, milo-mat-export, milo2gltf) link milo-engine too and use
# this same filtered list via DC3_EXPORT_TOOL_SOURCES.
#
# Consequence worth knowing: the files removed again immediately below compile
# to NO OBJECT in any native target -- the engine's copy is what links. (The
# renderer/gfx/audio copies -- Rnd/Mesh/Tex/Part_Wgpu.cpp, native/src/gfx/,
# native/src/audio/ -- were deleted outright on 2026-11-13, H-1: no target
# compiled them, and their stale HEADERS were shadowing the engine's for every
# dc3 TU, because -I native/src precedes -I <engine>/src.)
set(DC3_NATIVE_CORE_SOURCES_ENGINE ${DC3_NATIVE_CORE_SOURCES})
# src/platform/Cache_Stub.cpp is deliberately NOT in this removal list (it used
# to be, because it was an empty placeholder whose engine twin was the real
# thing -- the engine's copy is still a 127-byte comment). It now defines the
# native cache backend (CacheMgrNative/CacheNative/CacheIDNative), and
# src/system/utl/CacheMgr.cpp's HX_NATIVE branch references the factory that
# lives in it, so every engine-linking target must compile it.
list(REMOVE_ITEM DC3_NATIVE_CORE_SOURCES_ENGINE
    src/platform/AsyncFile_Native.cpp
    src/platform/BinkMovie_Stub.cpp
    src/platform/BoneSetup.cpp
    src/platform/Cam_Native.cpp
    src/platform/CDReader_Native.cpp
    src/platform/ChecksumData_Stub.cpp
    src/platform/DataParser_Native.cpp
    src/platform/File_Native.cpp
    src/platform/FxSendNative.cpp
    src/platform/Joypad_Native.cpp
    src/platform/Keyboard_Native.cpp
    src/platform/Keygen_Stub.cpp
    src/platform/MapFile_Stub.cpp
    src/platform/MaterialSetup.cpp
    src/platform/Memory_Native.cpp
    src/platform/MeshGpuCache.cpp
    src/platform/Net_Stub.cpp
    src/platform/NetworkSocket_Stub.cpp
    src/platform/RndTex_Native.cpp
    src/platform/SampleInst_Native.cpp
    src/platform/StreamReceiver_Native.cpp
    src/platform/Synth_Stub.cpp
    src/platform/SynthCommon_Stub.cpp
    src/platform/ThreadCall_Native.cpp
    src/platform/TransparentQueue.cpp
    src/platform/VirtualKeyboard_Stub.cpp
)

# DebugPanel.cpp is "deferred glue": the engine deliberately does NOT compile it
# (see milo-native-engine/CMakeLists.txt — it reaches TheHttpServer under
# DC3_HTTP_SERVER, so each consumer supplies it), yet libmilo-engine.a's
# Rnd_Wgpu.cpp calls DebugPanel::Toggle() from WgpuRnd::BeginDrawing. So EVERY
# target that links the engine needs this TU, and it belongs in the shared
# engine-linking list rather than being repeated per target — it was listed on
# dc3-native, milo-viewer and dc3-web but missing from milo-tests, render-test
# and all three export tools, which linked with DebugPanel::Toggle() undefined
# until -Wl,--no-undefined was turned on. DC3_HTTP_SERVER is defined only on
# dc3-native, so for the other targets this pulls in no HTTP dependency.
# (dc3-web keeps its own entry: it does not use this list.)
list(APPEND DC3_NATIVE_CORE_SOURCES_ENGINE src/platform/DebugPanel.cpp)

# ============================================================================
# Export pipeline sources (CPU-only, no GPU dependency for decode)
# ============================================================================

set(DC3_EXPORT_SOURCES
    src/export/BitmapExport.cpp
    src/export/TextureExporter.cpp
    src/export/MaterialExporter.cpp
    src/export/GltfExporter.cpp
)

# ============================================================================
# Build targets
# ============================================================================

# (Shared by dc3-web and the native targets -- source properties are
# directory-scoped; this used to sit in the native-only branch, so dc3-web
# compiled json-c as C and failed on `::operator delete`.)
# json-c third-party sources: named .c, but the PPC build compiles every one of
# them as C++ (`/TP` — see the msvc cflags in build.ninja), and the sources rely
# on it: since 38e9e7655 they free through `::operator delete` to reproduce the
# ??3@YAXPAX@Z relocations retail's objects carry. Clang picks the language from
# the .c suffix, so LANGUAGE C made those lines "expected expression". Compile
# them as CXX to match the PPC translation mode. Every json-c header already
# wraps its declarations in `extern "C"`, so the exported symbols stay C-mangled
# and the C++ callers link unchanged.
#
# Still skip the C++ PCH and disable -fms-compatibility (its __sigbits/global-
# inline quirk plus the win_types.h shim breaks these vendor TUs the same way it
# breaks tomcrypt).
set_source_files_properties(
    ${CMAKE_SOURCE_DIR}/../src/system/net/json-c/arraylist.c
    ${CMAKE_SOURCE_DIR}/../src/system/net/json-c/debug.c
    ${CMAKE_SOURCE_DIR}/../src/system/net/json-c/json_object.c
    ${CMAKE_SOURCE_DIR}/../src/system/net/json-c/json_tokener.c
    ${CMAKE_SOURCE_DIR}/../src/system/net/json-c/linkhash.c
    ${CMAKE_SOURCE_DIR}/../src/system/net/json-c/printbuf.c
    PROPERTIES LANGUAGE CXX
               SKIP_PRECOMPILE_HEADERS ON
               COMPILE_OPTIONS "-fno-ms-extensions;-fno-ms-compatibility"
)

if(EMSCRIPTEN)

# ============================================================================
# Web target (Emscripten + emdawnwebgpu)
# ============================================================================

# Web-specific platform sources (replace native platform files)
# The engine-clean platform/gfx TUs (AsyncFile/Joypad/Keyboard/Synth stubs,
# StreamReceiver_Native, SampleInst, FxSend, BoneSetup, MaterialSetup, Mesh_Wgpu,
# TransparentQueue, Part_Wgpu, Tex_Wgpu, Cam, RndTex, Rnd_Wgpu, MeshGpuCache,
# Memory/ThreadCall and the whole gfx/ set) come from libmilo-engine.a, exactly
# as for dc3-native (DC3_NATIVE_CORE_SOURCES_ENGINE). dc3-web used to compile
# the stale DC3-local copies of these plus the engine's Rnd_Wgpu.cpp, which
# stopped compiling once the engine's gfx passes moved on (2026-10: PostProcPass
# ::Run arity, SetSpotBeams, PipelineManager::SetAlphaBlendMax), and meant web
# ran a different renderer/audio backend from desktop. w23-web.
set(DC3_WEB_CORE_SOURCES
    src/xdk_shims.cpp
    # Present in DC3_NATIVE_CORE_SOURCES_ENGINE but missing here: without them
    # 75 Xbox entry points (Bink*, Nui*, Dm*, XAudio/XMA, ...) were left as
    # unresolved wasm imports that abort on first call.
    src/xbox_link_stubs.cpp
    src/telemetry/GameplayTelemetry.cpp
    src/platform/DtaEvalSupport.cpp
    # Web stand-ins for the XDK/NUI/XNet entry points (exact XDK signatures).
    # Dropped in 466a3a591 as a "deleted file" -- it was not deleted, and its
    # absence left those symbols as unresolved wasm imports.
    src/web_stubs.cpp
    # Memory_Native.cpp lifted to milo-engine (engine 0.2b drop-xdk graduation);
    # compiled via DC3_WEB_LIFTED_ENGINE_SOURCES below.
    src/platform/PlatformMgr_Native.cpp
    src/platform/DingoSvr_Native.cpp
    src/platform/System_Native.cpp
    # File_Web.cpp / CDReader_Web.cpp / WebAssets.cpp lifted to milo-engine
    # (MILO_BUILD_WEB platform layer, W0).
    src/platform/Skeleton_Native.cpp
    src/platform/ProcessSpawner_Native.cpp # pose_server.py launched from a pristine fork (RLIMIT_RTTIME)
    src/platform/NativeNotice.cpp
    src/platform/GestureMgr_Native.cpp
    src/platform/PoseLevel_Native.cpp # P-7: camera tilt -> vNormalToGravity, Create's level step
    src/platform/PoseFill_Native.cpp # DC3_POSE_FILL: frames between a slow camera's (np-pose-fill)
    src/platform/NuiImageSurface_Native.cpp
    src/platform/CameraStream_Native.cpp # PLAN P-4: webcam -> Kinect colour/depth streams
    src/platform/StreamCompose_Native.cpp # PLAN P-4: StreamRenderer's shaders on the CPU
    # ThreadCall_Native.cpp lifted to milo-engine (engine 0.2b drop-xdk POSIX
    # graduation); compiled via DC3_WEB_LIFTED_ENGINE_SOURCES below.
    # AudioDevice_Web.cpp lifted to milo-engine (MILO_BUILD_WEB, W0).
    src/platform/StbVorbisImpl.cpp
    # Rnd_Wgpu.cpp / MeshGpuCache.cpp lifted to milo-engine; compiled via
    # DC3_WEB_LIFTED_ENGINE_SOURCES below.
    src/platform/MeshFilter.cpp
    src/platform/Memcard_Stub.cpp
    src/platform/ContentMgr_Stub.cpp
    src/platform/Achievements_Stub.cpp
    src/platform/MemcardMgr_Stub.cpp
    # Native cache backend (CacheMgrNative/CacheNative/CacheIDNative). Needed
    # here for the same reason as on the desktop targets: CacheMgr.cpp's
    # HX_NATIVE branch calls CacheMgrCreateNativeBackend(), which only this TU
    # defines. (The engine's own Cache_Stub.cpp is a comment-only placeholder.)
    src/platform/Cache_Stub.cpp
    src/platform/WebMovieImpl.cpp
    src/platform/NetXbox_Stub.cpp
    src/platform/KinectShare_Stub.cpp
    src/platform/RenderState_Native.cpp
    src/engine_stubs_generated.cpp
    src/StubTrace.cpp
    src/native_link_glue.cpp
    src/native_job_stubs.cpp
    src/thunk_stubs.cpp
)

# Full engine + web platform layer
# Filter out files that can't compile under Emscripten
set(DC3_WEB_ENGINE_SOURCES ${DC3_ENGINE_SOURCES})
list(REMOVE_ITEM DC3_WEB_ENGINE_SOURCES
    ${CMAKE_SOURCE_DIR}/../src/system/net/HttpReqCurl.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/net/WebSvcMgrCurl.cpp
    # Emscripten provides ogg/vorbis via -sUSE_OGG=1 -sUSE_VORBIS=1
    ${CMAKE_SOURCE_DIR}/../src/system/oggvorbis/bitwise.c
    ${CMAKE_SOURCE_DIR}/../src/system/oggvorbis/framing.c
)

## dc3-web links libmilo-engine.a like every desktop target, but the W0-lifted
## engine *_Web.cpp TUs are excluded from that archive (see
## MILO_ENGINE_DECOMP_PLATFORM_EXCLUDE above) and compiled here instead, with
## dc3-web's flags. Keep this list aligned with milo-native-engine/CMakeLists.txt's
## MILO_ENGINE_WEB_PLATFORM_SOURCES / _WEB_GFX_SOURCES sets.
set(DC3_WEB_LIFTED_ENGINE_SOURCES
    ${MILO_ENGINE_PATH}/src/platform/WebAssets.cpp
    ${MILO_ENGINE_PATH}/src/platform/File_Web.cpp
    ${MILO_ENGINE_PATH}/src/platform/CDReader_Web.cpp
    ${MILO_ENGINE_PATH}/src/platform/GpuDevice_Web.cpp
    ${MILO_ENGINE_PATH}/src/audio/AudioDevice_Web.cpp
    ${MILO_ENGINE_PATH}/src/gfx/ImGuiBackend_Web.cpp
    # Memory_Native / ThreadCall_Native / MeshGpuCache / Rnd_Wgpu now come from
    # libmilo-engine.a (linked below), like every desktop target.
)

add_executable(dc3-web
    src/main_web.cpp
    src/platform/PoseInput_Web.cpp  # dc3_pose_push: the page's camera pipeline -> pose provider
    src/platform/CameraInput_Web.cpp # dc3_camera_push: the page's camera picture + mask (P-4)
    src/platform/WebSvcMgr_Stub.cpp
    src/platform/DebugPanel.cpp
    src/dc3_render_hook.cpp       # HamRenderHook (registers with milo-engine's WgpuRnd)
    ${DC3_WEB_LIFTED_ENGINE_SOURCES}
    ${DC3_WEB_ENGINE_SOURCES}
    ${DC3_WEB_CORE_SOURCES}
    # renderer + gfx passes + audio device come from libmilo-engine.a
)

target_compile_options(dc3-web PRIVATE
    ${DC3_MSVC_COMPAT_FLAGS}
    ${DC3_DECOMP_FLAGS}
)

target_compile_definitions(dc3-web PRIVATE
    HX_NATIVE=1
    HX_WEB=1
    HX_IMGUI=1
    MILO_DEBUG=1
    _DEBUG=1
    # libc++ removed deprecated C++ features in C++17 mode; re-enable them
    _LIBCPP_ENABLE_CXX17_REMOVED_RANDOM_SHUFFLE
    _LIBCPP_ENABLE_CXX17_REMOVED_BINDERS
    _LIBCPP_ENABLE_CXX17_REMOVED_AUTO_PTR
    _LIBCPP_ENABLE_CXX17_REMOVED_UNARY_BINARY_FUNCTION
)

# Engine's src/ root wins FIRST (SYSTEM BEFORE) for dc3-web. It also arrives via
# milo-engine's PUBLIC include dirs, but those land AFTER ${CMAKE_SOURCE_DIR}/src,
# where a stale local copy of a shared header used to shadow the engine's (the
# AudioDevice.h break; the same shadowing hit every desktop target through
# native/src/gfx/*.h until those copies were deleted, H-1). The STL iterator shim
# reaches dc3-web like every target, through the directory-level generated
# host_stl_shim include (emsdk's libc++ __wrap_iter).
target_include_directories(dc3-web SYSTEM BEFORE PRIVATE
    ${MILO_ENGINE_PATH}/src
)
target_include_directories(dc3-web PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/web_compat   # <execinfo.h> stand-in (Emscripten has none)
    ${CMAKE_SOURCE_DIR}/../src
    ${CMAKE_SOURCE_DIR}/../src/system
    ${CMAKE_SOURCE_DIR}/../src/lazer
    ${CMAKE_SOURCE_DIR}/../include
)

## Non-blocking file loading via JSPI (JavaScript Promise Integration).
## WebAssetsFetchSync() yields to the browser via emscripten_sleep(),
## so the loading screen can render while .milo_xbox files download.
## JSPI uses the browser's native stack switching — zero code size overhead
## vs old ASYNCIFY which instrumented every function (~50% bloat).
## Requires Chrome 137+ / Firefox 139+.
## Toggle via the engine's MILO_WEB_ASYNC option (default ON); set
## -DMILO_WEB_ASYNC=OFF on the emcmake line to fall back to blocking XHR.
##
## Engine helper applies the canonical emdawnwebgpu/JSPI/stack/memory/FETCH
## flag set + a base -sEXPORTED_FUNCTIONS=["_main"] and (when MILO_WEB_ASYNC)
## -sJSPI_EXPORTS=["_main"]. The subsequent target_link_options() block below
## extends those export lists with DC3's tick symbol — repeated
## -sEXPORTED_FUNCTIONS / -sJSPI_EXPORTS entries to wasm-ld replace earlier
## ones (last occurrence wins). asset_dest = native/web/build (the path
## scripts/web/server.py serves wasm/.js/.data from).
milo_engine_apply_web_target_options(dc3-web ${CMAKE_CURRENT_SOURCE_DIR}/web/build)
target_link_options(dc3-web PRIVATE
    -sEXPORTED_FUNCTIONS=["_main","_dc3MainLoopTick"]
    # Same as DC3_PLATFORM_LINK_OPTIONS on desktop: the decomp has intentional
    # stub/real pairs (engine_stubs_generated.cpp vs the real TU, dc3 TUs vs
    # libmilo-engine.a copies). First definition wins, in the same object order
    # as dc3-native (decomp TUs, then core/stubs, then the engine archive).
    -Wl,--allow-multiple-definition
    # dc3-local pre-js (URL ?env= → ENV bridge). Stacks with the engine's
    # missing_stubs.js pre-js from milo_engine_apply_web_target_options.
    "SHELL:--pre-js ${CMAKE_CURRENT_SOURCE_DIR}/web/dc3_pre.js"
)
if(MILO_WEB_ASYNC)
    target_link_options(dc3-web PRIVATE
        -sJSPI_EXPORTS=["_main","_dc3MainLoopTick","dc3MainLoopTick"]
    )
endif()

# Per-consumer parameterisation: canvas DOM selector + audio JS namespace.
# CANVAS_SELECTOR has `#` in the value, so it goes through the engine's
# header-routing helper (configure_file'd milo_web_config_<tgt>.h, force-
# included into every dc3-web TU). AUDIO_NS is an unquoted identifier and
# passes cleanly via -D.
milo_engine_set_web_canvas_selector(dc3-web "#dc3-canvas")
target_compile_definitions(dc3-web PRIVATE
    MILO_WEB_AUDIO_NS=dc3
)

target_link_libraries(dc3-web PRIVATE milo-engine imgui)

# Same precompiled/force-included header every native target uses (and the
# MSVC decomp build's /FI): decomp TUs rely on Object.h + Debug.h arriving
# without an #include (e.g. utl/BinkIntegration.cpp's os/Endian.h MILO_ASSERTs).
# The lifted engine *_Web.cpp TUs keep compiling without it (see the
# MILO_ENGINE_DECOMP_PLATFORM_EXCLUDE note above: the PCH locks <sys/stat.h>'s
# st_mtime macro state).
target_precompile_headers(dc3-web PRIVATE
    "$<$<COMPILE_LANGUAGE:CXX>:${DC3_PCH_HEADER}>"
)
set_source_files_properties(${DC3_WEB_LIFTED_ENGINE_SOURCES}
    PROPERTIES SKIP_PRECOMPILE_HEADERS ON)
# Jpeg.cpp / engine_stubs_generated.cpp: same PCH conflicts the native targets
# skip them for (see the list in the native branch).
set_source_files_properties(
    ${CMAKE_SOURCE_DIR}/../src/system/jpeg/Jpeg.cpp
    ${CMAKE_SOURCE_DIR}/src/engine_stubs_generated.cpp
    PROPERTIES SKIP_PRECOMPILE_HEADERS ON)

# ============================================================================
# Web release/debug split (mirrors rb3's RB3_WEB_RELEASE), one build dir each
# (scripts/web/build.sh):
#   DC3_WEB_RELEASE=ON  -> native/build-web-release, deployed to release/ (the
#       default page): -${DC3_WEB_OPT_LEVEL} (default O3), -g0, function names
#       kept (--profiling-funcs, so a crash stack reads as C++ names). Served
#       immutable + version-busted.
#   DC3_WEB_RELEASE=OFF -> native/build-web, deployed to debug/ (?debug=true):
#       -O0, linked -g2 (wasm function names; emcc keeps DWARF only at -g3).
#       build.sh configures it CMAKE_BUILD_TYPE=Debug, so the engine library
#       and imgui are unoptimised too. no-store.
#
# Both -O levels are set HERE, explicitly, on compile and link. They used to be
# implied, and came out swapped (W-3, 2026-11-13): build.sh configured both dirs
# with CMAKE_BUILD_TYPE=Release, whose -O3 sits at the front of every compile
# and link line; release appended -O0 (the old DC3_WEB_OPT_LEVEL default) and
# won, debug appended nothing and stayed -O3. emcc honours the LAST -O/-g, and
# these target options come after both CMAKE_CXX_FLAGS_<CONFIG> and the engine
# helper's -g2, so they win whatever build type a cache holds.
#
# Why O3 is now safe to ship (measured 2026-11-13, docs/native/WEB.md "Optimisation
# level"): the -O0 default dated from 6298c017c, which copied rb3's caution
# ("-O>0 risks the matched-fork") without a dc3 failure behind it. rb3's two
# -O>0 faults do not reproduce here: (1) rb3's inline-discard (cross-TU calls
# to `inline` bodies becoming undefined imports at -O>0) -- dc3's -O3 wasm has
# 163 imports vs 166 at -O0, none new; (2) per-TU miscompiles -- a whole song
# with webcam pose input plays at -O3 with no page error and scores like -O0.
# Keep -sASSERTIONS=1 / -sSTACK_OVERFLOW_CHECK=2 from the engine helper (the
# missing-stub hooks rely on them). No --closure: dc3 has no pre-js
# abort-patcher to break (it uses the engine's missing_stubs.js).
# ============================================================================
option(DC3_WEB_RELEASE "Optimised web release build (-O3 -g0); OFF = -O0 debug build" OFF)
set(DC3_WEB_OPT_LEVEL "O3" CACHE STRING
    "emcc -O level for dc3-web release builds (O3/O2/Os/Oz/O1/O0)")
set_property(CACHE DC3_WEB_OPT_LEVEL PROPERTY STRINGS O3 O2 Os Oz O1 O0)
if(DC3_WEB_RELEASE)
    message(STATUS "dc3-web: release build (-${DC3_WEB_OPT_LEVEL} -g0, function names kept)")
    target_compile_options(dc3-web PRIVATE -${DC3_WEB_OPT_LEVEL} -g0)
    target_link_options(dc3-web PRIVATE -${DC3_WEB_OPT_LEVEL} -g0 --profiling-funcs)
else()
    message(STATUS "dc3-web: debug build (-O0 -g2)")
    target_compile_options(dc3-web PRIVATE -O0)
    target_link_options(dc3-web PRIVATE -O0 -g2)
endif()

# Wasm heap cap (W-6). The engine helper links every web target with
# -sMAXIMUM_MEMORY=512MB; repeated -s settings resolve to the last one, so this
# overrides it for dc3-web only (rb3-web keeps the engine's value).
# Why 1024MB: measured with an 8MB linear growth step, one whole song peaks at
# 399-444 MB depending on venue (dci highest; docs/native/WEB.md "Heap per
# venue"). With emcc's normal geometric growth (+20%, at most +96 MB a step)
# the 512MB build's heap already sat AT the cap on dci's results screen, so
# the next growth there would have failed. The memory is not shared, so a
# higher max only reserves address space; nothing is committed until used.
# DC3_WEB_MEMORY_GROWTH_LINEAR_STEP is the measurement knob: with geometric
# growth buffer.byteLength can sit up to ~96 MB above what the program asked
# for; a small linear step (e.g. 8MB) makes it track the real high-water mark.
set(DC3_WEB_MAX_MEMORY "1024MB" CACHE STRING "dc3-web -sMAXIMUM_MEMORY")
set(DC3_WEB_MEMORY_GROWTH_LINEAR_STEP "" CACHE STRING
    "dc3-web -sMEMORY_GROWTH_LINEAR_STEP (empty = emcc's geometric growth)")
target_link_options(dc3-web PRIVATE -sMAXIMUM_MEMORY=${DC3_WEB_MAX_MEMORY})
if(DC3_WEB_MEMORY_GROWTH_LINEAR_STEP)
    target_link_options(dc3-web PRIVATE
        -sMEMORY_GROWTH_LINEAR_STEP=${DC3_WEB_MEMORY_GROWTH_LINEAR_STEP})
endif()

else() # NOT EMSCRIPTEN — native targets

add_executable(dc3-native
    ${DC3_ENGINE_SOURCES}
    src/main_native.cpp
    src/platform/WindowLoss_X11.cpp  # H-9: a destroyed X window ends the loop, not the process
    ${DC3_NATIVE_CORE_SOURCES_ENGINE}
    src/dc3_render_hook.cpp       # Phase 0.2a: HamRenderHook (registers with milo-engine)
    src/platform/DebugPanel.cpp   # deferred glue (pairs with HttpServer/DC3_HTTP_SERVER)
    # gfx set + ImGuiBackend.cpp now provided by milo-engine
)
# WindowLoss_X11.cpp includes Xlib (via glfw3native.h) and no decomp header:
# Xlib's macros collide with the decomp PCH, and it needs no MSVC compat.
set_source_files_properties(
    ${CMAKE_SOURCE_DIR}/src/platform/WindowLoss_X11.cpp
    PROPERTIES SKIP_PRECOMPILE_HEADERS ON
               COMPILE_OPTIONS "-fno-ms-extensions;-fno-ms-compatibility;-fno-delayed-template-parsing"
)
if(NOT APPLE)
    find_package(X11 REQUIRED)
    target_link_libraries(dc3-native PRIVATE X11::X11)
endif()

target_compile_options(dc3-native PRIVATE
    ${DC3_MSVC_COMPAT_FLAGS}
    ${DC3_DECOMP_FLAGS}
    ${DC3_ATOMIC_COMPAT_FLAGS}
    # -fsanitize=address  # Disabled: complex virtual inheritance VTT issues
)

target_compile_definitions(dc3-native PRIVATE
    HX_NATIVE=1
    MILO_DEBUG=1
    _DEBUG=1
    HX_IMGUI=1
    DC3_HTTP_SERVER=1
    $<$<BOOL:${ENABLE_FFMPEG}>:HX_FFMPEG=1>
)

# Include paths matching Xbox build structure
target_include_directories(dc3-native PRIVATE
    ${CMAKE_SOURCE_DIR}/src             # Native-specific headers (shims take priority)
    ${CMAKE_SOURCE_DIR}/../src          # Main source root
    ${CMAKE_SOURCE_DIR}/../src/system   # system/ headers use bare includes
    ${CMAKE_SOURCE_DIR}/../src/system/oggvorbis  # libogg/vorbis bundled headers
    ${CMAKE_SOURCE_DIR}/../src/lazer    # lazer/ headers use bare includes (game/, meta_ham/, etc.)
    # NOTE: src/xdk/LIBCMT is NOT included - it shadows system headers with Xbox-specific
    # types (size_t=unsigned int, etc.) that break 64-bit compilation
    ${CMAKE_SOURCE_DIR}/../include      # Additional headers
    ${FFMPEG_INCLUDE_DIRS}
)

target_link_options(dc3-native PRIVATE
    # -fsanitize=address  # Disabled: complex virtual inheritance VTT issues
    ${DC3_PLATFORM_LINK_OPTIONS}
)

target_link_libraries(dc3-native PRIVATE
    milo-engine
    ${DC3_PLATFORM_LIBS}
    ${FFMPEG_LIBRARIES}
    imgui
)

# NOTE: GFX sources include BOTH Dawn/WebGPU headers AND decomp engine headers.
# The -D__GNUC_STDC_INLINE__ flag (in DC3_MSVC_COMPAT_FLAGS) resolves the GCC 15
# glibc header conflict, allowing -fms-compatibility to coexist with Dawn headers.

# C files need special handling (some are .c but included in C++ project)
set_source_files_properties(
    ${CMAKE_SOURCE_DIR}/../src/system/obj/DataFlex.c
    PROPERTIES LANGUAGE C
)

# macOS-only settings for the targets above. (Files that include macOS SDK /
# framework headers break under -fms-compatibility: the SDK's ConditionalMacros.h
# cannot identify the compiler.)
if(APPLE)
    # The TUs that include macOS SDK / framework headers (MetalSurface.mm,
    # GpuDevice.cpp, AudioDevice.cpp) cannot take the MSVC compat flags or the
    # decomp PCH. They live in the engine, and since engine 4e738fe (PLAN M-2)
    # the engine's own CMakeLists exempts them, so dc3 no longer does.

    # dc3-native carries an Info.plist in its __TEXT,__info_plist section, the
    # standard way to give a bare (non-.app) executable a bundle identity. It
    # names why the camera is wanted (NSCameraUsageDescription), which macOS
    # shows if dc3-native is ever the process TCC asks on behalf of. Launched
    # from a terminal, the TERMINAL is that process and its own description is
    # shown instead (docs/native/MACOS.md, "First run"). Not a .app on purpose:
    # a bundle moves the executable to dc3-native.app/Contents/MacOS/, and
    # every test, script and NativeTreeDir() expects native/build/dc3-native.
    set(DC3_MAC_INFO_PLIST "${CMAKE_BINARY_DIR}/dc3-native-Info.plist")
    configure_file(${CMAKE_SOURCE_DIR}/cmake/dc3-native-Info.plist.in
        ${DC3_MAC_INFO_PLIST} @ONLY)
    target_link_options(dc3-native PRIVATE
        "LINKER:-sectcreate,__TEXT,__info_plist,${DC3_MAC_INFO_PLIST}")
    set_property(TARGET dc3-native APPEND PROPERTY LINK_DEPENDS ${DC3_MAC_INFO_PLIST})

    # tomcrypt C files: -fms-compatibility causes __sigbits from <signal.h> to
    # emit as a non-inline global, producing duplicate symbols across TUs.
    set_source_files_properties(
        ${CMAKE_SOURCE_DIR}/../src/system/synth/tomcrypt/aes.c
        ${CMAKE_SOURCE_DIR}/../src/system/synth/tomcrypt/crypt.c
        ${CMAKE_SOURCE_DIR}/../src/system/synth/tomcrypt/ctr.c
        PROPERTIES SKIP_PRECOMPILE_HEADERS ON
                   COMPILE_OPTIONS "-fno-ms-extensions;-fno-ms-compatibility"
    )
endif()

# Files that conflict with the PCH:
# - Jpeg.cpp: jpeglib.h redefines INT32 (long vs int from win_types.h)
# - File_Native.cpp: FileStat members clash with <sys/stat.h> st_*time macros
# - engine_stubs_generated.cpp: weak stubs intentionally conflict with real decls
set_source_files_properties(
    ${CMAKE_SOURCE_DIR}/../src/system/jpeg/Jpeg.cpp
    ${CMAKE_SOURCE_DIR}/src/platform/File_Native.cpp
    ${CMAKE_SOURCE_DIR}/src/engine_stubs_generated.cpp
    ${CMAKE_SOURCE_DIR}/src/platform/HttpServer.cpp
    PROPERTIES SKIP_PRECOMPILE_HEADERS ON
)

# Precompiled header — reuse the same decomp_pch.h used by the MSVC decomp build.
# Covers Object.h, Debug.h and their heavy transitive includes (Data.h, BinStream,
# Symbol, STL containers). Applies to all engine targets.
target_precompile_headers(dc3-native PRIVATE
    "$<$<COMPILE_LANGUAGE:CXX>:${DC3_PCH_HEADER}>"
)

# ============================================================================
# Milo Viewer — loads .milo_xbox files with orbit camera
# ============================================================================
add_executable(milo-viewer
    src/viewer/milo_viewer.cpp
    src/viewer/ViewerArgs.cpp
    src/viewer/ViewerCamera.cpp
    src/viewer/ViewerDebugUI.cpp
    src/viewer/ViewerPoseDump.cpp
    src/viewer/ViewerScene.cpp
    src/viewer/ViewerAnimation.cpp
    src/viewer/ViewerCapture.cpp
    src/dc3_render_hook.cpp       # Phase 0.2a: HamRenderHook (registers with milo-engine)
    src/platform/DebugPanel.cpp   # deferred glue (pairs with HttpServer)
    ${DC3_ENGINE_SOURCES}
    ${DC3_NATIVE_CORE_SOURCES_ENGINE}
    ${DC3_EXPORT_SOURCES}
    # CharTwistSolver + ImGuiBackend + VideoEncoder + gfx set -> milo-engine
)

target_compile_options(milo-viewer PRIVATE
    ${DC3_MSVC_COMPAT_FLAGS}
    ${DC3_DECOMP_FLAGS}
    ${DC3_ATOMIC_COMPAT_FLAGS}
    # -fsanitize=address  # Disabled: complex virtual inheritance VTT issues
)

target_compile_definitions(milo-viewer PRIVATE
    HX_NATIVE=1
    MILO_VIEWER=1
    HX_IMGUI=1
    MILO_DEBUG=1
    _DEBUG=1
    $<$<BOOL:${ENABLE_FFMPEG}>:HX_FFMPEG=1>
)

target_include_directories(milo-viewer PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/../src
    ${CMAKE_SOURCE_DIR}/../src/system
    ${CMAKE_SOURCE_DIR}/../src/system/oggvorbis  # libogg/vorbis bundled headers
    ${CMAKE_SOURCE_DIR}/../src/lazer
    ${CMAKE_SOURCE_DIR}/../include
    ${CMAKE_SOURCE_DIR}/include  # cgltf headers
    ${FFMPEG_INCLUDE_DIRS}
)

target_link_options(milo-viewer PRIVATE
    # -fsanitize=address  # Disabled: complex virtual inheritance VTT issues
    ${DC3_PLATFORM_LINK_OPTIONS}
)

target_link_libraries(milo-viewer PRIVATE
    milo-engine
    ${DC3_PLATFORM_LIBS}
    imgui
    $<$<BOOL:${ENABLE_FFMPEG}>:${FFMPEG_LIBRARIES}>
)
target_precompile_headers(milo-viewer PRIVATE
    "$<$<COMPILE_LANGUAGE:CXX>:${DC3_PCH_HEADER}>"
)

# ============================================================================
# Render Test — programmatic test scenes for renderer validation
# ============================================================================

add_executable(render-test
    src/render_test/render_test_main.cpp
    src/render_test/test_scene.cpp
    src/dc3_render_hook.cpp       # Phase 0.2a: HamRenderHook (registers with milo-engine)
    ${DC3_ENGINE_SOURCES}
    ${DC3_NATIVE_CORE_SOURCES_ENGINE}
    # gfx set -> milo-engine
)

target_compile_options(render-test PRIVATE
    ${DC3_MSVC_COMPAT_FLAGS}
    ${DC3_DECOMP_FLAGS}
    ${DC3_ATOMIC_COMPAT_FLAGS}
)

target_compile_definitions(render-test PRIVATE
    HX_NATIVE=1
    MILO_DEBUG=1
    _DEBUG=1
)

target_include_directories(render-test PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/../src
    ${CMAKE_SOURCE_DIR}/../src/system
    ${CMAKE_SOURCE_DIR}/../src/system/oggvorbis  # libogg/vorbis bundled headers
    ${CMAKE_SOURCE_DIR}/../src/lazer
    ${CMAKE_SOURCE_DIR}/../include
    ${CMAKE_SOURCE_DIR}/include
)

target_link_options(render-test PRIVATE
    ${DC3_PLATFORM_LINK_OPTIONS}
)

target_link_libraries(render-test PRIVATE
    milo-engine
    ${DC3_PLATFORM_LIBS}
)
target_precompile_headers(render-test PRIVATE
    "$<$<COMPILE_LANGUAGE:CXX>:${DC3_PCH_HEADER}>"
)

# ============================================================================
# Export CLI tools — headless .milo asset converters
# ============================================================================

# Common sources/options for export tools (same as milo-viewer minus VideoEncoder and GLFW rendering)
# Export tools now follow the proven engine-link recipe (dc3-native/milo-viewer/
# render-test): use the engine-filtered core (_ENGINE drops the 30 platform dups
# + audio + DataParser), drop the local gfx set entirely, and LINK milo-engine
# (which provides those gfx/platform TUs). This removes the last consumer of the
# local platform/gfx .cpp duplicates on the native side, so an engine header
# change can no longer break these tools via a stale local .cpp body.
set(DC3_EXPORT_TOOL_SOURCES
    ${DC3_ENGINE_SOURCES}
    ${DC3_NATIVE_CORE_SOURCES_ENGINE}
    ${DC3_EXPORT_SOURCES}
)

set(DC3_EXPORT_TOOL_INCLUDES
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/../src
    ${CMAKE_SOURCE_DIR}/../src/system
    ${CMAKE_SOURCE_DIR}/../src/system/oggvorbis  # libogg/vorbis bundled headers
    ${CMAKE_SOURCE_DIR}/../src/lazer
    ${CMAKE_SOURCE_DIR}/../include
    ${CMAKE_SOURCE_DIR}/include
    # Export tools now LINK milo-engine (see DC3_EXPORT_TOOL_SOURCES above); it
    # provides the gfx/platform TUs and propagates its PUBLIC include dirs. This
    # explicit engine-src add is redundant but harmless (kept for decomp-header
    # resolution edge cases, e.g. Rnd_Wgpu.h's `#include "gfx/UniformStructs.h"`).
    ${MILO_ENGINE_PATH}/src
)

set(DC3_EXPORT_TOOL_LIBS milo-engine imgui ${DC3_PLATFORM_LIBS})

# --- milo-tex-export ---
add_executable(milo-tex-export
    src/tools/milo_tex_export.cpp
    ${DC3_EXPORT_TOOL_SOURCES}
)
target_compile_options(milo-tex-export PRIVATE
    ${DC3_MSVC_COMPAT_FLAGS}
    ${DC3_DECOMP_FLAGS}
    ${DC3_ATOMIC_COMPAT_FLAGS}
)
target_compile_definitions(milo-tex-export PRIVATE HX_NATIVE=1 MILO_DEBUG=1 _DEBUG=1)
target_include_directories(milo-tex-export PRIVATE ${DC3_EXPORT_TOOL_INCLUDES})
target_link_options(milo-tex-export PRIVATE ${DC3_PLATFORM_LINK_OPTIONS})
target_link_libraries(milo-tex-export PRIVATE ${DC3_EXPORT_TOOL_LIBS})
target_precompile_headers(milo-tex-export PRIVATE
    "$<$<COMPILE_LANGUAGE:CXX>:${DC3_PCH_HEADER}>"
)

# --- milo-mat-export ---
add_executable(milo-mat-export
    src/tools/milo_mat_export.cpp
    ${DC3_EXPORT_TOOL_SOURCES}
)
target_compile_options(milo-mat-export PRIVATE
    ${DC3_MSVC_COMPAT_FLAGS}
    ${DC3_DECOMP_FLAGS}
    ${DC3_ATOMIC_COMPAT_FLAGS}
)
target_compile_definitions(milo-mat-export PRIVATE HX_NATIVE=1 MILO_DEBUG=1 _DEBUG=1)
target_include_directories(milo-mat-export PRIVATE ${DC3_EXPORT_TOOL_INCLUDES})
target_link_options(milo-mat-export PRIVATE ${DC3_PLATFORM_LINK_OPTIONS})
target_link_libraries(milo-mat-export PRIVATE ${DC3_EXPORT_TOOL_LIBS})
target_precompile_headers(milo-mat-export PRIVATE
    "$<$<COMPILE_LANGUAGE:CXX>:${DC3_PCH_HEADER}>"
)

# --- milo-pp-dump ---
# Every RndPostProc in a .milo_xbox as JSON lines, loaded through the decomp's
# own RndPostProc::Load (np-gfx-post usage inventory).
add_executable(milo-pp-dump
    src/tools/milo_pp_dump.cpp
    ${DC3_EXPORT_TOOL_SOURCES}
)
target_compile_options(milo-pp-dump PRIVATE
    ${DC3_MSVC_COMPAT_FLAGS}
    ${DC3_DECOMP_FLAGS}
    ${DC3_ATOMIC_COMPAT_FLAGS}
)
target_compile_definitions(milo-pp-dump PRIVATE HX_NATIVE=1 MILO_DEBUG=1 _DEBUG=1)
target_include_directories(milo-pp-dump PRIVATE ${DC3_EXPORT_TOOL_INCLUDES})
target_link_options(milo-pp-dump PRIVATE ${DC3_PLATFORM_LINK_OPTIONS})
target_link_libraries(milo-pp-dump PRIVATE ${DC3_EXPORT_TOOL_LIBS})
target_precompile_headers(milo-pp-dump PRIVATE
    "$<$<COMPILE_LANGUAGE:CXX>:${CMAKE_SOURCE_DIR}/../src/system/decomp_pch.h>"
)

# --- milo2gltf ---
add_executable(milo2gltf
    src/tools/milo2gltf.cpp
    ${DC3_EXPORT_TOOL_SOURCES}
)
target_compile_options(milo2gltf PRIVATE
    ${DC3_MSVC_COMPAT_FLAGS}
    ${DC3_DECOMP_FLAGS}
    ${DC3_ATOMIC_COMPAT_FLAGS}
)
target_compile_definitions(milo2gltf PRIVATE HX_NATIVE=1 MILO_DEBUG=1 _DEBUG=1)
target_include_directories(milo2gltf PRIVATE ${DC3_EXPORT_TOOL_INCLUDES})
target_link_options(milo2gltf PRIVATE ${DC3_PLATFORM_LINK_OPTIONS})
target_link_libraries(milo2gltf PRIVATE ${DC3_EXPORT_TOOL_LIBS})
target_precompile_headers(milo2gltf PRIVATE
    "$<$<COMPILE_LANGUAGE:CXX>:${DC3_PCH_HEADER}>"
)

# ============================================================================
# Test suite — GTest-based tests for .milo loading pipeline
# ============================================================================
option(BUILD_TESTS "Build milo loading test suite" ON)
if(BUILD_TESTS)
    find_package(GTest REQUIRED)
    enable_testing()


    add_executable(milo-tests
        tests/test_helpers.cpp
        tests/telemetry_parser.cpp
        tests/test_gates.cpp
        tests/test_gameplay_telemetry.cpp
        tests/test_binstream.cpp
        tests/test_charbones_serialization.cpp
        tests/test_chunkstream.cpp
        tests/test_dirloader.cpp
        tests/test_milo_diagnostic.cpp
        tests/test_flow_desync.cpp
        tests/test_bink_ffmpeg.cpp
        tests/test_bink_integration.cpp
        tests/test_extract_bik.cpp
        tests/test_input.cpp
        tests/test_audio.cpp
        tests/test_mogg_decode.cpp
        tests/test_mogg_v0xe.cpp
        tests/test_ham_provider.cpp
        tests/test_subsystems.cpp
        tests/test_loader_thread_stack.cpp
        tests/test_bone_ground_truth.cpp
        tests/test_headless_boot.cpp
        tests/test_native_exit.cpp
        tests/test_loading_panel.cpp
        tests/test_preload_budget.cpp
        tests/test_persistent_filecache.cpp
        tests/test_fast_time_audio.cpp
        tests/test_milo_viewer_screenshot.cpp
        tests/test_object_lifetime.cpp
        tests/test_manual_repro.cpp
        tests/test_charclipgroup.cpp
        tests/test_asset_loading.cpp
        tests/test_postproc_noise.cpp
        tests/test_dc3_retail_post.cpp
        tests/test_merge_scope_parity.cpp
        tests/test_merge_lifecycle.cpp
        tests/test_cascade_ref_invariant.cpp
        tests/test_async_unload_lifetime.cpp
        tests/test_archive_enumeration.cpp
        tests/test_mesh_loading.cpp
        tests/test_rndcam_projection.cpp
        tests/test_particle_color.cpp
        tests/test_drawrect_color.cpp
        tests/test_retail_lighting.cpp
        tests/test_retail_char_shading.cpp
        tests/test_dta_parser.cpp
        tests/test_movegraph.cpp
        tests/test_dta_flow.cpp
        tests/test_foot_bone_invariants.cpp
        tests/test_json_parse.cpp
        tests/test_stub_trace.cpp
        tests/test_native_boot_crashes.cpp
        tests/test_null_this_guards.cpp
        tests/test_sound_synthpoll.cpp
        tests/test_dxt5_alpha.cpp
        tests/test_crossfade_jump.cpp
        tests/test_enable_detector.cpp
        tests/test_sha1.cpp
        tests/test_rand_seed.cpp
        tests/test_threadtask_replace.cpp
        tests/test_hamnavlist_transition.cpp
        tests/test_native_animbypass.cpp
        tests/test_ui_message_routing.cpp
        tests/test_shaderprogram_cache_logic.cpp
        tests/test_dta_eval_endpoint.cpp
        tests/test_camera_yuv_decode.cpp
        tests/test_camera_stream.cpp
        tests/test_native_camera_view.cpp
        tests/test_native_crowd.cpp
        tests/test_native_shadow.cpp
        tests/test_native_shadow_stream.cpp
        tests/test_native_shadow_cam.cpp
        tests/test_native_shadow_bodies.cpp
        tests/test_native_shadow_hamchar.cpp
        tests/test_native_additions.cpp
        tests/test_native_gameflow.cpp
        tests/test_native_engineleads.cpp
        tests/test_native_suspects.cpp
        tests/test_native_partyplay.cpp
        tests/test_native_rehearse.cpp
        tests/test_native_lifetime2.cpp
        tests/test_cascade_guards.cpp
        tests/test_native_posesynth.cpp
        tests/test_pose_scoring.cpp
        tests/test_pose_level.cpp
        tests/test_pose_fill.cpp
        tests/test_pose_session.cpp
        tests/test_native_twoplayer.cpp
        tests/test_native_pose_dropout.cpp
        tests/test_native_second_person.cpp
        tests/test_native_pose_socket.cpp
        tests/test_native_pose_spawner.cpp
        tests/test_native_tree_paths.cpp
        tests/test_movie_audio.cpp
        tests/test_texrenderer_clear.cpp
        tests/test_native_gesture_menus.cpp
        tests/test_native_audioseek.cpp
        tests/test_native_notables.cpp
        tests/test_lp64_literals.cpp
        tests/test_clamp_fsel_nan.cpp
        tests/test_native_memory_lp64.cpp
        tests/test_cache_native.cpp
        tests/test_copy_poll_semantics.cpp
        tests/test_native_options.cpp
        tests/test_native_profile.cpp
        tests/test_native_glyph_render.cpp
        tests/test_native_ui_material_terms.cpp
        tests/test_frame_pacer.cpp     # H-29 frame cap + engine CPU-use guards
        src/dc3_render_hook.cpp       # Phase 0.2a: HamRenderHook (registers with milo-engine)
        ${DC3_ENGINE_SOURCES}
        ${DC3_NATIVE_CORE_SOURCES_ENGINE}
        # CharTwistSolver + gfx set -> milo-engine
    )
    target_compile_options(milo-tests PRIVATE
        ${DC3_MSVC_COMPAT_FLAGS}
        ${DC3_DECOMP_FLAGS}
        ${DC3_ATOMIC_COMPAT_FLAGS}
        # -fsanitize=address  # Disabled: complex virtual inheritance VTT issues
    )
    target_compile_definitions(milo-tests PRIVATE
        HX_NATIVE=1
        MILO_DEBUG=1
        _DEBUG=1
        $<$<BOOL:${ENABLE_FFMPEG}>:HX_FFMPEG=1>
    )
    # Absolute path to the decomp source tree. ctest runs the binary with
    # WORKING_DIRECTORY=orig-assets, so a test that needs to read decomp
    # SOURCE (rather than assets) cannot get there by a relative path.
    # Per FILE, not per target: an absolute worktree path in a target-wide
    # define puts that path on all ~900 milo-tests compile lines, and ccache
    # (docs/native/BUILD_CACHE.md) can then never share one of them between
    # worktrees. Add a file here when it starts using the macro.
    set_property(SOURCE
            tests/test_native_options.cpp
            tests/test_native_tree_paths.cpp
            tests/test_null_this_guards.cpp
            tests/test_object_lifetime.cpp
            tests/test_pose_scoring.cpp
        APPEND PROPERTY COMPILE_DEFINITIONS
            "DC3_DECOMP_SRC_DIR=\"${CMAKE_SOURCE_DIR}/../src\"")
    target_include_directories(milo-tests PRIVATE
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}/../src
        ${CMAKE_SOURCE_DIR}/../src/system
        ${CMAKE_SOURCE_DIR}/../src/system/oggvorbis
        ${CMAKE_SOURCE_DIR}/../src/lazer
        ${CMAKE_SOURCE_DIR}/../include
        ${CMAKE_SOURCE_DIR}/tests
        ${FFMPEG_INCLUDE_DIRS}
    )
    target_link_options(milo-tests PRIVATE
        # -fsanitize=address  # Disabled: complex virtual inheritance VTT issues
        ${DC3_PLATFORM_LINK_OPTIONS}
    )
    target_link_libraries(milo-tests PRIVATE
        milo-engine
        GTest::gtest GTest::gtest_main
        ${DC3_PLATFORM_LIBS}
        ${FFMPEG_LIBRARIES}
    )
    target_precompile_headers(milo-tests PRIVATE
        "$<$<COMPILE_LANGUAGE:CXX>:${DC3_PCH_HEADER}>"
    )
    # H-9 exit tests: Xlib (a second X client acting on the engine's window)
    # and no decomp header, so no PCH and no MSVC compat.
    set_source_files_properties(
        ${CMAKE_SOURCE_DIR}/tests/test_native_exit.cpp
        TARGET_DIRECTORY milo-tests
        PROPERTIES SKIP_PRECOMPILE_HEADERS ON
                   COMPILE_OPTIONS "-fno-ms-extensions;-fno-ms-compatibility;-fno-delayed-template-parsing"
    )
    if(NOT APPLE)
        find_package(X11 REQUIRED)
        target_link_libraries(milo-tests PRIVATE X11::X11)
    endif()
    include(GoogleTest)

    # Ensure the tmp directory exists for ASSERT_EXIT tests (GTest needs writable TMPDIR)
    file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/tmp")

    # Asset-aware working directory: the engine resolves game assets (gen/*.hdr,
    # config/*.dta, char/*.milo_xbox, ...) relative to the process CWD. Without a
    # WORKING_DIRECTORY, gtest_discover_tests spawns each test subprocess with
    # cwd = the build dir, so EVERY asset load fails ("Can't read gen/main_xbox.hdr")
    # and ~half the suite reports spurious failures. Resolve the assets dir at
    # configure time: prefer the in-tree orig-assets (a real dir in the main repo,
    # a symlink in worktrees), then fall back to the canonical main-repo path.
    set(MILO_TEST_ASSET_DIR "")
    foreach(_candidate
            "${CMAKE_SOURCE_DIR}/../orig-assets"
            "/home/free/code/milohax/dc3-decomp/orig-assets")
        if(EXISTS "${_candidate}/gen/main_xbox.hdr")
            get_filename_component(MILO_TEST_ASSET_DIR "${_candidate}" ABSOLUTE)
            break()
        endif()
    endforeach()
    if(MILO_TEST_ASSET_DIR STREQUAL "")
        # No assets found. The working directory must still EXIST: it is also
        # where gtest_discover_tests runs `milo-tests --gtest_list_tests` as a
        # POST_BUILD step, and a missing directory fails that step ("Result: no
        # such file or directory") and with it the whole milo-tests build. That
        # is exactly how the GitHub native-build job died once the runner image
        # could compile the tree (no orig-assets in CI). Use an empty dir in the
        # build tree; asset-backed tests SKIP/FAIL loudly on their own.
        set(MILO_TEST_ASSET_DIR "${CMAKE_CURRENT_BINARY_DIR}/no-orig-assets")
        file(MAKE_DIRECTORY "${MILO_TEST_ASSET_DIR}")
        message(WARNING "milo-tests: orig-assets/gen/main_xbox.hdr not found; "
                        "asset-backed tests will fail or skip. Looked under "
                        "${CMAKE_SOURCE_DIR}/../orig-assets and the main-repo path. "
                        "Tests run in ${MILO_TEST_ASSET_DIR}.")
    else()
        message(STATUS "milo-tests asset dir (ctest WORKING_DIRECTORY): ${MILO_TEST_ASSET_DIR}")
    endif()

    # Each CTest test spawns a separate milo-tests process that initializes
    # Dawn/WebGPU + audio (miniaudio). Running 32+ simultaneously exhausts GPU
    # device handles, file descriptors, and shared memory — causing spurious
    # SEGFAULTs. RESOURCE_LOCK serializes all in-process tests; subprocess tests
    # (HeadlessBootTest, MiloViewerScreenshot) don't init the engine and can
    # still run in parallel.
    #
    # The lock file is generated at configure time and included by CTest after
    # gtest_discover_tests populates the test list (if(TEST) doesn't work in
    # CTest include context, so we apply unconditionally and tolerate warnings
    # for tests that don't exist in a given configuration).
    # ------------------------------------------------------------------
    # Optional-suite gates
    # ------------------------------------------------------------------
    # A large part of the suite is env-gated and therefore SKIPS by default,
    # while `ctest` still prints "100% tests passed" — skips are counted as
    # passes. The 2026-08-19 toolchain audit found 79 of 441 skipping, and the
    # skipped tier is where two live native bugs were hiding.
    #
    # Gates whose prerequisites we can *prove* are present at configure time are
    # turned on here, so the default run is as wide as the box allows. Each one
    # is probed, never assumed: on a machine without the assets the probe fails,
    # the gate stays off, and the suite degrades to skipping rather than failing.
    #
    # MILO_TEST_GATE_STATUS is compiled into the binary so a test can check that
    # what CMake decided actually reached the test process (see TestGates below).
    set(MILO_TEST_ENV "TMPDIR=${CMAKE_CURRENT_BINARY_DIR}/tmp")
    # The dev profile (DC3_PROFILE, the default) starts the HTTP debug server,
    # and a windowed run prefers port 9090.  A test never takes 9090 from a
    # developer's running game: kernel-assigned unless the test names a port
    # (the port-file helpers read the one it got).
    list(APPEND MILO_TEST_ENV "DC3_HTTP_PORT=0")
    set(MILO_TEST_GATES_ON "")
    set(MILO_TEST_GATES_OFF "")
    # MILO_TEST_GATE_PROBES records, for every probed gate, the exact path whose
    # existence decided it -- ON or OFF. TestGates re-runs those probes at RUN
    # time and fails if a gate the source's own rule would now turn ON is baked
    # OFF. Without this the gate decision is frozen at the last configure: the
    # assets can appear on disk afterwards and nothing re-asks, because CMake
    # only re-runs when a CMakeLists changes. Format: NAME=path (one per entry).
    set(MILO_TEST_GATE_PROBES "")

    # MILO_LIB — extracted DC3 milo-rnd-library. Gates CharClipGroupTest (2) and
    # parts of MiloViewerScreenshot / merge-parity. test_asset_loading.cpp's
    # GetMiloLibRoot() already defaults to this path, so the two CharClipGroup
    # tests were skipping on a box where the assets are present.
    set(MILO_LIB_DIR "")
    set(_milo_lib_default
        "$ENV{HOME}/code/milohax/milo-engine-libs/harmonix-repos/milo-rnd-library/dc3")
    foreach(_cand "$ENV{MILO_LIB}" "${_milo_lib_default}")
        if(_cand AND EXISTS "${_cand}/char/crowd/anim/gen/female_base.milo_xbox")
            set(MILO_LIB_DIR "${_cand}")
            break()
        endif()
    endforeach()
    # Probe recorded against the DEFAULT candidate: $ENV{MILO_LIB} is a property
    # of whoever ran cmake and is not reproducible from the test process.
    list(APPEND MILO_TEST_GATE_PROBES
        "MILO_LIB=${_milo_lib_default}/char/crowd/anim/gen/female_base.milo_xbox")
    if(MILO_LIB_DIR)
        list(APPEND MILO_TEST_ENV "MILO_LIB=${MILO_LIB_DIR}")
        list(APPEND MILO_TEST_GATES_ON "MILO_LIB")
    else()
        list(APPEND MILO_TEST_GATES_OFF "MILO_LIB(no milo-rnd-library/dc3)")
    endif()

    # DC3_DTA_FLOW_TESTS — 7 DtaFlowTest cases. They need the game assets, which
    # is exactly what MILO_TEST_ASSET_DIR was just proven to contain. The audit
    # confirmed all 7 pass when enabled; they were pure lost coverage.
    list(APPEND MILO_TEST_GATE_PROBES
        "DC3_DTA_FLOW_TESTS=${MILO_TEST_ASSET_DIR}/gen/main_xbox.hdr")
    if(EXISTS "${MILO_TEST_ASSET_DIR}/gen/main_xbox.hdr")
        list(APPEND MILO_TEST_ENV "DC3_DTA_FLOW_TESTS=1")
        list(APPEND MILO_TEST_GATES_ON "DC3_DTA_FLOW_TESTS")
    else()
        list(APPEND MILO_TEST_GATES_OFF "DC3_DTA_FLOW_TESTS(no game assets)")
    endif()

    # Deliberately NOT defaulted on, with the reason recorded so the next person
    # does not have to re-derive it:
    #   DC3_GAMEPLAY_TESTS — 48 GameplayTelemetryTest cases behind one ~2-minute
    #                        9050-frame engine run, plus EmptySensorGameplayTest
    #                        (one ~200 s whole-song run with scoring off).
    #                        Too slow for the default loop; run it explicitly.
    #   DC3_AUDIO_TESTS    — real audio device; contends under ctest -j.
    #   MILO_LONG_TEST     — HeadlessBootTest.LongRunStability, ~5 minutes.
    list(APPEND MILO_TEST_GATES_OFF
        "DC3_GAMEPLAY_TESTS(slow - 48 tests behind one ~2min engine run + 1 ~200s song)"
        "DC3_AUDIO_TESTS(audio device contention)"
        "MILO_LONG_TEST(~5min)")

    string(REPLACE ";" "," _gates_on_str "${MILO_TEST_GATES_ON}")
    string(REPLACE ";" "," _gates_off_str "${MILO_TEST_GATES_OFF}")
    string(REPLACE ";" "|" _gate_probes_str "${MILO_TEST_GATE_PROBES}")

    # Build-currency inputs for TestGates.BuildMatchesSources.
    #
    # The gate table alone was not enough. On 2026-08-23 the main checkout's
    # native/build was three days old: `scripts/native_test.sh` runs ctest and
    # never built, so the suite ran an Aug-20 milo-tests against an Aug-23
    # source tree. It registered 449 tests where a fresh configure registers
    # 504, reported 380/380 green, and TestGates PASSED -- because the gates it
    # checks really were delivered, to a binary that was simply not this tree's.
    # The instrument verified everything about the run except that the run was
    # about the current source.
    #
    # There is no heuristic here: the test asks ninja, using the real dependency
    # graph, whether the targets the suite exercises are up to date. (Three of
    # them -- see MILO_TEST_REQUIRED_TARGETS below.)
    set(_ninja_exe "")
    if(CMAKE_GENERATOR MATCHES "Ninja")
        set(_ninja_exe "${CMAKE_MAKE_PROGRAM}")
    endif()

    # Every binary the suite drives, in ONE place.
    #
    # (`all` used to be unbuildable because of wgpu-window-test, broken from
    # 2026-03-25 until it was deleted on 2026-11-13. `all` now builds, but it
    # also builds targets the suite never runs, and the point here is the exact
    # set the suite drives, so the list stays.)
    #
    # A hand-maintained list is what let milo-viewer go unbuilt and cost 5 tests
    # to a skip, so the list is defined once here and BOTH consumers read it:
    # scripts/native_test.sh builds it (via the generated file below) and
    # TestGates.BuildMatchesSources verifies its currency. If one drifts, the
    # other fails.
    set(MILO_TEST_REQUIRED_TARGETS milo-tests dc3-native milo-viewer)
    string(REPLACE ";" " " _req_targets_str "${MILO_TEST_REQUIRED_TARGETS}")
    file(GENERATE
        OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/milo_test_required_targets.txt"
        CONTENT "${_req_targets_str}\n")
    # The ctest WORKING_DIRECTORY, for scripts/native_test.sh: --no-assets
    # refuses to run when this dir HAS game data (that mode's floor is a much
    # weaker check than the default skip budget, so it must not be usable to
    # dodge it), and the default run names a data-less dir when it exits 2.
    file(GENERATE
        OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/milo_test_asset_dir.txt"
        CONTENT "${MILO_TEST_ASSET_DIR}\n")

    # Only test_gates.cpp reads these, and two of them are absolute paths of
    # this build dir; per file for the same ccache reason as DC3_DECOMP_SRC_DIR.
    set_property(SOURCE tests/test_gates.cpp APPEND PROPERTY COMPILE_DEFINITIONS
        "MILO_TEST_GATES_ON_STR=\"${_gates_on_str}\""
        "MILO_TEST_GATES_OFF_STR=\"${_gates_off_str}\""
        "MILO_TEST_GATE_PROBES_STR=\"${_gate_probes_str}\""
        "MILO_TEST_BINARY_DIR_STR=\"${CMAKE_CURRENT_BINARY_DIR}\""
        "MILO_TEST_GENERATOR_STR=\"${CMAKE_GENERATOR}\""
        "MILO_TEST_NINJA_STR=\"${_ninja_exe}\""
        "MILO_TEST_REQUIRED_TARGETS_STR=\"${_req_targets_str}\"")

    message(STATUS "milo-tests gates ON : ${_gates_on_str}")
    message(STATUS "milo-tests gates OFF: ${_gates_off_str}")

    gtest_discover_tests(milo-tests
        WORKING_DIRECTORY "${MILO_TEST_ASSET_DIR}"
        PROPERTIES
            ENVIRONMENT "${MILO_TEST_ENV}"
    )

    # LinkHygiene.*: dc3-native must link without --allow-multiple-definition
    # (Apple ld has no equivalent; on Linux the flag silently keeps whichever
    # duplicate links first). The check relinks dc3-native from ninja's own
    # link command with the flag removed, into a temp file, and compares the
    # duplicate set with native/tests/link_duplicates_allow.txt. SelfTest is
    # its negative control: it injects a duplicate and requires it be seen.
    # GNU ld / lld diagnostics only, so Linux only.
    find_program(MILO_PYTHON3_LINK python3)
    if(MILO_PYTHON3_LINK AND CMAKE_SYSTEM_NAME STREQUAL "Linux" AND _ninja_exe)
        add_test(NAME LinkHygiene.DuplicateDefinitions
            COMMAND ${MILO_PYTHON3_LINK} ${CMAKE_SOURCE_DIR}/../scripts/native/check_link_duplicates.py
                --build-dir ${CMAKE_CURRENT_BINARY_DIR} --target dc3-native --ninja ${_ninja_exe}
                --allow ${CMAKE_SOURCE_DIR}/tests/link_duplicates_allow.txt)
        add_test(NAME LinkHygiene.DuplicateCheckSelfTest
            COMMAND ${MILO_PYTHON3_LINK} ${CMAKE_SOURCE_DIR}/../scripts/native/check_link_duplicates.py
                --build-dir ${CMAKE_CURRENT_BINARY_DIR} --target dc3-native --ninja ${_ninja_exe}
                --selftest)
        set_tests_properties(LinkHygiene.DuplicateDefinitions LinkHygiene.DuplicateCheckSelfTest
            PROPERTIES LABELS LinkHygiene TIMEOUT 300)
    endif()

    # PoseServer.Python: native/scripts/pose_server.py's camera mode -- capture
    # request, no-camera / lost-camera exit codes, capture timestamps, HFOV --
    # against a fake capture object (tests/test_pose_server.py, stdlib
    # unittest; no camera, no GPU). Its real-script cases need cv2 and skip
    # inside the file, not here, without it.
    if(MILO_PYTHON3_LINK)
        add_test(NAME PoseServer.Python
            COMMAND ${MILO_PYTHON3_LINK} -B ${CMAKE_SOURCE_DIR}/../tests/test_pose_server.py)
        set_tests_properties(PoseServer.Python PROPERTIES
            LABELS PoseServer TIMEOUT 120 ENVIRONMENT "${MILO_TEST_ENV}")
        # PoseFixture.Python: the keypoint-replay fixture that
        # scripts/pose_scoring_gate.sh replays (native/tests/data/pose/) --
        # a plausible, DANCING skeleton; song-time keyed replay; --replay-still
        # holds frame 0 (tests/test_pose_fixture.py; no GPU). Re-recording
        # determinism runs when the gitignored source video is present.
        add_test(NAME PoseFixture.Python
            COMMAND ${MILO_PYTHON3_LINK} -B ${CMAKE_SOURCE_DIR}/../tests/test_pose_fixture.py)
        set_tests_properties(PoseFixture.Python PROPERTIES
            LABELS PoseServer TIMEOUT 300 ENVIRONMENT "${MILO_TEST_ENV}")
        # PoseTracker.Python: CentroidTracker keeps a dancer's id through a
        # MediaPipe dropout (PLAN P-12), and the web port gives the same ids
        # (tests/test_pose_tracker.py; node for the parity half, skipped in
        # the file without it).
        add_test(NAME PoseTracker.Python
            COMMAND ${MILO_PYTHON3_LINK} -B ${CMAKE_SOURCE_DIR}/../tests/test_pose_tracker.py)
        set_tests_properties(PoseTracker.Python PROPERTIES
            LABELS PoseServer TIMEOUT 120 ENVIRONMENT "${MILO_TEST_ENV}")
        # PlaytestKit.Python: the playtest kit's own tools (scripts/playtest/:
        # pose-session cutting, the session summary, the player-box asset check,
        # the kit driver's null-screen guard) on synthetic files
        # (tests/test_playtest_kit.py; stdlib only, no game data, no GPU).
        add_test(NAME PlaytestKit.Python
            COMMAND ${MILO_PYTHON3_LINK} -B ${CMAKE_SOURCE_DIR}/../tests/test_playtest_kit.py)
        set_tests_properties(PlaytestKit.Python PROPERTIES
            LABELS PoseServer TIMEOUT 120 ENVIRONMENT "${MILO_TEST_ENV}")
    endif()

    # XeniaGolden.*: the native port against state recorded from the ORIGINAL
    # debug.xex under Xenia (docs/plans/XENIA_ORACLE.md, Tier A). Each test
    # boots dc3-native headless to title_screen on a private port, runs the
    # loader probe over the golden's .milo files and compares every field with
    # ${DC3_XENIA_GOLDEN_DIR}/<bundle>/golden.json. No Xenia is needed.
    # The bundles are property dumps of shipped game content, so they live in
    # the gitignored archive/ (setup_worktree.sh symlinks it into worktrees),
    # never in git. Exit 1 = an unadjudicated disagreement; 77 = skip (bundle
    # absent, or dc3-native not built), with the reason printed.
    set(DC3_XENIA_GOLDEN_DIR "${CMAKE_SOURCE_DIR}/../archive/state_diff/goldens/xenia"
        CACHE PATH "Directory holding the Xenia golden bundles (game-derived, not in git)")
    find_program(MILO_PYTHON3 python3)
    if(MILO_PYTHON3)
        foreach(_golden loader_char_v1 loader_venue_song_v1)
            add_test(NAME XeniaGolden.${_golden}
                COMMAND ${MILO_PYTHON3} -m state_diff.golden check
                    "${DC3_XENIA_GOLDEN_DIR}/${_golden}"
                    --native-binary "$<TARGET_FILE:dc3-native>"
                    --work-dir "${CMAKE_CURRENT_BINARY_DIR}/xenia_golden/${_golden}"
                WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}/../tools")
            set_tests_properties(XeniaGolden.${_golden} PROPERTIES
                LABELS XeniaGolden
                SKIP_RETURN_CODE 77
                TIMEOUT 600)
        endforeach()
    endif()
endif()

endif() # NOT EMSCRIPTEN
