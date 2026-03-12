// Auto-generated stubs for DC3 Native Port
// Provides stub implementations for all undefined symbols
// This allows linking - stubs return 0/nullptr at runtime

#include <cstdio>
#include <cstdlib>
#include <cstring>

class DataArray;

extern "C" {

// Function stubs with incorrect signatures — skip on Emscripten where the
// real definitions exist in decomp source and wasm-ld flags the mismatch.
#ifndef __EMSCRIPTEN__
int BinkGetError() { return 0; }
int BinkGoto() { return 0; }
int BinkInit() { return 0; }
int BinkOpen() { return 0; }
int BinkSetSoundTrack() { return 0; }
int BinkSetVideoOnOff() { return 0; }
int ctr_reinit() { return 0; }
int ctr_start() { return 0; }
int D3DResource_Release() { return 0; }
__attribute__((weak)) int DataInput(void*, int) { return 0; }
int DmCaptureStackBackTrace() { return 0; }
int DmGetSystemInfo() { return 0; }
int DmMapDevkitDrive() { return 0; }
struct complex { double x, y; };
void expand(complex*, int, complex*, ...) {}
complex expj(double) { return {0, 0}; }
int FileRecursePattern() { return 0; }
int FileTimeToSystemTime() { return 0; }
#endif // !__EMSCRIPTEN__
void* gCharHighlightY = 0;
// void* gCheatsManager = 0; // now defined in Cheats.cpp
// void* gDataThisPtr = 0; // now defined in DataFunc.cpp
// void* gDebugDepth = 0; // now defined in LiveCameraInput.cpp
#ifndef __EMSCRIPTEN__
int GetLastError() { return 0; }
int GetOverlappedResult() { return 0; }
int GetTimeZoneInformation() { return 0; }
#endif
void* gMemStackLock = 0;
#ifndef __EMSCRIPTEN__
int JoypadSetActuatorsImp() { return 0; }
int json_object_array_get_idx() { return 0; }
int json_object_array_length() { return 0; }
int json_object_get() { return 0; }
int json_object_get_boolean() { return 0; }
int json_object_get_int() { return 0; }
int json_object_get_object() { return 0; }
int json_object_get_string() { return 0; }
int json_object_get_type() { return 0; }
int json_object_new_array() { return 0; }
int json_object_put() { return 0; }
int json_tokener_parse() { return 0; }
#endif
extern "C" float lbl_82F0BE80 = 2.0f;
int lbl_82F14008 = 0;
DataArray *lbl_830A4100 = nullptr;
int lbl_830A4104 = 0;
unsigned int lbl_82F1AB98 = 60;     // seconds per minute
unsigned int lbl_82F1AB9C = 3600;   // seconds per hour
unsigned int lbl_82F1ABA0 = 86400;  // seconds per day
void* lbl_8316EB70 = 0;  // complex* (filterdesign)
void* lbl_8316EBA8 = 0;  // complex* (filterdesign)
void* lbl_83172BB0 = 0;  // complex* (filterdesign)
#ifndef __EMSCRIPTEN__
int lh_table_lookup() { return 0; }
int NuiAudioCreate() { return 0; }
int NuiAudioRegisterCallbacks() { return 0; }
int NuiAudioRelease() { return 0; }
int NuiAudioUnregisterCallbacks() { return 0; }
int NuiCameraAdjustTilt() { return 0; }
int NuiCameraElevationGetAngle() { return 0; }
int NuiCameraElevationSetAngle() { return 0; }
int NuiCameraGetExposureRegionOfInterest() { return 0; }
int NuiCameraGetProperty() { return 0; }
int NuiCameraGetPropertyF() { return 0; }
int NuiFitnessGetCurrentFitnessData() { return 0; }
int NuiFitnessPauseTracking() { return 0; }
int NuiFitnessResumeTracking() { return 0; }
int NuiFitnessStartTracking() { return 0; }
int NuiFitnessStopTracking() { return 0; }
int NuiIdentityAbort() { return 0; }
int NuiIdentityGetEnrollmentInformation() { return 0; }
int NuiIdentityIdentify() { return 0; }
int NuiImageStreamGetNextFrame() { return 0; }
int NuiImageStreamOpen() { return 0; }
int NuiImageStreamReleaseFrame() { return 0; }
int NuiInitialize() { return 0; }
int NuiShutdown() { return 0; }
int NuiSkeletonTrackingDisable() { return 0; }
int NuiSkeletonTrackingEnable() { return 0; }
int NuiSpeechAddWordTransition() { return 0; }
int NuiSpeechCommitGrammar() { return 0; }
int NuiSpeechCreateGrammar() { return 0; }
int NuiSpeechCreateRule() { return 0; }
int NuiSpeechCreateState() { return 0; }
int NuiSpeechDestroyEvent() { return 0; }
int NuiSpeechDisable() { return 0; }
int NuiSpeechEmulateRecognition() { return 0; }
int NuiSpeechEnable() { return 0; }
int NuiSpeechGetEvents() { return 0; }
int NuiSpeechLoadGrammar() { return 0; }
int NuiSpeechSetEventInterest() { return 0; }
int NuiSpeechSetGrammarState() { return 0; }
int NuiSpeechSetRuleState() { return 0; }
int NuiSpeechStartRecognition() { return 0; }
int NuiSpeechStopRecognition() { return 0; }
int NuiSpeechUnloadGrammar() { return 0; }
int NuiWaveGetGestureOwnerProgress() { return 0; }
int NuiWaveSetEnabled() { return 0; }
int OutputDebugStringA() { return 0; }
int printbuf_free() { return 0; }
int printbuf_memappend() { return 0; }
int printbuf_new() { return 0; }
#endif // !__EMSCRIPTEN__
// Floating-point constants referenced by symbol name (loaded via address)
double __real_0000000000000000 = 0.0;
double __real_3f50624dd2f1a9fc = 0.001;
double __real_3fe0000000000000 = 0.5;
double __real_4000000000000000 = 2.0;
double __real_400921fb60000000 = 3.14159274101257324;   // pi (float precision)
double __real_401921fb60000000 = 6.28318548202514648;   // 2*pi (float precision)
#ifndef __EMSCRIPTEN__
int register_cipher() { return 0; }
#endif
struct _cipher_descriptor { char dummy[256]; };
extern "C" const _cipher_descriptor rijndael_desc = {};
#ifndef __EMSCRIPTEN__
int rijndael_ecb_decrypt() { return 0; }
int rijndael_setup() { return 0; }
int SetUnhandledExceptionFilter() { return 0; }
#endif
// The* global pointer stubs - must be void* (not functions!) since C++ code
// declares them as extern ClassName* and dereferences them as pointers.
// Function stubs at these symbols would be read as non-null garbage pointers.
void* TheChallengeSortMgr = 0;
// TheContentMgr: provided by ContentMgr_Stub.cpp
void* TheDebugNotifyOncePrinter = 0;
// TheDxRnd: removed - provided by Rnd_Stub.cpp (not needed, rnddx9 excluded)
void* TheFitnessGoalMgr = 0;
// TheGameMode: now defined properly in GameMode.cpp
// TheHamUI: now defined in HamUI.cpp
void* TheHAQMgr = 0;
void* TheLeaderboards = 0;
// TheLocale: now defined in Locale.cpp
void* TheMaster = 0;
void* TheMC = 0;
// TheMoveMgr: now defined in MoveMgr.cpp
// TheMovieSys: removed - provided by MovieSys.cpp
void* TheMQSongSortMgr = 0;
// TheNgRnd: removed - provided by Rnd_Stub.cpp
void* TheRenderState = 0;
// TheRnd: removed - provided by Rnd_Stub.cpp
void* TheServer = 0;
// TheShaderMgr: removed - provided by Rnd_Stub.cpp
void* TheSkeletonIdentifier = 0;
void* TheSkeletonViz = 0;
void* TheSongSortMgr = 0;
// TheUI: removed - provided as proper UIManager* in Rnd_Stub.cpp
#ifndef __EMSCRIPTEN__
int vorbis_synthesis_poll() { return 0; }
int WideCharToMultiByte() { return 0; }
int XBackgroundDownloadSetMode() { return 0; }
int XInputGetCapabilities() { return 0; }
int XNetConnect() { return 0; }
int XNetGetConnectStatus() { return 0; }
int XNetGetTitleXnAddr() { return 0; }
int XNetRandom() { return 0; }
int XNetUnregisterInAddr() { return 0; }
int XNetXnAddrToMachineId() { return 0; }
int XShowMarketplaceDownloadItemsUI() { return 0; }
int XShowNuiTroubleshooterUI() { return 0; }
int XTitleServerCreateEnumerator() { return 0; }
#endif
} // extern "C"

// asm-label stubs: provide mangled C++ symbols via __asm__ name redirection.
// Variable stubs work fine on all platforms (just zero-initialized memory).
// Function stubs only work with ELF linkers — wasm-ld generates wrong
// signatures that insert 'unreachable' traps at call sites.

// C++ global variables
__attribute__((weak, used)) char _stub_var_0[256] __asm__("_ZN10FileMerger11sDisableAllE") = {};
__attribute__((weak, used)) char _stub_var_1[256] __asm__("_ZN10HamNavList17sSlideTrendAmountE") = {};
__attribute__((weak, used)) char _stub_var_2[256] __asm__("_ZN10HamNavList18sSlideSmoothAmountE") = {};
__attribute__((weak, used)) char _stub_var_3[256] __asm__("_ZN10HamNavList27sLastSelectInControllerModeE") = {};
__attribute__((weak, used)) char _stub_var_4[256] __asm__("_ZN10InlineHelp13sRotationTimeE") = {};
__attribute__((weak, used)) char _stub_var_5[256] __asm__("_ZN10InlineHelp16sLastUpdatedTimeE") = {};
__attribute__((weak, used)) char _stub_var_6[256] __asm__("_ZN10InlineHelp16sNeedsTextUpdateE") = {};
__attribute__((weak, used)) char _stub_var_7[256] __asm__("_ZN10InlineHelp27sHasFlippedTextThisRotationE") = {};
__attribute__((weak, used)) char _stub_var_8[256] __asm__("_ZN10InlineHelp8sRotatedE") = {};
__attribute__((weak, used)) char _stub_var_9[256] __asm__("_ZN10InlineHelp9sLabelRotE") = {};
__attribute__((weak, used)) char _stub_var_10[256] __asm__("_ZN11FixedString4nposE") = {};
__attribute__((weak, used)) char _stub_var_11[256] __asm__("_ZN11GlitchPoker11smThresholdE") = {};
__attribute__((weak, used)) char _stub_var_12[256] __asm__("_ZN11GlitchPoker12smDumpLeavesE") = {};
__attribute__((weak, used)) char _stub_var_13[256] __asm__("_ZN11GlitchPoker14smLastDumpTimeE") = {};
__attribute__((weak, used)) char _stub_var_14[256] __asm__("_ZN11GlitchPoker15smTotalLeafTimeE") = {};
__attribute__((weak, used)) char _stub_var_15[256] __asm__("_ZN11GlitchPoker18smNestedStartTimesE") = {};
__attribute__((weak, used)) char _stub_var_16[256] __asm__("_ZN11HamSongData9sInstanceE") = {};
__attribute__((weak, used)) char _stub_var_17[256] __asm__("_ZN11PlatformMgr14sXShowCallbackE") = {};
__attribute__((weak, used)) char _stub_var_18[256] __asm__("_ZN11RndDrawable15sHighlightStyleE") = {};
__attribute__((weak, used)) char _stub_var_19[256] __asm__("_ZN11RndDrawable20sNormalDisplayLengthE") = {};
__attribute__((weak, used)) char _stub_var_20[256] __asm__("_ZN11RndDrawable22sForceSubpartSelectionE") = {};
__attribute__((weak, used)) char _stub_var_21[256] __asm__("_ZN11RndPostProc12sDOFOverrideE") = {};
__attribute__((weak, used)) char _stub_var_22[256] __asm__("_ZN11RndPostProc15sBloomLocFactorE") = {};
__attribute__((weak, used)) char _stub_var_23[256] __asm__("_ZN11RndPostProc8sCurrentE") = {};
__attribute__((weak, used)) char _stub_var_24[256] __asm__("_ZN12HamCharacter14sSkeletonClipsE") = {};
__attribute__((weak, used)) char _stub_var_25[256] __asm__("_ZN12HamCharacter7sLoadVOE") = {};
__attribute__((weak, used)) char _stub_var_26[256] __asm__("_ZN12HelpBarPanel9sInstanceE") = {};
__attribute__((weak, used)) char _stub_var_27[256] __asm__("_ZN12LoadingPanel14sLoadingMasterE") = {};
__attribute__((weak, used)) char _stub_var_28[256] __asm__("_ZN12LoadingPanel7sSongDBE") = {};
__attribute__((weak, used)) char _stub_var_29[256] __asm__("_ZN12PreloadPanel6sCacheE") = {};
__attribute__((weak, used)) char _stub_var_30[256] __asm__("_ZN13FilterVersion13sNumHam2NodesE") = {};
__attribute__((weak, used)) char _stub_var_31[256] __asm__("_ZN13MetaPerformer12sCheatFinaleE") = {};
__attribute__((weak, used)) char _stub_var_32[256] __asm__("_ZN13PhysicsVolume8sShowingE") = {};
__attribute__((weak, used)) char _stub_var_33[256] __asm__("_ZN13SongCollision19sCollisionToleranceE") = {};
__attribute__((weak, used)) char _stub_var_34[256] __asm__("_ZN13SongStatusMgr29sFakeLeaderboardUploadFailureE") = {};
__attribute__((weak, used)) char _stub_var_35[256] __asm__("_ZN13UIListSubList18sNextFillSelectionE") = {};
__attribute__((weak, used)) char _stub_var_36[256] __asm__("_ZN14LetterboxPanel9sInstanceE") = {};
__attribute__((weak, used)) char _stub_var_37[256] __asm__("_ZN14SkeletonUpdate17sNewSkeletonEventE") = {};
__attribute__((weak, used)) char _stub_var_38[256] __asm__("_ZN14SkeletonUpdate21sSkeletonUpdatedEventE") = {};
__attribute__((weak, used)) char _stub_var_39[256] __asm__("_ZN14SkeletonUpdate9sInstanceE") = {};
__attribute__((weak, used)) char _stub_var_40[256] __asm__("_ZN14StandardStream12kStreamEndMsE") = {};
__attribute__((weak, used)) char _stub_var_41[256] __asm__("_ZN15BlacklightPanel9sInstanceE") = {};
__attribute__((weak, used)) char _stub_var_42[256] __asm__("_ZN15SpotlightDrawer12sShadowSpotsE") = {};
__attribute__((weak, used)) char _stub_var_43[256] __asm__("_ZN15SpotlightDrawer5sCansE") = {};
__attribute__((weak, used)) char _stub_var_44[256] __asm__("_ZN15SpotlightDrawer7sLightsE") = {};
__attribute__((weak, used)) char _stub_var_45[256] __asm__("_ZN15SpotlightDrawer8sCurrentE") = {};
__attribute__((weak, used)) char _stub_var_46[256] __asm__("_ZN15SpotlightDrawer9sNeedDrawE") = {};
__attribute__((weak, used)) char _stub_var_47[256] __asm__("_ZN16RndTransformable12sShadowPlaneE") = {};
__attribute__((weak, used)) char _stub_var_48[256] __asm__("_ZN17HamScrollBehavior12mScrollUpCapE") = {};
__attribute__((weak, used)) char _stub_var_49[256] __asm__("_ZN17HamScrollBehavior14mScrollDownCapE") = {};
__attribute__((weak, used)) char _stub_var_50[256] __asm__("_ZN17HamScrollBehavior16mFastUpTickDelayE") = {};
__attribute__((weak, used)) char _stub_var_51[256] __asm__("_ZN17HamScrollBehavior16mSlowScrollSpeedE") = {};
__attribute__((weak, used)) char _stub_var_52[256] __asm__("_ZN17HamScrollBehavior16mSlowUpTickDelayE") = {};
__attribute__((weak, used)) char _stub_var_53[256] __asm__("_ZN17HamScrollBehavior18mFastDownTickDelayE") = {};
__attribute__((weak, used)) char _stub_var_54[256] __asm__("_ZN17HamScrollBehavior18mNormalScrollSpeedE") = {};
__attribute__((weak, used)) char _stub_var_55[256] __asm__("_ZN17HamScrollBehavior18mSlowDownTickDelayE") = {};
__attribute__((weak, used)) char _stub_var_56[256] __asm__("_ZN17HamScrollBehavior18mSlowFastThresholdE") = {};
__attribute__((weak, used)) char _stub_var_57[256] __asm__("_ZN17HamScrollBehavior20mFastScrollSpeedBaseE") = {};
__attribute__((weak, used)) char _stub_var_58[256] __asm__("_ZN17HamScrollBehavior21mNeutralToSlowUpDelayE") = {};
__attribute__((weak, used)) char _stub_var_59[256] __asm__("_ZN17HamScrollBehavior21mSlowUpFirstTickDelayE") = {};
__attribute__((weak, used)) char _stub_var_60[256] __asm__("_ZN17HamScrollBehavior22mFastScrollSpeedScalarE") = {};
__attribute__((weak, used)) char _stub_var_61[256] __asm__("_ZN17HamScrollBehavior23mNeutralToSlowDownDelayE") = {};
__attribute__((weak, used)) char _stub_var_62[256] __asm__("_ZN17HamScrollBehavior23mSlowDownFirstTickDelayE") = {};
__attribute__((weak, used)) char _stub_var_63[256] __asm__("_ZN17NgSpotlightDrawer16sSharedResourcesE") = {};
__attribute__((weak, used)) char _stub_var_64[256] __asm__("_ZN21FreestyleMoveRecorder9sInstanceE") = {};
__attribute__((weak, used)) char _stub_var_65[256] __asm__("_ZN3Hmx6Object9sDeletingE") = {};
__attribute__((weak, used)) char _stub_var_66[256] __asm__("_ZN3Rnd19sPostProcPanelCountE") = {};
__attribute__((weak, used)) char _stub_var_67[256] __asm__("_ZN6Locale14sVerboseNotifyE") = {};
__attribute__((weak, used)) char _stub_var_68[256] __asm__("_ZN6RndCam8sCurrentE") = {};
__attribute__((weak, used)) char _stub_var_69[256] __asm__("_ZN6RndMat14sMetaMaterialsE") = {};
__attribute__((weak, used)) char _stub_var_70[256] __asm__("_ZN6Stream12kStreamEndMsE") = {};
__attribute__((weak, used)) char _stub_var_71[256] __asm__("_ZN6WavMgr5sFreeE") = {};
__attribute__((weak, used)) char _stub_var_72[256] __asm__("_ZN6WavMgr6sAllocE") = {};
__attribute__((weak, used)) char _stub_var_73[256] __asm__("_ZN7CamShot11sAnimTargetE") = {};
__attribute__((weak, used)) char _stub_var_74[256] __asm__("_ZN7LoadMgr17sFileOpenCallbackE") = {};
__attribute__((weak, used)) char _stub_var_75[256] __asm__("_ZN7MoveDir11sGameRecordE") = {};
__attribute__((weak, used)) char _stub_var_76[256] __asm__("_ZN7MoveDir15sLatencySecondsE") = {};
__attribute__((weak, used)) char _stub_var_77[256] __asm__("_ZN7MoveDir16sPLFMinTimeErrorE") = {};
__attribute__((weak, used)) char _stub_var_78[256] __asm__("_ZN7MoveDir18sGameRecord2PlayerE") = {};
__attribute__((weak, used)) char _stub_var_79[256] __asm__("_ZN7RndText22sBlacklightModeEnabledE") = {};
__attribute__((weak, used)) char _stub_var_80[256] __asm__("_ZN7UILabel15sDebugHighlightE") = {};
__attribute__((weak, used)) char _stub_var_81[256] __asm__("_ZN7UILabel19sRequireFixedLengthE") = {};
__attribute__((weak, used)) char _stub_var_82[256] __asm__("_ZN7UIPanel11sMaxPanelIdE") = {};
__attribute__((weak, used)) char _stub_var_83[256] __asm__("_ZN7UIPanel16sIsFinalDrawPassE") = {};
__attribute__((weak, used)) char _stub_var_84[256] __asm__("_ZN8CharEyes15sDisableEyeDartE") = {};
__attribute__((weak, used)) char _stub_var_85[256] __asm__("_ZN8CharEyes17sDisableEyeJitterE") = {};
__attribute__((weak, used)) char _stub_var_86[256] __asm__("_ZN8CharEyes19sDisableEyeClampingE") = {};
__attribute__((weak, used)) char _stub_var_87[256] __asm__("_ZN8CharEyes23sDisableInterestObjectsE") = {};
__attribute__((weak, used)) char _stub_var_88[256] __asm__("_ZN8CharEyes23sDisableProceduralBlinkE") = {};
__attribute__((weak, used)) char _stub_var_89[256] __asm__("_ZN8PanelDir16sAlwaysNeedFocusE") = {};
__attribute__((weak, used)) char _stub_var_90[256] __asm__("_ZN8UIScreen12sMaxScreenIdE") = {};
__attribute__((weak, used)) char _stub_var_91[256] __asm__("_ZN8Waypoint10sWaypointsB5cxx11E") = {};
__attribute__((weak, used)) char _stub_var_92[256] __asm__("_ZN8WorldDir8sGlowMatE") = {};
__attribute__((weak, used)) char _stub_var_93[256] __asm__("_ZN9DirLoader9sPathEvalE") = {};
__attribute__((weak, used)) char _stub_var_94[256] __asm__("_ZN9FileCache15sWavCacheHelperE") = {};
__attribute__((weak, used)) char _stub_var_95[256] __asm__("_ZN9FileCache20sResourceCacheHelperE") = {};
__attribute__((weak, used)) char _stub_var_96[256] __asm__("_ZN9MetaPanel10sHamMasterE") = {};
__attribute__((weak, used)) char _stub_var_97[256] __asm__("_ZN9MetaPanel10sMotdCheatE") = {};
__attribute__((weak, used)) char _stub_var_98[256] __asm__("_ZN9MetaPanel10sUnlockAllE") = {};
__attribute__((weak, used)) char _stub_var_99[256] __asm__("_ZN9MetaPanel7sSongDBE") = {};
__attribute__((weak, used)) char _stub_var_100[256] __asm__("_ZN9RndShader13sCurrentUseAOE") = {};
__attribute__((weak, used)) char _stub_var_101[256] __asm__("_ZN9RndShader13sMatShadersOKE") = {};
__attribute__((weak, used)) char _stub_var_102[256] __asm__("_ZN9RndShader14mModalCallbackE") = {};
__attribute__((weak, used)) char _stub_var_103[256] __asm__("_ZN9RndShader14sCurrentShaderE") = {};
__attribute__((weak, used)) char _stub_var_104[256] __asm__("_ZN9RndShader15sCurrentSkinnedE") = {};
__attribute__((weak, used)) char _stub_var_105[256] __asm__("_ZN9RndShader8sShadersE") = {};
__attribute__((weak, used)) char _stub_var_106[256] __asm__("_ZN9RndSpline20sGlobalDefaultSplineE") = {};
__attribute__((weak, used)) char _stub_var_107[256] __asm__("_ZN9Spotlight9sDiskMeshE") = {};
__attribute__((weak, used)) char _stub_var_108[256] __asm__("_ZTT10RndMatAnim") = {};
__attribute__((weak, used)) char _stub_var_109[256] __asm__("_ZTT11LocalePanel") = {};
__attribute__((weak, used)) char _stub_var_110[256] __asm__("_ZTT11RndMeshAnim") = {};
__attribute__((weak, used)) char _stub_var_111[256] __asm__("_ZTT15StubCameraInput") = {};
__attribute__((weak, used)) char _stub_var_112[256] __asm__("_ZTT17CharSignalApplier") = {};
__attribute__((weak, used)) char _stub_var_113[256] __asm__("_ZTT8AppLabel") = {};

// C++ function stubs — skip on Emscripten (asm-label functions cause
// signature mismatches that insert 'unreachable' traps in wasm-ld)
#ifndef __EMSCRIPTEN__
// AttachMesh(RndMesh*, RndMesh*)
extern "C" __attribute__((weak, used)) long _stub_fn_0() __asm__("_Z10AttachMeshP7RndMeshS0_");
extern "C" long _stub_fn_0() { return 0; }
// BinStream& CachedRead<unsigned char, std::allocator<unsigned char> >(BinStream&, std::vector<unsigned char, std::allocator<unsigned char> >&)
extern "C" __attribute__((weak, used)) long _stub_fn_1() __asm__("_Z10CachedReadIhSaIhEER9BinStreamS2_RSt6vectorIT_T0_E");
extern "C" long _stub_fn_1() { return 0; }
// BinStream& CachedRead<RndMesh::Face, std::allocator<RndMesh::Face> >(BinStream&, std::vector<RndMesh::Face, std::allocator<RndMesh::Face> >&)
extern "C" __attribute__((weak, used)) long _stub_fn_2() __asm__("_Z10CachedReadIN7RndMesh4FaceESaIS1_EER9BinStreamS4_RSt6vectorIT_T0_E");
extern "C" long _stub_fn_2() { return 0; }
// CalcSpline(float, float*)
extern "C" __attribute__((weak, used)) long _stub_fn_3() __asm__("_Z10CalcSplinefPf");
extern "C" long _stub_fn_3() { return 0; }
// ParseArray()
extern "C" __attribute__((weak, used)) long _stub_fn_4() __asm__("_Z10ParseArrayv");
extern "C" long _stub_fn_4() { return 0; }
// RandomXfms(RndMultiMesh*)
extern "C" __attribute__((weak, used)) long _stub_fn_5() __asm__("_Z10RandomXfmsP12RndMultiMesh");
extern "C" long _stub_fn_5() { return 0; }
// ScaleAddEq(Hmx::Quat&, Hmx::Quat const&, float)
extern "C" __attribute__((weak, used)) long _stub_fn_6() __asm__("_Z10ScaleAddEqRN3Hmx4QuatERKS0_f");
extern "C" long _stub_fn_6() { return 0; }
// CloseHandle(int)
extern "C" __attribute__((weak, used)) long _stub_fn_7() __asm__("_Z11CloseHandlei");
extern "C" long _stub_fn_7() { return 0; }
// DspAllocate(float*&, int, IXAudioBatchAllocator*)
extern "C" __attribute__((weak, used)) long _stub_fn_8() __asm__("_Z11DspAllocateRPfiP21IXAudioBatchAllocator");
extern "C" long _stub_fn_8() { return 0; }
// LoadSubPart(BinStreamRev&, CamShot*)
extern "C" __attribute__((weak, used)) long _stub_fn_9() __asm__("_Z11LoadSubPartR12BinStreamRevP7CamShot");
extern "C" long _stub_fn_9() { return 0; }
// MemFindHeap(char const*)
extern "C" __attribute__((weak, used)) long _stub_fn_10() __asm__("_Z11MemFindHeapPKc");
extern "C" long _stub_fn_10() { return 0; }
// OnDumpMoves(DataArray*)
extern "C" __attribute__((weak, used)) long _stub_fn_11() __asm__("_Z11OnDumpMovesP9DataArray");
extern "C" long _stub_fn_11() { return 0; }
// BinkFileIdle(BINKIO*)
extern "C" __attribute__((weak, used)) long _stub_fn_12() __asm__("_Z12BinkFileIdleP6BINKIO");
extern "C" long _stub_fn_12() { return 0; }
// BuildFromBSP(RndMesh*)
extern "C" __attribute__((weak, used)) long _stub_fn_13() __asm__("_Z12BuildFromBSPP7RndMesh");
extern "C" long _stub_fn_13() { return 0; }
// ComputeAngle(Vector3 const&, Vector3 const&, Vector3 const&)
extern "C" __attribute__((weak, used)) long _stub_fn_14() __asm__("_Z12ComputeAngleRK7Vector3S1_S1_");
extern "C" long _stub_fn_14() { return 0; }
// EstimateDraw(int)
extern "C" __attribute__((weak, used)) long _stub_fn_15() __asm__("_Z12EstimateDrawi");
extern "C" long _stub_fn_15() { return 0; }
// FixVertOrder(RndMesh const*, RndMesh*)
extern "C" __attribute__((weak, used)) long _stub_fn_16() __asm__("_Z12FixVertOrderPK7RndMeshPS_");
extern "C" long _stub_fn_16() { return 0; }
// MakeFileList(char const*, bool, bool (*)(char*))
extern "C" __attribute__((weak, used)) long _stub_fn_17() __asm__("_Z12MakeFileListPKcbPFbPcE");
extern "C" long _stub_fn_17() { return 0; }
// ResetNormals(RndMesh*)
extern "C" __attribute__((weak, used)) long _stub_fn_18() __asm__("_Z12ResetNormalsP7RndMesh");
extern "C" long _stub_fn_18() { return 0; }
// ScoreUtlInit(DataArray const*)
extern "C" __attribute__((weak, used)) long _stub_fn_19() __asm__("_Z12ScoreUtlInitPK9DataArray");
extern "C" long _stub_fn_19() { return 0; }
// SetupHXDrums(int, _XINPUT_CAPABILITIES const&)
extern "C" __attribute__((weak, used)) long _stub_fn_20() __asm__("_Z12SetupHXDrumsiRK20_XINPUT_CAPABILITIES");
extern "C" long _stub_fn_20() { return 0; }
// BinkNextFrame(BINK*)
extern "C" __attribute__((weak, used)) long _stub_fn_21() __asm__("_Z13BinkNextFrameP4BINK");
extern "C" long _stub_fn_21() { return 0; }
// BinkOpenTrack(BINK*, unsigned char)
extern "C" __attribute__((weak, used)) long _stub_fn_22() __asm__("_Z13BinkOpenTrackP4BINKh");
extern "C" long _stub_fn_22() { return 0; }
// BinkSetMemory(void* (*)(int), void (*)(void*))
extern "C" __attribute__((weak, used)) long _stub_fn_23() __asm__("_Z13BinkSetMemoryPFPviEPFvS_E");
extern "C" long _stub_fn_23() { return 0; }
// CacheResource(char const*, Hmx::Object const*)
extern "C" __attribute__((weak, used)) long _stub_fn_24() __asm__("_Z13CacheResourcePKcPKN3Hmx6ObjectE");
extern "C" long _stub_fn_24() { return 0; }
// DiffTblReport(char const*, BlockStatTable&, BlockStatTable&, TextStream&)
extern "C" __attribute__((weak, used)) long _stub_fn_25() __asm__("_Z13DiffTblReportPKcR14BlockStatTableS2_R10TextStream");
extern "C" long _stub_fn_25() { return 0; }
// DrawBufferMat(RndMat*, Hmx::Rect&)
extern "C" __attribute__((weak, used)) long _stub_fn_26() __asm__("_Z13DrawBufferMatP6RndMatRN3Hmx4RectE");
extern "C" long _stub_fn_26() { return 0; }
// GetScoreBonus(float, std::vector<float, std::allocator<float> > const*)
extern "C" __attribute__((weak, used)) long _stub_fn_27() __asm__("_Z13GetScoreBonusfPKSt6vectorIfSaIfEE");
extern "C" long _stub_fn_27() { return 0; }
// InitKeyCheats(DataArray const*)
extern "C" __attribute__((weak, used)) long _stub_fn_28() __asm__("_Z13InitKeyCheatsPK9DataArray");
extern "C" long _stub_fn_28() { return 0; }
// SetupHXGuitar(int, _XINPUT_CAPABILITIES const&)
extern "C" __attribute__((weak, used)) long _stub_fn_29() __asm__("_Z13SetupHXGuitariRK20_XINPUT_CAPABILITIES");
extern "C" long _stub_fn_29() { return 0; }
// SetupHXKeytar(int, _XINPUT_CAPABILITIES const&)
extern "C" __attribute__((weak, used)) long _stub_fn_30() __asm__("_Z13SetupHXKeytariRK20_XINPUT_CAPABILITIES");
extern "C" long _stub_fn_30() { return 0; }
// SpewTerminate()
extern "C" __attribute__((weak, used)) long _stub_fn_31() __asm__("_Z13SpewTerminatev");
extern "C" long _stub_fn_31() { return 0; }
// SystemPreInit(int, char**, char const*) -- provided by System_Native.cpp
// XNetDnsLookup(int, int, void*)
extern "C" __attribute__((weak, used)) long _stub_fn_33() __asm__("_Z13XNetDnsLookupiiPv");
extern "C" long _stub_fn_33() { return 0; }
// XZErrorWeight(Vector3 const&, float&, float&)
extern "C" __attribute__((weak, used)) long _stub_fn_34() __asm__("_Z13XZErrorWeightRK7Vector3RfS2_");
extern "C" long _stub_fn_34() { return 0; }
// BinkCloseTrack(BINKTRACK*)
extern "C" __attribute__((weak, used)) long _stub_fn_35() __asm__("_Z14BinkCloseTrackP9BINKTRACK");
extern "C" long _stub_fn_35() { return 0; }
// CompressThread(void*)
extern "C" __attribute__((weak, used)) long _stub_fn_36() __asm__("_Z14CompressThreadPv");
extern "C" long _stub_fn_36() { return 0; }
// DistributeXfms(RndMultiMesh*, int, float)
extern "C" __attribute__((weak, used)) long _stub_fn_37() __asm__("_Z14DistributeXfmsP12RndMultiMeshif");
extern "C" long _stub_fn_37() { return 0; }
// DrawGestureMgr(GestureMgr&, LiveCameraInput::BufferType, float)
extern "C" __attribute__((weak, used)) long _stub_fn_38() __asm__("_Z14DrawGestureMgrR10GestureMgrN15LiveCameraInput10BufferTypeEf");
extern "C" long _stub_fn_38() { return 0; }
// jpeg_std_error(jpeg_error_mgr*)
extern "C" __attribute__((weak, used)) long _stub_fn_39() __asm__("_Z14jpeg_std_errorP14jpeg_error_mgr");
extern "C" long _stub_fn_39() { return 0; }
// ListProperties(std::__cxx11::list<Symbol, std::allocator<Symbol> >&, Symbol, Symbol, std::__cxx11::list<Symbol, std::allocator<Symbol> >*, bool)
extern "C" __attribute__((weak, used)) long _stub_fn_40() __asm__("_Z14ListPropertiesRNSt7__cxx114listI6SymbolSaIS1_EEES1_S1_PS3_b");
extern "C" long _stub_fn_40() { return 0; }
// RndScaleObject(Hmx::Object*, float, float)
extern "C" __attribute__((weak, used)) long _stub_fn_41() __asm__("_Z14RndScaleObjectPN3Hmx6ObjectEff");
extern "C" long _stub_fn_41() { return 0; }
// TessellateMesh(RndMesh*)
extern "C" __attribute__((weak, used)) long _stub_fn_42() __asm__("_Z14TessellateMeshP7RndMesh");
extern "C" long _stub_fn_42() { return 0; }
// ThreadMemStack(bool)
extern "C" __attribute__((weak, used)) long _stub_fn_43() __asm__("_Z14ThreadMemStackb");
extern "C" long _stub_fn_43() { return 0; }
// UtilDrawSphere(Vector3 const&, float, Hmx::Color const&, RndMat*)
extern "C" __attribute__((weak, used)) long _stub_fn_44() __asm__("_Z14UtilDrawSphereRK7Vector3fRKN3Hmx5ColorEP6RndMat");
extern "C" long _stub_fn_44() { return 0; }
// WSACreateEvent()
extern "C" __attribute__((weak, used)) long _stub_fn_45() __asm__("_Z14WSACreateEventv");
extern "C" long _stub_fn_45() { return 0; }
// XNetDnsRelease(void*)
extern "C" __attribute__((weak, used)) long _stub_fn_46() __asm__("_Z14XNetDnsReleasePv");
extern "C" long _stub_fn_46() { return 0; }
// merged_82610090(char const*, int volatile*)
extern "C" __attribute__((weak, used)) long _stub_fn_47() __asm__("_Z15merged_82610090PKcPVi");
extern "C" long _stub_fn_47() { return 0; }
// OnCycleAutoplay(DataArray*)
extern "C" __attribute__((weak, used)) long _stub_fn_48() __asm__("_Z15OnCycleAutoplayP9DataArray");
extern "C" long _stub_fn_48() { return 0; }
// SliderChildSort(FlowNode*, FlowNode*)
extern "C" __attribute__((weak, used)) long _stub_fn_50() __asm__("_Z15SliderChildSortP8FlowNodeS0_");
extern "C" long _stub_fn_50() { return 0; }
// TestTextureSize(ObjectDir*, int, int, int, int, int)
extern "C" __attribute__((weak, used)) long _stub_fn_51() __asm__("_Z15TestTextureSizeP9ObjectDiriiiii");
extern "C" long _stub_fn_51() { return 0; }
// BinkGetTrackData(BINKTRACK*, void*)
extern "C" __attribute__((weak, used)) long _stub_fn_52() __asm__("_Z16BinkGetTrackDataP9BINKTRACKPv");
extern "C" long _stub_fn_52() { return 0; }
// HmxGlobalHandler(_EXCEPTION_POINTERS*)
extern "C" __attribute__((weak, used)) long _stub_fn_53() __asm__("_Z16HmxGlobalHandlerP19_EXCEPTION_POINTERS");
extern "C" long _stub_fn_53() { return 0; }
// MakeTangentsLate(RndMesh*)
extern "C" __attribute__((weak, used)) long _stub_fn_54() __asm__("_Z16MakeTangentsLateP7RndMesh");
extern "C" long _stub_fn_54() { return 0; }
// MemPrintOverview(int, char*)
extern "C" __attribute__((weak, used)) long _stub_fn_55() __asm__("_Z16MemPrintOverviewiPc");
extern "C" long _stub_fn_55() { return 0; }
// OnTestDrawGroups(DataArray*)
extern "C" __attribute__((weak, used)) long _stub_fn_56() __asm__("_Z16OnTestDrawGroupsP9DataArray");
extern "C" long _stub_fn_56() { return 0; }
// ScaleDistToError(ScaleOp const&, float)
extern "C" __attribute__((weak, used)) long _stub_fn_57() __asm__("_Z16ScaleDistToErrorRK7ScaleOpf");
extern "C" long _stub_fn_57() { return 0; }
// SyncReloadLocale()
extern "C" __attribute__((weak, used)) long _stub_fn_58() __asm__("_Z16SyncReloadLocalev");
extern "C" long _stub_fn_58() { return 0; }
// TestTexturePaths(ObjectDir*)
extern "C" __attribute__((weak, used)) long _stub_fn_59() __asm__("_Z16TestTexturePathsP9ObjectDir");
extern "C" long _stub_fn_59() { return 0; }
// UtilDrawCylinder(Transform const&, float, float, Hmx::Color const&, int)
extern "C" __attribute__((weak, used)) long _stub_fn_60() __asm__("_Z16UtilDrawCylinderRK9TransformffRKN3Hmx5ColorEi");
extern "C" long _stub_fn_60() { return 0; }
// ValidateThreadId(unsigned long)
extern "C" __attribute__((weak, used)) long _stub_fn_61() __asm__("_Z16ValidateThreadIdm");
extern "C" long _stub_fn_61() { return 0; }
// GetCurrentHeapNum()
extern "C" __attribute__((weak, used)) long _stub_fn_62() __asm__("_Z17GetCurrentHeapNumv");
extern "C" long _stub_fn_62() { return 0; }
// GetRenderTextures(ObjectDir*)
extern "C" __attribute__((weak, used)) long _stub_fn_63() __asm__("_Z17GetRenderTexturesP9ObjectDir");
extern "C" long _stub_fn_63() { return 0; }
// HolmesClientPrint(char const*)
extern "C" __attribute__((weak, used)) long _stub_fn_64() __asm__("_Z17HolmesClientPrintPKc");
extern "C" long _stub_fn_64() { return 0; }
// jpeg_set_defaults(jpeg_compress_struct*)
extern "C" __attribute__((weak, used)) long _stub_fn_65() __asm__("_Z17jpeg_set_defaultsP20jpeg_compress_struct");
extern "C" long _stub_fn_65() { return 0; }
// OnCycleTestDancer(DataArray*)
extern "C" __attribute__((weak, used)) long _stub_fn_66() __asm__("_Z17OnCycleTestDancerP9DataArray");
extern "C" long _stub_fn_66() { return 0; }
// SetColorWriteMask(ShaderOptions const&, RndMat*)
extern "C" __attribute__((weak, used)) long _stub_fn_67() __asm__("_Z17SetColorWriteMaskRK13ShaderOptionsP6RndMat");
extern "C" long _stub_fn_67() { return 0; }
// SetupHXRealGuitar(int, _XINPUT_CAPABILITIES const&)
extern "C" __attribute__((weak, used)) long _stub_fn_68() __asm__("_Z17SetupHXRealGuitariRK20_XINPUT_CAPABILITIES");
extern "C" long _stub_fn_68() { return 0; }
// CopyTypeProperties(Hmx::Object*, Hmx::Object*)
extern "C" __attribute__((weak, used)) long _stub_fn_69() __asm__("_Z18CopyTypePropertiesPN3Hmx6ObjectES1_");
extern "C" long _stub_fn_69() { return 0; }
// DetectFracToRating(float, std::vector<float, std::allocator<float> > const*, int*)
extern "C" __attribute__((weak, used)) long _stub_fn_70() __asm__("_Z18DetectFracToRatingfPKSt6vectorIfSaIfEEPi");
extern "C" long _stub_fn_70() { return 0; }
// HolmesSetFileShare(char const*, char const*)
extern "C" __attribute__((weak, used)) long _stub_fn_71() __asm__("_Z18HolmesSetFileSharePKcS0_");
extern "C" long _stub_fn_71() { return 0; }
// InitQuickJoyCheats(DataArray const*, CheatsManager::ShiftMode)
extern "C" __attribute__((weak, used)) long _stub_fn_72() __asm__("_Z18InitQuickJoyCheatsPK9DataArrayN13CheatsManager9ShiftModeE");
extern "C" long _stub_fn_72() { return 0; }
// OnCameraDebugDepth(DataArray*)
extern "C" __attribute__((weak, used)) long _stub_fn_73() __asm__("_Z18OnCameraDebugDepthP9DataArray");
extern "C" long _stub_fn_73() { return 0; }
// OnCameraDumpUnique(DataArray*)
extern "C" __attribute__((weak, used)) long _stub_fn_74() __asm__("_Z18OnCameraDumpUniqueP9DataArray");
extern "C" long _stub_fn_74() { return 0; }
// RatingToDetectFrac(Symbol, std::vector<float, std::allocator<float> > const*)
extern "C" __attribute__((weak, used)) long _stub_fn_75() __asm__("_Z18RatingToDetectFrac6SymbolPKSt6vectorIfSaIfEE");
extern "C" long _stub_fn_75() { return 0; }
// RatingToRatingFrac(Symbol)
extern "C" __attribute__((weak, used)) long _stub_fn_76() __asm__("_Z18RatingToRatingFrac6Symbol");
extern "C" long _stub_fn_76() { return 0; }
// EndMemTrackFileName()
extern "C" __attribute__((weak, used)) long _stub_fn_77() __asm__("_Z19EndMemTrackFileNamev");
extern "C" long _stub_fn_77() { return 0; }
// jpeg_CreateCompress(jpeg_compress_struct*, int, unsigned long)
extern "C" __attribute__((weak, used)) long _stub_fn_78() __asm__("_Z19jpeg_CreateCompressP20jpeg_compress_structim");
extern "C" long _stub_fn_78() { return 0; }
// jpeg_start_compress(jpeg_compress_struct*, unsigned char)
extern "C" __attribute__((weak, used)) long _stub_fn_79() __asm__("_Z19jpeg_start_compressP20jpeg_compress_structh");
extern "C" long _stub_fn_79() { return 0; }
// MergeObjectsRecurse(ObjectDir*, ObjectDir*, MergeFilter&, bool)
extern "C" __attribute__((weak, used)) long _stub_fn_80() __asm__("_Z19MergeObjectsRecurseP9ObjectDirS0_R11MergeFilterb");
extern "C" long _stub_fn_80() { return 0; }
// NormalizeSystemArgs()
extern "C" __attribute__((weak, used)) long _stub_fn_81() __asm__("_Z19NormalizeSystemArgsv");
extern "C" long _stub_fn_81() { return 0; }
// TerminateMakeString()
extern "C" __attribute__((weak, used)) long _stub_fn_82() __asm__("_Z19TerminateMakeStringv");
extern "C" long _stub_fn_82() { return 0; }
// WaitForSingleObject(int, int)
extern "C" __attribute__((weak, used)) long _stub_fn_83() __asm__("_Z19WaitForSingleObjectii");
extern "C" long _stub_fn_83() { return 0; }
// BinkStartAsyncThread(int, int)
extern "C" __attribute__((weak, used)) long _stub_fn_84() __asm__("_Z20BinkStartAsyncThreadii");
extern "C" long _stub_fn_84() { return 0; }
// GetNormalMapTextures(ObjectDir*)
extern "C" __attribute__((weak, used)) long _stub_fn_85() __asm__("_Z20GetNormalMapTexturesP9ObjectDir");
extern "C" long _stub_fn_85() { return 0; }
// GetRenderTexturesNoZ(ObjectDir*)
extern "C" __attribute__((weak, used)) long _stub_fn_86() __asm__("_Z20GetRenderTexturesNoZP9ObjectDir");
extern "C" long _stub_fn_86() { return 0; }
// GlitchFindScriptImpl(DataArray*, int)
extern "C" __attribute__((weak, used)) long _stub_fn_87() __asm__("_Z20GlitchFindScriptImplP9DataArrayi");
extern "C" long _stub_fn_87() { return 0; }
// HongKongExceptionMet()
extern "C" __attribute__((weak, used)) long _stub_fn_88() __asm__("_Z20HongKongExceptionMetv");
extern "C" long _stub_fn_88() { return 0; }
// jpeg_finish_compress(jpeg_compress_struct*)
extern "C" __attribute__((weak, used)) long _stub_fn_89() __asm__("_Z20jpeg_finish_compressP20jpeg_compress_struct");
extern "C" long _stub_fn_89() { return 0; }
// jpeg_write_scanlines(jpeg_compress_struct*, unsigned char**, unsigned int)
extern "C" __attribute__((weak, used)) long _stub_fn_90() __asm__("_Z20jpeg_write_scanlinesP20jpeg_compress_structPPhj");
extern "C" long _stub_fn_90() { return 0; }
// LocalizeSeparatedInt(int, Locale&)
extern "C" __attribute__((weak, used)) long _stub_fn_91() __asm__("_Z20LocalizeSeparatedIntiR6Locale");
extern "C" long _stub_fn_91() { return 0; }
// MakeFileListFullPath(char const*)
extern "C" __attribute__((weak, used)) long _stub_fn_92() __asm__("_Z20MakeFileListFullPathPKc");
extern "C" long _stub_fn_92() { return 0; }
// TestMaterialTextures(ObjectDir*)
extern "C" __attribute__((weak, used)) long _stub_fn_93() __asm__("_Z20TestMaterialTexturesP9ObjectDir");
extern "C" long _stub_fn_93() { return 0; }
// BeginMemTrackFileName(char const*)
extern "C" __attribute__((weak, used)) long _stub_fn_94() __asm__("_Z21BeginMemTrackFileNamePKc");
extern "C" long _stub_fn_94() { return 0; }
// ConvertBonesToTranses(ObjectDir*, bool)
extern "C" __attribute__((weak, used)) long _stub_fn_95() __asm__("_Z21ConvertBonesToTransesP9ObjectDirb");
extern "C" long _stub_fn_95() { return 0; }
// EndMemTrackObjectName()
extern "C" __attribute__((weak, used)) long _stub_fn_96() __asm__("_Z21EndMemTrackObjectNamev");
extern "C" long _stub_fn_96() { return 0; }
// HolmesClientTerminate()
extern "C" __attribute__((weak, used)) long _stub_fn_97() __asm__("_Z21HolmesClientTerminatev");
extern "C" long _stub_fn_97() { return 0; }
// DetectFracToRatingFrac(float, std::vector<float, std::allocator<float> > const*)
extern "C" __attribute__((weak, used)) long _stub_fn_98() __asm__("_Z22DetectFracToRatingFracfPKSt6vectorIfSaIfEE");
extern "C" long _stub_fn_98() { return 0; }
// unsigned int GatherObjectsFromGroup<RndMesh>(RndGroup*, std::vector<RndMesh*, std::allocator<RndMesh*> >&)
extern "C" __attribute__((weak, used)) long _stub_fn_99() __asm__("_Z22GatherObjectsFromGroupI7RndMeshEjP8RndGroupRSt6vectorIPT_SaIS5_EE");
extern "C" long _stub_fn_99() { return 0; }
// BeginMemTrackObjectName(char const*)
extern "C" __attribute__((weak, used)) long _stub_fn_100() __asm__("_Z23BeginMemTrackObjectNamePKc");
extern "C" long _stub_fn_100() { return 0; }
// compare_deferred_points(DeferredPoints, DeferredPoints)
extern "C" __attribute__((weak, used)) long _stub_fn_101() __asm__("_Z23compare_deferred_points14DeferredPointsS_");
extern "C" long _stub_fn_101() { return 0; }
// GetXinputSinceLastFrame(int, _XINPUT_STATE*, unsigned int*)
extern "C" __attribute__((weak, used)) long _stub_fn_102() __asm__("_Z23GetXinputSinceLastFrameiP13_XINPUT_STATEPj");
extern "C" long _stub_fn_102() { return 0; }
// OnCycleNumStubSkeletons(DataArray*)
extern "C" __attribute__((weak, used)) long _stub_fn_103() __asm__("_Z23OnCycleNumStubSkeletonsP9DataArray");
extern "C" long _stub_fn_103() { return 0; }
// OnCycleFakeShellSkeletons(DataArray*)
extern "C" __attribute__((weak, used)) long _stub_fn_105() __asm__("_Z25OnCycleFakeShellSkeletonsP9DataArray");
extern "C" long _stub_fn_105() { return 0; }
// ResetFontMapPageMeshFaces(RndMesh*, int)
extern "C" __attribute__((weak, used)) long _stub_fn_106() __asm__("_Z25ResetFontMapPageMeshFacesP7RndMeshi");
extern "C" long _stub_fn_106() { return 0; }
// OnToggleSkeletalUpdateThread(DataArray*)
extern "C" __attribute__((weak, used)) long _stub_fn_107() __asm__("_Z28OnToggleSkeletalUpdateThreadP9DataArray");
extern "C" long _stub_fn_107() { return 0; }
// OnGetFakeSkeletonSidesSwapped(DataArray*)
extern "C" __attribute__((weak, used)) long _stub_fn_108() __asm__("_Z29OnGetFakeSkeletonSidesSwappedP9DataArray");
extern "C" long _stub_fn_108() { return 0; }
// OnSetFakeSkeletonSidesSwapped(DataArray*)
extern "C" __attribute__((weak, used)) long _stub_fn_109() __asm__("_Z29OnSetFakeSkeletonSidesSwappedP9DataArray");
extern "C" long _stub_fn_109() { return 0; }
// OnCycleActiveFakeShellSkeleton(DataArray*)
extern "C" __attribute__((weak, used)) long _stub_fn_110() __asm__("_Z30OnCycleActiveFakeShellSkeletonP9DataArray");
extern "C" long _stub_fn_110() { return 0; }
// NuiTransformSkeletonToDepthImage(__vector4, float*, float*)
extern "C" __attribute__((weak, used)) long _stub_fn_111() __asm__("_Z32NuiTransformSkeletonToDepthImage9__vector4PfS0_");
extern "C" long _stub_fn_111() { return 0; }
// NuiTransformSkeletonToDepthImage(__vector4, long*, long*, unsigned short*)
extern "C" __attribute__((weak, used)) long _stub_fn_112() __asm__("_Z32NuiTransformSkeletonToDepthImage9__vector4PlS0_Pt");
extern "C" long _stub_fn_112() { return 0; }
// SkeletonUpdateCallbackSlowdownCB(float, void*)
extern "C" __attribute__((weak, used)) long _stub_fn_113() __asm__("_Z32SkeletonUpdateCallbackSlowdownCBfPv");
extern "C" long _stub_fn_113() { return 0; }
// Clip(Hmx::Polygon const&, Hmx::Ray const&, Hmx::Polygon&)
extern "C" __attribute__((weak, used)) long _stub_fn_114() __asm__("_Z4ClipRKN3Hmx7PolygonERKNS_3RayERS0_");
extern "C" long _stub_fn_114() { return 0; }
// altCfg(DataNode, DataNode)
extern "C" __attribute__((weak, used)) long _stub_fn_115() __asm__("_Z6altCfg8DataNodeS_");
extern "C" long _stub_fn_115() { return 0; }
// Invert(Hmx::Matrix4 const&, Hmx::Matrix4&)
extern "C" __attribute__((weak, used)) long _stub_fn_116() __asm__("_Z6InvertRKN3Hmx7Matrix4ERS0_");
extern "C" long _stub_fn_116() { return 0; }
// BurnXfm(RndMesh*, bool)
extern "C" __attribute__((weak, used)) long _stub_fn_117() __asm__("_Z7BurnXfmP7RndMeshb");
extern "C" long _stub_fn_117() { return 0; }
// CacheWav(char const*, CacheResourceResult&)
extern "C" __attribute__((weak, used)) long _stub_fn_118() __asm__("_Z8CacheWavPKcR19CacheResourceResult");
extern "C" long _stub_fn_118() { return 0; }
// Localize(Symbol, bool*, Locale&)
extern "C" __attribute__((weak, used)) long _stub_fn_119() __asm__("_Z8Localize6SymbolPbR6Locale");
extern "C" long _stub_fn_119() { return 0; }
// MemDelta(char const*, int)
extern "C" __attribute__((weak, used)) long _stub_fn_120() __asm__("_Z8MemDeltaPKci");
extern "C" long _stub_fn_120() { return 0; }
// MoveXfms(RndMultiMesh*, Vector3 const&)
extern "C" __attribute__((weak, used)) long _stub_fn_121() __asm__("_Z8MoveXfmsP12RndMultiMeshRK7Vector3");
extern "C" long _stub_fn_121() { return 0; }
// Multiply(Transform const&, Transform const&, Transform&)
extern "C" __attribute__((weak, used)) long _stub_fn_122() __asm__("_Z8MultiplyRK9TransformS1_RS_");
extern "C" long _stub_fn_122() { return 0; }
// Multiply(Hmx::Matrix3 const&, Hmx::Matrix3 const&, Hmx::Matrix3&)
extern "C" __attribute__((weak, used)) long _stub_fn_123() __asm__("_Z8MultiplyRKN3Hmx7Matrix3ES2_RS0_");
extern "C" long _stub_fn_123() { return 0; }
// bool PropSync<WorldInstance>(ObjDirPtr<WorldInstance>&, DataNode&, DataArray*, int, PropOp)
extern "C" __attribute__((weak, used)) long _stub_fn_125() __asm__("_Z8PropSyncI13WorldInstanceEbR9ObjDirPtrIT_ER8DataNodeP9DataArrayi6PropOp");
extern "C" long _stub_fn_125() { return 0; }
// PropSync(Box&, DataNode&, DataArray*, int, PropOp)
extern "C" __attribute__((weak, used)) long _stub_fn_133() __asm__("_Z8PropSyncR3BoxR8DataNodeP9DataArrayi6PropOp");
extern "C" long _stub_fn_133() { return 0; }
// PropSync(Sphere&, DataNode&, DataArray*, int, PropOp)
extern "C" __attribute__((weak, used)) long _stub_fn_134() __asm__("_Z8PropSyncR6SphereR8DataNodeP9DataArrayi6PropOp");
extern "C" long _stub_fn_134() { return 0; }
// PropSync(MsgSinks&, DataNode&, DataArray*, int, PropOp)
extern "C" __attribute__((weak, used)) long _stub_fn_135() __asm__("_Z8PropSyncR8MsgSinksR8DataNodeP9DataArrayi6PropOp");
extern "C" long _stub_fn_135() { return 0; }
// PropSync(Hmx::Rect&, DataNode&, DataArray*, int, PropOp)
extern "C" __attribute__((weak, used)) long _stub_fn_136() __asm__("_Z8PropSyncRN3Hmx4RectER8DataNodeP9DataArrayi6PropOp");
extern "C" long _stub_fn_136() { return 0; }
// PropSync(Hmx::Matrix3&, DataNode&, DataArray*, int, PropOp)
extern "C" __attribute__((weak, used)) long _stub_fn_137() __asm__("_Z8PropSyncRN3Hmx7Matrix3ER8DataNodeP9DataArrayi6PropOp");
extern "C" long _stub_fn_137() { return 0; }
// RadAlloc(int)
extern "C" __attribute__((weak, used)) long _stub_fn_138() __asm__("_Z8RadAlloci");
extern "C" long _stub_fn_138() { return 0; }
// SpewInit()
extern "C" __attribute__((weak, used)) long _stub_fn_139() __asm__("_Z8SpewInitv");
extern "C" long _stub_fn_139() { return 0; }
// BinkClose(BINK*)
extern "C" __attribute__((weak, used)) long _stub_fn_140() __asm__("_Z9BinkCloseP4BINK");
extern "C" long _stub_fn_140() { return 0; }
// Intersect(Segment const&, Triangle const&, int, float&)
extern "C" __attribute__((weak, used)) long _stub_fn_141() __asm__("_Z9IntersectRK7SegmentRK8TriangleiRf");
extern "C" long _stub_fn_141() { return 0; }
// Intersect(Transform const&, Hmx::Polygon const&, BSPNode const*)
extern "C" __attribute__((weak, used)) long _stub_fn_142() __asm__("_Z9IntersectRK9TransformRKN3Hmx7PolygonEPK7BSPNode");
extern "C" long _stub_fn_142() { return 0; }
// ScaleXfms(RndMultiMesh*, Vector3 const&)
extern "C" __attribute__((weak, used)) long _stub_fn_143() __asm__("_Z9ScaleXfmsP12RndMultiMeshRK7Vector3");
extern "C" long _stub_fn_143() { return 0; }
// BinStream& operator<< <CharLookAt>(BinStream&, ObjOwnerPtr<CharLookAt> const&)
extern "C" __attribute__((weak, used)) long _stub_fn_144() __asm__("_ZlsI10CharLookAtER9BinStreamS2_RK11ObjOwnerPtrIT_E");
extern "C" long _stub_fn_144() { return 0; }
// BinStream& operator<< <RndCamAnim>(BinStream&, ObjOwnerPtr<RndCamAnim> const&)
extern "C" __attribute__((weak, used)) long _stub_fn_146() __asm__("_ZlsI10RndCamAnimER9BinStreamS2_RK11ObjOwnerPtrIT_E");
extern "C" long _stub_fn_146() { return 0; }
// BinStream& operator<< <RndEnviron>(BinStream&, ObjOwnerPtr<RndEnviron> const&)
extern "C" __attribute__((weak, used)) long _stub_fn_147() __asm__("_ZlsI10RndEnvironER9BinStreamS2_RK11ObjOwnerPtrIT_E");
extern "C" long _stub_fn_147() { return 0; }
// BinStream& operator<< <RndMatAnim>(BinStream&, ObjOwnerPtr<RndMatAnim> const&)
extern "C" __attribute__((weak, used)) long _stub_fn_149() __asm__("_ZlsI10RndMatAnimER9BinStreamS2_RK11ObjOwnerPtrIT_E");
extern "C" long _stub_fn_149() { return 0; }
// BinStream& operator<< <RndDrawable>(BinStream&, ObjOwnerPtr<RndDrawable> const&)
extern "C" __attribute__((weak, used)) long _stub_fn_151() __asm__("_ZlsI11RndDrawableER9BinStreamS2_RK11ObjOwnerPtrIT_E");
extern "C" long _stub_fn_151() { return 0; }
// BinStream& operator<< <RndMeshAnim>(BinStream&, ObjOwnerPtr<RndMeshAnim> const&)
extern "C" __attribute__((weak, used)) long _stub_fn_154() __asm__("_ZlsI11RndMeshAnimER9BinStreamS2_RK11ObjOwnerPtrIT_E");
extern "C" long _stub_fn_154() { return 0; }
// BinStream& operator<< <CharInterest>(BinStream&, ObjOwnerPtr<CharInterest> const&)
extern "C" __attribute__((weak, used)) long _stub_fn_155() __asm__("_ZlsI12CharInterestER9BinStreamS2_RK11ObjOwnerPtrIT_E");
extern "C" long _stub_fn_155() { return 0; }
// BinStream& operator<< <EventTrigger>(BinStream&, ObjOwnerPtr<EventTrigger> const&)
extern "C" __attribute__((weak, used)) long _stub_fn_158() __asm__("_ZlsI12EventTriggerER9BinStreamS2_RK11ObjOwnerPtrIT_E");
extern "C" long _stub_fn_158() { return 0; }
// BinStream& operator<< <RndLightAnim>(BinStream&, ObjOwnerPtr<RndLightAnim> const&)
extern "C" __attribute__((weak, used)) long _stub_fn_159() __asm__("_ZlsI12RndLightAnimER9BinStreamS2_RK11ObjOwnerPtrIT_E");
extern "C" long _stub_fn_159() { return 0; }
// BinStream& operator<< <RndTransAnim>(BinStream&, ObjOwnerPtr<RndTransAnim> const&)
extern "C" __attribute__((weak, used)) long _stub_fn_160() __asm__("_ZlsI12RndTransAnimER9BinStreamS2_RK11ObjOwnerPtrIT_E");
extern "C" long _stub_fn_160() { return 0; }
// BinStream& operator<< <HamListRibbon>(BinStream&, ObjDirPtr<HamListRibbon> const&)
extern "C" __attribute__((weak, used)) long _stub_fn_161() __asm__("_ZlsI13HamListRibbonER9BinStreamS2_RK9ObjDirPtrIT_E");
extern "C" long _stub_fn_161() { return 0; }
// BinStream& operator<< <RndAnimatable>(BinStream&, ObjOwnerPtr<RndAnimatable> const&)
extern "C" __attribute__((weak, used)) long _stub_fn_163() __asm__("_ZlsI13RndAnimatableER9BinStreamS2_RK11ObjOwnerPtrIT_E");
extern "C" long _stub_fn_163() { return 0; }
// BinStream& operator<< <CharWeightable>(BinStream&, ObjOwnerPtr<CharWeightable> const&)
extern "C" __attribute__((weak, used)) long _stub_fn_164() __asm__("_ZlsI14CharWeightableER9BinStreamS2_RK11ObjOwnerPtrIT_E");
extern "C" long _stub_fn_164() { return 0; }
// BinStream& operator<< <RndTransformable>(BinStream&, ObjOwnerPtr<RndTransformable> const&)
extern "C" __attribute__((weak, used)) long _stub_fn_170() __asm__("_ZlsI16RndTransformableER9BinStreamS2_RK11ObjOwnerPtrIT_E");
extern "C" long _stub_fn_170() { return 0; }
// BinStream& operator<< <RndParticleSysAnim>(BinStream&, ObjOwnerPtr<RndParticleSysAnim> const&)
extern "C" __attribute__((weak, used)) long _stub_fn_172() __asm__("_ZlsI18RndParticleSysAnimER9BinStreamS2_RK11ObjOwnerPtrIT_E");
extern "C" long _stub_fn_172() { return 0; }
// BinStream& operator<< <HamScrollSpeedIndicator>(BinStream&, ObjDirPtr<HamScrollSpeedIndicator> const&)
extern "C" __attribute__((weak, used)) long _stub_fn_174() __asm__("_ZlsI23HamScrollSpeedIndicatorER9BinStreamS2_RK9ObjDirPtrIT_E");
extern "C" long _stub_fn_174() { return 0; }
// BinStream& operator<< <FxSend>(BinStream&, ObjOwnerPtr<FxSend> const&)
extern "C" __attribute__((weak, used)) long _stub_fn_177() __asm__("_ZlsI6FxSendER9BinStreamS2_RK11ObjOwnerPtrIT_E");
extern "C" long _stub_fn_177() { return 0; }
// BinStream& operator<< <RndDir>(BinStream&, ObjDirPtr<RndDir> const&)
extern "C" __attribute__((weak, used)) long _stub_fn_178() __asm__("_ZlsI6RndDirER9BinStreamS2_RK9ObjDirPtrIT_E");
extern "C" long _stub_fn_178() { return 0; }
// BinStream& operator<< <RndTex>(BinStream&, ObjOwnerPtr<RndTex> const&)
extern "C" __attribute__((weak, used)) long _stub_fn_181() __asm__("_ZlsI6RndTexER9BinStreamS2_RK11ObjOwnerPtrIT_E");
extern "C" long _stub_fn_181() { return 0; }
// BinStream& operator<< <RndFont>(BinStream&, ObjOwnerPtr<RndFont> const&)
extern "C" __attribute__((weak, used)) long _stub_fn_183() __asm__("_ZlsI7RndFontER9BinStreamS2_RK11ObjOwnerPtrIT_E");
extern "C" long _stub_fn_183() { return 0; }
// BinStream& operator<< <RndMesh>(BinStream&, ObjOwnerPtr<RndMesh> const&)
extern "C" __attribute__((weak, used)) long _stub_fn_185() __asm__("_ZlsI7RndMeshER9BinStreamS2_RK11ObjOwnerPtrIT_E");
extern "C" long _stub_fn_185() { return 0; }
// BinStream& operator<< <RndWind>(BinStream&, ObjOwnerPtr<RndWind> const&)
extern "C" __attribute__((weak, used)) long _stub_fn_186() __asm__("_ZlsI7RndWindER9BinStreamS2_RK11ObjOwnerPtrIT_E");
extern "C" long _stub_fn_186() { return 0; }
// BinStream& operator<< <RndLight>(BinStream&, ObjOwnerPtr<RndLight> const&)
extern "C" __attribute__((weak, used)) long _stub_fn_192() __asm__("_ZlsI8RndLightER9BinStreamS2_RK11ObjOwnerPtrIT_E");
extern "C" long _stub_fn_192() { return 0; }
// BinStream& operator<< <ObjectDir>(BinStream&, ObjOwnerPtr<ObjectDir> const&)
extern "C" __attribute__((weak, used)) long _stub_fn_198() __asm__("_ZlsI9ObjectDirER9BinStreamS2_RK11ObjOwnerPtrIT_E");
extern "C" long _stub_fn_198() { return 0; }
// BinStream& operator<< <ObjectDir>(BinStream&, ObjDirPtr<ObjectDir> const&)
extern "C" __attribute__((weak, used)) long _stub_fn_199() __asm__("_ZlsI9ObjectDirER9BinStreamS2_RK9ObjDirPtrIT_E");
extern "C" long _stub_fn_199() { return 0; }
// BinStream& operator<< <Spotlight>(BinStream&, ObjOwnerPtr<Spotlight> const&)
extern "C" __attribute__((weak, used)) long _stub_fn_200() __asm__("_ZlsI9SpotlightER9BinStreamS2_RK11ObjOwnerPtrIT_E");
extern "C" long _stub_fn_200() { return 0; }
// BinStream& operator<< <UIListDir>(BinStream&, ObjDirPtr<UIListDir> const&)
extern "C" __attribute__((weak, used)) long _stub_fn_202() __asm__("_ZlsI9UIListDirER9BinStreamS2_RK9ObjDirPtrIT_E");
extern "C" long _stub_fn_202() { return 0; }
// BinStream& operator<< <Hmx::Object>(BinStream&, ObjOwnerPtr<Hmx::Object> const&)
extern "C" __attribute__((weak, used)) long _stub_fn_204() __asm__("_ZlsIN3Hmx6ObjectEER9BinStreamS3_RK11ObjOwnerPtrIT_E");
extern "C" long _stub_fn_204() { return 0; }
// operator<<(BinStream&, HamCamShot::Target const&)
extern "C" __attribute__((weak, used)) long _stub_fn_206() __asm__("_ZlsR9BinStreamRKN10HamCamShot6TargetE");
extern "C" long _stub_fn_206() { return 0; }
// operator<<(BinStream&, CharBlendBone::ConstraintSystem const&)
extern "C" __attribute__((weak, used)) long _stub_fn_207() __asm__("_ZlsR9BinStreamRKN13CharBlendBone16ConstraintSystemE");
extern "C" long _stub_fn_207() { return 0; }
// CamTexClip::StoreTextureClip(RndTex*, float, float, float, float)
extern "C" __attribute__((weak, used)) long _stub_fn_208() __asm__("_ZN10CamTexClip16StoreTextureClipEP6RndTexffff");
extern "C" long _stub_fn_208() { return 0; }
// Challenges::HasNewChallenges()
extern "C" __attribute__((weak, used)) long _stub_fn_209() __asm__("_ZN10Challenges16HasNewChallengesEv");
extern "C" long _stub_fn_209() { return 0; }
// CharDriver::SyncProperty(DataNode&, DataArray*, int, PropOp)
extern "C" __attribute__((weak, used)) long _stub_fn_210() __asm__("_ZN10CharDriver12SyncPropertyER8DataNodeP9DataArrayi6PropOp");
extern "C" long _stub_fn_210() { return 0; }
// CharDriver::Copy(Hmx::Object const*, Hmx::Object::CopyType)
extern "C" __attribute__((weak, used)) long _stub_fn_211() __asm__("_ZN10CharDriver4CopyEPKN3Hmx6ObjectENS1_8CopyTypeE");
extern "C" long _stub_fn_211() { return 0; }
// CharDriver::Poll()
extern "C" __attribute__((weak, used)) long _stub_fn_212() __asm__("_ZN10CharDriver4PollEv");
extern "C" long _stub_fn_212() { return 0; }
// CharDriver::Handle(DataArray*, bool)
extern "C" __attribute__((weak, used)) long _stub_fn_213() __asm__("_ZN10CharDriver6HandleEP9DataArrayb");
extern "C" long _stub_fn_213() { return 0; }
// CharDriver::Display(float)
extern "C" __attribute__((weak, used)) long _stub_fn_214() __asm__("_ZN10CharDriver7DisplayEf");
extern "C" long _stub_fn_214() { return 0; }
// CharDriver::FindClip(DataNode const&, bool)
extern "C" __attribute__((weak, used)) long _stub_fn_215() __asm__("_ZN10CharDriver8FindClipERK8DataNodeb");
extern "C" long _stub_fn_215() { return 0; }
// CharDriver::PollDeps(std::__cxx11::list<Hmx::Object*, std::allocator<Hmx::Object*> >&, std::__cxx11::list<Hmx::Object*, std::allocator<Hmx::Object*> >&)
extern "C" __attribute__((weak, used)) long _stub_fn_216() __asm__("_ZN10CharDriver8PollDepsERNSt7__cxx114listIPN3Hmx6ObjectESaIS4_EEES7_");
extern "C" long _stub_fn_216() { return 0; }
// CharIKHand::Poll()
extern "C" __attribute__((weak, used)) long _stub_fn_217() __asm__("_ZN10CharIKHand4PollEv");
extern "C" long _stub_fn_217() { return 0; }
// CharIKHand::Highlight()
extern "C" __attribute__((weak, used)) long _stub_fn_218() __asm__("_ZN10CharIKHand9HighlightEv");
extern "C" long _stub_fn_218() { return 0; }
// CharIKHead::Poll()
extern "C" __attribute__((weak, used)) long _stub_fn_219() __asm__("_ZN10CharIKHead4PollEv");
extern "C" long _stub_fn_219() { return 0; }
// CharLookAt::Poll()
extern "C" __attribute__((weak, used)) long _stub_fn_220() __asm__("_ZN10CharLookAt4PollEv");
extern "C" long _stub_fn_220() { return 0; }
// CharLookAt::Highlight()
extern "C" __attribute__((weak, used)) long _stub_fn_221() __asm__("_ZN10CharLookAt9HighlightEv");
extern "C" long _stub_fn_221() { return 0; }
// ClipPlayer::ClipLength(CharClip*)
extern "C" __attribute__((weak, used)) long _stub_fn_222() __asm__("_ZN10ClipPlayer10ClipLengthEP8CharClip");
extern "C" long _stub_fn_222() { return 0; }
// ClipPlayer::AnnotateClip(float)
extern "C" __attribute__((weak, used)) long _stub_fn_223() __asm__("_ZN10ClipPlayer12AnnotateClipEf");
extern "C" long _stub_fn_223() { return 0; }
// ClipPlayer::PushRoutineBuilderClip(int, HamDriver::LayerArray*)
extern "C" __attribute__((weak, used)) long _stub_fn_224() __asm__("_ZN10ClipPlayer22PushRoutineBuilderClipEiPN9HamDriver10LayerArrayE");
extern "C" long _stub_fn_224() { return 0; }
// ClipPlayer::PushClip(int, HamDriver::LayerArray*)
extern "C" __attribute__((weak, used)) long _stub_fn_225() __asm__("_ZN10ClipPlayer8PushClipEiPN9HamDriver10LayerArrayE");
extern "C" long _stub_fn_225() { return 0; }
// DrawString::DrawFixedZ(float)
extern "C" __attribute__((weak, used)) long _stub_fn_226() __asm__("_ZN10DrawString10DrawFixedZEf");
extern "C" long _stub_fn_226() { return 0; }
// FlowMathOp::operator=(FlowMathOp const&)
extern "C" __attribute__((weak, used)) long _stub_fn_227() __asm__("_ZN10FlowMathOpaSERKS_");
extern "C" long _stub_fn_227() { return 0; }
// FlowSlider::UpdateActivations()
extern "C" __attribute__((weak, used)) long _stub_fn_228() __asm__("_ZN10FlowSlider17UpdateActivationsEv");
extern "C" long _stub_fn_228() { return 0; }
// FlowSwitch::VerifyTypes()
extern "C" __attribute__((weak, used)) long _stub_fn_229() __asm__("_ZN10FlowSwitch11VerifyTypesEv");
extern "C" long _stub_fn_229() { return 0; }
// FlowSwitch::ActivateValueCases(DataNode&, DataNode&)
extern "C" __attribute__((weak, used)) long _stub_fn_230() __asm__("_ZN10FlowSwitch18ActivateValueCasesER8DataNodeS1_");
extern "C" long _stub_fn_230() { return 0; }
// FreeCamera::UpdateFromCamera()
extern "C" __attribute__((weak, used)) long _stub_fn_231() __asm__("_ZN10FreeCamera16UpdateFromCameraEv");
extern "C" long _stub_fn_231() { return 0; }
// GestureMgr::PostUpdate(SkeletonUpdateData const*)
extern "C" __attribute__((weak, used)) long _stub_fn_232() __asm__("_ZN10GestureMgr10PostUpdateEPK18SkeletonUpdateData");
extern "C" long _stub_fn_232() { return 0; }
// HamCamShot::Reteleport(Vector3 const&, bool, Symbol)
extern "C" __attribute__((weak, used)) long _stub_fn_233() __asm__("_ZN10HamCamShot10ReteleportERK7Vector3b6Symbol");
extern "C" long _stub_fn_233() { return 0; }
// HamCamShot::SetPreFrame(float, float)
extern "C" __attribute__((weak, used)) long _stub_fn_234() __asm__("_ZN10HamCamShot11SetPreFrameEff");
extern "C" long _stub_fn_234() { return 0; }
// HamCamShot::OnAllowableNextShots(DataArray const*)
extern "C" __attribute__((weak, used)) long _stub_fn_235() __asm__("_ZN10HamCamShot20OnAllowableNextShotsEPK9DataArray");
extern "C" long _stub_fn_235() { return 0; }
// HamCamShot::UpdateTargetsFlipped()
extern "C" __attribute__((weak, used)) long _stub_fn_236() __asm__("_ZN10HamCamShot20UpdateTargetsFlippedEv");
extern "C" long _stub_fn_236() { return 0; }
// HamCamShot::Load(BinStream&)
extern "C" __attribute__((weak, used)) long _stub_fn_237() __asm__("_ZN10HamCamShot4LoadER9BinStream");
extern "C" long _stub_fn_237() { return 0; }
// HamCamShot::EndAnim()
extern "C" __attribute__((weak, used)) long _stub_fn_238() __asm__("_ZN10HamCamShot7EndAnimEv");
extern "C" long _stub_fn_238() { return 0; }
// HamNavList::PostUpdate(SkeletonUpdateData const*)
extern "C" __attribute__((weak, used)) long _stub_fn_239() __asm__("_ZN10HamNavList10PostUpdateEPK18SkeletonUpdateData");
extern "C" long _stub_fn_239() { return 0; }
// HamNavList::DrawShowing()
extern "C" __attribute__((weak, used)) long _stub_fn_240() __asm__("_ZN10HamNavList11DrawShowingEv");
extern "C" long _stub_fn_240() { return 0; }
// HamNavList::RealRefresh()
extern "C" __attribute__((weak, used)) long _stub_fn_241() __asm__("_ZN10HamNavList11RealRefreshEv");
extern "C" long _stub_fn_241() { return 0; }
// HamNavList::SetSelecting(bool)
extern "C" __attribute__((weak, used)) long _stub_fn_242() __asm__("_ZN10HamNavList12SetSelectingEb");
extern "C" long _stub_fn_242() { return 0; }
// HamNavList::PlayEnterAnim()
extern "C" __attribute__((weak, used)) long _stub_fn_243() __asm__("_ZN10HamNavList13PlayEnterAnimEv");
extern "C" long _stub_fn_243() { return 0; }
// HamNavList::ScrollToIndex(int, int)
extern "C" __attribute__((weak, used)) long _stub_fn_244() __asm__("_ZN10HamNavList13ScrollToIndexEii");
extern "C" long _stub_fn_244() { return 0; }
// HamNavList::SetRibbonMode(HamListRibbon::RibbonMode)
extern "C" __attribute__((weak, used)) long _stub_fn_245() __asm__("_ZN10HamNavList13SetRibbonModeEN13HamListRibbon10RibbonModeE");
extern "C" long _stub_fn_245() { return 0; }
// HamNavList::CompleteScroll(UIListState const&)
extern "C" __attribute__((weak, used)) long _stub_fn_246() __asm__("_ZN10HamNavList14CompleteScrollERK11UIListState");
extern "C" long _stub_fn_246() { return 0; }
// HamNavList::SetNavProvider(HamNavProvider*)
extern "C" __attribute__((weak, used)) long _stub_fn_247() __asm__("_ZN10HamNavList14SetNavProviderEP14HamNavProvider");
extern "C" long _stub_fn_247() { return 0; }
// HamNavList::UpdateGestures(Skeleton const*)
extern "C" __attribute__((weak, used)) long _stub_fn_248() __asm__("_ZN10HamNavList14UpdateGesturesEPK8Skeleton");
extern "C" long _stub_fn_248() { return 0; }
// HamNavList::ClearBigElements()
extern "C" __attribute__((weak, used)) long _stub_fn_249() __asm__("_ZN10HamNavList16ClearBigElementsEv");
extern "C" long _stub_fn_249() { return 0; }
// HamNavList::GetTargetSwellAmount(int)
extern "C" __attribute__((weak, used)) long _stub_fn_250() __asm__("_ZN10HamNavList20GetTargetSwellAmountEi");
extern "C" long _stub_fn_250() { return 0; }
// HamNavList::DetermineHighlightedItem()
extern "C" __attribute__((weak, used)) long _stub_fn_251() __asm__("_ZN10HamNavList24DetermineHighlightedItemEv");
extern "C" long _stub_fn_251() { return 0; }
// HamNavList::Exit()
extern "C" __attribute__((weak, used)) long _stub_fn_252() __asm__("_ZN10HamNavList4ExitEv");
extern "C" long _stub_fn_252() { return 0; }
// HamNavList::Clear()
extern "C" __attribute__((weak, used)) long _stub_fn_253() __asm__("_ZN10HamNavList5ClearEv");
extern "C" long _stub_fn_253() { return 0; }
// HamNavList::Enter()
extern "C" __attribute__((weak, used)) long _stub_fn_254() __asm__("_ZN10HamNavList5EnterEv");
extern "C" long _stub_fn_254() { return 0; }
// HamNavList::OnMsg(ButtonDownMsg const&)
extern "C" __attribute__((weak, used)) long _stub_fn_255() __asm__("_ZN10HamNavList5OnMsgERK13ButtonDownMsg");
extern "C" long _stub_fn_255() { return 0; }
// HamNavList::Update()
extern "C" __attribute__((weak, used)) long _stub_fn_256() __asm__("_ZN10HamNavList6UpdateEv");
extern "C" long _stub_fn_256() { return 0; }
// HamNavList::Disengage()
extern "C" __attribute__((weak, used)) long _stub_fn_257() __asm__("_ZN10HamNavList9DisengageEv");
extern "C" long _stub_fn_257() { return 0; }
// HamProfile::IsDifficultyUnlockedForProfile(Symbol, Symbol)
extern "C" __attribute__((weak, used)) long _stub_fn_258() __asm__("_ZN10HamProfile30IsDifficultyUnlockedForProfileE6SymbolS0_");
extern "C" long _stub_fn_258() { return 0; }
// InlineHelp::DrawShowing()
extern "C" __attribute__((weak, used)) long _stub_fn_259() __asm__("_ZN10InlineHelp11DrawShowingEv");
extern "C" long _stub_fn_259() { return 0; }
// InlineHelp::ClearActionToken(JoypadAction)
extern "C" __attribute__((weak, used)) long _stub_fn_260() __asm__("_ZN10InlineHelp16ClearActionTokenE12JoypadAction");
extern "C" long _stub_fn_260() { return 0; }
// InlineHelp::PreLoad(BinStream&)
extern "C" __attribute__((weak, used)) long _stub_fn_261() __asm__("_ZN10InlineHelp7PreLoadER9BinStream");
extern "C" long _stub_fn_261() { return 0; }
// MemcardMgr::OnLoadGame(Profile*, MemcardAction*)
extern "C" __attribute__((weak, used)) long _stub_fn_262() __asm__("_ZN10MemcardMgr10OnLoadGameEP7ProfileP13MemcardAction");
extern "C" long _stub_fn_262() { return 0; }
// MemcardMgr::OnSaveGame(Profile*, MemcardAction*, int)
extern "C" __attribute__((weak, used)) long _stub_fn_263() __asm__("_ZN10MemcardMgr10OnSaveGameEP7ProfileP13MemcardActioni");
extern "C" long _stub_fn_263() { return 0; }
// MemcardMgr::SelectDevice(Profile*, Hmx::Object*, int, bool)
extern "C" __attribute__((weak, used)) long _stub_fn_264() __asm__("_ZN10MemcardMgr12SelectDeviceEP7ProfilePN3Hmx6ObjectEib");
extern "C" long _stub_fn_264() { return 0; }
// MemcardMgr::OnDeleteSaves(Profile*)
extern "C" __attribute__((weak, used)) long _stub_fn_265() __asm__("_ZN10MemcardMgr13OnDeleteSavesEP7Profile");
extern "C" long _stub_fn_265() { return 0; }
// MemcardMgr::OnSearchForDevice(Profile*)
extern "C" __attribute__((weak, used)) long _stub_fn_266() __asm__("_ZN10MemcardMgr17OnSearchForDeviceEP7Profile");
extern "C" long _stub_fn_266() { return 0; }
// MemcardMgr::IsStorageDeviceValid(Profile*)
extern "C" __attribute__((weak, used)) long _stub_fn_267() __asm__("_ZN10MemcardMgr20IsStorageDeviceValidEP7Profile");
extern "C" long _stub_fn_267() { return 0; }
// MemcardMgr::OnCheckForSaveContainer(Profile*)
extern "C" __attribute__((weak, used)) long _stub_fn_268() __asm__("_ZN10MemcardMgr23OnCheckForSaveContainerEP7Profile");
extern "C" long _stub_fn_268() { return 0; }
// MemcardMgr::Init()
extern "C" __attribute__((weak, used)) long _stub_fn_269() __asm__("_ZN10MemcardMgr4InitEv");
extern "C" long _stub_fn_269() { return 0; }
// MemcardMgr::SetDevice(unsigned int)
extern "C" __attribute__((weak, used)) long _stub_fn_270() __asm__("_ZN10MemcardMgr9SetDeviceEj");
extern "C" long _stub_fn_270() { return 0; }
// MemcardMgr::MemcardMgr()
extern "C" __attribute__((weak, used)) long _stub_fn_271() __asm__("_ZN10MemcardMgrC1Ev");
extern "C" long _stub_fn_271() { return 0; }
// MemcardMgr::~MemcardMgr()
extern "C" __attribute__((weak, used)) long _stub_fn_272() __asm__("_ZN10MemcardMgrD1Ev");
extern "C" long _stub_fn_272() { return 0; }
// MemTracker::ReportMemoryAlloc(char const*)
extern "C" __attribute__((weak, used)) long _stub_fn_273() __asm__("_ZN10MemTracker17ReportMemoryAllocEPKc");
extern "C" long _stub_fn_273() { return 0; }
// MemTracker::ReportMemoryUsage(char const*)
extern "C" __attribute__((weak, used)) long _stub_fn_274() __asm__("_ZN10MemTracker17ReportMemoryUsageEPKc");
extern "C" long _stub_fn_274() { return 0; }
// MemTracker::ReportMemoryUsageOverview(char const*)
extern "C" __attribute__((weak, used)) long _stub_fn_275() __asm__("_ZN10MemTracker25ReportMemoryUsageOverviewEPKc");
extern "C" long _stub_fn_275() { return 0; }
// MQSongSort::BuildTree()
extern "C" __attribute__((weak, used)) long _stub_fn_276() __asm__("_ZN10MQSongSort9BuildTreeEv");
extern "C" long _stub_fn_276() { return 0; }
// NgPostProc::OnUnselect()
extern "C" __attribute__((weak, used)) long _stub_fn_277() __asm__("_ZN10NgPostProc10OnUnselectEv");
extern "C" long _stub_fn_277() { return 0; }
// NgPostProc::ReleaseTex()
extern "C" __attribute__((weak, used)) long _stub_fn_278() __asm__("_ZN10NgPostProc10ReleaseTexEv");
extern "C" long _stub_fn_278() { return 0; }
// NgPostProc::QueueMotionBlurObject(RndDrawable*)
extern "C" __attribute__((weak, used)) long _stub_fn_279() __asm__("_ZN10NgPostProc21QueueMotionBlurObjectEP11RndDrawable");
extern "C" long _stub_fn_279() { return 0; }
// NgPostProc::DoPost()
extern "C" __attribute__((weak, used)) long _stub_fn_280() __asm__("_ZN10NgPostProc6DoPostEv");
extern "C" long _stub_fn_280() { return 0; }
// NgPostProc::EndWorld()
extern "C" __attribute__((weak, used)) long _stub_fn_281() __asm__("_ZN10NgPostProc8EndWorldEv");
extern "C" long _stub_fn_281() { return 0; }
// NgPostProc::OnSelect()
extern "C" __attribute__((weak, used)) long _stub_fn_282() __asm__("_ZN10NgPostProc8OnSelectEv");
extern "C" long _stub_fn_282() { return 0; }
// NgPostProc::Terminate()
extern "C" __attribute__((weak, used)) long _stub_fn_283() __asm__("_ZN10NgPostProc9TerminateEv");
extern "C" long _stub_fn_283() { return 0; }
// ProfileMgr::GetAlternateOutfit(Symbol)
extern "C" __attribute__((weak, used)) long _stub_fn_382() __asm__("_ZN10ProfileMgr18GetAlternateOutfitE6Symbol");
extern "C" long _stub_fn_382() { return 0; }
// ProfileMgr::Poll()
extern "C" __attribute__((weak, used)) long _stub_fn_383() __asm__("_ZN10ProfileMgr4PollEv");
extern "C" long _stub_fn_383() { return 0; }
// ProfileMgr::OnMsg(SigninChangedMsg const&)
extern "C" __attribute__((weak, used)) long _stub_fn_384() __asm__("_ZN10ProfileMgr5OnMsgERK16SigninChangedMsg");
extern "C" long _stub_fn_384() { return 0; }
// RndConsole::SetShowing(bool)
extern "C" __attribute__((weak, used)) long _stub_fn_385() __asm__("_ZN10RndConsole10SetShowingEb");
extern "C" long _stub_fn_385() { return 0; }
// RndConsole::InsertBreak(DataArray*, int)
extern "C" __attribute__((weak, used)) long _stub_fn_386() __asm__("_ZN10RndConsole11InsertBreakEP9DataArrayi");
extern "C" long _stub_fn_386() { return 0; }
// RndConsole::Step(int)
extern "C" __attribute__((weak, used)) long _stub_fn_387() __asm__("_ZN10RndConsole4StepEi");
extern "C" long _stub_fn_387() { return 0; }
// RndConsole::Break(DataArray*)
extern "C" __attribute__((weak, used)) long _stub_fn_388() __asm__("_ZN10RndConsole5BreakEP9DataArray");
extern "C" long _stub_fn_388() { return 0; }
// RndConsole::Clear(int)
extern "C" __attribute__((weak, used)) long _stub_fn_389() __asm__("_ZN10RndConsole5ClearEi");
extern "C" long _stub_fn_389() { return 0; }
// RndConsole::OnMsg(KeyboardKeyMsg const&)
extern "C" __attribute__((weak, used)) long _stub_fn_390() __asm__("_ZN10RndConsole5OnMsgERK14KeyboardKeyMsg");
extern "C" long _stub_fn_390() { return 0; }
// RndConsole::MoveLevel(int)
extern "C" __attribute__((weak, used)) long _stub_fn_391() __asm__("_ZN10RndConsole9MoveLevelEi");
extern "C" long _stub_fn_391() { return 0; }
// RndEnviron::UpdateApproxLighting(Vector3 const*)
extern "C" __attribute__((weak, used)) long _stub_fn_392() __asm__("_ZN10RndEnviron20UpdateApproxLightingEPK7Vector3");
extern "C" long _stub_fn_392() { return 0; }
// RndEnviron::Load(BinStream&)
extern "C" __attribute__((weak, used)) long _stub_fn_393() __asm__("_ZN10RndEnviron4LoadER9BinStream");
extern "C" long _stub_fn_393() { return 0; }
// RndEnviron::Replace(ObjRef*, Hmx::Object*)
extern "C" __attribute__((weak, used)) long _stub_fn_394() __asm__("_ZN10RndEnviron7ReplaceEP6ObjRefPN3Hmx6ObjectE");
extern "C" long _stub_fn_394() { return 0; }
// RndMatAnim::LoadStages(BinStreamRev&)
extern "C" __attribute__((weak, used)) long _stub_fn_395() __asm__("_ZN10RndMatAnim10LoadStagesER12BinStreamRev");
extern "C" long _stub_fn_395() { return 0; }
// SampleData::Load(BinStream&, FilePath const&)
extern "C" __attribute__((weak, used)) long _stub_fn_396() __asm__("_ZN10SampleData4LoadER9BinStreamRK8FilePath");
extern "C" long _stub_fn_396() { return 0; }
// SampleData::LoadWAV(BinStream&, FilePath const&, bool)
extern "C" __attribute__((weak, used)) long _stub_fn_397() __asm__("_ZN10SampleData7LoadWAVER9BinStreamRK8FilePathb");
extern "C" long _stub_fn_397() { return 0; }
// SampleInst::SynthPoll()
extern "C" __attribute__((weak, used)) long _stub_fn_398() __asm__("_ZN10SampleInst9SynthPollEv");
extern "C" long _stub_fn_398() { return 0; }
// ScriptTask::UpdateVarsObjects(DataArray*)
extern "C" __attribute__((weak, used)) long _stub_fn_399() __asm__("_ZN10ScriptTask17UpdateVarsObjectsEP9DataArray");
extern "C" long _stub_fn_399() { return 0; }
// SongLayout::SetDefaultPattern(int)
extern "C" __attribute__((weak, used)) long _stub_fn_400() __asm__("_ZN10SongLayout17SetDefaultPatternEi");
extern "C" long _stub_fn_400() { return 0; }
// SongLayout::SetDefaultReplacer()
extern "C" __attribute__((weak, used)) long _stub_fn_401() __asm__("_ZN10SongLayout18SetDefaultReplacerEv");
extern "C" long _stub_fn_401() { return 0; }
// TextStream::operator<<(long)
extern "C" __attribute__((weak, used)) long _stub_fn_402() __asm__("_ZN10TextStreamlsEl");
extern "C" long _stub_fn_402() { return 0; }
// UIListSlot::Handle(DataArray*, bool)
extern "C" __attribute__((weak, used)) long _stub_fn_403() __asm__("_ZN10UIListSlot6HandleEP9DataArrayb");
extern "C" long _stub_fn_403() { return 0; }
// VectorSort<RndMesh*>::operator()(RndMesh*, RndMesh*)
extern "C" __attribute__((weak, used)) long _stub_fn_404() __asm__("_ZN10VectorSortIP7RndMeshEclES1_S1_");
extern "C" long _stub_fn_404() { return 0; }
// WorldCrowd::DrawShowing()
extern "C" __attribute__((weak, used)) long _stub_fn_405() __asm__("_ZN10WorldCrowd11DrawShowingEv");
extern "C" long _stub_fn_405() { return 0; }
// WorldCrowd::SetFullness(float, float)
extern "C" __attribute__((weak, used)) long _stub_fn_406() __asm__("_ZN10WorldCrowd11SetFullnessEff");
extern "C" long _stub_fn_406() { return 0; }
// WorldCrowd::Reset3DCrowd()
extern "C" __attribute__((weak, used)) long _stub_fn_407() __asm__("_ZN10WorldCrowd12Reset3DCrowdEv");
extern "C" long _stub_fn_407() { return 0; }
// WorldCrowd::Set3DCharXfm(std::_List_iterator<WorldCrowd::CharData> const&, int, Transform const&)
extern "C" __attribute__((weak, used)) long _stub_fn_408() __asm__("_ZN10WorldCrowd12Set3DCharXfmERKSt14_List_iteratorINS_8CharDataEEiRK9Transform");
extern "C" long _stub_fn_408() { return 0; }
// WorldCrowd::OnIterateFrac(DataArray*)
extern "C" __attribute__((weak, used)) long _stub_fn_409() __asm__("_ZN10WorldCrowd13OnIterateFracEP9DataArray");
extern "C" long _stub_fn_409() { return 0; }
// WorldCrowd::Set3DCharList(std::vector<std::pair<int, int>, std::allocator<std::pair<int, int> > > const&, Hmx::Object*)
extern "C" __attribute__((weak, used)) long _stub_fn_410() __asm__("_ZN10WorldCrowd13Set3DCharListERKSt6vectorISt4pairIiiESaIS2_EEPN3Hmx6ObjectE");
extern "C" long _stub_fn_410() { return 0; }
// WorldCrowd::Apply3DCharXfm(std::_List_iterator<WorldCrowd::CharData> const&, int, RndCam*)
extern "C" __attribute__((weak, used)) long _stub_fn_411() __asm__("_ZN10WorldCrowd14Apply3DCharXfmERKSt14_List_iteratorINS_8CharDataEEiP6RndCam");
extern "C" long _stub_fn_411() { return 0; }
// WorldCrowd::BuildBillboard(Character*, float)
extern "C" __attribute__((weak, used)) long _stub_fn_412() __asm__("_ZN10WorldCrowd14BuildBillboardEP9Characterf");
extern "C" long _stub_fn_412() { return 0; }
// WorldCrowd::AssignRandomColors(bool)
extern "C" __attribute__((weak, used)) long _stub_fn_413() __asm__("_ZN10WorldCrowd18AssignRandomColorsEb");
extern "C" long _stub_fn_413() { return 0; }
// WorldCrowd::Mats(std::__cxx11::list<RndMat*, std::allocator<RndMat*> >&, bool)
extern "C" __attribute__((weak, used)) long _stub_fn_414() __asm__("_ZN10WorldCrowd4MatsERNSt7__cxx114listIP6RndMatSaIS3_EEEb");
extern "C" long _stub_fn_414() { return 0; }
// ArcDetector::UpdateOverlay(RndOverlay*, float)
extern "C" __attribute__((weak, used)) long _stub_fn_415() __asm__("_ZN11ArcDetector13UpdateOverlayEP10RndOverlayf");
extern "C" long _stub_fn_415() { return 0; }
// ArcDetector::TryToStartSwipe(Vector3 const&, Skeleton const&)
extern "C" __attribute__((weak, used)) long _stub_fn_416() __asm__("_ZN11ArcDetector15TryToStartSwipeERK7Vector3RK8Skeleton");
extern "C" long _stub_fn_416() { return 0; }
// ArcDetector::Update(Skeleton const&, int)
extern "C" __attribute__((weak, used)) long _stub_fn_417() __asm__("_ZN11ArcDetector6UpdateERK8Skeletoni");
extern "C" long _stub_fn_417() { return 0; }
// CharCollide::Highlight()
extern "C" __attribute__((weak, used)) long _stub_fn_418() __asm__("_ZN11CharCollide9HighlightEv");
extern "C" long _stub_fn_418() { return 0; }
// CharIKScale::Poll()
extern "C" __attribute__((weak, used)) long _stub_fn_419() __asm__("_ZN11CharIKScale4PollEv");
extern "C" long _stub_fn_419() { return 0; }
// CharLipSync::PlayBack::SetClips(ObjPtr<ObjectDir>)
extern "C" __attribute__((weak, used)) long _stub_fn_420() __asm__("_ZN11CharLipSync8PlayBack8SetClipsE6ObjPtrI9ObjectDirE");
extern "C" long _stub_fn_420() { return 0; }
// CharLipSync::Generator::AddWeight(int, float)
extern "C" __attribute__((weak, used)) long _stub_fn_421() __asm__("_ZN11CharLipSync9Generator9AddWeightEif");
extern "C" long _stub_fn_421() { return 0; }
// ClipDistMap::FindBestNodeRecurse(float, float, float, float, float)
extern "C" __attribute__((weak, used)) long _stub_fn_422() __asm__("_ZN11ClipDistMap19FindBestNodeRecurseEfffff");
extern "C" long _stub_fn_422() { return 0; }
// ClipDistMap::Draw(float, float, CharDriver*)
extern "C" __attribute__((weak, used)) long _stub_fn_423() __asm__("_ZN11ClipDistMap4DrawEffP10CharDriver");
extern "C" long _stub_fn_423() { return 0; }
// ClipDistMap::FindDists(float, DataArray*)
extern "C" __attribute__((weak, used)) long _stub_fn_424() __asm__("_ZN11ClipDistMap9FindDistsEfP9DataArray");
extern "C" long _stub_fn_424() { return 0; }
// DelayEffect::SetParameter(int, float)
extern "C" __attribute__((weak, used)) long _stub_fn_425() __asm__("_ZN11DelayEffect12SetParameterEif");
extern "C" long _stub_fn_425() { return 0; }
// DingoServer::InitAndAddJob(DingoJob*, bool, bool)
extern "C" __attribute__((weak, used)) long _stub_fn_426() __asm__("_ZN11DingoServer13InitAndAddJobEP8DingoJobbb");
extern "C" long _stub_fn_426() { return 0; }
// DingoServer::SendAuthenticateMsg(char const*, DataPoint&, Hmx::Object*)
extern "C" __attribute__((weak, used)) long _stub_fn_427() __asm__("_ZN11DingoServer19SendAuthenticateMsgEPKcR9DataPointPN3Hmx6ObjectE");
extern "C" long _stub_fn_427() { return 0; }
// DingoServer::OnMsg(SigninChangedMsg const&)
extern "C" __attribute__((weak, used)) long _stub_fn_428() __asm__("_ZN11DingoServer5OnMsgERK16SigninChangedMsg");
extern "C" long _stub_fn_428() { return 0; }
// DingoServer::OnMsg(ConnectionStatusChangedMsg const&)
extern "C" __attribute__((weak, used)) long _stub_fn_429() __asm__("_ZN11DingoServer5OnMsgERK26ConnectionStatusChangedMsg");
extern "C" long _stub_fn_429() { return 0; }
// FilterQueue::Poll(SkeletonUpdateData const&)
extern "C" __attribute__((weak, used)) long _stub_fn_430() __asm__("_ZN11FilterQueue4PollERK18SkeletonUpdateData");
extern "C" long _stub_fn_430() { return 0; }
// FlowAnimate::OnAnimEvent(Symbol)
extern "C" __attribute__((weak, used)) long _stub_fn_431() __asm__("_ZN11FlowAnimate11OnAnimEventE6Symbol");
extern "C" long _stub_fn_431() { return 0; }
// FlowAnimate::ChildFinished(FlowNode*)
extern "C" __attribute__((weak, used)) long _stub_fn_432() __asm__("_ZN11FlowAnimate13ChildFinishedEP8FlowNode");
extern "C" long _stub_fn_432() { return 0; }
// FlowAnimate::Execute(FlowNode::QueueState)
extern "C" __attribute__((weak, used)) long _stub_fn_433() __asm__("_ZN11FlowAnimate7ExecuteEN8FlowNode10QueueStateE");
extern "C" long _stub_fn_433() { return 0; }
// FlowAnimate::Replace(ObjRef*, Hmx::Object*)
extern "C" __attribute__((weak, used)) long _stub_fn_434() __asm__("_ZN11FlowAnimate7ReplaceEP6ObjRefPN3Hmx6ObjectE");
extern "C" long _stub_fn_434() { return 0; }
// FlowAnimate::Activate()
extern "C" __attribute__((weak, used)) long _stub_fn_435() __asm__("_ZN11FlowAnimate8ActivateEv");
extern "C" long _stub_fn_435() { return 0; }
// FlowPickOne::OnChoiceTypeChanged()
extern "C" __attribute__((weak, used)) long _stub_fn_436() __asm__("_ZN11FlowPickOne19OnChoiceTypeChangedEv");
extern "C" long _stub_fn_436() { return 0; }
// FlowPickOne::Activate()
extern "C" __attribute__((weak, used)) long _stub_fn_437() __asm__("_ZN11FlowPickOne8ActivateEv");
extern "C" long _stub_fn_437() { return 0; }
// HamDirector::DrawIconMan(Difficulty, float, float, float, float, RndTex*)
extern "C" __attribute__((weak, used)) long _stub_fn_438() __asm__("_ZN11HamDirector11DrawIconManE10DifficultyffffP6RndTex");
extern "C" long _stub_fn_438() { return 0; }
// HamDirector::DrawIconMan(Symbol, Symbol, Symbol, float, float, RndTex*)
extern "C" __attribute__((weak, used)) long _stub_fn_439() __asm__("_ZN11HamDirector11DrawIconManE6SymbolS0_S0_ffP6RndTex");
extern "C" long _stub_fn_439() { return 0; }
// HamDirector::PlayNextShot()
extern "C" __attribute__((weak, used)) long _stub_fn_440() __asm__("_ZN11HamDirector12PlayNextShotEv");
extern "C" long _stub_fn_440() { return 0; }
// HamDirector::OnSelectCamera(DataArray*)
extern "C" __attribute__((weak, used)) long _stub_fn_441() __asm__("_ZN11HamDirector14OnSelectCameraEP9DataArray");
extern "C" long _stub_fn_441() { return 0; }
// HamDirector::OnPopulateMoves()
extern "C" __attribute__((weak, used)) long _stub_fn_442() __asm__("_ZN11HamDirector15OnPopulateMovesEv");
extern "C" long _stub_fn_442() { return 0; }
// HamDirector::OnPopulateFromMoveMgr()
extern "C" __attribute__((weak, used)) long _stub_fn_443() __asm__("_ZN11HamDirector21OnPopulateFromMoveMgrEv");
extern "C" long _stub_fn_443() { return 0; }
// HamDirector::GetClipStartAndEndBeats(Symbol, float&, float&, std::pair<float, float>*)
extern "C" __attribute__((weak, used)) long _stub_fn_444() __asm__("_ZN11HamDirector23GetClipStartAndEndBeatsE6SymbolRfS1_PSt4pairIffE");
extern "C" long _stub_fn_444() { return 0; }
// HamDirector::Poll()
extern "C" __attribute__((weak, used)) long _stub_fn_445() __asm__("_ZN11HamDirector4PollEv");
extern "C" long _stub_fn_445() { return 0; }
// HamRegulate::Poll()
extern "C" __attribute__((weak, used)) long _stub_fn_446() __asm__("_ZN11HamRegulate4PollEv");
extern "C" long _stub_fn_446() { return 0; }
// HamWardrobe::OnAddCrowd(DataArray*)
extern "C" __attribute__((weak, used)) long _stub_fn_447() __asm__("_ZN11HamWardrobe10OnAddCrowdEP9DataArray");
extern "C" long _stub_fn_447() { return 0; }
// HamWardrobe::OnSetVenue(DataArray*)
extern "C" __attribute__((weak, used)) long _stub_fn_448() __asm__("_ZN11HamWardrobe10OnSetVenueEP9DataArray");
extern "C" long _stub_fn_448() { return 0; }
// HamWardrobe::LoadCharacters(Symbol, Symbol, Symbol, Symbol, HamBackupDancers, Symbol, Symbol, bool)
extern "C" __attribute__((weak, used)) long _stub_fn_449() __asm__("_ZN11HamWardrobe14LoadCharactersE6SymbolS0_S0_S0_16HamBackupDancersS0_S0_b");
extern "C" long _stub_fn_449() { return 0; }
// HamWardrobe::OnLoadCharacters(DataArray*)
extern "C" __attribute__((weak, used)) long _stub_fn_450() __asm__("_ZN11HamWardrobe16OnLoadCharactersEP9DataArray");
extern "C" long _stub_fn_450() { return 0; }
// HamWardrobe::PlayCrowdAnimation(Symbol, int, bool)
extern "C" __attribute__((weak, used)) long _stub_fn_451() __asm__("_ZN11HamWardrobe18PlayCrowdAnimationE6Symbolib");
extern "C" long _stub_fn_451() { return 0; }
// LightPreset::SetFrameEx(float, float, bool)
extern "C" __attribute__((weak, used)) long _stub_fn_452() __asm__("_ZN11LightPreset10SetFrameExEffb");
extern "C" long _stub_fn_452() { return 0; }
// LightPreset::CacheFrames()
extern "C" __attribute__((weak, used)) long _stub_fn_453() __asm__("_ZN11LightPreset11CacheFramesEv");
extern "C" long _stub_fn_453() { return 0; }
// LightPreset::SetKeyframe(LightPreset::Keyframe&)
extern "C" __attribute__((weak, used)) long _stub_fn_454() __asm__("_ZN11LightPreset11SetKeyframeERNS_8KeyframeE");
extern "C" long _stub_fn_454() { return 0; }
// LightPreset::OnSetKeyframe(DataArray*)
extern "C" __attribute__((weak, used)) long _stub_fn_455() __asm__("_ZN11LightPreset13OnSetKeyframeEP9DataArray");
extern "C" long _stub_fn_455() { return 0; }
// LightPreset::FillEnvPresetData(RndEnviron*, LightPreset::EnvironmentEntry&)
extern "C" __attribute__((weak, used)) long _stub_fn_456() __asm__("_ZN11LightPreset17FillEnvPresetDataEP10RndEnvironRNS_16EnvironmentEntryE");
extern "C" long _stub_fn_456() { return 0; }
// LightPreset::Load(BinStream&)
extern "C" __attribute__((weak, used)) long _stub_fn_457() __asm__("_ZN11LightPreset4LoadER9BinStream");
extern "C" long _stub_fn_457() { return 0; }
// LightPreset::Animate(float)
extern "C" __attribute__((weak, used)) long _stub_fn_458() __asm__("_ZN11LightPreset7AnimateEf");
extern "C" long _stub_fn_458() { return 0; }
// LightPreset::Replace(ObjRef*, Hmx::Object*)
extern "C" __attribute__((weak, used)) long _stub_fn_459() __asm__("_ZN11LightPreset7ReplaceEP6ObjRefPN3Hmx6ObjectE");
extern "C" long _stub_fn_459() { return 0; }
// LocalePanel::AddDirEntries(ObjectDir*, char const*)
extern "C" __attribute__((weak, used)) long _stub_fn_460() __asm__("_ZN11LocalePanel13AddDirEntriesEP9ObjectDirPKc");
extern "C" long _stub_fn_460() { return 0; }
// LocationCmp::LocationCmp()
extern "C" __attribute__((weak, used)) long _stub_fn_461() __asm__("_ZN11LocationCmpC1Ev");
extern "C" long _stub_fn_461() { return 0; }
// MemcardXbox::Poll()
extern "C" __attribute__((weak, used)) long _stub_fn_462() __asm__("_ZN11MemcardXbox4PollEv");
extern "C" long _stub_fn_462() { return 0; }
// NavListSort::ChangeHighlightHeader(int)
extern "C" __attribute__((weak, used)) long _stub_fn_463() __asm__("_ZN11NavListSort21ChangeHighlightHeaderEi");
extern "C" long _stub_fn_463() { return 0; }
// NetCacheMgr::PollLoaders()
extern "C" __attribute__((weak, used)) long _stub_fn_464() __asm__("_ZN11NetCacheMgr11PollLoadersEv");
extern "C" long _stub_fn_464() { return 0; }
// NetCacheMgr::AddLoaderRef(char const*, NetCacheMgr::RefType, NetLoaderPos)
extern "C" __attribute__((weak, used)) long _stub_fn_465() __asm__("_ZN11NetCacheMgr12AddLoaderRefEPKcNS_7RefTypeE12NetLoaderPos");
extern "C" long _stub_fn_465() { return 0; }
// PlatformMgr::DisableXMP()
extern "C" __attribute__((weak, used)) long _stub_fn_466() __asm__("_ZN11PlatformMgr10DisableXMPEv");
extern "C" long _stub_fn_466() { return 0; }
// PlatformMgr::RegionInit()
extern "C" __attribute__((weak, used)) long _stub_fn_467() __asm__("_ZN11PlatformMgr10RegionInitEv");
extern "C" long _stub_fn_467() { return 0; }
// PlatformMgr::InviteParty(int)
extern "C" __attribute__((weak, used)) long _stub_fn_468() __asm__("_ZN11PlatformMgr11InvitePartyEi");
extern "C" long _stub_fn_468() { return 0; }
// PlatformMgr::ShowOfferUI(int)
extern "C" __attribute__((weak, used)) long _stub_fn_469() __asm__("_ZN11PlatformMgr11ShowOfferUIEi");
extern "C" long _stub_fn_469() { return 0; }
// PlatformMgr::ShowPartyUI(int)
extern "C" __attribute__((weak, used)) long _stub_fn_470() __asm__("_ZN11PlatformMgr11ShowPartyUIEi");
extern "C" long _stub_fn_470() { return 0; }
// PlatformMgr::SignInUsers(int, unsigned long)
extern "C" __attribute__((weak, used)) long _stub_fn_471() __asm__("_ZN11PlatformMgr11SignInUsersEim");
extern "C" long _stub_fn_471() { return 0; }
// PlatformMgr::CheckMailbox()
extern "C" __attribute__((weak, used)) long _stub_fn_472() __asm__("_ZN11PlatformMgr12CheckMailboxEv");
extern "C" long _stub_fn_472() { return 0; }
// PlatformMgr::GetServiceID(String const&, unsigned int&)
extern "C" __attribute__((weak, used)) long _stub_fn_473() __asm__("_ZN11PlatformMgr12GetServiceIDERK6StringRj");
extern "C" long _stub_fn_473() { return 0; }
// PlatformMgr::OnSignInUsers(DataArray*)
extern "C" __attribute__((weak, used)) long _stub_fn_474() __asm__("_ZN11PlatformMgr13OnSignInUsersEP9DataArray");
extern "C" long _stub_fn_474() { return 0; }
// PlatformMgr::ShowFriendsUI(int)
extern "C" __attribute__((weak, used)) long _stub_fn_475() __asm__("_ZN11PlatformMgr13ShowFriendsUIEi");
extern "C" long _stub_fn_475() { return 0; }
// PlatformMgr::SetScreenSaver(bool)
extern "C" __attribute__((weak, used)) long _stub_fn_476() __asm__("_ZN11PlatformMgr14SetScreenSaverEb");
extern "C" long _stub_fn_476() { return 0; }
// PlatformMgr::SmartGlassSend(unsigned long, DataArray const*)
extern "C" __attribute__((weak, used)) long _stub_fn_477() __asm__("_ZN11PlatformMgr14SmartGlassSendEmPK9DataArray");
extern "C" long _stub_fn_477() { return 0; }
// PlatformMgr::GetOwnerOfGuest(int)
extern "C" __attribute__((weak, used)) long _stub_fn_478() __asm__("_ZN11PlatformMgr15GetOwnerOfGuestEi");
extern "C" long _stub_fn_478() { return 0; }
// PlatformMgr::EnumerateFriends(int, std::vector<Friend*, std::allocator<Friend*> >&, Hmx::Object*)
extern "C" __attribute__((weak, used)) long _stub_fn_479() __asm__("_ZN11PlatformMgr16EnumerateFriendsEiRSt6vectorIP6FriendSaIS2_EEPN3Hmx6ObjectE");
extern "C" long _stub_fn_479() { return 0; }
// PlatformMgr::RunNetStartUtility()
extern "C" __attribute__((weak, used)) long _stub_fn_480() __asm__("_ZN11PlatformMgr18RunNetStartUtilityEv");
extern "C" long _stub_fn_480() { return 0; }
// PlatformMgr::IsInPartyWithOthers()
extern "C" __attribute__((weak, used)) long _stub_fn_481() __asm__("_ZN11PlatformMgr19IsInPartyWithOthersEv");
extern "C" long _stub_fn_481() { return 0; }
// PlatformMgr::SetNotifyUILocation(NotifyLocation)
extern "C" __attribute__((weak, used)) long _stub_fn_482() __asm__("_ZN11PlatformMgr19SetNotifyUILocationE14NotifyLocation");
extern "C" long _stub_fn_482() { return 0; }
// PlatformMgr::IsSmartGlassConnected()
extern "C" __attribute__((weak, used)) long _stub_fn_483() __asm__("_ZN11PlatformMgr21IsSmartGlassConnectedEv");
extern "C" long _stub_fn_483() { return 0; }
// PlatformMgr::ShowGamercardForPadNum(int, OnlineID const*)
extern "C" __attribute__((weak, used)) long _stub_fn_484() __asm__("_ZN11PlatformMgr22ShowGamercardForPadNumEiPK8OnlineID");
extern "C" long _stub_fn_484() { return 0; }
// PlatformMgr::PollXSocialCapabilities()
extern "C" __attribute__((weak, used)) long _stub_fn_485() __asm__("_ZN11PlatformMgr23PollXSocialCapabilitiesEv");
extern "C" long _stub_fn_485() { return 0; }
// PlatformMgr::IsEthernetCableConnected()
extern "C" __attribute__((weak, used)) long _stub_fn_486() __asm__("_ZN11PlatformMgr24IsEthernetCableConnectedEv");
extern "C" long _stub_fn_486() { return 0; }
// PlatformMgr::QueryXSocialCapabilities()
extern "C" __attribute__((weak, used)) long _stub_fn_487() __asm__("_ZN11PlatformMgr24QueryXSocialCapabilitiesEv");
extern "C" long _stub_fn_487() { return 0; }
// PlatformMgr::ShowControllerRequiredUI(Hmx::Object*)
extern "C" __attribute__((weak, used)) long _stub_fn_488() __asm__("_ZN11PlatformMgr24ShowControllerRequiredUIEPN3Hmx6ObjectE");
extern "C" long _stub_fn_488() { return 0; }
// PlatformMgr::ShowFitnessBodyProfileUI(int)
extern "C" __attribute__((weak, used)) long _stub_fn_489() __asm__("_ZN11PlatformMgr24ShowFitnessBodyProfileUIEi");
extern "C" long _stub_fn_489() { return 0; }
// PlatformMgr::SetBackgroundDownloadPriority(bool)
extern "C" __attribute__((weak, used)) long _stub_fn_490() __asm__("_ZN11PlatformMgr29SetBackgroundDownloadPriorityEb");
extern "C" long _stub_fn_490() { return 0; }
// PlatformMgr::Init()
extern "C" __attribute__((weak, used)) long _stub_fn_491() __asm__("_ZN11PlatformMgr4InitEv");
extern "C" long _stub_fn_491() { return 0; }
// PlatformMgr::Poll()
extern "C" __attribute__((weak, used)) long _stub_fn_492() __asm__("_ZN11PlatformMgr4PollEv");
extern "C" long _stub_fn_492() { return 0; }
// PlatformMgr::PreInit()
extern "C" __attribute__((weak, used)) long _stub_fn_493() __asm__("_ZN11PlatformMgr7PreInitEv");
extern "C" long _stub_fn_493() { return 0; }
// PlatformMgr::EnableXMP()
extern "C" __attribute__((weak, used)) long _stub_fn_494() __asm__("_ZN11PlatformMgr9EnableXMPEv");
extern "C" long _stub_fn_494() { return 0; }
// PlatformMgr::IsInParty()
extern "C" __attribute__((weak, used)) long _stub_fn_495() __asm__("_ZN11PlatformMgr9IsInPartyEv");
extern "C" long _stub_fn_495() { return 0; }
// PlatformMgr::PlatformMgr()
extern "C" __attribute__((weak, used)) long _stub_fn_496() __asm__("_ZN11PlatformMgrC1Ev");
extern "C" long _stub_fn_496() { return 0; }
// PlatformMgr::~PlatformMgr()
extern "C" __attribute__((weak, used)) long _stub_fn_497() __asm__("_ZN11PlatformMgrD1Ev");
extern "C" long _stub_fn_497() { return 0; }
// RndColorXfm::AdjustSaturation()
extern "C" __attribute__((weak, used)) long _stub_fn_498() __asm__("_ZN11RndColorXfm16AdjustSaturationEv");
extern "C" long _stub_fn_498() { return 0; }
// RndColorXfm::AdjustHue()
extern "C" __attribute__((weak, used)) long _stub_fn_499() __asm__("_ZN11RndColorXfm9AdjustHueEv");
extern "C" long _stub_fn_499() { return 0; }
// RndFontBase::Load(BinStream&) — now implemented in FontBase.cpp
// RndMeshAnim::ShrinkVerts(int)
extern "C" __attribute__((weak, used)) long _stub_fn_501() __asm__("_ZN11RndMeshAnim11ShrinkVertsEi");
extern "C" long _stub_fn_501() { return 0; }
// RndPostProc::UpdateColorModulation()
extern "C" __attribute__((weak, used)) long _stub_fn_502() __asm__("_ZN11RndPostProc21UpdateColorModulationEv");
extern "C" long _stub_fn_502() { return 0; }
// RndPostProc::Interp(RndPostProc const*, RndPostProc const*, float)
extern "C" __attribute__((weak, used)) long _stub_fn_503() __asm__("_ZN11RndPostProc6InterpEPKS_S1_f");
extern "C" long _stub_fn_503() { return 0; }
// RndPostProc::LoadRev(BinStreamRev&)
extern "C" __attribute__((weak, used)) long _stub_fn_504() __asm__("_ZN11RndPostProc7LoadRevER12BinStreamRev");
extern "C" long _stub_fn_504() { return 0; }
// RndPropAnim::ForeachKeyframe(DataArray const*)
extern "C" __attribute__((weak, used)) long _stub_fn_505() __asm__("_ZN11RndPropAnim15ForeachKeyframeEPK9DataArray");
extern "C" long _stub_fn_505() { return 0; }
// SharedGroup::AddPolls(RndGroup*)
extern "C" __attribute__((weak, used)) long _stub_fn_506() __asm__("_ZN11SharedGroup8AddPollsEP8RndGroup");
extern "C" long _stub_fn_506() { return 0; }
// SkeletonViz::DrawPoint3D(Vector3 const&, float, Hmx::Color const&, float)
extern "C" __attribute__((weak, used)) long _stub_fn_507() __asm__("_ZN11SkeletonViz11DrawPoint3DERK7Vector3fRKN3Hmx5ColorEf");
extern "C" long _stub_fn_507() { return 0; }
// SkeletonViz::Poll()
extern "C" __attribute__((weak, used)) long _stub_fn_508() __asm__("_ZN11SkeletonViz4PollEv");
extern "C" long _stub_fn_508() { return 0; }
// SkeletonViz::Visualize(CameraInput const&, BaseSkeleton const&, std::vector<SkeletonCallback*, std::allocator<SkeletonCallback*> >*, bool)
extern "C" __attribute__((weak, used)) long _stub_fn_509() __asm__("_ZN11SkeletonViz9VisualizeERK11CameraInputRK12BaseSkeletonPSt6vectorIP16SkeletonCallbackSaIS8_EEb");
extern "C" long _stub_fn_509() { return 0; }
// SongSortMgr::SetSetlistMode(bool)
extern "C" __attribute__((weak, used)) long _stub_fn_510() __asm__("_ZN11SongSortMgr14SetSetlistModeEb");
extern "C" long _stub_fn_510() { return 0; }
// SongSortMgr::SetupQuasiRandomSongs()
extern "C" __attribute__((weak, used)) long _stub_fn_511() __asm__("_ZN11SongSortMgr21SetupQuasiRandomSongsEv");
extern "C" long _stub_fn_511() { return 0; }
// SynthSample::Copy(Hmx::Object const*, Hmx::Object::CopyType)
extern "C" __attribute__((weak, used)) long _stub_fn_512() __asm__("_ZN11SynthSample4CopyEPKN3Hmx6ObjectENS1_8CopyTypeE");
extern "C" long _stub_fn_512() { return 0; }
// SynthSample::Load(BinStream&)
extern "C" __attribute__((weak, used)) long _stub_fn_513() __asm__("_ZN11SynthSample4LoadER9BinStream");
extern "C" long _stub_fn_513() { return 0; }
// SynthSample::PreLoad(BinStream&)
extern "C" __attribute__((weak, used)) long _stub_fn_514() __asm__("_ZN11SynthSample7PreLoadER9BinStream");
extern "C" long _stub_fn_514() { return 0; }
// SynthSample::PostLoad(BinStream&)
extern "C" __attribute__((weak, used)) long _stub_fn_515() __asm__("_ZN11SynthSample8PostLoadER9BinStream");
extern "C" long _stub_fn_515() { return 0; }
// ThreeDSound::CalculateFaderVolume()
extern "C" __attribute__((weak, used)) long _stub_fn_516() __asm__("_ZN11ThreeDSound20CalculateFaderVolumeEv");
extern "C" long _stub_fn_516() { return 0; }
// UIComponent::Exit()
extern "C" __attribute__((weak, used)) long _stub_fn_517() __asm__("_ZN11UIComponent4ExitEv");
extern "C" long _stub_fn_517() { return 0; }
// UIComponent::PostLoad(BinStream&)
extern "C" __attribute__((weak, used)) long _stub_fn_518() __asm__("_ZN11UIComponent8PostLoadER9BinStream");
extern "C" long _stub_fn_518() { return 0; }
// XboxMapFile::ParseStack(char const*, StackData*, int, FixedString&)
extern "C" __attribute__((weak, used)) long _stub_fn_519() __asm__("_ZN11XboxMapFile10ParseStackEPKcP9StackDataiR11FixedString");
extern "C" long _stub_fn_519() { return 0; }
// Achievements::PlatformInit()
extern "C" __attribute__((weak, used)) long _stub_fn_519b() __asm__("_ZN12Achievements12PlatformInitEv");
extern "C" long _stub_fn_519b() { return 0; }
// Achievements::GetAchievementData(int, int)
extern "C" __attribute__((weak, used)) long _stub_fn_520() __asm__("_ZN12Achievements18GetAchievementDataEii");
extern "C" long _stub_fn_520() { return 0; }
// Achievements::SubmitAchievementsFunc()
extern "C" __attribute__((weak, used)) long _stub_fn_521() __asm__("_ZN12Achievements22SubmitAchievementsFuncEv");
extern "C" long _stub_fn_521() { return 0; }
// AsyncFileWin::AsyncFileWin(char const*, int)
extern "C" __attribute__((weak, used)) long _stub_fn_522() __asm__("_ZN12AsyncFileWinC1EPKci");
extern "C" long _stub_fn_522() { return 0; }
// BaseSkeleton::MakeCameraToPlayerXfm(SkeletonCoordSys, Transform&, Vector3 const*, Vector3 const&)
extern "C" __attribute__((weak, used)) long _stub_fn_523() __asm__("_ZN12BaseSkeleton21MakeCameraToPlayerXfmE16SkeletonCoordSysR9TransformPK7Vector3RS4_");
extern "C" long _stub_fn_523() { return 0; }
// BinkMovieSys::PlatformInit()
extern "C" __attribute__((weak, used)) long _stub_fn_524() __asm__("_ZN12BinkMovieSys12PlatformInitEv");
extern "C" long _stub_fn_524() { return 0; }
// CacheMgrXbox::CacheMgrXbox()
extern "C" __attribute__((weak, used)) long _stub_fn_525() __asm__("_ZN12CacheMgrXboxC1Ev");
extern "C" long _stub_fn_525() { return 0; }
// CamShotFrame::Interp(CamShotFrame const&, float, float, RndCam*)
extern "C" __attribute__((weak, used)) long _stub_fn_526() __asm__("_ZN12CamShotFrame6InterpERKS_ffP6RndCam");
extern "C" long _stub_fn_526() { return 0; }
// CharFeedback::Poll()
extern "C" __attribute__((weak, used)) long _stub_fn_527() __asm__("_ZN12CharFeedback4PollEv");
extern "C" long _stub_fn_527() { return 0; }
// CharMeshHide::Handle(DataArray*, bool)
extern "C" __attribute__((weak, used)) long _stub_fn_528() __asm__("_ZN12CharMeshHide6HandleEP9DataArrayb");
extern "C" long _stub_fn_528() { return 0; }
// DanceRemixer::SetJump(int, int)
extern "C" __attribute__((weak, used)) long _stub_fn_529() __asm__("_ZN12DanceRemixer7SetJumpEii");
extern "C" long _stub_fn_529() { return 0; }
// DanceRemixer::ClearJump()
extern "C" __attribute__((weak, used)) long _stub_fn_530() __asm__("_ZN12DanceRemixer9ClearJumpEv");
extern "C" long _stub_fn_530() { return 0; }
// DrawString3D::DrawFixedZ(float)
extern "C" __attribute__((weak, used)) long _stub_fn_531() __asm__("_ZN12DrawString3D10DrawFixedZEf");
extern "C" long _stub_fn_531() { return 0; }
// FlowDistance::Execute(FlowNode::QueueState)
extern "C" __attribute__((weak, used)) long _stub_fn_534() __asm__("_ZN12FlowDistance7ExecuteEN8FlowNode10QueueStateE");
extern "C" long _stub_fn_534() { return 0; }
// FlowSequence::Activate()
extern "C" __attribute__((weak, used)) long _stub_fn_535() __asm__("_ZN12FlowSequence8ActivateEv");
extern "C" long _stub_fn_535() { return 0; }
// GlitchFinder::CheckDump()
extern "C" __attribute__((weak, used)) long _stub_fn_536() __asm__("_ZN12GlitchFinder9CheckDumpEv");
extern "C" long _stub_fn_536() { return 0; }
// (anonymous namespace)::CheckReads(bool)
extern "C" __attribute__((weak, used)) long _stub_fn_537() __asm__("_ZN12_GLOBAL__N_110CheckReadsEb");
extern "C" long _stub_fn_537() { return 0; }
// (anonymous namespace)::WaitForReads()
extern "C" __attribute__((weak, used)) long _stub_fn_538() __asm__("_ZN12_GLOBAL__N_112WaitForReadsEv");
extern "C" long _stub_fn_538() { return 0; }
// (anonymous namespace)::VertexToWorld(Vector3&, Transform const&, float, Vector4 const&)
extern "C" __attribute__((weak, used)) long _stub_fn_539() __asm__("_ZN12_GLOBAL__N_113VertexToWorldER7Vector3RK9TransformfRK7Vector4");
extern "C" long _stub_fn_539() { return 0; }
// (anonymous namespace)::WaitForResponse(Holmes::Protocol)
extern "C" __attribute__((weak, used)) long _stub_fn_540() __asm__("_ZN12_GLOBAL__N_115WaitForResponseEN6Holmes8ProtocolE");
extern "C" long _stub_fn_540() { return 0; }
// (anonymous namespace)::CheckForResponse(Holmes::Protocol, bool)
extern "C" __attribute__((weak, used)) long _stub_fn_541() __asm__("_ZN12_GLOBAL__N_116CheckForResponseEN6Holmes8ProtocolEb");
extern "C" long _stub_fn_541() { return 0; }
// (anonymous namespace)::DecodeThreadEntry(void*)
extern "C" __attribute__((weak, used)) long _stub_fn_542() __asm__("_ZN12_GLOBAL__N_117DecodeThreadEntryEPv");
extern "C" long _stub_fn_542() { return 0; }
// (anonymous namespace)::JointToVertexData(Vector3&, Skeleton const&, SkeletonJoint, Vector4 const&)
extern "C" __attribute__((weak, used)) long _stub_fn_543() __asm__("_ZN12_GLOBAL__N_117JointToVertexDataER7Vector3RK8Skeleton13SkeletonJointRK7Vector4");
extern "C" long _stub_fn_543() { return 0; }
// (anonymous namespace)::WaitForAnyResponse(Holmes::Protocol)
extern "C" __attribute__((weak, used)) long _stub_fn_544() __asm__("_ZN12_GLOBAL__N_118WaitForAnyResponseEN6Holmes8ProtocolE");
extern "C" long _stub_fn_544() { return 0; }
// (anonymous namespace)::WriteMemoryCallback(void*, unsigned int, unsigned int, void*)
extern "C" __attribute__((weak, used)) long _stub_fn_545() __asm__("_ZN12_GLOBAL__N_119WriteMemoryCallbackEPvjjS0_");
extern "C" long _stub_fn_545() { return 0; }
// (anonymous namespace)::LoadDebugDepthBuffer(RndTex*&)
extern "C" __attribute__((weak, used)) long _stub_fn_546() __asm__("_ZN12_GLOBAL__N_120LoadDebugDepthBufferERP6RndTex");
extern "C" long _stub_fn_546() { return 0; }
// (anonymous namespace)::SetColorCameraProperty(_NUI_CAMERA_PROPERTY, long)
extern "C" __attribute__((weak, used)) long _stub_fn_547() __asm__("_ZN12_GLOBAL__N_122SetColorCameraPropertyE20_NUI_CAMERA_PROPERTYl");
extern "C" long _stub_fn_547() { return 0; }
// (anonymous namespace)::HolmesFlushStreamBuffer()
extern "C" __attribute__((weak, used)) long _stub_fn_548() __asm__("_ZN12_GLOBAL__N_123HolmesFlushStreamBufferEv");
extern "C" long _stub_fn_548() { return 0; }
// (anonymous namespace)::ClipStart(CharClip*, float, float&, float&)
extern "C" __attribute__((weak, used)) long _stub_fn_549() __asm__("_ZN12_GLOBAL__N_19ClipStartEP8CharClipfRfS2_");
extern "C" long _stub_fn_549() { return 0; }
// HamCharacter::OnSoundPlay(DataArray const*)
extern "C" __attribute__((weak, used)) long _stub_fn_550() __asm__("_ZN12HamCharacter11OnSoundPlayEPK9DataArray");
extern "C" long _stub_fn_550() { return 0; }
// HamCharacter::GetNeutralSkeleton()
extern "C" __attribute__((weak, used)) long _stub_fn_551() __asm__("_ZN12HamCharacter18GetNeutralSkeletonEv");
extern "C" long _stub_fn_551() { return 0; }
// HamCharacter::SetFaceOverrideClip(Symbol, bool)
extern "C" __attribute__((weak, used)) long _stub_fn_552() __asm__("_ZN12HamCharacter19SetFaceOverrideClipE6Symbolb");
extern "C" long _stub_fn_552() { return 0; }
// HamCharacter::BlendInFaceOverrideClip(Symbol, float, float)
extern "C" __attribute__((weak, used)) long _stub_fn_553() __asm__("_ZN12HamCharacter23BlendInFaceOverrideClipE6Symbolff");
extern "C" long _stub_fn_553() { return 0; }
// HamCharacter::Poll()
extern "C" __attribute__((weak, used)) long _stub_fn_554() __asm__("_ZN12HamCharacter4PollEv");
extern "C" long _stub_fn_554() { return 0; }
// LoadingPanel::Poll()
extern "C" __attribute__((weak, used)) long _stub_fn_555() __asm__("_ZN12LoadingPanel4PollEv");
extern "C" long _stub_fn_555() { return 0; }
// MetagameRank::ComputeRankNumber(bool)
extern "C" __attribute__((weak, used)) long _stub_fn_556() __asm__("_ZN12MetagameRank17ComputeRankNumberEb");
extern "C" long _stub_fn_556() { return 0; }
// MetagameRank::SaveSize(int)
extern "C" __attribute__((weak, used)) long _stub_fn_557() __asm__("_ZN12MetagameRank8SaveSizeEi");
extern "C" long _stub_fn_557() { return 0; }
// MetaMaterial::IsEquivalent(MetaMaterial*)
extern "C" __attribute__((weak, used)) long _stub_fn_558() __asm__("_ZN12MetaMaterial12IsEquivalentEPS_");
extern "C" long _stub_fn_558() { return 0; }
// MeterDisplay::DrawShowing()
extern "C" __attribute__((weak, used)) long _stub_fn_559() __asm__("_ZN12MeterDisplay11DrawShowingEv");
extern "C" long _stub_fn_559() { return 0; }
// MoveDetector::Poll(int, int, MoveDir*)
extern "C" __attribute__((weak, used)) long _stub_fn_560() __asm__("_ZN12MoveDetector4PollEiiP7MoveDir");
extern "C" long _stub_fn_560() { return 0; }
// OptionsPanel::Poll()
extern "C" __attribute__((weak, used)) long _stub_fn_561() __asm__("_ZN12OptionsPanel4PollEv");
extern "C" long _stub_fn_561() { return 0; }
// PartyModeMgr::ResetModes(bool)
extern "C" __attribute__((weak, used)) long _stub_fn_562() __asm__("_ZN12PartyModeMgr10ResetModesEb");
extern "C" long _stub_fn_562() { return 0; }
// PartyModeMgr::ResetSongs()
extern "C" __attribute__((weak, used)) long _stub_fn_563() __asm__("_ZN12PartyModeMgr10ResetSongsEv");
extern "C" long _stub_fn_563() { return 0; }
// PartyModeMgr::FinalizeTeam(int)
extern "C" __attribute__((weak, used)) long _stub_fn_564() __asm__("_ZN12PartyModeMgr12FinalizeTeamEi");
extern "C" long _stub_fn_564() { return 0; }
// PartyModeMgr::GetCrewColor(int, int)
extern "C" __attribute__((weak, used)) long _stub_fn_565() __asm__("_ZN12PartyModeMgr12GetCrewColorEii");
extern "C" long _stub_fn_565() { return 0; }
// PartyModeMgr::PruneHistory()
extern "C" __attribute__((weak, used)) long _stub_fn_566() __asm__("_ZN12PartyModeMgr12PruneHistoryEv");
extern "C" long _stub_fn_566() { return 0; }
// PartyModeMgr::UpdateScores()
extern "C" __attribute__((weak, used)) long _stub_fn_567() __asm__("_ZN12PartyModeMgr12UpdateScoresEv");
extern "C" long _stub_fn_567() { return 0; }
// PartyModeMgr::FinalizeParty()
extern "C" __attribute__((weak, used)) long _stub_fn_568() __asm__("_ZN12PartyModeMgr13FinalizePartyEv");
extern "C" long _stub_fn_568() { return 0; }
// PartyModeMgr::OnSmartGlassListen(int)
extern "C" __attribute__((weak, used)) long _stub_fn_569() __asm__("_ZN12PartyModeMgr18OnSmartGlassListenEi");
extern "C" long _stub_fn_569() { return 0; }
// PartyModeMgr::ReadPartySongQueue()
extern "C" __attribute__((weak, used)) long _stub_fn_570() __asm__("_ZN12PartyModeMgr18ReadPartySongQueueEv");
extern "C" long _stub_fn_570() { return 0; }
// PartyModeMgr::OnSetSongAndDefaults(DataArray*)
extern "C" __attribute__((weak, used)) long _stub_fn_571() __asm__("_ZN12PartyModeMgr20OnSetSongAndDefaultsEP9DataArray");
extern "C" long _stub_fn_571() { return 0; }
// PartyModeMgr::SetSongsFromPlaylist()
extern "C" __attribute__((weak, used)) long _stub_fn_572() __asm__("_ZN12PartyModeMgr20SetSongsFromPlaylistEv");
extern "C" long _stub_fn_572() { return 0; }
// PartyModeMgr::ToggleIncludedModeOn(Symbol, bool)
extern "C" __attribute__((weak, used)) long _stub_fn_573() __asm__("_ZN12PartyModeMgr20ToggleIncludedModeOnE6Symbolb");
extern "C" long _stub_fn_573() { return 0; }
// PartyModeMgr::DetermineSubModePlayers(Symbol, int*, int*, std::vector<int, std::allocator<int> >*)
extern "C" __attribute__((weak, used)) long _stub_fn_574() __asm__("_ZN12PartyModeMgr23DetermineSubModePlayersE6SymbolPiS1_PSt6vectorIiSaIiEE");
extern "C" long _stub_fn_574() { return 0; }
// RndGenerator::DrawShowing()
extern "C" __attribute__((weak, used)) long _stub_fn_575() __asm__("_ZN12RndGenerator11DrawShowingEv");
extern "C" long _stub_fn_575() { return 0; }
// RndGenerator::MakeWorldSphere(Sphere&, bool)
extern "C" __attribute__((weak, used)) long _stub_fn_576() __asm__("_ZN12RndGenerator15MakeWorldSphereER6Sphereb");
extern "C" long _stub_fn_576() { return 0; }
// RndGenerator::Generate(float)
extern "C" __attribute__((weak, used)) long _stub_fn_577() __asm__("_ZN12RndGenerator8GenerateEf");
extern "C" long _stub_fn_577() { return 0; }
// RndGenerator::SetFrame(float, float)
extern "C" __attribute__((weak, used)) long _stub_fn_578() __asm__("_ZN12RndGenerator8SetFrameEff");
extern "C" long _stub_fn_578() { return 0; }
// SkeletonClip::PollRecording(SkeletonFrame const&)
extern "C" __attribute__((weak, used)) long _stub_fn_579() __asm__("_ZN12SkeletonClip13PollRecordingERK13SkeletonFrame");
extern "C" long _stub_fn_579() { return 0; }
// SkeletonClip::SwapMoveRecord()
extern "C" __attribute__((weak, used)) long _stub_fn_580() __asm__("_ZN12SkeletonClip14SwapMoveRecordEv");
extern "C" long _stub_fn_580() { return 0; }
// SkeletonClip::FillMoveRatings()
extern "C" __attribute__((weak, used)) long _stub_fn_581() __asm__("_ZN12SkeletonClip15FillMoveRatingsEv");
extern "C" long _stub_fn_581() { return 0; }
// SkeletonClip::RecordedFrameAt(std::vector<RecordedFrame, std::allocator<RecordedFrame> > const&, float, int&, int&)
extern "C" __attribute__((weak, used)) long _stub_fn_582() __asm__("_ZN12SkeletonClip15RecordedFrameAtERKSt6vectorI13RecordedFrameSaIS1_EEfRiS6_");
extern "C" long _stub_fn_582() { return 0; }
// SkeletonClip::LoadFrame(BinStream&, RecordedFrame&, int)
extern "C" __attribute__((weak, used)) long _stub_fn_583() __asm__("_ZN12SkeletonClip9LoadFrameER9BinStreamR13RecordedFramei");
extern "C" long _stub_fn_583() { return 0; }
// StoreEnumJob::OnCompletion(Hmx::Object*)
extern "C" __attribute__((weak, used)) long _stub_fn_584() __asm__("_ZN12StoreEnumJob12OnCompletionEPN3Hmx6ObjectE");
extern "C" long _stub_fn_584() { return 0; }
// SynthEmitter::Poll()
extern "C" __attribute__((weak, used)) long _stub_fn_585() __asm__("_ZN12SynthEmitter4PollEv");
extern "C" long _stub_fn_585() { return 0; }
// UIListWidget::Handle(DataArray*, bool)
extern "C" __attribute__((weak, used)) long _stub_fn_586() __asm__("_ZN12UIListWidget6HandleEP9DataArrayb");
extern "C" long _stub_fn_586() { return 0; }
// VorbisReader::DoFileRead()
extern "C" __attribute__((weak, used)) long _stub_fn_587() __asm__("_ZN12VorbisReader10DoFileReadEv");
extern "C" long _stub_fn_587() { return 0; }
// VorbisReader::Poll(float)
extern "C" __attribute__((weak, used)) long _stub_fn_588() __asm__("_ZN12VorbisReader4PollEf");
extern "C" long _stub_fn_588() { return 0; }
// AutoSlowFrame::AutoSlowFrame(char const*, float)
extern "C" __attribute__((weak, used)) long _stub_fn_589() __asm__("_ZN13AutoSlowFrameC1EPKcf");
extern "C" long _stub_fn_589() { return 0; }
// AutoSlowFrame::~AutoSlowFrame()
extern "C" __attribute__((weak, used)) long _stub_fn_590() __asm__("_ZN13AutoSlowFrameD1Ev");
extern "C" long _stub_fn_590() { return 0; }
// BinkMovieImpl::LockThread()
extern "C" __attribute__((weak, used)) long _stub_fn_591() __asm__("_ZN13BinkMovieImpl10LockThreadEv");
extern "C" long _stub_fn_591() { return 0; }
// BinkMovieImpl::UnlockThread()
extern "C" __attribute__((weak, used)) long _stub_fn_592() __asm__("_ZN13BinkMovieImpl12UnlockThreadEv");
extern "C" long _stub_fn_592() { return 0; }
// BinkMovieImpl::BeginFromFile(char const*, float, bool, bool, bool, bool, int, BinStream*, LoaderPos)
extern "C" __attribute__((weak, used)) long _stub_fn_593() __asm__("_ZN13BinkMovieImpl13BeginFromFileEPKcfbbbbiP9BinStream9LoaderPos");
extern "C" long _stub_fn_593() { return 0; }
// BinkMovieImpl::SetWidthHeight(int, int)
extern "C" __attribute__((weak, used)) long _stub_fn_594() __asm__("_ZN13BinkMovieImpl14SetWidthHeightEii");
extern "C" long _stub_fn_594() { return 0; }
// BinkMovieImpl::End()
extern "C" __attribute__((weak, used)) long _stub_fn_595() __asm__("_ZN13BinkMovieImpl3EndEv");
extern "C" long _stub_fn_595() { return 0; }
// BinkMovieImpl::Draw()
extern "C" __attribute__((weak, used)) long _stub_fn_596() __asm__("_ZN13BinkMovieImpl4DrawEv");
extern "C" long _stub_fn_596() { return 0; }
// BinkMovieImpl::Poll()
extern "C" __attribute__((weak, used)) long _stub_fn_597() __asm__("_ZN13BinkMovieImpl4PollEv");
extern "C" long _stub_fn_597() { return 0; }
// BinkMovieImpl::Save(BinStream*)
extern "C" __attribute__((weak, used)) long _stub_fn_598() __asm__("_ZN13BinkMovieImpl4SaveEP9BinStream");
extern "C" long _stub_fn_598() { return 0; }
// BinkMovieImpl::CheckOpen(bool)
extern "C" __attribute__((weak, used)) long _stub_fn_599() __asm__("_ZN13BinkMovieImpl9CheckOpenEb");
extern "C" long _stub_fn_599() { return 0; }
// BinkMovieImpl::SetPaused(bool)
extern "C" __attribute__((weak, used)) long _stub_fn_600() __asm__("_ZN13BinkMovieImpl9SetPausedEb");
extern "C" long _stub_fn_600() { return 0; }
// BinkMovieImpl::SetVolume(float)
extern "C" __attribute__((weak, used)) long _stub_fn_601() __asm__("_ZN13BinkMovieImpl9SetVolumeEf");
extern "C" long _stub_fn_601() { return 0; }
// BinkMovieImpl::Terminate()
extern "C" __attribute__((weak, used)) long _stub_fn_602() __asm__("_ZN13BinkMovieImpl9TerminateEv");
extern "C" long _stub_fn_602() { return 0; }
// BustAMoveData::Handle(DataArray*, bool)
extern "C" __attribute__((weak, used)) long _stub_fn_603() __asm__("_ZN13BustAMoveData6HandleEP9DataArrayb");
extern "C" long _stub_fn_603() { return 0; }
// CameraManager::Poll()
extern "C" __attribute__((weak, used)) long _stub_fn_604() __asm__("_ZN13CameraManager4PollEv");
extern "C" long _stub_fn_604() { return 0; }
// ChallengeSort::BuildTree()
extern "C" __attribute__((weak, used)) long _stub_fn_605() __asm__("_ZN13ChallengeSort9BuildTreeEv");
extern "C" long _stub_fn_605() { return 0; }
// CharClipGroup::SetClipFlags(int)
extern "C" __attribute__((weak, used)) long _stub_fn_606() __asm__("_ZN13CharClipGroup12SetClipFlagsEi");
extern "C" long _stub_fn_606() { return 0; }
// CharClipGroup::DeleteRemaining(int)
extern "C" __attribute__((weak, used)) long _stub_fn_607() __asm__("_ZN13CharClipGroup15DeleteRemainingEi");
extern "C" long _stub_fn_607() { return 0; }
// CharForeTwist::Poll()
extern "C" __attribute__((weak, used)) long _stub_fn_608() __asm__("_ZN13CharForeTwist4PollEv");
extern "C" long _stub_fn_608() { return 0; }
// CharIKFingers::MoveFinger(CharIKFingers::FingerNum)
extern "C" __attribute__((weak, used)) long _stub_fn_609() __asm__("_ZN13CharIKFingers10MoveFingerENS_9FingerNumE");
extern "C" long _stub_fn_609() { return 0; }
// CharIKFingers::MeasureLengths()
extern "C" __attribute__((weak, used)) long _stub_fn_610() __asm__("_ZN13CharIKFingers14MeasureLengthsEv");
extern "C" long _stub_fn_610() { return 0; }
// CharIKFingers::FixSingleFinger(RndTransformable*, RndTransformable*, RndTransformable*)
extern "C" __attribute__((weak, used)) long _stub_fn_611() __asm__("_ZN13CharIKFingers15FixSingleFingerEP16RndTransformableS1_S1_");
extern "C" long _stub_fn_611() { return 0; }
// CharIKFingers::CalculateHandDest(int, int)
extern "C" __attribute__((weak, used)) long _stub_fn_612() __asm__("_ZN13CharIKFingers17CalculateHandDestEii");
extern "C" long _stub_fn_612() { return 0; }
// CharIKFingers::CalculateFingerDest(CharIKFingers::FingerNum)
extern "C" __attribute__((weak, used)) long _stub_fn_613() __asm__("_ZN13CharIKFingers19CalculateFingerDestENS_9FingerNumE");
extern "C" long _stub_fn_613() { return 0; }
// CharNeckTwist::Poll()
extern "C" __attribute__((weak, used)) long _stub_fn_614() __asm__("_ZN13CharNeckTwist4PollEv");
extern "C" long _stub_fn_614() { return 0; }
// CharServoBone::PollDeps(std::__cxx11::list<Hmx::Object*, std::allocator<Hmx::Object*> >&, std::__cxx11::list<Hmx::Object*, std::allocator<Hmx::Object*> >&)
extern "C" __attribute__((weak, used)) long _stub_fn_615() __asm__("_ZN13CharServoBone8PollDepsERNSt7__cxx114listIPN3Hmx6ObjectESaIS4_EEES7_");
extern "C" long _stub_fn_615() { return 0; }
// CheatsManager::OnMsg(ButtonDownMsg const&)
extern "C" __attribute__((weak, used)) long _stub_fn_616() __asm__("_ZN13CheatsManager5OnMsgERK13ButtonDownMsg");
extern "C" long _stub_fn_616() { return 0; }
// CheatsManager::OnMsg(KeyboardKeyMsg const&)
extern "C" __attribute__((weak, used)) long _stub_fn_617() __asm__("_ZN13CheatsManager5OnMsgERK14KeyboardKeyMsg");
extern "C" long _stub_fn_617() { return 0; }
// DepthBuffer3D::DrawShowing()
extern "C" __attribute__((weak, used)) long _stub_fn_618() __asm__("_ZN13DepthBuffer3D11DrawShowingEv");
extern "C" long _stub_fn_618() { return 0; }
// DepthBuffer3D::ListDrawChildren(std::__cxx11::list<RndDrawable*, std::allocator<RndDrawable*> >&)
extern "C" __attribute__((weak, used)) long _stub_fn_619() __asm__("_ZN13DepthBuffer3D16ListDrawChildrenERNSt7__cxx114listIP11RndDrawableSaIS3_EEE");
extern "C" long _stub_fn_619() { return 0; }
// DepthBuffer3D::Copy(Hmx::Object const*, Hmx::Object::CopyType)
extern "C" __attribute__((weak, used)) long _stub_fn_620() __asm__("_ZN13DepthBuffer3D4CopyEPKN3Hmx6ObjectENS1_8CopyTypeE");
extern "C" long _stub_fn_620() { return 0; }
// DepthBuffer3D::Load(BinStream&)
extern "C" __attribute__((weak, used)) long _stub_fn_621() __asm__("_ZN13DepthBuffer3D4LoadER9BinStream");
extern "C" long _stub_fn_621() { return 0; }
// DepthBuffer3D::Save(BinStream&)
extern "C" __attribute__((weak, used)) long _stub_fn_622() __asm__("_ZN13DepthBuffer3D4SaveER9BinStream");
extern "C" long _stub_fn_622() { return 0; }
// FlowQueueable::Deactivate(bool)
extern "C" __attribute__((weak, used)) long _stub_fn_623() __asm__("_ZN13FlowQueueable10DeactivateEb");
extern "C" long _stub_fn_623() { return 0; }
// FlowQueueable::ChildFinished(FlowNode*)
extern "C" __attribute__((weak, used)) long _stub_fn_624() __asm__("_ZN13FlowQueueable13ChildFinishedEP8FlowNode");
extern "C" long _stub_fn_624() { return 0; }
// FlowQueueable::Activate(Hmx::Object*)
extern "C" __attribute__((weak, used)) long _stub_fn_625() __asm__("_ZN13FlowQueueable8ActivateEPN3Hmx6ObjectE");
extern "C" long _stub_fn_625() { return 0; }
// HamIKEffector::ComputeHandPullAndQuat(QuatXfm&, Transform&, Transform const&, Vector3 const&)
extern "C" __attribute__((weak, used)) long _stub_fn_626() __asm__("_ZN13HamIKEffector22ComputeHandPullAndQuatER7QuatXfmR9TransformRKS2_RK7Vector3");
extern "C" long _stub_fn_626() { return 0; }
// HamIKEffector::Poll()
extern "C" __attribute__((weak, used)) long _stub_fn_627() __asm__("_ZN13HamIKEffector4PollEv");
extern "C" long _stub_fn_627() { return 0; }
// HamListRibbon::Draw(Transform const&, std::vector<HamListRibbonDrawState, std::allocator<HamListRibbonDrawState> > const&, bool, bool)
extern "C" __attribute__((weak, used)) long _stub_fn_628() __asm__("_ZN13HamListRibbon4DrawERK9TransformRKSt6vectorI22HamListRibbonDrawStateSaIS4_EEbb");
extern "C" long _stub_fn_628() { return 0; }
// HamListRibbon::EndFrame()
extern "C" __attribute__((weak, used)) long _stub_fn_629() __asm__("_ZN13HamListRibbon8EndFrameEv");
extern "C" long _stub_fn_629() { return 0; }
// HamStorePanel::UpdateOffers(std::__cxx11::list<EnumProduct, std::allocator<EnumProduct> > const&, bool)
extern "C" __attribute__((weak, used)) long _stub_fn_630() __asm__("_ZN13HamStorePanel12UpdateOffersERKNSt7__cxx114listI11EnumProductSaIS2_EEEb");
extern "C" long _stub_fn_630() { return 0; }
// HamStorePanel::ContentMounted(char const*, char const*)
extern "C" __attribute__((weak, used)) long _stub_fn_631() __asm__("_ZN13HamStorePanel14ContentMountedEPKcS1_");
extern "C" long _stub_fn_631() { return 0; }
// HamStorePanel::BuySpecialOffer(Symbol)
extern "C" __attribute__((weak, used)) long _stub_fn_632() __asm__("_ZN13HamStorePanel15BuySpecialOfferE6Symbol");
extern "C" long _stub_fn_632() { return 0; }
// HamStorePanel::ContentDiscovered(Symbol)
extern "C" __attribute__((weak, used)) long _stub_fn_633() __asm__("_ZN13HamStorePanel17ContentDiscoveredE6Symbol");
extern "C" long _stub_fn_633() { return 0; }
// HamStorePanel::ContentTitleDiscovered(unsigned int, Symbol)
extern "C" __attribute__((weak, used)) long _stub_fn_634() __asm__("_ZN13HamStorePanel22ContentTitleDiscoveredEj6Symbol");
extern "C" long _stub_fn_634() { return 0; }
// HamStorePanel::RefreshSpecialOfferStatus()
extern "C" __attribute__((weak, used)) long _stub_fn_635() __asm__("_ZN13HamStorePanel25RefreshSpecialOfferStatusEv");
extern "C" long _stub_fn_635() { return 0; }
// HamStorePanel::Poll()
extern "C" __attribute__((weak, used)) long _stub_fn_636() __asm__("_ZN13HamStorePanel4PollEv");
extern "C" long _stub_fn_636() { return 0; }
// HamStorePanel::OnMsg(RCJobCompleteMsg const&)
extern "C" __attribute__((weak, used)) long _stub_fn_637() __asm__("_ZN13HamStorePanel5OnMsgERK16RCJobCompleteMsg");
extern "C" long _stub_fn_637() { return 0; }
// HamStorePanel::Unload()
extern "C" __attribute__((weak, used)) long _stub_fn_638() __asm__("_ZN13HamStorePanel6UnloadEv");
extern "C" long _stub_fn_638() { return 0; }
// MainMenuPanel::LoadArt(String)
extern "C" __attribute__((weak, used)) long _stub_fn_639() __asm__("_ZN13MainMenuPanel7LoadArtE6String");
extern "C" long _stub_fn_639() { return 0; }
// NetLoaderXbox::NetLoaderXbox(String const&)
extern "C" __attribute__((weak, used)) long _stub_fn_640() __asm__("_ZN13NetLoaderXboxC1ERK6String");
extern "C" long _stub_fn_640() { return 0; }
// NetworkSocket::IPIntToString(unsigned int)
extern "C" __attribute__((weak, used)) long _stub_fn_641() __asm__("_ZN13NetworkSocket13IPIntToStringEj");
extern "C" long _stub_fn_641() { return 0; }
// NetworkSocket::IPStringToInt(String const&)
extern "C" __attribute__((weak, used)) long _stub_fn_642() __asm__("_ZN13NetworkSocket13IPStringToIntERK6String");
extern "C" long _stub_fn_642() { return 0; }
// NetworkSocket::~NetworkSocket()
extern "C" __attribute__((weak, used)) long _stub_fn_643() __asm__("_ZN13NetworkSocketD2Ev");
extern "C" long _stub_fn_643() { return 0; }
// PhysicsVolume::DrawShowing()
extern "C" __attribute__((weak, used)) long _stub_fn_644() __asm__("_ZN13PhysicsVolume11DrawShowingEv");
extern "C" long _stub_fn_644() { return 0; }
// PhysicsVolume::Load(BinStream&)
extern "C" __attribute__((weak, used)) long _stub_fn_645() __asm__("_ZN13PhysicsVolume4LoadER9BinStream");
extern "C" long _stub_fn_645() { return 0; }
// RndAnimFilter::OnSafeAnims(DataArray*)
extern "C" __attribute__((weak, used)) long _stub_fn_646() __asm__("_ZN13RndAnimFilter11OnSafeAnimsEP9DataArray");
extern "C" long _stub_fn_646() { return 0; }
// RndAnimFilter::SetFrame(float, float)
extern "C" __attribute__((weak, used)) long _stub_fn_647() __asm__("_ZN13RndAnimFilter8SetFrameEff");
extern "C" long _stub_fn_647() { return 0; }
// RndMeshDeform::VertArray::AppendWeights(int, int*, float*)
extern "C" __attribute__((weak, used)) long _stub_fn_648() __asm__("_ZN13RndMeshDeform9VertArray13AppendWeightsEiPiPf");
extern "C" long _stub_fn_648() { return 0; }

// RndTexBlender::DrawShowing()
extern "C" __attribute__((weak, used)) long _stub_fn_651() __asm__("_ZN13RndTexBlender11DrawShowingEv");
extern "C" long _stub_fn_651() { return 0; }
// SongCollision::Equals(SongCollision*)
extern "C" __attribute__((weak, used)) long _stub_fn_652() __asm__("_ZN13SongCollision6EqualsEPS_");
extern "C" long _stub_fn_652() { return 0; }
// WorldInstance::SavePersistentObjects(BinStream&)
extern "C" __attribute__((weak, used)) long _stub_fn_653() __asm__("_ZN13WorldInstance21SavePersistentObjectsER9BinStream");
extern "C" long _stub_fn_653() { return 0; }
// WorldInstance::Load(BinStream&)
extern "C" __attribute__((weak, used)) long _stub_fn_654() __asm__("_ZN13WorldInstance4LoadER9BinStream");
extern "C" long _stub_fn_654() { return 0; }
// WorldInstance::PreSave(BinStream&)
extern "C" __attribute__((weak, used)) long _stub_fn_655() __asm__("_ZN13WorldInstance7PreSaveER9BinStream");
extern "C" long _stub_fn_655() { return 0; }
// WorldInstance::SyncDir()
extern "C" __attribute__((weak, used)) long _stub_fn_656() __asm__("_ZN13WorldInstance7SyncDirEv");
extern "C" long _stub_fn_656() { return 0; }
// WorldInstance::PostLoad(BinStream&)
extern "C" __attribute__((weak, used)) long _stub_fn_657() __asm__("_ZN13WorldInstance8PostLoadER9BinStream");
extern "C" long _stub_fn_657() { return 0; }
// AddPlaylistJob::GetPlaylistID(CustomPlaylist*)
extern "C" __attribute__((weak, used)) long _stub_fn_658() __asm__("_ZN14AddPlaylistJob13GetPlaylistIDEP14CustomPlaylist");
extern "C" long _stub_fn_658() { return 0; }
// CharClipDriver::ExecuteEvent(Symbol)
extern "C" __attribute__((weak, used)) long _stub_fn_659() __asm__("_ZN14CharClipDriver12ExecuteEventE6Symbol");
extern "C" long _stub_fn_659() { return 0; }
// CharClipDriver::SetBeatOffset(float, TaskUnits, Symbol)
extern "C" __attribute__((weak, used)) long _stub_fn_660() __asm__("_ZN14CharClipDriver13SetBeatOffsetEf9TaskUnits6Symbol");
extern "C" long _stub_fn_660() { return 0; }
// FitnessGoalMgr::IsProfileChanged()
extern "C" __attribute__((weak, used)) long _stub_fn_661() __asm__("_ZN14FitnessGoalMgr16IsProfileChangedEv");
extern "C" long _stub_fn_661() { return 0; }
// FitnessGoalMgr::AddPendingProfile(HamProfile*)
extern "C" __attribute__((weak, used)) long _stub_fn_662() __asm__("_ZN14FitnessGoalMgr17AddPendingProfileEP10HamProfile");
extern "C" long _stub_fn_662() { return 0; }
// FitnessGoalMgr::OnSmartGlassListen(int)
extern "C" __attribute__((weak, used)) long _stub_fn_663() __asm__("_ZN14FitnessGoalMgr18OnSmartGlassListenEi");
extern "C" long _stub_fn_663() { return 0; }
// FitnessGoalMgr::ProcessNextCommand()
extern "C" __attribute__((weak, used)) long _stub_fn_664() __asm__("_ZN14FitnessGoalMgr18ProcessNextCommandEv");
extern "C" long _stub_fn_664() { return 0; }
// FitnessGoalMgr::OnMsg(RCJobCompleteMsg const&)
extern "C" __attribute__((weak, used)) long _stub_fn_665() __asm__("_ZN14FitnessGoalMgr5OnMsgERK16RCJobCompleteMsg");
extern "C" long _stub_fn_665() { return 0; }
// FlowSwitchCase::IsValidCase(FlowNode*, DataNode*, DataNode const*, bool)
extern "C" __attribute__((weak, used)) long _stub_fn_666() __asm__("_ZN14FlowSwitchCase11IsValidCaseEP8FlowNodeP8DataNodePKS2_b");
extern "C" long _stub_fn_666() { return 0; }
// FxSendBitCrush::Handle(DataArray*, bool)
extern "C" __attribute__((weak, used)) long _stub_fn_667() __asm__("_ZN14FxSendBitCrush6HandleEP9DataArrayb");
extern "C" long _stub_fn_667() { return 0; }
// HamNavProvider::OnSetFormatArgs(DataArray const*)
extern "C" __attribute__((weak, used)) long _stub_fn_668() __asm__("_ZN14HamNavProvider15OnSetFormatArgsEPK9DataArray");
extern "C" long _stub_fn_668() { return 0; }
// KinectShareJob::KinectShareJob(Hmx::Object*)
extern "C" __attribute__((weak, used)) long _stub_fn_669() __asm__("_ZN14KinectShareJobC1EPN3Hmx6ObjectE");
extern "C" long _stub_fn_669() { return 0; }
// MidiInstrument::SynthPoll()
extern "C" __attribute__((weak, used)) long _stub_fn_670() __asm__("_ZN14MidiInstrument9SynthPollEv");
extern "C" long _stub_fn_670() { return 0; }
// NavListSortMgr::SelectionIs(Symbol)
extern "C" __attribute__((weak, used)) long _stub_fn_671() __asm__("_ZN14NavListSortMgr11SelectionIsE6Symbol");
extern "C" long _stub_fn_671() { return 0; }
// NavListSortMgr::HeadersSelectable()
extern "C" __attribute__((weak, used)) long _stub_fn_672() __asm__("_ZN14NavListSortMgr17HeadersSelectableEv");
extern "C" long _stub_fn_672() { return 0; }
// NavListSortMgr::DataIs(int, Symbol)
extern "C" __attribute__((weak, used)) long _stub_fn_673() __asm__("_ZN14NavListSortMgr6DataIsEi6Symbol");
extern "C" long _stub_fn_673() { return 0; }
// NavListSortMgr::MoveOn()
extern "C" __attribute__((weak, used)) long _stub_fn_674() __asm__("_ZN14NavListSortMgr6MoveOnEv");
extern "C" long _stub_fn_674() { return 0; }
// NavListSortMgr::OnEnter()
extern "C" __attribute__((weak, used)) long _stub_fn_675() __asm__("_ZN14NavListSortMgr7OnEnterEv");
extern "C" long _stub_fn_675() { return 0; }
// NetCacheLoader::SetState(NetCacheLoader::State)
extern "C" __attribute__((weak, used)) long _stub_fn_676() __asm__("_ZN14NetCacheLoader8SetStateENS_5StateE");
extern "C" long _stub_fn_676() { return 0; }
// ObjRefConcrete<CharDriver, ObjectDir>::CopyRef(ObjRefConcrete<CharDriver, ObjectDir> const&)
extern "C" __attribute__((weak, used)) long _stub_fn_677() __asm__("_ZN14ObjRefConcreteI10CharDriver9ObjectDirE7CopyRefERKS2_");
extern "C" long _stub_fn_677() { return 0; }
// ObjRefConcrete<CharIKFoot, ObjectDir>::CopyRef(ObjRefConcrete<CharIKFoot, ObjectDir> const&)
extern "C" __attribute__((weak, used)) long _stub_fn_678() __asm__("_ZN14ObjRefConcreteI10CharIKFoot9ObjectDirE7CopyRefERKS2_");
extern "C" long _stub_fn_678() { return 0; }
// ObjRefConcrete<CharLookAt, ObjectDir>::CopyRef(ObjRefConcrete<CharLookAt, ObjectDir> const&)
extern "C" __attribute__((weak, used)) long _stub_fn_679() __asm__("_ZN14ObjRefConcreteI10CharLookAt9ObjectDirE7CopyRefERKS2_");
extern "C" long _stub_fn_679() { return 0; }
// ObjRefConcrete<HamCamShot, ObjectDir>::CopyRef(ObjRefConcrete<HamCamShot, ObjectDir> const&)
extern "C" __attribute__((weak, used)) long _stub_fn_680() __asm__("_ZN14ObjRefConcreteI10HamCamShot9ObjectDirE7CopyRefERKS2_");
extern "C" long _stub_fn_680() { return 0; }
// ObjRefConcrete<RndCubeTex, ObjectDir>::CopyRef(ObjRefConcrete<RndCubeTex, ObjectDir> const&)
extern "C" __attribute__((weak, used)) long _stub_fn_681() __asm__("_ZN14ObjRefConcreteI10RndCubeTex9ObjectDirE7CopyRefERKS2_");
extern "C" long _stub_fn_681() { return 0; }
// ObjRefConcrete<RndEnviron, ObjectDir>::CopyRef(ObjRefConcrete<RndEnviron, ObjectDir> const&)
extern "C" __attribute__((weak, used)) long _stub_fn_682() __asm__("_ZN14ObjRefConcreteI10RndEnviron9ObjectDirE7CopyRefERKS2_");
extern "C" long _stub_fn_682() { return 0; }
// ObjRefConcrete<UILabelDir, ObjectDir>::CopyRef(ObjRefConcrete<UILabelDir, ObjectDir> const&)
extern "C" __attribute__((weak, used)) long _stub_fn_683() __asm__("_ZN14ObjRefConcreteI10UILabelDir9ObjectDirE7CopyRefERKS2_");
extern "C" long _stub_fn_683() { return 0; }
// ObjRefConcrete<WorldCrowd, ObjectDir>::CopyRef(ObjRefConcrete<WorldCrowd, ObjectDir> const&)
extern "C" __attribute__((weak, used)) long _stub_fn_684() __asm__("_ZN14ObjRefConcreteI10WorldCrowd9ObjectDirE7CopyRefERKS2_");
extern "C" long _stub_fn_684() { return 0; }
// ObjRefConcrete<CharCollide, ObjectDir>::CopyRef(ObjRefConcrete<CharCollide, ObjectDir> const&)
extern "C" __attribute__((weak, used)) long _stub_fn_685() __asm__("_ZN14ObjRefConcreteI11CharCollide9ObjectDirE7CopyRefERKS2_");
extern "C" long _stub_fn_685() { return 0; }
// ObjRefConcrete<CharLipSync, ObjectDir>::CopyRef(ObjRefConcrete<CharLipSync, ObjectDir> const&)
extern "C" __attribute__((weak, used)) long _stub_fn_686() __asm__("_ZN14ObjRefConcreteI11CharLipSync9ObjectDirE7CopyRefERKS2_");
extern "C" long _stub_fn_686() { return 0; }
// ObjRefConcrete<LightPreset, ObjectDir>::CopyRef(ObjRefConcrete<LightPreset, ObjectDir> const&)
extern "C" __attribute__((weak, used)) long _stub_fn_687() __asm__("_ZN14ObjRefConcreteI11LightPreset9ObjectDirE7CopyRefERKS2_");
extern "C" long _stub_fn_687() { return 0; }
// ObjRefConcrete<RndDrawable, ObjectDir>::CopyRef(ObjRefConcrete<RndDrawable, ObjectDir> const&)
extern "C" __attribute__((weak, used)) long _stub_fn_688() __asm__("_ZN14ObjRefConcreteI11RndDrawable9ObjectDirE7CopyRefERKS2_");
extern "C" long _stub_fn_688() { return 0; }
// ObjRefConcrete<RndFontBase, ObjectDir>::CopyRef(ObjRefConcrete<RndFontBase, ObjectDir> const&)
extern "C" __attribute__((weak, used)) long _stub_fn_689() __asm__("_ZN14ObjRefConcreteI11RndFontBase9ObjectDirE7CopyRefERKS2_");
extern "C" long _stub_fn_689() { return 0; }
// ObjRefConcrete<RndPostProc, ObjectDir>::CopyRef(ObjRefConcrete<RndPostProc, ObjectDir> const&)
extern "C" __attribute__((weak, used)) long _stub_fn_690() __asm__("_ZN14ObjRefConcreteI11RndPostProc9ObjectDirE7CopyRefERKS2_");
extern "C" long _stub_fn_690() { return 0; }
// ObjRefConcrete<RndPropAnim, ObjectDir>::CopyRef(ObjRefConcrete<RndPropAnim, ObjectDir> const&)
extern "C" __attribute__((weak, used)) long _stub_fn_691() __asm__("_ZN14ObjRefConcreteI11RndPropAnim9ObjectDirE7CopyRefERKS2_");
extern "C" long _stub_fn_691() { return 0; }
// ObjRefConcrete<SynthSample, ObjectDir>::CopyRef(ObjRefConcrete<SynthSample, ObjectDir> const&)
extern "C" __attribute__((weak, used)) long _stub_fn_692() __asm__("_ZN14ObjRefConcreteI11SynthSample9ObjectDirE7CopyRefERKS2_");
extern "C" long _stub_fn_692() { return 0; }
// ObjRefConcrete<UIComponent, ObjectDir>::CopyRef(ObjRefConcrete<UIComponent, ObjectDir> const&)
extern "C" __attribute__((weak, used)) long _stub_fn_693() __asm__("_ZN14ObjRefConcreteI11UIComponent9ObjectDirE7CopyRefERKS2_");
extern "C" long _stub_fn_693() { return 0; }
// ObjRefConcrete<BaseMaterial, ObjectDir>::CopyRef(ObjRefConcrete<BaseMaterial, ObjectDir> const&)
extern "C" __attribute__((weak, used)) long _stub_fn_694() __asm__("_ZN14ObjRefConcreteI12BaseMaterial9ObjectDirE7CopyRefERKS2_");
extern "C" long _stub_fn_694() { return 0; }
// ObjRefConcrete<CharPollable, ObjectDir>::CopyRef(ObjRefConcrete<CharPollable, ObjectDir> const&)
extern "C" __attribute__((weak, used)) long _stub_fn_695() __asm__("_ZN14ObjRefConcreteI12CharPollable9ObjectDirE7CopyRefERKS2_");
extern "C" long _stub_fn_695() { return 0; }
// ObjRefConcrete<EventTrigger, ObjectDir>::CopyRef(ObjRefConcrete<EventTrigger, ObjectDir> const&)
extern "C" __attribute__((weak, used)) long _stub_fn_696() __asm__("_ZN14ObjRefConcreteI12EventTrigger9ObjectDirE7CopyRefERKS2_");
extern "C" long _stub_fn_696() { return 0; }
// ObjRefConcrete<MetaMaterial, ObjectDir>::CopyRef(ObjRefConcrete<MetaMaterial, ObjectDir> const&)
extern "C" __attribute__((weak, used)) long _stub_fn_697() __asm__("_ZN14ObjRefConcreteI12MetaMaterial9ObjectDirE7CopyRefERKS2_");
extern "C" long _stub_fn_697() { return 0; }
// ObjRefConcrete<RndMultiMesh, ObjectDir>::CopyRef(ObjRefConcrete<RndMultiMesh, ObjectDir> const&)
extern "C" __attribute__((weak, used)) long _stub_fn_698() __asm__("_ZN14ObjRefConcreteI12RndMultiMesh9ObjectDirE7CopyRefERKS2_");
extern "C" long _stub_fn_698() { return 0; }
// ObjRefConcrete<RndTransAnim, ObjectDir>::CopyRef(ObjRefConcrete<RndTransAnim, ObjectDir> const&)
extern "C" __attribute__((weak, used)) long _stub_fn_699() __asm__("_ZN14ObjRefConcreteI12RndTransAnim9ObjectDirE7CopyRefERKS2_");
extern "C" long _stub_fn_699() { return 0; }
// ObjRefConcrete<SkeletonClip, ObjectDir>::CopyRef(ObjRefConcrete<SkeletonClip, ObjectDir> const&)
extern "C" __attribute__((weak, used)) long _stub_fn_700() __asm__("_ZN14ObjRefConcreteI12SkeletonClip9ObjectDirE7CopyRefERKS2_");
extern "C" long _stub_fn_700() { return 0; }
// ObjRefConcrete<CharFaceServo, ObjectDir>::CopyRef(ObjRefConcrete<CharFaceServo, ObjectDir> const&)
extern "C" __attribute__((weak, used)) long _stub_fn_701() __asm__("_ZN14ObjRefConcreteI13CharFaceServo9ObjectDirE7CopyRefERKS2_");
extern "C" long _stub_fn_701() { return 0; }
// ObjRefConcrete<HamIKEffector, ObjectDir>::CopyRef(ObjRefConcrete<HamIKEffector, ObjectDir> const&)
extern "C" __attribute__((weak, used)) long _stub_fn_702() __asm__("_ZN14ObjRefConcreteI13HamIKEffector9ObjectDirE7CopyRefERKS2_");
extern "C" long _stub_fn_702() { return 0; }
// ObjRefConcrete<HamIKSkeleton, ObjectDir>::CopyRef(ObjRefConcrete<HamIKSkeleton, ObjectDir> const&)
extern "C" __attribute__((weak, used)) long _stub_fn_703() __asm__("_ZN14ObjRefConcreteI13HamIKSkeleton9ObjectDirE7CopyRefERKS2_");
extern "C" long _stub_fn_703() { return 0; }
// ObjRefConcrete<RndAnimatable, ObjectDir>::CopyRef(ObjRefConcrete<RndAnimatable, ObjectDir> const&)
extern "C" __attribute__((weak, used)) long _stub_fn_704() __asm__("_ZN14ObjRefConcreteI13RndAnimatable9ObjectDirE7CopyRefERKS2_");
extern "C" long _stub_fn_704() { return 0; }
// ObjRefConcrete<CharWeightable, ObjectDir>::CopyRef(ObjRefConcrete<CharWeightable, ObjectDir> const&)
extern "C" __attribute__((weak, used)) long _stub_fn_705() __asm__("_ZN14ObjRefConcreteI14CharWeightable9ObjectDirE7CopyRefERKS2_");
extern "C" long _stub_fn_705() { return 0; }
// ObjRefConcrete<DancerSequence, ObjectDir>::CopyRef(ObjRefConcrete<DancerSequence, ObjectDir> const&)
extern "C" __attribute__((weak, used)) long _stub_fn_706() __asm__("_ZN14ObjRefConcreteI14DancerSequence9ObjectDirE7CopyRefERKS2_");
extern "C" long _stub_fn_706() { return 0; }
// ObjRefConcrete<HamNavProvider, ObjectDir>::CopyRef(ObjRefConcrete<HamNavProvider, ObjectDir> const&)
extern "C" __attribute__((weak, used)) long _stub_fn_707() __asm__("_ZN14ObjRefConcreteI14HamNavProvider9ObjectDirE7CopyRefERKS2_");
extern "C" long _stub_fn_707() { return 0; }
// ObjRefConcrete<HamPhraseMeter, ObjectDir>::CopyRef(ObjRefConcrete<HamPhraseMeter, ObjectDir> const&)
extern "C" __attribute__((weak, used)) long _stub_fn_708() __asm__("_ZN14ObjRefConcreteI14HamPhraseMeter9ObjectDirE7CopyRefERKS2_");
extern "C" long _stub_fn_708() { return 0; }
// ObjRefConcrete<RndParticleSys, ObjectDir>::CopyRef(ObjRefConcrete<RndParticleSys, ObjectDir> const&)
extern "C" __attribute__((weak, used)) long _stub_fn_709() __asm__("_ZN14ObjRefConcreteI14RndParticleSys9ObjectDirE7CopyRefERKS2_");
extern "C" long _stub_fn_709() { return 0; }
// ObjRefConcrete<CharBonesObject, ObjectDir>::CopyRef(ObjRefConcrete<CharBonesObject, ObjectDir> const&)
extern "C" __attribute__((weak, used)) long _stub_fn_710() __asm__("_ZN14ObjRefConcreteI15CharBonesObject9ObjectDirE7CopyRefERKS2_");
extern "C" long _stub_fn_710() { return 0; }
// ObjRefConcrete<CharWeightSetter, ObjectDir>::CopyRef(ObjRefConcrete<CharWeightSetter, ObjectDir> const&)
extern "C" __attribute__((weak, used)) long _stub_fn_711() __asm__("_ZN14ObjRefConcreteI16CharWeightSetter9ObjectDirE7CopyRefERKS2_");
extern "C" long _stub_fn_711() { return 0; }
// ObjRefConcrete<RndTransformable, ObjectDir>::CopyRef(ObjRefConcrete<RndTransformable, ObjectDir> const&)
extern "C" __attribute__((weak, used)) long _stub_fn_712() __asm__("_ZN14ObjRefConcreteI16RndTransformable9ObjectDirE7CopyRefERKS2_");
extern "C" long _stub_fn_712() { return 0; }
// ObjRefConcrete<CharLipSyncDriver, ObjectDir>::CopyRef(ObjRefConcrete<CharLipSyncDriver, ObjectDir> const&)
extern "C" __attribute__((weak, used)) long _stub_fn_713() __asm__("_ZN14ObjRefConcreteI17CharLipSyncDriver9ObjectDirE7CopyRefERKS2_");
extern "C" long _stub_fn_713() { return 0; }
// ObjRefConcrete<FxSendMeterEffect, ObjectDir>::CopyRef(ObjRefConcrete<FxSendMeterEffect, ObjectDir> const&)
extern "C" __attribute__((weak, used)) long _stub_fn_714() __asm__("_ZN14ObjRefConcreteI17FxSendMeterEffect9ObjectDirE7CopyRefERKS2_");
extern "C" long _stub_fn_714() { return 0; }
// ObjRefConcrete<CharEyeDartRuleset, ObjectDir>::CopyRef(ObjRefConcrete<CharEyeDartRuleset, ObjectDir> const&)
extern "C" __attribute__((weak, used)) long _stub_fn_715() __asm__("_ZN14ObjRefConcreteI18CharEyeDartRuleset9ObjectDirE7CopyRefERKS2_");
extern "C" long _stub_fn_715() { return 0; }
// ObjRefConcrete<RhythmBattlePlayer, ObjectDir>::CopyRef(ObjRefConcrete<RhythmBattlePlayer, ObjectDir> const&)
extern "C" __attribute__((weak, used)) long _stub_fn_716() __asm__("_ZN14ObjRefConcreteI18RhythmBattlePlayer9ObjectDirE7CopyRefERKS2_");
extern "C" long _stub_fn_716() { return 0; }
// ObjRefConcrete<Sfx, ObjectDir>::CopyRef(ObjRefConcrete<Sfx, ObjectDir> const&)
extern "C" __attribute__((weak, used)) long _stub_fn_717() __asm__("_ZN14ObjRefConcreteI3Sfx9ObjectDirE7CopyRefERKS2_");
extern "C" long _stub_fn_717() { return 0; }
// ObjRefConcrete<ADSR, ObjectDir>::CopyRef(ObjRefConcrete<ADSR, ObjectDir> const&)
extern "C" __attribute__((weak, used)) long _stub_fn_718() __asm__("_ZN14ObjRefConcreteI4ADSR9ObjectDirE7CopyRefERKS2_");
extern "C" long _stub_fn_718() { return 0; }
// ObjRefConcrete<Flow, ObjectDir>::CopyRef(ObjRefConcrete<Flow, ObjectDir> const&)
extern "C" __attribute__((weak, used)) long _stub_fn_719() __asm__("_ZN14ObjRefConcreteI4Flow9ObjectDirE7CopyRefERKS2_");
extern "C" long _stub_fn_719() { return 0; }
// ObjRefConcrete<Fader, ObjectDir>::CopyRef(ObjRefConcrete<Fader, ObjectDir> const&)
extern "C" __attribute__((weak, used)) long _stub_fn_720() __asm__("_ZN14ObjRefConcreteI5Fader9ObjectDirE7CopyRefERKS2_");
extern "C" long _stub_fn_720() { return 0; }
// ObjRefConcrete<Sound, ObjectDir>::CopyRef(ObjRefConcrete<Sound, ObjectDir> const&)
extern "C" __attribute__((weak, used)) long _stub_fn_721() __asm__("_ZN14ObjRefConcreteI5Sound9ObjectDirE7CopyRefERKS2_");
extern "C" long _stub_fn_721() { return 0; }
// ObjRefConcrete<FxSend, ObjectDir>::CopyRef(ObjRefConcrete<FxSend, ObjectDir> const&)
extern "C" __attribute__((weak, used)) long _stub_fn_722() __asm__("_ZN14ObjRefConcreteI6FxSend9ObjectDirE7CopyRefERKS2_");
extern "C" long _stub_fn_722() { return 0; }
// ObjRefConcrete<RndCam, ObjectDir>::CopyRef(ObjRefConcrete<RndCam, ObjectDir> const&)
extern "C" __attribute__((weak, used)) long _stub_fn_723() __asm__("_ZN14ObjRefConcreteI6RndCam9ObjectDirE7CopyRefERKS2_");
extern "C" long _stub_fn_723() { return 0; }
// ObjRefConcrete<RndDir, ObjectDir>::CopyRef(ObjRefConcrete<RndDir, ObjectDir> const&)
extern "C" __attribute__((weak, used)) long _stub_fn_724() __asm__("_ZN14ObjRefConcreteI6RndDir9ObjectDirE7CopyRefERKS2_");
extern "C" long _stub_fn_724() { return 0; }
// ObjRefConcrete<RndFur, ObjectDir>::CopyRef(ObjRefConcrete<RndFur, ObjectDir> const&)
extern "C" __attribute__((weak, used)) long _stub_fn_725() __asm__("_ZN14ObjRefConcreteI6RndFur9ObjectDirE7CopyRefERKS2_");
extern "C" long _stub_fn_725() { return 0; }
// ObjRefConcrete<RndMat, ObjectDir>::CopyRef(ObjRefConcrete<RndMat, ObjectDir> const&)
extern "C" __attribute__((weak, used)) long _stub_fn_726() __asm__("_ZN14ObjRefConcreteI6RndMat9ObjectDirE7CopyRefERKS2_");
extern "C" long _stub_fn_726() { return 0; }
// ObjRefConcrete<RndTex, ObjectDir>::CopyRef(ObjRefConcrete<RndTex, ObjectDir> const&)
extern "C" __attribute__((weak, used)) long _stub_fn_727() __asm__("_ZN14ObjRefConcreteI6RndTex9ObjectDirE7CopyRefERKS2_");
extern "C" long _stub_fn_727() { return 0; }
// ObjRefConcrete<UIList, ObjectDir>::CopyRef(ObjRefConcrete<UIList, ObjectDir> const&)
extern "C" __attribute__((weak, used)) long _stub_fn_728() __asm__("_ZN14ObjRefConcreteI6UIList9ObjectDirE7CopyRefERKS2_");
extern "C" long _stub_fn_728() { return 0; }
// ObjRefConcrete<CamShot, ObjectDir>::CopyRef(ObjRefConcrete<CamShot, ObjectDir> const&)
extern "C" __attribute__((weak, used)) long _stub_fn_729() __asm__("_ZN14ObjRefConcreteI7CamShot9ObjectDirE7CopyRefERKS2_");
extern "C" long _stub_fn_729() { return 0; }
// ObjRefConcrete<HamMove, ObjectDir>::CopyRef(ObjRefConcrete<HamMove, ObjectDir> const&)
extern "C" __attribute__((weak, used)) long _stub_fn_730() __asm__("_ZN14ObjRefConcreteI7HamMove9ObjectDirE7CopyRefERKS2_");
extern "C" long _stub_fn_730() { return 0; }
// ObjRefConcrete<RndMesh, ObjectDir>::CopyRef(ObjRefConcrete<RndMesh, ObjectDir> const&)
extern "C" __attribute__((weak, used)) long _stub_fn_731() __asm__("_ZN14ObjRefConcreteI7RndMesh9ObjectDirE7CopyRefERKS2_");
extern "C" long _stub_fn_731() { return 0; }
// ObjRefConcrete<RndWind, ObjectDir>::CopyRef(ObjRefConcrete<RndWind, ObjectDir> const&)
extern "C" __attribute__((weak, used)) long _stub_fn_732() __asm__("_ZN14ObjRefConcreteI7RndWind9ObjectDirE7CopyRefERKS2_");
extern "C" long _stub_fn_732() { return 0; }
// ObjRefConcrete<UIColor, ObjectDir>::CopyRef(ObjRefConcrete<UIColor, ObjectDir> const&)
extern "C" __attribute__((weak, used)) long _stub_fn_733() __asm__("_ZN14ObjRefConcreteI7UIColor9ObjectDirE7CopyRefERKS2_");
extern "C" long _stub_fn_733() { return 0; }
// ObjRefConcrete<UILabel, ObjectDir>::CopyRef(ObjRefConcrete<UILabel, ObjectDir> const&)
extern "C" __attribute__((weak, used)) long _stub_fn_734() __asm__("_ZN14ObjRefConcreteI7UILabel9ObjectDirE7CopyRefERKS2_");
extern "C" long _stub_fn_734() { return 0; }
// ObjRefConcrete<CharBone, ObjectDir>::CopyRef(ObjRefConcrete<CharBone, ObjectDir> const&)
extern "C" __attribute__((weak, used)) long _stub_fn_735() __asm__("_ZN14ObjRefConcreteI8CharBone9ObjectDirE7CopyRefERKS2_");
extern "C" long _stub_fn_735() { return 0; }
// ObjRefConcrete<CharClip, ObjectDir>::CopyRef(ObjRefConcrete<CharClip, ObjectDir> const&)
extern "C" __attribute__((weak, used)) long _stub_fn_736() __asm__("_ZN14ObjRefConcreteI8CharClip9ObjectDirE7CopyRefERKS2_");
extern "C" long _stub_fn_736() { return 0; }
// ObjRefConcrete<HamLabel, ObjectDir>::CopyRef(ObjRefConcrete<HamLabel, ObjectDir> const&)
extern "C" __attribute__((weak, used)) long _stub_fn_737() __asm__("_ZN14ObjRefConcreteI8HamLabel9ObjectDirE7CopyRefERKS2_");
extern "C" long _stub_fn_737() { return 0; }
// ObjRefConcrete<LightHue, ObjectDir>::CopyRef(ObjRefConcrete<LightHue, ObjectDir> const&)
extern "C" __attribute__((weak, used)) long _stub_fn_738() __asm__("_ZN14ObjRefConcreteI8LightHue9ObjectDirE7CopyRefERKS2_");
extern "C" long _stub_fn_738() { return 0; }
// ObjRefConcrete<MoggClip, ObjectDir>::CopyRef(ObjRefConcrete<MoggClip, ObjectDir> const&)
extern "C" __attribute__((weak, used)) long _stub_fn_739() __asm__("_ZN14ObjRefConcreteI8MoggClip9ObjectDirE7CopyRefERKS2_");
extern "C" long _stub_fn_739() { return 0; }
// ObjRefConcrete<RndGroup, ObjectDir>::CopyRef(ObjRefConcrete<RndGroup, ObjectDir> const&)
extern "C" __attribute__((weak, used)) long _stub_fn_740() __asm__("_ZN14ObjRefConcreteI8RndGroup9ObjectDirE7CopyRefERKS2_");
extern "C" long _stub_fn_740() { return 0; }
// ObjRefConcrete<RndLight, ObjectDir>::CopyRef(ObjRefConcrete<RndLight, ObjectDir> const&)
extern "C" __attribute__((weak, used)) long _stub_fn_741() __asm__("_ZN14ObjRefConcreteI8RndLight9ObjectDirE7CopyRefERKS2_");
extern "C" long _stub_fn_741() { return 0; }
// ObjRefConcrete<Character, ObjectDir>::CopyRef(ObjRefConcrete<Character, ObjectDir> const&)
extern "C" __attribute__((weak, used)) long _stub_fn_742() __asm__("_ZN14ObjRefConcreteI9Character9ObjectDirE7CopyRefERKS2_");
extern "C" long _stub_fn_742() { return 0; }
// ObjRefConcrete<ObjectDir, ObjectDir>::CopyRef(ObjRefConcrete<ObjectDir, ObjectDir> const&)
extern "C" __attribute__((weak, used)) long _stub_fn_743() __asm__("_ZN14ObjRefConcreteI9ObjectDirS0_E7CopyRefERKS1_");
extern "C" long _stub_fn_743() { return 0; }
// ObjRefConcrete<Spotlight, ObjectDir>::CopyRef(ObjRefConcrete<Spotlight, ObjectDir> const&)
extern "C" __attribute__((weak, used)) long _stub_fn_744() __asm__("_ZN14ObjRefConcreteI9Spotlight9ObjectDirE7CopyRefERKS2_");
extern "C" long _stub_fn_744() { return 0; }
// ObjRefConcrete<Hmx::Object, ObjectDir>::CopyRef(ObjRefConcrete<Hmx::Object, ObjectDir> const&)
extern "C" __attribute__((weak, used)) long _stub_fn_745() __asm__("_ZN14ObjRefConcreteIN3Hmx6ObjectE9ObjectDirE7CopyRefERKS3_");
extern "C" long _stub_fn_745() { return 0; }
// PoseFatalities::UpdateClipDriver(int)
extern "C" __attribute__((weak, used)) long _stub_fn_746() __asm__("_ZN14PoseFatalities16UpdateClipDriverEi");
extern "C" long _stub_fn_746() { return 0; }
// PoseFatalities::UpdateMatchingPose(int)
extern "C" __attribute__((weak, used)) long _stub_fn_747() __asm__("_ZN14PoseFatalities18UpdateMatchingPoseEi");
extern "C" long _stub_fn_747() { return 0; }
// PoseFatalities::DrawDebug()
extern "C" __attribute__((weak, used)) long _stub_fn_748() __asm__("_ZN14PoseFatalities9DrawDebugEv");
extern "C" long _stub_fn_748() { return 0; }
// RandomGroupSeq::PickNextIndex()
extern "C" __attribute__((weak, used)) long _stub_fn_749() __asm__("_ZN14RandomGroupSeq13PickNextIndexEv");
extern "C" long _stub_fn_749() { return 0; }
// RhythmDetector::ProcessFrames()
extern "C" __attribute__((weak, used)) long _stub_fn_750() __asm__("_ZN14RhythmDetector13ProcessFramesEv");
extern "C" long _stub_fn_750() { return 0; }
// RhythmDetector::AddFrame(BaseSkeleton const&)
extern "C" __attribute__((weak, used)) long _stub_fn_751() __asm__("_ZN14RhythmDetector8AddFrameERK12BaseSkeleton");
extern "C" long _stub_fn_751() { return 0; }
// RhythmDetector::GetRecord(float, float, bool, Symbol, TextStream*)
extern "C" __attribute__((weak, used)) long _stub_fn_752() __asm__("_ZN14RhythmDetector9GetRecordEffb6SymbolP10TextStream");
extern "C" long _stub_fn_752() { return 0; }
// RndParticleSys::InitParticle(float, RndParticle*, Transform const*, PartOverride&)
extern "C" __attribute__((weak, used)) long _stub_fn_753() __asm__("_ZN14RndParticleSys12InitParticleEfP11RndParticlePK9TransformR12PartOverride");
extern "C" long _stub_fn_753() { return 0; }
// RndParticleSys::Load(BinStream&)
extern "C" __attribute__((weak, used)) long _stub_fn_754() __asm__("_ZN14RndParticleSys4LoadER9BinStream");
extern "C" long _stub_fn_754() { return 0; }
// RndParticleSys::Mats(std::__cxx11::list<RndMat*, std::allocator<RndMat*> >&, bool)
extern "C" __attribute__((weak, used)) long _stub_fn_755() __asm__("_ZN14RndParticleSys4MatsERNSt7__cxx114listIP6RndMatSaIS3_EEEb");
extern "C" long _stub_fn_755() { return 0; }
// RndParticleSys::Poll()
extern "C" __attribute__((weak, used)) long _stub_fn_756() __asm__("_ZN14RndParticleSys4PollEv");
extern "C" long _stub_fn_756() { return 0; }
// RndParticleSys::Replace(ObjRef*, Hmx::Object*)
extern "C" __attribute__((weak, used)) long _stub_fn_757() __asm__("_ZN14RndParticleSys7ReplaceEP6ObjRefPN3Hmx6ObjectE");
extern "C" long _stub_fn_757() { return 0; }
// RndRenderState::SetBlendOp(RndRenderState::BlendOp)
extern "C" __attribute__((weak, used)) long _stub_fn_758() __asm__("_ZN14RndRenderState10SetBlendOpENS_7BlendOpE");
extern "C" long _stub_fn_758() { return 0; }
// RndRenderState::SetCullMode(RndRenderState::CullMode)
extern "C" __attribute__((weak, used)) long _stub_fn_759() __asm__("_ZN14RndRenderState11SetCullModeENS_8CullModeE");
extern "C" long _stub_fn_759() { return 0; }
// RndRenderState::SetFillMode(RndRenderState::FillMode)
extern "C" __attribute__((weak, used)) long _stub_fn_760() __asm__("_ZN14RndRenderState11SetFillModeENS_8FillModeE");
extern "C" long _stub_fn_760() { return 0; }
// RndRenderState::SetAlphaFunc(RndRenderState::TestFunc, unsigned int)
extern "C" __attribute__((weak, used)) long _stub_fn_761() __asm__("_ZN14RndRenderState12SetAlphaFuncENS_8TestFuncEj");
extern "C" long _stub_fn_761() { return 0; }
// RndRenderState::SetDepthFunc(RndRenderState::TestFunc)
extern "C" __attribute__((weak, used)) long _stub_fn_762() __asm__("_ZN14RndRenderState12SetDepthFuncENS_8TestFuncE");
extern "C" long _stub_fn_762() { return 0; }
// RndRenderState::SetStencilOp(RndRenderState::StencilOp, RndRenderState::StencilOp, RndRenderState::StencilOp)
extern "C" __attribute__((weak, used)) long _stub_fn_763() __asm__("_ZN14RndRenderState12SetStencilOpENS_9StencilOpES0_S0_");
extern "C" long _stub_fn_763() { return 0; }
// RndRenderState::SetBlendEnable(bool)
extern "C" __attribute__((weak, used)) long _stub_fn_764() __asm__("_ZN14RndRenderState14SetBlendEnableEb");
extern "C" long _stub_fn_764() { return 0; }
// RndRenderState::SetBorderColor(unsigned int, bool)
extern "C" __attribute__((weak, used)) long _stub_fn_765() __asm__("_ZN14RndRenderState14SetBorderColorEjb");
extern "C" long _stub_fn_765() { return 0; }
// RndRenderState::SetStencilFunc(RndRenderState::TestFunc, unsigned char)
extern "C" __attribute__((weak, used)) long _stub_fn_766() __asm__("_ZN14RndRenderState14SetStencilFuncENS_8TestFuncEh");
extern "C" long _stub_fn_766() { return 0; }
// RndRenderState::SetTextureClamp(unsigned int, RndRenderState::ClampMode)
extern "C" __attribute__((weak, used)) long _stub_fn_767() __asm__("_ZN14RndRenderState15SetTextureClampEjNS_9ClampModeE");
extern "C" long _stub_fn_767() { return 0; }
// RndRenderState::SetTextureFilter(unsigned int, RndRenderState::FilterMode, bool)
extern "C" __attribute__((weak, used)) long _stub_fn_768() __asm__("_ZN14RndRenderState16SetTextureFilterEjNS_10FilterModeEb");
extern "C" long _stub_fn_768() { return 0; }
// RndRenderState::SetAlphaTestEnable(bool)
extern "C" __attribute__((weak, used)) long _stub_fn_769() __asm__("_ZN14RndRenderState18SetAlphaTestEnableEb");
extern "C" long _stub_fn_769() { return 0; }
// RndRenderState::SetDepthTestEnable(bool)
extern "C" __attribute__((weak, used)) long _stub_fn_770() __asm__("_ZN14RndRenderState18SetDepthTestEnableEb");
extern "C" long _stub_fn_770() { return 0; }
// RndRenderState::SetDepthWriteEnable(bool)
extern "C" __attribute__((weak, used)) long _stub_fn_771() __asm__("_ZN14RndRenderState19SetDepthWriteEnableEb");
extern "C" long _stub_fn_771() { return 0; }
// RndRenderState::SetStencilTestEnable(bool)
extern "C" __attribute__((weak, used)) long _stub_fn_772() __asm__("_ZN14RndRenderState20SetStencilTestEnableEb");
extern "C" long _stub_fn_772() { return 0; }
// RndRenderState::SetBlend(RndRenderState::Blend, RndRenderState::Blend, RndRenderState::Blend, RndRenderState::Blend)
extern "C" __attribute__((weak, used)) long _stub_fn_773() __asm__("_ZN14RndRenderState8SetBlendENS_5BlendES0_S0_S0_");
extern "C" long _stub_fn_773() { return 0; }
// SkeletonUpdate::Update()
extern "C" __attribute__((weak, used)) long _stub_fn_774() __asm__("_ZN14SkeletonUpdate6UpdateEv");
extern "C" long _stub_fn_774() { return 0; }
// SongHeaderNode::GetFirstActive()
extern "C" __attribute__((weak, used)) long _stub_fn_775() __asm__("_ZN14SongHeaderNode14GetFirstActiveEv");
extern "C" long _stub_fn_775() { return 0; }
// SongSortBySong::SongSortBySong()
extern "C" __attribute__((weak, used)) long _stub_fn_776() __asm__("_ZN14SongSortBySongC1Ev");
extern "C" long _stub_fn_776() { return 0; }
// SpotDrawParams::operator=(SpotDrawParams const&)
extern "C" __attribute__((weak, used)) long _stub_fn_777() __asm__("_ZN14SpotDrawParamsaSERKS_");
extern "C" long _stub_fn_777() { return 0; }
// StandardStream::PollStream()
extern "C" __attribute__((weak, used)) long _stub_fn_778() __asm__("_ZN14StandardStream10PollStreamEv");
extern "C" long _stub_fn_778() { return 0; }
// StandardStream::UpdateTime()
extern "C" __attribute__((weak, used)) long _stub_fn_779() __asm__("_ZN14StandardStream10UpdateTimeEv");
extern "C" long _stub_fn_779() { return 0; }
// StandardStream::ConsumeData(void**, int, int)
extern "C" __attribute__((weak, used)) long _stub_fn_780() __asm__("_ZN14StandardStream11ConsumeDataEPPvii");
extern "C" long _stub_fn_780() { return 0; }
// StandardStream::UpdateVolumes()
extern "C" __attribute__((weak, used)) long _stub_fn_781() __asm__("_ZN14StandardStream13UpdateVolumesEv");
extern "C" long _stub_fn_781() { return 0; }
// StandardStream::GetJumpBackTotalTime(float)
extern "C" __attribute__((weak, used)) long _stub_fn_782() __asm__("_ZN14StandardStream20GetJumpBackTotalTimeEf");
extern "C" long _stub_fn_782() { return 0; }
// StandardStream::setJumpSamplesFromMs(float, float)
extern "C" __attribute__((weak, used)) long _stub_fn_783() __asm__("_ZN14StandardStream20setJumpSamplesFromMsEff");
extern "C" long _stub_fn_783() { return 0; }
// StandardStream::UpdateTimeByFiltering()
extern "C" __attribute__((weak, used)) long _stub_fn_784() __asm__("_ZN14StandardStream21UpdateTimeByFilteringEv");
extern "C" long _stub_fn_784() { return 0; }
// StandardStream::IsPastStreamJumpPointOfNoReturn()
extern "C" __attribute__((weak, used)) long _stub_fn_785() __asm__("_ZN14StandardStream31IsPastStreamJumpPointOfNoReturnEv");
extern "C" long _stub_fn_785() { return 0; }
// StreamReceiver::GetBytesPlayed()
extern "C" __attribute__((weak, used)) long _stub_fn_786() __asm__("_ZN14StreamReceiver14GetBytesPlayedEv");
extern "C" long _stub_fn_786() { return 0; }
// StreamReceiver::New(int, int, bool, int)
extern "C" __attribute__((weak, used)) long _stub_fn_787() __asm__("_ZN14StreamReceiver3NewEiibi");
extern "C" long _stub_fn_787() { return 0; }
// StreamReceiver::Poll()
extern "C" __attribute__((weak, used)) long _stub_fn_788() __asm__("_ZN14StreamReceiver4PollEv");
extern "C" long _stub_fn_788() { return 0; }
// StreamRecorder::DrawShowing()
extern "C" __attribute__((weak, used)) long _stub_fn_789() __asm__("_ZN14StreamRecorder11DrawShowingEv");
extern "C" long _stub_fn_789() { return 0; }
// StreamRecorder::Poll()
extern "C" __attribute__((weak, used)) long _stub_fn_790() __asm__("_ZN14StreamRecorder4PollEv");
extern "C" long _stub_fn_790() { return 0; }
// StreamRenderer::DrawToTexture()
extern "C" __attribute__((weak, used)) long _stub_fn_791() __asm__("_ZN14StreamRenderer13DrawToTextureEv");
extern "C" long _stub_fn_791() { return 0; }
// StreamRenderer::SetCrewPhotoPlayerCenters()
extern "C" __attribute__((weak, used)) long _stub_fn_792() __asm__("_ZN14StreamRenderer25SetCrewPhotoPlayerCentersEv");
extern "C" long _stub_fn_792() { return 0; }
// XLSPConnection::SetState(XLSPConnection::State)
extern "C" __attribute__((weak, used)) long _stub_fn_793() __asm__("_ZN14XLSPConnection8SetStateENS_5StateE");
extern "C" long _stub_fn_793() { return 0; }
// CharCameraInput::PollNewFrame()
extern "C" __attribute__((weak, used)) long _stub_fn_794() __asm__("_ZN15CharCameraInput12PollNewFrameEv");
extern "C" long _stub_fn_794() { return 0; }
// CharCameraInput::ResetSkeletonCharOrigin()
extern "C" __attribute__((weak, used)) long _stub_fn_795() __asm__("_ZN15CharCameraInput23ResetSkeletonCharOriginEv");
extern "C" long _stub_fn_795() { return 0; }
// CharClipDisplay::SetStartEnd(float, float, bool)
extern "C" __attribute__((weak, used)) long _stub_fn_796() __asm__("_ZN15CharClipDisplay11SetStartEndEffb");
extern "C" long _stub_fn_796() { return 0; }
// CriticalSection::~CriticalSection()
extern "C" __attribute__((weak, used)) long _stub_fn_797() __asm__("_ZN15CriticalSectionD1Ev");
extern "C" long _stub_fn_797() { return 0; }
// CriticalSection::~CriticalSection()
extern "C" __attribute__((weak, used)) long _stub_fn_798() __asm__("_ZN15CriticalSectionD2Ev");
extern "C" long _stub_fn_798() { return 0; }
// HamCamTransform::ClearOldCrowds()
extern "C" __attribute__((weak, used)) long _stub_fn_799() __asm__("_ZN15HamCamTransform14ClearOldCrowdsEv");
extern "C" long _stub_fn_799() { return 0; }
// HamCamTransform::Copy(Hmx::Object const*, Hmx::Object::CopyType)
extern "C" __attribute__((weak, used)) long _stub_fn_800() __asm__("_ZN15HamCamTransform4CopyEPKN3Hmx6ObjectENS1_8CopyTypeE");
extern "C" long _stub_fn_800() { return 0; }
// HamCamTransform::Load(BinStream&)
extern "C" __attribute__((weak, used)) long _stub_fn_801() __asm__("_ZN15HamCamTransform4LoadER9BinStream");
extern "C" long _stub_fn_801() { return 0; }
// HamCamTransform::Setup(bool)
extern "C" __attribute__((weak, used)) long _stub_fn_802() __asm__("_ZN15HamCamTransform5SetupEb");
extern "C" long _stub_fn_802() { return 0; }
// LiveCameraInput::TextureStore::StoreColorBufferClip(LiveCameraInput*, float, float, float, float)
extern "C" __attribute__((weak, used)) long _stub_fn_803() __asm__("_ZN15LiveCameraInput12TextureStore20StoreColorBufferClipEPS_ffff");
extern "C" long _stub_fn_803() { return 0; }
// LiveCameraInput::TextureStore::StoreDepthBufferClip(LiveCameraInput*, float, float, float, float)
extern "C" __attribute__((weak, used)) long _stub_fn_804() __asm__("_ZN15LiveCameraInput12TextureStore20StoreDepthBufferClipEPS_ffff");
extern "C" long _stub_fn_804() { return 0; }
// LiveCameraInput::TextureStore::UpdateFromColorBuffer(LiveCameraInput*)
extern "C" __attribute__((weak, used)) long _stub_fn_805() __asm__("_ZN15LiveCameraInput12TextureStore21UpdateFromColorBufferEPS_");
extern "C" long _stub_fn_805() { return 0; }
// LiveCameraInput::TextureStore::UpdateFromDepthBuffer(LiveCameraInput*)
extern "C" __attribute__((weak, used)) long _stub_fn_806() __asm__("_ZN15LiveCameraInput12TextureStore21UpdateFromDepthBufferEPS_");
extern "C" long _stub_fn_806() { return 0; }
// LiveCameraInput::ClearSnapshots()
extern "C" __attribute__((weak, used)) long _stub_fn_807() __asm__("_ZN15LiveCameraInput14ClearSnapshotsEv");
extern "C" long _stub_fn_807() { return 0; }
// LiveCameraInput::SetAutoexposure(bool)
extern "C" __attribute__((weak, used)) long _stub_fn_808() __asm__("_ZN15LiveCameraInput15SetAutoexposureEb");
extern "C" long _stub_fn_808() { return 0; }
// LiveCameraInput::SetExposureRegion(float, float, float, float)
extern "C" __attribute__((weak, used)) long _stub_fn_809() __asm__("_ZN15LiveCameraInput17SetExposureRegionEffff");
extern "C" long _stub_fn_809() { return 0; }
// LiveCameraInput::NuiAudioDataCallback(_NUIAUDIO_RESULTS*)
extern "C" __attribute__((weak, used)) long _stub_fn_810() __asm__("_ZN15LiveCameraInput20NuiAudioDataCallbackEP17_NUIAUDIO_RESULTS");
extern "C" long _stub_fn_810() { return 0; }
// LiveCameraInput::SetTweakedAutoexposure(bool)
extern "C" __attribute__((weak, used)) long _stub_fn_811() __asm__("_ZN15LiveCameraInput22SetTweakedAutoexposureEb");
extern "C" long _stub_fn_811() { return 0; }
// MicClientMapper::RefreshMics()
extern "C" __attribute__((weak, used)) long _stub_fn_812() __asm__("_ZN15MicClientMapper11RefreshMicsEv");
extern "C" long _stub_fn_812() { return 0; }
// NetCacheMgrXbox::NetCacheMgrXbox()
extern "C" __attribute__((weak, used)) long _stub_fn_813() __asm__("_ZN15NetCacheMgrXboxC1Ev");
extern "C" long _stub_fn_813() { return 0; }
// PlaylistSortMgr::UpdateList()
extern "C" __attribute__((weak, used)) long _stub_fn_814() __asm__("_ZN15PlaylistSortMgr10UpdateListEv");
extern "C" long _stub_fn_814() { return 0; }
// PlaylistSortMgr::GetPlaylist(int)
extern "C" __attribute__((weak, used)) long _stub_fn_815() __asm__("_ZN15PlaylistSortMgr11GetPlaylistEi");
extern "C" long _stub_fn_815() { return 0; }
// PlaylistSortMgr::SendPassiveMsg(Symbol)
extern "C" __attribute__((weak, used)) long _stub_fn_816() __asm__("_ZN15PlaylistSortMgr14SendPassiveMsgE6Symbol");
extern "C" long _stub_fn_816() { return 0; }
// PlaylistSortMgr::OnDeletePlaylistFromRC(Playlist*)
extern "C" __attribute__((weak, used)) long _stub_fn_817() __asm__("_ZN15PlaylistSortMgr22OnDeletePlaylistFromRCEP8Playlist");
extern "C" long _stub_fn_817() { return 0; }
// PlaylistSortMgr::UpdateCurrPlaylistWithRC()
extern "C" __attribute__((weak, used)) long _stub_fn_818() __asm__("_ZN15PlaylistSortMgr24UpdateCurrPlaylistWithRCEv");
extern "C" long _stub_fn_818() { return 0; }
// PlaylistSortMgr::QueueCmdGetPlaylistsFromRC()
extern "C" __attribute__((weak, used)) long _stub_fn_819() __asm__("_ZN15PlaylistSortMgr26QueueCmdGetPlaylistsFromRCEv");
extern "C" long _stub_fn_819() { return 0; }
// PlaylistSortMgr::QueueCmdChangeProfileOnlineID(String)
extern "C" __attribute__((weak, used)) long _stub_fn_820() __asm__("_ZN15PlaylistSortMgr29QueueCmdChangeProfileOnlineIDE6String");
extern "C" long _stub_fn_820() { return 0; }
// PlaylistSortMgr::HandleCmdChangeProfileOnlineID()
extern "C" __attribute__((weak, used)) long _stub_fn_821() __asm__("_ZN15PlaylistSortMgr30HandleCmdChangeProfileOnlineIDEv");
extern "C" long _stub_fn_821() { return 0; }
// PlaylistSortMgr::OnMsg(RCJobCompleteMsg const&)
extern "C" __attribute__((weak, used)) long _stub_fn_822() __asm__("_ZN15PlaylistSortMgr5OnMsgERK16RCJobCompleteMsg");
extern "C" long _stub_fn_822() { return 0; }
// RndShaderSimple::CalcShaderOpts(NgMat*, ShaderType, bool)
extern "C" __attribute__((weak, used)) long _stub_fn_823() __asm__("_ZN15RndShaderSimple14CalcShaderOptsEP5NgMat10ShaderTypeb");
extern "C" long _stub_fn_823() { return 0; }
// SaveLoadManager::HandleEventResponse(HamProfile*, int)
extern "C" __attribute__((weak, used)) long _stub_fn_824() __asm__("_ZN15SaveLoadManager19HandleEventResponseEP10HamProfilei");
extern "C" long _stub_fn_824() { return 0; }
// SaveLoadManager::Poll()
extern "C" __attribute__((weak, used)) long _stub_fn_825() __asm__("_ZN15SaveLoadManager4PollEv");
extern "C" long _stub_fn_825() { return 0; }
// SaveLoadManager::OnMsg(MCResultMsg const&)
extern "C" __attribute__((weak, used)) long _stub_fn_826() __asm__("_ZN15SaveLoadManager5OnMsgERK11MCResultMsg");
extern "C" long _stub_fn_826() { return 0; }
// SaveLoadManager::OnMsg(SigninChangedMsg const&)
extern "C" __attribute__((weak, used)) long _stub_fn_827() __asm__("_ZN15SaveLoadManager5OnMsgERK16SigninChangedMsg");
extern "C" long _stub_fn_827() { return 0; }
// SkeletonChooser::RoundRobinForHandRaised(int)
extern "C" __attribute__((weak, used)) long _stub_fn_828() __asm__("_ZN15SkeletonChooser23RoundRobinForHandRaisedEi");
extern "C" long _stub_fn_828() { return 0; }
// SkeletonChooser::ResolveMultiPlayerUpdate()
extern "C" __attribute__((weak, used)) long _stub_fn_829() __asm__("_ZN15SkeletonChooser24ResolveMultiPlayerUpdateEv");
extern "C" long _stub_fn_829() { return 0; }
// SkeletonChooser::CheckToSwitchActivePlayer()
extern "C" __attribute__((weak, used)) long _stub_fn_830() __asm__("_ZN15SkeletonChooser25CheckToSwitchActivePlayerEv");
extern "C" long _stub_fn_830() { return 0; }
// SkeletonChooser::RoundRobinForStandingStill(int)
extern "C" __attribute__((weak, used)) long _stub_fn_831() __asm__("_ZN15SkeletonChooser26RoundRobinForStandingStillEi");
extern "C" long _stub_fn_831() { return 0; }
// SkeletonChooser::UpdatePlayerSkeletonNavData()
extern "C" __attribute__((weak, used)) long _stub_fn_832() __asm__("_ZN15SkeletonChooser27UpdatePlayerSkeletonNavDataEv");
extern "C" long _stub_fn_832() { return 0; }
// SkeletonChooser::DrawDebug()
extern "C" __attribute__((weak, used)) long _stub_fn_833() __asm__("_ZN15SkeletonChooser9DrawDebugEv");
extern "C" long _stub_fn_833() { return 0; }
// SpotlightDrawer::DrawShadow()
extern "C" __attribute__((weak, used)) long _stub_fn_834() __asm__("_ZN15SpotlightDrawer10DrawShadowEv");
extern "C" long _stub_fn_834() { return 0; }
// SpotlightDrawer::ClearLights()
extern "C" __attribute__((weak, used)) long _stub_fn_835() __asm__("_ZN15SpotlightDrawer11ClearLightsEv");
extern "C" long _stub_fn_835() { return 0; }
// SpotlightDrawer::DrawShowing()
extern "C" __attribute__((weak, used)) long _stub_fn_836() __asm__("_ZN15SpotlightDrawer11DrawShowingEv");
extern "C" long _stub_fn_836() { return 0; }
// SpotlightDrawer::UpdateBoxMap()
extern "C" __attribute__((weak, used)) long _stub_fn_837() __asm__("_ZN15SpotlightDrawer12UpdateBoxMapEv");
extern "C" long _stub_fn_837() { return 0; }
// SpotlightDrawer::Load(BinStream&)
extern "C" __attribute__((weak, used)) long _stub_fn_838() __asm__("_ZN15SpotlightDrawer4LoadER9BinStream");
extern "C" long _stub_fn_838() { return 0; }
// SpotlightDrawer::DeSelect()
extern "C" __attribute__((weak, used)) long _stub_fn_839() __asm__("_ZN15SpotlightDrawer8DeSelectEv");
extern "C" long _stub_fn_839() { return 0; }
// SpotlightDrawer::DrawWorld()
extern "C" __attribute__((weak, used)) long _stub_fn_840() __asm__("_ZN15SpotlightDrawer9DrawWorldEv");
extern "C" long _stub_fn_840() { return 0; }
// StorePreviewMgr::Poll()
extern "C" __attribute__((weak, used)) long _stub_fn_841() __asm__("_ZN15StorePreviewMgr4PollEv");
extern "C" long _stub_fn_841() { return 0; }
// TransConstraint::Poll()
extern "C" __attribute__((weak, used)) long _stub_fn_842() __asm__("_ZN15TransConstraint4PollEv");
extern "C" long _stub_fn_842() { return 0; }
// TransConstraint::Highlight()
extern "C" __attribute__((weak, used)) long _stub_fn_843() __asm__("_ZN15TransConstraint9HighlightEv");
extern "C" long _stub_fn_843() { return 0; }
// VirtualKeyboard::PlatformPoll()
extern "C" __attribute__((weak, used)) long _stub_fn_844() __asm__("_ZN15VirtualKeyboard12PlatformPollEv");
extern "C" long _stub_fn_844() { return 0; }
// VirtualKeyboard::GetInputString()
extern "C" __attribute__((weak, used)) long _stub_fn_845() __asm__("_ZN15VirtualKeyboard14GetInputStringEv");
extern "C" long _stub_fn_845() { return 0; }
// VirtualKeyboard::ShowKeyboardUI(int, int, String, String, String, int)
extern "C" __attribute__((weak, used)) long _stub_fn_846() __asm__("_ZN15VirtualKeyboard14ShowKeyboardUIEii6StringS0_S0_i");
extern "C" long _stub_fn_846() { return 0; }
// VoiceInputPanel::ActivateVoiceContext(Symbol)
extern "C" __attribute__((weak, used)) long _stub_fn_847() __asm__("_ZN15VoiceInputPanel20ActivateVoiceContextE6Symbol");
extern "C" long _stub_fn_847() { return 0; }
// VoiceInputPanel::OnMsg(SpeechRecoMessage const&)
extern "C" __attribute__((weak, used)) long _stub_fn_848() __asm__("_ZN15VoiceInputPanel5OnMsgERK17SpeechRecoMessage");
extern "C" long _stub_fn_848() { return 0; }
// WorldReflection::Highlight()
extern "C" __attribute__((weak, used)) long _stub_fn_849() __asm__("_ZN15WorldReflection9HighlightEv");
extern "C" long _stub_fn_849() { return 0; }
// AudioDuckerGroup::Add(Fader*, float)
extern "C" __attribute__((weak, used)) long _stub_fn_850() __asm__("_ZN16AudioDuckerGroup3AddEP5Faderf");
extern "C" long _stub_fn_850() { return 0; }
// AudioDuckerGroup::Remove(Fader*)
extern "C" __attribute__((weak, used)) long _stub_fn_851() __asm__("_ZN16AudioDuckerGroup6RemoveEP5Fader");
extern "C" long _stub_fn_851() { return 0; }
// AutoGlitchReport::EndExternal(float, float, char const*, void (*)(float, void*), void*)
extern "C" __attribute__((weak, used)) long _stub_fn_852() __asm__("_ZN16AutoGlitchReport11EndExternalEffPKcPFvfPvES2_");
extern "C" long _stub_fn_852() { return 0; }
// ChallengeSortMgr::GetChallengerXp(int)
extern "C" __attribute__((weak, used)) long _stub_fn_853() __asm__("_ZN16ChallengeSortMgr15GetChallengerXpEi");
extern "C" long _stub_fn_853() { return 0; }
// ChallengeSortMgr::GetChallengerGamertag(int)
extern "C" __attribute__((weak, used)) long _stub_fn_854() __asm__("_ZN16ChallengeSortMgr21GetChallengerGamertagEi");
extern "C" long _stub_fn_854() { return 0; }
// ChallengeSortMgr::OnEnter()
extern "C" __attribute__((weak, used)) long _stub_fn_855() __asm__("_ZN16ChallengeSortMgr7OnEnterEv");
extern "C" long _stub_fn_855() { return 0; }
// CharBonesSamples::LoadHeader(BinStreamRev&)
extern "C" __attribute__((weak, used)) long _stub_fn_856() __asm__("_ZN16CharBonesSamples10LoadHeaderER12BinStreamRev");
extern "C" long _stub_fn_856() { return 0; }
// CharBonesSamples::SyncProperty(DataNode&, DataArray*, int, PropOp)
extern "C" __attribute__((weak, used)) long _stub_fn_857() __asm__("_ZN16CharBonesSamples12SyncPropertyER8DataNodeP9DataArrayi6PropOp");
extern "C" long _stub_fn_857() { return 0; }
// CharBonesSamples::EvaluateChannel(void*, int, int, float)
extern "C" __attribute__((weak, used)) long _stub_fn_858() __asm__("_ZN16CharBonesSamples15EvaluateChannelEPviif");
extern "C" long _stub_fn_858() { return 0; }
// CharBonesSamples::Save(BinStream&)
extern "C" __attribute__((weak, used)) long _stub_fn_859() __asm__("_ZN16CharBonesSamples4SaveER9BinStream");
extern "C" long _stub_fn_859() { return 0; }
// CharBonesSamples::LoadData(BinStreamRev&)
extern "C" __attribute__((weak, used)) long _stub_fn_860() __asm__("_ZN16CharBonesSamples8LoadDataER12BinStreamRev");
extern "C" long _stub_fn_860() { return 0; }
// HamStoreProvider::OnNextSort()
extern "C" __attribute__((weak, used)) long _stub_fn_861() __asm__("_ZN16HamStoreProvider10OnNextSortEv");
extern "C" long _stub_fn_861() { return 0; }
// HamStoreProvider::RefreshFilteredCartOffers()
extern "C" __attribute__((weak, used)) long _stub_fn_862() __asm__("_ZN16HamStoreProvider25RefreshFilteredCartOffersEv");
extern "C" long _stub_fn_862() { return 0; }
// HamStoreProvider::Refresh()
extern "C" __attribute__((weak, used)) long _stub_fn_863() __asm__("_ZN16HamStoreProvider7RefreshEv");
extern "C" long _stub_fn_863() { return 0; }
// HamStoreProvider::SetFilter(StoreOffer const*)
extern "C" __attribute__((weak, used)) long _stub_fn_864() __asm__("_ZN16HamStoreProvider9SetFilterEPK10StoreOffer");
extern "C" long _stub_fn_864() { return 0; }
// HamStoreProvider::SetFilter(HamStoreFilter const*)
extern "C" __attribute__((weak, used)) long _stub_fn_865() __asm__("_ZN16HamStoreProvider9SetFilterEPK14HamStoreFilter");
extern "C" long _stub_fn_865() { return 0; }
// KinectSharePanel::KinectSharePanel()
extern "C" __attribute__((weak, used)) long _stub_fn_866() __asm__("_ZN16KinectSharePanelC1Ev");
extern "C" long _stub_fn_866() { return 0; }
// MetaMusicManager::Handle(DataArray*, bool)
extern "C" __attribute__((weak, used)) long _stub_fn_867() __asm__("_ZN16MetaMusicManager6HandleEP9DataArrayb");
extern "C" long _stub_fn_867() { return 0; }
// RndShaderProgram::Cache(ShaderType, ShaderOptions const&, RndShaderBuffer*, RndShaderBuffer*)
extern "C" __attribute__((weak, used)) long _stub_fn_868() __asm__("_ZN16RndShaderProgram5CacheE10ShaderTypeRK13ShaderOptionsP15RndShaderBufferS5_");
extern "C" long _stub_fn_868() { return 0; }
// RndTransformable::ApplyDynamicConstraint()
extern "C" __attribute__((weak, used)) long _stub_fn_869() __asm__("_ZN16RndTransformable22ApplyDynamicConstraintEv");
extern "C" long _stub_fn_869() { return 0; }
// CharLipSyncDriver::Poll()
extern "C" __attribute__((weak, used)) long _stub_fn_870() __asm__("_ZN17CharLipSyncDriver4PollEv");
extern "C" long _stub_fn_870() { return 0; }
// FlowEventListener::Load(BinStream&)
extern "C" __attribute__((weak, used)) long _stub_fn_871() __asm__("_ZN17FlowEventListener4LoadER9BinStream");
extern "C" long _stub_fn_871() { return 0; }
// HollaBackMinigame::OnBeat()
extern "C" __attribute__((weak, used)) long _stub_fn_872() __asm__("_ZN17HollaBackMinigame6OnBeatEv");
extern "C" long _stub_fn_872() { return 0; }
// MoveAsyncDetector::MoveRatingFrac(int, MoveAsyncDetector::RatingBar, HamMove const*)
extern "C" __attribute__((weak, used)) long _stub_fn_873() __asm__("_ZN17MoveAsyncDetector14MoveRatingFracEiNS_9RatingBarEPK7HamMove");
extern "C" long _stub_fn_873() { return 0; }
// MoveAsyncDetector::ClearLoopedRatingFrac(HamMove const*)
extern "C" __attribute__((weak, used)) long _stub_fn_874() __asm__("_ZN17MoveAsyncDetector21ClearLoopedRatingFracEPK7HamMove");
extern "C" long _stub_fn_874() { return 0; }
// NavListHeaderNode::SelectChildren(std::__cxx11::list<NavListSortNode*, std::allocator<NavListSortNode*> >&, int)
extern "C" __attribute__((weak, used)) long _stub_fn_875() __asm__("_ZN17NavListHeaderNode14SelectChildrenERNSt7__cxx114listIP15NavListSortNodeSaIS3_EEEi");
extern "C" long _stub_fn_875() { return 0; }
// NgSpotlightDrawer::RenderScene()
extern "C" __attribute__((weak, used)) long _stub_fn_876() __asm__("_ZN17NgSpotlightDrawer11RenderSceneEv");
extern "C" long _stub_fn_876() { return 0; }
// NgSpotlightDrawer::CheckCam()
extern "C" __attribute__((weak, used)) long _stub_fn_877() __asm__("_ZN17NgSpotlightDrawer8CheckCamEv");
extern "C" long _stub_fn_877() { return 0; }
// NgSpotlightDrawer::CheckRTs(NgSpotlightDrawer::SpotlightResources*)
extern "C" __attribute__((weak, used)) long _stub_fn_878() __asm__("_ZN17NgSpotlightDrawer8CheckRTsEPNS_18SpotlightResourcesE");
extern "C" long _stub_fn_878() { return 0; }
// SingleItemEnumJob::IsFinished()
extern "C" __attribute__((weak, used)) long _stub_fn_880() __asm__("_ZN17SingleItemEnumJob10IsFinishedEv");
extern "C" long _stub_fn_880() { return 0; }
// SingleItemEnumJob::OnCompletion(Hmx::Object*)
extern "C" __attribute__((weak, used)) long _stub_fn_881() __asm__("_ZN17SingleItemEnumJob12OnCompletionEPN3Hmx6ObjectE");
extern "C" long _stub_fn_881() { return 0; }
// SingleItemEnumJob::Start()
extern "C" __attribute__((weak, used)) long _stub_fn_882() __asm__("_ZN17SingleItemEnumJob5StartEv");
extern "C" long _stub_fn_882() { return 0; }
// SingleItemEnumJob::Cancel(Hmx::Object*)
extern "C" __attribute__((weak, used)) long _stub_fn_883() __asm__("_ZN17SingleItemEnumJob6CancelEPN3Hmx6ObjectE");
extern "C" long _stub_fn_883() { return 0; }
// SingleItemEnumJob::SingleItemEnumJob(Hmx::Object*, int, unsigned long long)
extern "C" __attribute__((weak, used)) long _stub_fn_884() __asm__("_ZN17SingleItemEnumJobC1EPN3Hmx6ObjectEiy");
extern "C" long _stub_fn_884() { return 0; }
// SingleItemEnumJob::SingleItemEnumJob(Hmx::Object*, int, unsigned long long)
extern "C" __attribute__((weak, used)) long _stub_fn_885() __asm__("_ZN17SingleItemEnumJobC2EPN3Hmx6ObjectEiy");
extern "C" long _stub_fn_885() { return 0; }
// SingleItemEnumJob::~SingleItemEnumJob()
extern "C" __attribute__((weak, used)) long _stub_fn_886() __asm__("_ZN17SingleItemEnumJobD2Ev");
extern "C" long _stub_fn_886() { return 0; }
// SkeletonRecoverer::WaitingToRecover()
extern "C" __attribute__((weak, used)) long _stub_fn_887() __asm__("_ZN17SkeletonRecoverer16WaitingToRecoverEv");
extern "C" long _stub_fn_887() { return 0; }
// SkeletonRecoverer::GetTrackingIDWithRecovery(int, int)
extern "C" __attribute__((weak, used)) long _stub_fn_888() __asm__("_ZN17SkeletonRecoverer25GetTrackingIDWithRecoveryEii");
extern "C" long _stub_fn_888() { return 0; }
// Vector2DESmoother::Smooth(Vector2, float, bool)
extern "C" __attribute__((weak, used)) long _stub_fn_889() __asm__("_ZN17Vector2DESmoother6SmoothE7Vector2fb");
extern "C" long _stub_fn_889() { return 0; }
// Vector3DESmoother::Smooth(Vector3, float, bool)
extern "C" __attribute__((weak, used)) long _stub_fn_890() __asm__("_ZN17Vector3DESmoother6SmoothE7Vector3fb");
extern "C" long _stub_fn_890() { return 0; }
// CharPollableSorter::Sort(std::vector<RndPollable*, std::allocator<RndPollable*> >&)
extern "C" __attribute__((weak, used)) long _stub_fn_891() __asm__("_ZN18CharPollableSorter4SortERSt6vectorIP11RndPollableSaIS2_EE");
extern "C" long _stub_fn_891() { return 0; }
// LabelShrinkWrapper::OldResourcePreload(BinStream&)
extern "C" __attribute__((weak, used)) long _stub_fn_892() __asm__("_ZN18LabelShrinkWrapper18OldResourcePreloadER9BinStream");
extern "C" long _stub_fn_892() { return 0; }
// LabelShrinkWrapper::UpdateAndDrawWrapper()
extern "C" __attribute__((weak, used)) long _stub_fn_893() __asm__("_ZN18LabelShrinkWrapper20UpdateAndDrawWrapperEv");
extern "C" long _stub_fn_893() { return 0; }
// LabelShrinkWrapper::Poll()
extern "C" __attribute__((weak, used)) long _stub_fn_894() __asm__("_ZN18LabelShrinkWrapper4PollEv");
extern "C" long _stub_fn_894() { return 0; }
// LockedContentPanel::FinishLoad()
extern "C" __attribute__((weak, used)) long _stub_fn_895() __asm__("_ZN18LockedContentPanel10FinishLoadEv");
extern "C" long _stub_fn_895() { return 0; }
// PhysMemTypeTracker::PhysMemTypeTracker(Symbol)
extern "C" __attribute__((weak, used)) long _stub_fn_896() __asm__("_ZN18PhysMemTypeTrackerC1E6Symbol");
extern "C" long _stub_fn_896() { return 0; }
// PhysMemTypeTracker::~PhysMemTypeTracker()
extern "C" __attribute__((weak, used)) long _stub_fn_897() __asm__("_ZN18PhysMemTypeTrackerD1Ev");
extern "C" long _stub_fn_897() { return 0; }
// PlaylistSortByType::PlaylistSortByType()
extern "C" __attribute__((weak, used)) long _stub_fn_898() __asm__("_ZN18PlaylistSortByTypeC1Ev");
extern "C" long _stub_fn_898() { return 0; }
// PseudoRandomPicker<Symbol>::GetItem(int)
extern "C" __attribute__((weak, used)) long _stub_fn_899() __asm__("_ZN18PseudoRandomPickerI6SymbolE7GetItemEi");
extern "C" long _stub_fn_899() { return 0; }
// PseudoRandomPicker<Symbol>::Randomize()
extern "C" __attribute__((weak, used)) long _stub_fn_900() __asm__("_ZN18PseudoRandomPickerI6SymbolE9RandomizeEv");
extern "C" long _stub_fn_900() { return 0; }
// PseudoRandomPicker<int>::GetItem(int)
extern "C" __attribute__((weak, used)) long _stub_fn_901() __asm__("_ZN18PseudoRandomPickerIiE7GetItemEi");
extern "C" long _stub_fn_901() { return 0; }
// RhythmBattlePlayer::AnimateBoxyState(int, bool, bool)
extern "C" __attribute__((weak, used)) long _stub_fn_902() __asm__("_ZN18RhythmBattlePlayer16AnimateBoxyStateEibb");
extern "C" long _stub_fn_902() { return 0; }
// RhythmBattlePlayer::UpdateComboProgress()
extern "C" __attribute__((weak, used)) long _stub_fn_903() __asm__("_ZN18RhythmBattlePlayer19UpdateComboProgressEv");
extern "C" long _stub_fn_903() { return 0; }
// SkeletonIdentifier::OnMsg(SigninChangedMsg const&)
extern "C" __attribute__((weak, used)) long _stub_fn_904() __asm__("_ZN18SkeletonIdentifier5OnMsgERK16SigninChangedMsg");
extern "C" long _stub_fn_904() { return 0; }
// SkeletonIdentifier::OnMsg(SkeletonIdentifiedMsg const&)
extern "C" __attribute__((weak, used)) long _stub_fn_905() __asm__("_ZN18SkeletonIdentifier5OnMsgERK21SkeletonIdentifiedMsg");
extern "C" long _stub_fn_905() { return 0; }
// SkeletonIdentifier::DrawDebug()
extern "C" __attribute__((weak, used)) long _stub_fn_906() __asm__("_ZN18SkeletonIdentifier9DrawDebugEv");
extern "C" long _stub_fn_906() { return 0; }
// ThreeDSoundManager::Poll()
extern "C" __attribute__((weak, used)) long _stub_fn_907() __asm__("_ZN18ThreeDSoundManager4PollEv");
extern "C" long _stub_fn_907() { return 0; }
// ChallengeHeaderNode::GetAlbumArtPath()
extern "C" __attribute__((weak, used)) long _stub_fn_908() __asm__("_ZN19ChallengeHeaderNode15GetAlbumArtPathEv");
extern "C" long _stub_fn_908() { return 0; }
// ChallengeHeaderNode::GetSongShortTitle()
extern "C" __attribute__((weak, used)) long _stub_fn_909() __asm__("_ZN19ChallengeHeaderNode17GetSongShortTitleEv");
extern "C" long _stub_fn_909() { return 0; }
// ChallengeHeaderNode::GetTotalEarnedExp(int)
extern "C" __attribute__((weak, used)) long _stub_fn_910() __asm__("_ZN19ChallengeHeaderNode17GetTotalEarnedExpEi");
extern "C" long _stub_fn_910() { return 0; }
// ChallengeHeaderNode::GetPotentialChallengeExp(NavListSortNode*)
extern "C" __attribute__((weak, used)) long _stub_fn_911() __asm__("_ZN19ChallengeHeaderNode24GetPotentialChallengeExpEP15NavListSortNode");
extern "C" long _stub_fn_911() { return 0; }
// ChallengeHeaderNode::Select()
extern "C" __attribute__((weak, used)) long _stub_fn_912() __asm__("_ZN19ChallengeHeaderNode6SelectEv");
extern "C" long _stub_fn_912() { return 0; }
// DrivenPropertyEntry::Load(BinStream&, FlowNode*)
extern "C" __attribute__((weak, used)) long _stub_fn_913() __asm__("_ZN19DrivenPropertyEntry4LoadER9BinStreamP8FlowNode");
extern "C" long _stub_fn_913() { return 0; }
// NavListFunctionNode::NavListFunctionNode(NavListItemSortCmp*, Symbol, char const*)
extern "C" __attribute__((weak, used)) long _stub_fn_914() __asm__("_ZN19NavListFunctionNodeC2EP18NavListItemSortCmp6SymbolPKc");
extern "C" long _stub_fn_914() { return 0; }
// RndAmbientOcclusion::Tessellate(float*, float*)
extern "C" __attribute__((weak, used)) long _stub_fn_915() __asm__("_ZN19RndAmbientOcclusion10TessellateEPfS0_");
extern "C" long _stub_fn_915() { return 0; }
// RndAmbientOcclusion::CalculateAO(float*)
extern "C" __attribute__((weak, used)) long _stub_fn_916() __asm__("_ZN19RndAmbientOcclusion11CalculateAOEPf");
extern "C" long _stub_fn_916() { return 0; }
// ChallengeResultPanel::OnMsg(UIComponentScrollMsg const&)
extern "C" __attribute__((weak, used)) long _stub_fn_917() __asm__("_ZN20ChallengeResultPanel5OnMsgERK20UIComponentScrollMsg");
extern "C" long _stub_fn_917() { return 0; }
// HamSkeletonConverter::Set(BaseSkeleton const*)
extern "C" __attribute__((weak, used)) long _stub_fn_918() __asm__("_ZN20HamSkeletonConverter3SetEPK12BaseSkeleton");
extern "C" long _stub_fn_918() { return 0; }
// UpdateFriendsListJob::OnMsg(PlatformMgrOpCompleteMsg const&)
extern "C" __attribute__((weak, used)) long _stub_fn_919() __asm__("_ZN20UpdateFriendsListJob5OnMsgERK24PlatformMgrOpCompleteMsg");
extern "C" long _stub_fn_919() { return 0; }
// DefaultPhysicsManager::AddCollidable(Hmx::Object*, ObjectDir*, bool)
extern "C" __attribute__((weak, used)) long _stub_fn_920() __asm__("_ZN21DefaultPhysicsManager13AddCollidableEPN3Hmx6ObjectEP9ObjectDirb");
extern "C" long _stub_fn_920() { return 0; }
// DefaultPhysicsManager::RemoveCollidable(Hmx::Object*)
extern "C" __attribute__((weak, used)) long _stub_fn_921() __asm__("_ZN21DefaultPhysicsManager16RemoveCollidableEPN3Hmx6ObjectE");
extern "C" long _stub_fn_921() { return 0; }
// DefaultPhysicsManager::DeactivateCollidable(Hmx::Object*)
extern "C" __attribute__((weak, used)) long _stub_fn_922() __asm__("_ZN21DefaultPhysicsManager20DeactivateCollidableEPN3Hmx6ObjectE");
extern "C" long _stub_fn_922() { return 0; }
// FitnessCalorieSortMgr::Handle(DataArray*, bool)
extern "C" __attribute__((weak, used)) long _stub_fn_923() __asm__("_ZN21FitnessCalorieSortMgr6HandleEP9DataArrayb");
extern "C" long _stub_fn_923() { return 0; }
// FreestyleMoveRecorder::StopRecording()
extern "C" __attribute__((weak, used)) long _stub_fn_924() __asm__("_ZN21FreestyleMoveRecorder13StopRecordingEv");
extern "C" long _stub_fn_924() { return 0; }
// FreestyleMoveRecorder::GetLiveSkeleton()
extern "C" __attribute__((weak, used)) long _stub_fn_925() __asm__("_ZN21FreestyleMoveRecorder15GetLiveSkeletonEv");
extern "C" long _stub_fn_925() { return 0; }
// FreestyleMoveRecorder::Poll()
extern "C" __attribute__((weak, used)) long _stub_fn_926() __asm__("_ZN21FreestyleMoveRecorder4PollEv");
extern "C" long _stub_fn_926() { return 0; }
// FreestyleMoveRecorder::GetScore(BaseSkeleton const*, int, float, bool)
extern "C" __attribute__((weak, used)) long _stub_fn_927() __asm__("_ZN21FreestyleMoveRecorder8GetScoreEPK12BaseSkeletonifb");
extern "C" long _stub_fn_927() { return 0; }
// FreestyleMoveRecorder::DrawDebug()
extern "C" __attribute__((weak, used)) long _stub_fn_928() __asm__("_ZN21FreestyleMoveRecorder9DrawDebugEv");
extern "C" long _stub_fn_928() { return 0; }
// GameEndedDataPointJob::GetXUIDStrFromProfile(HamProfile*)
extern "C" __attribute__((weak, used)) long _stub_fn_929() __asm__("_ZN21GameEndedDataPointJob21GetXUIDStrFromProfileEP10HamProfile");
extern "C" long _stub_fn_929() { return 0; }
// KinectShareConnection::Poll()
extern "C" __attribute__((weak, used)) long _stub_fn_930() __asm__("_ZN21KinectShareConnection4PollEv");
extern "C" long _stub_fn_930() { return 0; }
// KinectShareConnection::~KinectShareConnection()
extern "C" __attribute__((weak, used)) long _stub_fn_931() __asm__("_ZN21KinectShareConnectionD1Ev");
extern "C" long _stub_fn_931() { return 0; }
// MultiUserGesturePanel::UpdateCharPic(UIPicture*, int, int, Symbol, Symbol)
extern "C" __attribute__((weak, used)) long _stub_fn_932() __asm__("_ZN21MultiUserGesturePanel13UpdateCharPicEP9UIPictureii6SymbolS2_");
extern "C" long _stub_fn_932() { return 0; }
// MultiUserGesturePanel::UpdateVenueMesh(RndMesh*, int, int, Symbol, Symbol)
extern "C" __attribute__((weak, used)) long _stub_fn_933() __asm__("_ZN21MultiUserGesturePanel15UpdateVenueMeshEP7RndMeshii6SymbolS2_");
extern "C" long _stub_fn_933() { return 0; }
// MultiUserGesturePanel::GetVoiceCommandOutfitTag(int, Symbol)
extern "C" __attribute__((weak, used)) long _stub_fn_934() __asm__("_ZN21MultiUserGesturePanel24GetVoiceCommandOutfitTagEi6Symbol");
extern "C" long _stub_fn_934() { return 0; }
// MultiUserGesturePanel::UpdateProviderPlayerIndices()
extern "C" __attribute__((weak, used)) long _stub_fn_935() __asm__("_ZN21MultiUserGesturePanel27UpdateProviderPlayerIndicesEv");
extern "C" long _stub_fn_935() { return 0; }
// MultiUserGesturePanel::Enter()
extern "C" __attribute__((weak, used)) long _stub_fn_936() __asm__("_ZN21MultiUserGesturePanel5EnterEv");
extern "C" long _stub_fn_936() { return 0; }
// RndSoftParticleBuffer::Queue(RndDrawable*, BaseMaterial::Blend)
extern "C" __attribute__((weak, used)) long _stub_fn_937() __asm__("_ZN21RndSoftParticleBuffer5QueueEP11RndDrawableN12BaseMaterial5BlendE");
extern "C" long _stub_fn_937() { return 0; }
// RndSoftParticleBuffer::DoPost()
extern "C" __attribute__((weak, used)) long _stub_fn_938() __asm__("_ZN21RndSoftParticleBuffer6DoPostEv");
extern "C" long _stub_fn_938() { return 0; }
// HamListRibbonDrawState::HamListRibbonDrawState()
extern "C" __attribute__((weak, used)) long _stub_fn_939() __asm__("_ZN22HamListRibbonDrawStateC1Ev");
extern "C" long _stub_fn_939() { return 0; }
// WorldCrowd3DCharHandle::SyncProperty(DataNode&, DataArray*, int, PropOp)
extern "C" __attribute__((weak, used)) long _stub_fn_940() __asm__("_ZN22WorldCrowd3DCharHandle12SyncPropertyER8DataNodeP9DataArrayi6PropOp");
extern "C" long _stub_fn_940() { return 0; }
// HamScrollSpeedIndicator::Update(float, float, float)
extern "C" __attribute__((weak, used)) long _stub_fn_941() __asm__("_ZN23HamScrollSpeedIndicator6UpdateEfff");
extern "C" long _stub_fn_941() { return 0; }
// HandInvokeGestureFilter::Update(Skeleton const&, int)
extern "C" __attribute__((weak, used)) long _stub_fn_942() __asm__("_ZN23HandInvokeGestureFilter6UpdateERK8Skeletoni");
extern "C" long _stub_fn_942() { return 0; }
// HandRaisedGestureFilter::Update(Skeleton const&, int)
extern "C" __attribute__((weak, used)) long _stub_fn_943() __asm__("_ZN23HandRaisedGestureFilter6UpdateERK8Skeletoni");
extern "C" long _stub_fn_943() { return 0; }
// FitnessCalorieHeaderNode::GetFirstActive()
extern "C" __attribute__((weak, used)) long _stub_fn_944() __asm__("_ZN24FitnessCalorieHeaderNode14GetFirstActiveEv");
extern "C" long _stub_fn_944() { return 0; }
// PhotoSpotlightPositioner::Poll()
extern "C" __attribute__((weak, used)) long _stub_fn_945() __asm__("_ZN24PhotoSpotlightPositioner4PollEv");
extern "C" long _stub_fn_945() { return 0; }
// AppMiniLeaderboardDisplay::UpdateSelfInRows()
extern "C" __attribute__((weak, used)) long _stub_fn_946() __asm__("_ZN25AppMiniLeaderboardDisplay16UpdateSelfInRowsEv");
extern "C" long _stub_fn_946() { return 0; }
// SingleUserCrewSelectPanel::UpdateCrewMesh(RndMesh*, int, Symbol)
extern "C" __attribute__((weak, used)) long _stub_fn_947() __asm__("_ZN25SingleUserCrewSelectPanel14UpdateCrewMeshEP7RndMeshi6Symbol");
extern "C" long _stub_fn_947() { return 0; }
// StandingStillGestureFilter::Update(Skeleton const&, int)
extern "C" __attribute__((weak, used)) long _stub_fn_948() __asm__("_ZN26StandingStillGestureFilter6UpdateERK8Skeletoni");
extern "C" long _stub_fn_948() { return 0; }
// CampaignMasterQuestSongSelectPanel::OnHighlightHeader()
extern "C" __attribute__((weak, used)) long _stub_fn_949() __asm__("_ZN34CampaignMasterQuestSongSelectPanel17OnHighlightHeaderEv");
extern "C" long _stub_fn_949() { return 0; }
// Hmx::CRC::ValidateCRC(int, char const*)
extern "C" __attribute__((weak, used)) long _stub_fn_950() __asm__("_ZN3Hmx3CRC11ValidateCRCEiPKc");
extern "C" long _stub_fn_950() { return 0; }
// Hmx::Object::ClearAllTypeProps()
extern "C" __attribute__((weak, used)) long _stub_fn_951() __asm__("_ZN3Hmx6Object17ClearAllTypePropsEv");
extern "C" long _stub_fn_951() { return 0; }
// Hmx::operator*(Transform const&, Hmx::Matrix4 const&)
extern "C" __attribute__((weak, used)) long _stub_fn_952() __asm__("_ZN3HmxmlERK9TransformRKNS_7Matrix4E");
extern "C" long _stub_fn_952() { return 0; }
// Rnd::DrawTimers(float)
extern "C" __attribute__((weak, used)) long _stub_fn_953() __asm__("_ZN3Rnd10DrawTimersEf");
extern "C" long _stub_fn_953() { return 0; }
// Rnd::UpdateRate()
extern "C" __attribute__((weak, used)) long _stub_fn_954() __asm__("_ZN3Rnd10UpdateRateEv");
extern "C" long _stub_fn_954() { return 0; }
// Rnd::DrawPreClear()
extern "C" __attribute__((weak, used)) long _stub_fn_955() __asm__("_ZN3Rnd12DrawPreClearEv");
extern "C" long _stub_fn_955() { return 0; }
// Rnd::OnToggleHeap(DataArray const*)
extern "C" __attribute__((weak, used)) long _stub_fn_956() __asm__("_ZN3Rnd12OnToggleHeapEPK9DataArray");
extern "C" long _stub_fn_956() { return 0; }
// Rnd::CreateDefaultTexture(Rnd::DefaultTextureType)
extern "C" __attribute__((weak, used)) long _stub_fn_957() __asm__("_ZN3Rnd20CreateDefaultTextureENS_18DefaultTextureTypeE");
extern "C" long _stub_fn_957() { return 0; }
// Flow::Copy(Hmx::Object const*, Hmx::Object::CopyType)
extern "C" __attribute__((weak, used)) long _stub_fn_958() __asm__("_ZN4Flow4CopyEPKN3Hmx6ObjectENS1_8CopyTypeE");
extern "C" long _stub_fn_958() { return 0; }
// Flow::Exit()
extern "C" __attribute__((weak, used)) long _stub_fn_959() __asm__("_ZN4Flow4ExitEv");
extern "C" long _stub_fn_959() { return 0; }
// Flow::Enter()
extern "C" __attribute__((weak, used)) long _stub_fn_960() __asm__("_ZN4Flow5EnterEv");
extern "C" long _stub_fn_960() { return 0; }
// Game::Poll()
extern "C" __attribute__((weak, used)) long _stub_fn_961() __asm__("_ZN4Game4PollEv");
extern "C" long _stub_fn_961() { return 0; }
// Pose::Update(Skeleton const&)
extern "C" __attribute__((weak, used)) long _stub_fn_962() __asm__("_ZN4Pose6UpdateERK8Skeleton");
extern "C" long _stub_fn_962() { return 0; }
// Rand::Int(int, int) - now implemented in Rand.cpp
// Rand::Int() - now implemented in Rand.cpp
// Song::SyncState()
extern "C" __attribute__((weak, used)) long _stub_fn_965() __asm__("_ZN4Song9SyncStateEv");
extern "C" long _stub_fn_965() { return 0; }
// Debug::Modal(Debug::ModalType&, char const*, void*)
extern "C" __attribute__((weak, used)) long _stub_fn_966() __asm__("_ZN5Debug5ModalERNS_9ModalTypeEPKcPv");
extern "C" long _stub_fn_966() { return 0; }
// DxTex::SetDeviceTex(D3DTexture*)
extern "C" __attribute__((weak, used)) long _stub_fn_967() __asm__("_ZN5DxTex12SetDeviceTexEP10D3DTexture");
extern "C" long _stub_fn_967() { return 0; }
// HamUI::OnMsg(ConnectionStatusChangedMsg const&)
extern "C" __attribute__((weak, used)) long _stub_fn_968() __asm__("_ZN5HamUI5OnMsgERK26ConnectionStatusChangedMsg");
extern "C" long _stub_fn_968() { return 0; }
// NgMat::RefreshState()
extern "C" __attribute__((weak, used)) long _stub_fn_969() __asm__("_ZN5NgMat12RefreshStateEv");
extern "C" long _stub_fn_969() { return 0; }
// NgMat::SetRegularShaderConst(bool)
extern "C" __attribute__((weak, used)) long _stub_fn_970() __asm__("_ZN5NgMat21SetRegularShaderConstEb");
extern "C" long _stub_fn_970() { return 0; }
// Sound::SetPan(float, Hmx::Object*)
extern "C" __attribute__((weak, used)) long _stub_fn_971() __asm__("_ZN5Sound6SetPanEfPN3Hmx6ObjectE");
extern "C" long _stub_fn_971() { return 0; }
// Sound::SetSpeed(float, Hmx::Object*)
extern "C" __attribute__((weak, used)) long _stub_fn_972() __asm__("_ZN5Sound8SetSpeedEfPN3Hmx6ObjectE");
extern "C" long _stub_fn_972() { return 0; }
// Synth::NewBufStream(void const*, int, Symbol, float, bool)
extern "C" __attribute__((weak, used)) long _stub_fn_973() __asm__("_ZN5Synth12NewBufStreamEPKvi6Symbolfb");
extern "C" long _stub_fn_973() { return 0; }
// Synth::UpdateOverlay(RndOverlay*, float)
extern "C" __attribute__((weak, used)) long _stub_fn_974() __asm__("_ZN5Synth13UpdateOverlayEP10RndOverlayf");
extern "C" long _stub_fn_974() { return 0; }
// Synth::NewStream(char const*, float, float, bool)
extern "C" __attribute__((weak, used)) long _stub_fn_975() __asm__("_ZN5Synth9NewStreamEPKcffb");
extern "C" long _stub_fn_975() { return 0; }
// DxMesh::GetMultimeshFaces()
extern "C" __attribute__((weak, used)) long _stub_fn_976() __asm__("_ZN6DxMesh17GetMultimeshFacesEv");
extern "C" long _stub_fn_976() { return 0; }
// kdTree<Triangle>::kdTreeNode::FindSplit_SAH(Box const&, std::__cxx11::list<Triangle*, std::allocator<Triangle*> > const&)
extern "C" __attribute__((weak, used)) long _stub_fn_977() __asm__("_ZN6kdTreeI8TriangleE10kdTreeNode13FindSplit_SAHERK3BoxRKNSt7__cxx114listIPS0_SaIS8_EEE");
extern "C" long _stub_fn_977() { return 0; }
// RndCam::UpdateLocal()
extern "C" __attribute__((weak, used)) long _stub_fn_978() __asm__("_ZN6RndCam11UpdateLocalEv");
extern "C" long _stub_fn_978() { return 0; }
// RndCam::GetCamFrustum(Vector3&, Vector3 (&) [4])
extern "C" __attribute__((weak, used)) long _stub_fn_979() __asm__("_ZN6RndCam13GetCamFrustumER7Vector3RA4_S0_");
extern "C" long _stub_fn_979() { return 0; }
// RndTex::SyncBitmap()
extern "C" __attribute__((weak, used)) long _stub_fn_980() __asm__("_ZN6RndTex10SyncBitmapEv");
extern "C" long _stub_fn_980() { return 0; }
// RndTex::OnSetBitmap(DataArray const*)
extern "C" __attribute__((weak, used)) long _stub_fn_981() __asm__("_ZN6RndTex11OnSetBitmapEPK9DataArray");
extern "C" long _stub_fn_981() { return 0; }
// RndTex::PresyncBitmap()
extern "C" __attribute__((weak, used)) long _stub_fn_982() __asm__("_ZN6RndTex13PresyncBitmapEv");
extern "C" long _stub_fn_982() { return 0; }
// RndTex::Load, PreLoad, PostLoad: provided by RndTex_Native.cpp
// RndTex::CheckSize(int, int, int, int, RndTex::Type, bool)
extern "C" __attribute__((weak, used)) long _stub_fn_986() __asm__("_ZN6RndTex9CheckSizeEiiiiNS_4TypeEb");
extern "C" long _stub_fn_986() { return 0; }
// RndTex::SetBitmap(int, int, int, RndTex::Type, bool, char const*)
extern "C" __attribute__((weak, used)) long _stub_fn_987() __asm__("_ZN6RndTex9SetBitmapEiiiNS_4TypeEbPKc");
extern "C" long _stub_fn_987() { return 0; }
// RndTex::SetBitmap(RndBitmap const&, char const*, bool, RndTex::Type)
extern "C" __attribute__((weak, used)) long _stub_fn_988() __asm__("_ZN6RndTex9SetBitmapERK9RndBitmapPKcbNS_4TypeE");
extern "C" long _stub_fn_988() { return 0; }
// WavMgr::SyncProperty(DataNode&, DataArray*, int, PropOp)
extern "C" __attribute__((weak, used)) long _stub_fn_989() __asm__("_ZN6WavMgr12SyncPropertyER8DataNodeP9DataArrayi6PropOp");
extern "C" long _stub_fn_989() { return 0; }
// CamShot::GetKey(float, CamShotFrame*&, CamShotFrame*&, float&)
extern "C" __attribute__((weak, used)) long _stub_fn_990() __asm__("_ZN7CamShot6GetKeyEfRP12CamShotFrameS2_Rf");
extern "C" long _stub_fn_990() { return 0; }
// CamShot::SetPos(CamShotFrame&, RndCam*)
extern "C" __attribute__((weak, used)) long _stub_fn_991() __asm__("_ZN7CamShot6SetPosER12CamShotFrameP6RndCam");
extern "C" long _stub_fn_991() { return 0; }
// FlowPtr<Hmx::Object>::FlowPtr(FlowPtr<Hmx::Object> const&)
extern "C" __attribute__((weak, used)) long _stub_fn_992() __asm__("_ZN7FlowPtrIN3Hmx6ObjectEEC1ERKS2_");
extern "C" long _stub_fn_992() { return 0; }
// FlowRun::ResolveTarget()
extern "C" __attribute__((weak, used)) long _stub_fn_993() __asm__("_ZN7FlowRun13ResolveTargetEv");
extern "C" long _stub_fn_993() { return 0; }
// FlowRun::Activate()
extern "C" __attribute__((weak, used)) long _stub_fn_994() __asm__("_ZN7FlowRun8ActivateEv");
extern "C" long _stub_fn_994() { return 0; }
// HDCache::WriteAsync(int, int, void const*)
extern "C" __attribute__((weak, used)) long _stub_fn_995() __asm__("_ZN7HDCache10WriteAsyncEiiPKv");
extern "C" long _stub_fn_995() { return 0; }
// HDCache::Flush()
extern "C" __attribute__((weak, used)) long _stub_fn_996() __asm__("_ZN7HDCache5FlushEv");
extern "C" long _stub_fn_996() { return 0; }
// LoadMgr::PollFrontLoader() - now implemented in Loader.cpp
// MemHeap::Free(int*)
extern "C" __attribute__((weak, used)) long _stub_fn_998() __asm__("_ZN7MemHeap4FreeEPi");
extern "C" long _stub_fn_998() { return 0; }
// MemHeap::Truncate(int*, int, int&)
extern "C" __attribute__((weak, used)) long _stub_fn_999() __asm__("_ZN7MemHeap8TruncateEPiiRi");
extern "C" long _stub_fn_999() { return 0; }
// MicNull::GetContinuousBuf(int&)
extern "C" __attribute__((weak, used)) long _stub_fn_1000() __asm__("_ZN7MicNull16GetContinuousBufERi");
extern "C" long _stub_fn_1000() { return 0; }
// MoveDir::DetectFrac(int, int)
extern "C" __attribute__((weak, used)) long _stub_fn_1001() __asm__("_ZN7MoveDir10DetectFracEii");
extern "C" long _stub_fn_1001() { return 0; }
// MoveDir::DrawShowing()
extern "C" __attribute__((weak, used)) long _stub_fn_1002() __asm__("_ZN7MoveDir11DrawShowingEv");
extern "C" long _stub_fn_1002() { return 0; }
// MoveDir::UpdateOverlay(RndOverlay*, float)
extern "C" __attribute__((weak, used)) long _stub_fn_1003() __asm__("_ZN7MoveDir13UpdateOverlayEP10RndOverlayf");
extern "C" long _stub_fn_1003() { return 0; }
// MoveDir::PostUpdateFilters()
extern "C" __attribute__((weak, used)) long _stub_fn_1004() __asm__("_ZN7MoveDir17PostUpdateFiltersEv");
extern "C" long _stub_fn_1004() { return 0; }
// MoveDir::ResetDetectFrames(int, Difficulty)
extern "C" __attribute__((weak, used)) long _stub_fn_1005() __asm__("_ZN7MoveDir17ResetDetectFramesEi10Difficulty");
extern "C" long _stub_fn_1005() { return 0; }
// MoveDir::EnqueueDetectFrames(float, int, std::vector<DetectFrame, std::allocator<DetectFrame> >&, FilterVersion const*)
extern "C" __attribute__((weak, used)) long _stub_fn_1006() __asm__("_ZN7MoveDir19EnqueueDetectFramesEfiRSt6vectorI11DetectFrameSaIS1_EEPK13FilterVersion");
extern "C" long _stub_fn_1006() { return 0; }
// MoveDir::FinalPoseStateMachine()
extern "C" __attribute__((weak, used)) long _stub_fn_1007() __asm__("_ZN7MoveDir21FinalPoseStateMachineEv");
extern "C" long _stub_fn_1007() { return 0; }
// MoveMgr::FillInRoutineAt(int, int)
extern "C" __attribute__((weak, used)) long _stub_fn_1008() __asm__("_ZN7MoveMgr15FillInRoutineAtEii");
extern "C" long _stub_fn_1008() { return 0; }
// MoveMgr::ComputeLoadedMoveSet()
extern "C" __attribute__((weak, used)) long _stub_fn_1009() __asm__("_ZN7MoveMgr20ComputeLoadedMoveSetEv");
extern "C" long _stub_fn_1009() { return 0; }
// MoveMgr::FillRoutineFromVerses(int)
extern "C" __attribute__((weak, used)) long _stub_fn_1010() __asm__("_ZN7MoveMgr21FillRoutineFromVersesEi");
extern "C" long _stub_fn_1010() { return 0; }
// MoveMgr::ComputeRandomChoiceSet(int)
extern "C" __attribute__((weak, used)) long _stub_fn_1011() __asm__("_ZN7MoveMgr22ComputeRandomChoiceSetEi");
extern "C" long _stub_fn_1011() { return 0; }
// MoveMgr::FillRoutineFromReplacer(int)
extern "C" __attribute__((weak, used)) long _stub_fn_1012() __asm__("_ZN7MoveMgr23FillRoutineFromReplacerEi");
extern "C" long _stub_fn_1012() { return 0; }
// NgLight::BlurShadowRT()
extern "C" __attribute__((weak, used)) long _stub_fn_1013() __asm__("_ZN7NgLight12BlurShadowRTEv");
extern "C" long _stub_fn_1013() { return 0; }
// NgLight::RenderShadows(std::vector<RndDrawable*, std::allocator<RndDrawable*> >&)
extern "C" __attribute__((weak, used)) long _stub_fn_1014() __asm__("_ZN7NgLight13RenderShadowsERSt6vectorIP11RndDrawableSaIS2_EE");
extern "C" long _stub_fn_1014() { return 0; }
// NgLight::CheckShadowMap()
extern "C" __attribute__((weak, used)) long _stub_fn_1015() __asm__("_ZN7NgLight14CheckShadowMapEv");
extern "C" long _stub_fn_1015() { return 0; }
// NgLight::SphereConeTest(Vector3 const&, float)
extern "C" __attribute__((weak, used)) long _stub_fn_1016() __asm__("_ZN7NgLight14SphereConeTestERK7Vector3f");
extern "C" long _stub_fn_1016() { return 0; }
// NgLight::SetAndClearShadowViewport()
extern "C" __attribute__((weak, used)) long _stub_fn_1017() __asm__("_ZN7NgLight25SetAndClearShadowViewportEv");
extern "C" long _stub_fn_1017() { return 0; }
// Profile::MakeDirty()
extern "C" __attribute__((weak, used)) long _stub_fn_1018() __asm__("_ZN7Profile9MakeDirtyEv");
extern "C" long _stub_fn_1018() { return 0; }
// QuatXfm::QuatXfm(Transform const&)
extern "C" __attribute__((weak, used)) long _stub_fn_1019() __asm__("_ZN7QuatXfmC1ERK9Transform");
extern "C" long _stub_fn_1019() { return 0; }
// RndFont::UpdateChars()
extern "C" __attribute__((weak, used)) long _stub_fn_1020() __asm__("_ZN7RndFont11UpdateCharsEv");
extern "C" long _stub_fn_1020() { return 0; }
// RndFont::BleedTest()
extern "C" __attribute__((weak, used)) long _stub_fn_1021() __asm__("_ZN7RndFont9BleedTestEv");
extern "C" long _stub_fn_1021() { return 0; }
// RndLine::UpdateLine(Transform const&, float)
extern "C" __attribute__((weak, used)) long _stub_fn_1022() __asm__("_ZN7RndLine10UpdateLineERK9Transformf");
extern "C" long _stub_fn_1022() { return 0; }
// RndLine::SetPointsColor(int, int, Hmx::Color const&)
extern "C" __attribute__((weak, used)) long _stub_fn_1023() __asm__("_ZN7RndLine14SetPointsColorEiiRKN3Hmx5ColorE");
extern "C" long _stub_fn_1023() { return 0; }
// RndLine::MapVerts(int, RndLine::VertsMap&)
extern "C" __attribute__((weak, used)) long _stub_fn_1024() __asm__("_ZN7RndLine8MapVertsEiRNS_8VertsMapE");
extern "C" long _stub_fn_1024() { return 0; }
// RndMesh::SkinVertex(RndMesh::Vert const&, Vector3*)
extern "C" __attribute__((weak, used)) long _stub_fn_1025() __asm__("_ZN7RndMesh10SkinVertexERKNS_4VertEP7Vector3");
extern "C" long _stub_fn_1025() { return 0; }
// RndMesh::DeleteBones(bool)
extern "C" __attribute__((weak, used)) long _stub_fn_1026() __asm__("_ZN7RndMesh11DeleteBonesEb");
extern "C" long _stub_fn_1026() { return 0; }
// RndMesh::LoadVertices(BinStreamRev&)
extern "C" __attribute__((weak, used)) long _stub_fn_1027() __asm__("_ZN7RndMesh12LoadVerticesER12BinStreamRev");
extern "C" long _stub_fn_1027() { return 0; }
// RndMesh::SaveVertices(BinStream&)
extern "C" __attribute__((weak, used)) long _stub_fn_1028() __asm__("_ZN7RndMesh12SaveVerticesER9BinStream");
extern "C" long _stub_fn_1028() { return 0; }
// RndMesh::CollideShowing(Segment const&, float&, Plane&)
extern "C" __attribute__((weak, used)) long _stub_fn_1029() __asm__("_ZN7RndMesh14CollideShowingERK7SegmentRfR5Plane");
extern "C" long _stub_fn_1029() { return 0; }
// RndMesh::OnCompareEdgeVerts(DataArray const*)
extern "C" __attribute__((weak, used)) long _stub_fn_1030() __asm__("_ZN7RndMesh18OnCompareEdgeVertsEPK9DataArray");
extern "C" long _stub_fn_1030() { return 0; }
// RndMesh::InstanceGeomOwnerBones()
extern "C" __attribute__((weak, used)) long _stub_fn_1031() __asm__("_ZN7RndMesh22InstanceGeomOwnerBonesEv");
extern "C" long _stub_fn_1031() { return 0; }
// RndMesh::OnSync(int)
extern "C" __attribute__((weak, used)) long _stub_fn_1032() __asm__("_ZN7RndMesh6OnSyncEi");
extern "C" long _stub_fn_1032() { return 0; }
// RndMesh::Replace(ObjRef*, Hmx::Object*)
extern "C" __attribute__((weak, used)) long _stub_fn_1033() __asm__("_ZN7RndMesh7ReplaceEP6ObjRefPN3Hmx6ObjectE");
extern "C" long _stub_fn_1033() { return 0; }
// RndMesh::SetVolume(RndMesh::Volume)
extern "C" __attribute__((weak, used)) long _stub_fn_1034() __asm__("_ZN7RndMesh9SetVolumeENS_6VolumeE");
extern "C" long _stub_fn_1034() { return 0; }
// RndText::UpdateText()
extern "C" __attribute__((weak, used)) long _stub_fn_1035() __asm__("_ZN7RndText10UpdateTextEv");
extern "C" long _stub_fn_1035() { return 0; }
// RndText::DrawShowing()
extern "C" __attribute__((weak, used)) long _stub_fn_1036() __asm__("_ZN7RndText11DrawShowingEv");
extern "C" long _stub_fn_1036() { return 0; }
// RndText::AcquireFontMap(RndFontBase*)
extern "C" __attribute__((weak, used)) long _stub_fn_1037() __asm__("_ZN7RndText14AcquireFontMapEP11RndFontBase");
extern "C" long _stub_fn_1037() { return 0; }
// RndText::DrawBlacklight()
extern "C" __attribute__((weak, used)) long _stub_fn_1038() __asm__("_ZN7RndText14DrawBlacklightEv");
extern "C" long _stub_fn_1038() { return 0; }
// RndText::MakeWorldSphere(Sphere&, bool)
extern "C" __attribute__((weak, used)) long _stub_fn_1039() __asm__("_ZN7RndText15MakeWorldSphereER6Sphereb");
extern "C" long _stub_fn_1039() { return 0; }
// RndText::ReFitTextScroll(String)
extern "C" __attribute__((weak, used)) long _stub_fn_1040() __asm__("_ZN7RndText15ReFitTextScrollE6String");
extern "C" long _stub_fn_1040() { return 0; }
// RndText::GetDistanceToPlane(Plane const&, Vector3&)
extern "C" __attribute__((weak, used)) long _stub_fn_1041() __asm__("_ZN7RndText18GetDistanceToPlaneERK5PlaneR7Vector3");
extern "C" long _stub_fn_1041() { return 0; }
// RndText::ComputeCharWidthsForText(String)
extern "C" __attribute__((weak, used)) long _stub_fn_1042() __asm__("_ZN7RndText24ComputeCharWidthsForTextE6String");
extern "C" long _stub_fn_1042() { return 0; }
// RndText::FontMap::SetupCharacter(unsigned short, float&, float, RndText::StyleState const&, unsigned short, float, RndText::FitType, float)
extern "C" __attribute__((weak, used)) long _stub_fn_1043() __asm__("_ZN7RndText7FontMap14SetupCharacterEtRffRKNS_10StyleStateEtfNS_7FitTypeEf");
extern "C" long _stub_fn_1043() { return 0; }
// RndText::FontMap3d::AllocateMeshes(RndText*, int)
extern "C" __attribute__((weak, used)) long _stub_fn_1044() __asm__("_ZN7RndText9FontMap3d14AllocateMeshesEPS_i");
extern "C" long _stub_fn_1044() { return 0; }
// RndText::FontMap3d::SetupCharacter(unsigned short, float&, float, RndText::StyleState const&, unsigned short, float, RndText::FitType, float)
extern "C" __attribute__((weak, used)) long _stub_fn_1045() __asm__("_ZN7RndText9FontMap3d14SetupCharacterEtRffRKNS_10StyleStateEtfNS_7FitTypeEf");
extern "C" long _stub_fn_1045() { return 0; }
// RndText::FontMap3d::CleanupSyncMeshes()
extern "C" __attribute__((weak, used)) long _stub_fn_1046() __asm__("_ZN7RndText9FontMap3d17CleanupSyncMeshesEv");
extern "C" long _stub_fn_1046() { return 0; }
// RndText::FontMap3d::IncrementDisplayableChars(unsigned short)
extern "C" __attribute__((weak, used)) long _stub_fn_1047() __asm__("_ZN7RndText9FontMap3d25IncrementDisplayableCharsEt");
extern "C" long _stub_fn_1047() { return 0; }
// SfxInst::SetTranspose(float)
extern "C" __attribute__((weak, used)) long _stub_fn_1048() __asm__("_ZN7SfxInst12SetTransposeEf");
extern "C" long _stub_fn_1048() { return 0; }
// SfxInst::UpdateVolume()
extern "C" __attribute__((weak, used)) long _stub_fn_1049() __asm__("_ZN7SfxInst12UpdateVolumeEv");
extern "C" long _stub_fn_1049() { return 0; }
// SfxInst::SetReverbMixDb(float)
extern "C" __attribute__((weak, used)) long _stub_fn_1050() __asm__("_ZN7SfxInst14SetReverbMixDbEf");
extern "C" long _stub_fn_1050() { return 0; }
// SfxInst::SetReverbEnable(bool)
extern "C" __attribute__((weak, used)) long _stub_fn_1051() __asm__("_ZN7SfxInst15SetReverbEnableEb");
extern "C" long _stub_fn_1051() { return 0; }
// SfxInst::Pause(bool)
extern "C" __attribute__((weak, used)) long _stub_fn_1052() __asm__("_ZN7SfxInst5PauseEb");
extern "C" long _stub_fn_1052() { return 0; }
// SfxInst::SetPan(float)
extern "C" __attribute__((weak, used)) long _stub_fn_1053() __asm__("_ZN7SfxInst6SetPanEf");
extern "C" long _stub_fn_1053() { return 0; }
// SfxInst::SetSend(FxSend*)
extern "C" __attribute__((weak, used)) long _stub_fn_1054() __asm__("_ZN7SfxInst7SetSendEP6FxSend");
extern "C" long _stub_fn_1054() { return 0; }
// SfxInst::StartImpl()
extern "C" __attribute__((weak, used)) long _stub_fn_1055() __asm__("_ZN7SfxInst9StartImplEv");
extern "C" long _stub_fn_1055() { return 0; }
// TaskMgr::Poll()
extern "C" __attribute__((weak, used)) long _stub_fn_1056() __asm__("_ZN7TaskMgr4PollEv");
extern "C" long _stub_fn_1056() { return 0; }
// TexProc::DrawToTexture()
extern "C" __attribute__((weak, used)) long _stub_fn_1057() __asm__("_ZN7TexProc13DrawToTextureEv");
extern "C" long _stub_fn_1057() { return 0; }
// TexProc::Poll()
extern "C" __attribute__((weak, used)) long _stub_fn_1058() __asm__("_ZN7TexProc4PollEv");
extern "C" long _stub_fn_1058() { return 0; }
// UILabel::LabelStyle::~LabelStyle()
extern "C" __attribute__((weak, used)) long _stub_fn_1059() __asm__("_ZN7UILabel10LabelStyleD1Ev");
extern "C" long _stub_fn_1059() { return 0; }
// UILabel::Terminate()
extern "C" __attribute__((weak, used)) long _stub_fn_1060() __asm__("_ZN7UILabel9TerminateEv");
extern "C" long _stub_fn_1060() { return 0; }
// UIPanel::SetPaused(bool)
extern "C" __attribute__((weak, used)) long _stub_fn_1061() __asm__("_ZN7UIPanel9SetPausedEb");
extern "C" long _stub_fn_1061() { return 0; }
// AppLabel::SetStoreFilterName(HamStoreFilter const*)
extern "C" __attribute__((weak, used)) long _stub_fn_1062() __asm__("_ZN8AppLabel18SetStoreFilterNameEPK14HamStoreFilter");
extern "C" long _stub_fn_1062() { return 0; }
// AppLabel::SetTimeElapsedSince(unsigned int)
extern "C" __attribute__((weak, used)) long _stub_fn_1063() __asm__("_ZN8AppLabel19SetTimeElapsedSinceEj");
extern "C" long _stub_fn_1063() { return 0; }
// BlockMgr::KillBlockRequests(ArkFile*)
extern "C" __attribute__((weak, used)) long _stub_fn_1064() __asm__("_ZN8BlockMgr17KillBlockRequestsEP7ArkFile");
extern "C" long _stub_fn_1064() { return 0; }
// BlockMgr::GetAssociatedBlocks(unsigned long long, int, int&, int&, int&)
extern "C" __attribute__((weak, used)) long _stub_fn_1065() __asm__("_ZN8BlockMgr19GetAssociatedBlocksEyiRiS0_S0_");
extern "C" long _stub_fn_1065() { return 0; }
// BlockMgr::Poll()
extern "C" __attribute__((weak, used)) long _stub_fn_1066() __asm__("_ZN8BlockMgr4PollEv");
extern "C" long _stub_fn_1066() { return 0; }
// BlockMgr::AddTask(AsyncTask const&)
extern "C" __attribute__((weak, used)) long _stub_fn_1067() __asm__("_ZN8BlockMgr7AddTaskERK9AsyncTask");
extern "C" long _stub_fn_1067() { return 0; }
// CacheMgr::GetLastResult()
extern "C" __attribute__((weak, used)) long _stub_fn_1068() __asm__("_ZN8CacheMgr13GetLastResultEv");
extern "C" long _stub_fn_1068() { return 0; }
// CharClip::Transitions::AddNode(CharClip*, CharGraphNode const&)
extern "C" __attribute__((weak, used)) long _stub_fn_1069() __asm__("_ZN8CharClip11Transitions7AddNodeEPS_RK13CharGraphNode");
extern "C" long _stub_fn_1069() { return 0; }
// CharEyes::EyesOnTarget(float)
extern "C" __attribute__((weak, used)) long _stub_fn_1070() __asm__("_ZN8CharEyes12EyesOnTargetEf");
extern "C" long _stub_fn_1070() { return 0; }
// CharEyes::OnAddInterest(DataArray*)
extern "C" __attribute__((weak, used)) long _stub_fn_1071() __asm__("_ZN8CharEyes13OnAddInterestEP9DataArray");
extern "C" long _stub_fn_1071() { return 0; }
// CharEyes::AddInterestObject(CharInterest*)
extern "C" __attribute__((weak, used)) long _stub_fn_1072() __asm__("_ZN8CharEyes17AddInterestObjectEP12CharInterest");
extern "C" long _stub_fn_1072() { return 0; }
// CharEyes::GenerateDartOffset()
extern "C" __attribute__((weak, used)) long _stub_fn_1073() __asm__("_ZN8CharEyes18GenerateDartOffsetEv");
extern "C" long _stub_fn_1073() { return 0; }
// CharEyes::OnToggleForceFocus(DataArray*)
extern "C" __attribute__((weak, used)) long _stub_fn_1074() __asm__("_ZN8CharEyes18OnToggleForceFocusEP9DataArray");
extern "C" long _stub_fn_1074() { return 0; }
// CharEyes::OnToggleInterestOverlay(DataArray*)
extern "C" __attribute__((weak, used)) long _stub_fn_1075() __asm__("_ZN8CharEyes23OnToggleInterestOverlayEP9DataArray");
extern "C" long _stub_fn_1075() { return 0; }
// CharEyes::Exit()
extern "C" __attribute__((weak, used)) long _stub_fn_1076() __asm__("_ZN8CharEyes4ExitEv");
extern "C" long _stub_fn_1076() { return 0; }
// CharEyes::Load(BinStream&)
extern "C" __attribute__((weak, used)) long _stub_fn_1077() __asm__("_ZN8CharEyes4LoadER9BinStream");
extern "C" long _stub_fn_1077() { return 0; }
// CharEyes::Poll()
extern "C" __attribute__((weak, used)) long _stub_fn_1078() __asm__("_ZN8CharEyes4PollEv");
extern "C" long _stub_fn_1078() { return 0; }
// CharEyes::Replace(ObjRef*, Hmx::Object*)
extern "C" __attribute__((weak, used)) long _stub_fn_1079() __asm__("_ZN8CharEyes7ReplaceEP6ObjRefPN3Hmx6ObjectE");
extern "C" long _stub_fn_1079() { return 0; }
// CharEyes::PollDeps(std::__cxx11::list<Hmx::Object*, std::allocator<Hmx::Object*> >&, std::__cxx11::list<Hmx::Object*, std::allocator<Hmx::Object*> >&)
extern "C" __attribute__((weak, used)) long _stub_fn_1080() __asm__("_ZN8CharEyes8PollDepsERNSt7__cxx114listIPN3Hmx6ObjectESaIS4_EEES7_");
extern "C" long _stub_fn_1080() { return 0; }
// CharEyes::Highlight()
extern "C" __attribute__((weak, used)) long _stub_fn_1081() __asm__("_ZN8CharEyes9HighlightEv");
extern "C" long _stub_fn_1081() { return 0; }
// CharHair::FreezePoseRaw()
extern "C" __attribute__((weak, used)) long _stub_fn_1082() __asm__("_ZN8CharHair13FreezePoseRawEv");
extern "C" long _stub_fn_1082() { return 0; }
// CharHair::SimulateLoops(int, float)
extern "C" __attribute__((weak, used)) long _stub_fn_1083() __asm__("_ZN8CharHair13SimulateLoopsEif");
extern "C" long _stub_fn_1083() { return 0; }
// CharHair::Point::Point(Hmx::Object*)
extern "C" __attribute__((weak, used)) long _stub_fn_1084() __asm__("_ZN8CharHair5PointC1EPN3Hmx6ObjectE");
extern "C" long _stub_fn_1084() { return 0; }
// CharHair::Strand::SetRoot(RndTransformable*)
extern "C" __attribute__((weak, used)) long _stub_fn_1086() __asm__("_ZN8CharHair6Strand7SetRootEP16RndTransformable");
extern "C" long _stub_fn_1086() { return 0; }
// CharHair::DoReset(int)
extern "C" __attribute__((weak, used)) long _stub_fn_1087() __asm__("_ZN8CharHair7DoResetEi");
extern "C" long _stub_fn_1087() { return 0; }
// DateTime::ToDayNumber()
extern "C" __attribute__((weak, used)) long _stub_fn_1088() __asm__("_ZN8DateTime11ToDayNumberEv");
extern "C" long _stub_fn_1088() { return 0; }
// DateTime::FromDayNumber(int)
extern "C" __attribute__((weak, used)) long _stub_fn_1089() __asm__("_ZN8DateTime13FromDayNumberEi");
extern "C" long _stub_fn_1089() { return 0; }
// DateTime::FromUtcToLocal()
extern "C" __attribute__((weak, used)) long _stub_fn_1090() __asm__("_ZN8DateTime14FromUtcToLocalEv");
extern "C" long _stub_fn_1090() { return 0; }
// DateTime::ParseDate(char const*)
extern "C" __attribute__((weak, used)) long _stub_fn_1091() __asm__("_ZN8DateTime9ParseDateEPKc");
extern "C" long _stub_fn_1091() { return 0; }
// DateTime::ToSeconds()
extern "C" __attribute__((weak, used)) long _stub_fn_1092() __asm__("_ZN8DateTime9ToSecondsEv");
extern "C" long _stub_fn_1092() { return 0; }
// DingoJob::GetResponseString()
extern "C" __attribute__((weak, used)) long _stub_fn_1093() __asm__("_ZN8DingoJob17GetResponseStringEv");
extern "C" long _stub_fn_1093() { return 0; }
// EQEffect::SetParameter(int, float)
extern "C" __attribute__((weak, used)) long _stub_fn_1094() __asm__("_ZN8EQEffect12SetParameterEif");
extern "C" long _stub_fn_1094() { return 0; }
// EQEffect::Reset()
extern "C" __attribute__((weak, used)) long _stub_fn_1095() __asm__("_ZN8EQEffect5ResetEv");
extern "C" long _stub_fn_1095() { return 0; }
// FlowNode::MoveIntoDir(ObjectDir*, ObjectDir*)
extern "C" __attribute__((weak, used)) long _stub_fn_1096() __asm__("_ZN8FlowNode11MoveIntoDirEP9ObjectDirS1_");
extern "C" long _stub_fn_1096() { return 0; }
// FlowNode::DuplicateChild(FlowNode*)
extern "C" __attribute__((weak, used)) long _stub_fn_1097() __asm__("_ZN8FlowNode14DuplicateChildEPS_");
extern "C" long _stub_fn_1097() { return 0; }
// FlowNode::PushDrivenProperties()
extern "C" __attribute__((weak, used)) long _stub_fn_1098() __asm__("_ZN8FlowNode20PushDrivenPropertiesEv");
extern "C" long _stub_fn_1098() { return 0; }
// FlowNode::LoadObjectFromMainOrDir(BinStream&, ObjectDir*)
extern "C" __attribute__((weak, used)) long _stub_fn_1099() __asm__("_ZN8FlowNode23LoadObjectFromMainOrDirER9BinStreamP9ObjectDir");
extern "C" long _stub_fn_1099() { return 0; }
// HamAudio::FinishLoad()
extern "C" __attribute__((weak, used)) long _stub_fn_1100() __asm__("_ZN8HamAudio10FinishLoadEv");
extern "C" long _stub_fn_1100() { return 0; }
// HamAudio::PollCrossfade()
extern "C" __attribute__((weak, used)) long _stub_fn_1101() __asm__("_ZN8HamAudio13PollCrossfadeEv");
extern "C" long _stub_fn_1101() { return 0; }
// KeyChain::getNumKeys()
extern "C" __attribute__((weak, used)) long _stub_fn_1102() __asm__("_ZN8KeyChain10getNumKeysEv");
extern "C" long _stub_fn_1102() { return 0; }
// KeyChain::getKey(unsigned int, unsigned char*, unsigned char*)
extern "C" __attribute__((weak, used)) long _stub_fn_1103() __asm__("_ZN8KeyChain6getKeyEjPhS0_");
extern "C" long _stub_fn_1103() { return 0; }
// KeyChain::getMasher(unsigned char*)
extern "C" __attribute__((weak, used)) long _stub_fn_1104() __asm__("_ZN8KeyChain9getMasherEPh");
extern "C" long _stub_fn_1104() { return 0; }
// MoggClip::SetPan(int, float)
extern "C" __attribute__((weak, used)) long _stub_fn_1105() __asm__("_ZN8MoggClip6SetPanEif");
extern "C" long _stub_fn_1105() { return 0; }
// MsgSinks::MergeSinks(Hmx::Object*)
extern "C" __attribute__((weak, used)) long _stub_fn_1106() __asm__("_ZN8MsgSinks10MergeSinksEPN3Hmx6ObjectE");
extern "C" long _stub_fn_1106() { return 0; }
// MsgSinks::RemoveSink(Hmx::Object*, Symbol)
extern "C" __attribute__((weak, used)) long _stub_fn_1107() __asm__("_ZN8MsgSinks10RemoveSinkEPN3Hmx6ObjectE6Symbol");
extern "C" long _stub_fn_1107() { return 0; }
// MsgSinks::EventSinkElem::operator=(MsgSinks::EventSinkElem const&)
extern "C" __attribute__((weak, used)) long _stub_fn_1108() __asm__("_ZN8MsgSinks13EventSinkElemaSERKS0_");
extern "C" long _stub_fn_1108() { return 0; }
// MsgSinks::RemovePropertySink(Hmx::Object*, DataArray*)
extern "C" __attribute__((weak, used)) long _stub_fn_1109() __asm__("_ZN8MsgSinks18RemovePropertySinkEPN3Hmx6ObjectEP9DataArray");
extern "C" long _stub_fn_1109() { return 0; }
// MsgSinks::Export(DataArray*)
extern "C" __attribute__((weak, used)) long _stub_fn_1110() __asm__("_ZN8MsgSinks6ExportEP9DataArray");
extern "C" long _stub_fn_1110() { return 0; }
// MsgSinks::Replace(ObjRef*, Hmx::Object*)
extern "C" __attribute__((weak, used)) long _stub_fn_1111() __asm__("_ZN8MsgSinks7ReplaceEP6ObjRefPN3Hmx6ObjectE");
extern "C" long _stub_fn_1111() { return 0; }
// QuatKeys::QuatAt(float, Hmx::Quat&)
extern "C" __attribute__((weak, used)) long _stub_fn_1112() __asm__("_ZN8QuatKeys6QuatAtEfRN3Hmx4QuatE");
extern "C" long _stub_fn_1112() { return 0; }
// RndFlare::DrawShowing()
extern "C" __attribute__((weak, used)) long _stub_fn_1113() __asm__("_ZN8RndFlare11DrawShowingEv");
extern "C" long _stub_fn_1113() { return 0; }
// RndFlare::Mats(std::__cxx11::list<RndMat*, std::allocator<RndMat*> >&, bool)
extern "C" __attribute__((weak, used)) long _stub_fn_1114() __asm__("_ZN8RndFlare4MatsERNSt7__cxx114listIP6RndMatSaIS3_EEEb");
extern "C" long _stub_fn_1114() { return 0; }
// RndGroup::MoveObject(Hmx::Object*, int)
extern "C" __attribute__((weak, used)) long _stub_fn_1115() __asm__("_ZN8RndGroup10MoveObjectEPN3Hmx6ObjectEi");
extern "C" long _stub_fn_1115() { return 0; }
// RndGroup::DrawShowing()
extern "C" __attribute__((weak, used)) long _stub_fn_1116() __asm__("_ZN8RndGroup11DrawShowingEv");
extern "C" long _stub_fn_1116() { return 0; }
// RndLight::Copy(Hmx::Object const*, Hmx::Object::CopyType)
extern "C" __attribute__((weak, used)) long _stub_fn_1117() __asm__("_ZN8RndLight4CopyEPKN3Hmx6ObjectENS1_8CopyTypeE");
extern "C" long _stub_fn_1117() { return 0; }
// RndLight::Replace(ObjRef*, Hmx::Object*)
extern "C" __attribute__((weak, used)) long _stub_fn_1118() __asm__("_ZN8RndLight7ReplaceEP6ObjRefPN3Hmx6ObjectE");
extern "C" long _stub_fn_1118() { return 0; }
// RndMorph::EndFrame()
extern "C" __attribute__((weak, used)) long _stub_fn_1119() __asm__("_ZN8RndMorph8EndFrameEv");
extern "C" long _stub_fn_1119() { return 0; }
// RndMorph::SetFrame(float, float)
extern "C" __attribute__((weak, used)) long _stub_fn_1120() __asm__("_ZN8RndMorph8SetFrameEff");
extern "C" long _stub_fn_1120() { return 0; }
// Skeleton::EnrollIdentity(int)
extern "C" __attribute__((weak, used)) long _stub_fn_1121() __asm__("_ZN8Skeleton14EnrollIdentityEi");
extern "C" long _stub_fn_1121() { return 0; }
// Skeleton::IdentityCallback(void*, _NUI_IDENTITY_MESSAGE*)
extern "C" __attribute__((weak, used)) long _stub_fn_1122() __asm__("_ZN8Skeleton16IdentityCallbackEPvP21_NUI_IDENTITY_MESSAGE");
extern "C" long _stub_fn_1122() { return 0; }
// Skeleton::Poll(int, SkeletonFrame const&)
extern "C" __attribute__((weak, used)) long _stub_fn_1123() __asm__("_ZN8Skeleton4PollEiRK13SkeletonFrame");
extern "C" long _stub_fn_1123() { return 0; }
// Skeleton::operator=(Skeleton const&)
extern "C" __attribute__((weak, used)) long _stub_fn_1124() __asm__("_ZN8SkeletonaSERKS_");
extern "C" long _stub_fn_1124() { return 0; }
// Triangle::Set(Vector3 const&, Vector3 const&, Vector3 const&)
extern "C" __attribute__((weak, used)) long _stub_fn_1125() __asm__("_ZN8Triangle3SetERK7Vector3S2_S2_");
extern "C" long _stub_fn_1125() { return 0; }
// Waypoint::FindNearest(Vector3 const&, int)
extern "C" __attribute__((weak, used)) long _stub_fn_1126() __asm__("_ZN8Waypoint11FindNearestERK7Vector3i");
extern "C" long _stub_fn_1126() { return 0; }
// Waypoint::OnWaypointLast(DataArray*)
extern "C" __attribute__((weak, used)) long _stub_fn_1127() __asm__("_ZN8Waypoint14OnWaypointLastEP9DataArray");
extern "C" long _stub_fn_1127() { return 0; }
// Waypoint::Constrain(Transform&)
extern "C" __attribute__((weak, used)) long _stub_fn_1128() __asm__("_ZN8Waypoint9ConstrainER9Transform");
extern "C" long _stub_fn_1128() { return 0; }
// Waypoint::Highlight()
extern "C" __attribute__((weak, used)) long _stub_fn_1129() __asm__("_ZN8Waypoint9HighlightEv");
extern "C" long _stub_fn_1129() { return 0; }
// WorldDir::DrawShowing()
extern "C" __attribute__((weak, used)) long _stub_fn_1130() __asm__("_ZN8WorldDir11DrawShowingEv");
extern "C" long _stub_fn_1130() { return 0; }
// WorldDir::BitmapOverride::Sync(bool)
extern "C" __attribute__((weak, used)) long _stub_fn_1131() __asm__("_ZN8WorldDir14BitmapOverride4SyncEb");
extern "C" long _stub_fn_1131() { return 0; }
// BeatClock::OnSyncState(DataArray*)
extern "C" __attribute__((weak, used)) long _stub_fn_1132() __asm__("_ZN9BeatClock11OnSyncStateEP9DataArray");
extern "C" long _stub_fn_1132() { return 0; }
// BeatClock::UpdateSongPos()
extern "C" __attribute__((weak, used)) long _stub_fn_1133() __asm__("_ZN9BeatClock13UpdateSongPosEv");
extern "C" long _stub_fn_1133() { return 0; }
// BufStream::Size()
extern "C" __attribute__((weak, used)) long _stub_fn_1134() __asm__("_ZN9BufStream4SizeEv");
extern "C" long _stub_fn_1134() { return 0; }
// Character::SyncShadow()
extern "C" __attribute__((weak, used)) long _stub_fn_1135() __asm__("_ZN9Character10SyncShadowEv");
extern "C" long _stub_fn_1135() { return 0; }
// Character::DrawShowing()
extern "C" __attribute__((weak, used)) long _stub_fn_1136() __asm__("_ZN9Character11DrawShowingEv");
extern "C" long _stub_fn_1136() { return 0; }
// Character::UnhookShadow()
extern "C" __attribute__((weak, used)) long _stub_fn_1137() __asm__("_ZN9Character12UnhookShadowEv");
extern "C" long _stub_fn_1137() { return 0; }
// Character::FindInterestObjects(ObjectDir*)
extern "C" __attribute__((weak, used)) long _stub_fn_1138() __asm__("_ZN9Character19FindInterestObjectsEP9ObjectDir");
extern "C" long _stub_fn_1138() { return 0; }
// CharBones::AddBones(std::__cxx11::list<CharBones::Bone, std::allocator<CharBones::Bone> > const&)
extern "C" __attribute__((weak, used)) long _stub_fn_1139() __asm__("_ZN9CharBones8AddBonesERKNSt7__cxx114listINS_4BoneESaIS2_EEE");
extern "C" long _stub_fn_1139() { return 0; }
// CharBones::AddBones(std::vector<CharBones::Bone, std::allocator<CharBones::Bone> > const&)
extern "C" __attribute__((weak, used)) long _stub_fn_1140() __asm__("_ZN9CharBones8AddBonesERKSt6vectorINS_4BoneESaIS1_EE");
extern "C" long _stub_fn_1140() { return 0; }
// DirLoader::New(FilePath const&, LoaderPos)
extern "C" __attribute__((weak, used)) long _stub_fn_1141() __asm__("_ZN9DirLoader3NewERK8FilePath9LoaderPos");
extern "C" long _stub_fn_1141() { return 0; }
// FlowSound::OnMarkerEvent(Symbol)
extern "C" __attribute__((weak, used)) long _stub_fn_1143() __asm__("_ZN9FlowSound13OnMarkerEventE6Symbol");
extern "C" long _stub_fn_1143() { return 0; }
// FlowWhile::ReActivate()
extern "C" __attribute__((weak, used)) long _stub_fn_1144() __asm__("_ZN9FlowWhile10ReActivateEv");
extern "C" long _stub_fn_1144() { return 0; }
// FxSendWah::Load(BinStream&)
extern "C" __attribute__((weak, used)) long _stub_fn_1145() __asm__("_ZN9FxSendWah4LoadER9BinStream");
extern "C" long _stub_fn_1145() { return 0; }
// GamePanel::UpdateNowBar()
extern "C" __attribute__((weak, used)) long _stub_fn_1146() __asm__("_ZN9GamePanel12UpdateNowBarEv");
extern "C" long _stub_fn_1146() { return 0; }
// GamePanel::UpdateLatency()
extern "C" __attribute__((weak, used)) long _stub_fn_1147() __asm__("_ZN9GamePanel13UpdateLatencyEv");
extern "C" long _stub_fn_1147() { return 0; }
// GamePanel::DeJitter(float)
extern "C" __attribute__((weak, used)) long _stub_fn_1148() __asm__("_ZN9GamePanel8DeJitterEf");
extern "C" long _stub_fn_1148() { return 0; }
// HamDriver::DisplayRecurse(HamDriver::Layer*, int, float)
extern "C" __attribute__((weak, used)) long _stub_fn_1149() __asm__("_ZN9HamDriver14DisplayRecurseEPNS_5LayerEif");
extern "C" long _stub_fn_1149() { return 0; }
// HamDriver::Poll()
extern "C" __attribute__((weak, used)) long _stub_fn_1150() __asm__("_ZN9HamDriver4PollEv");
extern "C" long _stub_fn_1150() { return 0; }
// HamDriver::PostLoad(BinStream&)
extern "C" __attribute__((weak, used)) long _stub_fn_1151() __asm__("_ZN9HamDriver8PostLoadER9BinStream");
extern "C" long _stub_fn_1151() { return 0; }
// HamRibbon::UpdateChase()
extern "C" __attribute__((weak, used)) long _stub_fn_1152() __asm__("_ZN9HamRibbon11UpdateChaseEv");
extern "C" long _stub_fn_1152() { return 0; }
// HamVisDir::UpdateGestureFilter(Skeleton const&, int)
extern "C" __attribute__((weak, used)) long _stub_fn_1153() __asm__("_ZN9HamVisDir19UpdateGestureFilterERK8Skeletoni");
extern "C" long _stub_fn_1153() { return 0; }
// NgDOFProc::Set(RndCam*, float, float, float, float)
extern "C" __attribute__((weak, used)) long _stub_fn_1154() __asm__("_ZN9NgDOFProc3SetEP6RndCamffff");
extern "C" long _stub_fn_1154() { return 0; }
// NgDOFProc::DoPost()
extern "C" __attribute__((weak, used)) long _stub_fn_1155() __asm__("_ZN9NgDOFProc6DoPostEv");
extern "C" long _stub_fn_1155() { return 0; }
// ObjDirPtr<ObjectDir>::ObjDirPtr(ObjectDir*)
extern "C" __attribute__((weak, used)) long _stub_fn_1156() __asm__("_ZN9ObjDirPtrI9ObjectDirEC1EPS0_");
extern "C" long _stub_fn_1156() { return 0; }
// ObjectDir::InlineSubDirType()
extern "C" __attribute__((weak, used)) long _stub_fn_1157() __asm__("_ZN9ObjectDir16InlineSubDirTypeEv");
extern "C" long _stub_fn_1157() { return 0; }
// RndBitmap::SetPreMultipliedAlpha()
extern "C" __attribute__((weak, used)) long _stub_fn_1180() __asm__("_ZN9RndBitmap21SetPreMultipliedAlphaEv");
extern "C" long _stub_fn_1180() { return 0; }
// RndBitmap::Load: provided by RndTex_Native.cpp
// RndBitmap::LoadDIB(BinStream*, unsigned int)
extern "C" __attribute__((weak, used)) long _stub_fn_1182() __asm__("_ZN9RndBitmap7LoadDIBEP9BinStreamj");
extern "C" long _stub_fn_1182() { return 0; }
// RndRibbon::UpdateChase()
extern "C" __attribute__((weak, used)) long _stub_fn_1183() __asm__("_ZN9RndRibbon11UpdateChaseEv");
extern "C" long _stub_fn_1183() { return 0; }
// RndRibbon::ConstructMesh()
extern "C" __attribute__((weak, used)) long _stub_fn_1184() __asm__("_ZN9RndRibbon13ConstructMeshEv");
extern "C" long _stub_fn_1184() { return 0; }
// RndSpline::SetEndCtrlPoint(int)
extern "C" __attribute__((weak, used)) long _stub_fn_1185() __asm__("_ZN9RndSpline15SetEndCtrlPointEi");
extern "C" long _stub_fn_1185() { return 0; }
// RndSpline::SetStartCtrlPoint(int)
extern "C" __attribute__((weak, used)) long _stub_fn_1186() __asm__("_ZN9RndSpline17SetStartCtrlPointEi");
extern "C" long _stub_fn_1186() { return 0; }
// RndSpline::SyncPristineCtrlPoints()
extern "C" __attribute__((weak, used)) long _stub_fn_1187() __asm__("_ZN9RndSpline22SyncPristineCtrlPointsEv");
extern "C" long _stub_fn_1187() { return 0; }
// RndSpline::Poll()
extern "C" __attribute__((weak, used)) long _stub_fn_1188() __asm__("_ZN9RndSpline4PollEv");
extern "C" long _stub_fn_1188() { return 0; }
// Spotlight::BuildNGCone(Spotlight::BeamDef&, int)
extern "C" __attribute__((weak, used)) long _stub_fn_1189() __asm__("_ZN9Spotlight11BuildNGConeERNS_7BeamDefEi");
extern "C" long _stub_fn_1189() { return 0; }
// Spotlight::BuildNGQuad(Spotlight::BeamDef&, RndTransformable::Constraint)
extern "C" __attribute__((weak, used)) long _stub_fn_1190() __asm__("_ZN9Spotlight11BuildNGQuadERNS_7BeamDefEN16RndTransformable10ConstraintE");
extern "C" long _stub_fn_1190() { return 0; }
// Spotlight::DrawShowing()
extern "C" __attribute__((weak, used)) long _stub_fn_1191() __asm__("_ZN9Spotlight11DrawShowingEv");
extern "C" long _stub_fn_1191() { return 0; }
// Spotlight::BuildNGSheet(Spotlight::BeamDef&)
extern "C" __attribute__((weak, used)) long _stub_fn_1192() __asm__("_ZN9Spotlight12BuildNGSheetERNS_7BeamDefE");
extern "C" long _stub_fn_1192() { return 0; }
// Spotlight::RemoveFromLists(Spotlight*)
extern "C" __attribute__((weak, used)) long _stub_fn_1193() __asm__("_ZN9Spotlight15RemoveFromListsEPS_");
extern "C" long _stub_fn_1193() { return 0; }
// Spotlight::UpdateTransforms()
extern "C" __attribute__((weak, used)) long _stub_fn_1194() __asm__("_ZN9Spotlight16UpdateTransformsEv");
extern "C" long _stub_fn_1194() { return 0; }
// Spotlight::UpdateFloorSpotTransform(Transform const&)
extern "C" __attribute__((weak, used)) long _stub_fn_1195() __asm__("_ZN9Spotlight24UpdateFloorSpotTransformERK9Transform");
extern "C" long _stub_fn_1195() { return 0; }
// Spotlight::Mats(std::__cxx11::list<RndMat*, std::allocator<RndMat*> >&, bool)
extern "C" __attribute__((weak, used)) long _stub_fn_1196() __asm__("_ZN9Spotlight4MatsERNSt7__cxx114listIP6RndMatSaIS3_EEEb");
extern "C" long _stub_fn_1196() { return 0; }
// Spotlight::Poll()
extern "C" __attribute__((weak, used)) long _stub_fn_1197() __asm__("_ZN9Spotlight4PollEv");
extern "C" long _stub_fn_1197() { return 0; }
// Spotlight::BuildBeam(Spotlight::BeamDef&)
extern "C" __attribute__((weak, used)) long _stub_fn_1198() __asm__("_ZN9Spotlight9BuildBeamERNS_7BeamDefE");
extern "C" long _stub_fn_1198() { return 0; }
// Spotlight::BuildCone(Spotlight::BeamDef&)
extern "C" __attribute__((weak, used)) long _stub_fn_1199() __asm__("_ZN9Spotlight9BuildConeERNS_7BeamDefE");
extern "C" long _stub_fn_1199() { return 0; }
// Transform::LookAt(Vector3 const&, Vector3 const&)
extern "C" __attribute__((weak, used)) long _stub_fn_1200() __asm__("_ZN9Transform6LookAtERK7Vector3S2_");
extern "C" long _stub_fn_1200() { return 0; }
// WavReader::Poll(float)
extern "C" __attribute__((weak, used)) long _stub_fn_1201() __asm__("_ZN9WavReader4PollEf");
extern "C" long _stub_fn_1201() { return 0; }
// WebSvcMgr::Poll()
extern "C" __attribute__((weak, used)) long _stub_fn_1202() __asm__("_ZN9WebSvcMgr4PollEv");
extern "C" long _stub_fn_1202() { return 0; }
// DrawPtrVec::CollideShowing(Segment const&, float&, Plane&) const
extern "C" __attribute__((weak, used)) long _stub_fn_1203() __asm__("_ZNK10DrawPtrVec14CollideShowingERK7SegmentRfR5Plane");
extern "C" long _stub_fn_1203() { return 0; }
// GestureMgr::GetLiveCameraInput() const
extern "C" __attribute__((weak, used)) long _stub_fn_1204() __asm__("_ZNK10GestureMgr18GetLiveCameraInputEv");
extern "C" long _stub_fn_1204() { return 0; }
// HamNavList::CalculateSwell(int) const
extern "C" __attribute__((weak, used)) long _stub_fn_1205() __asm__("_ZNK10HamNavList14CalculateSwellEi");
extern "C" long _stub_fn_1205() { return 0; }
// HamNavList::GetDisabledCount(int) const
extern "C" __attribute__((weak, used)) long _stub_fn_1206() __asm__("_ZNK10HamNavList16GetDisabledCountEi");
extern "C" long _stub_fn_1206() { return 0; }
// HamNavList::DrawDebug() const
extern "C" __attribute__((weak, used)) long _stub_fn_1207() __asm__("_ZNK10HamNavList9DrawDebugEv");
extern "C" long _stub_fn_1207() { return 0; }
// ProfileMgr::GetPadExtraLag(int, LagContext) const
extern "C" __attribute__((weak, used)) long _stub_fn_1280() __asm__("_ZNK10ProfileMgr14GetPadExtraLagEi10LagContext");
extern "C" long _stub_fn_1280() { return 0; }
// SampleData::SizeAs(SampleData::Format) const
extern "C" __attribute__((weak, used)) long _stub_fn_1281() __asm__("_ZNK10SampleData6SizeAsENS_6FormatE");
extern "C" long _stub_fn_1281() { return 0; }
// StorePanel::StoreProfile() const
extern "C" __attribute__((weak, used)) long _stub_fn_1282() __asm__("_ZNK10StorePanel12StoreProfileEv");
extern "C" long _stub_fn_1282() { return 0; }
// StorePanel::ExitStore(StoreError) const
extern "C" __attribute__((weak, used)) long _stub_fn_1283() __asm__("_ZNK10StorePanel9ExitStoreE10StoreError");
extern "C" long _stub_fn_1283() { return 0; }
// UIListMesh::DefaultMat() const
extern "C" __attribute__((weak, used)) long _stub_fn_1284() __asm__("_ZNK10UIListMesh10DefaultMatEv");
extern "C" long _stub_fn_1284() { return 0; }
// ArcDetector::IsLockedIn() const
extern "C" __attribute__((weak, used)) long _stub_fn_1285() __asm__("_ZNK11ArcDetector10IsLockedInEv");
extern "C" long _stub_fn_1285() { return 0; }
// ArcDetector::GetPathError() const
extern "C" __attribute__((weak, used)) long _stub_fn_1286() __asm__("_ZNK11ArcDetector12GetPathErrorEv");
extern "C" long _stub_fn_1286() { return 0; }
// ArcDetector::GetPathLength() const
extern "C" __attribute__((weak, used)) long _stub_fn_1287() __asm__("_ZNK11ArcDetector13GetPathLengthEv");
extern "C" long _stub_fn_1287() { return 0; }
// ArcDetector::GetSwipeAmount() const
extern "C" __attribute__((weak, used)) long _stub_fn_1288() __asm__("_ZNK11ArcDetector14GetSwipeAmountEv");
extern "C" long _stub_fn_1288() { return 0; }
// CampaignEra::GetName() const
extern "C" __attribute__((weak, used)) long _stub_fn_1289() __asm__("_ZNK11CampaignEra7GetNameEv");
extern "C" long _stub_fn_1289() { return 0; }
// LightPreset::GetKey(float, int&, int&, float&) const
extern "C" __attribute__((weak, used)) long _stub_fn_1290() __asm__("_ZNK11LightPreset6GetKeyEfRiS0_Rf");
extern "C" long _stub_fn_1290() { return 0; }
// PlatformMgr::IsPadAGuest(int) const
extern "C" __attribute__((weak, used)) long _stub_fn_1291() __asm__("_ZNK11PlatformMgr11IsPadAGuestEi");
extern "C" long _stub_fn_1291() { return 0; }
// PlatformMgr::SetPadContext(int, int, int) const
extern "C" __attribute__((weak, used)) long _stub_fn_1292() __asm__("_ZNK11PlatformMgr13SetPadContextEiii");
extern "C" long _stub_fn_1292() { return 0; }
// PlatformMgr::SetPadPresence(int, int) const
extern "C" __attribute__((weak, used)) long _stub_fn_1293() __asm__("_ZNK11PlatformMgr14SetPadPresenceEii");
extern "C" long _stub_fn_1293() { return 0; }
// PlatformMgr::SetPadProperty(int, int, unsigned short const*) const
extern "C" __attribute__((weak, used)) long _stub_fn_1294() __asm__("_ZNK11PlatformMgr14SetPadPropertyEiiPKt");
extern "C" long _stub_fn_1294() { return 0; }
// PlatformMgr::IsSignedIntoLive(int) const
extern "C" __attribute__((weak, used)) long _stub_fn_1295() __asm__("_ZNK11PlatformMgr16IsSignedIntoLiveEi");
extern "C" long _stub_fn_1295() { return 0; }
// PlatformMgr::HasOnlinePrivilege(int) const
extern "C" __attribute__((weak, used)) long _stub_fn_1296() __asm__("_ZNK11PlatformMgr18HasOnlinePrivilegeEi");
extern "C" long _stub_fn_1296() { return 0; }
// PlatformMgr::HasKinectSharePrvilege() const
extern "C" __attribute__((weak, used)) long _stub_fn_1297() __asm__("_ZNK11PlatformMgr22HasKinectSharePrvilegeEv");
extern "C" long _stub_fn_1297() { return 0; }
// PlatformMgr::HasCreatedContentPrivilege() const
extern "C" __attribute__((weak, used)) long _stub_fn_1298() __asm__("_ZNK11PlatformMgr26HasCreatedContentPrivilegeEv");
extern "C" long _stub_fn_1298() { return 0; }
// PlatformMgr::GetName(int) const
extern "C" __attribute__((weak, used)) long _stub_fn_1299() __asm__("_ZNK11PlatformMgr7GetNameEi");
extern "C" long _stub_fn_1299() { return 0; }
// BaseSkeleton::LimbNormPos(SkeletonCoordSys, SkeletonJoint, bool, Vector3 const&, Vector3&) const
extern "C" __attribute__((weak, used)) long _stub_fn_1300() __asm__("_ZNK12BaseSkeleton11LimbNormPosE16SkeletonCoordSys13SkeletonJointbRK7Vector3RS2_");
extern "C" long _stub_fn_1300() { return 0; }
// DanceRemixer::JumpedBeat(float) const
extern "C" __attribute__((weak, used)) long _stub_fn_1301() __asm__("_ZNK12DanceRemixer10JumpedBeatEf");
extern "C" long _stub_fn_1301() { return 0; }
// DanceRemixer::JumpedMeasureAdd(int, int) const
extern "C" __attribute__((weak, used)) long _stub_fn_1302() __asm__("_ZNK12DanceRemixer16JumpedMeasureAddEii");
extern "C" long _stub_fn_1302() { return 0; }
// DanceRemixer::JumpedMoveIdxAdd(int, int) const
extern "C" __attribute__((weak, used)) long _stub_fn_1303() __asm__("_ZNK12DanceRemixer16JumpedMoveIdxAddEii");
extern "C" long _stub_fn_1303() { return 0; }
// DanceRemixer::MoveVariantFromHamMove(HamMove const*) const
extern "C" __attribute__((weak, used)) long _stub_fn_1304() __asm__("_ZNK12DanceRemixer22MoveVariantFromHamMoveEPK7HamMove");
extern "C" long _stub_fn_1304() { return 0; }
// DanceRemixer::JumpedMeasureStepsBetween(int, int, int) const
extern "C" __attribute__((weak, used)) long _stub_fn_1305() __asm__("_ZNK12DanceRemixer25JumpedMeasureStepsBetweenEiii");
extern "C" long _stub_fn_1305() { return 0; }
// MetagameRank::GetRankInTier() const
extern "C" __attribute__((weak, used)) long _stub_fn_1306() __asm__("_ZNK12MetagameRank13GetRankInTierEv");
extern "C" long _stub_fn_1306() { return 0; }
// MetagameRank::GetTier() const
extern "C" __attribute__((weak, used)) long _stub_fn_1307() __asm__("_ZNK12MetagameRank7GetTierEv");
extern "C" long _stub_fn_1307() { return 0; }
// SkeletonClip::PrevSkeleton(Skeleton const&, int, ArchiveSkeleton&, int&) const
extern "C" __attribute__((weak, used)) long _stub_fn_1308() __asm__("_ZNK12SkeletonClip12PrevSkeletonERK8SkeletoniR15ArchiveSkeletonRi");
extern "C" long _stub_fn_1308() { return 0; }
// SkeletonClip::CurRecordedFrame(int&, int&) const
extern "C" __attribute__((weak, used)) long _stub_fn_1309() __asm__("_ZNK12SkeletonClip16CurRecordedFrameERiS0_");
extern "C" long _stub_fn_1309() { return 0; }
// SkeletonClip::SongStartSeconds() const
extern "C" __attribute__((weak, used)) long _stub_fn_1310() __asm__("_ZNK12SkeletonClip16SongStartSecondsEv");
extern "C" long _stub_fn_1310() { return 0; }
// SongInfoCopy::GetBaseFileName() const
extern "C" __attribute__((weak, used)) long _stub_fn_1311() __asm__("_ZNK12SongInfoCopy15GetBaseFileNameEv");
extern "C" long _stub_fn_1311() { return 0; }
// SongInfoCopy::GetName() const
extern "C" __attribute__((weak, used)) long _stub_fn_1312() __asm__("_ZNK12SongInfoCopy7GetNameEv");
extern "C" long _stub_fn_1312() { return 0; }
// SongInfoCopy::GetPans() const
extern "C" __attribute__((weak, used)) long _stub_fn_1313() __asm__("_ZNK12SongInfoCopy7GetPansEv");
extern "C" long _stub_fn_1313() { return 0; }
// SongInfoCopy::GetCores() const
extern "C" __attribute__((weak, used)) long _stub_fn_1314() __asm__("_ZNK12SongInfoCopy8GetCoresEv");
extern "C" long _stub_fn_1314() { return 0; }
// SongInfoCopy::GetTracks() const
extern "C" __attribute__((weak, used)) long _stub_fn_1315() __asm__("_ZNK12SongInfoCopy9GetTracksEv");
extern "C" long _stub_fn_1315() { return 0; }
// SongMetadata::GameOrigin() const
extern "C" __attribute__((weak, used)) long _stub_fn_1316() __asm__("_ZNK12SongMetadata10GameOriginEv");
extern "C" long _stub_fn_1316() { return 0; }
// SongMetadata::ID() const
extern "C" __attribute__((weak, used)) long _stub_fn_1317() __asm__("_ZNK12SongMetadata2IDEv");
extern "C" long _stub_fn_1317() { return 0; }
// SongMetadata::IsOnDisc() const
extern "C" __attribute__((weak, used)) long _stub_fn_1318() __asm__("_ZNK12SongMetadata8IsOnDiscEv");
extern "C" long _stub_fn_1318() { return 0; }
// BinkMovieImpl::MsPerFrame() const
extern "C" __attribute__((weak, used)) long _stub_fn_1319() __asm__("_ZNK13BinkMovieImpl10MsPerFrameEv");
extern "C" long _stub_fn_1319() { return 0; }
// BinkMovieImpl::Ready() const
extern "C" __attribute__((weak, used)) long _stub_fn_1320() __asm__("_ZNK13BinkMovieImpl5ReadyEv");
extern "C" long _stub_fn_1320() { return 0; }
// BinkMovieImpl::IsOpen() const
extern "C" __attribute__((weak, used)) long _stub_fn_1321() __asm__("_ZNK13BinkMovieImpl6IsOpenEv");
extern "C" long _stub_fn_1321() { return 0; }
// BinkMovieImpl::GetFrame() const
extern "C" __attribute__((weak, used)) long _stub_fn_1322() __asm__("_ZNK13BinkMovieImpl8GetFrameEv");
extern "C" long _stub_fn_1322() { return 0; }
// BinkMovieImpl::IsLoading() const
extern "C" __attribute__((weak, used)) long _stub_fn_1323() __asm__("_ZNK13BinkMovieImpl9IsLoadingEv");
extern "C" long _stub_fn_1323() { return 0; }
// BinkMovieImpl::NumFrames() const
extern "C" __attribute__((weak, used)) long _stub_fn_1324() __asm__("_ZNK13BinkMovieImpl9NumFramesEv");
extern "C" long _stub_fn_1324() { return 0; }
// CharClipGroup::FindClip(char const*) const
extern "C" __attribute__((weak, used)) long _stub_fn_1325() __asm__("_ZNK13CharClipGroup8FindClipEPKc");
extern "C" long _stub_fn_1325() { return 0; }
// HamStoreOffer::Cmp(StoreOffer const&, Symbol) const
extern "C" __attribute__((weak, used)) long _stub_fn_1326() __asm__("_ZNK13HamStoreOffer3CmpERK10StoreOffer6Symbol");
extern "C" long _stub_fn_1326() { return 0; }
// HamStorePanel::IsSpecialOfferOwned(Symbol) const
extern "C" __attribute__((weak, used)) long _stub_fn_1327() __asm__("_ZNK13HamStorePanel19IsSpecialOfferOwnedE6Symbol");
extern "C" long _stub_fn_1327() { return 0; }
// HamStorePanel::GetOfferIDsToEnumerate(std::vector<unsigned long long, std::allocator<unsigned long long> >&, bool) const
extern "C" __attribute__((weak, used)) long _stub_fn_1328() __asm__("_ZNK13HamStorePanel22GetOfferIDsToEnumerateERSt6vectorIySaIyEEb");
extern "C" long _stub_fn_1328() { return 0; }
// MetaPerformer::CheckRecommendedPracticeMove(String, int) const
extern "C" __attribute__((weak, used)) long _stub_fn_1329() __asm__("_ZNK13MetaPerformer28CheckRecommendedPracticeMoveE6Stringi");
extern "C" long _stub_fn_1329() { return 0; }
// RecordedFrame::MakeSkeletonFrame(SkeletonFrame&, int) const
extern "C" __attribute__((weak, used)) long _stub_fn_1330() __asm__("_ZNK13RecordedFrame17MakeSkeletonFrameER13SkeletonFramei");
extern "C" long _stub_fn_1330() { return 0; }
// VenueProvider::NumData() const
extern "C" __attribute__((weak, used)) long _stub_fn_1331() __asm__("_ZNK13VenueProvider7NumDataEv");
extern "C" long _stub_fn_1331() { return 0; }
// BoxMapLighting::ApplyLight(BoxLightArray<BoxMapLighting::LightParams_Spot, 50> const&, Vector3 const&) const
extern "C" __attribute__((weak, used)) long _stub_fn_1332() __asm__("_ZNK14BoxMapLighting10ApplyLightERK13BoxLightArrayINS_16LightParams_SpotELi50EERK7Vector3");
extern "C" long _stub_fn_1332() { return 0; }
// BoxMapLighting::ApplyLight(BoxLightArray<BoxMapLighting::LightParams_Point, 50> const&, Vector3 const&) const
extern "C" __attribute__((weak, used)) long _stub_fn_1333() __asm__("_ZNK14BoxMapLighting10ApplyLightERK13BoxLightArrayINS_17LightParams_PointELi50EERK7Vector3");
extern "C" long _stub_fn_1333() { return 0; }
// BoxMapLighting::ApplyLight(BoxLightArray<BoxMapLighting::LightParams_Directional, 50> const&) const
extern "C" __attribute__((weak, used)) long _stub_fn_1334() __asm__("_ZNK14BoxMapLighting10ApplyLightERK13BoxLightArrayINS_23LightParams_DirectionalELi50EE");
extern "C" long _stub_fn_1334() { return 0; }
// DancerSkeleton::CameraToPlayerXfm(SkeletonCoordSys, Transform&) const
extern "C" __attribute__((weak, used)) long _stub_fn_1335() __asm__("_ZNK14DancerSkeleton17CameraToPlayerXfmE16SkeletonCoordSysR9Transform");
extern "C" long _stub_fn_1335() { return 0; }
// NetCacheLoader::GetFailType() const
extern "C" __attribute__((weak, used)) long _stub_fn_1336() __asm__("_ZNK14NetCacheLoader11GetFailTypeEv");
extern "C" long _stub_fn_1336() { return 0; }
// HamSongMetadata::Album() const
extern "C" __attribute__((weak, used)) long _stub_fn_1337() __asm__("_ZNK15HamSongMetadata5AlbumEv");
extern "C" long _stub_fn_1337() { return 0; }
// HamSongMetadata::Title() const
extern "C" __attribute__((weak, used)) long _stub_fn_1338() __asm__("_ZNK15HamSongMetadata5TitleEv");
extern "C" long _stub_fn_1338() { return 0; }
// LiveCameraInput::GetAutoexposure() const
extern "C" __attribute__((weak, used)) long _stub_fn_1339() __asm__("_ZNK15LiveCameraInput15GetAutoexposureEv");
extern "C" long _stub_fn_1339() { return 0; }
// LiveCameraInput::SetTrackedSkeletons(int, int) const
extern "C" __attribute__((weak, used)) long _stub_fn_1340() __asm__("_ZNK15LiveCameraInput19SetTrackedSkeletonsEii");
extern "C" long _stub_fn_1340() { return 0; }
// LiveCameraInput::GetTweakedAutoexposure() const
extern "C" __attribute__((weak, used)) long _stub_fn_1341() __asm__("_ZNK15LiveCameraInput22GetTweakedAutoexposureEv");
extern "C" long _stub_fn_1341() { return 0; }
// VoiceInputPanel::CreatePlaylistEditorGrammar() const
extern "C" __attribute__((weak, used)) long _stub_fn_1342() __asm__("_ZNK15VoiceInputPanel27CreatePlaylistEditorGrammarEv");
extern "C" long _stub_fn_1342() { return 0; }
// CampaignProgress::IsCampaignIntroCompleted() const
extern "C" __attribute__((weak, used)) long _stub_fn_1343() __asm__("_ZNK16CampaignProgress24IsCampaignIntroCompletedEv");
extern "C" long _stub_fn_1343() { return 0; }
// CampaignProgress::IsCampaignMindControlCompleted() const
extern "C" __attribute__((weak, used)) long _stub_fn_1344() __asm__("_ZNK16CampaignProgress30IsCampaignMindControlCompletedEv");
extern "C" long _stub_fn_1344() { return 0; }
// HamStoreProvider::ShowBrowserPurchased(StoreOffer const*) const
extern "C" __attribute__((weak, used)) long _stub_fn_1345() __asm__("_ZNK16HamStoreProvider20ShowBrowserPurchasedEPK10StoreOffer");
extern "C" long _stub_fn_1345() { return 0; }
// MetaMusicManager::GetScene(Symbol) const
extern "C" __attribute__((weak, used)) long _stub_fn_1346() __asm__("_ZNK16MetaMusicManager8GetSceneE6Symbol");
extern "C" long _stub_fn_1346() { return 0; }
// NavListItemSortCmp::GetDateCmp() const
extern "C" __attribute__((weak, used)) long _stub_fn_1347() __asm__("_ZNK18NavListItemSortCmp10GetDateCmpEv");
extern "C" long _stub_fn_1347() { return 0; }
// NavListItemSortCmp::GetSongCmp() const
extern "C" __attribute__((weak, used)) long _stub_fn_1348() __asm__("_ZNK18NavListItemSortCmp10GetSongCmpEv");
extern "C" long _stub_fn_1348() { return 0; }
// NavListItemSortCmp::GetAlbumCmp() const
extern "C" __attribute__((weak, used)) long _stub_fn_1349() __asm__("_ZNK18NavListItemSortCmp11GetAlbumCmpEv");
extern "C" long _stub_fn_1349() { return 0; }
// NavListItemSortCmp::GetVenueCmp() const
extern "C" __attribute__((weak, used)) long _stub_fn_1350() __asm__("_ZNK18NavListItemSortCmp11GetVenueCmpEv");
extern "C" long _stub_fn_1350() { return 0; }
// NavListItemSortCmp::GetArtistCmp() const
extern "C" __attribute__((weak, used)) long _stub_fn_1351() __asm__("_ZNK18NavListItemSortCmp12GetArtistCmpEv");
extern "C" long _stub_fn_1351() { return 0; }
// NavListItemSortCmp::GetDecadeCmp() const
extern "C" __attribute__((weak, used)) long _stub_fn_1352() __asm__("_ZNK18NavListItemSortCmp12GetDecadeCmpEv");
extern "C" long _stub_fn_1352() { return 0; }
// NavListItemSortCmp::GetLocationCmp() const
extern "C" __attribute__((weak, used)) long _stub_fn_1353() __asm__("_ZNK18NavListItemSortCmp14GetLocationCmpEv");
extern "C" long _stub_fn_1353() { return 0; }
// NavListItemSortCmp::GetDifficultyCmp() const
extern "C" __attribute__((weak, used)) long _stub_fn_1354() __asm__("_ZNK18NavListItemSortCmp16GetDifficultyCmpEv");
extern "C" long _stub_fn_1354() { return 0; }
// NavListItemSortCmp::GetMQSongCharCmp() const
extern "C" __attribute__((weak, used)) long _stub_fn_1355() __asm__("_ZNK18NavListItemSortCmp16GetMQSongCharCmpEv");
extern "C" long _stub_fn_1355() { return 0; }
// NavListItemSortCmp::GetVocalPartsCmp() const
extern "C" __attribute__((weak, used)) long _stub_fn_1356() __asm__("_ZNK18NavListItemSortCmp16GetVocalPartsCmpEv");
extern "C" long _stub_fn_1356() { return 0; }
// NavListItemSortCmp::GetPlaylistTypeCmp() const
extern "C" __attribute__((weak, used)) long _stub_fn_1357() __asm__("_ZNK18NavListItemSortCmp18GetPlaylistTypeCmpEv");
extern "C" long _stub_fn_1357() { return 0; }
// NavListItemSortCmp::GetChallengeScoreCmp() const
extern "C" __attribute__((weak, used)) long _stub_fn_1358() __asm__("_ZNK18NavListItemSortCmp20GetChallengeScoreCmpEv");
extern "C" long _stub_fn_1358() { return 0; }
// NavListItemSortCmp::GetFitnessCalorieSortCmp() const
extern "C" __attribute__((weak, used)) long _stub_fn_1359() __asm__("_ZNK18NavListItemSortCmp24GetFitnessCalorieSortCmpEv");
extern "C" long _stub_fn_1359() { return 0; }
// PlaylistSortByType::NewHeaderNode(NavListItemNode*, NavListItemNode*) const
extern "C" __attribute__((weak, used)) long _stub_fn_1360() __asm__("_ZNK18PlaylistSortByType13NewHeaderNodeEP15NavListItemNodeS1_");
extern "C" long _stub_fn_1360() { return 0; }
// SongSortByLocation::NewHeaderNode(NavListItemNode*) const
extern "C" __attribute__((weak, used)) long _stub_fn_1361() __asm__("_ZNK18SongSortByLocation13NewHeaderNodeEP15NavListItemNode");
extern "C" long _stub_fn_1361() { return 0; }
// SongSortByLocation::NewHeaderNode(NavListItemNode*, NavListItemNode*) const
extern "C" __attribute__((weak, used)) long _stub_fn_1362() __asm__("_ZNK18SongSortByLocation13NewHeaderNodeEP15NavListItemNodeS1_");
extern "C" long _stub_fn_1362() { return 0; }
// AccomplishmentGroup::GetAward() const
extern "C" __attribute__((weak, used)) long _stub_fn_1363() __asm__("_ZNK19AccomplishmentGroup8GetAwardEv");
extern "C" long _stub_fn_1363() { return 0; }
// ChallengeHeaderNode::IsActive() const
extern "C" __attribute__((weak, used)) long _stub_fn_1364() __asm__("_ZNK19ChallengeHeaderNode8IsActiveEv");
extern "C" long _stub_fn_1364() { return 0; }
// RndAmbientOcclusion::BurnTransform(RndMesh*, std::__cxx11::list<RndMesh*, std::allocator<RndMesh*> >&) const
extern "C" __attribute__((weak, used)) long _stub_fn_1365() __asm__("_ZNK19RndAmbientOcclusion13BurnTransformEP7RndMeshRNSt7__cxx114listIS1_SaIS1_EEE");
extern "C" long _stub_fn_1365() { return 0; }
// RndAmbientOcclusion::IsValid_Tessellate(RndMesh const*, ObjectDir const*) const
extern "C" __attribute__((weak, used)) long _stub_fn_1366() __asm__("_ZNK19RndAmbientOcclusion18IsValid_TessellateEPK7RndMeshPK9ObjectDir");
extern "C" long _stub_fn_1366() { return 0; }
// WeightInputProvider::GetKgForPounds(float) const
extern "C" __attribute__((weak, used)) long _stub_fn_1367() __asm__("_ZNK19WeightInputProvider14GetKgForPoundsEf");
extern "C" long _stub_fn_1367() { return 0; }
// Ham1DisplacementNode::Errors(ErrorFrameInput const&, ErrorNodeInput const&, Ham1DisplacementNode::ErrorData&, BaseDisplacementNode::DisplacementData&, BaseDisplacementNode::Ham1DisplacementData&) const
extern "C" __attribute__((weak, used)) long _stub_fn_1368() __asm__("_ZNK20Ham1DisplacementNode6ErrorsERK15ErrorFrameInputRK14ErrorNodeInputRNS_9ErrorDataERN20BaseDisplacementNode16DisplacementDataERNS8_20Ham1DisplacementDataE");
extern "C" long _stub_fn_1368() { return 0; }
// FreestyleMoveRecorder::CompareSkeletonPositions(BaseSkeleton const*, BaseSkeleton const*, float) const
extern "C" __attribute__((weak, used)) long _stub_fn_1369() __asm__("_ZNK21FreestyleMoveRecorder24CompareSkeletonPositionsEPK12BaseSkeletonS2_f");
extern "C" long _stub_fn_1369() { return 0; }
// GameEndedDataPointJob::CompileMoveRatings(String&, int, bool) const
extern "C" __attribute__((weak, used)) long _stub_fn_1370() __asm__("_ZNK21GameEndedDataPointJob18CompileMoveRatingsER6Stringib");
extern "C" long _stub_fn_1370() { return 0; }
// MultiUserGesturePanel::HasNavList() const
extern "C" __attribute__((weak, used)) long _stub_fn_1371() __asm__("_ZNK21MultiUserGesturePanel10HasNavListEv");
extern "C" long _stub_fn_1371() { return 0; }
// AccomplishmentProgress::GetNumCompleted() const
extern "C" __attribute__((weak, used)) long _stub_fn_1372() __asm__("_ZNK22AccomplishmentProgress15GetNumCompletedEv");
extern "C" long _stub_fn_1372() { return 0; }
// AccomplishmentProgress::GetTotalSongsPlayed() const
extern "C" __attribute__((weak, used)) long _stub_fn_1373() __asm__("_ZNK22AccomplishmentProgress19GetTotalSongsPlayedEv");
extern "C" long _stub_fn_1373() { return 0; }
// AccomplishmentProgress::GetFlawlessMoveCount() const
extern "C" __attribute__((weak, used)) long _stub_fn_1374() __asm__("_ZNK22AccomplishmentProgress20GetFlawlessMoveCountEv");
extern "C" long _stub_fn_1374() { return 0; }
// AccomplishmentProgress::GetTotalCampaignSongsPlayed() const
extern "C" __attribute__((weak, used)) long _stub_fn_1375() __asm__("_ZNK22AccomplishmentProgress27GetTotalCampaignSongsPlayedEv");
extern "C" long _stub_fn_1375() { return 0; }
// FitnessCalorieHeaderNode::IsActive() const
extern "C" __attribute__((weak, used)) long _stub_fn_1376() __asm__("_ZNK24FitnessCalorieHeaderNode8IsActiveEv");
extern "C" long _stub_fn_1376() { return 0; }
// AppMiniLeaderboardDisplay::Text(int, int, UIListLabel*, UILabel*) const
extern "C" __attribute__((weak, used)) long _stub_fn_1377() __asm__("_ZNK25AppMiniLeaderboardDisplay4TextEiiP11UIListLabelP7UILabel");
extern "C" long _stub_fn_1377() { return 0; }
// DirectionGestureFilterDoubleUser::IsHandValid(Skeleton const&) const
extern "C" __attribute__((weak, used)) long _stub_fn_1378() __asm__("_ZNK32DirectionGestureFilterDoubleUser11IsHandValidERK8Skeleton");
extern "C" long _stub_fn_1378() { return 0; }
// DirectionGestureFilterDoubleUser::IsValidScrollPos(Skeleton const&) const
extern "C" __attribute__((weak, used)) long _stub_fn_1379() __asm__("_ZNK32DirectionGestureFilterDoubleUser16IsValidScrollPosERK8Skeleton");
extern "C" long _stub_fn_1379() { return 0; }
// DirectionGestureFilterDoubleUser::GetValidSkeletons(int&, int&) const
extern "C" __attribute__((weak, used)) long _stub_fn_1380() __asm__("_ZNK32DirectionGestureFilterDoubleUser17GetValidSkeletonsERiS0_");
extern "C" long _stub_fn_1380() { return 0; }
// DirectionGestureFilterSingleUser::HandAtSide(Skeleton const&, float, float, float) const
extern "C" __attribute__((weak, used)) long _stub_fn_1381() __asm__("_ZNK32DirectionGestureFilterSingleUser10HandAtSideERK8Skeletonfff");
extern "C" long _stub_fn_1381() { return 0; }
// Game::GetNumRestarts() const
extern "C" __attribute__((weak, used)) long _stub_fn_1382() __asm__("_ZNK4Game14GetNumRestartsEv");
extern "C" long _stub_fn_1382() { return 0; }
// Pose::CurrentScore() const
extern "C" __attribute__((weak, used)) long _stub_fn_1383() __asm__("_ZNK4Pose12CurrentScoreEv");
extern "C" long _stub_fn_1383() { return 0; }
// Award::GetName() const
extern "C" __attribute__((weak, used)) long _stub_fn_1384() __asm__("_ZNK5Award7GetNameEv");
extern "C" long _stub_fn_1384() { return 0; }
// Award::IsSilent() const
extern "C" __attribute__((weak, used)) long _stub_fn_1385() __asm__("_ZNK5Award8IsSilentEv");
extern "C" long _stub_fn_1385() { return 0; }
// Synth::GetNumMics() const
extern "C" __attribute__((weak, used)) long _stub_fn_1386() __asm__("_ZNK5Synth10GetNumMicsEv");
extern "C" long _stub_fn_1386() { return 0; }
// RndCam::GetViewProjectXfms(Transform&, Hmx::Matrix4&) const
extern "C" __attribute__((weak, used)) long _stub_fn_1387() __asm__("_ZNK6RndCam18GetViewProjectXfmsER9TransformRN3Hmx7Matrix4E");
extern "C" long _stub_fn_1387() { return 0; }
// UIList::NumData() const
extern "C" __attribute__((weak, used)) long _stub_fn_1388() __asm__("_ZNK6UIList7NumDataEv");
extern "C" long _stub_fn_1388() { return 0; }
// HamMove::PSNRToDetectFrac(float) const
extern "C" __attribute__((weak, used)) long _stub_fn_1389() __asm__("_ZNK7HamMove16PSNRToDetectFracEf");
extern "C" long _stub_fn_1389() { return 0; }
// Profile::GetPadNum() const
extern "C" __attribute__((weak, used)) long _stub_fn_1390() __asm__("_ZNK7Profile9GetPadNumEv");
extern "C" long _stub_fn_1390() { return 0; }
// RndText::GetWidthHeightBox(Box&) const
extern "C" __attribute__((weak, used)) long _stub_fn_1391() __asm__("_ZNK7RndText17GetWidthHeightBoxER3Box");
extern "C" long _stub_fn_1391() { return 0; }
// SortCmp::operator()(StoreOffer const*, StoreOffer const*) const
extern "C" __attribute__((weak, used)) long _stub_fn_1392() __asm__("_ZNK7SortCmpclEPK10StoreOfferS2_");
extern "C" long _stub_fn_1392() { return 0; }
// UIColor::GetColor() const
extern "C" __attribute__((weak, used)) long _stub_fn_1393() __asm__("_ZNK7UIColor8GetColorEv");
extern "C" long _stub_fn_1393() { return 0; }
// CharEyes::ListPollChildren(std::__cxx11::list<RndPollable*, std::allocator<RndPollable*> >&) const
extern "C" __attribute__((weak, used)) long _stub_fn_1394() __asm__("_ZNK8CharEyes16ListPollChildrenERNSt7__cxx114listIP11RndPollableSaIS3_EEE");
extern "C" long _stub_fn_1394() { return 0; }
// DateTime::DayOfWeek() const
extern "C" __attribute__((weak, used)) long _stub_fn_1395() __asm__("_ZNK8DateTime9DayOfWeekEv");
extern "C" long _stub_fn_1395() { return 0; }
// Skeleton::Displacements(SkeletonHistory const*, SkeletonCoordSys, int, Vector3*, int&) const
extern "C" __attribute__((weak, used)) long _stub_fn_1396() __asm__("_ZNK8Skeleton13DisplacementsEPK15SkeletonHistory16SkeletonCoordSysiP7Vector3Ri");
extern "C" long _stub_fn_1396() { return 0; }
// AllocInfo::PrintForReport(TextStream&) const
extern "C" __attribute__((weak, used)) long _stub_fn_1397() __asm__("_ZNK9AllocInfo14PrintForReportER10TextStream");
extern "C" long _stub_fn_1397() { return 0; }
// MoveGraph::GetNonConstMoveParent(Symbol) const
extern "C" __attribute__((weak, used)) long _stub_fn_1398() __asm__("_ZNK9MoveGraph21GetNonConstMoveParentE6Symbol");
extern "C" long _stub_fn_1398() { return 0; }
// ObjDirPtr<UILabelDir>::IsLoaded() const
extern "C" __attribute__((weak, used)) long _stub_fn_1399() __asm__("_ZNK9ObjDirPtrI10UILabelDirE8IsLoadedEv");
extern "C" long _stub_fn_1399() { return 0; }
// ObjDirPtr<ObjectDir>::IsLoaded() const
extern "C" __attribute__((weak, used)) long _stub_fn_1400() __asm__("_ZNK9ObjDirPtrI9ObjectDirE8IsLoadedEv");
extern "C" long _stub_fn_1400() { return 0; }
// operator>>(BinStream&, FilePath&) — implemented in FilePath.cpp
// operator>>(BinStream&, FlowTrigger::PropTriggerDefn&)
extern "C" __attribute__((weak, used)) long _stub_fn_1420() __asm__("_ZrsR9BinStreamRN11FlowTrigger15PropTriggerDefnE");
extern "C" long _stub_fn_1420() { return 0; }
// non-virtual thunk to CharFeedback::Poll()
extern "C" __attribute__((weak, used)) long _stub_fn_1421() __asm__("_ZThn104_N12CharFeedback4PollEv");
extern "C" long _stub_fn_1421() { return 0; }
// non-virtual thunk to DepthBuffer3D::Copy(Hmx::Object const*, Hmx::Object::CopyType)
extern "C" __attribute__((weak, used)) long _stub_fn_1422() __asm__("_ZThn104_N13DepthBuffer3D4CopyEPKN3Hmx6ObjectENS1_8CopyTypeE");
extern "C" long _stub_fn_1422() { return 0; }
// non-virtual thunk to DepthBuffer3D::Load(BinStream&)
extern "C" __attribute__((weak, used)) long _stub_fn_1423() __asm__("_ZThn104_N13DepthBuffer3D4LoadER9BinStream");
extern "C" long _stub_fn_1423() { return 0; }
// non-virtual thunk to DepthBuffer3D::Save(BinStream&)
extern "C" __attribute__((weak, used)) long _stub_fn_1424() __asm__("_ZThn104_N13DepthBuffer3D4SaveER9BinStream");
extern "C" long _stub_fn_1424() { return 0; }
// non-virtual thunk to PhysicsVolume::Load(BinStream&)
extern "C" __attribute__((weak, used)) long _stub_fn_1425() __asm__("_ZThn104_N13PhysicsVolume4LoadER9BinStream");
extern "C" long _stub_fn_1425() { return 0; }
// non-virtual thunk to StreamRecorder::Poll()
extern "C" __attribute__((weak, used)) long _stub_fn_1426() __asm__("_ZThn104_N14StreamRecorder4PollEv");
extern "C" long _stub_fn_1426() { return 0; }
// non-virtual thunk to WorldReflection::Highlight()
extern "C" __attribute__((weak, used)) long _stub_fn_1427() __asm__("_ZThn104_N15WorldReflection9HighlightEv");
extern "C" long _stub_fn_1427() { return 0; }
// non-virtual thunk to RndMesh::Replace(ObjRef*, Hmx::Object*)
extern "C" __attribute__((weak, used)) long _stub_fn_1428() __asm__("_ZThn104_N7RndMesh7ReplaceEP6ObjRefPN3Hmx6ObjectE");
extern "C" long _stub_fn_1428() { return 0; }
// non-virtual thunk to MoveDir::UpdateOverlay(RndOverlay*, float)
extern "C" __attribute__((weak, used)) long _stub_fn_1429() __asm__("_ZThn1160_N7MoveDir13UpdateOverlayEP10RndOverlayf");
extern "C" long _stub_fn_1429() { return 0; }
// non-virtual thunk to UIList::NumData() const
extern "C" __attribute__((weak, used)) long _stub_fn_1430() __asm__("_ZThn120_NK6UIList7NumDataEv");
extern "C" long _stub_fn_1430() { return 0; }
// non-virtual thunk to HamNavList::CompleteScroll(UIListState const&)
extern "C" __attribute__((weak, used)) long _stub_fn_1431() __asm__("_ZThn144_N10HamNavList14CompleteScrollERK11UIListState");
extern "C" long _stub_fn_1431() { return 0; }
// non-virtual thunk to HamNavList::PostUpdate(SkeletonUpdateData const*)
extern "C" __attribute__((weak, used)) long _stub_fn_1432() __asm__("_ZThn152_N10HamNavList10PostUpdateEPK18SkeletonUpdateData");
extern "C" long _stub_fn_1432() { return 0; }
// non-virtual thunk to HamNavList::Clear()
extern "C" __attribute__((weak, used)) long _stub_fn_1433() __asm__("_ZThn152_N10HamNavList5ClearEv");
extern "C" long _stub_fn_1433() { return 0; }
// non-virtual thunk to AppMiniLeaderboardDisplay::Text(int, int, UIListLabel*, UILabel*) const
extern "C" __attribute__((weak, used)) long _stub_fn_1434() __asm__("_ZThn168_NK25AppMiniLeaderboardDisplay4TextEiiP11UIListLabelP7UILabel");
extern "C" long _stub_fn_1434() { return 0; }
// non-virtual thunk to HamCamShot::Load(BinStream&)
extern "C" __attribute__((weak, used)) long _stub_fn_1435() __asm__("_ZThn16_N10HamCamShot4LoadER9BinStream");
extern "C" long _stub_fn_1435() { return 0; }
// non-virtual thunk to RndParticleSys::Poll()
extern "C" __attribute__((weak, used)) long _stub_fn_1436() __asm__("_ZThn16_N14RndParticleSys4PollEv");
extern "C" long _stub_fn_1436() { return 0; }
// non-virtual thunk to RndGroup::DrawShowing()
extern "C" __attribute__((weak, used)) long _stub_fn_1437() __asm__("_ZThn16_N8RndGroup11DrawShowingEv");
extern "C" long _stub_fn_1437() { return 0; }
// non-virtual thunk to Flow::Copy(Hmx::Object const*, Hmx::Object::CopyType)
extern "C" __attribute__((weak, used)) long _stub_fn_1438() __asm__("_ZThn192_N4Flow4CopyEPKN3Hmx6ObjectENS1_8CopyTypeE");
extern "C" long _stub_fn_1438() { return 0; }
// non-virtual thunk to RndParticleSys::Load(BinStream&)
extern "C" __attribute__((weak, used)) long _stub_fn_1439() __asm__("_ZThn24_N14RndParticleSys4LoadER9BinStream");
extern "C" long _stub_fn_1439() { return 0; }
// non-virtual thunk to RndParticleSys::Replace(ObjRef*, Hmx::Object*)
extern "C" __attribute__((weak, used)) long _stub_fn_1440() __asm__("_ZThn24_N14RndParticleSys7ReplaceEP6ObjRefPN3Hmx6ObjectE");
extern "C" long _stub_fn_1440() { return 0; }
// non-virtual thunk to WorldInstance::Load(BinStream&)
extern "C" __attribute__((weak, used)) long _stub_fn_1441() __asm__("_ZThn280_N13WorldInstance4LoadER9BinStream");
extern "C" long _stub_fn_1441() { return 0; }
// non-virtual thunk to MoveDir::DrawShowing()
extern "C" __attribute__((weak, used)) long _stub_fn_1442() __asm__("_ZThn280_N7MoveDir11DrawShowingEv");
extern "C" long _stub_fn_1442() { return 0; }
// non-virtual thunk to WorldDir::DrawShowing()
extern "C" __attribute__((weak, used)) long _stub_fn_1443() __asm__("_ZThn280_N8WorldDir11DrawShowingEv");
extern "C" long _stub_fn_1443() { return 0; }
// non-virtual thunk to Character::DrawShowing()
extern "C" __attribute__((weak, used)) long _stub_fn_1444() __asm__("_ZThn280_N9Character11DrawShowingEv");
extern "C" long _stub_fn_1444() { return 0; }
// non-virtual thunk to HamStorePanel::ContentMounted(char const*, char const*)
extern "C" __attribute__((weak, used)) long _stub_fn_1445() __asm__("_ZThn288_N13HamStorePanel14ContentMountedEPKcS1_");
extern "C" long _stub_fn_1445() { return 0; }
// non-virtual thunk to HamStorePanel::ContentDiscovered(Symbol)
extern "C" __attribute__((weak, used)) long _stub_fn_1446() __asm__("_ZThn288_N13HamStorePanel17ContentDiscoveredE6Symbol");
extern "C" long _stub_fn_1446() { return 0; }
// non-virtual thunk to HamStorePanel::ContentTitleDiscovered(unsigned int, Symbol)
extern "C" __attribute__((weak, used)) long _stub_fn_1447() __asm__("_ZThn288_N13HamStorePanel22ContentTitleDiscoveredEj6Symbol");
extern "C" long _stub_fn_1447() { return 0; }
// non-virtual thunk to RndEnviron::Load(BinStream&)
extern "C" __attribute__((weak, used)) long _stub_fn_1448() __asm__("_ZThn312_N10RndEnviron4LoadER9BinStream");
extern "C" long _stub_fn_1448() { return 0; }
// non-virtual thunk to WorldCrowd3DCharHandle::SyncProperty(DataNode&, DataArray*, int, PropOp)
extern "C" __attribute__((weak, used)) long _stub_fn_1449() __asm__("_ZThn312_N22WorldCrowd3DCharHandle12SyncPropertyER8DataNodeP9DataArrayi6PropOp");
extern "C" long _stub_fn_1449() { return 0; }
// non-virtual thunk to RndFlare::DrawShowing()
extern "C" __attribute__((weak, used)) long _stub_fn_1450() __asm__("_ZThn312_N8RndFlare11DrawShowingEv");
extern "C" long _stub_fn_1450() { return 0; }
// non-virtual thunk to RndFlare::Mats(std::__cxx11::list<RndMat*, std::allocator<RndMat*> >&, bool)
extern "C" __attribute__((weak, used)) long _stub_fn_1451() __asm__("_ZThn312_N8RndFlare4MatsERNSt7__cxx114listIP6RndMatSaIS3_EEEb");
extern "C" long _stub_fn_1451() { return 0; }
// non-virtual thunk to RndGenerator::DrawShowing()
extern "C" __attribute__((weak, used)) long _stub_fn_1452() __asm__("_ZThn328_N12RndGenerator11DrawShowingEv");
extern "C" long _stub_fn_1452() { return 0; }
// non-virtual thunk to RndGenerator::MakeWorldSphere(Sphere&, bool)
extern "C" __attribute__((weak, used)) long _stub_fn_1453() __asm__("_ZThn328_N12RndGenerator15MakeWorldSphereER6Sphereb");
extern "C" long _stub_fn_1453() { return 0; }
// non-virtual thunk to RndParticleSys::Load(BinStream&)
extern "C" __attribute__((weak, used)) long _stub_fn_1454() __asm__("_ZThn336_N14RndParticleSys4LoadER9BinStream");
extern "C" long _stub_fn_1454() { return 0; }
// non-virtual thunk to RndParticleSys::Mats(std::__cxx11::list<RndMat*, std::allocator<RndMat*> >&, bool)
extern "C" __attribute__((weak, used)) long _stub_fn_1455() __asm__("_ZThn336_N14RndParticleSys4MatsERNSt7__cxx114listIP6RndMatSaIS3_EEEb");
extern "C" long _stub_fn_1455() { return 0; }
// non-virtual thunk to HamListRibbon::EndFrame()
extern "C" __attribute__((weak, used)) long _stub_fn_1456() __asm__("_ZThn384_N13HamListRibbon8EndFrameEv");
extern "C" long _stub_fn_1456() { return 0; }
// non-virtual thunk to WorldInstance::Load(BinStream&)
extern "C" __attribute__((weak, used)) long _stub_fn_1457() __asm__("_ZThn384_N13WorldInstance4LoadER9BinStream");
extern "C" long _stub_fn_1457() { return 0; }
// non-virtual thunk to WorldInstance::Load(BinStream&)
extern "C" __attribute__((weak, used)) long _stub_fn_1458() __asm__("_ZThn400_N13WorldInstance4LoadER9BinStream");
extern "C" long _stub_fn_1458() { return 0; }
// non-virtual thunk to SkeletonViz::Poll()
extern "C" __attribute__((weak, used)) long _stub_fn_1459() __asm__("_ZThn416_N11SkeletonViz4PollEv");
extern "C" long _stub_fn_1459() { return 0; }
// non-virtual thunk to SynthEmitter::Poll()
extern "C" __attribute__((weak, used)) long _stub_fn_1460() __asm__("_ZThn416_N12SynthEmitter4PollEv");
extern "C" long _stub_fn_1460() { return 0; }
// non-virtual thunk to Spotlight::Poll()
extern "C" __attribute__((weak, used)) long _stub_fn_1461() __asm__("_ZThn416_N9Spotlight4PollEv");
extern "C" long _stub_fn_1461() { return 0; }
// non-virtual thunk to Flow::Exit()
extern "C" __attribute__((weak, used)) long _stub_fn_1462() __asm__("_ZThn472_N4Flow4ExitEv");
extern "C" long _stub_fn_1462() { return 0; }
// non-virtual thunk to Flow::Enter()
extern "C" __attribute__((weak, used)) long _stub_fn_1463() __asm__("_ZThn472_N4Flow5EnterEv");
extern "C" long _stub_fn_1463() { return 0; }
// non-virtual thunk to CharIKScale::Poll()
extern "C" __attribute__((weak, used)) long _stub_fn_1464() __asm__("_ZThn56_N11CharIKScale4PollEv");
extern "C" long _stub_fn_1464() { return 0; }
// non-virtual thunk to CharDriver::Poll()
extern "C" __attribute__((weak, used)) long _stub_fn_1465() __asm__("_ZThn64_N10CharDriver4PollEv");
extern "C" long _stub_fn_1465() { return 0; }
// non-virtual thunk to CharDriver::Handle(DataArray*, bool)
extern "C" __attribute__((weak, used)) long _stub_fn_1466() __asm__("_ZThn64_N10CharDriver6HandleEP9DataArrayb");
extern "C" long _stub_fn_1466() { return 0; }
// non-virtual thunk to CharDriver::PollDeps(std::__cxx11::list<Hmx::Object*, std::allocator<Hmx::Object*> >&, std::__cxx11::list<Hmx::Object*, std::allocator<Hmx::Object*> >&)
extern "C" __attribute__((weak, used)) long _stub_fn_1467() __asm__("_ZThn64_N10CharDriver8PollDepsERNSt7__cxx114listIPN3Hmx6ObjectESaIS4_EEES7_");
extern "C" long _stub_fn_1467() { return 0; }
// non-virtual thunk to CharIKHand::Poll()
extern "C" __attribute__((weak, used)) long _stub_fn_1468() __asm__("_ZThn64_N10CharIKHand4PollEv");
extern "C" long _stub_fn_1468() { return 0; }
// non-virtual thunk to CharIKHead::Poll()
extern "C" __attribute__((weak, used)) long _stub_fn_1469() __asm__("_ZThn64_N10CharIKHead4PollEv");
extern "C" long _stub_fn_1469() { return 0; }
// non-virtual thunk to CharLookAt::Poll()
extern "C" __attribute__((weak, used)) long _stub_fn_1470() __asm__("_ZThn64_N10CharLookAt4PollEv");
extern "C" long _stub_fn_1470() { return 0; }
// non-virtual thunk to HamIKEffector::Poll()
extern "C" __attribute__((weak, used)) long _stub_fn_1471() __asm__("_ZThn64_N13HamIKEffector4PollEv");
extern "C" long _stub_fn_1471() { return 0; }
// non-virtual thunk to CharLipSyncDriver::Poll()
extern "C" __attribute__((weak, used)) long _stub_fn_1472() __asm__("_ZThn64_N17CharLipSyncDriver4PollEv");
extern "C" long _stub_fn_1472() { return 0; }
// non-virtual thunk to CharEyes::Exit()
extern "C" __attribute__((weak, used)) long _stub_fn_1473() __asm__("_ZThn64_N8CharEyes4ExitEv");
extern "C" long _stub_fn_1473() { return 0; }
// non-virtual thunk to CharEyes::Poll()
extern "C" __attribute__((weak, used)) long _stub_fn_1474() __asm__("_ZThn64_N8CharEyes4PollEv");
extern "C" long _stub_fn_1474() { return 0; }
// non-virtual thunk to CharEyes::PollDeps(std::__cxx11::list<Hmx::Object*, std::allocator<Hmx::Object*> >&, std::__cxx11::list<Hmx::Object*, std::allocator<Hmx::Object*> >&)
extern "C" __attribute__((weak, used)) long _stub_fn_1475() __asm__("_ZThn64_N8CharEyes8PollDepsERNSt7__cxx114listIPN3Hmx6ObjectESaIS4_EEES7_");
extern "C" long _stub_fn_1475() { return 0; }
// non-virtual thunk to HamDriver::Poll()
extern "C" __attribute__((weak, used)) long _stub_fn_1476() __asm__("_ZThn64_N9HamDriver4PollEv");
extern "C" long _stub_fn_1476() { return 0; }
// non-virtual thunk to CharEyes::ListPollChildren(std::__cxx11::list<RndPollable*, std::allocator<RndPollable*> >&) const
extern "C" __attribute__((weak, used)) long _stub_fn_1477() __asm__("_ZThn64_NK8CharEyes16ListPollChildrenERNSt7__cxx114listIP11RndPollableSaIS3_EEE");
extern "C" long _stub_fn_1477() { return 0; }
// non-virtual thunk to SkeletonClip::PrevSkeleton(Skeleton const&, int, ArchiveSkeleton&, int&) const
extern "C" __attribute__((weak, used)) long _stub_fn_1478() __asm__("_ZThn6616_NK12SkeletonClip12PrevSkeletonERK8SkeletoniR15ArchiveSkeletonRi");
extern "C" long _stub_fn_1478() { return 0; }
// non-virtual thunk to HamCharacter::Poll()
extern "C" __attribute__((weak, used)) long _stub_fn_1479() __asm__("_ZThn712_N12HamCharacter4PollEv");
extern "C" long _stub_fn_1479() { return 0; }
// non-virtual thunk to GestureMgr::PostUpdate(SkeletonUpdateData const*)
extern "C" __attribute__((weak, used)) long _stub_fn_1480() __asm__("_ZThn88_N10GestureMgr10PostUpdateEPK18SkeletonUpdateData");
extern "C" long _stub_fn_1480() { return 0; }
// non-virtual thunk to NgPostProc::DoPost()
extern "C" __attribute__((weak, used)) long _stub_fn_1481() __asm__("_ZThn88_N10NgPostProc6DoPostEv");
extern "C" long _stub_fn_1481() { return 0; }
// non-virtual thunk to NgPostProc::EndWorld()
extern "C" __attribute__((weak, used)) long _stub_fn_1482() __asm__("_ZThn88_N10NgPostProc8EndWorldEv");
extern "C" long _stub_fn_1482() { return 0; }
// non-virtual thunk to SampleInst::SynthPoll()
extern "C" __attribute__((weak, used)) long _stub_fn_1483() __asm__("_ZThn88_N10SampleInst9SynthPollEv");
extern "C" long _stub_fn_1483() { return 0; }
// non-virtual thunk to MidiInstrument::SynthPoll()
extern "C" __attribute__((weak, used)) long _stub_fn_1484() __asm__("_ZThn88_N14MidiInstrument9SynthPollEv");
extern "C" long _stub_fn_1484() { return 0; }
// non-virtual thunk to RndSoftParticleBuffer::DoPost()
extern "C" __attribute__((weak, used)) long _stub_fn_1485() __asm__("_ZThn88_N21RndSoftParticleBuffer6DoPostEv");
extern "C" long _stub_fn_1485() { return 0; }
// non-virtual thunk to Synth::UpdateOverlay(RndOverlay*, float)
extern "C" __attribute__((weak, used)) long _stub_fn_1486() __asm__("_ZThn88_N5Synth13UpdateOverlayEP10RndOverlayf");
extern "C" long _stub_fn_1486() { return 0; }
// non-virtual thunk to NgDOFProc::DoPost()
extern "C" __attribute__((weak, used)) long _stub_fn_1487() __asm__("_ZThn88_N9NgDOFProc6DoPostEv");
extern "C" long _stub_fn_1487() { return 0; }
// non-virtual thunk to CharDriver::SyncProperty(DataNode&, DataArray*, int, PropOp)
extern "C" __attribute__((weak, used)) long _stub_fn_1488() __asm__("_ZThn8_N10CharDriver12SyncPropertyER8DataNodeP9DataArrayi6PropOp");
extern "C" long _stub_fn_1488() { return 0; }
// non-virtual thunk to CharDriver::Copy(Hmx::Object const*, Hmx::Object::CopyType)
extern "C" __attribute__((weak, used)) long _stub_fn_1489() __asm__("_ZThn8_N10CharDriver4CopyEPKN3Hmx6ObjectENS1_8CopyTypeE");
extern "C" long _stub_fn_1489() { return 0; }
// non-virtual thunk to CharDriver::Handle(DataArray*, bool)
extern "C" __attribute__((weak, used)) long _stub_fn_1490() __asm__("_ZThn8_N10CharDriver6HandleEP9DataArrayb");
extern "C" long _stub_fn_1490() { return 0; }
// non-virtual thunk to HamRegulate::Poll()
extern "C" __attribute__((weak, used)) long _stub_fn_1491() __asm__("_ZThn8_N11HamRegulate4PollEv");
extern "C" long _stub_fn_1491() { return 0; }
// non-virtual thunk to CharServoBone::PollDeps(std::__cxx11::list<Hmx::Object*, std::allocator<Hmx::Object*> >&, std::__cxx11::list<Hmx::Object*, std::allocator<Hmx::Object*> >&)
extern "C" __attribute__((weak, used)) long _stub_fn_1492() __asm__("_ZThn8_N13CharServoBone8PollDepsERNSt7__cxx114listIPN3Hmx6ObjectESaIS4_EEES7_");
extern "C" long _stub_fn_1492() { return 0; }
// non-virtual thunk to TransConstraint::Poll()
extern "C" __attribute__((weak, used)) long _stub_fn_1493() __asm__("_ZThn8_N15TransConstraint4PollEv");
extern "C" long _stub_fn_1493() { return 0; }
// non-virtual thunk to FitnessCalorieSortMgr::Handle(DataArray*, bool)
extern "C" __attribute__((weak, used)) long _stub_fn_1494() __asm__("_ZThn8_N21FitnessCalorieSortMgr6HandleEP9DataArrayb");
extern "C" long _stub_fn_1494() { return 0; }
// non-virtual thunk to CharEyes::Load(BinStream&)
extern "C" __attribute__((weak, used)) long _stub_fn_1495() __asm__("_ZThn8_N8CharEyes4LoadER9BinStream");
extern "C" long _stub_fn_1495() { return 0; }
// non-virtual thunk to CharEyes::Replace(ObjRef*, Hmx::Object*)
extern "C" __attribute__((weak, used)) long _stub_fn_1496() __asm__("_ZThn8_N8CharEyes7ReplaceEP6ObjRefPN3Hmx6ObjectE");
extern "C" long _stub_fn_1496() { return 0; }
// virtual thunk to HamCamShot::Load(BinStream&)
extern "C" __attribute__((weak, used)) long _stub_fn_1497() __asm__("_ZTv0_n104_N10HamCamShot4LoadER9BinStream");
extern "C" long _stub_fn_1497() { return 0; }
// virtual thunk to RndEnviron::Load(BinStream&)
extern "C" __attribute__((weak, used)) long _stub_fn_1498() __asm__("_ZTv0_n104_N10RndEnviron4LoadER9BinStream");
extern "C" long _stub_fn_1498() { return 0; }
// virtual thunk to LightPreset::Load(BinStream&)
extern "C" __attribute__((weak, used)) long _stub_fn_1499() __asm__("_ZTv0_n104_N11LightPreset4LoadER9BinStream");
extern "C" long _stub_fn_1499() { return 0; }
// virtual thunk to DepthBuffer3D::Load(BinStream&)
extern "C" __attribute__((weak, used)) long _stub_fn_1500() __asm__("_ZTv0_n104_N13DepthBuffer3D4LoadER9BinStream");
extern "C" long _stub_fn_1500() { return 0; }
// virtual thunk to PhysicsVolume::Load(BinStream&)
extern "C" __attribute__((weak, used)) long _stub_fn_1501() __asm__("_ZTv0_n104_N13PhysicsVolume4LoadER9BinStream");
extern "C" long _stub_fn_1501() { return 0; }

// virtual thunk to WorldInstance::Load(BinStream&)
extern "C" __attribute__((weak, used)) long _stub_fn_1503() __asm__("_ZTv0_n104_N13WorldInstance4LoadER9BinStream");
extern "C" long _stub_fn_1503() { return 0; }
// virtual thunk to RndParticleSys::Load(BinStream&)
extern "C" __attribute__((weak, used)) long _stub_fn_1504() __asm__("_ZTv0_n104_N14RndParticleSys4LoadER9BinStream");
extern "C" long _stub_fn_1504() { return 0; }
// virtual thunk to HamCamTransform::Load(BinStream&)
extern "C" __attribute__((weak, used)) long _stub_fn_1505() __asm__("_ZTv0_n104_N15HamCamTransform4LoadER9BinStream");
extern "C" long _stub_fn_1505() { return 0; }
// virtual thunk to SpotlightDrawer::Load(BinStream&)
extern "C" __attribute__((weak, used)) long _stub_fn_1506() __asm__("_ZTv0_n104_N15SpotlightDrawer4LoadER9BinStream");
extern "C" long _stub_fn_1506() { return 0; }
// virtual thunk to FlowEventListener::Load(BinStream&)
extern "C" __attribute__((weak, used)) long _stub_fn_1507() __asm__("_ZTv0_n104_N17FlowEventListener4LoadER9BinStream");
extern "C" long _stub_fn_1507() { return 0; }
// virtual thunk to CharEyes::Load(BinStream&)
extern "C" __attribute__((weak, used)) long _stub_fn_1508() __asm__("_ZTv0_n104_N8CharEyes4LoadER9BinStream");
extern "C" long _stub_fn_1508() { return 0; }
// virtual thunk to WorldInstance::PreSave(BinStream&)
extern "C" __attribute__((weak, used)) long _stub_fn_1509() __asm__("_ZTv0_n112_N13WorldInstance7PreSaveER9BinStream");
extern "C" long _stub_fn_1509() { return 0; }
// virtual thunk to RndText::GetDistanceToPlane(Plane const&, Vector3&)
extern "C" __attribute__((weak, used)) long _stub_fn_1510() __asm__("_ZTv0_n120_N7RndText18GetDistanceToPlaneERK5PlaneR7Vector3");
extern "C" long _stub_fn_1510() { return 0; }
// virtual thunk to RndText::MakeWorldSphere(Sphere&, bool)
extern "C" __attribute__((weak, used)) long _stub_fn_1511() __asm__("_ZTv0_n128_N7RndText15MakeWorldSphereER6Sphereb");
extern "C" long _stub_fn_1511() { return 0; }
// virtual thunk to HamNavList::DrawShowing()
extern "C" __attribute__((weak, used)) long _stub_fn_1512() __asm__("_ZTv0_n160_N10HamNavList11DrawShowingEv");
extern "C" long _stub_fn_1512() { return 0; }
// virtual thunk to InlineHelp::DrawShowing()
extern "C" __attribute__((weak, used)) long _stub_fn_1513() __asm__("_ZTv0_n160_N10InlineHelp11DrawShowingEv");
extern "C" long _stub_fn_1513() { return 0; }
// virtual thunk to MeterDisplay::DrawShowing()
extern "C" __attribute__((weak, used)) long _stub_fn_1514() __asm__("_ZTv0_n160_N12MeterDisplay11DrawShowingEv");
extern "C" long _stub_fn_1514() { return 0; }
// virtual thunk to RndText::DrawShowing()
extern "C" __attribute__((weak, used)) long _stub_fn_1515() __asm__("_ZTv0_n160_N7RndText11DrawShowingEv");
extern "C" long _stub_fn_1515() { return 0; }
// virtual thunk to InlineHelp::PreLoad(BinStream&)
extern "C" __attribute__((weak, used)) long _stub_fn_1516() __asm__("_ZTv0_n176_N10InlineHelp7PreLoadER9BinStream");
extern "C" long _stub_fn_1516() { return 0; }
// virtual thunk to UIComponent::PostLoad(BinStream&)
extern "C" __attribute__((weak, used)) long _stub_fn_1517() __asm__("_ZTv0_n184_N11UIComponent8PostLoadER9BinStream");
extern "C" long _stub_fn_1517() { return 0; }
// virtual thunk to WorldInstance::PostLoad(BinStream&)
extern "C" __attribute__((weak, used)) long _stub_fn_1518() __asm__("_ZTv0_n184_N13WorldInstance8PostLoadER9BinStream");
extern "C" long _stub_fn_1518() { return 0; }
// virtual thunk to HamDriver::PostLoad(BinStream&)
extern "C" __attribute__((weak, used)) long _stub_fn_1519() __asm__("_ZTv0_n184_N9HamDriver8PostLoadER9BinStream");
extern "C" long _stub_fn_1519() { return 0; }
// virtual thunk to RndEnviron::Replace(ObjRef*, Hmx::Object*)
extern "C" __attribute__((weak, used)) long _stub_fn_1520() __asm__("_ZTv0_n40_N10RndEnviron7ReplaceEP6ObjRefPN3Hmx6ObjectE");
extern "C" long _stub_fn_1520() { return 0; }
// virtual thunk to FlowAnimate::Replace(ObjRef*, Hmx::Object*)
extern "C" __attribute__((weak, used)) long _stub_fn_1521() __asm__("_ZTv0_n40_N11FlowAnimate7ReplaceEP6ObjRefPN3Hmx6ObjectE");
extern "C" long _stub_fn_1521() { return 0; }
// virtual thunk to LightPreset::Replace(ObjRef*, Hmx::Object*)
extern "C" __attribute__((weak, used)) long _stub_fn_1522() __asm__("_ZTv0_n40_N11LightPreset7ReplaceEP6ObjRefPN3Hmx6ObjectE");
extern "C" long _stub_fn_1522() { return 0; }
// virtual thunk to RndParticleSys::Replace(ObjRef*, Hmx::Object*)
extern "C" __attribute__((weak, used)) long _stub_fn_1523() __asm__("_ZTv0_n40_N14RndParticleSys7ReplaceEP6ObjRefPN3Hmx6ObjectE");
extern "C" long _stub_fn_1523() { return 0; }
// virtual thunk to RndMesh::Replace(ObjRef*, Hmx::Object*)
extern "C" __attribute__((weak, used)) long _stub_fn_1524() __asm__("_ZTv0_n40_N7RndMesh7ReplaceEP6ObjRefPN3Hmx6ObjectE");
extern "C" long _stub_fn_1524() { return 0; }
// virtual thunk to CharEyes::Replace(ObjRef*, Hmx::Object*)
extern "C" __attribute__((weak, used)) long _stub_fn_1525() __asm__("_ZTv0_n40_N8CharEyes7ReplaceEP6ObjRefPN3Hmx6ObjectE");
extern "C" long _stub_fn_1525() { return 0; }
// virtual thunk to RndLight::Replace(ObjRef*, Hmx::Object*)
extern "C" __attribute__((weak, used)) long _stub_fn_1526() __asm__("_ZTv0_n40_N8RndLight7ReplaceEP6ObjRefPN3Hmx6ObjectE");
extern "C" long _stub_fn_1526() { return 0; }
// virtual thunk to CharDriver::Handle(DataArray*, bool)
extern "C" __attribute__((weak, used)) long _stub_fn_1527() __asm__("_ZTv0_n64_N10CharDriver6HandleEP9DataArrayb");
extern "C" long _stub_fn_1527() { return 0; }
// virtual thunk to CharDriver::SyncProperty(DataNode&, DataArray*, int, PropOp)
extern "C" __attribute__((weak, used)) long _stub_fn_1528() __asm__("_ZTv0_n72_N10CharDriver12SyncPropertyER8DataNodeP9DataArrayi6PropOp");
extern "C" long _stub_fn_1528() { return 0; }
// virtual thunk to WorldCrowd3DCharHandle::SyncProperty(DataNode&, DataArray*, int, PropOp)
extern "C" __attribute__((weak, used)) long _stub_fn_1529() __asm__("_ZTv0_n72_N22WorldCrowd3DCharHandle12SyncPropertyER8DataNodeP9DataArrayi6PropOp");
extern "C" long _stub_fn_1529() { return 0; }
// virtual thunk to DepthBuffer3D::Save(BinStream&)
extern "C" __attribute__((weak, used)) long _stub_fn_1530() __asm__("_ZTv0_n88_N13DepthBuffer3D4SaveER9BinStream");
extern "C" long _stub_fn_1530() { return 0; }
// virtual thunk to CharDriver::Copy(Hmx::Object const*, Hmx::Object::CopyType)
extern "C" __attribute__((weak, used)) long _stub_fn_1531() __asm__("_ZTv0_n96_N10CharDriver4CopyEPKN3Hmx6ObjectENS1_8CopyTypeE");
extern "C" long _stub_fn_1531() { return 0; }
// virtual thunk to DepthBuffer3D::Copy(Hmx::Object const*, Hmx::Object::CopyType)
extern "C" __attribute__((weak, used)) long _stub_fn_1532() __asm__("_ZTv0_n96_N13DepthBuffer3D4CopyEPKN3Hmx6ObjectENS1_8CopyTypeE");
extern "C" long _stub_fn_1532() { return 0; }
// virtual thunk to HamCamTransform::Copy(Hmx::Object const*, Hmx::Object::CopyType)
extern "C" __attribute__((weak, used)) long _stub_fn_1533() __asm__("_ZTv0_n96_N15HamCamTransform4CopyEPKN3Hmx6ObjectENS1_8CopyTypeE");
extern "C" long _stub_fn_1533() { return 0; }
// virtual thunk to Flow::Copy(Hmx::Object const*, Hmx::Object::CopyType)
extern "C" __attribute__((weak, used)) long _stub_fn_1534() __asm__("_ZTv0_n96_N4Flow4CopyEPKN3Hmx6ObjectENS1_8CopyTypeE");
extern "C" long _stub_fn_1534() { return 0; }
// virtual thunk to RndLight::Copy(Hmx::Object const*, Hmx::Object::CopyType)
extern "C" __attribute__((weak, used)) long _stub_fn_1535() __asm__("_ZTv0_n96_N8RndLight4CopyEPKN3Hmx6ObjectENS1_8CopyTypeE");
extern "C" long _stub_fn_1535() { return 0; }

// vtable and typeinfo stubs for classes without key functions.
// WARNING: These are zero-initialized. If dynamic_cast hits one of these,
// it will crash. Fix: add BEGIN_HANDLERS to the class's .cpp file so the
// compiler emits real typeinfo/vtable (which override these weak stubs).
// Classes already fixed: UIListWidget, UIListMesh, UIListSlot, UIListArrow,
// UIListLabel, UIListSubList, NavListItemSortCmp, RndFont3d.
__attribute__((weak, used)) char _stub_vt_0[1024] __asm__("_ZTI10RndMatAnim") = {};
__attribute__((weak, used)) char _stub_vt_3[1024] __asm__("_ZTI11RndMeshAnim") = {};
__attribute__((weak, used)) char _stub_vt_7[1024] __asm__("_ZTI13NetworkSocket") = {};
__attribute__((weak, used)) char _stub_vt_9[1024] __asm__("_ZTI14MQSongSortNode") = {};
__attribute__((weak, used)) char _stub_vt_10[1024] __asm__("_ZTI17CharSignalApplier") = {};
__attribute__((weak, used)) char _stub_vt_11[1024] __asm__("_ZTI17SingleItemEnumJob") = {};
__attribute__((weak, used)) char _stub_vt_12[1024] __asm__("_ZTI18AudioDuckerTrigger") = {};
__attribute__((weak, used)) char _stub_vt_14[1024] __asm__("_ZTI4ADSR") = {};
__attribute__((weak, used)) char _stub_vt_15[1024] __asm__("_ZTI5DxTex") = {};
__attribute__((weak, used)) char _stub_vt_16[1024] __asm__("_ZTI8AppLabel") = {};
__attribute__((weak, used)) char _stub_vt_18[1024] __asm__("_ZTV10RndMatAnim") = {};
__attribute__((weak, used)) char _stub_vt_21[1024] __asm__("_ZTV11LocalePanel") = {};
__attribute__((weak, used)) char _stub_vt_22[1024] __asm__("_ZTV11RndMeshAnim") = {};
__attribute__((weak, used)) char _stub_vt_25[1024] __asm__("_ZTV12PropertyTask") = {};
__attribute__((weak, used)) char _stub_vt_26[1024] __asm__("_ZTV12RndShaderFur") = {};
__attribute__((weak, used)) char _stub_vt_28[1024] __asm__("_ZTV13DifficultyCmp") = {};
__attribute__((weak, used)) char _stub_vt_29[1024] __asm__("_ZTV13NetworkSocket") = {};
__attribute__((weak, used)) char _stub_vt_30[1024] __asm__("_ZTV13OvershellSlot") = {};
__attribute__((weak, used)) char _stub_vt_32[1024] __asm__("_ZTV14MQSongSortNode") = {};
__attribute__((weak, used)) char _stub_vt_33[1024] __asm__("_ZTV14SongSortByDiff") = {};
__attribute__((weak, used)) char _stub_vt_34[1024] __asm__("_ZTV15StubCameraInput") = {};
__attribute__((weak, used)) char _stub_vt_35[1024] __asm__("_ZTV17CharSignalApplier") = {};
__attribute__((weak, used)) char _stub_vt_36[1024] __asm__("_ZTV17RndShaderDrawRect") = {};
__attribute__((weak, used)) char _stub_vt_37[1024] __asm__("_ZTV17RndShaderPostProc") = {};
__attribute__((weak, used)) char _stub_vt_38[1024] __asm__("_ZTV17RndShaderStandard") = {};
__attribute__((weak, used)) char _stub_vt_39[1024] __asm__("_ZTV17RndShaderUnwrapUV") = {};
__attribute__((weak, used)) char _stub_vt_40[1024] __asm__("_ZTV17RndShaderVelocity") = {};
__attribute__((weak, used)) char _stub_vt_41[1024] __asm__("_ZTV18AudioDuckerTrigger") = {};
__attribute__((weak, used)) char _stub_vt_43[1024] __asm__("_ZTV18RndShaderMultimesh") = {};
__attribute__((weak, used)) char _stub_vt_44[1024] __asm__("_ZTV18RndShaderParticles") = {};
__attribute__((weak, used)) char _stub_vt_45[1024] __asm__("_ZTV18RndShaderSyncTrack") = {};
__attribute__((weak, used)) char _stub_vt_46[1024] __asm__("_ZTV18StreamReceiverFile") = {};
__attribute__((weak, used)) char _stub_vt_47[1024] __asm__("_ZTV20ChallengeSortByScore") = {};
__attribute__((weak, used)) char _stub_vt_48[1024] __asm__("_ZTV20RndShaderDepthVolume") = {};
__attribute__((weak, used)) char _stub_vt_49[1024] __asm__("_ZTV21FitnessCalorieSortCmp") = {};
__attribute__((weak, used)) char _stub_vt_50[1024] __asm__("_ZTV21MQSongSortByCharacter") = {};
__attribute__((weak, used)) char _stub_vt_51[1024] __asm__("_ZTV22CamDistancePoseElement") = {};
__attribute__((weak, used)) char _stub_vt_52[1024] __asm__("_ZTV23RndShaderVelocityCamera") = {};
__attribute__((weak, used)) char _stub_vt_53[1024] __asm__("_ZTV25BoneAngleRangePoseElement") = {};
__attribute__((weak, used)) char _stub_vt_54[1024] __asm__("_ZTV26RandomIntervalGroupSeqInst") = {};
__attribute__((weak, used)) char _stub_vt_55[1024] __asm__("_ZTV27FitnessCalorieSortByCalorie") = {};
__attribute__((weak, used)) char _stub_vt_56[1024] __asm__("_ZTV4ADSR") = {};
__attribute__((weak, used)) char _stub_vt_57[1024] __asm__("_ZTV7SongCmp") = {};
__attribute__((weak, used)) char _stub_vt_58[1024] __asm__("_ZTV8AppLabel") = {};
__attribute__((weak, used)) char _stub_vt_59[1024] __asm__("_ZTV9MemStream") = {};
__attribute__((weak, used)) char _stub_vt_61[1024] __asm__("_ZTVN9HamDriver10LayerArrayE") = {};

// =============================================================================
// Asm-label stubs for remaining undefined symbols (ObjPtrVec/ObjPtrList related)
// =============================================================================

// PropSync<ObjPtrVec<T>> stubs
extern "C" __attribute__((weak, used)) long _stub_propsync_0() __asm__("_Z8PropSyncI8CharClipEbR9ObjPtrVecIT_9ObjectDirER8DataNodeP9DataArrayi6PropOp");
extern "C" long _stub_propsync_0() { return 0; }
extern "C" __attribute__((weak, used)) long _stub_propsync_1() __asm__("_Z8PropSyncI4FlowEbR9ObjPtrVecIT_9ObjectDirER8DataNodeP9DataArrayi6PropOp");
extern "C" long _stub_propsync_1() { return 0; }
extern "C" __attribute__((weak, used)) long _stub_propsync_2() __asm__("_Z8PropSyncIN3Hmx6ObjectEEbR9ObjPtrVecIT_9ObjectDirER8DataNodeP9DataArrayi6PropOp");
extern "C" long _stub_propsync_2() { return 0; }
extern "C" __attribute__((weak, used)) long _stub_propsync_3() __asm__("_Z8PropSyncI14RhythmDetectorEbR9ObjPtrVecIT_9ObjectDirER8DataNodeP9DataArrayi6PropOp");
extern "C" long _stub_propsync_3() { return 0; }
extern "C" __attribute__((weak, used)) long _stub_propsync_4() __asm__("_Z8PropSyncI11RndDrawableEbR9ObjPtrVecIT_9ObjectDirER8DataNodeP9DataArrayi6PropOp");
extern "C" long _stub_propsync_4() { return 0; }
extern "C" __attribute__((weak, used)) long _stub_propsync_5() __asm__("_Z8PropSyncI6RndMatEbR9ObjPtrVecIT_9ObjectDirER8DataNodeP9DataArrayi6PropOp");
extern "C" long _stub_propsync_5() { return 0; }
extern "C" __attribute__((weak, used)) long _stub_propsync_6() __asm__("_Z8PropSyncI16RndTransformableEbR9ObjPtrVecIT_9ObjectDirER8DataNodeP9DataArrayi6PropOp");
extern "C" long _stub_propsync_6() { return 0; }
extern "C" __attribute__((weak, used)) long _stub_propsync_7() __asm__("_Z8PropSyncI8WaypointEbR9ObjPtrVecIT_9ObjectDirER8DataNodeP9DataArrayi6PropOp");
extern "C" long _stub_propsync_7() { return 0; }

// CharHair::Hookup
extern "C" __attribute__((weak, used)) long _stub_hookup() __asm__("_ZN8CharHair6HookupER10ObjPtrListI11CharCollide9ObjectDirE");
extern "C" long _stub_hookup() { return 0; }

// merged_ObjPtrListPopBack (ICF merged stub)
extern "C" __attribute__((weak, used)) long _stub_popback() __asm__("_Z24merged_ObjPtrListPopBackPv");
extern "C" long _stub_popback() { return 0; }

// RndVelocityBuffer::Draw
extern "C" __attribute__((weak, used)) long _stub_velbuf_draw() __asm__("_ZN17RndVelocityBuffer4DrawEP6RndCamR10ObjPtrListI11RndDrawable9ObjectDirE");
extern "C" long _stub_velbuf_draw() { return 0; }

// EventTask::EventTask (C1 and C2 constructors)
extern "C" __attribute__((weak, used)) long _stub_eventtask_c1() __asm__("_ZN9EventTaskC1EP9FlowTimerP9ObjPtrVecI8FlowNode9ObjectDirE9TaskUnitsf");
extern "C" long _stub_eventtask_c1() { return 0; }
extern "C" __attribute__((weak, used)) long _stub_eventtask_c2() __asm__("_ZN9EventTaskC2EP9FlowTimerP9ObjPtrVecI8FlowNode9ObjectDirE9TaskUnitsf");
extern "C" long _stub_eventtask_c2() { return 0; }

// ScanForOutPorts
extern "C" __attribute__((weak, used)) long _stub_scanoutports() __asm__("_Z15ScanForOutPortsR9ObjPtrVecI11FlowOutPort9ObjectDirEP8FlowNodeP4Flow");
extern "C" long _stub_scanoutports() { return 0; }

#endif // !__EMSCRIPTEN__
