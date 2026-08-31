// Link glue: provides ICF-merged function definitions that are missing from
// split objects. The original linker folded these with identical functions via
// Identical COMDAT Folding (ICF), so they don't exist as separate symbols in
// any split .obj file. Our decomp source defines them (MemMgr.cpp, DataArray.cpp)
// but those units are NonMatching, so the split objects are used instead.
//
// This file also provides stub definitions for unresolved link symbols from
// third-party libraries (libjpeg, zlib, vorbis, curl, etc.) and Xbox SDK
// functions that are not part of the decomp scope.

#include "obj\Data.h"
#include "os\Debug.h"
#include "os\HDCache.h"
#include "utl\MemMgr.h"
#include "utl\PoolAlloc.h"
#include "synth\Faders.h"
#include "rndobj\Lit.h"
#include "world\Spotlight.h"
#include "char\Waypoint.h"
#include "rndobj\Wind.h"
#include "rndobj\Dir.h"
#include "ui\UIListDir.h"
#include "ui\UILabelDir.h"
#include "hamobj\HamListRibbon.h"
#include "char\CharPollable.h"
#include "char\CharWeightSetter.h"
#include "flow\FlowNode.h"
#include "rndobj\CamAnim.h"

// ============================================================================
// ICF-merged function definitions
// ============================================================================

void operator delete(void *v) { MemFree(v, "unknown", 0, "unknown"); }
void operator delete[](void *v) { MemFree(v, "unknown", 0, "unknown"); }

// HDCache::Flush was defined in a separate TU from HDCache.cpp in the original
// build, so the compiler couldn't see the empty body and emitted bl calls.
// Keeping it here preserves that behavior for matching codegen.
void HDCache::Flush() {}

// (DataArray::Node removed — obj/DataArray is Matching)

void MemOrPoolFreeSTL(
    int poolIdx, void *mem, const char *file, int line, const char *name
) {
    if (mem) {
        if (poolIdx > 0x80) {
            MemFree(mem, file, line, name);
        } else {
            PoolFree(poolIdx, mem, file, line, name);
        }
    }
}

// ============================================================================
// Third-party C library stubs (extern "C")
// ============================================================================

extern "C" {

// -- Ogg/Vorbis --
// OggFree is now defined in src/system/oggvorbis/VorbisMem.cpp (Xbox/PPC body);
// keeping a stub here would cause a duplicate definition at link time.
void _vp_global_look(void) {}
float _vp_ampmax_decay(float a, void *) { return 0; }
void vorbis_lpc_predict(float *, float *, int) {}
void vorbis_lpc_from_data(float *, float *, int, int) {}

// -- zlib --
void zcfree(void *, void *) {}
void _tr_stored_block(void *, void *, unsigned long, int) {}
unsigned long compressBound(unsigned long sourceLen) {
    return sourceLen + (sourceLen >> 12) + (sourceLen >> 14) + (sourceLen >> 25) + 13;
}

// -- CRT --
int vsnprintf(char *buf, unsigned int count, const char *fmt, char *args) { return 0; }
int stricmp(const char *, const char *) { return 0; }
int _strnicmp(const char *, const char *, unsigned int) { return 0; }
int strnicmp(const char *, const char *, int) { return 0; }
char *itoa(int, char *, int) { return 0; }
long long _64time(long *) { return 0; }
// wmemcpy: absent from ham_xbox_r.map (the original inlined it), but SpeechMgr
// instantiates basic_string<wchar_t>, and STLport's char_traits<wchar_t>::copy
// is a straight `return wmemcpy(...)`.  Aliased to __link_glue_noop, every
// wide-string copy, assign and range-construct in SpeechMgr silently copied
// nothing and returned its own destination pointer, so the caller could not
// tell.  A real body, not a stub: this one has a correct answer.
wchar_t *wmemcpy(wchar_t *dest, const wchar_t *src, unsigned int count) {
    return (wchar_t *)memcpy(dest, src, count * sizeof(wchar_t));
}

struct _stati64_s {
    char _pad[128];
};
int _stati64(const char *, struct _stati64_s *) { return -1; }

// -- Winsock --
void *gethostbyname(const char *) { return 0; }

// -- libcurl --
int curlx_sltosi(long) { return 0; }
unsigned short curlx_sltous(long) { return 0; }
unsigned int curlx_sltoui(long) { return 0; }
unsigned short curlx_ultous(unsigned long) { return 0; }
int curlx_uztosz(unsigned int) { return 0; }
int Curl_multi_canPipeline(void *) { return 0; }
void *Curl_str2addr(const char *) { return 0; }
// -1, not 0.  ham_xbox_r.map has Curl_gethostname at 0x8261A3C0, whose body is
// `li r3,-0x1; blr` -- the `#ifndef HAVE_GETHOSTNAME` arm of the real
// src/system/net/curl/lib/curl_gethostname.c, which this stub stands in for
// (that TU is not in config/373307D9/objects.json).  Returning 0 claimed
// SUCCESS and left `name` uninitialised, so smtp.c and curl_ntlm_msgs.c used a
// garbage local hostname instead of taking their failure path.
int Curl_gethostname(char *, int) { return -1; }
char *curl_getenv(const char *) { return 0; }
// Same shape as Curl_gethostname, and it was aliased to __link_glue_noop.
// ham_xbox_r.map puts Curl_if2ip at 0x82AEAE70, whose body is `li r3,0; blr`
// -- the `#else` arm of if2ip.c, i.e. "this platform cannot enumerate
// interfaces".  On PPC a no-op leaf is a bare `blr`, so the caller got r3
// back unchanged -- the *first argument*, `af`.  connect.c:268 does
// `if(Curl_if2ip(af, dev, myhost, sizeof(myhost)))`, and AF_INET is 2, so the
// test was not merely wrong, it was inverted: every call reported success and
// handed curl the uninitialised stack buffer `myhost` as an interface address.
char *Curl_if2ip(int, const char *, char *, int) { return 0; }

} // extern "C"

// ============================================================================
// JPEG memory manager stubs (C++ mangled)
// These are custom memory allocation hooks for libjpeg that the game provides.
// ============================================================================

struct jpeg_common_struct;

void jpeg_mem_term(jpeg_common_struct *) {}
void *jpeg_get_small(jpeg_common_struct *, unsigned int sz) { return malloc(sz); }
void *jpeg_get_large(jpeg_common_struct *, unsigned int sz) { return malloc(sz); }
void jpeg_free_small(jpeg_common_struct *, void *ptr, unsigned int) { free(ptr); }
void jpeg_free_large(jpeg_common_struct *, void *ptr, unsigned int) { free(ptr); }
long jpeg_mem_init(jpeg_common_struct *) { return 0; }
long jpeg_mem_available(jpeg_common_struct *, long, long max_bytes, long) {
    return max_bytes;
}

// ============================================================================
// Xbox SDK stubs (C++ mangled)
// ============================================================================

void WaitForSingleObject(int, int) {}
void CloseHandle(int) {}
int XNetDnsLookup(int, int, void *) { return 0; }
int XNetDnsRelease(void *) { return 0; }
int WSACreateEvent() { return 0; }

// ============================================================================
// Decomp member function stubs
// These are functions whose definitions are needed by the linker but whose
// translation units are not yet decomped or are NonMatching split objects.
// ============================================================================

// (String constructors removed — utl/Str is Matching)

// -- FormatString --
#include "utl\MakeString.h"

FormatString &FormatString::operator<<(float) { return *this; }
FormatString &FormatString::operator<<(long) { return *this; }
FormatString &FormatString::operator<<(unsigned int) { return *this; }
FormatString &FormatString::operator<<(unsigned long long) { return *this; }

// -- ObjectDir --
#include "obj\Dir.h"

// -- PanelDir --
#include "ui\PanelDir.h"

// -- UIComponent --
#include "ui\UIComponent.h"

// -- UIList --
#include "ui\UIList.h"

// -- BufStream --
// Still needed: virtual method not exported from decomp .obj, referenced by other split
// .objs
#include "utl\BufStream.h"

// (ObjPtrList/ObjRefConcrete/BinStream template specializations removed —
// generic templates in ObjPtr_p.h now provide all needed instantiations via COMDAT)

// ============================================================================
// Headers still needed for COMDAT instantiation by the compiler
// (including these headers causes the compiler to emit needed template COMDATs)
// ============================================================================

#include "meta_ham\VenueProvider.h"
#include "synth\WavMgr.h"
#include "meta\Achievements.h"
#include "meta\SongMetadata.h"

// (ObjRefConcrete::CopyRef and BinStream operator<< specializations removed —
// provided by COMDAT from real TUs via ObjPtr_p.h generic templates)

// ============================================================================
// Missing accessor/method stubs for Matching unit resolution
// ============================================================================

#include "meta\Profile.h"
#include "meta_ham\AccomplishmentProgress.h"
#include "meta_ham\CampaignEra.h"
#include "meta_ham\AccomplishmentGroup.h"
#include "meta_ham\Award.h"
#include "meta\MetaMusicManager.h"
#include "meta\MetaMusicScene.h"
#include "os\PlatformMgr.h"

// -- CampaignEra stubs --

// -- PlatformMgr stubs --
// (PlatformMgr::DisableXMP removed — os/PlatformMgr_Xbox is Matching)

// -- AccomplishmentProgress stubs --

// -- AccomplishmentGroup stubs --

// -- Award stubs --

// -- ProfileMgr stubs --
#include "meta_ham\ProfileMgr.h"

// -- CharServoBone stubs --
#include "char\CharServoBone.h"

// -- CharBonesMeshes stubs (vtordisp thunk needs this) --
#include "char\CharBonesMeshes.h"

// -- AppLabel stubs --
#include "meta_ham\AppLabel.h"

// ============================================================================
// Round 2: Additional stubs for 55 more Matching units
// ============================================================================

// -- FormatString stubs --
#include "utl\MakeString.h"
FormatString &FormatString::operator<<(void *) { return *this; }
FormatString &FormatString::operator<<(unsigned long) { return *this; }

// -- DebugNotifyOncePrinter global --
DebugNotifyOncePrinter TheDebugNotifyOncePrinter;

// -- NavListSortMgr stubs --
#include "meta_ham\NavListSortMgr.h"

// -- ObjRefConcrete template stubs --

#include "rndobj\Wind.h"

// -- ObjPtrVec<RndTransformable> stubs --

// (ObjPtrVec Node::RefOwner and erase template specializations removed —
// provided by generic templates in ObjPtr_p.h)

// -- CharDriver stubs --
#include "char\CharDriver.h"

// -- SongMetadata stubs --
#include "meta\SongMetadata.h"

// -- CacheMgr stubs --
#include "utl\CacheMgr.h"

// (PracticeSection::Steps removed — hamobj/PracticeSection is Matching)

// -- GestureMgr stubs --
#include "gesture\GestureMgr.h"

// -- CampaignProgress stubs --
#include "meta_ham\CampaignProgress.h"

// -- CampaignEra stubs (round 2) --

// -- UIListMesh stubs --
#include "ui\UIListMesh.h"

// -- Hmx::Object stubs --

// -- Award stubs (round 2) --

// -- FaderGroup stubs --
#include "synth\Faders.h"

// -- UIList stubs --
#include "ui\UIList.h"

// -- NetCacheMgr stubs --
#include "utl\NetCacheMgr.h"

// -- NetCacheMgrXbox stubs --
#include "utl\NetCacheMgr_Xbox.h"

// -- Round 3 stubs --

// ObjRefConcrete::CopyRef for RndParticleSys (PartLauncher.obj)
#include "rndobj\Part.h"

// ObjRefConcrete::CopyRef for CharBone (CharBone.obj)
#include "char\CharBone.h"

// FileLoader::GetSize (FileCache.obj, NetLoader.obj)
#include "utl/Loader.h"

// NavListHeaderNode::Handle (MQSongSortNode.obj)
#include "meta_ham\NavListNode.h"

// BinStream operator<< for ObjOwnerPtr<RndTransAnim> (TransAnim.obj)
#include "rndobj\TransAnim.h"

// BinStream operator<< for ObjDirPtr<RndDir> (UISlider.obj)
#include "obj\Dir.h"
#include "rndobj\Rnd.h"

// BinStream operator<< for ObjPtrList<CharPollable> (CharPollGroup.obj - if needed later)
// wmemcpy (SpeechMgr.obj - CRT function, needs library)

// -- HolmesClientPrint stub (ArkFile.obj) --
#include "os\HolmesClient.h"
void HolmesClientPrint(const char *) {}

// -- MemOrPoolFree stub (Str.obj) --
#include "utl\MemMgr.h"
void MemOrPoolFree(int, void *mem, const char *, int, const char *) {}

// (ObjPtrList Node::RefOwner specializations removed — provided by generic templates)

// EaseLinear standalone instantiation (inline in Easing.h, needs out-of-line symbol)
// Including Easing.h emits gEaseFuncs[] which takes &EaseLinear, forcing out-of-line copy
#include "math/Easing.h"

// ObjDirPtr<ObjectDir>::IsLoaded

// ============================================================================
// Linker stubs for compiler-generated symbols missing from split objects
// These are unresolved because we skip split objects for Matching units,
// and these compiler-generated symbols have no decomp-source equivalent.
// ============================================================================

// Noop function target for ALTERNATENAME redirects
extern "C" void __link_glue_noop(void) {}

// floor0_ stubs (Ogg Vorbis, not in decomp scope)
extern "C" void floor0_free_info(void) {}
extern "C" void floor0_free_look(void) {}
extern "C" void floor0_inverse1(void) {}
extern "C" void floor0_inverse2(void) {}
extern "C" void floor0_look(void) {}
extern "C" void floor0_unpack(void) {}

// lbl_ data stubs: now resolved by create_data_stubs.py, removed from here

// Data stubs for vtable/static data ALTERNATENAME redirects
extern "C" int __link_glue_zero[64] = { 0 };
extern "C" const char __link_glue_empty_str[] = "";

// Remaining unresolved symbols from Matching unit decomp-only linking.
// Removed: NewBufStream@Synth — already implemented in Synth.cpp (matching unit)
// Removed: ~CriticalSection — implemented in CritSec.cpp (matching unit)
// Removed: Terminate@UILabel — implemented in UILabel.cpp
// Removed: gCheatsManager — defined in Cheats.cpp (matching unit)

// ============================================================================
// Auto-generated stubs for symbols lost when units promoted to Matching
// Generated from link errors after 339 units promoted via sync_match_percent.py
// ============================================================================

// -- ObjPtr/ObjRef template instantiations --
// merged_ObjPtrListPopBack is an invented name (it appears in no config file
// and in no map); PostProc_NG.cpp declares it and calls it inside
// NgPostProc::DoVelocity's drain loop.  Bound to __link_glue_noop that loop
// HANGS: it re-reads the list head from the same address every iteration and
// the no-op never advances it, so a non-empty mMotionBlurDrawList spins
// forever.  The shipped image calls a real function there --
// build/373307D9/asm/system/rndobj/PostProc_NG.s, 0x826AB720:
//     bl "?pop_back@?$ObjPtrList@VRndMesh@@VObjectDir@@@@QAAXXZ"
// -- which our link inputs already define (map: 0x823912C8), same
// this-in-r3 signature.  Point the alias at it.
#pragma comment(                                                                         \
    linker,                                                                              \
    "/ALTERNATENAME:?merged_ObjPtrListPopBack@@YAXPAX@Z="                                \
    "?pop_back@?$ObjPtrList@VRndMesh@@VObjectDir@@@@QAAXXZ"                              \
)

// -- BinStream operators --
// Removed: operator>>(BinStream&, FlowTrigger::PropTriggerDefn&) — implemented in
// FlowTrigger.cpp (matching unit) Removed: PostLoad@HamDriver — implemented in
// HamDriver.cpp (matching unit)

// -- Data symbols --
#pragma comment(linker, "/ALTERNATENAME:?lbl_82F1AB98@@3IA=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:?lbl_82F1AB9C@@3IA=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:?lbl_82F1ABA0@@3IA=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:?lbl_82F5E180@@3JC=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:?DAT_82f63aec@@3HA=__link_glue_zero")
#pragma comment(                                                                                 \
    linker,                                                                                      \
    "/ALTERNATENAME:?DAT_82f63ae4@@3V?DingoJobCompleteMsg@?1???R@Hamac@@QAE@XZ=__link_glue_zero" \
)
#pragma comment(                                                                                 \
    linker,                                                                                      \
    "/ALTERNATENAME:?DAT_82f63ae8@@3V?DingoJobCompleteMsg@?1???R@Hamac@@QAE@XZ=__link_glue_zero" \
)
#pragma comment(                                                                                 \
    linker,                                                                                      \
    "/ALTERNATENAME:?DAT_82f63adc@@3V?DingoJobCompleteMsg@?1???R@Hamac@@QAE@XZ=__link_glue_zero" \
)
#pragma comment(                                                                                 \
    linker,                                                                                      \
    "/ALTERNATENAME:?DAT_82f63ae0@@3V?DingoJobCompleteMsg@?1???R@Hamac@@QAE@XZ=__link_glue_zero" \
)
// Removed: sLoadingMaster@LoadingPanel — defined in LoadingPanel.cpp (matching unit)
// Removed: sSongDB@LoadingPanel — defined in LoadingPanel.cpp (matching unit)
#pragma comment(                                                                         \
    linker, "/ALTERNATENAME:?tf2cf@RndRenderState@@2PAW4_D3DCMPFUNC@@A=__link_glue_zero" \
)

// -- Other functions --
// Removed: LocationCmp::LocationCmp — implemented in SongSortByLocation.cpp (matching
// unit) Removed: ~DifficultyCmp — implemented in SongSortByDiff.cpp (matching unit)
// Removed: ~MQSongSortNode — implemented in MQSongSortNode.cpp (matching unit)
// Removed: ~SongCmp — implemented in SongSortBySong.cpp (matching unit)
// Removed: SortCmp::operator() — implemented in StoreOffer.cpp (matching unit)
// Removed: DrawFixedZ@DrawString — implemented in Graph.cpp
// Removed: DrawShowing@SpotlightDrawer — implemented in SpotlightDrawer.cpp (matching
// unit) Removed: GetBufferSize@HttpGet — implemented in HttpGet.cpp Removed:
// GetColor@UIColor — implemented in UIColor.cpp (matching unit) Removed:
// GetNumRestarts@Game — implemented in Game.cpp Removed: GetSlipOffset@StreamReceiverFile
// — implemented in StreamReceiverFile.cpp Removed: Highlight@Waypoint — implemented in
// Waypoint.cpp Removed: OnSelect@NgPostProc — implemented in PostProc_NG.cpp (matching
// unit) Removed: OnSync@RndMesh — implemented in Mesh.cpp (matching unit) Removed:
// OnUnselect@NgPostProc — implemented in PostProc_NG.cpp (matching unit) Removed:
// PresyncBitmap@RndTex — implemented in Tex.cpp (matching unit) Removed: SpewInit —
// implemented in Spew.cpp Removed: SpewTerminate — implemented in Spew.cpp Removed:
// SyncBitmap@RndTex — implemented in Tex.cpp (matching unit) Removed: TerminateMakeString
// — implemented in MakeString.cpp Removed: ValidateCRC@CRC@Hmx — implemented in Crc.cpp
// Removed: Flush@HDCache — implemented in HDCache.cpp (matching unit)
// Removed: Handle@BustAMoveData — implemented in BustAMoveData.cpp (matching unit)
// Removed: Handle@OvershellSlot — implemented in Overshell.cpp (matching unit)
// Removed: InsertBreak@RndConsole — implemented in Console.cpp (matching unit)
// Removed: IsDifficultyUnlockedForProfile@HamProfile — implemented in HamProfile.cpp
// (matching unit) Removed: JointToVertexData — implemented in DepthBuffer3D.cpp (matching
// unit) Removed: OnMsg@HamUI — implemented in HamUI.cpp (matching unit) Removed:
// RemoveFromLists@Spotlight — implemented in Spotlight.cpp (matching unit) Removed:
// VertexToWorld — implemented in DepthBuffer3D.cpp (matching unit)
// Removed: altCfg — was most vexing parse in Locale.cpp (now fixed, no function
// reference)
#pragma comment(linker, "/ALTERNATENAME:?merged_82610090@@YAPBDPBDPCH@Z=__link_glue_noop")

// -- BinStream operator<< template instantiations --

// -- C runtime / third-party library symbols --
// Removed: HIBYTE / LOBYTE / MAKEWORD -- now macros in xdk/win_types.h, so
// nothing references them.  Removed: htons / ntohs -- now identity macros in
// xdk/xnet/winsockx.h (Xenon is big-endian and the shipped image contains no
// htons/ntohs symbol).  Removed: RtlDeleteCriticalSection -- now an inline
// no-op in xdk/XBOXKRNL.h.  If any of those five ever comes back as an
// unresolved external, the header fix regressed; do not re-add the alias.
//
// `read` and `_close` are real LIBCMT bodies in the shipped image, and the
// split objects supply them under their ICF-canonical spellings (_read at
// 0x829A8550, close at 0x829A65F8 -- same address, same object).  Point the
// aliases at those instead of at a no-op: a no-op `read` returns its own fd
// and leaves the caller's buffer untouched.
#pragma comment(linker, "/ALTERNATENAME:_fstati64=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:read=_read")
#pragma comment(linker, "/ALTERNATENAME:strncasecmp=__link_glue_noop")

// -- BinStream operator<< non-template targets (decomp compiler ALTERNATENAME chains) --

// -- Dynamic initializers (??__E) needed by auto_08_82F05C00_data.obj --
// These ??__E symbols are referenced from the CRT __xc_a section but their
// defining TUs are NonMatching split objects that lack the definitions.
#pragma comment(                                                                                                                                                        \
    linker,                                                                                                                                                             \
    "/ALTERNATENAME:??__E?mAssocMicXbox@ExternalMicClientMgr@@0V?$vector@PAVMicXbox@@V?$StlNodeAlloc@PAVMicXbox@@@stlpmtx_std@@@stlpmtx_std@@A@@YAXXZ=__link_glue_noop" \
)
#pragma comment(                                                                                                                                    \
    linker,                                                                                                                                         \
    "/ALTERNATENAME:??__E?mDevToMicMaster@ExternalMicClientMgr@@0V?$vector@KV?$StlNodeAlloc@K@stlpmtx_std@@@stlpmtx_std@@A@@YAXXZ=__link_glue_noop" \
)
#pragma comment(                                                                                                                                    \
    linker,                                                                                                                                         \
    "/ALTERNATENAME:??__E?mMicMasterToDev@ExternalMicClientMgr@@0V?$vector@KV?$StlNodeAlloc@K@stlpmtx_std@@@stlpmtx_std@@A@@YAXXZ=__link_glue_noop" \
)
#pragma comment(                                                                                                                                                                                    \
    linker,                                                                                                                                                                                         \
    "/ALTERNATENAME:??__E?mMicMasters@ExternalMicClientMgr@@0V?$vector@PAVExternalMicClientProxy@@V?$StlNodeAlloc@PAVExternalMicClientProxy@@@stlpmtx_std@@@stlpmtx_std@@A@@YAXXZ=__link_glue_noop" \
)
#pragma comment(linker, "/ALTERNATENAME:??__EgInput@?A0x49b544a7@@YAXXZ=__link_glue_noop")
#pragma comment(                                                                         \
    linker, "/ALTERNATENAME:??__EgJoypadData@?A0xca10770b@@YAXXZ=__link_glue_noop"       \
)
#pragma comment(linker, "/ALTERNATENAME:??__EgMics@?A0x0c39da7f@@YAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??__Es_voiceGC@@YAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??__Es_voiceGCInProgress@@YAXXZ=__link_glue_noop")
// sFlipYZ: removed stub — Cam.cpp now has unconditional static initializer
// Removed: ??__EsIdentityXfm — Env_NG.cpp now has static initializer
#pragma comment(linker, "/ALTERNATENAME:except_data_82918780=__link_glue_zero")

// ============================================================================
// Additional stubs for remaining unresolved symbols
// Generated from link error analysis
// ============================================================================

// -- Dynamic initializers (76 symbols) --
#pragma comment(linker, "/ALTERNATENAME:??__EgCrit@@YAXXZ=__link_glue_noop")
// Removed: ??__EgChildPolys — symbol exists in matching Utl.obj
// Removed: ??__EgParentPolys — symbol exists in matching Utl.obj
// Removed: ??__EgPhysicsVolumeBox — PhysicsVolume.cpp now has static initializer
#pragma comment(                                                                         \
    linker,                                                                              \
    "/ALTERNATENAME:??__EmFriendEnumRequests@?A0x8a9ffbf2@@YAXXZ=__link_glue_noop"       \
)
#pragma comment(                                                                         \
    linker, "/ALTERNATENAME:??__EmServiceIdMap@?A0x8a9ffbf2@@YAXXZ=__link_glue_noop"     \
)
#pragma comment(linker, "/ALTERNATENAME:??__EmTime@?A0x8a9ffbf2@@YAXXZ=__link_glue_noop")
// Removed: ??__EsOverlayWidth — MoveDir.cpp now has static initializer
// Removed: ??__EsSuperClassMap — Dir.cpp now has static initializer

// -- Audio SDK (11 symbols) --

// -- String COMDATs (12 symbols) --

// -- Data labels (123 symbols) --

// -- Float constants (6 symbols) --

// -- Exception/unwind data (4 symbols) --

// -- STL template instantiations (28 symbols) --

// -- MakeString instantiations (4 symbols) --

// -- ObjPtr/ObjPtrVec template instantiations (72 symbols) --

// -- ObjRef/ObjDirPtr template instantiations (11 symbols) --

// -- BinStream operator instantiations (1 symbols) --

// -- Game/engine data symbols (17 symbols) --

// -- Game/engine function stubs (372 symbols) --
// Removed: DataOwner@RndFont3d — implemented in Font3d.cpp (matching unit)
// Removed: ExitStore@StorePanel — implemented in StorePanel.cpp (matching unit)
// Removed: GetFailType@NetCacheLoader — implemented in NetCacheLoader.cpp
// Removed: GetJumpBackTotalTime@StandardStream — implemented in StandardStream.cpp
// (matching unit) Removed: GetName@MicXbox — implemented in Mic.cpp Removed:
// Handle@FitnessCalorieSortMgr — implemented in FitnessCalorieSortMgr.cpp (matching unit)
// Removed: Handle@RndFont3d — implemented in Font3d.cpp (matching unit)
// Removed: Load@SynthSample — implemented in SynthSample.cpp (matching unit)
// Removed: Mat@RndFont3d — implemented in Font3d.cpp (matching unit)
// Removed: NewHeaderNode(2-arg)@ChallengeSortByScore — implemented in
// ChallengeSortByScore.cpp (matching unit) Removed:
// NewHeaderNode(2-arg)@FitnessCalorieSortByCalorie — implemented in
// FitnessCalorieSortByCalorie.cpp (matching unit) Removed:
// NewHeaderNode(2-arg)@MQSongSortByCharacter — implemented in MQSongSortByCharacter.cpp
// (matching unit) Removed: NewHeaderNode(2-arg)@SongSortByLocation — implemented in
// SongSortByLocation.cpp (matching unit) Removed: OldResourcePreload@LabelShrinkWrapper —
// implemented in LabelShrinkWrapper.cpp (matching unit) Removed:
// OnParametersChanged@FxSendFlanger360 — implemented in FxSendFlanger.cpp Removed:
// OnSync@DxMesh — implemented in rnddx9/Mesh.cpp (matching unit) Removed:
// Poll@LabelShrinkWrapper — implemented in LabelShrinkWrapper.cpp (matching unit)
// Removed: Poll@RandomIntervalGroupSeqInst — implemented in Sequence.cpp (matching unit)
// Removed: Select@ChallengeHeaderNode — implemented in ChallengeSortNode.cpp (matching
// unit) Removed: Set@NgDOFProc — implemented in DOFProc_NG.cpp (matching unit) Removed:
// SetPaused@BinkMovieImpl — implemented in BinkMovieImpl.cpp (matching unit) Removed:
// SetVConstant(float*)@DxShaderMgr — implemented in ShaderMgr.cpp (matching unit)
// Removed: StartImpl@RandomIntervalGroupSeqInst — implemented in Sequence.cpp (matching
// unit) Removed: StoreProfile@StorePanel — implemented in StorePanel.cpp (matching unit)
// Removed: SyncBitmap@DxTex — implemented in rnddx9/Tex.cpp (matching unit)
// Removed: UpdateApproxLighting@RndEnviron — implemented in Env.cpp (matching unit)

// -- Remaining symbols missed due to substring overlap with ??__E entries --
// -- Template instantiations (46 symbols) --

// -- Game/engine data (810 symbols) --
// Removed: gDebugDepth — defined in LiveCameraInput.cpp
#pragma comment(linker, "/ALTERNATENAME:?lbl_82F14008@@3HA=__link_glue_zero")
// Removed: sHamMaster@MetaPanel — defined in MetaPanel.cpp (matching unit)
// Removed: sSongDB@MetaPanel — defined in MetaPanel.cpp (matching unit)

// -- Game/engine functions (535 symbols) --
#pragma comment(linker, "/ALTERNATENAME:??0CXAPOBase@ATG@@QAA@XZ=__link_glue_noop")
#pragma comment(                                                                         \
    linker, "/ALTERNATENAME:??0CXAPOParametersBase@ATG@@QAA@PBXPAXIE@Z=__link_glue_noop" \
)
#pragma comment(linker, "/ALTERNATENAME:??0ID3DXInclude@@QAA@XZ=__link_glue_noop")
// Removed: ~AppLabel — implemented in AppLabel.cpp (matching unit)
// Removed: ~FitnessCalorieSortByCalorie — implemented in FitnessCalorieSortByCalorie.cpp
// (matching unit) Removed: ~FitnessCalorieSortCmp — implemented in
// FitnessCalorieSortByCalorie.cpp (matching unit) Removed: ~MQSongSortByCharacter —
// implemented in MQSongSortByCharacter.cpp (matching unit)
#pragma comment(                                                                         \
    linker, "/ALTERNATENAME:??1PeakDetector@Synapse@DSP@@QAA@XZ=__link_glue_noop"        \
)
#pragma comment(                                                                         \
    linker, "/ALTERNATENAME:??1PitchCorrectedVoice@Synapse@DSP@@QAA@XZ=__link_glue_noop" \
)
#pragma comment(linker, "/ALTERNATENAME:?BinkClose@@YAXPAUBINK@@@Z=__link_glue_noop")
#pragma comment(                                                                         \
    linker, "/ALTERNATENAME:?BinkCloseTrack@@YAXPAUBINKTRACK@@@Z=__link_glue_noop"       \
)
#pragma comment(                                                                         \
    linker, "/ALTERNATENAME:?BinkGetTrackData@@YAIPAUBINKTRACK@@PAX@Z=__link_glue_noop"  \
)
#pragma comment(linker, "/ALTERNATENAME:?BinkNextFrame@@YAXPAUBINK@@@Z=__link_glue_noop")
#pragma comment(                                                                         \
    linker,                                                                              \
    "/ALTERNATENAME:?BinkOpenTrack@@YAPAUBINKTRACK@@PAUBINK@@E@Z=__link_glue_noop"       \
)
#pragma comment(                                                                         \
    linker, "/ALTERNATENAME:?BinkSetMemory@@YAXP6APAXH@ZP6AXPAX@Z@Z=__link_glue_noop"    \
)
#pragma comment(linker, "/ALTERNATENAME:?BinkStartAsyncThread@@YAHHH@Z=__link_glue_noop")
// Removed: GetLastResult@Cache — implemented in Cache.cpp
// Removed: Intersect(Segment,Triangle,int,float&) — fixed signature and already in
// Geo.cpp (matching unit) Removed: OnSmartGlassListen@FitnessGoalMgr — implemented in
// FitnessGoalMgr.cpp (matching unit) Removed: PreSave@WorldInstance — implemented in
// Instance.cpp (matching unit)
#pragma comment(linker, "/ALTERNATENAME:?RadAlloc@@YAPAXH@Z=__link_glue_noop")
#pragma comment(                                                                                    \
    linker,                                                                                         \
    "/ALTERNATENAME:?SetReleaseSmoothing@PitchCorrectedVoice@Synapse@DSP@@QAAXM@Z=__link_glue_noop" \
)
// Removed: UpdateGestures@HamNavList — implemented in HamNavList.cpp (matching unit)
#pragma comment(                                                                                                      \
    linker,                                                                                                           \
    "/ALTERNATENAME:?__pop_heap_aux@stlpmtx_std@@YAXPAUMemDiffEntry@@0HU?$less@UMemDiffEntry@@@1@@Z=__link_glue_noop" \
)
// Removed: dispose@Voice — implemented in Voice.cpp (matching unit)
// Removed: kStreamEndMs@StandardStream — defined in StandardStream.cpp (matching unit)
#pragma comment(linker, "/ALTERNATENAME:?merged_82610090@@YAPAXPBXPAI@Z=__link_glue_noop")
// Removed: CopyRef@ObjRefConcrete<Hmx::Object,ObjectDir> — explicit instantiation in
// link_glue.cpp Removed: IsLoaded@ObjDirPtr<ObjectDir> — explicit instantiation in
// link_glue.cpp Removed: RefOwner@ObjPtrList<FlowNode> — explicit instantiation in
// link_glue.cpp Removed: Node::RefOwner for CharPollable, CharWeightSetter,
// CharWeightable, Fader,
//          NoteVoiceInst, ObjectDir, RndLight, SeqInst, SfxInst, ThreeDSound,
//          Waypoint, WorldCrowd — explicit instantiations in link_glue.cpp
#pragma comment(                                                                            \
    linker,                                                                                 \
    "/ALTERNATENAME:?copy@?$char_traits@_W@stlpmtx_std@@SAPA_WPA_WPB_WI@Z=__link_glue_noop" \
)
// ---------------------------------------------------------------------------
// The createFilter shape: an UNMANGLED alternatename standing in for a symbol
// the image spells C++-MANGLED.  An entry like this cannot ever be satisfied by
// the real function -- it silently redirects the reference to a no-op, and
// objdiff's normalized ruler charges nothing for it.  Audited 2026-08-20
// against orig/373307D9/ham_xbox_r.map; six of the thirty unmangled entries
// were of this shape, and all six now have a C++-linkage declaration in the
// tree, so the entries below are dead.  Deleted rather than left as traps:
//     BinkInit        -> ?BinkInit@@YAXXZ           utl:BinkIntegration.obj
//     D3DXSetDXT3DXT5 -> ?D3DXSetDXT3DXT5@@YAXH@Z   d3dx9:d3dx9tex.obj
//     FFTRealForward  -> ?FFTRealForward@@YAHPAMK0@Z synth_xbox:FFT.obj
//     cexp            -> ?cexp@@YA?AUcomplex@@U1@@Z  synth:complex.obj
//     expj            -> ?expj@@YA?AUcomplex@@N@Z    synth:complex.obj
//     expand          -> ?expand@@YAXQAUcomplex@@H0@Z synth:filterdesign.obj
// The D3DTexture_* entries below are NOT of that shape -- the map carries them
// unmangled (d3d9i:texture.obj), so they are genuine C symbols.
// ...and they are not missing either.  All three ICF-fold in the shipped image
// with a sibling that the split d3d9i:texture.obj DOES define under the
// canonical name, so the real body was in the link the whole time and these
// three references were being answered with `blr`:
//     D3DTexture_GetLevelDesc  == D3DLineTexture_GetLevelDesc  @0x82B9BC88
//     D3DTexture_LockRect      == D3DLineTexture_LockRect      @0x82B9C018
//     D3DTexture_UnlockRect    == D3DCubeTexture_UnlockRect    @0x82B9BEC0
// A no-op D3DTexture_LockRect is worse than inert: RndTex::Lock and
// Rnd_Xbox's front-buffer readback pass a D3DLOCKED_RECT by address and then
// write pixels through rect.pBits, which the no-op leaves as whatever was on
// the stack.  Same shape for GetLevelDesc (Tex.cpp:258 reads the descriptor
// back).  Equality is by map address, not by name similarity.
#pragma comment(                                                                         \
    linker, "/ALTERNATENAME:D3DTexture_GetLevelDesc=D3DLineTexture_GetLevelDesc"         \
)
#pragma comment(linker, "/ALTERNATENAME:D3DTexture_LockRect=D3DLineTexture_LockRect")
#pragma comment(linker, "/ALTERNATENAME:D3DTexture_UnlockRect=D3DCubeTexture_UnlockRect")
// MSVC spells a literal-pool constant `__real@<hex>` / `__vmx@<hex>`; `@` is
// not a valid identifier character, so src/system/synth_xbox/FFT.cpp declares
// the VMX ones with `_`.  Pointing that spelling at __link_glue_noop meant
// `*(XMVECTOR *)__vmx_00000000000000000000000000000000` loaded the first 16
// bytes of a no-op FUNCTION BODY instead of a zero vector -- a live
// wrong-data bug of exactly the createFilter shape, and one the normalized
// ruler charges nothing for.  Redirect to the real .rdata symbols instead
// (config/373307D9/symbols.txt: 0x82142500, 0x822639A0, 0x822639B0).
#pragma comment(                                                                         \
    linker,                                                                              \
    "/ALTERNATENAME:__vmx_00000000000000000000000000000000="                             \
    "__vmx@00000000000000000000000000000000"                                             \
)
#pragma comment(                                                                         \
    linker,                                                                              \
    "/ALTERNATENAME:__vmx_3f800000bf8000003f800000bf800000="                             \
    "__vmx@3f800000bf8000003f800000bf800000"                                             \
)
#pragma comment(                                                                         \
    linker,                                                                              \
    "/ALTERNATENAME:__vmx_bf8000003f800000bf8000003f800000="                             \
    "__vmx@bf8000003f800000bf8000003f800000"                                             \
)
// The six `__real_<hex>` entries that stood here were dead: no translation unit
// in the tree references that spelling (only link_glue named them).  Removed.
// _close: same story as `read` above -- LIBCMT:close.obj defines `close` and
// `_close` at the same address (0x829A65F8), and the split object carries the
// `close` spelling, so the real body was already linked in.
#pragma comment(linker, "/ALTERNATENAME:_close=close")
#pragma comment(linker, "/ALTERNATENAME:hypot=__link_glue_noop")
// Removed: wmemcpy -- link_glue.cpp now defines it for real (see the CRT block
// near the top).  If it reappears as an unresolved external, that definition
// stopped matching xdk/LIBCMT/wchar.h's declaration.

// -- ICF fold-group aliases (2026-08-23) --
// Each alias target is the symbols.txt name that survived at the fold address;
// in the original image every name in the group IS that one body, so aliasing
// reproduces the original call graph exactly.  Verified against
// orig/373307D9/ham_xbox_r.map: the wanted name and the target name share the
// listed address.
// 0x82621910:
#pragma comment(linker, "/ALTERNATENAME:?Highlight@WorldReflection@@UAAXXZ=?Highlight@RndMesh@@UAAXXZ")
// 0x827118E0:
#pragma comment(                                                                         \
    linker,                                                                              \
    "/ALTERNATENAME:?SyncProperty@WorldCrowd3DCharHandle@@UAA_NAAVDataNode@@PAVDataArray@@HW4PropOp@@@Z=" \
    "?SyncProperty@RndMultiMeshProxy@@UAA_NAAVDataNode@@PAVDataArray@@HW4PropOp@@@Z"     \
)
// 0x82E2D4F8:
#pragma comment(                                                                         \
    linker,                                                                              \
    "/ALTERNATENAME:?AddRef@CXAPOParametersBase@@UAAKXZ=?AddRef@CAudioSRC@LEAPFX@@UAAKXZ" \
)
// 0x82E2D590:
#pragma comment(                                                                         \
    linker,                                                                              \
    "/ALTERNATENAME:?CalcInputFrames@CXAPOBase@@UAAII@Z="                                \
    "?HashKey@?$CSPHash@KPAVCRule@NUISPEECH@@@NUISPEECH@@MBAIK@Z"                        \
)
#pragma comment(                                                                         \
    linker,                                                                              \
    "/ALTERNATENAME:?CalcOutputFrames@CXAPOBase@@UAAII@Z="                               \
    "?HashKey@?$CSPHash@KPAVCRule@NUISPEECH@@@NUISPEECH@@MBAIK@Z"                        \
)
// 0x82AEAE70 (li r3,0; blr):
#pragma comment(                                                                         \
    linker,                                                                              \
    "/ALTERNATENAME:?Initialize@CXAPOBase@@UAAJPBXI@Z="                                  \
    "?SetEngine@CTrigramStore@NUISPEECH@@QAAJPAVCEngine@2@@Z"                            \
)
// 0x82E63A38:
#pragma comment(                                                                         \
    linker,                                                                              \
    "/ALTERNATENAME:?IsOutputFormatSupported@CXAPOBase@@UAAJPBUtWAVEFORMATEX@@0PAPAU2@@Z=" \
    "?IsInputFormatSupported@CXAPOBase@@UAAJPBUtWAVEFORMATEX@@0PAPAU2@@Z"                \
)
// 0x823E3B70 (OnlyReturns — the empty-body fold group):
#pragma comment(linker, "/ALTERNATENAME:?Reset@CXAPOBase@@UAAXXZ=OnlyReturns")
#pragma comment(linker, "/ALTERNATENAME:?SpewInit@@YAXXZ=OnlyReturns")
#pragma comment(linker, "/ALTERNATENAME:?SpewTerminate@@YAXXZ=OnlyReturns")
// 0x82B9BEC0 (Line/Cube UnlockRect fold):
#pragma comment(                                                                         \
    linker, "/ALTERNATENAME:D3DLineTexture_UnlockRect=D3DCubeTexture_UnlockRect"         \
)
// 0x82E44240: all CSampleXAPOBase<T>::OnSetParameters instantiations fold to one
// body; GainEffect's is the name symbols.txt kept.
#define ONSETPARAMS_ALIAS(WANT)                                                          \
    __pragma(comment(                                                                    \
        linker,                                                                          \
        "/ALTERNATENAME:?OnSetParameters@?$CSampleXAPOBase@" WANT                        \
        "@ATG@@MAAXPBXI@Z="                                                              \
        "?OnSetParameters@?$CSampleXAPOBase@VGainEffect@@UGainEffectParams@@"            \
        "@ATG@@MAAXPBXI@Z"                                                               \
    ))
ONSETPARAMS_ALIAS("VBitCrushEffect@@UParams@1@")
ONSETPARAMS_ALIAS("VCompressionEffect@@UParams@1@")
ONSETPARAMS_ALIAS("VDelayEffect@@UParams@1@")
ONSETPARAMS_ALIAS("VDistortionEffect@@UParams@1@")
ONSETPARAMS_ALIAS("VEQEffect@@UParams@1@")
ONSETPARAMS_ALIAS("VEnvelopeGenerator@@UEnvelopeGeneratorParams@@")
ONSETPARAMS_ALIAS("VFlangerEffect@@UParams@1@")
ONSETPARAMS_ALIAS("VHeadsetPlaybackEffect@@UHeadsetPlaybackEffectParams@@")
ONSETPARAMS_ALIAS("VHeadsetXferEffect@@UHeadsetXferEffectParams@@")
ONSETPARAMS_ALIAS("VMeterEffect@@UMeterEffectParams@@")
ONSETPARAMS_ALIAS("VPitchShiftEffect@@UPitchShiftEffectParams@@")
ONSETPARAMS_ALIAS("VSynapseAPO@DSP@@USynapseAPOParams@2@")
ONSETPARAMS_ALIAS("VWahEffect@@UParams@1@")
#undef ONSETPARAMS_ALIAS

// -- Mangled externs for split-obj lbl_* data (2026-08-23) --
// Decomp sources declare these as C++ externs, so the reference is mangled;
// the data-stub .objs define the unmangled name at the correct layout slot.
#pragma comment(linker, "/ALTERNATENAME:?lbl_82066608@@3PBDB=lbl_82066608")
#pragma comment(linker, "/ALTERNATENAME:?lbl_82F0E8A4@@3HA=lbl_82F0E8A4")
#pragma comment(linker, "/ALTERNATENAME:?lbl_830A4100@@3PAVDataArray@@A=lbl_830A4100")
#pragma comment(linker, "/ALTERNATENAME:?lbl_830A4104@@3HA=lbl_830A4104")

// sprintf/_snprintf: the LIBCMT split objects export these as fn_<addr>.
// Restoring the real names in symbols.txt does NOT stick: `dtk xex split`
// rewrites symbols.txt on every run and reverted both renames within one
// split (tried 2026-08-23; the 1,427-name strip in commit 7274dd67a has the
// same signature and was likely a committed write-back).  Until jeff stops
// dropping these names, alias the callers to the auto names.  Addresses per
// orig map: sprintf=0x829A2760, _snprintf=0x829A1AD0.
#pragma comment(linker, "/ALTERNATENAME:sprintf=fn_829A2760")
#pragma comment(linker, "/ALTERNATENAME:_snprintf=fn_829A1AD0")

// getenv: the original's getenv folded into the return-0 group (map:
// curl_getenv at 0x82AEAE70, li r3,0; blr).  curl_getenv here returns 0, which
// is byte-for-byte what every original caller got.
#pragma comment(linker, "/ALTERNATENAME:getenv=curl_getenv")
// RtlDeleteCriticalSection: not in the original import table, and the original
// emitted no ~CriticalSection at all (CritSec.obj: ctor/Enter/TryEnter/Abandon
// only) -- the destructor's delete call has nothing to bind to.
#pragma comment(linker, "/ALTERNATENAME:RtlDeleteCriticalSection=__link_glue_noop")

// ============================================================================
// Template instantiation stubs
// ALTERNATENAME doesn't work for ??$ template symbols. These need actual
// compiled code to satisfy the linker.
// ============================================================================

// -- Additional includes for template instantiations --
#include "world\CameraShot.h"
#include "char\CharClip.h"
#include "char\CharInterest.h"
#include "char\CharLookAt.h"
#include "flow\Flow.h"
#include "hamobj\HamCamShot.h"
#include "hamobj\HamListRibbon.h"
#include "hamobj\HamScrollSpeedIndicator.h"
#include "hamobj\RhythmDetector.h"
#include "rndobj\Env.h"
#include "rndobj\FontBase.h"
#include "rndobj\Font.h"
#include "rndobj\LitAnim.h"
#include "rndobj\Mat.h"
#include "rndobj\MatAnim.h"
#include "rndobj\MeshAnim.h"
#include "rndobj\PartAnim.h"
#include "rndobj\PartLauncher.h"
#include "rndobj/TexBlendController.h"
#include "rndobj\Group.h"
#include "world\SpotlightDrawer.h"
#include "world\Instance.h"
#include "ui/UIListDir.h"
#include "char\CharCollide.h"
#include "char\CharHair.h"
#include "char\CharClipSet.h"

// -- BinStream operator<< for ObjPtrList<T> --

#define BINSTREAM_OP_OBJPTRLIST(T)                                                       \
    template <>                                                                          \
    BinStream &operator<<(BinStream &bs, const ObjPtrList<T, ObjectDir> &list) {         \
        bs << list.size();                                                               \
        for (ObjPtrList<T>::iterator it = list.begin(); it != list.end(); ++it) {        \
            Hmx::Object *obj = *it;                                                      \
            const char *name = obj ? obj->Name() : "";                                   \
            bs << name;                                                                  \
        }                                                                                \
        return bs;                                                                       \
    }

#undef BINSTREAM_OP_OBJPTRLIST

// -- BinStream operator<< for ObjPtrVec<T> --

#define BINSTREAM_OP_OBJPTRVEC(T)                                                        \
    template <>                                                                          \
    BinStream &operator<<(BinStream &bs, const ObjPtrVec<T, ObjectDir> &vec) {           \
        bs << (int)vec.size();                                                           \
        for (int i = 0; i < (int)vec.size(); i++) {                                      \
            const Hmx::Object *obj = vec[i];                                             \
            const char *name = obj ? obj->Name() : "";                                   \
            bs << name;                                                                  \
        }                                                                                \
        return bs;                                                                       \
    }

#undef BINSTREAM_OP_OBJPTRVEC

// -- BinStream operator<< for ObjOwnerPtr<T> --

#define BINSTREAM_OP_OBJOWNERPTR(T)                                                      \
    template <>                                                                          \
    BinStream &operator<<(BinStream &bs, const ObjOwnerPtr<T> &ptr) {                    \
        Hmx::Object *obj = ptr;                                                          \
        const char *name = obj ? obj->Name() : "";                                       \
        bs << name;                                                                      \
        return bs;                                                                       \
    }

#undef BINSTREAM_OP_OBJOWNERPTR

// -- BinStream operator<< for ObjDirPtr<T> --

#define BINSTREAM_OP_OBJDIRPTR(T)                                                        \
    template <>                                                                          \
    BinStream &operator<<(BinStream &bs, const ObjDirPtr<T> &ptr) {                      \
        T *dir = ptr;                                                                    \
        const char *name = dir ? dir->Name() : "";                                       \
        bs << name;                                                                      \
        return bs;                                                                       \
    }

#undef BINSTREAM_OP_OBJDIRPTR

// Explicit ObjDirPtr<T> operator<< specializations (2026-08-23).  These are
// ??$-mangled, so they cannot be ALTERNATENAME'd (see the section header) and
// need compiled bodies.  In the original all of them fold to one body at
// 0x82793CA8 that writes the directory's name; this body is that body.
#define BINSTREAM_OP_OBJDIRPTR_SPEC(T)                                                   \
    template <>                                                                          \
    BinStream &operator<<(BinStream &bs, const ObjDirPtr<T> &ptr) {                      \
        T *dir = ptr;                                                                    \
        const char *name = dir ? dir->Name() : "";                                       \
        bs << name;                                                                      \
        return bs;                                                                       \
    }
BINSTREAM_OP_OBJDIRPTR_SPEC(ObjectDir)
BINSTREAM_OP_OBJDIRPTR_SPEC(RndDir)
BINSTREAM_OP_OBJDIRPTR_SPEC(UIListDir)
BINSTREAM_OP_OBJDIRPTR_SPEC(UILabelDir)
BINSTREAM_OP_OBJDIRPTR_SPEC(HamListRibbon)
#undef BINSTREAM_OP_OBJDIRPTR_SPEC

// -- PropSync<T> for ObjPtrVec<T> --
// These are stub implementations that just return false.

#define PROPSYNC_OBJPTRVEC(T)                                                            \
    template <>                                                                          \
    bool PropSync(ObjPtrVec<T, ObjectDir> &, DataNode &, DataArray *, int, PropOp) {     \
        return false;                                                                    \
    }

#undef PROPSYNC_OBJPTRVEC

// -- PropSync<T> for ObjDirPtr<T> --
// (WorldInstance specialization moved to Instance.cpp)

// -- GatherObjectsFromGroup<RndMesh> --

template <class T>
unsigned int GatherObjectsFromGroup(RndGroup *, std::vector<T *> &);
