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
// TheMC: now defined in Memcard_Stub.cpp
// TheMoveMgr: now defined in MoveMgr.cpp
// TheMovieSys: removed - provided by MovieSys.cpp
void* TheMQSongSortMgr = 0;
// TheNgRnd: removed - provided by Rnd_Stub.cpp
// TheRenderState: removed - provided by RenderState_Native.cpp
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
// SetupHXGuitar(int, _XINPUT_CAPABILITIES const&)
extern "C" __attribute__((weak, used)) long _stub_fn_29() __asm__(ASM_SYM("_Z13SetupHXGuitariRK20_XINPUT_CAPABILITIES"));
extern "C" long _stub_fn_29() { return 0; }
// SetupHXKeytar(int, _XINPUT_CAPABILITIES const&)
extern "C" __attribute__((weak, used)) long _stub_fn_30() __asm__(ASM_SYM("_Z13SetupHXKeytariRK20_XINPUT_CAPABILITIES"));
extern "C" long _stub_fn_30() { return 0; }
// SystemPreInit(int, char**, char const*) -- provided by System_Native.cpp
// XNetDnsLookup(int, int, void*)
extern "C" __attribute__((weak, used)) long _stub_fn_33() __asm__(ASM_SYM("_Z13XNetDnsLookupiiPv"));
extern "C" long _stub_fn_33() { return 0; }
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
// ValidateThreadId(unsigned long)
extern "C" __attribute__((weak, used)) long _stub_fn_61() __asm__(ASM_SYM("_Z16ValidateThreadIdm"));
extern "C" long _stub_fn_61() { return 0; }
// jpeg_set_defaults(jpeg_compress_struct*)
extern "C" __attribute__((weak, used)) long _stub_fn_65() __asm__(ASM_SYM("_Z17jpeg_set_defaultsP20jpeg_compress_struct"));
extern "C" long _stub_fn_65() { return 0; }
// SetupHXRealGuitar(int, _XINPUT_CAPABILITIES const&)
extern "C" __attribute__((weak, used)) long _stub_fn_68() __asm__(ASM_SYM("_Z17SetupHXRealGuitariRK20_XINPUT_CAPABILITIES"));
extern "C" long _stub_fn_68() { return 0; }
// jpeg_CreateCompress(jpeg_compress_struct*, int, unsigned long)
extern "C" __attribute__((weak, used)) long _stub_fn_78() __asm__(ASM_SYM("_Z19jpeg_CreateCompressP20jpeg_compress_structim"));
extern "C" long _stub_fn_78() { return 0; }
// jpeg_start_compress(jpeg_compress_struct*, unsigned char)
extern "C" __attribute__((weak, used)) long _stub_fn_79() __asm__(ASM_SYM("_Z19jpeg_start_compressP20jpeg_compress_structh"));
extern "C" long _stub_fn_79() { return 0; }
// WaitForSingleObject(int, int)
extern "C" __attribute__((weak, used)) long _stub_fn_83() __asm__(ASM_SYM("_Z19WaitForSingleObjectii"));
extern "C" long _stub_fn_83() { return 0; }
// jpeg_finish_compress(jpeg_compress_struct*)
extern "C" __attribute__((weak, used)) long _stub_fn_89() __asm__(ASM_SYM("_Z20jpeg_finish_compressP20jpeg_compress_struct"));
extern "C" long _stub_fn_89() { return 0; }
// jpeg_write_scanlines(jpeg_compress_struct*, unsigned char**, unsigned int)
extern "C" __attribute__((weak, used)) long _stub_fn_90() __asm__(ASM_SYM("_Z20jpeg_write_scanlinesP20jpeg_compress_structPPhj"));
extern "C" long _stub_fn_90() { return 0; }
// GetXinputSinceLastFrame(int, _XINPUT_STATE*, unsigned int*)
extern "C" __attribute__((weak, used)) long _stub_fn_102() __asm__(ASM_SYM("_Z23GetXinputSinceLastFrameiP13_XINPUT_STATEPj"));
extern "C" long _stub_fn_102() { return 0; }
// NuiTransformSkeletonToDepthImage(__vector4, float*, float*)
extern "C" __attribute__((weak, used)) long _stub_fn_111() __asm__(ASM_SYM("_Z32NuiTransformSkeletonToDepthImage9__vector4PfS0_"));
extern "C" long _stub_fn_111() { return 0; }
// altCfg: removed — was most vexing parse in Locale.cpp (now fixed, no function exists)
// LightPreset stubs removed — real impl in LightPreset.cpp
// RndFontBase::Load(BinStream&) — now implemented in FontBase.cpp
// XboxMapFile::ParseStack(char const*, StackData*, int, FixedString&)
extern "C" __attribute__((weak, used)) long _stub_fn_519() __asm__(ASM_SYM("_ZN11XboxMapFile10ParseStackEPKcP9StackDataiR11FixedString"));
extern "C" long _stub_fn_519() { return 0; }
// CacheMgrXbox::CacheMgrXbox()
extern "C" __attribute__((weak, used)) long _stub_fn_525() __asm__(ASM_SYM("_ZN12CacheMgrXboxC1Ev"));
extern "C" long _stub_fn_525() { return 0; }
// DrawString3D::DrawFixedZ — now in Graph.cpp
// AutoSlowFrame — now in Timer.h (inline, C2/D2 mangling)
// NetLoaderXbox::NetLoaderXbox(String const&)
extern "C" __attribute__((weak, used)) long _stub_fn_640() __asm__(ASM_SYM("_ZN13NetLoaderXboxC1ERK6String"));
extern "C" long _stub_fn_640() { return 0; }

// ObjRefConcrete<LightPreset, ObjectDir>::CopyRef stub removed — real impl in LightPreset.cpp
// RndRenderState stubs (758-773): removed - provided by RenderState_Native.cpp
// NetCacheMgrXbox::NetCacheMgrXbox()
extern "C" __attribute__((weak, used)) long _stub_fn_813() __asm__(ASM_SYM("_ZN15NetCacheMgrXboxC1Ev"));
extern "C" long _stub_fn_813() { return 0; }
// SingleUserCrewSelectPanel::UpdateCrewMesh(RndMesh*, int, Symbol) - now implemented in SingleUserCrewSelectPanel.cpp
// Rand::Int(int, int) - now implemented in Rand.cpp
// Rand::Int() - now implemented in Rand.cpp
// DxTex::SetDeviceTex(D3DTexture*)
extern "C" __attribute__((weak, used)) long _stub_fn_967() __asm__(ASM_SYM("_ZN5DxTex12SetDeviceTexEP10D3DTexture"));
extern "C" long _stub_fn_967() { return 0; }
// RndTex::Load, PreLoad, PostLoad: provided by RndTex_Native.cpp
// LoadMgr::PollFrontLoader() - now implemented in Loader.cpp
// RndBitmap::Load: provided by RndTex_Native.cpp
// LightPreset::GetKey stub removed — real impl in LightPreset.cpp
// operator>>(BinStream&, FilePath&) — implemented in FilePath.cpp
// virtual thunk to LightPreset::Load stub removed — real impl in LightPreset.cpp

// virtual thunk to LightPreset::Replace stub removed — real impl in LightPreset.cpp

// vtable and typeinfo stubs for classes without key functions.
__attribute__((weak, used)) char _stub_vt_15[1024] __asm__(ASM_SYM("_ZTI5DxTex")) = {};

// =============================================================================
// Asm-label stubs for remaining undefined symbols (ObjPtrVec/ObjPtrList related)
// =============================================================================

#endif // !__EMSCRIPTEN__

// LabelShrinkWrapper::Poll — stub (no decomp source)
extern "C" void _ZN18LabelShrinkWrapper4PollEv() {}
