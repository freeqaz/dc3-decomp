// Auto-generated stubs for DC3 Native Port
// Provides stub implementations for all undefined symbols
// This allows linking - stubs return 0/nullptr at runtime

#include <cstdio>
#include <cstdlib>
#include <cstring>

// macOS Mach-O prefixes all symbols with '_'. The __asm__ names in this file
// use Itanium mangling (e.g. "_Z5FooPv") which needs a leading underscore on
// macOS to match references from C++ TUs (which produce "__Z5FooPv").
#ifdef __APPLE__
#define ASM_SYM(name) "_" name
#else
#define ASM_SYM(name) name
#endif

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
// ctr_reinit and ctr_start now provided by tomcrypt/ctr.c
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
#ifdef __APPLE__
__attribute__((weak))
#endif
int register_cipher() { return 0; }
#endif
// rijndael_desc, rijndael_setup, rijndael_ecb_decrypt now provided by tomcrypt/aes.c
#ifndef __EMSCRIPTEN__
int SetUnhandledExceptionFilter() { return 0; }
#endif
// The* global pointer stubs - must be void* (not functions!) since C++ code
// declares them as extern ClassName* and dereferences them as pointers.
// Function stubs at these symbols would be read as non-null garbage pointers.
void* TheChallengeSortMgr = 0;
// TheContentMgr: provided by ContentMgr_Stub.cpp
void* TheDebugNotifyOncePrinter = 0;
// TheDxRnd: DxRnd is Xbox-only (D3D9 renderer), provide zero storage for native
__attribute__((weak, used)) char TheDxRnd[8192] = {};
// D3DDevice_SetDepthStencilSurface: Xbox D3D9 API stub
extern "C" __attribute__((weak)) void D3DDevice_SetDepthStencilSurface(void*, int) {}
// MemHeapStack::sDefaultHeap: static member definition
#include "utl/MemHeap.h"
int MemHeapStack::sDefaultHeap = 0;
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

// vorbis_synthesis_poll was Harmonix's incremental decoder for Xbox.
// On native/web, delegate to standard vorbis_synthesis which does full decode at once.
struct vorbis_block;
struct ogg_packet;
extern "C" int vorbis_synthesis(vorbis_block *vb, ogg_packet *op);
int vorbis_synthesis_poll(vorbis_block *vb, ogg_packet *op) { return vorbis_synthesis(vb, op); }

#ifndef __EMSCRIPTEN__
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
__attribute__((weak, used)) char _stub_var_40[256] __asm__(ASM_SYM("_ZN14StandardStream12kStreamEndMsE")) = {};
__attribute__((weak, used)) char _stub_var_112[256] __asm__(ASM_SYM("_ZTT17CharSignalApplier")) = {};

// C++ function stubs — skip on Emscripten (asm-label functions cause
// signature mismatches that insert 'unreachable' traps in wasm-ld)
#ifndef __EMSCRIPTEN__
// CloseHandle(int)
extern "C" __attribute__((weak, used)) long _stub_fn_7() __asm__(ASM_SYM("_Z11CloseHandlei"));
extern "C" long _stub_fn_7() { return 0; }
// DspAllocate(float*&, int, IXAudioBatchAllocator*)
extern "C" __attribute__((weak, used)) long _stub_fn_8() __asm__(ASM_SYM("_Z11DspAllocateRPfiP21IXAudioBatchAllocator"));
extern "C" long _stub_fn_8() { return 0; }
// SetupHXDrums(int, _XINPUT_CAPABILITIES const&)
extern "C" __attribute__((weak, used)) long _stub_fn_20() __asm__(ASM_SYM("_Z12SetupHXDrumsiRK20_XINPUT_CAPABILITIES"));
extern "C" long _stub_fn_20() { return 0; }
// BinkNextFrame(BINK*)
extern "C" __attribute__((weak, used)) long _stub_fn_21() __asm__(ASM_SYM("_Z13BinkNextFrameP4BINK"));
extern "C" long _stub_fn_21() { return 0; }
// BinkOpenTrack(BINK*, unsigned char)
extern "C" __attribute__((weak, used)) long _stub_fn_22() __asm__(ASM_SYM("_Z13BinkOpenTrackP4BINKh"));
extern "C" long _stub_fn_22() { return 0; }
// BinkSetMemory(void* (*)(int), void (*)(void*))
extern "C" __attribute__((weak, used)) long _stub_fn_23() __asm__(ASM_SYM("_Z13BinkSetMemoryPFPviEPFvS_E"));
extern "C" long _stub_fn_23() { return 0; }
// DiffTblReport(char const*, BlockStatTable&, BlockStatTable&, TextStream&)
extern "C" __attribute__((weak, used)) long _stub_fn_25() __asm__(ASM_SYM("_Z13DiffTblReportPKcR14BlockStatTableS2_R10TextStream"));
extern "C" long _stub_fn_25() { return 0; }
// DrawBufferMat(RndMat*, Hmx::Rect&)
extern "C" __attribute__((weak, used)) long _stub_fn_26() __asm__(ASM_SYM("_Z13DrawBufferMatP6RndMatRN3Hmx4RectE"));
extern "C" long _stub_fn_26() { return 0; }
// SetupHXGuitar(int, _XINPUT_CAPABILITIES const&)
extern "C" __attribute__((weak, used)) long _stub_fn_29() __asm__(ASM_SYM("_Z13SetupHXGuitariRK20_XINPUT_CAPABILITIES"));
extern "C" long _stub_fn_29() { return 0; }
// SetupHXKeytar(int, _XINPUT_CAPABILITIES const&)
extern "C" __attribute__((weak, used)) long _stub_fn_30() __asm__(ASM_SYM("_Z13SetupHXKeytariRK20_XINPUT_CAPABILITIES"));
extern "C" long _stub_fn_30() { return 0; }
// SpewTerminate()
extern "C" __attribute__((weak, used)) long _stub_fn_31() __asm__(ASM_SYM("_Z13SpewTerminatev"));
extern "C" long _stub_fn_31() { return 0; }
// SystemPreInit(int, char**, char const*) -- provided by System_Native.cpp
// XNetDnsLookup(int, int, void*)
extern "C" __attribute__((weak, used)) long _stub_fn_33() __asm__(ASM_SYM("_Z13XNetDnsLookupiiPv"));
extern "C" long _stub_fn_33() { return 0; }
// BinkCloseTrack(BINKTRACK*)
extern "C" __attribute__((weak, used)) long _stub_fn_35() __asm__(ASM_SYM("_Z14BinkCloseTrackP9BINKTRACK"));
extern "C" long _stub_fn_35() { return 0; }
// CompressThread(void*)
extern "C" __attribute__((weak, used)) long _stub_fn_36() __asm__(ASM_SYM("_Z14CompressThreadPv"));
extern "C" long _stub_fn_36() { return 0; }
// jpeg_std_error(jpeg_error_mgr*)
extern "C" __attribute__((weak, used)) long _stub_fn_39() __asm__(ASM_SYM("_Z14jpeg_std_errorP14jpeg_error_mgr"));
extern "C" long _stub_fn_39() { return 0; }
// WSACreateEvent()
extern "C" __attribute__((weak, used)) long _stub_fn_45() __asm__(ASM_SYM("_Z14WSACreateEventv"));
extern "C" long _stub_fn_45() { return 0; }
// XNetDnsRelease(void*)
extern "C" __attribute__((weak, used)) long _stub_fn_46() __asm__(ASM_SYM("_Z14XNetDnsReleasePv"));
extern "C" long _stub_fn_46() { return 0; }
// merged_82610090(char const*, int volatile*)
extern "C" __attribute__((weak, used)) long _stub_fn_47() __asm__(ASM_SYM("_Z15merged_82610090PKcPVi"));
extern "C" long _stub_fn_47() { return 0; }
// BinkGetTrackData(BINKTRACK*, void*)
extern "C" __attribute__((weak, used)) long _stub_fn_52() __asm__(ASM_SYM("_Z16BinkGetTrackDataP9BINKTRACKPv"));
extern "C" long _stub_fn_52() { return 0; }
// ValidateThreadId(unsigned long)
extern "C" __attribute__((weak, used)) long _stub_fn_61() __asm__(ASM_SYM("_Z16ValidateThreadIdm"));
extern "C" long _stub_fn_61() { return 0; }
// HolmesClientPrint(char const*)
extern "C" __attribute__((weak, used)) long _stub_fn_64() __asm__(ASM_SYM("_Z17HolmesClientPrintPKc"));
extern "C" long _stub_fn_64() { return 0; }
// jpeg_set_defaults(jpeg_compress_struct*)
extern "C" __attribute__((weak, used)) long _stub_fn_65() __asm__(ASM_SYM("_Z17jpeg_set_defaultsP20jpeg_compress_struct"));
extern "C" long _stub_fn_65() { return 0; }
// SetupHXRealGuitar(int, _XINPUT_CAPABILITIES const&)
extern "C" __attribute__((weak, used)) long _stub_fn_68() __asm__(ASM_SYM("_Z17SetupHXRealGuitariRK20_XINPUT_CAPABILITIES"));
extern "C" long _stub_fn_68() { return 0; }
// HolmesSetFileShare(char const*, char const*)
extern "C" __attribute__((weak, used)) long _stub_fn_71() __asm__(ASM_SYM("_Z18HolmesSetFileSharePKcS0_"));
extern "C" long _stub_fn_71() { return 0; }
// OnCameraDebugDepth(DataArray*)
extern "C" __attribute__((weak, used)) long _stub_fn_73() __asm__(ASM_SYM("_Z18OnCameraDebugDepthP9DataArray"));
extern "C" long _stub_fn_73() { return 0; }
// OnCameraDumpUnique(DataArray*)
extern "C" __attribute__((weak, used)) long _stub_fn_74() __asm__(ASM_SYM("_Z18OnCameraDumpUniqueP9DataArray"));
extern "C" long _stub_fn_74() { return 0; }
// jpeg_CreateCompress(jpeg_compress_struct*, int, unsigned long)
extern "C" __attribute__((weak, used)) long _stub_fn_78() __asm__(ASM_SYM("_Z19jpeg_CreateCompressP20jpeg_compress_structim"));
extern "C" long _stub_fn_78() { return 0; }
// jpeg_start_compress(jpeg_compress_struct*, unsigned char)
extern "C" __attribute__((weak, used)) long _stub_fn_79() __asm__(ASM_SYM("_Z19jpeg_start_compressP20jpeg_compress_structh"));
extern "C" long _stub_fn_79() { return 0; }
// TerminateMakeString()
extern "C" __attribute__((weak, used)) long _stub_fn_82() __asm__(ASM_SYM("_Z19TerminateMakeStringv"));
extern "C" long _stub_fn_82() { return 0; }
// WaitForSingleObject(int, int)
extern "C" __attribute__((weak, used)) long _stub_fn_83() __asm__(ASM_SYM("_Z19WaitForSingleObjectii"));
extern "C" long _stub_fn_83() { return 0; }
// BinkStartAsyncThread(int, int)
extern "C" __attribute__((weak, used)) long _stub_fn_84() __asm__(ASM_SYM("_Z20BinkStartAsyncThreadii"));
extern "C" long _stub_fn_84() { return 0; }
// HongKongExceptionMet()
extern "C" __attribute__((weak, used)) long _stub_fn_88() __asm__(ASM_SYM("_Z20HongKongExceptionMetv"));
extern "C" long _stub_fn_88() { return 0; }
// jpeg_finish_compress(jpeg_compress_struct*)
extern "C" __attribute__((weak, used)) long _stub_fn_89() __asm__(ASM_SYM("_Z20jpeg_finish_compressP20jpeg_compress_struct"));
extern "C" long _stub_fn_89() { return 0; }
// jpeg_write_scanlines(jpeg_compress_struct*, unsigned char**, unsigned int)
extern "C" __attribute__((weak, used)) long _stub_fn_90() __asm__(ASM_SYM("_Z20jpeg_write_scanlinesP20jpeg_compress_structPPhj"));
extern "C" long _stub_fn_90() { return 0; }
// HolmesClientTerminate()
extern "C" __attribute__((weak, used)) long _stub_fn_97() __asm__(ASM_SYM("_Z21HolmesClientTerminatev"));
extern "C" long _stub_fn_97() { return 0; }
// GetXinputSinceLastFrame(int, _XINPUT_STATE*, unsigned int*)
extern "C" __attribute__((weak, used)) long _stub_fn_102() __asm__(ASM_SYM("_Z23GetXinputSinceLastFrameiP13_XINPUT_STATEPj"));
extern "C" long _stub_fn_102() { return 0; }
// NuiTransformSkeletonToDepthImage(__vector4, float*, float*)
extern "C" __attribute__((weak, used)) long _stub_fn_111() __asm__(ASM_SYM("_Z32NuiTransformSkeletonToDepthImage9__vector4PfS0_"));
extern "C" long _stub_fn_111() { return 0; }
// NuiTransformSkeletonToDepthImage(__vector4, long*, long*, unsigned short*)
extern "C" __attribute__((weak, used)) long _stub_fn_112() __asm__(ASM_SYM("_Z32NuiTransformSkeletonToDepthImage9__vector4PlS0_Pt"));
extern "C" long _stub_fn_112() { return 0; }
// altCfg(DataNode, DataNode)
extern "C" __attribute__((weak, used)) long _stub_fn_115() __asm__(ASM_SYM("_Z6altCfg8DataNodeS_"));
extern "C" long _stub_fn_115() { return 0; }
// CacheWav(char const*, CacheResourceResult&)
extern "C" __attribute__((weak, used)) long _stub_fn_118() __asm__(ASM_SYM("_Z8CacheWavPKcR19CacheResourceResult"));
extern "C" long _stub_fn_118() { return 0; }
// RadAlloc(int)
extern "C" __attribute__((weak, used)) long _stub_fn_138() __asm__(ASM_SYM("_Z8RadAlloci"));
extern "C" long _stub_fn_138() { return 0; }
// SpewInit()
extern "C" __attribute__((weak, used)) long _stub_fn_139() __asm__(ASM_SYM("_Z8SpewInitv"));
extern "C" long _stub_fn_139() { return 0; }
// BinkClose(BINK*)
extern "C" __attribute__((weak, used)) long _stub_fn_140() __asm__(ASM_SYM("_Z9BinkCloseP4BINK"));
extern "C" long _stub_fn_140() { return 0; }
// Intersect(Segment const&, Triangle const&, int, float&)
extern "C" __attribute__((weak, used)) long _stub_fn_141() __asm__(ASM_SYM("_Z9IntersectRK7SegmentRK8TriangleiRf"));
extern "C" long _stub_fn_141() { return 0; }
// CamTexClip::StoreTextureClip(RndTex*, float, float, float, float)
extern "C" __attribute__((weak, used)) long _stub_fn_208() __asm__(ASM_SYM("_ZN10CamTexClip16StoreTextureClipEP6RndTexffff"));
extern "C" long _stub_fn_208() { return 0; }
// MemTracker::ReportMemoryAlloc(char const*)
extern "C" __attribute__((weak, used)) long _stub_fn_273() __asm__(ASM_SYM("_ZN10MemTracker17ReportMemoryAllocEPKc"));
extern "C" long _stub_fn_273() { return 0; }
// MemTracker::ReportMemoryUsage(char const*)
extern "C" __attribute__((weak, used)) long _stub_fn_274() __asm__(ASM_SYM("_ZN10MemTracker17ReportMemoryUsageEPKc"));
extern "C" long _stub_fn_274() { return 0; }
// MemTracker::ReportMemoryUsageOverview(char const*)
extern "C" __attribute__((weak, used)) long _stub_fn_275() __asm__(ASM_SYM("_ZN10MemTracker25ReportMemoryUsageOverviewEPKc"));
extern "C" long _stub_fn_275() { return 0; }
// MQSongSort::BuildTree()
extern "C" __attribute__((weak, used)) long _stub_fn_276() __asm__(ASM_SYM("_ZN10MQSongSort9BuildTreeEv"));
extern "C" long _stub_fn_276() { return 0; }
// TextStream::operator<<(long)
extern "C" __attribute__((weak, used)) long _stub_fn_402() __asm__(ASM_SYM("_ZN10TextStreamlsEl"));
extern "C" long _stub_fn_402() { return 0; }
// ArcDetector::UpdateOverlay(RndOverlay*, float)
extern "C" __attribute__((weak, used)) long _stub_fn_415() __asm__(ASM_SYM("_ZN11ArcDetector13UpdateOverlayEP10RndOverlayf"));
extern "C" long _stub_fn_415() { return 0; }
// ArcDetector::TryToStartSwipe(Vector3 const&, Skeleton const&)
extern "C" __attribute__((weak, used)) long _stub_fn_416() __asm__(ASM_SYM("_ZN11ArcDetector15TryToStartSwipeERK7Vector3RK8Skeleton"));
extern "C" long _stub_fn_416() { return 0; }
// ArcDetector::Update(Skeleton const&, int)
extern "C" __attribute__((weak, used)) long _stub_fn_417() __asm__(ASM_SYM("_ZN11ArcDetector6UpdateERK8Skeletoni"));
extern "C" long _stub_fn_417() { return 0; }
// LightPreset stubs removed — real impl in LightPreset.cpp
// LocationCmp::LocationCmp()
extern "C" __attribute__((weak, used)) long _stub_fn_461() __asm__(ASM_SYM("_ZN11LocationCmpC1Ev"));
extern "C" long _stub_fn_461() { return 0; }
// MemcardXbox::Poll()
extern "C" __attribute__((weak, used)) long _stub_fn_462() __asm__(ASM_SYM("_ZN11MemcardXbox4PollEv"));
extern "C" long _stub_fn_462() { return 0; }
// PlatformMgr::OnSignInUsers(DataArray*)
extern "C" __attribute__((weak, used)) long _stub_fn_474() __asm__(ASM_SYM("_ZN11PlatformMgr13OnSignInUsersEP9DataArray"));
extern "C" long _stub_fn_474() { return 0; }
// RndFontBase::Load(BinStream&) — now implemented in FontBase.cpp
// SongSortMgr::SetupQuasiRandomSongs()
extern "C" __attribute__((weak, used)) long _stub_fn_511() __asm__(ASM_SYM("_ZN11SongSortMgr21SetupQuasiRandomSongsEv"));
extern "C" long _stub_fn_511() { return 0; }
// XboxMapFile::ParseStack(char const*, StackData*, int, FixedString&)
extern "C" __attribute__((weak, used)) long _stub_fn_519() __asm__(ASM_SYM("_ZN11XboxMapFile10ParseStackEPKcP9StackDataiR11FixedString"));
extern "C" long _stub_fn_519() { return 0; }
// Achievements::PlatformInit()
extern "C" __attribute__((weak, used)) long _stub_fn_519b() __asm__(ASM_SYM("_ZN12Achievements12PlatformInitEv"));
extern "C" long _stub_fn_519b() { return 0; }
// Achievements::GetAchievementData(int, int)
extern "C" __attribute__((weak, used)) long _stub_fn_520() __asm__(ASM_SYM("_ZN12Achievements18GetAchievementDataEii"));
extern "C" long _stub_fn_520() { return 0; }
// Achievements::SubmitAchievementsFunc()
extern "C" __attribute__((weak, used)) long _stub_fn_521() __asm__(ASM_SYM("_ZN12Achievements22SubmitAchievementsFuncEv"));
extern "C" long _stub_fn_521() { return 0; }
// AsyncFileWin::AsyncFileWin(char const*, int)
extern "C" __attribute__((weak, used)) long _stub_fn_522() __asm__(ASM_SYM("_ZN12AsyncFileWinC1EPKci"));
extern "C" long _stub_fn_522() { return 0; }
// BaseSkeleton::MakeCameraToPlayerXfm(SkeletonCoordSys, Transform&, Vector3 const*, Vector3 const&)
extern "C" __attribute__((weak, used)) long _stub_fn_523() __asm__(ASM_SYM("_ZN12BaseSkeleton21MakeCameraToPlayerXfmE16SkeletonCoordSysR9TransformPK7Vector3RS4_"));
extern "C" long _stub_fn_523() { return 0; }
// BinkMovieSys::PlatformInit()
extern "C" __attribute__((weak, used)) long _stub_fn_524() __asm__(ASM_SYM("_ZN12BinkMovieSys12PlatformInitEv"));
extern "C" long _stub_fn_524() { return 0; }
// CacheMgrXbox::CacheMgrXbox()
extern "C" __attribute__((weak, used)) long _stub_fn_525() __asm__(ASM_SYM("_ZN12CacheMgrXboxC1Ev"));
extern "C" long _stub_fn_525() { return 0; }
// DrawString3D::DrawFixedZ(float)
extern "C" __attribute__((weak, used)) long _stub_fn_531() __asm__(ASM_SYM("_ZN12DrawString3D10DrawFixedZEf"));
extern "C" long _stub_fn_531() { return 0; }
// (anonymous namespace)::CheckReads(bool)
extern "C" __attribute__((weak, used)) long _stub_fn_537() __asm__(ASM_SYM("_ZN12_GLOBAL__N_110CheckReadsEb"));
extern "C" long _stub_fn_537() { return 0; }
// (anonymous namespace)::WaitForReads()
extern "C" __attribute__((weak, used)) long _stub_fn_538() __asm__(ASM_SYM("_ZN12_GLOBAL__N_112WaitForReadsEv"));
extern "C" long _stub_fn_538() { return 0; }
// (anonymous namespace)::VertexToWorld(Vector3&, Transform const&, float, Vector4 const&)
extern "C" __attribute__((weak, used)) long _stub_fn_539() __asm__(ASM_SYM("_ZN12_GLOBAL__N_113VertexToWorldER7Vector3RK9TransformfRK7Vector4"));
extern "C" long _stub_fn_539() { return 0; }
// (anonymous namespace)::WaitForResponse(Holmes::Protocol)
extern "C" __attribute__((weak, used)) long _stub_fn_540() __asm__(ASM_SYM("_ZN12_GLOBAL__N_115WaitForResponseEN6Holmes8ProtocolE"));
extern "C" long _stub_fn_540() { return 0; }
// (anonymous namespace)::CheckForResponse(Holmes::Protocol, bool)
extern "C" __attribute__((weak, used)) long _stub_fn_541() __asm__(ASM_SYM("_ZN12_GLOBAL__N_116CheckForResponseEN6Holmes8ProtocolEb"));
extern "C" long _stub_fn_541() { return 0; }
// (anonymous namespace)::DecodeThreadEntry(void*)
extern "C" __attribute__((weak, used)) long _stub_fn_542() __asm__(ASM_SYM("_ZN12_GLOBAL__N_117DecodeThreadEntryEPv"));
extern "C" long _stub_fn_542() { return 0; }
// (anonymous namespace)::JointToVertexData(Vector3&, Skeleton const&, SkeletonJoint, Vector4 const&)
extern "C" __attribute__((weak, used)) long _stub_fn_543() __asm__(ASM_SYM("_ZN12_GLOBAL__N_117JointToVertexDataER7Vector3RK8Skeleton13SkeletonJointRK7Vector4"));
extern "C" long _stub_fn_543() { return 0; }
// (anonymous namespace)::WaitForAnyResponse(Holmes::Protocol)
extern "C" __attribute__((weak, used)) long _stub_fn_544() __asm__(ASM_SYM("_ZN12_GLOBAL__N_118WaitForAnyResponseEN6Holmes8ProtocolE"));
extern "C" long _stub_fn_544() { return 0; }
// (anonymous namespace)::WriteMemoryCallback(void*, unsigned int, unsigned int, void*)
extern "C" __attribute__((weak, used)) long _stub_fn_545() __asm__(ASM_SYM("_ZN12_GLOBAL__N_119WriteMemoryCallbackEPvjjS0_"));
extern "C" long _stub_fn_545() { return 0; }
// (anonymous namespace)::LoadDebugDepthBuffer(RndTex*&)
extern "C" __attribute__((weak, used)) long _stub_fn_546() __asm__(ASM_SYM("_ZN12_GLOBAL__N_120LoadDebugDepthBufferERP6RndTex"));
extern "C" long _stub_fn_546() { return 0; }
// (anonymous namespace)::SetColorCameraProperty(_NUI_CAMERA_PROPERTY, long)
extern "C" __attribute__((weak, used)) long _stub_fn_547() __asm__(ASM_SYM("_ZN12_GLOBAL__N_122SetColorCameraPropertyE20_NUI_CAMERA_PROPERTYl"));
extern "C" long _stub_fn_547() { return 0; }
// (anonymous namespace)::HolmesFlushStreamBuffer()
extern "C" __attribute__((weak, used)) long _stub_fn_548() __asm__(ASM_SYM("_ZN12_GLOBAL__N_123HolmesFlushStreamBufferEv"));
extern "C" long _stub_fn_548() { return 0; }
// (anonymous namespace)::ClipStart(CharClip*, float, float&, float&)
extern "C" __attribute__((weak, used)) long _stub_fn_549() __asm__(ASM_SYM("_ZN12_GLOBAL__N_19ClipStartEP8CharClipfRfS2_"));
extern "C" long _stub_fn_549() { return 0; }
// PartyModeMgr::DetermineSubModePlayers(Symbol, int*, int*, std::vector<int, std::allocator<int> >*)
extern "C" __attribute__((weak, used)) long _stub_fn_574() __asm__(ASM_SYM("_ZN12PartyModeMgr23DetermineSubModePlayersE6SymbolPiS1_PSt6vectorIiSaIiEE"));
extern "C" long _stub_fn_574() { return 0; }
// AutoSlowFrame::AutoSlowFrame(char const*, float)
extern "C" __attribute__((weak, used)) long _stub_fn_589() __asm__(ASM_SYM("_ZN13AutoSlowFrameC1EPKcf"));
extern "C" long _stub_fn_589() { return 0; }
// AutoSlowFrame::~AutoSlowFrame()
extern "C" __attribute__((weak, used)) long _stub_fn_590() __asm__(ASM_SYM("_ZN13AutoSlowFrameD1Ev"));
extern "C" long _stub_fn_590() { return 0; }
// BinkMovieImpl::LockThread()
extern "C" __attribute__((weak, used)) long _stub_fn_591() __asm__(ASM_SYM("_ZN13BinkMovieImpl10LockThreadEv"));
extern "C" long _stub_fn_591() { return 0; }
// BinkMovieImpl::UnlockThread()
extern "C" __attribute__((weak, used)) long _stub_fn_592() __asm__(ASM_SYM("_ZN13BinkMovieImpl12UnlockThreadEv"));
extern "C" long _stub_fn_592() { return 0; }
// BinkMovieImpl::BeginFromFile(char const*, float, bool, bool, bool, bool, int, BinStream*, LoaderPos)
extern "C" __attribute__((weak, used)) long _stub_fn_593() __asm__(ASM_SYM("_ZN13BinkMovieImpl13BeginFromFileEPKcfbbbbiP9BinStream9LoaderPos"));
extern "C" long _stub_fn_593() { return 0; }
// BinkMovieImpl::SetWidthHeight(int, int)
extern "C" __attribute__((weak, used)) long _stub_fn_594() __asm__(ASM_SYM("_ZN13BinkMovieImpl14SetWidthHeightEii"));
extern "C" long _stub_fn_594() { return 0; }
// BinkMovieImpl::End()
extern "C" __attribute__((weak, used)) long _stub_fn_595() __asm__(ASM_SYM("_ZN13BinkMovieImpl3EndEv"));
extern "C" long _stub_fn_595() { return 0; }
// BinkMovieImpl::Draw()
extern "C" __attribute__((weak, used)) long _stub_fn_596() __asm__(ASM_SYM("_ZN13BinkMovieImpl4DrawEv"));
extern "C" long _stub_fn_596() { return 0; }
// BinkMovieImpl::Poll()
extern "C" __attribute__((weak, used)) long _stub_fn_597() __asm__(ASM_SYM("_ZN13BinkMovieImpl4PollEv"));
extern "C" long _stub_fn_597() { return 0; }
// BinkMovieImpl::Save(BinStream*)
extern "C" __attribute__((weak, used)) long _stub_fn_598() __asm__(ASM_SYM("_ZN13BinkMovieImpl4SaveEP9BinStream"));
extern "C" long _stub_fn_598() { return 0; }
// BinkMovieImpl::CheckOpen(bool)
extern "C" __attribute__((weak, used)) long _stub_fn_599() __asm__(ASM_SYM("_ZN13BinkMovieImpl9CheckOpenEb"));
extern "C" long _stub_fn_599() { return 0; }
// BinkMovieImpl::SetPaused(bool)
extern "C" __attribute__((weak, used)) long _stub_fn_600() __asm__(ASM_SYM("_ZN13BinkMovieImpl9SetPausedEb"));
extern "C" long _stub_fn_600() { return 0; }
// BinkMovieImpl::SetVolume(float)
extern "C" __attribute__((weak, used)) long _stub_fn_601() __asm__(ASM_SYM("_ZN13BinkMovieImpl9SetVolumeEf"));
extern "C" long _stub_fn_601() { return 0; }
// BinkMovieImpl::Terminate()
extern "C" __attribute__((weak, used)) long _stub_fn_602() __asm__(ASM_SYM("_ZN13BinkMovieImpl9TerminateEv"));
extern "C" long _stub_fn_602() { return 0; }
// ChallengeSort::BuildTree()
extern "C" __attribute__((weak, used)) long _stub_fn_605() __asm__(ASM_SYM("_ZN13ChallengeSort9BuildTreeEv"));
extern "C" long _stub_fn_605() { return 0; }
// DepthBuffer3D::DrawShowing()
extern "C" __attribute__((weak, used)) long _stub_fn_618() __asm__(ASM_SYM("_ZN13DepthBuffer3D11DrawShowingEv"));
extern "C" long _stub_fn_618() { return 0; }
// NetLoaderXbox::NetLoaderXbox(String const&)
extern "C" __attribute__((weak, used)) long _stub_fn_640() __asm__(ASM_SYM("_ZN13NetLoaderXboxC1ERK6String"));
extern "C" long _stub_fn_640() { return 0; }
// NetworkSocket::IPIntToString(unsigned int)
extern "C" __attribute__((weak, used)) long _stub_fn_641() __asm__(ASM_SYM("_ZN13NetworkSocket13IPIntToStringEj"));
extern "C" long _stub_fn_641() { return 0; }
// NetworkSocket::IPStringToInt(String const&)
extern "C" __attribute__((weak, used)) long _stub_fn_642() __asm__(ASM_SYM("_ZN13NetworkSocket13IPStringToIntERK6String"));
extern "C" long _stub_fn_642() { return 0; }

// KinectShareJob::KinectShareJob(Hmx::Object*)
extern "C" __attribute__((weak, used)) long _stub_fn_669() __asm__(ASM_SYM("_ZN14KinectShareJobC1EPN3Hmx6ObjectE"));
extern "C" long _stub_fn_669() { return 0; }
// ObjRefConcrete<LightPreset, ObjectDir>::CopyRef stub removed — real impl in LightPreset.cpp
// RndRenderState::SetBlendOp(RndRenderState::BlendOp)
extern "C" __attribute__((weak, used)) long _stub_fn_758() __asm__(ASM_SYM("_ZN14RndRenderState10SetBlendOpENS_7BlendOpE"));
extern "C" long _stub_fn_758() { return 0; }
// RndRenderState::SetCullMode(RndRenderState::CullMode)
extern "C" __attribute__((weak, used)) long _stub_fn_759() __asm__(ASM_SYM("_ZN14RndRenderState11SetCullModeENS_8CullModeE"));
extern "C" long _stub_fn_759() { return 0; }
// RndRenderState::SetFillMode(RndRenderState::FillMode)
extern "C" __attribute__((weak, used)) long _stub_fn_760() __asm__(ASM_SYM("_ZN14RndRenderState11SetFillModeENS_8FillModeE"));
extern "C" long _stub_fn_760() { return 0; }
// RndRenderState::SetAlphaFunc(RndRenderState::TestFunc, unsigned int)
extern "C" __attribute__((weak, used)) long _stub_fn_761() __asm__(ASM_SYM("_ZN14RndRenderState12SetAlphaFuncENS_8TestFuncEj"));
extern "C" long _stub_fn_761() { return 0; }
// RndRenderState::SetDepthFunc(RndRenderState::TestFunc)
extern "C" __attribute__((weak, used)) long _stub_fn_762() __asm__(ASM_SYM("_ZN14RndRenderState12SetDepthFuncENS_8TestFuncE"));
extern "C" long _stub_fn_762() { return 0; }
// RndRenderState::SetStencilOp(RndRenderState::StencilOp, RndRenderState::StencilOp, RndRenderState::StencilOp)
extern "C" __attribute__((weak, used)) long _stub_fn_763() __asm__(ASM_SYM("_ZN14RndRenderState12SetStencilOpENS_9StencilOpES0_S0_"));
extern "C" long _stub_fn_763() { return 0; }
// RndRenderState::SetBlendEnable(bool)
extern "C" __attribute__((weak, used)) long _stub_fn_764() __asm__(ASM_SYM("_ZN14RndRenderState14SetBlendEnableEb"));
extern "C" long _stub_fn_764() { return 0; }
// RndRenderState::SetBorderColor(unsigned int, bool)
extern "C" __attribute__((weak, used)) long _stub_fn_765() __asm__(ASM_SYM("_ZN14RndRenderState14SetBorderColorEjb"));
extern "C" long _stub_fn_765() { return 0; }
// RndRenderState::SetStencilFunc(RndRenderState::TestFunc, unsigned char)
extern "C" __attribute__((weak, used)) long _stub_fn_766() __asm__(ASM_SYM("_ZN14RndRenderState14SetStencilFuncENS_8TestFuncEh"));
extern "C" long _stub_fn_766() { return 0; }
// RndRenderState::SetTextureClamp(unsigned int, RndRenderState::ClampMode)
extern "C" __attribute__((weak, used)) long _stub_fn_767() __asm__(ASM_SYM("_ZN14RndRenderState15SetTextureClampEjNS_9ClampModeE"));
extern "C" long _stub_fn_767() { return 0; }
// RndRenderState::SetTextureFilter(unsigned int, RndRenderState::FilterMode, bool)
extern "C" __attribute__((weak, used)) long _stub_fn_768() __asm__(ASM_SYM("_ZN14RndRenderState16SetTextureFilterEjNS_10FilterModeEb"));
extern "C" long _stub_fn_768() { return 0; }
// RndRenderState::SetAlphaTestEnable(bool)
extern "C" __attribute__((weak, used)) long _stub_fn_769() __asm__(ASM_SYM("_ZN14RndRenderState18SetAlphaTestEnableEb"));
extern "C" long _stub_fn_769() { return 0; }
// RndRenderState::SetDepthTestEnable(bool)
extern "C" __attribute__((weak, used)) long _stub_fn_770() __asm__(ASM_SYM("_ZN14RndRenderState18SetDepthTestEnableEb"));
extern "C" long _stub_fn_770() { return 0; }
// RndRenderState::SetDepthWriteEnable(bool)
extern "C" __attribute__((weak, used)) long _stub_fn_771() __asm__(ASM_SYM("_ZN14RndRenderState19SetDepthWriteEnableEb"));
extern "C" long _stub_fn_771() { return 0; }
// RndRenderState::SetStencilTestEnable(bool)
extern "C" __attribute__((weak, used)) long _stub_fn_772() __asm__(ASM_SYM("_ZN14RndRenderState20SetStencilTestEnableEb"));
extern "C" long _stub_fn_772() { return 0; }
// RndRenderState::SetBlend(RndRenderState::Blend, RndRenderState::Blend, RndRenderState::Blend, RndRenderState::Blend)
extern "C" __attribute__((weak, used)) long _stub_fn_773() __asm__(ASM_SYM("_ZN14RndRenderState8SetBlendENS_5BlendES0_S0_S0_"));
extern "C" long _stub_fn_773() { return 0; }
// StandardStream::GetJumpBackTotalTime(float)
extern "C" __attribute__((weak, used)) long _stub_fn_782() __asm__(ASM_SYM("_ZN14StandardStream20GetJumpBackTotalTimeEf"));
extern "C" long _stub_fn_782() { return 0; }
// StreamRecorder::DrawShowing()
extern "C" __attribute__((weak, used)) long _stub_fn_789() __asm__(ASM_SYM("_ZN14StreamRecorder11DrawShowingEv"));
extern "C" long _stub_fn_789() { return 0; }
// StreamRenderer::DrawToTexture()
extern "C" __attribute__((weak, used)) long _stub_fn_791() __asm__(ASM_SYM("_ZN14StreamRenderer13DrawToTextureEv"));
extern "C" long _stub_fn_791() { return 0; }
// StreamRenderer::SetCrewPhotoPlayerCenters()
extern "C" __attribute__((weak, used)) long _stub_fn_792() __asm__(ASM_SYM("_ZN14StreamRenderer25SetCrewPhotoPlayerCentersEv"));
extern "C" long _stub_fn_792() { return 0; }
// LiveCameraInput::TextureStore::StoreColorBufferClip(LiveCameraInput*, float, float, float, float)
extern "C" __attribute__((weak, used)) long _stub_fn_803() __asm__(ASM_SYM("_ZN15LiveCameraInput12TextureStore20StoreColorBufferClipEPS_ffff"));
extern "C" long _stub_fn_803() { return 0; }
// LiveCameraInput::TextureStore::StoreDepthBufferClip(LiveCameraInput*, float, float, float, float)
extern "C" __attribute__((weak, used)) long _stub_fn_804() __asm__(ASM_SYM("_ZN15LiveCameraInput12TextureStore20StoreDepthBufferClipEPS_ffff"));
extern "C" long _stub_fn_804() { return 0; }
// LiveCameraInput::TextureStore::UpdateFromColorBuffer(LiveCameraInput*)
extern "C" __attribute__((weak, used)) long _stub_fn_805() __asm__(ASM_SYM("_ZN15LiveCameraInput12TextureStore21UpdateFromColorBufferEPS_"));
extern "C" long _stub_fn_805() { return 0; }
// LiveCameraInput::TextureStore::UpdateFromDepthBuffer(LiveCameraInput*)
extern "C" __attribute__((weak, used)) long _stub_fn_806() __asm__(ASM_SYM("_ZN15LiveCameraInput12TextureStore21UpdateFromDepthBufferEPS_"));
extern "C" long _stub_fn_806() { return 0; }
// LiveCameraInput::ClearSnapshots()
extern "C" __attribute__((weak, used)) long _stub_fn_807() __asm__(ASM_SYM("_ZN15LiveCameraInput14ClearSnapshotsEv"));
extern "C" long _stub_fn_807() { return 0; }
// LiveCameraInput::SetAutoexposure(bool)
extern "C" __attribute__((weak, used)) long _stub_fn_808() __asm__(ASM_SYM("_ZN15LiveCameraInput15SetAutoexposureEb"));
extern "C" long _stub_fn_808() { return 0; }
// LiveCameraInput::SetExposureRegion(float, float, float, float)
extern "C" __attribute__((weak, used)) long _stub_fn_809() __asm__(ASM_SYM("_ZN15LiveCameraInput17SetExposureRegionEffff"));
extern "C" long _stub_fn_809() { return 0; }
// LiveCameraInput::NuiAudioDataCallback(_NUIAUDIO_RESULTS*)
extern "C" __attribute__((weak, used)) long _stub_fn_810() __asm__(ASM_SYM("_ZN15LiveCameraInput20NuiAudioDataCallbackEP17_NUIAUDIO_RESULTS"));
extern "C" long _stub_fn_810() { return 0; }
// LiveCameraInput::SetTweakedAutoexposure(bool)
extern "C" __attribute__((weak, used)) long _stub_fn_811() __asm__(ASM_SYM("_ZN15LiveCameraInput22SetTweakedAutoexposureEb"));
extern "C" long _stub_fn_811() { return 0; }
// NetCacheMgrXbox::NetCacheMgrXbox()
extern "C" __attribute__((weak, used)) long _stub_fn_813() __asm__(ASM_SYM("_ZN15NetCacheMgrXboxC1Ev"));
extern "C" long _stub_fn_813() { return 0; }
// VirtualKeyboard::PlatformPoll()
extern "C" __attribute__((weak, used)) long _stub_fn_844() __asm__(ASM_SYM("_ZN15VirtualKeyboard12PlatformPollEv"));
extern "C" long _stub_fn_844() { return 0; }
// VirtualKeyboard::GetInputString()
extern "C" __attribute__((weak, used)) long _stub_fn_845() __asm__(ASM_SYM("_ZN15VirtualKeyboard14GetInputStringEv"));
extern "C" long _stub_fn_845() { return 0; }
// VirtualKeyboard::ShowKeyboardUI(int, int, String, String, String, int)
extern "C" __attribute__((weak, used)) long _stub_fn_846() __asm__(ASM_SYM("_ZN15VirtualKeyboard14ShowKeyboardUIEii6StringS0_S0_i"));
extern "C" long _stub_fn_846() { return 0; }
// VoiceInputPanel::ActivateVoiceContext(Symbol)
extern "C" __attribute__((weak, used)) long _stub_fn_847() __asm__(ASM_SYM("_ZN15VoiceInputPanel20ActivateVoiceContextE6Symbol"));
extern "C" long _stub_fn_847() { return 0; }
// VoiceInputPanel::OnMsg(SpeechRecoMessage const&)
extern "C" __attribute__((weak, used)) long _stub_fn_848() __asm__(ASM_SYM("_ZN15VoiceInputPanel5OnMsgERK17SpeechRecoMessage"));
extern "C" long _stub_fn_848() { return 0; }
// PhysMemTypeTracker::PhysMemTypeTracker(Symbol)
extern "C" __attribute__((weak, used)) long _stub_fn_896() __asm__(ASM_SYM("_ZN18PhysMemTypeTrackerC1E6Symbol"));
extern "C" long _stub_fn_896() { return 0; }
// PhysMemTypeTracker::~PhysMemTypeTracker()
extern "C" __attribute__((weak, used)) long _stub_fn_897() __asm__(ASM_SYM("_ZN18PhysMemTypeTrackerD1Ev"));
extern "C" long _stub_fn_897() { return 0; }
// FitnessCalorieSortMgr::Handle(DataArray*, bool)
extern "C" __attribute__((weak, used)) long _stub_fn_923() __asm__(ASM_SYM("_ZN21FitnessCalorieSortMgr6HandleEP9DataArrayb"));
extern "C" long _stub_fn_923() { return 0; }
// KinectShareConnection::Poll()
extern "C" __attribute__((weak, used)) long _stub_fn_930() __asm__(ASM_SYM("_ZN21KinectShareConnection4PollEv"));
extern "C" long _stub_fn_930() { return 0; }
// KinectShareConnection::~KinectShareConnection()
extern "C" __attribute__((weak, used)) long _stub_fn_931() __asm__(ASM_SYM("_ZN21KinectShareConnectionD1Ev"));
extern "C" long _stub_fn_931() { return 0; }
// MultiUserGesturePanel::UpdateCharPic(UIPicture*, int, int, Symbol, Symbol)
extern "C" __attribute__((weak, used)) long _stub_fn_932() __asm__(ASM_SYM("_ZN21MultiUserGesturePanel13UpdateCharPicEP9UIPictureii6SymbolS2_"));
extern "C" long _stub_fn_932() { return 0; }
// MultiUserGesturePanel::UpdateVenueMesh(RndMesh*, int, int, Symbol, Symbol)
extern "C" __attribute__((weak, used)) long _stub_fn_933() __asm__(ASM_SYM("_ZN21MultiUserGesturePanel15UpdateVenueMeshEP7RndMeshii6SymbolS2_"));
extern "C" long _stub_fn_933() { return 0; }
// MultiUserGesturePanel::GetVoiceCommandOutfitTag(int, Symbol)
extern "C" __attribute__((weak, used)) long _stub_fn_934() __asm__(ASM_SYM("_ZN21MultiUserGesturePanel24GetVoiceCommandOutfitTagEi6Symbol"));
extern "C" long _stub_fn_934() { return 0; }
// MultiUserGesturePanel::UpdateProviderPlayerIndices()
extern "C" __attribute__((weak, used)) long _stub_fn_935() __asm__(ASM_SYM("_ZN21MultiUserGesturePanel27UpdateProviderPlayerIndicesEv"));
extern "C" long _stub_fn_935() { return 0; }
// HandInvokeGestureFilter::Update(Skeleton const&, int)
extern "C" __attribute__((weak, used)) long _stub_fn_942() __asm__(ASM_SYM("_ZN23HandInvokeGestureFilter6UpdateERK8Skeletoni"));
extern "C" long _stub_fn_942() { return 0; }
// SingleUserCrewSelectPanel::UpdateCrewMesh(RndMesh*, int, Symbol)
extern "C" __attribute__((weak, used)) long _stub_fn_947() __asm__(ASM_SYM("_ZN25SingleUserCrewSelectPanel14UpdateCrewMeshEP7RndMeshi6Symbol"));
extern "C" long _stub_fn_947() { return 0; }
// Rand::Int(int, int) - now implemented in Rand.cpp
// Rand::Int() - now implemented in Rand.cpp
// DxTex::SetDeviceTex(D3DTexture*)
extern "C" __attribute__((weak, used)) long _stub_fn_967() __asm__(ASM_SYM("_ZN5DxTex12SetDeviceTexEP10D3DTexture"));
extern "C" long _stub_fn_967() { return 0; }
// DxMesh::GetMultimeshFaces()
extern "C" __attribute__((weak, used)) long _stub_fn_976() __asm__(ASM_SYM("_ZN6DxMesh17GetMultimeshFacesEv"));
extern "C" long _stub_fn_976() { return 0; }
// RndTex::Load, PreLoad, PostLoad: provided by RndTex_Native.cpp
// FlowPtr<Hmx::Object>::FlowPtr(FlowPtr<Hmx::Object> const&)
extern "C" __attribute__((weak, used)) long _stub_fn_992() __asm__(ASM_SYM("_ZN7FlowPtrIN3Hmx6ObjectEEC1ERKS2_"));
extern "C" long _stub_fn_992() { return 0; }
// HDCache::Flush()
extern "C" __attribute__((weak, used)) long _stub_fn_996() __asm__(ASM_SYM("_ZN7HDCache5FlushEv"));
extern "C" long _stub_fn_996() { return 0; }
// LoadMgr::PollFrontLoader() - now implemented in Loader.cpp
// MoveDir::EnqueueDetectFrames(float, int, std::vector<DetectFrame, std::allocator<DetectFrame> >&, FilterVersion const*)
extern "C" __attribute__((weak, used)) long _stub_fn_1006() __asm__(ASM_SYM("_ZN7MoveDir19EnqueueDetectFramesEfiRSt6vectorI11DetectFrameSaIS1_EEPK13FilterVersion"));
extern "C" long _stub_fn_1006() { return 0; }
// UILabel::LabelStyle::~LabelStyle()
extern "C" __attribute__((weak, used)) long _stub_fn_1059() __asm__(ASM_SYM("_ZN7UILabel10LabelStyleD1Ev"));
extern "C" long _stub_fn_1059() { return 0; }
// UILabel::Terminate()
extern "C" __attribute__((weak, used)) long _stub_fn_1060() __asm__(ASM_SYM("_ZN7UILabel9TerminateEv"));
extern "C" long _stub_fn_1060() { return 0; }
// DingoJob::GetResponseString()
extern "C" __attribute__((weak, used)) long _stub_fn_1093() __asm__(ASM_SYM("_ZN8DingoJob17GetResponseStringEv"));
extern "C" long _stub_fn_1093() { return 0; }
// Waypoint::Highlight()
extern "C" __attribute__((weak, used)) long _stub_fn_1129() __asm__(ASM_SYM("_ZN8Waypoint9HighlightEv"));
extern "C" long _stub_fn_1129() { return 0; }
// NgDOFProc::Set(RndCam*, float, float, float, float)
extern "C" __attribute__((weak, used)) long _stub_fn_1154() __asm__(ASM_SYM("_ZN9NgDOFProc3SetEP6RndCamffff"));
extern "C" long _stub_fn_1154() { return 0; }
// ObjDirPtr<ObjectDir>::ObjDirPtr(ObjectDir*)
extern "C" __attribute__((weak, used)) long _stub_fn_1156() __asm__(ASM_SYM("_ZN9ObjDirPtrI9ObjectDirEC1EPS0_"));
extern "C" long _stub_fn_1156() { return 0; }
// RndBitmap::Load: provided by RndTex_Native.cpp
// Spotlight::RemoveFromLists(Spotlight*)
extern "C" __attribute__((weak, used)) long _stub_fn_1193() __asm__(ASM_SYM("_ZN9Spotlight15RemoveFromListsEPS_"));
extern "C" long _stub_fn_1193() { return 0; }
// WebSvcMgr::Poll()
extern "C" __attribute__((weak, used)) long _stub_fn_1202() __asm__(ASM_SYM("_ZN9WebSvcMgr4PollEv"));
extern "C" long _stub_fn_1202() { return 0; }
// ArcDetector::IsLockedIn() const
extern "C" __attribute__((weak, used)) long _stub_fn_1285() __asm__(ASM_SYM("_ZNK11ArcDetector10IsLockedInEv"));
extern "C" long _stub_fn_1285() { return 0; }
// ArcDetector::GetPathError() const
extern "C" __attribute__((weak, used)) long _stub_fn_1286() __asm__(ASM_SYM("_ZNK11ArcDetector12GetPathErrorEv"));
extern "C" long _stub_fn_1286() { return 0; }
// ArcDetector::GetPathLength() const
extern "C" __attribute__((weak, used)) long _stub_fn_1287() __asm__(ASM_SYM("_ZNK11ArcDetector13GetPathLengthEv"));
extern "C" long _stub_fn_1287() { return 0; }
// ArcDetector::GetSwipeAmount() const
extern "C" __attribute__((weak, used)) long _stub_fn_1288() __asm__(ASM_SYM("_ZNK11ArcDetector14GetSwipeAmountEv"));
extern "C" long _stub_fn_1288() { return 0; }
// LightPreset::GetKey stub removed — real impl in LightPreset.cpp
// BaseSkeleton::LimbNormPos(SkeletonCoordSys, SkeletonJoint, bool, Vector3 const&, Vector3&) const
extern "C" __attribute__((weak, used)) long _stub_fn_1300() __asm__(ASM_SYM("_ZNK12BaseSkeleton11LimbNormPosE16SkeletonCoordSys13SkeletonJointbRK7Vector3RS2_"));
extern "C" long _stub_fn_1300() { return 0; }
// BinkMovieImpl::MsPerFrame() const
extern "C" __attribute__((weak, used)) long _stub_fn_1319() __asm__(ASM_SYM("_ZNK13BinkMovieImpl10MsPerFrameEv"));
extern "C" long _stub_fn_1319() { return 0; }
// BinkMovieImpl::Ready() const
extern "C" __attribute__((weak, used)) long _stub_fn_1320() __asm__(ASM_SYM("_ZNK13BinkMovieImpl5ReadyEv"));
extern "C" long _stub_fn_1320() { return 0; }
// BinkMovieImpl::IsOpen() const
extern "C" __attribute__((weak, used)) long _stub_fn_1321() __asm__(ASM_SYM("_ZNK13BinkMovieImpl6IsOpenEv"));
extern "C" long _stub_fn_1321() { return 0; }
// BinkMovieImpl::GetFrame() const
extern "C" __attribute__((weak, used)) long _stub_fn_1322() __asm__(ASM_SYM("_ZNK13BinkMovieImpl8GetFrameEv"));
extern "C" long _stub_fn_1322() { return 0; }
// BinkMovieImpl::IsLoading() const
extern "C" __attribute__((weak, used)) long _stub_fn_1323() __asm__(ASM_SYM("_ZNK13BinkMovieImpl9IsLoadingEv"));
extern "C" long _stub_fn_1323() { return 0; }
// BinkMovieImpl::NumFrames() const
extern "C" __attribute__((weak, used)) long _stub_fn_1324() __asm__(ASM_SYM("_ZNK13BinkMovieImpl9NumFramesEv"));
extern "C" long _stub_fn_1324() { return 0; }
// HamStorePanel::GetOfferIDsToEnumerate(std::vector<unsigned long long, std::allocator<unsigned long long> >&, bool) const
extern "C" __attribute__((weak, used)) long _stub_fn_1328() __asm__(ASM_SYM("_ZNK13HamStorePanel22GetOfferIDsToEnumerateERSt6vectorIySaIyEEb"));
extern "C" long _stub_fn_1328() { return 0; }
// DancerSkeleton::CameraToPlayerXfm(SkeletonCoordSys, Transform&) const
extern "C" __attribute__((weak, used)) long _stub_fn_1335() __asm__(ASM_SYM("_ZNK14DancerSkeleton17CameraToPlayerXfmE16SkeletonCoordSysR9Transform"));
extern "C" long _stub_fn_1335() { return 0; }
// LiveCameraInput::GetAutoexposure() const
extern "C" __attribute__((weak, used)) long _stub_fn_1339() __asm__(ASM_SYM("_ZNK15LiveCameraInput15GetAutoexposureEv"));
extern "C" long _stub_fn_1339() { return 0; }
// LiveCameraInput::SetTrackedSkeletons(int, int) const
extern "C" __attribute__((weak, used)) long _stub_fn_1340() __asm__(ASM_SYM("_ZNK15LiveCameraInput19SetTrackedSkeletonsEii"));
extern "C" long _stub_fn_1340() { return 0; }
// LiveCameraInput::GetTweakedAutoexposure() const
extern "C" __attribute__((weak, used)) long _stub_fn_1341() __asm__(ASM_SYM("_ZNK15LiveCameraInput22GetTweakedAutoexposureEv"));
extern "C" long _stub_fn_1341() { return 0; }
// VoiceInputPanel::CreatePlaylistEditorGrammar() const
extern "C" __attribute__((weak, used)) long _stub_fn_1342() __asm__(ASM_SYM("_ZNK15VoiceInputPanel27CreatePlaylistEditorGrammarEv"));
extern "C" long _stub_fn_1342() { return 0; }
// PlaylistSortByType::NewHeaderNode(NavListItemNode*, NavListItemNode*) const
extern "C" __attribute__((weak, used)) long _stub_fn_1360() __asm__(ASM_SYM("_ZNK18PlaylistSortByType13NewHeaderNodeEP15NavListItemNodeS1_"));
extern "C" long _stub_fn_1360() { return 0; }
// GameEndedDataPointJob::CompileMoveRatings(String&, int, bool) const
extern "C" __attribute__((weak, used)) long _stub_fn_1370() __asm__(ASM_SYM("_ZNK21GameEndedDataPointJob18CompileMoveRatingsER6Stringib"));
extern "C" long _stub_fn_1370() { return 0; }
// MultiUserGesturePanel::HasNavList() const
extern "C" __attribute__((weak, used)) long _stub_fn_1371() __asm__(ASM_SYM("_ZNK21MultiUserGesturePanel10HasNavListEv"));
extern "C" long _stub_fn_1371() { return 0; }
// AccomplishmentProgress::GetNumCompleted() const
extern "C" __attribute__((weak, used)) long _stub_fn_1372() __asm__(ASM_SYM("_ZNK22AccomplishmentProgress15GetNumCompletedEv"));
extern "C" long _stub_fn_1372() { return 0; }
// AccomplishmentProgress::GetTotalSongsPlayed() const
extern "C" __attribute__((weak, used)) long _stub_fn_1373() __asm__(ASM_SYM("_ZNK22AccomplishmentProgress19GetTotalSongsPlayedEv"));
extern "C" long _stub_fn_1373() { return 0; }
// AccomplishmentProgress::GetTotalCampaignSongsPlayed() const
extern "C" __attribute__((weak, used)) long _stub_fn_1375() __asm__(ASM_SYM("_ZNK22AccomplishmentProgress27GetTotalCampaignSongsPlayedEv"));
extern "C" long _stub_fn_1375() { return 0; }
// DirectionGestureFilterDoubleUser::IsHandValid(Skeleton const&) const
extern "C" __attribute__((weak, used)) long _stub_fn_1378() __asm__(ASM_SYM("_ZNK32DirectionGestureFilterDoubleUser11IsHandValidERK8Skeleton"));
extern "C" long _stub_fn_1378() { return 0; }
// DirectionGestureFilterDoubleUser::IsValidScrollPos(Skeleton const&) const
extern "C" __attribute__((weak, used)) long _stub_fn_1379() __asm__(ASM_SYM("_ZNK32DirectionGestureFilterDoubleUser16IsValidScrollPosERK8Skeleton"));
extern "C" long _stub_fn_1379() { return 0; }
// DirectionGestureFilterDoubleUser::GetValidSkeletons(int&, int&) const
extern "C" __attribute__((weak, used)) long _stub_fn_1380() __asm__(ASM_SYM("_ZNK32DirectionGestureFilterDoubleUser17GetValidSkeletonsERiS0_"));
extern "C" long _stub_fn_1380() { return 0; }
// DirectionGestureFilterSingleUser::HandAtSide(Skeleton const&, float, float, float) const
extern "C" __attribute__((weak, used)) long _stub_fn_1381() __asm__(ASM_SYM("_ZNK32DirectionGestureFilterSingleUser10HandAtSideERK8Skeletonfff"));
extern "C" long _stub_fn_1381() { return 0; }
// AllocInfo::PrintForReport(TextStream&) const
extern "C" __attribute__((weak, used)) long _stub_fn_1397() __asm__(ASM_SYM("_ZNK9AllocInfo14PrintForReportER10TextStream"));
extern "C" long _stub_fn_1397() { return 0; }
// operator>>(BinStream&, FilePath&) — implemented in FilePath.cpp
// non-virtual thunk to FitnessCalorieSortMgr::Handle(DataArray*, bool)
extern "C" __attribute__((weak, used)) long _stub_fn_1494() __asm__(ASM_SYM("_ZThn8_N21FitnessCalorieSortMgr6HandleEP9DataArrayb"));
extern "C" long _stub_fn_1494() { return 0; }
// virtual thunk to LightPreset::Load stub removed — real impl in LightPreset.cpp

// virtual thunk to LightPreset::Replace stub removed — real impl in LightPreset.cpp

// vtable and typeinfo stubs for classes without key functions.
// WARNING: These are zero-initialized. If dynamic_cast hits one of these,
__attribute__((weak, used)) char _stub_vt_10[1024] __asm__(ASM_SYM("_ZTI17CharSignalApplier")) = {};
__attribute__((weak, used)) char _stub_vt_15[1024] __asm__(ASM_SYM("_ZTI5DxTex")) = {};
__attribute__((weak, used)) char _stub_vt_35[1024] __asm__(ASM_SYM("_ZTV17CharSignalApplier")) = {};

// =============================================================================
// Asm-label stubs for remaining undefined symbols (ObjPtrVec/ObjPtrList related)
// =============================================================================

// merged_ObjPtrListPopBack (ICF merged stub)
extern "C" __attribute__((weak, used)) long _stub_popback() __asm__(ASM_SYM("_Z24merged_ObjPtrListPopBackPv"));
extern "C" long _stub_popback() { return 0; }

#endif // !__EMSCRIPTEN__
