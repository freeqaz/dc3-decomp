// web_stubs.cpp — Proper C++ stub implementations for WASM build
// These replace the asm-label stubs in engine_stubs_generated.cpp that wasm-ld
// can't handle (asm labels produce wrong mangled names or `unreachable` traps).
// Each stub here uses proper C++ types so the Itanium ABI mangler generates
// the correct symbol names for libc++ (std::__2 namespace).

#ifdef __EMSCRIPTEN__

#include "math/Mtx.h"
#include "math/Geo.h"
#include "math/Vec.h"
#include "math/kdTree.h"
#include "utl/TextStream.h"
#include "utl/BinStream.h"
#include "utl/Cache.h"
#include "obj/Dir.h"
#include "obj/ObjPtr_p.h"
#include "rndobj/Utl.h"
#include "rndobj/Lit_NG.h"
#include "rndobj/AmbientOcclusion.h"
#include "rndobj/TexBlendController.h"
#include "rndobj/Part.h"
#include "rndobj/SoftParticleBuffer.h"
#include "rndobj/Draw.h"
#include "rndobj/Mesh.h"
#include "rndobj/Font.h"
#include "ui/UILabelDir.h"

#include <list>
#include <vector>

// ============================================================================
// Math / Geometry stubs
// ============================================================================

void ScaleAddEq(Transform &, const Transform &, float) {}

// MakeBSPTree, BSPFace::Set, Intersect overloads — now in Geo.cpp

void BuildSphereStratified(unsigned int, std::vector<Vector3> &) {}

BuildPoly::BuildPoly() : mPoly(), mTransform() {}

// ============================================================================
// kdTree template stub
// ============================================================================

template <>
bool kdTree<Triangle>::kdTreeNode::FindSplit_SAH(
    const Box &, const std::list<Triangle *> &
) { return false; }

// ============================================================================
// Rendering stubs
// ============================================================================

void NgLight::RenderShadows(std::vector<RndDrawable *> &) {}

void RndAmbientOcclusion::BurnTransform(RndMesh *, std::list<RndMesh *> &) const {}

// ============================================================================
// TextStream stub
// ============================================================================

TextStream &TextStream::operator<<(double) { return *this; }

// ============================================================================
// ObjPtr / BinStream operator<< template instantiations
// ============================================================================

template <>
BinStream &operator<<(BinStream &bs, const ObjDirPtr<UILabelDir> &) { return bs; }

template <>
BinStream &operator<<(BinStream &bs, const ObjOwnerPtr<RndFont3d> &) { return bs; }

// ============================================================================
// Object property listing
// ============================================================================

void ListProperties(std::list<Symbol> &, Symbol, Symbol, std::list<Symbol> *, bool) {}

// ============================================================================
// Holmes (debug network) stub
// ============================================================================

CacheResourceResult HolmesClientCacheResource(const char *, const char *) {
    return kCacheUnnecessary;
}

void HolmesClientPrint(const char *) {}

// ============================================================================
// Engine init stubs — functions called during SystemInit that are either
// unimplemented or Xbox-specific. Emscripten weak stubs don't resolve
// at runtime, so we need strong definitions here.
// ============================================================================

// Spew (debug output system)
void SpewInit() {}
void SpewTerminate() {}

// MemFindHeap, GetCurrentHeapNum — now in MemMgr.cpp

// File system operations not available in MEMFS
void FileRecursePattern(const char *, void (*)(const char *, const char *), bool) {}
int MakeFileList(const char *, bool, bool (*)(char *)) { return 0; }

// Bink video — not supported in browser
void BinkSetMemory(void *(*)(int), void (*)(void *)) {}
int BinkStartAsyncThread(int, int) { return 1; }  // 1 = success
void *RadAlloc(int size) { return malloc(size); }

// Draw estimation — not needed for init
int EstimateDraw(int) { return 0; }

// Xbox/Kinect/network stubs
#include "os/File.h"
#include "xdk/xbdm/xbdm.h"

HRESULT DmMapDevkitDrive() { return 0; }

// System — locale/region stubs
bool HongKongExceptionMet() { return false; }

// Memory profiling — no-ops
#include "Memory.h"
PhysMemTypeTracker::PhysMemTypeTracker(Symbol) {}
PhysMemTypeTracker::~PhysMemTypeTracker() {}
void JoypadSetActuatorsImp(int, int, int) {}

// RecursePatternInternal — now in File.cpp

// NUI speech stubs (Kinect) — use exact XDK signatures
#include "xdk/nui/nuispeech.h"

extern "C" {
HRESULT NuiSpeechDisable() { return 0; }
HRESULT NuiSpeechEnable(NUI_SPEECH_INIT_PROPERTIES *, DWORD) { return 0; }
HRESULT NuiSpeechLoadGrammar(LPCWSTR, ULONG, NUI_SPEECH_LOADOPTIONS, NUI_SPEECH_GRAMMAR *) { return 0; }
HRESULT NuiSpeechUnloadGrammar(NUI_SPEECH_GRAMMAR *) { return 0; }
HRESULT NuiSpeechSetEventInterest(ULONG) { return 0; }
HRESULT NuiSpeechSetGrammarState(NUI_SPEECH_GRAMMAR *, NUI_SPEECH_GRAMMARSTATE) { return 0; }
HRESULT NuiSpeechStartRecognition() { return 0; }
HRESULT NuiSpeechStopRecognition() { return 0; }
HRESULT NuiSpeechSetRuleState(NUI_SPEECH_GRAMMAR *, LPCWSTR, NUI_SPEECH_RULESTATE) { return 0; }
HRESULT NuiSpeechCreateRule(NUI_SPEECH_GRAMMAR *, LPCWSTR, DWORD, BOOL, HANDLE) { return 0; }
HRESULT NuiSpeechCreateState(NUI_SPEECH_GRAMMAR *, HANDLE, HANDLE *) { return 0; }
HRESULT NuiSpeechAddWordTransition(NUI_SPEECH_GRAMMAR *, HANDLE, HANDLE, LPCWSTR, LPCWSTR, NUI_SPEECH_WORDTYPE, float, NUI_SPEECH_SEMANTIC *) { return 0; }
HRESULT NuiSpeechCommitGrammar(NUI_SPEECH_GRAMMAR *) { return 0; }
HRESULT NuiWaveSetEnabled(BOOL) { return 0; }
}

// ============================================================================
// RndParticleSys — virtual method stubs for thunk generation
// ============================================================================

bool RndParticleSys::Replace(ObjRef *ref, Hmx::Object *obj) {
    return Hmx::Object::Replace(ref, obj);
}

// ============================================================================
// RndSoftParticleBuffer — virtual method stub for thunk generation
// ============================================================================

void RndSoftParticleBuffer::DoPost() {}

// Debug::Modal — now in Debug.cpp

// NormalizeSystemArgs — now in System.cpp

// ============================================================================
// Xbox Debug Monitor (xbdm) stubs
// ============================================================================

#include "xdk/xbdm/xbdm.h"

extern "C" {
HRESULT DmGetSystemInfo(DM_SYSTEM_INFO *) { return -1; }  // E_FAIL
}

// ============================================================================
// Vtable anchor stubs — classes with virtual methods that lack implementations
// ============================================================================

// CharSignalApplier — only Handle() is missing (vtable anchor)
#include "char/CharSignalApplier.h"

DataNode CharSignalApplier::Handle(DataArray *d, bool b) { return Hmx::Object::Handle(d, b); }

#include "meta_ham/ChallengeSortByScore.h"

NavListItemNode *ChallengeSortByScore::NewItemNode(void *) const { return nullptr; }
NavListShortcutNode *ChallengeSortByScore::NewShortcutNode(NavListItemNode *) const { return nullptr; }
NavListHeaderNode *ChallengeSortByScore::NewHeaderNode(NavListItemNode *) const { return nullptr; }
NavListHeaderNode *ChallengeSortByScore::NewHeaderNode(NavListItemNode *, NavListItemNode *) const { return nullptr; }

int ChallengeScoreCmp::Compare(const NavListItemSortCmp *, NavListNodeType) const { return 0; }

#include "char/Waypoint.h"

std::list<Waypoint *> *Waypoint::sWaypoints = nullptr;

// ============================================================================
// Bink video — not supported in browser
// ============================================================================

#include "moviebink/BinkMovieSys.h"

void BinkMovieSys::PlatformInit() {}

// BinkMovieImpl — all platform-specific methods (were in Xbox .cpp files)
#include "moviebink/BinkMovieImpl.h"

void BinkMovieImpl::SetWidthHeight(int, int) {}
bool BinkMovieImpl::Ready() const { return true; }
bool BinkMovieImpl::BeginFromFile(const char *, float, bool, bool, bool, bool, int, BinStream *, LoaderPos) { return false; }
void BinkMovieImpl::Draw() {}
bool BinkMovieImpl::Poll() { return true; }
void BinkMovieImpl::Save(BinStream *) {}
void BinkMovieImpl::End() {}
bool BinkMovieImpl::IsOpen() const { return false; }
bool BinkMovieImpl::IsLoading() const { return false; }
bool BinkMovieImpl::CheckOpen(bool) { return false; }
bool BinkMovieImpl::SetPaused(bool) { return false; }
void BinkMovieImpl::UnlockThread() {}
void BinkMovieImpl::LockThread() {}
int BinkMovieImpl::GetFrame() const { return 0; }
float BinkMovieImpl::MsPerFrame() const { return 33.33f; }
int BinkMovieImpl::NumFrames() const { return 0; }
void BinkMovieImpl::SetVolume(float) {}
void BinkMovieImpl::Terminate() {}
bool BinkMovieImpl::PlatformCacheFile(const char *) { return false; }

// Xbox memory card — not available on web
#include "os/Memcard_Xbox.h"
void MemcardXbox::Poll() {}

// WebSvcMgr — network web services, not available on web
#include "net/WebSvcMgr.h"
void WebSvcMgr::Poll() {}

#include "os/VirtualKeyboard.h"
void VirtualKeyboard::PlatformPoll() {}

// Profiling/debug stubs
void AutoGlitchReport::EndExternal(float, float, const char *, AutoTimerCallback, void *) {}

// Kinect voice/speech — not available on web
#include "meta_ham/VoiceInputPanel.h"
void VoiceInputPanel::ActivateVoiceContext(Symbol) {}
void VoiceInputPanel::CreatePlaylistEditorGrammar() const {}

// Forward declare to avoid nuispeech.h conflicts
class SpeechRecoMessage;
DataNode VoiceInputPanel::OnMsg(const SpeechRecoMessage &) { return DataNode(); }


// SpotlightDrawer stubs — now in SpotlightDrawer.cpp / SpotlightDrawer_NG.cpp
// MemPrintOverview — now in MemMgr.cpp
// CopyTypeProperties — now in Utl.cpp

// Memory tracking — no-ops on web
void BeginMemTrackFileName(const char *) {}
void EndMemTrackFileName() {}
void BeginMemTrackObjectName(const char *) {}
void EndMemTrackObjectName() {}
void HolmesClientTerminate() {}
void TerminateMakeString() {}
char *MakeFileListFullPath(const char *) { return nullptr; }

#include "rndobj/Mat.h"
void SetColorWriteMask(const MatShaderOptions &, RndMat *) {}

// Platform-specific inits — no-ops on web
#include "meta/Achievements.h"
void Achievements::PlatformInit() {}

#include "meta_ham/PlaylistSort.h"
PlaylistSortByType::PlaylistSortByType() {}

// Network — not available on web
#include "os/NetworkSocket.h"
unsigned int NetworkSocket::IPStringToInt(const String &) { return 0; }

// Win32/XDK stubs — APIs that don't exist on web
int WSACreateEvent() { return 0; }
int WaitForSingleObject(int, int) { return 0; }
int CloseHandle(int) { return 0; }
int WideCharToMultiByte(int, int, const void *, int, char *, int, const char *, int *) { return 0; }
int XNetDnsLookup(int, int, void *) { return 0; }
void XNetDnsRelease(void *) {}
int XSetThreadProcessor(int, int) { return 0; }

// Scoring / game utilities — stubs for web
void ScoreUtlInit(const DataArray *) {}
void SynthUtlTerm() {}
void altCfg(DataNode, DataNode) {}
void CacheWav(const char *, CacheResourceResult &r) { r = kCacheUnnecessary; }
// DateTimeCmp — now in DateTime.cpp

// ============================================================================
// AudioDevice — no-op for web (no miniaudio)
// ============================================================================

#include "audio/AudioDevice.h"

AudioDevice &AudioDevice::GetInstance() {
    static AudioDevice instance;
    return instance;
}
AudioDevice::AudioDevice() : mDevice(nullptr), mInitialized(false), mSampleRate(0) {}
AudioDevice::~AudioDevice() {}
bool AudioDevice::Init(int) { mSampleRate = 44100; return true; }
void AudioDevice::Terminate() {}
void AudioDevice::AddSource(AudioSource *) {}
void AudioDevice::RemoveSource(AudioSource *) {}
void AudioDevice::MixSources(float *output, int frameCount) {
    memset(output, 0, frameCount * 2 * sizeof(float));
}

#endif // __EMSCRIPTEN__
