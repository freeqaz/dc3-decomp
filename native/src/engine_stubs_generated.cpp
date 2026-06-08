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
void BinkSetMemory(void*(*)(int), void(*)(void*)) {}
// ctr_reinit and ctr_start now provided by tomcrypt/ctr.c
int D3DResource_Release() { return 0; }
__attribute__((weak)) int DataInput(void*, int) { return 0; }
int DmCaptureStackBackTrace() { return 0; }
int DmGetSystemInfo() { return 0; }
int DmMapDevkitDrive() { return 0; }
struct complex { double x, y; };
void expand(complex*, int, complex*, ...) {}
complex expj(double) { return {0, 0}; }
__attribute__((weak)) int FileRecursePattern() { return 0; }
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
int lbl_82F0E8A4 = 4;  // RhythmDetectorGroup .data:0xF8 (0x82F0E8A4); HollaBackMinigame OnBeat compares subMeasure against it
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
int NuiWaveSetEnabled() { return -1; } // non-zero = failure (no Kinect on native)
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
// TheMQSongSortMgr: removed - provided by SongSortMgr_Native.cpp
// TheNgRnd: removed - provided by Rnd_Stub.cpp
// TheRenderState: removed - provided by RenderState_Native.cpp
// TheRnd: removed - provided by Rnd_Stub.cpp
// TheServer: removed - provided by DingoSvr_Native.cpp
// TheShaderMgr: removed - provided by Rnd_Stub.cpp
void* TheSkeletonIdentifier = 0;
void* TheSkeletonViz = 0;
// TheSongSortMgr: removed - provided by SongSortMgr_Native.cpp
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
int XNetUnregisterInAddr() { return 0; }
int XNetXnAddrToMachineId() { return 0; }
int XShowMarketplaceDownloadItemsUI() { return 0; }
int XShowNuiTroubleshooterUI() { return 0; }
int XTitleServerCreateEnumerator() { return 0; }
#endif
extern "C" int XNetRandom(unsigned char *pb, unsigned int cb) {
    for (unsigned int i = 0; i < cb; i++)
        pb[i] = (unsigned char)rand();
    return 0;
}
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

// RadAlloc(int)
extern "C" __attribute__((weak, used)) void* _stub_radalloc() __asm__(ASM_SYM("_Z8RadAlloci"));
extern "C" void* _stub_radalloc() { return 0; }
// PhysicalAllocTracked(unsigned long, unsigned long, char const*, int, char const*)
extern "C" __attribute__((weak, used)) void* _stub_physalloc() __asm__(ASM_SYM("_Z20PhysicalAllocTrackedmmPKciS0_"));
extern "C" void* _stub_physalloc() { return 0; }
// DxRnd::ReleaseAutoRelease()
extern "C" __attribute__((weak, used)) long _stub_dxrnd_rar() __asm__(ASM_SYM("_ZN5DxRnd18ReleaseAutoReleaseEv"));
extern "C" long _stub_dxrnd_rar() { return 0; }
// D3DCubeTexture_UnlockRect
extern "C" __attribute__((weak)) int D3DCubeTexture_UnlockRect() { return 0; }
// NuiCameraSetExposureRegionOfInterest
extern "C" __attribute__((weak)) int NuiCameraSetExposureRegionOfInterest() { return 0; }
// NuiCameraSetProperty
extern "C" __attribute__((weak)) int NuiCameraSetProperty() { return 0; }
// NuiSkeletonSetTrackedSkeletons
extern "C" __attribute__((weak)) int NuiSkeletonSetTrackedSkeletons() { return 0; }

// BinkSetMemory(void*(*)(int), void(*)(void*)) — C++ mangled version
extern "C" __attribute__((weak, used)) long _stub_binksetmem_cpp() __asm__(ASM_SYM("_Z13BinkSetMemoryPFPviEPFvS_E"));
extern "C" long _stub_binksetmem_cpp() { return 0; }
// BinkStartAsyncThread(int, int) — C++ mangled version
extern "C" __attribute__((weak, used)) long _stub_binkstartasync() __asm__(ASM_SYM("_Z20BinkStartAsyncThreadii"));
extern "C" long _stub_binkstartasync() { return 0; }
// XGOffsetResourceAddress
extern "C" __attribute__((weak)) int XGOffsetResourceAddress() { return 0; }
// XGSetTextureHeader
extern "C" __attribute__((weak)) int XGSetTextureHeader() { return 0; }
// XNetServerToInAddr
extern "C" __attribute__((weak)) int XNetServerToInAddr() { return 0; }

// vtable and typeinfo stubs for classes without key functions.
__attribute__((weak, used)) char _stub_vt_15[1024] __asm__(ASM_SYM("_ZTI5DxTex")) = {};
// typeinfo for FFmpegMovieImpl
__attribute__((weak, used)) char _stub_ti_ffmpeg[128] __asm__(ASM_SYM("_ZTI15FFmpegMovieImpl")) = {};
// (anonymous namespace)::YUVtoRGB(int, int, int)
extern "C" __attribute__((weak, used)) long _stub_yuvtorgb() __asm__(ASM_SYM("_ZN12_GLOBAL__N_18YUVtoRGBEiii"));
extern "C" long _stub_yuvtorgb() { return 0; }

// =============================================================================
// Asm-label stubs for remaining undefined symbols (ObjPtrVec/ObjPtrList related)
// =============================================================================

// PropSync<ObjPtrVec<T>> stubs
extern "C" __attribute__((weak, used)) long _stub_propsync_0() __asm__(ASM_SYM("_Z8PropSyncI8CharClipEbR9ObjPtrVecIT_9ObjectDirER8DataNodeP9DataArrayi6PropOp"));
extern "C" long _stub_propsync_0() { return 0; }
extern "C" __attribute__((weak, used)) long _stub_propsync_1() __asm__(ASM_SYM("_Z8PropSyncI4FlowEbR9ObjPtrVecIT_9ObjectDirER8DataNodeP9DataArrayi6PropOp"));
extern "C" long _stub_propsync_1() { return 0; }
extern "C" __attribute__((weak, used)) long _stub_propsync_2() __asm__(ASM_SYM("_Z8PropSyncIN3Hmx6ObjectEEbR9ObjPtrVecIT_9ObjectDirER8DataNodeP9DataArrayi6PropOp"));
extern "C" long _stub_propsync_2() { return 0; }
extern "C" __attribute__((weak, used)) long _stub_propsync_3() __asm__(ASM_SYM("_Z8PropSyncI14RhythmDetectorEbR9ObjPtrVecIT_9ObjectDirER8DataNodeP9DataArrayi6PropOp"));
extern "C" long _stub_propsync_3() { return 0; }
extern "C" __attribute__((weak, used)) long _stub_propsync_4() __asm__(ASM_SYM("_Z8PropSyncI11RndDrawableEbR9ObjPtrVecIT_9ObjectDirER8DataNodeP9DataArrayi6PropOp"));
extern "C" long _stub_propsync_4() { return 0; }
extern "C" __attribute__((weak, used)) long _stub_propsync_5() __asm__(ASM_SYM("_Z8PropSyncI6RndMatEbR9ObjPtrVecIT_9ObjectDirER8DataNodeP9DataArrayi6PropOp"));
extern "C" long _stub_propsync_5() { return 0; }
extern "C" __attribute__((weak, used)) long _stub_propsync_6() __asm__(ASM_SYM("_Z8PropSyncI16RndTransformableEbR9ObjPtrVecIT_9ObjectDirER8DataNodeP9DataArrayi6PropOp"));
extern "C" long _stub_propsync_6() { return 0; }
extern "C" __attribute__((weak, used)) long _stub_propsync_7() __asm__(ASM_SYM("_Z8PropSyncI8WaypointEbR9ObjPtrVecIT_9ObjectDirER8DataNodeP9DataArrayi6PropOp"));
extern "C" long _stub_propsync_7() { return 0; }

// CharHair::Hookup
extern "C" __attribute__((weak, used)) long _stub_hookup() __asm__(ASM_SYM("_ZN8CharHair6HookupER10ObjPtrListI11CharCollide9ObjectDirE"));
extern "C" long _stub_hookup() { return 0; }

// merged_ObjPtrListPopBack (ICF merged stub)
extern "C" __attribute__((weak, used)) long _stub_popback() __asm__(ASM_SYM("_Z24merged_ObjPtrListPopBackPv"));
extern "C" long _stub_popback() { return 0; }

// RndVelocityBuffer::Draw
extern "C" __attribute__((weak, used)) long _stub_velbuf_draw() __asm__(ASM_SYM("_ZN17RndVelocityBuffer4DrawEP6RndCamR10ObjPtrListI11RndDrawable9ObjectDirE"));
extern "C" long _stub_velbuf_draw() { return 0; }

// EventTask::EventTask (C1 and C2 constructors)
extern "C" __attribute__((weak, used)) long _stub_eventtask_c1() __asm__(ASM_SYM("_ZN9EventTaskC1EP9FlowTimerP9ObjPtrVecI8FlowNode9ObjectDirE9TaskUnitsf"));
extern "C" long _stub_eventtask_c1() { return 0; }
extern "C" __attribute__((weak, used)) long _stub_eventtask_c2() __asm__(ASM_SYM("_ZN9EventTaskC2EP9FlowTimerP9ObjPtrVecI8FlowNode9ObjectDirE9TaskUnitsf"));
extern "C" long _stub_eventtask_c2() { return 0; }

// ScanForOutPorts
extern "C" __attribute__((weak, used)) long _stub_scanoutports() __asm__(ASM_SYM("_Z15ScanForOutPortsR9ObjPtrVecI11FlowOutPort9ObjectDirEP8FlowNodeP4Flow"));
extern "C" long _stub_scanoutports() { return 0; }

// =============================================================================
// macOS libc++ ABI stubs
//
// On macOS, libc++ mangles std::list as std::__1::list (not std::__cxx11::list
// from libstdc++). The asm-name stubs above use libstdc++ mangling, so they
// don't resolve on macOS. These use __asm__ with correct libc++ mangled names.
// Names obtained by compiling actual C++ signatures with Apple clang.
// =============================================================================
#ifdef __APPLE__

// --- std::list parameter stubs (libc++ ABI) ---

// ListProperties(std::list<Symbol>&, Symbol, Symbol, std::list<Symbol>*, bool)
extern "C" __attribute__((weak, used)) long _stub_apple_listprops() __asm__("__Z14ListPropertiesRNSt3__14listI6SymbolNS_9allocatorIS1_EEEES1_S1_PS4_b");
extern "C" long _stub_apple_listprops() { return 0; }

// CharDriver::PollDeps(std::list<Hmx::Object*>&, std::list<Hmx::Object*>&)
extern "C" __attribute__((weak, used)) long _stub_apple_chardriver_polldeps() __asm__("__ZN10CharDriver8PollDepsERNSt3__14listIPN3Hmx6ObjectENS0_9allocatorIS4_EEEES8_");
extern "C" long _stub_apple_chardriver_polldeps() { return 0; }

// CharServoBone::PollDeps(std::list<Hmx::Object*>&, std::list<Hmx::Object*>&)
extern "C" __attribute__((weak, used)) long _stub_apple_charservo_polldeps() __asm__("__ZN13CharServoBone8PollDepsERNSt3__14listIPN3Hmx6ObjectENS0_9allocatorIS4_EEEES8_");
extern "C" long _stub_apple_charservo_polldeps() { return 0; }

// CharEyes::PollDeps(std::list<Hmx::Object*>&, std::list<Hmx::Object*>&)
extern "C" __attribute__((weak, used)) long _stub_apple_chareyes_polldeps() __asm__("__ZN8CharEyes8PollDepsERNSt3__14listIPN3Hmx6ObjectENS0_9allocatorIS4_EEEES8_");
extern "C" long _stub_apple_chareyes_polldeps() { return 0; }

// CharEyes::ListPollChildren(std::list<RndPollable*>&) const
extern "C" __attribute__((weak, used)) long _stub_apple_chareyes_listpoll() __asm__("__ZNK8CharEyes16ListPollChildrenERNSt3__14listIP11RndPollableNS0_9allocatorIS3_EEEE");
extern "C" long _stub_apple_chareyes_listpoll() { return 0; }

// CharBones::AddBones(const std::list<Bone>&)
extern "C" __attribute__((weak, used)) long _stub_apple_charbones_addbones() __asm__("__ZN9CharBones8AddBonesERKNSt3__14listI4BoneNS0_9allocatorIS2_EEEE");
extern "C" long _stub_apple_charbones_addbones() { return 0; }

// DepthBuffer3D::ListDrawChildren(std::list<RndDrawable*>&)
extern "C" __attribute__((weak, used)) long _stub_apple_depth3d_listdraw() __asm__("__ZN13DepthBuffer3D16ListDrawChildrenERNSt3__14listIP11RndDrawableNS0_9allocatorIS3_EEEE");
extern "C" long _stub_apple_depth3d_listdraw() { return 0; }

// HamStorePanel::UpdateOffers(std::list<EnumProduct> const&, bool)
extern "C" __attribute__((weak, used)) long _stub_apple_hamstore_update() __asm__("__ZN13HamStorePanel12UpdateOffersERKNSt3__14listI11EnumProductNS0_9allocatorIS2_EEEEb");
extern "C" long _stub_apple_hamstore_update() { return 0; }

// NavListHeaderNode::SelectChildren(std::list<NavListSortNode*>&, int)
extern "C" __attribute__((weak, used)) long _stub_apple_navlist_select() __asm__("__ZN17NavListHeaderNode14SelectChildrenERNSt3__14listIP15NavListSortNodeNS0_9allocatorIS3_EEEEi");
extern "C" long _stub_apple_navlist_select() { return 0; }

// RndParticleSys::Mats(std::list<RndMat*>&, bool)
extern "C" __attribute__((weak, used)) long _stub_apple_partsys_mats() __asm__("__ZN14RndParticleSys4MatsERNSt3__14listIP6RndMatNS0_9allocatorIS3_EEEEb");
extern "C" long _stub_apple_partsys_mats() { return 0; }

// RndFlare::Mats(std::list<RndMat*>&, bool)
extern "C" __attribute__((weak, used)) long _stub_apple_flare_mats() __asm__("__ZN8RndFlare4MatsERNSt3__14listIP6RndMatNS0_9allocatorIS3_EEEEb");
extern "C" long _stub_apple_flare_mats() { return 0; }

// Spotlight::Mats(std::list<RndMat*>&, bool)
extern "C" __attribute__((weak, used)) long _stub_apple_spotlight_mats() __asm__("__ZN9Spotlight4MatsERNSt3__14listIP6RndMatNS0_9allocatorIS3_EEEEb");
extern "C" long _stub_apple_spotlight_mats() { return 0; }

// WorldCrowd::Mats(std::list<RndMat*>&, bool)
extern "C" __attribute__((weak, used)) long _stub_apple_worldcrowd_mats() __asm__("__ZN10WorldCrowd4MatsERNSt3__14listIP6RndMatNS0_9allocatorIS3_EEEEb");
extern "C" long _stub_apple_worldcrowd_mats() { return 0; }

// RndAmbientOcclusion::BurnTransform(RndMesh*, std::list<RndMesh*>&) const
extern "C" __attribute__((weak, used)) long _stub_apple_rndao_burn() __asm__("__ZNK19RndAmbientOcclusion13BurnTransformEP7RndMeshRNSt3__14listIS1_NS2_9allocatorIS1_EEEE");
extern "C" long _stub_apple_rndao_burn() { return 0; }

// kdTree<Triangle>::kdTreeNode::FindSplit_SAH(const Box&, const std::list<Triangle*>&)
extern "C" __attribute__((weak, used)) long _stub_apple_kdtree_split() __asm__("__ZN6kdTreeI8TriangleE10kdTreeNode13FindSplit_SAHERK3BoxRKNSt3__14listIPS0_NS6_9allocatorIS8_EEEE");
extern "C" long _stub_apple_kdtree_split() { return 0; }

// --- std::vector parameter stubs (libc++ ABI) ---

// PartyModeMgr::DetermineSubModePlayers(Symbol, int*, int*, std::vector<int>*)
extern "C" __attribute__((weak, used)) long _stub_apple_partymode_submode() __asm__("__ZN12PartyModeMgr23DetermineSubModePlayersE6SymbolPiS1_PNSt3__16vectorIiNS2_9allocatorIiEEEE");
extern "C" long _stub_apple_partymode_submode() { return 0; }

// MoveDir::EnqueueDetectFrames(float, int, std::vector<DetectFrame>&, const FilterVersion*)
extern "C" __attribute__((weak, used)) long _stub_apple_movedir_enqueue() __asm__("__ZN7MoveDir19EnqueueDetectFramesEfiRNSt3__16vectorI11DetectFrameNS0_9allocatorIS2_EEEEPK13FilterVersion");
extern "C" long _stub_apple_movedir_enqueue() { return 0; }

// HamStorePanel::GetOfferIDsToEnumerate(std::vector<unsigned long long>&, bool) const
extern "C" __attribute__((weak, used)) long _stub_apple_hamstore_getoffers() __asm__("__ZNK13HamStorePanel22GetOfferIDsToEnumerateERNSt3__16vectorIyNS0_9allocatorIyEEEEb");
extern "C" long _stub_apple_hamstore_getoffers() { return 0; }

// --- Completely missing stubs (libc++ ABI) ---

// Hmx::Matrix4::Col3(int) const
extern "C" __attribute__((weak, used)) long _stub_apple_matrix4_col3() __asm__("__ZNK3Hmx7Matrix44Col3Ei");
extern "C" long _stub_apple_matrix4_col3() { return 0; }

// ScaleAddEq(Transform&, const Transform&, float)
extern "C" __attribute__((weak, used)) long _stub_apple_scaleaddeq() __asm__("__Z10ScaleAddEqR9TransformRKS_f");
extern "C" long _stub_apple_scaleaddeq() { return 0; }

// RecursePatternInternal(const char*, void(*)(const char*, const char*), bool, bool)
extern "C" __attribute__((weak, used)) long _stub_apple_recursepattern() __asm__("__Z22RecursePatternInternalPKcPFvS0_S0_Ebb");
extern "C" long _stub_apple_recursepattern() { return 0; }

// Waypoint::sWaypoints (static member, std::list<Waypoint*>)
extern "C" __attribute__((weak, used)) long _stub_apple_waypoint_swaypoints __asm__("__ZN8Waypoint10sWaypointsE");
long _stub_apple_waypoint_swaypoints = 0;

#endif // __APPLE__

#endif // !__EMSCRIPTEN__

// LabelShrinkWrapper::Poll — moved to LabelShrinkWrapper.cpp
