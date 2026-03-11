cmake_minimum_required(VERSION 3.22)

# This project requires Clang due to MSVC compatibility flags (-fms-extensions, etc.)
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

project(dc3-native LANGUAGES C CXX)

# Verify we're using Clang (the MSVC compat flags won't work with GCC)
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

# MSVC compatibility flags — applied per-target (dc3-native only), not globally.
# Standalone tools (wgpu-window-test, milo-viewer) use standard C++ compilation.
set(DC3_MSVC_COMPAT_FLAGS
    -fms-extensions          # __declspec support (xdk headers)
    -fms-compatibility       # Full MSVC compat (dependent base class lookup)
    -fms-compatibility-version=19.29  # VS2019+ — char16_t/char32_t as builtins
    -fdelayed-template-parsing  # MSVC-style template parsing
    # GCC 15 headers: clang in MSVC mode drops __GNUC__ macros, breaking
    # glibc's sys/cdefs.h and libstdc++'s atomic_base.h. These defines restore
    # the compiler builtins that GCC 15 headers expect.
    -D__GNUC_STDC_INLINE__
    -D__GCC_ATOMIC_TEST_AND_SET_TRUEVAL=1
)
# GCC 15 <atomic>/<thread>/<mutex> need __GCC_ATOMIC_*_LOCK_FREE builtins,
# which clang drops in -fms-compatibility mode. Define them all (=2 on x86_64).
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
set(DC3_DECOMP_FLAGS
    -ferror-limit=0
    -fno-omit-frame-pointer
)
if(LP64_AUDIT)
    list(APPEND DC3_DECOMP_FLAGS -Wno-everything -Wpointer-to-int-cast -Wint-to-pointer-cast)
else()
    list(APPEND DC3_DECOMP_FLAGS -w)
endif()

# MSVC STL compat: shadow libc++'s __wrap_iter with a patched version that
# adds implicit conversion to the underlying pointer type. This makes
# vector::begin()/end() results implicitly convertible to T*, matching
# Xbox 360 MSVC's old STL behavior and fixing 600+ call sites.
include_directories(BEFORE SYSTEM ${CMAKE_SOURCE_DIR}/include)

find_package(Threads REQUIRED)
find_package(Dawn REQUIRED)
find_package(glfw3 REQUIRED)

# FFmpeg — used for Bink video/audio decoding (replaces RAD Bink SDK)
option(ENABLE_FFMPEG "Enable FFmpeg for Bink video/audio decoding" ON)
if(ENABLE_FFMPEG)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(FFMPEG REQUIRED libavformat libavcodec libswscale libavutil)
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
    ${CMAKE_SOURCE_DIR}/../src/system/synth/BitCrushEffect.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/synth/ByteGrinder.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/synth/CompressionEffect.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/synth/DelayEffect.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/synth/DistortionEffect.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/synth/EQEffect.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/synth/Emitter.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/synth/Faders.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/synth/FlangerEffect.cpp
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
    ${CMAKE_SOURCE_DIR}/../src/system/synth/WahEffect.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/synth/WavMgr.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/synth/WavReader.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/synth/complex.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/synth/filterdesign.cpp
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
    ${CMAKE_SOURCE_DIR}/../src/system/utl/NetLoader.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/utl/OSCMessenger.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/utl/Option.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/utl/Pool.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/utl/PoolAlloc.cpp
    ${CMAKE_SOURCE_DIR}/../src/system/utl/Profiler.cpp
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
    src/platform/Memory_Native.cpp
    src/platform/PlatformMgr_Native.cpp
    src/platform/System_Native.cpp
    src/platform/File_Native.cpp
    src/platform/AsyncFile_Native.cpp
    src/platform/CDReader_Native.cpp
    src/platform/ThreadCall_Native.cpp
    src/platform/Joypad_Native.cpp
    src/platform/Keyboard_Native.cpp
    src/platform/Synth_Stub.cpp
    src/platform/Skeleton_Native.cpp
    src/platform/GestureMgr_Native.cpp
    src/audio/AudioDevice.cpp
    src/platform/StreamReceiver_Native.cpp
    src/platform/SampleInst_Native.cpp
    src/platform/FxSendNative.cpp
    src/platform/Rnd_Wgpu.cpp
    src/platform/MeshFilter.cpp
    src/platform/BoneSetup.cpp
    src/platform/MeshGpuCache.cpp
    src/platform/MaterialSetup.cpp
    src/platform/Mesh_Wgpu.cpp
    src/platform/TransparentQueue.cpp
    src/platform/Part_Wgpu.cpp
    src/platform/Tex_Wgpu.cpp
    src/platform/Cam_Native.cpp
    src/platform/Net_Stub.cpp
    src/platform/Memcard_Stub.cpp
    src/platform/ContentMgr_Stub.cpp
    src/platform/MapFile_Stub.cpp
    src/platform/VirtualKeyboard_Stub.cpp
    src/platform/Achievements_Stub.cpp
    src/platform/MemcardMgr_Stub.cpp
    src/platform/BinkMovie_Stub.cpp
    $<$<BOOL:${ENABLE_FFMPEG}>:src/platform/FFmpegAudioReader.cpp>
    $<$<BOOL:${ENABLE_FFMPEG}>:src/platform/FFmpegMovieImpl.cpp>
    src/platform/NetXbox_Stub.cpp
    src/platform/KinectShare_Stub.cpp
    src/platform/Cache_Stub.cpp
    src/platform/ChecksumData_Stub.cpp
    src/platform/Keygen_Stub.cpp
    src/platform/NetworkSocket_Stub.cpp
    src/platform/SynthCommon_Stub.cpp
    src/platform/RndTex_Native.cpp
    src/engine_stubs_generated.cpp
    src/native_link_glue.cpp
    src/thunk_stubs.cpp
)

# Full native sources (core + entry point)
set(DC3_NATIVE_SOURCES
    src/main_native.cpp
    ${DC3_NATIVE_CORE_SOURCES}
)

# ============================================================================
# Graphics subsystem sources (WebGPU/Dawn)
# ============================================================================

# Core GPU utilities (no decomp dependencies — standalone tools can use these)
set(DC3_GFX_CORE_SOURCES
    src/gfx/GpuDevice.cpp
)

# Full graphics sources (depend on decomp types like RndBitmap, RndMesh)
set(DC3_GFX_SOURCES
    ${DC3_GFX_CORE_SOURCES}
    src/gfx/Screenshot.cpp
    src/gfx/TextureConvert.cpp
    src/gfx/VertexFormats.cpp
    src/gfx/PipelineManager.cpp
    src/gfx/FrameCapture.cpp
    src/gfx/ShadowPass.cpp
    src/gfx/BloomPass.cpp
    src/gfx/DofPass.cpp
    src/gfx/PostProcPass.cpp
    src/gfx/DrawRect2D.cpp
    src/gfx/GpuResourceRegistry.cpp
    src/gfx/mikktspace.c
)
set_source_files_properties(src/gfx/mikktspace.c PROPERTIES LANGUAGE C)

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
# Build target
# ============================================================================

add_executable(dc3-native
    ${DC3_ENGINE_SOURCES}
    ${DC3_NATIVE_SOURCES}
    ${DC3_GFX_SOURCES}
)

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
    $<$<BOOL:${ENABLE_FFMPEG}>:HX_FFMPEG=1>
)

# Include paths matching Xbox build structure
target_include_directories(dc3-native PRIVATE
    ${CMAKE_SOURCE_DIR}/src             # Native-specific headers (shims take priority)
    ${CMAKE_SOURCE_DIR}/../src          # Main source root
    ${CMAKE_SOURCE_DIR}/../src/system   # system/ headers use bare includes
    ${CMAKE_SOURCE_DIR}/../src/lazer    # lazer/ headers use bare includes (game/, meta_ham/, etc.)
    # NOTE: src/xdk/LIBCMT is NOT included - it shadows system headers with Xbox-specific
    # types (size_t=unsigned int, etc.) that break 64-bit compilation
    ${CMAKE_SOURCE_DIR}/../include      # Additional headers
    ${FFMPEG_INCLUDE_DIRS}
)

target_link_options(dc3-native PRIVATE
    -rdynamic
    # -fsanitize=address  # Disabled: complex virtual inheritance VTT issues  # ASan link flag
    -Wl,--unresolved-symbols=ignore-all  # Non-virtual thunks for stubbed functions
    -Wl,--warn-unresolved-symbols  # Warn but don't error
)

target_link_libraries(dc3-native PRIVATE
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
    ${FFMPEG_LIBRARIES}
)

# NOTE: GFX sources include BOTH Dawn/WebGPU headers AND decomp engine headers.
# The -D__GNUC_STDC_INLINE__ flag (in DC3_MSVC_COMPAT_FLAGS) resolves the GCC 15
# glibc header conflict, allowing -fms-compatibility to coexist with Dawn headers.

# C files need special handling (some are .c but included in C++ project)
set_source_files_properties(
    ${CMAKE_SOURCE_DIR}/../src/system/obj/DataFlex.c
    PROPERTIES LANGUAGE C
)

# ============================================================================
# Standalone WebGPU window test (does not link engine — just GLFW + Dawn)
# ============================================================================
add_executable(wgpu-window-test
    src/main.cpp
    ${DC3_GFX_CORE_SOURCES}
)
target_include_directories(wgpu-window-test PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)
target_link_libraries(wgpu-window-test PRIVATE
    Threads::Threads
    dawn::webgpu_dawn
    glfw
)

# ============================================================================
# Milo Viewer — loads .milo_xbox files with orbit camera
# ============================================================================
add_executable(milo-viewer
    src/viewer/milo_viewer.cpp
    src/viewer/ViewerArgs.cpp
    src/viewer/ViewerCamera.cpp
    src/char/CharTwistSolver.cpp
    src/viewer/ViewerPoseDump.cpp
    src/viewer/ViewerScene.cpp
    src/viewer/ViewerAnimation.cpp
    src/viewer/ViewerCapture.cpp
    src/gfx/VideoEncoder.cpp
    ${DC3_ENGINE_SOURCES}
    ${DC3_NATIVE_CORE_SOURCES}
    ${DC3_GFX_SOURCES}
    ${DC3_EXPORT_SOURCES}
)

target_compile_options(milo-viewer PRIVATE
    ${DC3_MSVC_COMPAT_FLAGS}
    ${DC3_DECOMP_FLAGS}
    ${DC3_ATOMIC_COMPAT_FLAGS}
    # -fsanitize=address  # Disabled: complex virtual inheritance VTT issues
)

target_compile_definitions(milo-viewer PRIVATE
    HX_NATIVE=1
    MILO_DEBUG=1
    _DEBUG=1
)

target_include_directories(milo-viewer PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/../src
    ${CMAKE_SOURCE_DIR}/../src/system
    ${CMAKE_SOURCE_DIR}/../src/lazer
    ${CMAKE_SOURCE_DIR}/../include
    ${CMAKE_SOURCE_DIR}/include  # cgltf headers
)

target_link_options(milo-viewer PRIVATE
    -rdynamic
    # -fsanitize=address  # Disabled: complex virtual inheritance VTT issues
    -Wl,--unresolved-symbols=ignore-all
    -Wl,--warn-unresolved-symbols
    -Wl,--allow-multiple-definition
)

target_link_libraries(milo-viewer PRIVATE
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

# ============================================================================
# Render Test — programmatic test scenes for renderer validation
# ============================================================================

add_executable(render-test
    src/render_test/render_test_main.cpp
    src/render_test/test_scene.cpp
    ${DC3_ENGINE_SOURCES}
    ${DC3_NATIVE_CORE_SOURCES}
    ${DC3_GFX_SOURCES}
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
    ${CMAKE_SOURCE_DIR}/../src/lazer
    ${CMAKE_SOURCE_DIR}/../include
    ${CMAKE_SOURCE_DIR}/include
)

target_link_options(render-test PRIVATE
    -rdynamic
    -Wl,--unresolved-symbols=ignore-all
    -Wl,--warn-unresolved-symbols
    -Wl,--allow-multiple-definition
)

target_link_libraries(render-test PRIVATE
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

# ============================================================================
# Export CLI tools — headless .milo asset converters
# ============================================================================

# Common sources/options for export tools (same as milo-viewer minus VideoEncoder and GLFW rendering)
set(DC3_EXPORT_TOOL_SOURCES
    ${DC3_ENGINE_SOURCES}
    ${DC3_NATIVE_CORE_SOURCES}
    ${DC3_GFX_SOURCES}
    ${DC3_EXPORT_SOURCES}
)

set(DC3_EXPORT_TOOL_INCLUDES
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/../src
    ${CMAKE_SOURCE_DIR}/../src/system
    ${CMAKE_SOURCE_DIR}/../src/lazer
    ${CMAKE_SOURCE_DIR}/../include
    ${CMAKE_SOURCE_DIR}/include
)

set(DC3_EXPORT_TOOL_LIBS
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
target_link_options(milo-tex-export PRIVATE
    -rdynamic -Wl,--unresolved-symbols=ignore-all -Wl,--warn-unresolved-symbols -Wl,--allow-multiple-definition
)
target_link_libraries(milo-tex-export PRIVATE ${DC3_EXPORT_TOOL_LIBS})

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
target_link_options(milo-mat-export PRIVATE
    -rdynamic -Wl,--unresolved-symbols=ignore-all -Wl,--warn-unresolved-symbols -Wl,--allow-multiple-definition
)
target_link_libraries(milo-mat-export PRIVATE ${DC3_EXPORT_TOOL_LIBS})

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
target_link_options(milo2gltf PRIVATE
    -rdynamic -Wl,--unresolved-symbols=ignore-all -Wl,--warn-unresolved-symbols -Wl,--allow-multiple-definition
)
target_link_libraries(milo2gltf PRIVATE ${DC3_EXPORT_TOOL_LIBS})

# ============================================================================
# Test suite — GTest-based tests for .milo loading pipeline
# ============================================================================
option(BUILD_TESTS "Build milo loading test suite" ON)
if(BUILD_TESTS)
    find_package(GTest REQUIRED)
    enable_testing()


    add_executable(milo-tests
        tests/test_helpers.cpp
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
        tests/test_ham_provider.cpp
        tests/test_subsystems.cpp
        tests/test_bone_ground_truth.cpp
        tests/test_headless_boot.cpp
        tests/test_milo_viewer_screenshot.cpp
        tests/test_object_lifetime.cpp
        tests/test_asset_loading.cpp
        tests/test_rndcam_projection.cpp
        ${DC3_ENGINE_SOURCES}
        ${DC3_NATIVE_CORE_SOURCES}
        ${DC3_GFX_SOURCES}
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
    target_include_directories(milo-tests PRIVATE
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}/../src
        ${CMAKE_SOURCE_DIR}/../src/system
        ${CMAKE_SOURCE_DIR}/../src/lazer
        ${CMAKE_SOURCE_DIR}/../include
        ${CMAKE_SOURCE_DIR}/tests
        ${FFMPEG_INCLUDE_DIRS}
    )
    target_link_options(milo-tests PRIVATE
        -rdynamic
        # -fsanitize=address  # Disabled: complex virtual inheritance VTT issues
        -Wl,--unresolved-symbols=ignore-all
        -Wl,--warn-unresolved-symbols
        -Wl,--allow-multiple-definition
    )
    target_link_libraries(milo-tests PRIVATE
        GTest::gtest GTest::gtest_main Threads::Threads
        dawn::webgpu_dawn glfw dl curl jpeg vorbis vorbisfile ogg z
        ${FFMPEG_LIBRARIES}
    )
    include(GoogleTest)
    gtest_discover_tests(milo-tests)
endif()
