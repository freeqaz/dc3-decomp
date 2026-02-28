// Link glue: provides ICF-merged function definitions that are missing from
// split objects. The original linker folded these with identical functions via
// Identical COMDAT Folding (ICF), so they don't exist as separate symbols in
// any split .obj file. Our decomp source defines them (MemMgr.cpp, DataArray.cpp)
// but those units are NonMatching, so the split objects are used instead.
//
// This file also provides stub definitions for unresolved link symbols from
// third-party libraries (libjpeg, zlib, vorbis, curl, etc.) and Xbox SDK
// functions that are not part of the decomp scope.

#include "obj/Data.h"
#include "os/Debug.h"
#include "utl/MemMgr.h"
#include "utl/PoolAlloc.h"
#include "system/synth_xbox/soundtouch/source/SoundTouch/FIRFilter.h"
#include "system/synth/Faders.h"
#include "system/rndobj/Lit.h"
#include "system/world/Spotlight.h"
#include "system/char/Waypoint.h"
#include "system/rndobj/Wind.h"
#include "system/char/CharPollable.h"
#include "system/char/CharWeightSetter.h"
#include "system/flow/FlowNode.h"
#include "system/rndobj/CamAnim.h"

// ============================================================================
// ICF-merged function definitions
// ============================================================================

void operator delete(void *v) { MemFree(v, "unknown", 0, "unknown"); }
void operator delete[](void *v) { MemFree(v, "unknown", 0, "unknown"); }

DataNode &DataArray::Node(int i) const {
    MILO_ASSERT_FMT(
        i >= 0 && i < mSize,
        "Array doesn't have node %d, only has %d (file %s, line %d)",
        i, mSize, File(), Line()
    );
    return mNodes[i];
}

DataNode &DataArray::Node(int i) {
    MILO_ASSERT_FMT(
        i >= 0 && i < mSize,
        "Array doesn't have node %d, only has %d (file %s, line %d)",
        i, mSize, File(), Line()
    );
    return mNodes[i];
}

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
void OggFree(void *) {}
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

struct _stati64_s { char _pad[128]; };
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
int Curl_gethostname(char *, int) { return 0; }
char *curl_getenv(const char *) { return 0; }

} // extern "C"

// ============================================================================
// JPEG memory manager stubs (C++ mangled)
// These are custom memory allocation hooks for libjpeg that the game provides.
// ============================================================================

struct jpeg_common_struct;

void jpeg_mem_term(jpeg_common_struct *) {}
void *jpeg_get_small(jpeg_common_struct *, unsigned int) { return 0; }
void *jpeg_get_large(jpeg_common_struct *, unsigned int) { return 0; }
void jpeg_free_small(jpeg_common_struct *, void *, unsigned int) {}
void jpeg_free_large(jpeg_common_struct *, void *, unsigned int) {}
long jpeg_mem_init(jpeg_common_struct *) { return 0; }
long jpeg_mem_available(jpeg_common_struct *, long, long, long) { return 0; }

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
#include "utl/MakeString.h"

FormatString &FormatString::operator<<(float) { return *this; }
FormatString &FormatString::operator<<(long) { return *this; }
FormatString &FormatString::operator<<(unsigned int) { return *this; }
FormatString &FormatString::operator<<(unsigned long long) { return *this; }

// -- ObjectDir --
#include "obj/Dir.h"

InlineDirType ObjectDir::InlineSubDirType() { return mInlineSubDirType; }
void ObjectDir::AddedObject(Hmx::Object *) {}

// -- PanelDir --
#include "ui/PanelDir.h"

UIComponent *PanelDir::FocusComponent() { return mFocusComponent; }

// -- UIComponent --
#include "ui/UIComponent.h"

void UIComponent::Exit() {}

// -- UIList --
#include "ui/UIList.h"

int UIList::NumData() const { return mNumData; }

// -- BufStream --
// Still needed: virtual method not exported from decomp .obj, referenced by other split .objs
#include "utl/BufStream.h"

int BufStream::Size() { return mSize; }

// -- ObjPtrList template instantiations --
// The linker needs concrete instantiations for various ObjPtrList<T> methods.
// These are normally emitted by other TUs but may be missing from split objects.

#include "rndobj/Trans.h"
#include "rndobj/Draw.h"
#include "rndobj/Anim.h"
#include "rndobj/Mesh.h"
#include "ui/UILabel.h"

// ObjPtrList::RefOwner instantiations
template <>
Hmx::Object *ObjPtrList<Hmx::Object>::RefOwner() const {
    return mOwner ? mOwner->RefOwner() : 0;
}

template <>
Hmx::Object *ObjPtrList<UILabel>::RefOwner() const {
    return mOwner ? mOwner->RefOwner() : 0;
}

template <>
Hmx::Object *ObjPtrList<RndMesh>::RefOwner() const {
    return mOwner ? mOwner->RefOwner() : 0;
}

// ObjPtrList::Replace instantiations
template <>
bool ObjPtrList<Hmx::Object>::Replace(ObjRef *ref, Hmx::Object *obj) {
    for (iterator it = begin(); it != end(); ++it) {
        if (it.mNode == ref) {
            ReplaceNode(it.mNode, obj);
            return true;
        }
    }
    return false;
}

template <>
bool ObjPtrList<UILabel>::Replace(ObjRef *ref, Hmx::Object *obj) {
    for (iterator it = begin(); it != end(); ++it) {
        if (it.mNode == ref) {
            ReplaceNode(it.mNode, obj);
            return true;
        }
    }
    return false;
}

template <>
bool ObjPtrList<RndMesh>::Replace(ObjRef *ref, Hmx::Object *obj) {
    for (iterator it = begin(); it != end(); ++it) {
        if (it.mNode == ref) {
            ReplaceNode(it.mNode, obj);
            return true;
        }
    }
    return false;
}

// ObjPtrList::Node::RefOwner instantiations
template <>
Hmx::Object *ObjPtrList<Hmx::Object>::Node::RefOwner() const {
    ObjPtrList<Hmx::Object> *list = static_cast<ObjPtrList<Hmx::Object> *>(mOwner);
    return list->Owner();
}

template <>
Hmx::Object *ObjPtrList<UILabel>::Node::RefOwner() const {
    ObjPtrList<UILabel> *list = static_cast<ObjPtrList<UILabel> *>(mOwner);
    return list->Owner();
}

template <>
Hmx::Object *ObjPtrList<RndMesh>::Node::RefOwner() const {
    ObjPtrList<RndMesh> *list = static_cast<ObjPtrList<RndMesh> *>(mOwner);
    return list->Owner();
}

// ObjPtrList front() instantiations
template <>
Hmx::Object *ObjPtrList<Hmx::Object>::front() const {
    return mNodes ? mNodes->Obj() : 0;
}

template <>
UILabel *ObjPtrList<UILabel>::front() const {
    return mNodes ? mNodes->Obj() : 0;
}

template <>
RndMesh *ObjPtrList<RndMesh>::front() const {
    return mNodes ? mNodes->Obj() : 0;
}

// ObjPtrList::Unlink instantiations
// Removes node from the linked list, adjusts mSize, returns next node.
template <>
ObjPtrList<Hmx::Object>::Node *ObjPtrList<Hmx::Object>::Unlink(Node *node) {
    Node *next = node->next;
    Node *prev = node->prev;
    if (prev) prev->next = next;
    if (next) next->prev = prev;
    if (mNodes == node) mNodes = next;
    mSize--;
    return next;
}

template <>
ObjPtrList<UILabel>::Node *ObjPtrList<UILabel>::Unlink(Node *node) {
    Node *next = node->next;
    Node *prev = node->prev;
    if (prev) prev->next = next;
    if (next) next->prev = prev;
    if (mNodes == node) mNodes = next;
    mSize--;
    return next;
}

template <>
ObjPtrList<RndMesh>::Node *ObjPtrList<RndMesh>::Unlink(Node *node) {
    Node *next = node->next;
    Node *prev = node->prev;
    if (prev) prev->next = next;
    if (next) next->prev = prev;
    if (mNodes == node) mNodes = next;
    mSize--;
    return next;
}

// ObjPtrList erase instantiations
template <>
ObjPtrList<Hmx::Object>::iterator ObjPtrList<Hmx::Object>::erase(iterator it) {
    Node *node = it.mNode;
    Node *next = Unlink(node);
    delete node;
    return iterator(next);
}

template <>
ObjPtrList<UILabel>::iterator ObjPtrList<UILabel>::erase(iterator it) {
    Node *node = it.mNode;
    Node *next = Unlink(node);
    delete node;
    return iterator(next);
}

template <>
ObjPtrList<RndMesh>::iterator ObjPtrList<RndMesh>::erase(iterator it) {
    Node *node = it.mNode;
    Node *next = Unlink(node);
    delete node;
    return iterator(next);
}

// ObjPtrList sort instantiations
template <>
void ObjPtrList<Hmx::Object>::sort(SortFunc *) {}

// -- ObjRefConcrete::CopyRef instantiations --

template <>
void ObjRefConcrete<RndTransformable>::CopyRef(const ObjRefConcrete<RndTransformable> &o) {
    SetObjConcrete(o.mObject);
}

template <>
void ObjRefConcrete<RndDrawable>::CopyRef(const ObjRefConcrete<RndDrawable> &o) {
    SetObjConcrete(o.mObject);
}

template <>
void ObjRefConcrete<RndAnimatable>::CopyRef(const ObjRefConcrete<RndAnimatable> &o) {
    SetObjConcrete(o.mObject);
}

// -- BinStream operator<< for ObjPtrList --

template <>
BinStream &operator<<(BinStream &bs, const ObjPtrList<Hmx::Object, ObjectDir> &list) {
    bs << list.size();
    for (ObjPtrList<Hmx::Object>::iterator it = list.begin(); it != list.end(); ++it) {
        Hmx::Object *obj = *it;
        const char *name = obj ? obj->Name() : "";
        bs << name;
    }
    return bs;
}

template <>
BinStream &operator<<(BinStream &bs, const ObjPtrList<UILabel, ObjectDir> &list) {
    bs << list.size();
    for (ObjPtrList<UILabel>::iterator it = list.begin(); it != list.end(); ++it) {
        Hmx::Object *obj = *it;
        const char *name = obj ? obj->Name() : "";
        bs << name;
    }
    return bs;
}

template <>
BinStream &operator<<(BinStream &bs, const ObjPtrList<RndMesh, ObjectDir> &list) {
    bs << list.size();
    for (ObjPtrList<RndMesh>::iterator it = list.begin(); it != list.end(); ++it) {
        Hmx::Object *obj = *it;
        const char *name = obj ? obj->Name() : "";
        bs << name;
    }
    return bs;
}

// -- BinStream operator<< for ObjPtrList (additional from Matching promotion) --

template <>
BinStream &operator<<(BinStream &bs, const ObjPtrList<Fader, ObjectDir> &list) {
    bs << list.size();
    for (ObjPtrList<Fader>::iterator it = list.begin(); it != list.end(); ++it) {
        Hmx::Object *obj = *it;
        const char *name = obj ? obj->Name() : "";
        bs << name;
    }
    return bs;
}

template <>
BinStream &operator<<(BinStream &bs, const ObjPtrList<RndLight, ObjectDir> &list) {
    bs << list.size();
    for (ObjPtrList<RndLight>::iterator it = list.begin(); it != list.end(); ++it) {
        Hmx::Object *obj = *it;
        const char *name = obj ? obj->Name() : "";
        bs << name;
    }
    return bs;
}

template <>
BinStream &operator<<(BinStream &bs, const ObjPtrList<Waypoint, ObjectDir> &list) {
    bs << list.size();
    for (ObjPtrList<Waypoint>::iterator it = list.begin(); it != list.end(); ++it) {
        Hmx::Object *obj = *it;
        const char *name = obj ? obj->Name() : "";
        bs << name;
    }
    return bs;
}

template <>
BinStream &operator<<(BinStream &bs, const ObjPtrList<CharPollable, ObjectDir> &list) {
    bs << list.size();
    for (ObjPtrList<CharPollable>::iterator it = list.begin(); it != list.end(); ++it) {
        Hmx::Object *obj = *it;
        const char *name = obj ? obj->Name() : "";
        bs << name;
    }
    return bs;
}

template <>
BinStream &operator<<(BinStream &bs, const ObjPtrList<CharWeightSetter, ObjectDir> &list) {
    bs << list.size();
    for (ObjPtrList<CharWeightSetter>::iterator it = list.begin(); it != list.end(); ++it) {
        Hmx::Object *obj = *it;
        const char *name = obj ? obj->Name() : "";
        bs << name;
    }
    return bs;
}

// -- BinStream operator<< for ObjPtrVec (additional from Matching promotion) --

template <>
BinStream &operator<<(BinStream &bs, const ObjPtrVec<Waypoint, ObjectDir> &vec) {
    bs << (int)vec.size();
    for (int i = 0; i < (int)vec.size(); i++) {
        const Hmx::Object *obj = vec[i];
        const char *name = obj ? obj->Name() : "";
        bs << name;
    }
    return bs;
}

template <>
BinStream &operator<<(BinStream &bs, const ObjPtrVec<FlowNode, ObjectDir> &vec) {
    bs << (int)vec.size();
    for (int i = 0; i < (int)vec.size(); i++) {
        const Hmx::Object *obj = vec[i];
        const char *name = obj ? obj->Name() : "";
        bs << name;
    }
    return bs;
}

template <>
BinStream &operator<<(BinStream &bs, const ObjPtrVec<RndTransformable, ObjectDir> &vec) {
    bs << (int)vec.size();
    for (int i = 0; i < (int)vec.size(); i++) {
        const Hmx::Object *obj = vec[i];
        const char *name = obj ? obj->Name() : "";
        bs << name;
    }
    return bs;
}

// -- BinStream operator<< for ObjOwnerPtr (additional from Matching promotion) --

template <>
BinStream &operator<<(BinStream &bs, const ObjOwnerPtr<RndLight> &ptr) {
    Hmx::Object *obj = ptr;
    const char *name = obj ? obj->Name() : "";
    bs << name;
    return bs;
}

template <>
BinStream &operator<<(BinStream &bs, const ObjOwnerPtr<RndWind> &ptr) {
    Hmx::Object *obj = ptr;
    const char *name = obj ? obj->Name() : "";
    bs << name;
    return bs;
}

template <>
BinStream &operator<<(BinStream &bs, const ObjOwnerPtr<Spotlight> &ptr) {
    Hmx::Object *obj = ptr;
    const char *name = obj ? obj->Name() : "";
    bs << name;
    return bs;
}

template <>
BinStream &operator<<(BinStream &bs, const ObjOwnerPtr<RndCamAnim> &ptr) {
    Hmx::Object *obj = ptr;
    const char *name = obj ? obj->Name() : "";
    bs << name;
    return bs;
}

template <>
BinStream &operator<<(BinStream &bs, const ObjOwnerPtr<RndMesh> &ptr) {
    Hmx::Object *obj = ptr;
    const char *name = obj ? obj->Name() : "";
    bs << name;
    return bs;
}

template <>
BinStream &operator<<(BinStream &bs, const ObjOwnerPtr<RndTransformable> &ptr) {
    Hmx::Object *obj = ptr;
    const char *name = obj ? obj->Name() : "";
    bs << name;
    return bs;
}

// -- BinStream operator<< for ObjOwnerPtr --

template <>
BinStream &operator<<(BinStream &bs, const ObjOwnerPtr<Hmx::Object> &ptr) {
    Hmx::Object *obj = ptr;
    const char *name = obj ? obj->Name() : "";
    bs << name;
    return bs;
}

// -- DeJitter --
#include "utl/DeJitter.h"

DeJitter::DeJitter() : mCurrentIndex(0), mHistoryCount(0), mFilteredDelta(0), mPreviousOutput(0) {
    for (int i = 0; i < 0x20; i++) mHistoryBuffer[i] = 0;
}

void DeJitter::Reset() {
    mCurrentIndex = 0;
    mHistoryCount = 0;
    mFilteredDelta = 0;
    mPreviousOutput = 0;
    for (int i = 0; i < 0x20; i++) mHistoryBuffer[i] = 0;
}

// (DancerSkeleton removed — hamobj/DancerSkeleton is Matching)

// -- VenueProvider --
// Still needed: virtual NumData referenced by CharacterProvider, HamUI, LocalePanel split .objs
#include "meta_ham/VenueProvider.h"

int VenueProvider::NumData() const { return mVenues.size(); }

// (Accomplishment removed — meta_ham/Accomplishment is Matching)

// (FxSendBitCrush removed — synth/FxSendBitCrush is Matching)

// -- CharSignalApplier --
// Still needed: vtordisp thunk in split .obj references this
#include "char/CharSignalApplier.h"

DataNode CharSignalApplier::Handle(DataArray *msg, bool warn) {
    return Hmx::Object::Handle(msg, warn);
}

// -- WorldCrowd3DCharHandle --
// Still needed: vtordisp thunk in split .obj references this
#include "world/Crowd3DCharHandle.h"

bool WorldCrowd3DCharHandle::SyncProperty(DataNode &, DataArray *, int, PropOp) {
    return false;
}

// -- WavMgr --
// Still needed: SyncProperty referenced from WavMgr.obj split
#include "synth/WavMgr.h"

bool WavMgr::SyncProperty(DataNode &, DataArray *, int, PropOp) { return false; }

// -- Achievements --
// Still needed: PlatformInit referenced from Achievements.obj itself
#include "meta/Achievements.h"

void Achievements::PlatformInit() {}

// (UIManager removed — ui/UI is Matching)

// -- Synth --
#include "synth/Synth.h"

int Synth::GetNumMics() const { return mNumMics; }

// -- SongMetadata --
// Still needed: inline in header, not exported from decomp .obj, referenced by SongMgr/SongRecord
#include "meta/SongMetadata.h"

int SongMetadata::ID() const { return mID; }
Symbol SongMetadata::GameOrigin() const { return mGameOrigin; }

// (HamProfile removed — meta_ham/HamProfile is Matching)

// -- HamSongMetadata --
// Still needed: not exported from decomp .obj, referenced by MetagameStats/HamSongMgr/PresenceMgr
#include "meta_ham/HamSongMetadata.h"

const char *HamSongMetadata::Title() const { return mName.c_str(); }

// (FixedSizeSaveableStream removed — meta/FixedSizeSaveableStream is Matching)

// -- soundtouch::FIRFilter --
// The soundtouch library is compiled separately but getLength() may be missing
// from split objects due to ICF.
namespace soundtouch {
    unsigned int FIRFilter::getLength() const { return length; }
}

// -- Stream --
// Static member definition
const float Stream::kStreamEndMs = 1000000000.0f;

// ============================================================================
// Additional ObjPtrList template instantiations for RndTransformable,
// RndDrawable, RndAnimatable, and FlowNode
// ============================================================================

#include "flow/FlowNode.h"

// -- ObjPtrList<RndTransformable, ObjectDir> --

template <>
Hmx::Object *ObjPtrList<RndTransformable>::RefOwner() const {
    return mOwner ? mOwner->RefOwner() : 0;
}

template <>
bool ObjPtrList<RndTransformable>::Replace(ObjRef *ref, Hmx::Object *obj) {
    for (iterator it = begin(); it != end(); ++it) {
        if (it.mNode == ref) {
            ReplaceNode(it.mNode, obj);
            return true;
        }
    }
    return false;
}

template <>
ObjPtrList<RndTransformable>::Node *ObjPtrList<RndTransformable>::Unlink(Node *node) {
    Node *next = node->next;
    Node *prev = node->prev;
    if (prev) prev->next = next;
    if (next) next->prev = prev;
    if (mNodes == node) mNodes = next;
    mSize--;
    return next;
}

template <>
ObjPtrList<RndTransformable>::iterator ObjPtrList<RndTransformable>::erase(iterator it) {
    Node *node = it.mNode;
    Node *next = Unlink(node);
    delete node;
    return iterator(next);
}

template <>
void ObjPtrList<RndTransformable>::Link(iterator it, Node *node) {
    node->mOwner = this;
    Node *pos = it.mNode;
    if (pos) {
        node->next = pos;
        node->prev = pos->prev;
        if (pos->prev) pos->prev->next = node;
        pos->prev = node;
        if (mNodes == pos) mNodes = node;
    } else {
        // insert at end
        if (mNodes) {
            Node *tail = mNodes->prev;
            node->prev = tail;
            node->next = nullptr;
            if (tail) tail->next = node;
        } else {
            node->prev = nullptr;
            node->next = nullptr;
            mNodes = node;
        }
    }
    mSize++;
}

template <>
Hmx::Object *ObjPtrList<RndTransformable>::Node::RefOwner() const {
    ObjPtrList<RndTransformable> *list = static_cast<ObjPtrList<RndTransformable> *>(mOwner);
    return list->Owner();
}

template <>
BinStream &operator<<(BinStream &bs, const ObjPtrList<RndTransformable, ObjectDir> &list) {
    bs << list.size();
    for (ObjPtrList<RndTransformable>::iterator it = list.begin(); it != list.end(); ++it) {
        Hmx::Object *obj = *it;
        const char *name = obj ? obj->Name() : "";
        bs << name;
    }
    return bs;
}

// -- ObjPtrList<RndDrawable, ObjectDir> --

template <>
Hmx::Object *ObjPtrList<RndDrawable>::RefOwner() const {
    return mOwner ? mOwner->RefOwner() : 0;
}

template <>
bool ObjPtrList<RndDrawable>::Replace(ObjRef *ref, Hmx::Object *obj) {
    for (iterator it = begin(); it != end(); ++it) {
        if (it.mNode == ref) {
            ReplaceNode(it.mNode, obj);
            return true;
        }
    }
    return false;
}

template <>
ObjPtrList<RndDrawable>::Node *ObjPtrList<RndDrawable>::Unlink(Node *node) {
    Node *next = node->next;
    Node *prev = node->prev;
    if (prev) prev->next = next;
    if (next) next->prev = prev;
    if (mNodes == node) mNodes = next;
    mSize--;
    return next;
}

template <>
ObjPtrList<RndDrawable>::iterator ObjPtrList<RndDrawable>::erase(iterator it) {
    Node *node = it.mNode;
    Node *next = Unlink(node);
    delete node;
    return iterator(next);
}

template <>
void ObjPtrList<RndDrawable>::Link(iterator it, Node *node) {
    node->mOwner = this;
    Node *pos = it.mNode;
    if (pos) {
        node->next = pos;
        node->prev = pos->prev;
        if (pos->prev) pos->prev->next = node;
        pos->prev = node;
        if (mNodes == pos) mNodes = node;
    } else {
        if (mNodes) {
            Node *tail = mNodes->prev;
            node->prev = tail;
            node->next = nullptr;
            if (tail) tail->next = node;
        } else {
            node->prev = nullptr;
            node->next = nullptr;
            mNodes = node;
        }
    }
    mSize++;
}

template <>
Hmx::Object *ObjPtrList<RndDrawable>::Node::RefOwner() const {
    ObjPtrList<RndDrawable> *list = static_cast<ObjPtrList<RndDrawable> *>(mOwner);
    return list->Owner();
}

template <>
BinStream &operator<<(BinStream &bs, const ObjPtrList<RndDrawable, ObjectDir> &list) {
    bs << list.size();
    for (ObjPtrList<RndDrawable>::iterator it = list.begin(); it != list.end(); ++it) {
        Hmx::Object *obj = *it;
        const char *name = obj ? obj->Name() : "";
        bs << name;
    }
    return bs;
}

// -- ObjPtrList<RndAnimatable, ObjectDir> --

template <>
Hmx::Object *ObjPtrList<RndAnimatable>::RefOwner() const {
    return mOwner ? mOwner->RefOwner() : 0;
}

template <>
bool ObjPtrList<RndAnimatable>::Replace(ObjRef *ref, Hmx::Object *obj) {
    for (iterator it = begin(); it != end(); ++it) {
        if (it.mNode == ref) {
            ReplaceNode(it.mNode, obj);
            return true;
        }
    }
    return false;
}

template <>
ObjPtrList<RndAnimatable>::Node *ObjPtrList<RndAnimatable>::Unlink(Node *node) {
    Node *next = node->next;
    Node *prev = node->prev;
    if (prev) prev->next = next;
    if (next) next->prev = prev;
    if (mNodes == node) mNodes = next;
    mSize--;
    return next;
}

template <>
ObjPtrList<RndAnimatable>::iterator ObjPtrList<RndAnimatable>::erase(iterator it) {
    Node *node = it.mNode;
    Node *next = Unlink(node);
    delete node;
    return iterator(next);
}

template <>
void ObjPtrList<RndAnimatable>::Link(iterator it, Node *node) {
    node->mOwner = this;
    Node *pos = it.mNode;
    if (pos) {
        node->next = pos;
        node->prev = pos->prev;
        if (pos->prev) pos->prev->next = node;
        pos->prev = node;
        if (mNodes == pos) mNodes = node;
    } else {
        if (mNodes) {
            Node *tail = mNodes->prev;
            node->prev = tail;
            node->next = nullptr;
            if (tail) tail->next = node;
        } else {
            node->prev = nullptr;
            node->next = nullptr;
            mNodes = node;
        }
    }
    mSize++;
}

template <>
Hmx::Object *ObjPtrList<RndAnimatable>::Node::RefOwner() const {
    ObjPtrList<RndAnimatable> *list = static_cast<ObjPtrList<RndAnimatable> *>(mOwner);
    return list->Owner();
}

template <>
BinStream &operator<<(BinStream &bs, const ObjPtrList<RndAnimatable, ObjectDir> &list) {
    bs << list.size();
    for (ObjPtrList<RndAnimatable>::iterator it = list.begin(); it != list.end(); ++it) {
        Hmx::Object *obj = *it;
        const char *name = obj ? obj->Name() : "";
        bs << name;
    }
    return bs;
}

// -- ObjPtrList<FlowNode, ObjectDir> --

template <>
ObjPtrList<FlowNode>::Node *ObjPtrList<FlowNode>::Unlink(Node *node) {
    Node *next = node->next;
    Node *prev = node->prev;
    if (prev) prev->next = next;
    if (next) next->prev = prev;
    if (mNodes == node) mNodes = next;
    mSize--;
    return next;
}

template <>
ObjPtrList<FlowNode>::iterator ObjPtrList<FlowNode>::erase(iterator it) {
    Node *node = it.mNode;
    Node *next = Unlink(node);
    delete node;
    return iterator(next);
}

template <>
void ObjPtrList<FlowNode>::Link(iterator it, Node *node) {
    node->mOwner = this;
    Node *pos = it.mNode;
    if (pos) {
        node->next = pos;
        node->prev = pos->prev;
        if (pos->prev) pos->prev->next = node;
        pos->prev = node;
        if (mNodes == pos) mNodes = node;
    } else {
        if (mNodes) {
            Node *tail = mNodes->prev;
            node->prev = tail;
            node->next = nullptr;
            if (tail) tail->next = node;
        } else {
            node->prev = nullptr;
            node->next = nullptr;
            mNodes = node;
        }
    }
    mSize++;
}

template <>
Hmx::Object *ObjPtrList<FlowNode>::Node::RefOwner() const {
    ObjPtrList<FlowNode> *list = static_cast<ObjPtrList<FlowNode> *>(mOwner);
    return list->Owner();
}

// -- ObjPtrList<Hmx::Object, ObjectDir>::Link --

template <>
void ObjPtrList<Hmx::Object>::Link(iterator it, Node *node) {
    node->mOwner = this;
    Node *pos = it.mNode;
    if (pos) {
        node->next = pos;
        node->prev = pos->prev;
        if (pos->prev) pos->prev->next = node;
        pos->prev = node;
        if (mNodes == pos) mNodes = node;
    } else {
        if (mNodes) {
            Node *tail = mNodes->prev;
            node->prev = tail;
            node->next = nullptr;
            if (tail) tail->next = node;
        } else {
            node->prev = nullptr;
            node->next = nullptr;
            mNodes = node;
        }
    }
    mSize++;
}

// ============================================================================
// Additional ObjRefConcrete::CopyRef instantiations
// ============================================================================

#include "rndobj/Tex.h"
#include "synth/MoggClip.h"

template <>
void ObjRefConcrete<RndTex, ObjectDir>::CopyRef(const ObjRefConcrete<RndTex, ObjectDir> &o) {
    SetObjConcrete(o.mObject);
}

template <>
void ObjRefConcrete<ObjectDir, ObjectDir>::CopyRef(const ObjRefConcrete<ObjectDir, ObjectDir> &o) {
    SetObjConcrete(o.mObject);
}

template <>
void ObjRefConcrete<MoggClip, ObjectDir>::CopyRef(const ObjRefConcrete<MoggClip, ObjectDir> &o) {
    SetObjConcrete(o.mObject);
}

// ============================================================================
// BinStream operator<< for ObjOwnerPtr<FxSend>
// ============================================================================

#include "synth/FxSend.h"

template <>
BinStream &operator<<(BinStream &bs, const ObjOwnerPtr<FxSend> &ptr) {
    Hmx::Object *obj = ptr;
    const char *name = obj ? obj->Name() : "";
    bs << name;
    return bs;
}

// ============================================================================
// BeatMap constructor stub
// ============================================================================

#include "utl/BeatMap.h"

BeatMap::BeatMap() {}

// ============================================================================
// MidiReader::GetFilename stub
// ============================================================================

#include "midi/Midi.h"

const char *MidiReader::GetFilename() const { return mStreamName.c_str(); }

// (FixedString::FixedString removed — utl/Str is Matching)

// ============================================================================
// ObjPtrList instantiations for Character, Sequence, Task, EventTrigger,
// RndPartLauncher — needed by Reflection, UITrigger
// ============================================================================

#include "char/Character.h"
#include "synth/Sequence.h"
#include "obj/Task.h"
#include "rndobj/EventTrigger.h"
#include "rndobj/Part.h"

// -- ObjPtrList<Character, ObjectDir> --

template <>
Hmx::Object *ObjPtrList<Character>::RefOwner() const {
    return mOwner ? mOwner->RefOwner() : 0;
}

template <>
bool ObjPtrList<Character>::Replace(ObjRef *ref, Hmx::Object *obj) {
    for (iterator it = begin(); it != end(); ++it) {
        if (it.mNode == ref) { ReplaceNode(it.mNode, obj); return true; }
    }
    return false;
}

template <>
ObjPtrList<Character>::Node *ObjPtrList<Character>::Unlink(Node *node) {
    Node *next = node->next;
    Node *prev = node->prev;
    if (prev) prev->next = next;
    if (next) next->prev = prev;
    if (mNodes == node) mNodes = next;
    mSize--;
    return next;
}

template <>
ObjPtrList<Character>::iterator ObjPtrList<Character>::erase(iterator it) {
    Node *node = it.mNode;
    Node *next = Unlink(node);
    delete node;
    return iterator(next);
}

template <>
void ObjPtrList<Character>::Link(iterator it, Node *node) {
    node->mOwner = this;
    Node *pos = it.mNode;
    if (pos) {
        node->next = pos;
        node->prev = pos->prev;
        if (pos->prev) pos->prev->next = node;
        pos->prev = node;
        if (mNodes == pos) mNodes = node;
    } else {
        if (mNodes) {
            Node *tail = mNodes->prev;
            node->prev = tail;
            node->next = nullptr;
            if (tail) tail->next = node;
        } else {
            node->prev = nullptr;
            node->next = nullptr;
            mNodes = node;
        }
    }
    mSize++;
}

template <>
Hmx::Object *ObjPtrList<Character>::Node::RefOwner() const {
    ObjPtrList<Character> *list = static_cast<ObjPtrList<Character> *>(mOwner);
    return list->Owner();
}

template <>
BinStream &operator<<(BinStream &bs, const ObjPtrList<Character, ObjectDir> &list) {
    bs << list.size();
    for (ObjPtrList<Character>::iterator it = list.begin(); it != list.end(); ++it) {
        Hmx::Object *obj = *it;
        bs << (obj ? obj->Name() : "");
    }
    return bs;
}

// -- ObjPtrList<Sequence, ObjectDir> --

template <>
Hmx::Object *ObjPtrList<Sequence>::RefOwner() const {
    return mOwner ? mOwner->RefOwner() : 0;
}

template <>
bool ObjPtrList<Sequence>::Replace(ObjRef *ref, Hmx::Object *obj) {
    for (iterator it = begin(); it != end(); ++it) {
        if (it.mNode == ref) { ReplaceNode(it.mNode, obj); return true; }
    }
    return false;
}

template <>
ObjPtrList<Sequence>::Node *ObjPtrList<Sequence>::Unlink(Node *node) {
    Node *next = node->next;
    Node *prev = node->prev;
    if (prev) prev->next = next;
    if (next) next->prev = prev;
    if (mNodes == node) mNodes = next;
    mSize--;
    return next;
}

template <>
ObjPtrList<Sequence>::iterator ObjPtrList<Sequence>::erase(iterator it) {
    Node *node = it.mNode;
    Node *next = Unlink(node);
    delete node;
    return iterator(next);
}

template <>
Hmx::Object *ObjPtrList<Sequence>::Node::RefOwner() const {
    ObjPtrList<Sequence> *list = static_cast<ObjPtrList<Sequence> *>(mOwner);
    return list->Owner();
}

// -- ObjPtrList<Task, ObjectDir> --

template <>
Hmx::Object *ObjPtrList<Task>::RefOwner() const {
    return mOwner ? mOwner->RefOwner() : 0;
}

template <>
bool ObjPtrList<Task>::Replace(ObjRef *ref, Hmx::Object *obj) {
    for (iterator it = begin(); it != end(); ++it) {
        if (it.mNode == ref) { ReplaceNode(it.mNode, obj); return true; }
    }
    return false;
}

template <>
ObjPtrList<Task>::Node *ObjPtrList<Task>::Unlink(Node *node) {
    Node *next = node->next;
    Node *prev = node->prev;
    if (prev) prev->next = next;
    if (next) next->prev = prev;
    if (mNodes == node) mNodes = next;
    mSize--;
    return next;
}

template <>
ObjPtrList<Task>::iterator ObjPtrList<Task>::erase(iterator it) {
    Node *node = it.mNode;
    Node *next = Unlink(node);
    delete node;
    return iterator(next);
}

template <>
Hmx::Object *ObjPtrList<Task>::Node::RefOwner() const {
    ObjPtrList<Task> *list = static_cast<ObjPtrList<Task> *>(mOwner);
    return list->Owner();
}

// -- ObjPtrList<EventTrigger, ObjectDir> --

template <>
Hmx::Object *ObjPtrList<EventTrigger>::RefOwner() const {
    return mOwner ? mOwner->RefOwner() : 0;
}

template <>
bool ObjPtrList<EventTrigger>::Replace(ObjRef *ref, Hmx::Object *obj) {
    for (iterator it = begin(); it != end(); ++it) {
        if (it.mNode == ref) { ReplaceNode(it.mNode, obj); return true; }
    }
    return false;
}

template <>
ObjPtrList<EventTrigger>::Node *ObjPtrList<EventTrigger>::Unlink(Node *node) {
    Node *next = node->next;
    Node *prev = node->prev;
    if (prev) prev->next = next;
    if (next) next->prev = prev;
    if (mNodes == node) mNodes = next;
    mSize--;
    return next;
}

template <>
ObjPtrList<EventTrigger>::iterator ObjPtrList<EventTrigger>::erase(iterator it) {
    Node *node = it.mNode;
    Node *next = Unlink(node);
    delete node;
    return iterator(next);
}

template <>
Hmx::Object *ObjPtrList<EventTrigger>::Node::RefOwner() const {
    ObjPtrList<EventTrigger> *list = static_cast<ObjPtrList<EventTrigger> *>(mOwner);
    return list->Owner();
}

// -- ObjPtrList<RndPartLauncher, ObjectDir> --

template <>
Hmx::Object *ObjPtrList<RndPartLauncher>::RefOwner() const {
    return mOwner ? mOwner->RefOwner() : 0;
}

template <>
bool ObjPtrList<RndPartLauncher>::Replace(ObjRef *ref, Hmx::Object *obj) {
    for (iterator it = begin(); it != end(); ++it) {
        if (it.mNode == ref) { ReplaceNode(it.mNode, obj); return true; }
    }
    return false;
}

template <>
ObjPtrList<RndPartLauncher>::Node *ObjPtrList<RndPartLauncher>::Unlink(Node *node) {
    Node *next = node->next;
    Node *prev = node->prev;
    if (prev) prev->next = next;
    if (next) next->prev = prev;
    if (mNodes == node) mNodes = next;
    mSize--;
    return next;
}

template <>
ObjPtrList<RndPartLauncher>::iterator ObjPtrList<RndPartLauncher>::erase(iterator it) {
    Node *node = it.mNode;
    Node *next = Unlink(node);
    delete node;
    return iterator(next);
}

template <>
Hmx::Object *ObjPtrList<RndPartLauncher>::Node::RefOwner() const {
    ObjPtrList<RndPartLauncher> *list = static_cast<ObjPtrList<RndPartLauncher> *>(mOwner);
    return list->Owner();
}

// ============================================================================
// Additional ObjRefConcrete::CopyRef instantiations
// ============================================================================

template <>
void ObjRefConcrete<EventTrigger, ObjectDir>::CopyRef(const ObjRefConcrete<EventTrigger, ObjectDir> &o) {
    SetObjConcrete(o.mObject);
}

template <>
void ObjRefConcrete<UILabel, ObjectDir>::CopyRef(const ObjRefConcrete<UILabel, ObjectDir> &o) {
    SetObjConcrete(o.mObject);
}

template <>
void ObjRefConcrete<RndMesh, ObjectDir>::CopyRef(const ObjRefConcrete<RndMesh, ObjectDir> &o) {
    SetObjConcrete(o.mObject);
}

// ============================================================================
// BinStream operator<< for additional ObjOwnerPtr types
// ============================================================================

#include "char/CharWeightable.h"

template <>
BinStream &operator<<(BinStream &bs, const ObjOwnerPtr<CharWeightable> &ptr) {
    Hmx::Object *obj = ptr;
    bs << (obj ? obj->Name() : "");
    return bs;
}

template <>
BinStream &operator<<(BinStream &bs, const ObjOwnerPtr<RndTex> &ptr) {
    Hmx::Object *obj = ptr;
    bs << (obj ? obj->Name() : "");
    return bs;
}

// ============================================================================
// Missing accessor/method stubs for Matching unit resolution
// ============================================================================

#include "meta/Profile.h"
#include "meta_ham/AccomplishmentProgress.h"
#include "meta_ham/CampaignEra.h"
#include "meta_ham/AccomplishmentGroup.h"
#include "meta_ham/Award.h"
#include "meta/MetaMusicManager.h"
#include "meta/MetaMusicScene.h"
#include "os/PlatformMgr.h"

// -- Profile stubs --
int Profile::GetPadNum() const { return mPadNum; }
void Profile::MakeDirty() { mDirty = true; }

// -- CampaignEra stubs --
Symbol CampaignEra::GetName() const { return mEra; }

// -- UIComponent stubs --
void UIComponent::PostLoad(BinStream &) {}

// -- PlatformMgr stubs --
void PlatformMgr::RunNetStartUtility() {}
void PlatformMgr::CheckMailbox() {}
void PlatformMgr::DisableXMP() {}
DataNode PlatformMgr::OnSignInUsers(DataArray *) { return DataNode(0); }

// -- AccomplishmentProgress stubs --
int AccomplishmentProgress::GetNumCompleted() const { return 0; }
int AccomplishmentProgress::GetTotalSongsPlayed() const { return mTotalSongsPlayed; }
int AccomplishmentProgress::GetTotalCampaignSongsPlayed() const { return mTotalCampaignSongsPlayed; }

// -- AccomplishmentGroup stubs --
Symbol AccomplishmentGroup::GetAward() const { return mAward; }

// -- Award stubs --
Symbol Award::GetName() const { return mName; }

// -- MetaMusicScene stubs --
Symbol MetaMusicScene::GetName() const { return m_symName; }
const std::list<Symbol> &MetaMusicScene::GetScreenList() const { return m_lScreens; }

// -- MetaMusicManager stubs --
MetaMusicScene *MetaMusicManager::GetScene(Symbol s) const {
    std::map<Symbol, MetaMusicScene *>::const_iterator it = m_mapScenes.find(s);
    if (it != m_mapScenes.end())
        return it->second;
    return 0;
}

DataNode MetaMusicManager::Handle(DataArray *da, bool b) {
    return Hmx::Object::Handle(da, b);
}

// -- ProfileMgr stubs --
#include "meta_ham/ProfileMgr.h"
std::vector<HamProfile *> ProfileMgr::GetSignedInProfiles() {
    std::vector<HamProfile *> v;
    return v;
}

// -- CharServoBone stubs --
#include "char/CharServoBone.h"
void CharServoBone::PollDeps(
    std::list<Hmx::Object *> &changedBy, std::list<Hmx::Object *> &change
) {
    CharBonesMeshes::StuffMeshes(change);
}

// -- CharBonesMeshes stubs (vtordisp thunk needs this) --
#include "char/CharBonesMeshes.h"
bool CharBonesMeshes::Replace(ObjRef *ref, Hmx::Object *obj) {
    return Hmx::Object::Replace(ref, obj);
}

// -- WorldReflection stubs --
#include "world/Reflection.h"
void WorldReflection::Highlight() {}

// -- AppLabel stubs --
#include "meta_ham/AppLabel.h"
void AppLabel::SetFromGeneralSelectNode(const NavListNode *) {}

// ============================================================================
// Round 2: Additional stubs for 55 more Matching units
// ============================================================================

// -- FormatString stubs --
#include "utl/MakeString.h"
FormatString &FormatString::operator<<(void *) { return *this; }
FormatString &FormatString::operator<<(unsigned long) { return *this; }

// -- DebugNotifyOncePrinter global --
DebugNotifyOncePrinter TheDebugNotifyOncePrinter;

// -- NavListSortMgr stubs --
#include "meta_ham/NavListSortMgr.h"
bool NavListSortMgr::HeadersSelectable() { return false; }
bool NavListSortMgr::SelectionIs(Symbol) { return false; }
bool NavListSortMgr::DataIs(int, Symbol) { return false; }
Symbol NavListSortMgr::MoveOn() { return gNullStr; }
void NavListSortMgr::OnEnter() {}

// -- ObjRefConcrete template stubs --
template <>
void ObjRefConcrete<CharWeightable, ObjectDir>::CopyRef(const ObjRefConcrete<CharWeightable, ObjectDir> &o) {
    SetObjConcrete(o.mObject);
}

#include "rndobj/Wind.h"
template <>
void ObjRefConcrete<RndWind, ObjectDir>::CopyRef(const ObjRefConcrete<RndWind, ObjectDir> &o) {
    SetObjConcrete(o.mObject);
}

// -- ObjPtrVec<RndTransformable> stubs --
template <>
Hmx::Object *ObjPtrVec<RndTransformable, ObjectDir>::Node::RefOwner() const {
    return static_cast<Hmx::Object*>(mOwner);
}

template <>
ObjPtrVec<RndTransformable, ObjectDir>::iterator
ObjPtrVec<RndTransformable, ObjectDir>::erase(ObjPtrVec<RndTransformable, ObjectDir>::iterator it) {
    return mNodes.erase(&*it);
}

// -- CharDriver stubs --
#include "char/CharDriver.h"
void CharDriver::PollDeps(std::list<Hmx::Object *> &, std::list<Hmx::Object *> &) {}
void CharDriver::Exit() {}

// -- SongMetadata stubs --
#include "meta/SongMetadata.h"
bool SongMetadata::IsOnDisc() const { return mIsOnDisc; }

// -- CacheMgr stubs --
#include "utl/CacheMgr.h"
CacheResult CacheMgr::GetLastResult() { return mLastResult; }

// -- MemStream stubs --
#include "utl/MemStream.h"
void MemStream::Flush() {}
bool MemStream::Fail() { return mFail; }

// -- XLSPConnection stubs --
#include "net/XLSPConnection.h"
unsigned int XLSPConnection::GetServiceIP() { return 0; }

// -- PracticeSection stubs --
#include "hamobj/PracticeSection.h"
const std::vector<PracticeStep> &PracticeSection::Steps() const { return mSteps; }

// -- ADSR stubs --
#include "synth/ADSR.h"
DataNode ADSR::Handle(DataArray *da, bool b) { return Hmx::Object::Handle(da, b); }

// -- GestureMgr stubs --
#include "gesture/GestureMgr.h"
LiveCameraInput *GestureMgr::GetLiveCameraInput() const { return 0; }

// -- CampaignProgress stubs --
#include "meta_ham/CampaignProgress.h"
bool CampaignProgress::IsCampaignIntroCompleted() const { return false; }
bool CampaignProgress::IsCampaignMindControlCompleted() const { return false; }

// -- CampaignEra stubs (round 2) --
Symbol CampaignEra::GetIntroMovie() const { return mEraIntroMovie; }

// -- UIListMesh stubs --
#include "ui/UIListMesh.h"
RndMat *UIListMesh::DefaultMat() const { return mDefaultMat; }

// -- Hmx::Object stubs --
void Hmx::Object::ClearAllTypeProps() {}

// -- Award stubs (round 2) --
bool Award::IsSilent() const { return mIsSilent; }

// -- FaderGroup stubs --
#include "synth/Faders.h"
bool FaderGroup::Dirty() { return mDirty; }

// -- UIList stubs --
#include "ui/UIList.h"
int UIList::SelectedPos() const { return mListState.Selected(); }

// -- NetCacheMgr stubs --
#include "utl/NetCacheMgr.h"
unsigned int NetCacheMgr::GetServiceId() const { return 0; }
const char *NetCacheMgr::GetXLSPFilter() const { return ""; }

// -- NetCacheMgrXbox stubs --
#include "utl/NetCacheMgr_Xbox.h"
DataNode NetCacheMgrXbox::Handle(DataArray *da, bool b) { return NetCacheMgr::Handle(da, b); }

// -- Round 3 stubs --

// ObjRefConcrete::CopyRef for RndParticleSys (PartLauncher.obj)
#include "rndobj/Part.h"
template <>
void ObjRefConcrete<RndParticleSys, ObjectDir>::CopyRef(const ObjRefConcrete<RndParticleSys, ObjectDir> &o) {
    SetObjConcrete(o.mObject);
}

// ObjRefConcrete::CopyRef for CharBone (CharBone.obj)
#include "char/CharBone.h"
template <>
void ObjRefConcrete<CharBone, ObjectDir>::CopyRef(const ObjRefConcrete<CharBone, ObjectDir> &o) {
    SetObjConcrete(o.mObject);
}

// FileLoader::GetSize (FileCache.obj, NetLoader.obj)
#include "utl/Loader.h"
int FileLoader::GetSize() { return mBufLen; }

// NavListHeaderNode::Handle (MQSongSortNode.obj)
#include "meta_ham/NavListNode.h"
DataNode NavListHeaderNode::Handle(DataArray *da, bool b) { return NavListSortNode::Handle(da, b); }

// BinStream operator<< for ObjOwnerPtr<RndTransAnim> (TransAnim.obj)
#include "rndobj/TransAnim.h"
template <>
BinStream &operator<<(BinStream &bs, const ObjOwnerPtr<RndTransAnim> &ptr) {
    Hmx::Object *obj = ptr;
    const char *name = obj ? obj->Name() : "";
    bs << name;
    return bs;
}

// BinStream operator<< for ObjDirPtr<RndDir> (UISlider.obj)
#include "obj/Dir.h"
#include "rndobj/Rnd.h"
template <>
BinStream &operator<<(BinStream &bs, const ObjDirPtr<RndDir> &ptr) {
    RndDir *dir = ptr;
    const char *name = dir ? dir->Name() : "";
    bs << name;
    return bs;
}

// BinStream operator<< for ObjPtrList<CharPollable> (CharPollGroup.obj - if needed later)
// wmemcpy (SpeechMgr.obj - CRT function, needs library)

// -- HolmesClientPrint stub (ArkFile.obj) --
#include "os/HolmesClient.h"
void HolmesClientPrint(const char *) {}

// -- MemOrPoolFree stub (Str.obj) --
#include "utl/MemMgr.h"
void MemOrPoolFree(int, void *mem, const char *, int, const char *) {}


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
extern "C" int __link_glue_zero[64] = {0};
extern "C" const char __link_glue_empty_str[] = "";


// Remaining unresolved symbols from Matching unit decomp-only linking.
#pragma comment(linker, "/ALTERNATENAME:?NewBufStream@Synth@@UAAPAVStream@@PBXHVSymbol@@M_N@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??1CriticalSection@@QAA@XZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?Terminate@UILabel@@SAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?gCheatsManager@@3PAVCheatsManager@@A=__link_glue_zero")

// ============================================================================
// Auto-generated stubs for symbols lost when units promoted to Matching
// Generated from link errors after 339 units promoted via sync_match_percent.py
// ============================================================================

// -- ObjPtr/ObjRef template instantiations --
#pragma comment(linker, "/ALTERNATENAME:?CopyRef@?$ObjRefConcrete@VBaseMaterial@@VObjectDir@@@@QAAXABV1@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?CopyRef@?$ObjRefConcrete@VCharDriver@@VObjectDir@@@@QAAXABV1@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?CopyRef@?$ObjRefConcrete@VCharEyeDartRuleset@@VObjectDir@@@@QAAXABV1@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?CopyRef@?$ObjRefConcrete@VCharIKFoot@@VObjectDir@@@@QAAXABV1@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?CopyRef@?$ObjRefConcrete@VCharPollable@@VObjectDir@@@@QAAXABV1@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?CopyRef@?$ObjRefConcrete@VCharacter@@VObjectDir@@@@QAAXABV1@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?CopyRef@?$ObjRefConcrete@VFader@@VObjectDir@@@@QAAXABV1@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?CopyRef@?$ObjRefConcrete@VHamLabel@@VObjectDir@@@@QAAXABV1@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?CopyRef@?$ObjRefConcrete@VHamPhraseMeter@@VObjectDir@@@@QAAXABV1@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?CopyRef@?$ObjRefConcrete@VObject@Hmx@@VObjectDir@@@@QAAXABV1@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?CopyRef@?$ObjRefConcrete@VRhythmBattlePlayer@@VObjectDir@@@@QAAXABV1@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?CopyRef@?$ObjRefConcrete@VRndCam@@VObjectDir@@@@QAAXABV1@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?CopyRef@?$ObjRefConcrete@VRndCubeTex@@VObjectDir@@@@QAAXABV1@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?CopyRef@?$ObjRefConcrete@VRndEnviron@@VObjectDir@@@@QAAXABV1@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?CopyRef@?$ObjRefConcrete@VRndFur@@VObjectDir@@@@QAAXABV1@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?CopyRef@?$ObjRefConcrete@VRndGroup@@VObjectDir@@@@QAAXABV1@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?CopyRef@?$ObjRefConcrete@VRndMat@@VObjectDir@@@@QAAXABV1@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?CopyRef@?$ObjRefConcrete@VRndTransAnim@@VObjectDir@@@@QAAXABV1@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?CopyRef@?$ObjRefConcrete@VSound@@VObjectDir@@@@QAAXABV1@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?CopyRef@?$ObjRefConcrete@VSpotlight@@VObjectDir@@@@QAAXABV1@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?CopyRef@?$ObjRefConcrete@VUIColor@@VObjectDir@@@@QAAXABV1@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?CopyRef@?$ObjRefConcrete@VUIComponent@@VObjectDir@@@@QAAXABV1@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?CopyRef@?$ObjRefConcrete@VUIList@@VObjectDir@@@@QAAXABV1@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?CopyRef@?$ObjRefConcrete@VWorldCrowd@@VObjectDir@@@@QAAXABV1@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?Link@?$ObjPtrList@VCharPollable@@VObjectDir@@@@AAAXViterator@1@PAUNode@1@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?Link@?$ObjPtrList@VCharWeightSetter@@VObjectDir@@@@AAAXViterator@1@PAUNode@1@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?Link@?$ObjPtrList@VCharWeightable@@VObjectDir@@@@AAAXViterator@1@PAUNode@1@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?Link@?$ObjPtrList@VFader@@VObjectDir@@@@AAAXViterator@1@PAUNode@1@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?Link@?$ObjPtrList@VRndLight@@VObjectDir@@@@AAAXViterator@1@PAUNode@1@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?Link@?$ObjPtrList@VThreeDSound@@VObjectDir@@@@AAAXViterator@1@PAUNode@1@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?Link@?$ObjPtrList@VWaypoint@@VObjectDir@@@@AAAXViterator@1@PAUNode@1@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?RefOwner@?$ObjPtrList@VCharPollable@@VObjectDir@@@@EBAPAVObject@Hmx@@XZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?RefOwner@?$ObjPtrList@VCharWeightSetter@@VObjectDir@@@@EBAPAVObject@Hmx@@XZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?RefOwner@?$ObjPtrList@VCharWeightable@@VObjectDir@@@@EBAPAVObject@Hmx@@XZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?RefOwner@?$ObjPtrList@VFader@@VObjectDir@@@@EBAPAVObject@Hmx@@XZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?RefOwner@?$ObjPtrList@VFlowNode@@VObjectDir@@@@EBAPAVObject@Hmx@@XZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?RefOwner@?$ObjPtrList@VRndFontBase@@VObjectDir@@@@EBAPAVObject@Hmx@@XZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?RefOwner@?$ObjPtrList@VRndLight@@VObjectDir@@@@EBAPAVObject@Hmx@@XZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?RefOwner@?$ObjPtrList@VRndMat@@VObjectDir@@@@EBAPAVObject@Hmx@@XZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?RefOwner@?$ObjPtrList@VThreeDSound@@VObjectDir@@@@EBAPAVObject@Hmx@@XZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?RefOwner@?$ObjPtrList@VWaypoint@@VObjectDir@@@@EBAPAVObject@Hmx@@XZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?RefOwner@Node@?$ObjPtrList@VCharPollable@@VObjectDir@@@@UBAPAVObject@Hmx@@XZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?RefOwner@Node@?$ObjPtrList@VCharWeightSetter@@VObjectDir@@@@UBAPAVObject@Hmx@@XZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?RefOwner@Node@?$ObjPtrList@VCharWeightable@@VObjectDir@@@@UBAPAVObject@Hmx@@XZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?RefOwner@Node@?$ObjPtrList@VFader@@VObjectDir@@@@UBAPAVObject@Hmx@@XZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?RefOwner@Node@?$ObjPtrList@VRndLight@@VObjectDir@@@@UBAPAVObject@Hmx@@XZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?RefOwner@Node@?$ObjPtrList@VThreeDSound@@VObjectDir@@@@UBAPAVObject@Hmx@@XZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?RefOwner@Node@?$ObjPtrList@VWaypoint@@VObjectDir@@@@UBAPAVObject@Hmx@@XZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?RefOwner@Node@?$ObjPtrVec@VFlowNode@@VObjectDir@@@@UBAPAVObject@Hmx@@XZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?RefOwner@Node@?$ObjPtrVec@VRndTex@@VObjectDir@@@@UBAPAVObject@Hmx@@XZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?RefOwner@Node@?$ObjPtrVec@VWaypoint@@VObjectDir@@@@UBAPAVObject@Hmx@@XZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?Replace@?$ObjPtrList@VCharPollable@@VObjectDir@@@@EAA_NPAVObjRef@@PAVObject@Hmx@@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?Replace@?$ObjPtrList@VCharWeightSetter@@VObjectDir@@@@EAA_NPAVObjRef@@PAVObject@Hmx@@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?Replace@?$ObjPtrList@VCharWeightable@@VObjectDir@@@@EAA_NPAVObjRef@@PAVObject@Hmx@@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?Replace@?$ObjPtrList@VFader@@VObjectDir@@@@EAA_NPAVObjRef@@PAVObject@Hmx@@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?Replace@?$ObjPtrList@VFlowNode@@VObjectDir@@@@EAA_NPAVObjRef@@PAVObject@Hmx@@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?Replace@?$ObjPtrList@VRndFontBase@@VObjectDir@@@@EAA_NPAVObjRef@@PAVObject@Hmx@@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?Replace@?$ObjPtrList@VRndLight@@VObjectDir@@@@EAA_NPAVObjRef@@PAVObject@Hmx@@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?Replace@?$ObjPtrList@VRndMat@@VObjectDir@@@@EAA_NPAVObjRef@@PAVObject@Hmx@@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?Replace@?$ObjPtrList@VThreeDSound@@VObjectDir@@@@EAA_NPAVObjRef@@PAVObject@Hmx@@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?Replace@?$ObjPtrList@VWaypoint@@VObjectDir@@@@EAA_NPAVObjRef@@PAVObject@Hmx@@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?erase@?$ObjPtrList@VCharPollable@@VObjectDir@@@@QAA?AViterator@1@V21@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?erase@?$ObjPtrList@VCharWeightSetter@@VObjectDir@@@@QAA?AViterator@1@V21@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?erase@?$ObjPtrList@VCharWeightable@@VObjectDir@@@@QAA?AViterator@1@V21@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?erase@?$ObjPtrList@VFader@@VObjectDir@@@@QAA?AViterator@1@V21@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?erase@?$ObjPtrList@VRndFontBase@@VObjectDir@@@@QAA?AViterator@1@V21@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?erase@?$ObjPtrList@VRndLight@@VObjectDir@@@@QAA?AViterator@1@V21@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?erase@?$ObjPtrList@VRndMat@@VObjectDir@@@@QAA?AViterator@1@V21@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?erase@?$ObjPtrList@VThreeDSound@@VObjectDir@@@@QAA?AViterator@1@V21@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?erase@?$ObjPtrList@VWaypoint@@VObjectDir@@@@QAA?AViterator@1@V21@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?erase@?$ObjPtrVec@VWaypoint@@VObjectDir@@@@QAA?AViterator@1@V21@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?front@?$ObjPtrList@VFader@@VObjectDir@@@@QBAPAVFader@@XZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?merged_ObjPtrListPopBack@@YAXPAX@Z=__link_glue_noop")

// -- BinStream operators --
#pragma comment(linker, "/ALTERNATENAME:??5@YAAAVBinStream@@AAV0@AAUPropTriggerDefn@FlowTrigger@@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?PostLoad@HamDriver@@UAAXAAVBinStream@@@Z=__link_glue_noop")

// -- Data symbols --
#pragma comment(linker, "/ALTERNATENAME:?lbl_82F1AB98@@3IA=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:?lbl_82F1AB9C@@3IA=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:?lbl_82F1ABA0@@3IA=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:?lbl_82F5E180@@3JC=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:?sLoadingMaster@LoadingPanel@@2PAVHamMaster@@A=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:?sSongDB@LoadingPanel@@2PAVSongDB@@A=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:?tf2cf@RndRenderState@@2PAW4_D3DCMPFUNC@@A=__link_glue_zero")

// -- Other functions --
#pragma comment(linker, "/ALTERNATENAME:??0LocationCmp@@QAA@XZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??1DifficultyCmp@@UAA@XZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??1MQSongSortNode@@UAA@XZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??1SongCmp@@UAA@XZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??RSortCmp@@QBA_NPBVStoreOffer@@0@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?ClipStart@?A0xf8c6d506@@YAMPAVCharClip@@MAAM1@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?DrawFixedZ@DrawString@@UAAXM@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?DrawShowing@SpotlightDrawer@@UAAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?Flush@HDCache@@AAAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?GetAlbumCmp@NavListItemSortCmp@@UBAPBVAlbumCmp@@XZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?GetArtistCmp@NavListItemSortCmp@@UBAPBVArtistCmp@@XZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?GetBaseFileName@SongInfoCopy@@UBAPBDXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?GetBufferSize@HttpGet@@QAAIXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?GetChallengeScoreCmp@NavListItemSortCmp@@UBAPBVChallengeScoreCmp@@XZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?GetColor@UIColor@@QBAABVColor@Hmx@@XZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?GetCores@SongInfoCopy@@UBAABV?$vector@HV?$StlNodeAlloc@H@stlpmtx_std@@@stlpmtx_std@@XZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?GetDateCmp@NavListItemSortCmp@@UBAPBVDateCmp@@XZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?GetDecadeCmp@NavListItemSortCmp@@UBAPBVDecadeCmp@@XZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?GetDifficultyCmp@NavListItemSortCmp@@UBAPBVDifficultyCmp@@XZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?GetFitnessCalorieSortCmp@NavListItemSortCmp@@UBAPBVFitnessCalorieSortCmp@@XZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?GetLocationCmp@NavListItemSortCmp@@UBAPBVLocationCmp@@XZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?GetMQSongCharCmp@NavListItemSortCmp@@UBAPBVMQSongCharCmp@@XZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?GetName@SongInfoCopy@@UBA?AVSymbol@@XZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?GetNumRestarts@Game@@QBAHXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?GetPans@SongInfoCopy@@UBAABV?$vector@MV?$StlNodeAlloc@M@stlpmtx_std@@@stlpmtx_std@@XZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?GetPlaylistTypeCmp@NavListItemSortCmp@@UBAPBVPlaylistTypeCmp@@XZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?GetSlipOffset@StreamReceiverFile@@UAAMXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?GetVenueCmp@NavListItemSortCmp@@UBAPBVVenueCmp@@XZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?GetVocalPartsCmp@NavListItemSortCmp@@UBAPBVVocalPartsCmp@@XZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?Handle@BustAMoveData@@UAA?AVDataNode@@PAVDataArray@@_N@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?Handle@CharMeshHide@@UAA?AVDataNode@@PAVDataArray@@_N@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?Handle@OvershellSlot@@UAA?AVDataNode@@PAVDataArray@@_N@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?Handle@UIListArrow@@UAA?AVDataNode@@PAVDataArray@@_N@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?Handle@UIListSlot@@UAA?AVDataNode@@PAVDataArray@@_N@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?Handle@UIListSubList@@UAA?AVDataNode@@PAVDataArray@@_N@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?Highlight@Waypoint@@UAAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?InsertBreak@RndConsole@@QAAXPAVDataArray@@H@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?IsDifficultyUnlockedForProfile@HamProfile@@QAA_NVSymbol@@0@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?IsLoaded@?$ObjDirPtr@VObjectDir@@@@QBA_NXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?JointToVertexData@?A0x790ae044@@YAXAAVVector3@@ABVSkeleton@@W4SkeletonJoint@@ABVVector4@@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?OnMsg@HamUI@@IAA?AVDataNode@@ABVConnectionStatusChangedMsg@@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?OnSelect@NgPostProc@@UAAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?OnSync@RndMesh@@UAAXH@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?OnUnselect@NgPostProc@@UAAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?PresyncBitmap@RndTex@@UAAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?RemoveFromLists@Spotlight@@SAXPAV1@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?RootTrans@UIListSubList@@UAAPAVRndTransformable@@XZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?SpewInit@@YAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?SpewTerminate@@YAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?SyncBitmap@RndTex@@UAAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?TerminateMakeString@@YAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?ValidateCRC@CRC@Hmx@@SA_NHPBD@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?VertexToWorld@?A0x790ae044@@YAXAAVVector3@@ABVTransform@@MABVVector4@@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?altCfg@@YA?AVDataArrayPtr@@VDataNode@@0@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?merged_82610090@@YAPBDPBDPCH@Z=__link_glue_noop")

// -- BinStream operator<< template instantiations --

// -- C runtime / third-party library symbols --
#pragma comment(linker, "/ALTERNATENAME:Curl_if2ip=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:FD_SET=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:FFTRealForward=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:HIBYTE=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:JoypadSetActuatorsImp=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:LOBYTE=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:MAKEWORD=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:_close=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:_fstati64=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:htons=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:hypot=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:ntohs=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:read=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:strncasecmp=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:wmemcpy=__link_glue_noop")

// -- BinStream operator<< non-template targets (decomp compiler ALTERNATENAME chains) --
#pragma comment(linker, "/ALTERNATENAME:??6@YAAAVBinStream@@AAV0@ABV?$ObjOwnerPtr@VRndCamAnim@@@@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??6@YAAAVBinStream@@AAV0@ABV?$ObjOwnerPtr@VRndLight@@@@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??6@YAAAVBinStream@@AAV0@ABV?$ObjOwnerPtr@VRndMesh@@@@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??6@YAAAVBinStream@@AAV0@ABV?$ObjOwnerPtr@VRndTransformable@@@@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??6@YAAAVBinStream@@AAV0@ABV?$ObjOwnerPtr@VRndWind@@@@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??6@YAAAVBinStream@@AAV0@ABV?$ObjOwnerPtr@VSpotlight@@@@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??6@YAAAVBinStream@@AAV0@ABV?$ObjPtrList@VCharPollable@@VObjectDir@@@@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??6@YAAAVBinStream@@AAV0@ABV?$ObjPtrList@VCharWeightSetter@@VObjectDir@@@@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??6@YAAAVBinStream@@AAV0@ABV?$ObjPtrList@VFader@@VObjectDir@@@@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??6@YAAAVBinStream@@AAV0@ABV?$ObjPtrList@VRndDrawable@@VObjectDir@@@@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??6@YAAAVBinStream@@AAV0@ABV?$ObjPtrList@VRndLight@@VObjectDir@@@@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??6@YAAAVBinStream@@AAV0@ABV?$ObjPtrList@VWaypoint@@VObjectDir@@@@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??6@YAAAVBinStream@@AAV0@ABV?$ObjPtrVec@VFlowNode@@VObjectDir@@@@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??6@YAAAVBinStream@@AAV0@ABV?$ObjPtrVec@VRndTransformable@@VObjectDir@@@@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??6@YAAAVBinStream@@AAV0@ABV?$ObjPtrVec@VWaypoint@@VObjectDir@@@@@Z=__link_glue_noop")

// -- Dynamic initializers (??__E) needed by auto_08_82F05C00_data.obj --
// These ??__E symbols are referenced from the CRT __xc_a section but their
// defining TUs are NonMatching split objects that lack the definitions.
#pragma comment(linker, "/ALTERNATENAME:??__E?gThreadAchievements@Achievements@@0V?$vector@UXUSER_ACHIEVEMENT@@V?$StlNodeAlloc@UXUSER_ACHIEVEMENT@@@stlpmtx_std@@@stlpmtx_std@@A@@YAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??__E?kServerVer@RockCentral@@0VString@@B@@YAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??__E?mAssocMicXbox@ExternalMicClientMgr@@0V?$vector@PAVMicXbox@@V?$StlNodeAlloc@PAVMicXbox@@@stlpmtx_std@@@stlpmtx_std@@A@@YAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??__E?mDevToMicMaster@ExternalMicClientMgr@@0V?$vector@KV?$StlNodeAlloc@K@stlpmtx_std@@@stlpmtx_std@@A@@YAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??__E?mMicMasterToDev@ExternalMicClientMgr@@0V?$vector@KV?$StlNodeAlloc@K@stlpmtx_std@@@stlpmtx_std@@A@@YAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??__E?mMicMasters@ExternalMicClientMgr@@0V?$vector@PAVExternalMicClientProxy@@V?$StlNodeAlloc@PAVExternalMicClientProxy@@@stlpmtx_std@@@stlpmtx_std@@A@@YAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??__E?m_regProps@?$CSampleXAPOBase@VCompressionEffect@@UParams@1@@ATG@@0UXAPO_REGISTRATION_PROPERTIES@@A@@YAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??__E?sBloom@NgPostProc@@1V?$BloomTextures@$02@1@A@@YAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??__E?sCritSec@SkeletonUpdateHandle@@0VCriticalSection@@A@@YAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??__E?sCurrentExportEvent@MsgSinks@@0VSymbol@@A@@YAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??__E?sFactories@Object@Hmx@@0V?$map@VSymbol@@P6APAVObject@Hmx@@XZU?$less@VSymbol@@@stlpmtx_std@@V?$StlNodeAlloc@U?$pair@$$CBVSymbol@@P6APAVObject@Hmx@@XZ@stlpmtx_std@@@5@@stlpmtx_std@@A@@YAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??__E?sID@Matrix2@Hmx@@0V12@A@@YAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??__E?sID@Matrix3@Hmx@@0V12@A@@YAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??__E?sID@Matrix4@Hmx@@0V12@A@@YAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??__E?sID@Transform@@0V1@A@@YAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??__E?sNull@FilePath@@0V1@A@@YAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??__E?sOverlays@RndOverlay@@0V?$list@PAVRndOverlay@@V?$StlNodeAlloc@PAVRndOverlay@@@stlpmtx_std@@@stlpmtx_std@@A@@YAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??__E?sParsers@MidiParser@@0V?$list@PAVMidiParser@@V?$StlNodeAlloc@PAVMidiParser@@@stlpmtx_std@@@stlpmtx_std@@A@@YAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??__E?sPollables@SynthPollable@@0V?$list@PAVSynthPollable@@V?$StlNodeAlloc@PAVSynthPollable@@@stlpmtx_std@@@stlpmtx_std@@A@@YAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??__E?sProxyPool@RndMultiMesh@@1V?$list@U?$pair@PAVRndMultiMeshProxy@@H@stlpmtx_std@@V?$StlNodeAlloc@U?$pair@PAVRndMultiMeshProxy@@H@stlpmtx_std@@@2@@stlpmtx_std@@A@@YAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??__E?sRand@Rand@@2V1@A@@YAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??__E?sRemapClipReplace@SkeletonClip@@2VString@@A@@YAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??__E?sRemapClipSearch@SkeletonClip@@2VString@@A@@YAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??__E?sRoot@FilePath@@0V1@A@@YAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??__E?sSingleton@RndVelocityBuffer@@0V1@A@@YAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??__E?sSlowFrameTimer@Timer@@0V1@A@@YAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??__E?sTimers@AutoTimer@@0V?$list@U?$pair@VTimer@@VTimerStats@@@stlpmtx_std@@V?$StlNodeAlloc@U?$pair@VTimer@@VTimerStats@@@stlpmtx_std@@@2@@stlpmtx_std@@A@@YAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??__E?sUpVectorSmoother@SkeletonFrame@@2VVector3DESmoother@@A@@YAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??__E?smNestedStartTimes@GlitchPoker@@0V?$vector@MV?$StlNodeAlloc@M@stlpmtx_std@@@stlpmtx_std@@A@@YAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??__ETheDebug@@YAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??__ETheGlitchFinder@@YAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??__ETheHDCache@@YAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??__ETheHamSongMgr@@YAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??__ETheHamUI@@YAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??__ETheKnownIssues@@YAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??__ETheLoadMgr@@YAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??__ETheLocale@@YAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??__ETheMemcardMgr@@YAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??__ETheOSCMessenger@@YAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??__EThePlatformMgr@@YAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??__EThePresenceMgr@@YAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??__ETheProfileMgr@@YAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??__ETheRockCentral@@YAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??__ETheSystemArgs@@YAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??__ETheVirtualKeyboard@@YAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??__EgArkFiles@?A0x7f36a62b@@YAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??__EgCaches@@YAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??__EgChecksumData@@YAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??__EgContextRand@?A0x24773155@@YAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??__EgCrit@?A0x49b544a7@@YAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??__EgCritSection@?A0xf503845b@@YAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??__EgDataFuncs@@YAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??__EgDataPointMgr@@YAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??__EgDataProcessedEvt@?A0x7ea4e606@@YAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??__EgDataReadyEvt@?A0x7ea4e606@@YAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??__EgDataThisPtr@@YAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??__EgDataVars@@YAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??__EgDecompressionCritSec@?A0x7ea4e606@@YAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??__EgDecompressionQueue@?A0x7ea4e606@@YAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??__EgDefaultBeatMap@@YAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??__EgDefaultTempoMap@@YAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??__EgDeferredAwardQueue@?A0xf8e4b4b5@@YAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??__EgDingoSvrXbox@@YAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??__EgEntries@@YAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??__EgEvalNode@@YAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??__EgExternalArkFiles@?A0x7f36a62b@@YAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??__EgGamePanelCallback@@YAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??__EgHashTable@@YAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??__EgHiResScreen@@YAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??__EgHolmesTarget@?A0x49b544a7@@YAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??__EgInput@?A0x49b544a7@@YAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??__EgJoypadData@?A0xca10770b@@YAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??__EgLastCachedResource@@YAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??__EgLoopVizCallback@@YAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??__EgMacroTable@@YAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??__EgMics@?A0x0c39da7f@@YAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??__EgNotifies@@YAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??__EgNotifyThreadSec@@YAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??__EgNotifyThreadSync@@YAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??__EgOverride@@YAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??__EgPatchVerts@@YAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??__EgPhysicalType@?A0x2be09a71@@YAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??__EgPristineSystemArgs@@YAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??__EgProfile@?A0x49b544a7@@YAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??__EgPropPaths@@YAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??__EgRequests@?A0x49b544a7@@YAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??__EgServerName@?A0x49b544a7@@YAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??__EgSinks@@YAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??__EgSystemLanguage@@YAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??__EgSystemLocale@@YAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??__EgSystemTimer@@YAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??__EgTiers@?A0xf8e4b4b5@@YAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??__EgTransListAlloc@@YAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??__EgUnlockables@?A0xf8e4b4b5@@YAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??__EgUsedContexts@?A0x24773155@@YAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??__EgUseLowestMipExceptions@@YAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??__EgVarStack@@YAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??__EgVoiceGC@@YAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??__EgWavFileCacheHelper@@YAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??__EgWavMgr@@YAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??__EgWebSvcMgr@@YAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??__EgInProgressSyncVoices@@YAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??__EgInProgressVoices@@YAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??__EgLockPendingLists@@YAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??__EgMemLogType@@YAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??__EgMemTrackSourceFile@@YAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??__EgMemTrackSourceObject@@YAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??__EgNewReaders@?A0xcb0871ef@@YAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??__EgNoPartOverride@@YAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??__EgOldChars@@YAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??__EgPendingSyncVoices@@YAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??__EgPendingVoices@@YAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??__EgReaders@?A0xcb0871ef@@YAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??__EgLock@?A0xcb0871ef@@YAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??__EgDebugGraphs@?A0xb39b74bf@@YAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??__EjobQueueMutex@JobQueue@@YAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??__EkListChunkID@@YAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??__EkMidiChunkID@@YAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??__EkMidiHeaderChunkID@@YAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??__EkMidiTrackChunkID@@YAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??__EkRiffChunkID@@YAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??__EkWaveAdditionalChunkID@@YAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??__EkWaveChunkID@@YAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??__EkWaveCueChunkID@@YAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??__EkWaveDataChunkID@@YAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??__EkWaveFactChunkID@@YAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??__EkWaveFormatChunkID@@YAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??__EkWaveInstChunkID@@YAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??__EkWaveLabelChunkID@@YAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??__EkWaveSampleChunkID@@YAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??__EkWaveTextChunkID@@YAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??__EmCampaignVO@@YAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??__Es_voiceGC@@YAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??__Es_voiceGCInProgress@@YAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??__EsAutoplayStates@@YAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??__EsCam@@YAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??__EsCamFrame@@YAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??__EsCollisionUsefulBoneNames@@YAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??__EsConditionalTimersEnabled@@YAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??__EsDefaultRatingThresholds@@YAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??__EsFakes@@YAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??__EsFlipYZ@@YAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??__EsFrames@@YAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??__EsIdentityXfm@?A0x8e417309@@YAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??__EsLoadedFile@@YAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??__EsRand@@YAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??__EsRatingStates@@YAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??__EsShaderTypes@@YAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??__E?sLights@SpotlightDrawer@@1V?$vector@VSpotlightEntry@SpotlightDrawer@@V?$StlNodeAlloc@VSpotlightEntry@SpotlightDrawer@@@stlpmtx_std@@@stlpmtx_std@@A@@YAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??__E?sCans@SpotlightDrawer@@1V?$vector@VSpotMeshEntry@SpotlightDrawer@@V?$StlNodeAlloc@VSpotMeshEntry@SpotlightDrawer@@@stlpmtx_std@@@stlpmtx_std@@A@@YAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??__E?sShadowSpots@SpotlightDrawer@@1V?$vector@PAVSpotlight@@V?$StlNodeAlloc@PAVSpotlight@@@stlpmtx_std@@@stlpmtx_std@@A@@YAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??__E?sManualEvents@LightPreset@@1V?$deque@U?$pair@W4KeyframeCmd@LightPreset@@M@stlpmtx_std@@V?$StlNodeAlloc@U?$pair@W4KeyframeCmd@LightPreset@@M@stlpmtx_std@@@2@@stlpmtx_std@@A@@YAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??__E?m_regProps@?$CSampleXAPOBase@VBitCrushEffect@@UParams@1@@ATG@@0UXAPO_REGISTRATION_PROPERTIES@@A@@YAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??__E?m_regProps@?$CSampleXAPOBase@VDistortionEffect@@UParams@1@@ATG@@0UXAPO_REGISTRATION_PROPERTIES@@A@@YAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??__E?m_regProps@?$CSampleXAPOBase@VDelayEffect@@UParams@1@@ATG@@0UXAPO_REGISTRATION_PROPERTIES@@A@@YAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??__E?m_regProps@?$CSampleXAPOBase@VFlangerEffect@@UParams@1@@ATG@@0UXAPO_REGISTRATION_PROPERTIES@@A@@YAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??__E?m_regProps@?$CSampleXAPOBase@VEQEffect@@UParams@1@@ATG@@0UXAPO_REGISTRATION_PROPERTIES@@A@@YAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??__E?m_regProps@?$CSampleXAPOBase@VWahEffect@@UParams@1@@ATG@@0UXAPO_REGISTRATION_PROPERTIES@@A@@YAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??__E?m_regProps@?$CSampleXAPOBase@VMeterEffect@@UMeterEffectParams@@@ATG@@0UXAPO_REGISTRATION_PROPERTIES@@A@@YAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??__EtCritSection@?A0x439b694a@@YAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:except_data_82918780=__link_glue_zero")

// ============================================================================
// Additional stubs for remaining unresolved symbols
// Generated from link error analysis
// ============================================================================

// -- Dynamic initializers (76 symbols) --
#pragma comment(linker, "/ALTERNATENAME:??__E?kMThd@MidiChunkID@@2V1@B@@YAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??__E?kMTrk@MidiChunkID@@2V1@B@@YAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??__E?mTimer@UsbMidiGuitar@@0VTimer@@A@@YAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??__E?mXLSPRefCountMap@XLSPConnection@@2V?$map@KHU?$less@K@stlpmtx_std@@V?$StlNodeAlloc@U?$pair@$$CBKH@stlpmtx_std@@@2@@stlpmtx_std@@A@@YAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??__E?m_regProps@?$CSampleXAPOBase@VEnvelopeGenerator@@UEnvelopeGeneratorParams@@@ATG@@0UXAPO_REGISTRATION_PROPERTIES@@A@@YAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??__E?m_regProps@?$CSampleXAPOBase@VGainEffect@@UGainEffectParams@@@ATG@@0UXAPO_REGISTRATION_PROPERTIES@@A@@YAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??__E?m_regProps@?$CSampleXAPOBase@VHeadsetPlaybackEffect@@UHeadsetPlaybackEffectParams@@@ATG@@0UXAPO_REGISTRATION_PROPERTIES@@A@@YAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??__E?m_regProps@?$CSampleXAPOBase@VHeadsetXferEffect@@UHeadsetXferEffectParams@@@ATG@@0UXAPO_REGISTRATION_PROPERTIES@@A@@YAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??__E?m_regProps@?$CSampleXAPOBase@VPitchShiftEffect@@UPitchShiftEffectParams@@@ATG@@0UXAPO_REGISTRATION_PROPERTIES@@A@@YAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??__E?m_regProps@?$CSampleXAPOBase@VSynapseAPO@DSP@@USynapseAPOParams@2@@ATG@@0UXAPO_REGISTRATION_PROPERTIES@@A@@YAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??__E?sActiveMovies@BinkMovieImpl@@0V?$vector@PAVBinkMovieImpl@@V?$StlNodeAlloc@PAVBinkMovieImpl@@@stlpmtx_std@@@stlpmtx_std@@A@@YAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??__E?sBlacklightPacketPool@RndText@@1V?$vector@VBlacklightPacket@RndText@@V?$StlNodeAlloc@VBlacklightPacket@RndText@@@stlpmtx_std@@@stlpmtx_std@@A@@YAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??__E?sCache@HamCamShot@@1V?$list@UTargetCache@HamCamShot@@V?$StlNodeAlloc@UTargetCache@HamCamShot@@@stlpmtx_std@@@stlpmtx_std@@A@@YAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??__E?sFacingPos@FacingSet@CharClip@@2UFacingBones@12@A@@YAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??__E?sFacingRotAndPos@FacingSet@CharClip@@2UFacingBones@12@A@@YAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??__E?sFilterVersions@MoveDir@@0V?$vector@PAVFilterVersion@@V?$StlNodeAlloc@PAVFilterVersion@@@stlpmtx_std@@@stlpmtx_std@@A@@YAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??__E?sFontMapCache@RndText@@1V?$list@PAVFontMapBase@RndText@@V?$StlNodeAlloc@PAVFontMapBase@RndText@@@stlpmtx_std@@@stlpmtx_std@@A@@YAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??__E?sGlobalLighting@RndEnviron@@1VBoxMapLighting@@A@@YAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??__E?sInterpMessage@PropKeys@@2VMessage@@A@@YAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??__E?sListStateMaxDisplay@HamNavList@@0HB@@YAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??__E?sMemPointMap@DirLoader@@0V?$map@VString@@UMemPointDelta@@U?$less@VString@@@stlpmtx_std@@V?$StlNodeAlloc@U?$pair@$$CBVString@@UMemPointDelta@@@stlpmtx_std@@@4@@stlpmtx_std@@A@@YAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??__ETheBlockMgr@@YAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??__ETheCharDebug@@YAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??__ETheDxRnd@@YAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??__ETheDxShaderMgr@@YAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??__ETheDxTexMgr@@YAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??__ETheMC@@YAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??__ETheSongSequence@@YAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??__ETheTaskMgr@@YAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??__EgAllTextures@@YAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??__EgBinkMovieSys@@YAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??__EgCatPriority@@YAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??__EgChildPolys@@YAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??__EgClients@?A0x831dd776@@YAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??__EgConditional@@YAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??__EgContentMgr@@YAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??__EgCrit@@YAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??__EgCritSection@@YAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??__EgDataReadCrit@@YAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??__EgDirList@@YAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??__EgFile@@YAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??__EgFiles@@YAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??__EgIgnoredContent@@YAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??__EgMoveMergeMap@@YAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??__EgOfflineCallback@@YAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??__EgParentPolys@@YAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??__EgPhysicsVolumeBox@?A0x5ba00aca@@YAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??__EgPreloaded@?A0xf8b42a02@@YAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??__EgQueue@@YAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??__EgReadFiles@@YAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??__EgReadTime@@YAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??__EgResourceFileCacheHelper@@YAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??__EgShaderDepthVolume@@YAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??__EgShaderDrawRect@@YAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??__EgShaderFur@@YAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??__EgShaderMultimesh@@YAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??__EgShaderParticles@@YAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??__EgShaderPostProc@@YAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??__EgShaderSimple@@YAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??__EgShaderStandard@@YAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??__EgShaderSyncTrack@@YAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??__EgShaderUnwrapUV@@YAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??__EgShaderVelocity@@YAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??__EgShaderVelocityCamera@@YAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??__EkConvLen@?A0x5c754947@@YAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??__EmFriendEnumRequests@?A0x8a9ffbf2@@YAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??__EmServiceIdMap@?A0x8a9ffbf2@@YAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??__EmTime@?A0x8a9ffbf2@@YAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??__EsFilePaths@@YAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??__EsFiles@@YAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??__EsKeyReplace@@YAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??__EsLastComparedDancerSkel@@YAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??__EsLicense@@YAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??__EsOverlayWidth@?A0xe50ea9df@@YAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??__EsSuperClassMap@@YAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??__EsWarnings@@YAXXZ=__link_glue_noop")

// -- Audio SDK (11 symbols) --
#pragma comment(linker, "/ALTERNATENAME:?GetLatency@CXboxRendererConnection@LEAPCORE@@UBAIXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?GetName@CGMClassifier@NUISPEECH@@UBAPB_WXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?GetStringKey@CFEModuleDef@NUISPEECH@@QBAPB_WXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?Release@?$CComContainedObject@VCTextNormMultiResult@NUISPEECH@@@ATL@NUISPEECH@@UAAKXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?SyncEffectParams@FxSendChorus360@@UBAXPAUIXAudio2SubmixVoice@@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?SyncEffectParams@FxSendDelay360@@UBAXPAUIXAudio2SubmixVoice@@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?SyncEffectParams@FxSendEQ360@@UBAXPAUIXAudio2SubmixVoice@@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?SyncEffectParams@FxSendFlanger360@@UBAXPAUIXAudio2SubmixVoice@@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?SyncEffectParams@FxSendReverb360@@UBAXPAUIXAudio2SubmixVoice@@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?SyncEffectParams@FxSendWah360@@UBAXPAUIXAudio2SubmixVoice@@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?gathering@CUgtFilter@NUISPEECH@@QAA_NXZ=__link_glue_noop")

// -- String COMDATs (12 symbols) --
#pragma comment(linker, "/ALTERNATENAME:??_C@_03OBJFJEBA@any?$AA@=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:??_C@_0CL@FCAICFOO@e?3?2lazer_build_gmc1?2system?2src?2m@=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:??_C@_0CM@OKDJEAIK@e?3?2lazer_build_gmc1?2system?2src?2u@=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:??_C@_0CN@GGKIFLBP@e?3?2lazer_build_gmc1?2system?2src?2m@=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:??_C@_0CO@HIBDCBHF@e?3?2lazer_build_gmc1?2system?2src?2u@=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:??_C@_0CO@JEAIEAJK@e?3?2lazer_build_gmc1?2system?2src?2u@=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:??_C@_0CO@JOPGEACP@e?3?2lazer_build_gmc1?2system?2src?2u@=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:??_C@_0CP@NOKFNCAK@e?3?2lazer_build_gmc1?2system?2src?2u@=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:??_C@_0DA@HMFHGDBI@e?3?2lazer_build_gmc1?2system?2src?2u@=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:??_C@_0DG@KLPECOLD@e?3?2lazer_build_gmc1?2system?2src?2s@=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:??_C@_15IMCJNNE@?$CF?$AAd?$AA?$AA?$AA@=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:??_C@_17EJHNJINI@?4?$AA?4?$AA?4?$AA?$AA?$AA@=__link_glue_zero")

// -- Data labels (123 symbols) --
#pragma comment(linker, "/ALTERNATENAME:lbl_82007E00=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_82008206=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_8200820C=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_82008301=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_82009204=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_82009207=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_8200FDFE=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_8200FF00=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_82010C04=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_82029200=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_82039104=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_8203910A=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_82039204=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_82039205=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_8203FCFC=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_82040080=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_82048414=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_82049105=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_82058414=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_8205FE0E=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_8206FCFE=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_82082080=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_82088200=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_82088408=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_82098414=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_82098424=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_820AA41C=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_820B0C04=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_8225F7FF=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_82FC14A2=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_83000000=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_8300004E=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_83000080=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_8300009C=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_830000BB=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_830000F0=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_830000FA=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_83000138=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_83000177=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_830001F4=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_83000200=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_830002EA=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_830002EE=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_83000300=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_830003E8=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_83000400=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_83000500=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_830005D8=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_830005DC=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_83000600=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_83000700=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_830009C4=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_83000A00=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_83000BB8=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_83000C04=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_83000F00=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_83001194=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_83001388=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_83002100=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_83008001=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_83008002=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_83008201=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_83008301=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_83008302=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_83008303=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_83008304=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_83008307=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_8300830C=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_83009001=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_83009101=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_83009F00=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_8300A201=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_8300F300=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_8300FEFE=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_83010000=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_83010005=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_83010008=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_8301000E=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_8301000F=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_83010C04=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_83018202=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_83018203=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_83018302=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_8301A002=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_8301FEFE=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_8301FFFD=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_830200C0=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_83020C04=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_83028103=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_83028200=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_83028203=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_8302820B=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_83029103=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_83030C04=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_83038000=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_83038104=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_83038204=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_83039101=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_83039104=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_83039204=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_83040080=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_83040C04=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_83058106=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_83058306=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_83059106=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_8305FFFF=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_83068307=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_83069107=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_8306911B=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_8306FBFF=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_8306FFFE=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_83073162=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_8307840C=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_83079101=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_8307FFF9=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_83080080=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_83088209=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_83088309=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_83088410=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_83089109=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_8308911C=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_8308FD1B=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_83168300=__link_glue_zero")

// -- Float constants (6 symbols) --
#pragma comment(linker, "/ALTERNATENAME:__real@3b000000=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:__real@3d800000=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:__real@3db851ec=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:__real@3e0f5c29=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:__real@3e400000=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:__real@3fa00000=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:__real@43b40000=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:__real@477fff00=__link_glue_zero")

// -- Exception/unwind data (4 symbols) --
#pragma comment(linker, "/ALTERNATENAME:__unwind$120075=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:__unwind$221726=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:except_record_8240C740=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:except_record_8250C4B0=__link_glue_zero")

// -- STL template instantiations (28 symbols) --
#pragma comment(linker, "/ALTERNATENAME:??$_Copy_Construct@U?$pair@$$CBVString@@I@stlpmtx_std@@@stlpmtx_std@@YAXPAU?$pair@$$CBVString@@I@0@ABU10@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??$_Destroy_Range@PAULevelData@@@stlpmtx_std@@YAXPAULevelData@@0@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??$_M_find@H@?$_Rb_tree@HU?$less@H@stlpmtx_std@@U?$pair@$$CBHVSongStatus@@@2@U?$_Select1st@U?$pair@$$CBHVSongStatus@@@stlpmtx_std@@@2@U?$_MapTraitsT@U?$pair@$$CBHVSongStatus@@@stlpmtx_std@@@priv@2@V?$StlNodeAlloc@U?$pair@$$CBHVSongStatus@@@stlpmtx_std@@@2@@stlpmtx_std@@ABAPAU_Rb_tree_node_base@1@ABH@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??$_M_find@VString@@@?$_Rb_tree@VString@@U?$less@VString@@@stlpmtx_std@@U?$pair@$$CBVString@@VChallengeBadgeInfo@@@3@U?$_Select1st@U?$pair@$$CBVString@@VChallengeBadgeInfo@@@stlpmtx_std@@@3@U?$_MapTraitsT@U?$pair@$$CBVString@@VChallengeBadgeInfo@@@stlpmtx_std@@@priv@3@V?$StlNodeAlloc@U?$pair@$$CBVString@@VChallengeBadgeInfo@@@stlpmtx_std@@@3@@stlpmtx_std@@ABAPAU_Rb_tree_node_base@1@ABVString@@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??$_M_find@VSymbol@@@?$_Rb_tree@VSymbol@@U?$less@VSymbol@@@stlpmtx_std@@U?$pair@$$CBVSymbol@@_N@3@U?$_Select1st@U?$pair@$$CBVSymbol@@_N@stlpmtx_std@@@3@U?$_MapTraitsT@U?$pair@$$CBVSymbol@@_N@stlpmtx_std@@@priv@3@V?$StlNodeAlloc@U?$pair@$$CBVSymbol@@_N@stlpmtx_std@@@3@@stlpmtx_std@@ABAPAU_Rb_tree_node_base@1@ABVSymbol@@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??1?$pair@$$CBVString@@I@stlpmtx_std@@QAA@XZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??4exception@std@@QAAAAV01@ABV01@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?Apply3DCharXfm@WorldCrowd@@QAAXABU?$_List_iterator@UCharData@WorldCrowd@@U?$_Nonconst_traits@UCharData@WorldCrowd@@@stlpmtx_std@@@stlpmtx_std@@HPAVRndCam@@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?EnqueueDetectFrames@MoveDir@@QAAXMHAAV?$vector@VDetectFrame@@V?$StlNodeAlloc@VDetectFrame@@@stlpmtx_std@@@stlpmtx_std@@PBVFilterVersion@@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?EnumerateFriends@PlatformMgr@@QAAXHAAV?$vector@PAVFriend@@V?$StlNodeAlloc@PAVFriend@@@stlpmtx_std@@@stlpmtx_std@@PAVObject@Hmx@@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?GetOfferIDsToEnumerate@HamStorePanel@@UBAXAAV?$vector@_KV?$StlNodeAlloc@_K@stlpmtx_std@@@stlpmtx_std@@_N@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?GetTracks@SongInfoCopy@@UBAABV?$vector@UTrackChannels@@V?$StlNodeAlloc@UTrackChannels@@@stlpmtx_std@@@stlpmtx_std@@XZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?ListPollChildren@CharEyes@@UBAXAAV?$list@PAVRndPollable@@V?$StlNodeAlloc@PAVRndPollable@@@stlpmtx_std@@@stlpmtx_std@@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?Mats@RndParticleSys@@UAAXAAV?$list@PAVRndMat@@V?$StlNodeAlloc@PAVRndMat@@@stlpmtx_std@@@stlpmtx_std@@_N@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?Mats@WorldCrowd@@UAAXAAV?$list@PAVRndMat@@V?$StlNodeAlloc@PAVRndMat@@@stlpmtx_std@@@stlpmtx_std@@_N@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?PollDeps@CharEyes@@UAAXAAV?$list@PAVObject@Hmx@@V?$StlNodeAlloc@PAVObject@Hmx@@@stlpmtx_std@@@stlpmtx_std@@0@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?RenderShadows@NgLight@@MAAXAAV?$vector@PAVRndDrawable@@V?$StlNodeAlloc@PAVRndDrawable@@@stlpmtx_std@@@stlpmtx_std@@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?SelectChildren@NavListHeaderNode@@UAA?AVSymbol@@AAV?$list@PAVNavListSortNode@@V?$StlNodeAlloc@PAVNavListSortNode@@@stlpmtx_std@@@stlpmtx_std@@H@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?Set3DCharList@WorldCrowd@@QAAXABV?$vector@U?$pair@HH@stlpmtx_std@@V?$StlNodeAlloc@U?$pair@HH@stlpmtx_std@@@2@@stlpmtx_std@@PAVObject@Hmx@@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?Set3DCharXfm@WorldCrowd@@QAAXABU?$_List_iterator@UCharData@WorldCrowd@@U?$_Nonconst_traits@UCharData@WorldCrowd@@@stlpmtx_std@@@stlpmtx_std@@HABVTransform@@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?Sort@CharPollableSorter@@QAAXAAV?$vector@PAVRndPollable@@V?$StlNodeAlloc@PAVRndPollable@@@stlpmtx_std@@@stlpmtx_std@@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?UpdateOffers@HamStorePanel@@MAA?AW4StoreError@@ABV?$list@UEnumProduct@@V?$StlNodeAlloc@UEnumProduct@@@stlpmtx_std@@@stlpmtx_std@@_N@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?Visualize@SkeletonViz@@QAAXABVCameraInput@@ABVBaseSkeleton@@PAV?$vector@PAVSkeletonCallback@@V?$StlNodeAlloc@PAVSkeletonCallback@@@stlpmtx_std@@@stlpmtx_std@@_N@Z=__link_glue_noop")

// -- MakeString instantiations (4 symbols) --
#pragma comment(linker, "/ALTERNATENAME:??$MakeString@$$BY0BH@$$CBDHPBD@@YAPBDPBDAAY0BH@$$CBDABHABQBD@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??$MakeString@HW4State@SaveLoadManager@@@@YAPBDPBDABHABW4State@SaveLoadManager@@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??$MakeString@PBD$$BY0BE@$$CBDHG@@YAPBDPBDABQBDAAY0BE@$$CBDABHABG@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??$MakeString@W4BlendEaseMode@CamShotFrame@@@@YAPBDPBDABW4BlendEaseMode@CamShotFrame@@@Z=__link_glue_noop")

// -- ObjPtr/ObjPtrVec template instantiations (72 symbols) --
#pragma comment(linker, "/ALTERNATENAME:??$?6VEventTrigger@@@@YAAAVBinStream@@AAV0@ABV?$ObjPtrList@VEventTrigger@@VObjectDir@@@@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??$?6VHamCamShot@@@@YAAAVBinStream@@AAV0@ABV?$ObjPtrList@VHamCamShot@@VObjectDir@@@@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??$?6VRhythmDetector@@@@YAAAVBinStream@@AAV0@ABV?$ObjPtrVec@VRhythmDetector@@VObjectDir@@@@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??$?6VRndMat@@@@YAAAVBinStream@@AAV0@ABV?$ObjPtrList@VRndMat@@VObjectDir@@@@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??$?6VSequence@@@@YAAAVBinStream@@AAV0@ABV?$ObjPtrList@VSequence@@VObjectDir@@@@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??$PropSync@VRndTransformable@@@@YA_NAAV?$ObjPtrVec@VRndTransformable@@VObjectDir@@@@AAVDataNode@@PAVDataArray@@HW4PropOp@@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?Link@?$ObjPtrList@VCamShot@@VObjectDir@@@@AAAXViterator@1@PAUNode@1@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?Link@?$ObjPtrList@VEventTrigger@@VObjectDir@@@@AAAXViterator@1@PAUNode@1@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?Link@?$ObjPtrList@VHamCamShot@@VObjectDir@@@@AAAXViterator@1@PAUNode@1@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?Link@?$ObjPtrList@VObjectDir@@V1@@@AAAXViterator@1@PAUNode@1@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?Link@?$ObjPtrList@VRndMat@@VObjectDir@@@@AAAXViterator@1@PAUNode@1@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?Link@?$ObjPtrList@VRndMesh@@VObjectDir@@@@AAAXViterator@1@PAUNode@1@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?Link@?$ObjPtrList@VSequence@@VObjectDir@@@@AAAXViterator@1@PAUNode@1@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?PopClipPlanesInternal@DxRnd@@UAAXAAV?$ObjPtrVec@VRndTransformable@@VObjectDir@@@@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?PushClipPlanesInternal@DxRnd@@UAAXAAV?$ObjPtrVec@VRndTransformable@@VObjectDir@@@@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?RefOwner@?$ObjPtrList@VCamShot@@VObjectDir@@@@EBAPAVObject@Hmx@@XZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?RefOwner@?$ObjPtrList@VCharBone@@VObjectDir@@@@EBAPAVObject@Hmx@@XZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?RefOwner@?$ObjPtrList@VCharCollide@@VObjectDir@@@@EBAPAVObject@Hmx@@XZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?RefOwner@?$ObjPtrList@VCharInterest@@VObjectDir@@@@EBAPAVObject@Hmx@@XZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?RefOwner@?$ObjPtrList@VHamCamShot@@VObjectDir@@@@EBAPAVObject@Hmx@@XZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?RefOwner@?$ObjPtrList@VNoteVoiceInst@@VObjectDir@@@@EBAPAVObject@Hmx@@XZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?RefOwner@?$ObjPtrList@VObjectDir@@V1@@@EBAPAVObject@Hmx@@XZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?RefOwner@?$ObjPtrList@VRndTexBlendController@@VObjectDir@@@@EBAPAVObject@Hmx@@XZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?RefOwner@?$ObjPtrList@VSeqInst@@VObjectDir@@@@EBAPAVObject@Hmx@@XZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?RefOwner@?$ObjPtrList@VSfxInst@@VObjectDir@@@@EBAPAVObject@Hmx@@XZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?RefOwner@?$ObjPtrList@VWorldCrowd@@VObjectDir@@@@EBAPAVObject@Hmx@@XZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?RefOwner@Node@?$ObjPtrList@VCamShot@@VObjectDir@@@@UBAPAVObject@Hmx@@XZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?RefOwner@Node@?$ObjPtrList@VCharBone@@VObjectDir@@@@UBAPAVObject@Hmx@@XZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?RefOwner@Node@?$ObjPtrList@VCharCollide@@VObjectDir@@@@UBAPAVObject@Hmx@@XZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?RefOwner@Node@?$ObjPtrList@VCharInterest@@VObjectDir@@@@UBAPAVObject@Hmx@@XZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?RefOwner@Node@?$ObjPtrList@VHamCamShot@@VObjectDir@@@@UBAPAVObject@Hmx@@XZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?RefOwner@Node@?$ObjPtrList@VNoteVoiceInst@@VObjectDir@@@@UBAPAVObject@Hmx@@XZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?RefOwner@Node@?$ObjPtrList@VObjectDir@@V1@@@UBAPAVObject@Hmx@@XZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?RefOwner@Node@?$ObjPtrList@VRndFontBase@@VObjectDir@@@@UBAPAVObject@Hmx@@XZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?RefOwner@Node@?$ObjPtrList@VRndMat@@VObjectDir@@@@UBAPAVObject@Hmx@@XZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?RefOwner@Node@?$ObjPtrList@VRndTexBlendController@@VObjectDir@@@@UBAPAVObject@Hmx@@XZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?RefOwner@Node@?$ObjPtrList@VSeqInst@@VObjectDir@@@@UBAPAVObject@Hmx@@XZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?RefOwner@Node@?$ObjPtrList@VSfxInst@@VObjectDir@@@@UBAPAVObject@Hmx@@XZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?RefOwner@Node@?$ObjPtrList@VWorldCrowd@@VObjectDir@@@@UBAPAVObject@Hmx@@XZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?RefOwner@Node@?$ObjPtrVec@VCharClip@@VObjectDir@@@@UBAPAVObject@Hmx@@XZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?RefOwner@Node@?$ObjPtrVec@VFlow@@VObjectDir@@@@UBAPAVObject@Hmx@@XZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?RefOwner@Node@?$ObjPtrVec@VFlowLabel@@VObjectDir@@@@UBAPAVObject@Hmx@@XZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?RefOwner@Node@?$ObjPtrVec@VHamCharacter@@VObjectDir@@@@UBAPAVObject@Hmx@@XZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?RefOwner@Node@?$ObjPtrVec@VHamMove@@VObjectDir@@@@UBAPAVObject@Hmx@@XZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?RefOwner@Node@?$ObjPtrVec@VObject@Hmx@@VObjectDir@@@@UBAPAVObject@Hmx@@XZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?RefOwner@Node@?$ObjPtrVec@VRhythmDetector@@VObjectDir@@@@UBAPAVObject@Hmx@@XZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?RefOwner@Node@?$ObjPtrVec@VRndDrawable@@VObjectDir@@@@UBAPAVObject@Hmx@@XZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?RefOwner@Node@?$ObjPtrVec@VRndEnviron@@VObjectDir@@@@UBAPAVObject@Hmx@@XZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?RefOwner@Node@?$ObjPtrVec@VRndGroup@@VObjectDir@@@@UBAPAVObject@Hmx@@XZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?RefOwner@Node@?$ObjPtrVec@VRndLight@@VObjectDir@@@@UBAPAVObject@Hmx@@XZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?RefOwner@Node@?$ObjPtrVec@VRndMat@@VObjectDir@@@@UBAPAVObject@Hmx@@XZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?RefOwner@Node@?$ObjPtrVec@VSpotlight@@VObjectDir@@@@UBAPAVObject@Hmx@@XZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?RefOwner@Node@?$ObjPtrVec@VSpotlightDrawer@@VObjectDir@@@@UBAPAVObject@Hmx@@XZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?Replace@?$ObjPtrList@VCharBone@@VObjectDir@@@@EAA_NPAVObjRef@@PAVObject@Hmx@@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?Replace@?$ObjPtrList@VCharCollide@@VObjectDir@@@@EAA_NPAVObjRef@@PAVObject@Hmx@@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?Replace@?$ObjPtrList@VCharInterest@@VObjectDir@@@@EAA_NPAVObjRef@@PAVObject@Hmx@@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?Replace@?$ObjPtrList@VHamCamShot@@VObjectDir@@@@EAA_NPAVObjRef@@PAVObject@Hmx@@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?Replace@?$ObjPtrList@VNoteVoiceInst@@VObjectDir@@@@EAA_NPAVObjRef@@PAVObject@Hmx@@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?Replace@?$ObjPtrList@VObjectDir@@V1@@@EAA_NPAVObjRef@@PAVObject@Hmx@@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?Replace@?$ObjPtrList@VRndTexBlendController@@VObjectDir@@@@EAA_NPAVObjRef@@PAVObject@Hmx@@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?Replace@?$ObjPtrList@VSeqInst@@VObjectDir@@@@EAA_NPAVObjRef@@PAVObject@Hmx@@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?Replace@?$ObjPtrList@VSfxInst@@VObjectDir@@@@EAA_NPAVObjRef@@PAVObject@Hmx@@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?Replace@?$ObjPtrList@VWorldCrowd@@VObjectDir@@@@EAA_NPAVObjRef@@PAVObject@Hmx@@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?erase@?$ObjPtrList@VCamShot@@VObjectDir@@@@QAA?AViterator@1@V21@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?erase@?$ObjPtrList@VHamCamShot@@VObjectDir@@@@QAA?AViterator@1@V21@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?erase@?$ObjPtrList@VObjectDir@@V1@@@QAA?AViterator@1@V21@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?erase@?$ObjPtrVec@VFlowNode@@VObjectDir@@@@QAA?AViterator@1@V21@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?erase@?$ObjPtrVec@VHamMove@@VObjectDir@@@@QAA?AViterator@1@V21@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?erase@?$ObjPtrVec@VObject@Hmx@@VObjectDir@@@@QAA?AViterator@1@V21@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?erase@?$ObjPtrVec@VRndTex@@VObjectDir@@@@QAA?AViterator@1@V21@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?front@?$ObjPtrList@VNoteVoiceInst@@VObjectDir@@@@QBAPAVNoteVoiceInst@@XZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?remove@?$ObjPtrVec@VFlowNode@@VObjectDir@@@@QAA_NPAVFlowNode@@@Z=__link_glue_noop")

// -- ObjRef/ObjDirPtr template instantiations (11 symbols) --
#pragma comment(linker, "/ALTERNATENAME:??$?6VHamScrollSpeedIndicator@@@@YAAAVBinStream@@AAV0@ABV?$ObjDirPtr@VHamScrollSpeedIndicator@@@@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??$?6VObjectDir@@@@YAAAVBinStream@@AAV0@ABV?$ObjDirPtr@VObjectDir@@@@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??$?6VUIListDir@@@@YAAAVBinStream@@AAV0@ABV?$ObjDirPtr@VUIListDir@@@@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??0?$ObjDirPtr@VObjectDir@@@@QAA@PAVObjectDir@@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?CopyRef@?$ObjRefConcrete@VCharClip@@VObjectDir@@@@QAAXABV1@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?CopyRef@?$ObjRefConcrete@VFlow@@VObjectDir@@@@QAAXABV1@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?CopyRef@?$ObjRefConcrete@VFxSend@@VObjectDir@@@@QAAXABV1@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?CopyRef@?$ObjRefConcrete@VHamMove@@VObjectDir@@@@QAAXABV1@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?CopyRef@?$ObjRefConcrete@VRndFontBase@@VObjectDir@@@@QAAXABV1@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?CopyRef@?$ObjRefConcrete@VSynthSample@@VObjectDir@@@@QAAXABV1@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?IsLoaded@?$ObjDirPtr@VUILabelDir@@@@QBA_NXZ=__link_glue_noop")

// -- BinStream operator instantiations (1 symbols) --
#pragma comment(linker, "/ALTERNATENAME:??5@YAAAVBinStream@@AAV0@AAVFilePath@@@Z=__link_glue_noop")

// -- Game/engine data symbols (17 symbols) --
#pragma comment(linker, "/ALTERNATENAME:?TheChallengeSortMgr@@3PAVChallengeSortMgr@@A=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:?TheContentMgr@@3AAVContentMgr@@A=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:?TheFitnessGoalMgr@@3PAVFitnessGoalMgr@@A=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:?TheLeaderboards@@3PAVLeaderboards@@A=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:?TheMQSongSortMgr@@3PAVMQSongSortMgr@@A=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:?TheMoveMgr@@3PAVMoveMgr@@A=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:?TheMovieSys@@3AAVMovieSys@@A=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:?TheNgRnd@@3AAVNgRnd@@A=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:?TheRnd@@3AAVRnd@@A=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:?TheSkeletonIdentifier@@3PAVSkeletonIdentifier@@A=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:?TheSkeletonViz@@3PAVSkeletonViz@@A=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:?TheSongSortMgr@@3PAVSongSortMgr@@A=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:?gCharHighlightY@@3MA=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:?sBloomLocFactor@RndPostProc@@1MA=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:?sCurrent@SpotlightDrawer@@1PAV1@A=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:?sPathEval@DirLoader@@0P6A_NPBD@ZA=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:?sXShowCallback@PlatformMgr@@2P6A_NAAK@ZA=__link_glue_zero")

// -- Game/engine function stubs (372 symbols) --
#pragma comment(linker, "/ALTERNATENAME:??0?$FlowPtr@VObject@Hmx@@@@QAA@ABV0@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??0MeterEffect@@QAA@XZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??0SingleItemEnumJob@@QAA@PAVObject@Hmx@@H_K@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??1?$StackString@$0IA@@@UAA@XZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??1LabelStyle@UILabel@@QAA@XZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??1PlatformMgr@@UAA@XZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??DHmx@@YA?AVMatrix4@0@ABVTransform@@ABV10@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??RFileMergerSort@@QAA_NPBUMerger@FileMerger@@0@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?AddNode@Transitions@CharClip@@QAAXPAV2@ABUCharGraphNode@@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?AddTask@BlockMgr@@QAAXABVAsyncTask@@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?Album@HamSongMetadata@@QBAPBDXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?AllocateMeshes@FontMap3d@RndText@@UAAXPAV2@H@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?AspectRatio@RndFont3d@@UBAMXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?AttachMesh@@YAXPAVRndMesh@@0@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?BeginDrawing@DxRnd@@UAAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?BeginFromFile@BinkMovieImpl@@UAA_NPBDM_N111HPAVBinStream@@W4LoaderPos@@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?BeginMemTrackFileName@@YAXPBD@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?BeginMemTrackObjectName@@YAXPBD@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?BlurShadowRT@NgLight@@MAAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?BuildFromBSP@@YAXPAVRndMesh@@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?BuildTree@ChallengeSort@@UAAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?BurnXfm@@YAXPAVRndMesh@@_N@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?CacheResource@@YAPBDPBDPBVObject@Hmx@@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?CacheWav@@YAPBDPBDAAW4CacheResourceResult@@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?CalcShaderOpts@RndShaderDepthVolume@@MAA_KPAVNgMat@@W4ShaderType@@_N@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?CalcShaderOpts@RndShaderDrawRect@@MAA_KPAVNgMat@@W4ShaderType@@_N@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?CalcShaderOpts@RndShaderFur@@MAA_KPAVNgMat@@W4ShaderType@@_N@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?CalcShaderOpts@RndShaderMultimesh@@MAA_KPAVNgMat@@W4ShaderType@@_N@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?CalcShaderOpts@RndShaderParticles@@MAA_KPAVNgMat@@W4ShaderType@@_N@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?CalcShaderOpts@RndShaderPostProc@@MAA_KPAVNgMat@@W4ShaderType@@_N@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?CalcShaderOpts@RndShaderSimple@@MAA_KPAVNgMat@@W4ShaderType@@_N@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?CalcShaderOpts@RndShaderStandard@@MAA_KPAVNgMat@@W4ShaderType@@_N@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?CalcShaderOpts@RndShaderSyncTrack@@MAA_KPAVNgMat@@W4ShaderType@@_N@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?CalcShaderOpts@RndShaderUnwrapUV@@MAA_KPAVNgMat@@W4ShaderType@@_N@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?CalcShaderOpts@RndShaderVelocity@@MAA_KPAVNgMat@@W4ShaderType@@_N@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?CalcShaderOpts@RndShaderVelocityCamera@@MAA_KPAVNgMat@@W4ShaderType@@_N@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?CalculateSinCosTable@@YAHJPAM@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?CalculateSwell@HamNavList@@QBAMH@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?Cancel@SingleItemEnumJob@@UAAXPAVObject@Hmx@@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?CharAdvance@RndFont3d@@UBAMG@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?CharAdvance@RndFont3d@@UBA_NGGAAM@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?CharWidth@RndFont3d@@UBAMG@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?CheckOpen@BinkMovieImpl@@UAA_N_N@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?CheckShadowMap@NgLight@@QAAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?CleanupSyncMeshes@FontMap3d@RndText@@UAAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?Clear@HamNavList@@UAAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?ClearBigElements@HamNavList@@QAAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?ClearLights@SpotlightDrawer@@QAAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?ClearSnapshots@LiveCameraInput@@QAAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?CompareSkeletonPositions@FreestyleMoveRecorder@@QBAMPBVBaseSkeleton@@0M@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?CompleteScroll@HamNavList@@UAAXABVUIListState@@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?ComputeLoadedMoveSet@MoveMgr@@QAAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?ConsumeData@StandardStream@@QAAHPAPAXHH@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?ContentDiscovered@HamStorePanel@@UAA_NVSymbol@@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?ContentMounted@HamStorePanel@@UAAXPBD0@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?ContentTitleDiscovered@HamStorePanel@@UAA_NIVSymbol@@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?Copy@CharDriver@@UAAXPBVObject@Hmx@@W4CopyType@23@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?Copy@RndFont3d@@UAAXPBVObject@Hmx@@W4CopyType@23@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?Copy@SynthSample@@UAAXPBVObject@Hmx@@W4CopyType@23@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?Create@NetworkSocket@@SAPAV1@_N@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?CreateFx@FxSendBitCrush360@@MAAPAUIUnknown@@XZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?CreateFx@FxSendDelay360@@MAAPAUIUnknown@@XZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?CreateFx@FxSendDistortion360@@MAAPAUIUnknown@@XZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?CreateFx@FxSendEQ360@@MAAPAUIUnknown@@XZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?CreateFx@FxSendReverb360@@MAAPAUIUnknown@@XZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?CreateFx@FxSendWah360@@MAAPAUIUnknown@@XZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?CreateLargeQuad@DxRnd@@UAAXHHAAULargeQuadRenderData@@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?DataOwner@RndFont3d@@UBAPBVRndFontBase@@XZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?Disengage@HamNavList@@QAAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?DistributeXfms@@YAXPAVRndMultiMesh@@HM@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?DoPost@NgDOFProc@@UAAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?Draw3DChars@WorldCrowd@@IAAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?Draw@BinkMovieImpl@@UAAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?Draw@ClipDistMap@@QAAXMMPAVCharDriver@@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?DrawBlacklight@RndText@@SAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?DrawDebug@FreestyleMoveRecorder@@QAAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?DrawFacesInRange@DxMesh@@UAAXHH@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?DrawGestureMgr@@YAXAAVGestureMgr@@W4BufferType@LiveCameraInput@@M@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?DrawIconMan@HamDirector@@QAAXVSymbol@@00MMPAVRndTex@@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?DrawIconMan@HamDirector@@QAAXW4Difficulty@@MMMMPAVRndTex@@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?DrawPoint3D@SkeletonViz@@QAAXABVVector3@@MABVColor@Hmx@@M@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?DrawPreClear@Rnd@@MAAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?DrawRect@DxRnd@@UAAXABVRect@Hmx@@PAVRndMat@@W4ShaderType@@ABVColor@3@PBV63@4@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?DrawRectDepth@DxRnd@@UAAXABVVector3@@AAY03$$CBV2@ABVVector4@@PAVRndMat@@W4ShaderType@@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?DrawShadow@SpotlightDrawer@@MAAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?DrawShowing@Character@@UAAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?DrawShowing@DxMesh@@UAAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?DrawShowing@MoveDir@@UAAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?DrawShowing@PhysicsVolume@@UAAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?DrawShowing@RndGenerator@@UAAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?DrawShowing@RndScreenMask@@UAAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?DrawShowing@RndTexBlender@@UAAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?DrawShowing@RndText@@UAAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?DrawShowing@WorldCrowd@@UAAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?DrawShowing@WorldDir@@UAAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?DrawString@DxRnd@@UAAAAVVector2@@PBDABV2@ABVColor@Hmx@@_N@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?DrawWorld@SpotlightDrawer@@MAAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?End@BinkMovieImpl@@UAAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?EndAnim@HamCamShot@@UAAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?EndDrawing@DxRnd@@UAAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?EndFrame@HamListRibbon@@UAAMXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?EndFrame@RndMorph@@UAAMXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?EndMemTrackFileName@@YAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?EndMemTrackObjectName@@YAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?Enter@Flow@@UAAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?Enter@HamNavList@@UAAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?Exit@CharEyes@@UAAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?Exit@Flow@@UAAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?Exit@HamNavList@@UAAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?ExitStore@StorePanel@@UBAXW4StoreError@@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?FillInRoutineAt@MoveMgr@@QAAXHH@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?FindClip@CharClipGroup@@QBAPAVCharClip@@PBD@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?FindClip@CharDriver@@QAAPAVCharClip@@ABVDataNode@@_N@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?FindDists@ClipDistMap@@QAAXMPAVDataArray@@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?FinishDrawTarget@DxTex@@UAAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?Free@MemHeap@@QAAHPAH@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?GetAlbumArtPath@ChallengeHeaderNode@@UAAPBDXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?GetAssociatedBlocks@BlockMgr@@QAAX_KHAAH11@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?GetChallengerGamertag@ChallengeSortMgr@@QAAPBDH@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?GetContinuousBuf@MicXbox@@UAAPAFAAH@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?GetDistanceToPlane@RndText@@UAAMABVPlane@@AAVVector3@@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?GetFailType@NetCacheLoader@@QBA?AW4NetCacheMgrFailType@@XZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?GetFirstActive@FitnessCalorieHeaderNode@@UAAPAVNavListSortNode@@XZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?GetFirstActive@SongHeaderNode@@UAAPAVNavListSortNode@@XZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?GetFrame@BinkMovieImpl@@UBAHXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?GetHostName@NetworkSocket@@SA?AVString@@XZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?GetInstance@MicManagerXbox@@SAPAV1@XZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?GetJumpBackTotalTime@StandardStream@@UAAMM@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?GetLiveSkeleton@FreestyleMoveRecorder@@QAAPAVBaseSkeleton@@XZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?GetLux@ExposureRecipe@TrueColor@@QAAMXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?GetMultimeshFaces@DxMesh@@QAAPAUD3DVertexBuffer@@XZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?GetName@MicXbox@@UBAAAVSymbol@@XZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?GetName@PlatformMgr@@QBAPBDH@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?GetNonConstMoveParent@MoveGraph@@ABAPAVMoveParent@@VSymbol@@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?GetNormalMapTextures@@YA?AVDataNode@@PAVObjectDir@@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?GetOwnerOfGuest@PlatformMgr@@QAAHH@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?GetPlaylist@PlaylistSortMgr@@QAAPAVPlaylist@@H@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?GetRecentBuf@MicXbox@@UAAPAFAAH@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?GetRecord@RhythmDetector@@QAAABURecordData@1@MM_NVSymbol@@PAVTextStream@@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?GetRenderTextures@@YA?AVDataNode@@PAVObjectDir@@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?GetResponseString@DingoJob@@QAAPBDXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?GetScore@FreestyleMoveRecorder@@QAAMPBVBaseSkeleton@@HM_N@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?GetServiceID@PlatformMgr@@QAA_NABVString@@AAI@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?GetSongCmp@NavListItemSortCmp@@UBAPBVSongCmp@@XZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?GetType@MicXbox@@UBA?AW4Type@Mic@@XZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?Handle@CharDriver@@UAA?AVDataNode@@PAVDataArray@@_N@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?Handle@FitnessCalorieSortMgr@@UAA?AVDataNode@@PAVDataArray@@_N@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?Handle@RndFont3d@@UAA?AVDataNode@@PAVDataArray@@_N@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?Handle@UIListLabel@@UAA?AVDataNode@@PAVDataArray@@_N@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?Handle@UIListMesh@@UAA?AVDataNode@@PAVDataArray@@_N@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?Handle@UIListWidget@@UAA?AVDataNode@@PAVDataArray@@_N@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?HasOnlinePrivilege@PlatformMgr@@QBA_NH@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?HasPendingVoices@Voice@@SA_NXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?Highlight@CharEyes@@UAAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?Highlight@CharIKHand@@UAAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?IPIntToString@NetworkSocket@@SA?AVString@@I@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?IPStringToInt@NetworkSocket@@SAIABVString@@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?IncrementDisplayableChars@FontMap3d@RndText@@UAAXG@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?Init@MicManagerXbox@@QAAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?Init@PlatformMgr@@QAAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?InitQuickJoyCheats@@YAXPBVDataArray@@W4ShiftMode@CheatsManager@@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?Int@Rand@@QAAHXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?Interp@RndPostProc@@QAAXPBV1@0M@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?InviteParty@PlatformMgr@@QAAXH@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?IsActive@ChallengeHeaderNode@@UBA_NXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?IsActive@FitnessCalorieHeaderNode@@UBA_NXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?IsFinished@SingleItemEnumJob@@UAA_NXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?IsInParty@PlatformMgr@@QAA_NXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?IsInPartyWithOthers@PlatformMgr@@QAA_NXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?IsLoadConst@IRLoadConst@XGRAPHICS@@UBA?B_NXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?IsLoading@BinkMovieImpl@@UBA_NXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?IsOpen@BinkMovieImpl@@UBA_NXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?IsPastStreamJumpPointOfNoReturn@StandardStream@@QAA_NXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?IsRunning@RandomIntervalGroupSeqInst@@UAA_NXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?JumpedMoveIdxAdd@DanceRemixer@@QBAHHH@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?Kerning@RndFont3d@@UBAMGG@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?KillBlockRequests@BlockMgr@@QAAXPAVArkFile@@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?Load@DrivenPropertyEntry@@QAAXAAVBinStream@@PAVFlowNode@@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?Load@RndEnviron@@UAAXAAVBinStream@@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?Load@RndFont3d@@UAAXAAVBinStream@@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?Load@RndFontBase@@UAAXAAVBinStream@@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?Load@RndParticleSys@@UAAXAAVBinStream@@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?Load@SpotlightDrawer@@UAAXAAVBinStream@@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?Load@SynthSample@@UAAXAAVBinStream@@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?LoadData@CharBonesSamples@@QAAXAAVBinStreamRev@@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?LoadHeader@CharBonesSamples@@QAAXAAVBinStreamRev@@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?LoadRev@RndPostProc@@QAAXAAVBinStreamRev@@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?LockBitmap@DxTex@@UAAXAAVRndBitmap@@H@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?LockThread@BinkMovieImpl@@UAAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?LookAt@Transform@@QAAXABVVector3@@0@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?MakeDrawTarget@DxTex@@UAAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?MakeFileList@@YA?AVDataNode@@PBD_NP6A_NPAD@Z@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?MakeFileListFullPath@@YA?AVDataNode@@PBD@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?MakeTangentsLate@@YAXPAVRndMesh@@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?MakeWorldSphere@RndGenerator@@UAA_NAAVSphere@@_N@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?MakeWorldSphere@RndText@@UAA_NAAVSphere@@_N@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?Mat@RndFont3d@@UBAPAVRndMat@@XZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?MountContent@XboxContentMgr@@UAA_NVSymbol@@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?MoveXfms@@YAXPAVRndMultiMesh@@ABVVector3@@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?MsPerFrame@BinkMovieImpl@@UBAMXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?NewHeaderNode@ChallengeSortByScore@@UBAPAVNavListHeaderNode@@PAVNavListItemNode@@0@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?NewHeaderNode@FitnessCalorieSortByCalorie@@UBAPAVNavListHeaderNode@@PAVNavListItemNode@@0@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?NewHeaderNode@MQSongSortByCharacter@@UBAPAVNavListHeaderNode@@PAVNavListItemNode@@0@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?NewHeaderNode@SongSortByDiff@@UBAPAVNavListHeaderNode@@PAVNavListItemNode@@0@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?NewHeaderNode@SongSortByLocation@@UBAPAVNavListHeaderNode@@PAVNavListItemNode@@0@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?NewStream@Synth@@UAAPAVStream@@PBDMM_N@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?NumFrames@BinkMovieImpl@@UBAHXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?OldResourcePreload@LabelShrinkWrapper@@UAAXAAVBinStream@@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?OnCompletion@StoreEnumJob@@UAAXPAVObject@Hmx@@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?OnDeletePlaylistFromRC@PlaylistSortMgr@@QAAXPAVPlaylist@@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?OnEnter@ChallengeSortMgr@@UAAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?OnParametersChanged@FxSendFlanger360@@MAAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?OnSmartGlassListen@PartyModeMgr@@AAAXH@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?OnSync@DxMesh@@UAAXH@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?PSNRToDetectFrac@HamMove@@QBAMM@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?PlayCrowdAnimation@HamWardrobe@@QAAXVSymbol@@H_N@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?Poll@BinkMovieImpl@@UAA_NXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?Poll@BlockMgr@@QAAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?Poll@CharDriver@@UAAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?Poll@CharEyes@@UAAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?Poll@CharIKHand@@UAAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?Poll@CharLipSyncDriver@@UAAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?Poll@FreestyleMoveRecorder@@QAAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?Poll@Game@@QAAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?Poll@HamDirector@@UAAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?Poll@HamIKEffector@@UAAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?Poll@HamStorePanel@@UAAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?Poll@LabelShrinkWrapper@@UAAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?Poll@PlatformMgr@@QAAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?Poll@PropertyTask@@UAAXM@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?Poll@RandomIntervalGroupSeqInst@@UAAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?Poll@RndParticleSys@@UAAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?Poll@SaveLoadManager@@QAAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?Poll@SkeletonViz@@UAAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?Poll@SynthEmitter@@UAAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?Poll@TaskMgr@@QAAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?Poll@VorbisReader@@UAAXM@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?Poll@XLSPConnection@@QAAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?PollRecording@SkeletonClip@@QAAXABUSkeletonFrame@@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?PollRefresh@XboxContentMgr@@UAAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?PollStream@StandardStream@@QAAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?PollXSocialCapabilities@PlatformMgr@@QAA_NXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?PostLoad@SynthSample@@UAAXAAVBinStream@@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?PostUpdate@GestureMgr@@UAAXPBUSkeletonUpdateData@@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?PostUpdate@HamNavList@@UAAXPBUSkeletonUpdateData@@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?PreLoad@SynthSample@@UAAXAAVBinStream@@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?PrevSkeleton@SkeletonClip@@UBA_NABVSkeleton@@HAAVArchiveSkeleton@@AAH@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?QuatAt@QuatKeys@@UAAHMAAVQuat@Hmx@@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?QueryXSocialCapabilities@PlatformMgr@@QAA_NXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?RandomXfms@@YAXPAVRndMultiMesh@@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?Ready@BinkMovieImpl@@UBA_NXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?Replace@PropertyTask@@UAA_NPAVObjRef@@PAVObject@Hmx@@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?Replace@RndEnviron@@UAA_NPAVObjRef@@PAVObject@Hmx@@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?Replace@RndParticleSys@@UAA_NPAVObjRef@@PAVObject@Hmx@@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?ResetDetectFrames@MoveDir@@QAAXHW4Difficulty@@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?ResetNormals@@YAXPAVRndMesh@@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?Reteleport@HamCamShot@@QAAXABVVector3@@_NVSymbol@@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?RootTrans@UIListLabel@@UAAPAVRndTransformable@@XZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?Save@BinkMovieImpl@@UAAXPAVBinStream@@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?Save@RndFont3d@@UAAXAAVBinStream@@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?ScaleXfms@@YAXPAVRndMultiMesh@@ABVVector3@@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?Select@ChallengeHeaderNode@@UAA?AVSymbol@@XZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?Select@DxTex@@UAAXH@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?Select@RndShaderDepthVolume@@MAAXPAVRndMat@@W4ShaderType@@_N@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?Select@RndShaderDrawRect@@MAAXPAVRndMat@@W4ShaderType@@_N@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?Select@RndShaderFur@@MAAXPAVRndMat@@W4ShaderType@@_N@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?Select@RndShaderMultimesh@@MAAXPAVRndMat@@W4ShaderType@@_N@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?Select@RndShaderParticles@@MAAXPAVRndMat@@W4ShaderType@@_N@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?Select@RndShaderPostProc@@MAAXPAVRndMat@@W4ShaderType@@_N@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?Select@RndShaderStandard@@MAAXPAVRndMat@@W4ShaderType@@_N@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?Select@RndShaderSyncTrack@@MAAXPAVRndMat@@W4ShaderType@@_N@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?Select@RndShaderUnwrapUV@@MAAXPAVRndMat@@W4ShaderType@@_N@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?Select@RndShaderVelocity@@MAAXPAVRndMat@@W4ShaderType@@_N@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?Select@RndShaderVelocityCamera@@MAAXPAVRndMat@@W4ShaderType@@_N@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?Set@NgDOFProc@@UAAXPAVRndCam@@MMMM@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?SetAndClearShadowViewport@NgLight@@MAAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?SetBloomBlurWeights@@YAX_NMM@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?SetFrame@RndGenerator@@UAAXMM@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?SetFrame@RndMeshAnim@@UAAXMM@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?SetFrame@RndMorph@@UAAXMM@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?SetFrameEx@LightPreset@@QAAXMM_N@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?SetFullness@WorldCrowd@@QAAXMM@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?SetGain@MicXbox@@UAAXM@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?SetMinIntegrationTime@ExposureRecipe@TrueColor@@QAAXM@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?SetNotifyUILocation@PlatformMgr@@QAAXW4NotifyLocation@@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?SetPConstant4x3@DxShaderMgr@@UAAXW4PShaderConstant@@ABVMatrix4@Hmx@@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?SetPConstant@DxShaderMgr@@UAAXW4PShaderConstant@@ABVMatrix4@Hmx@@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?SetPConstant@DxShaderMgr@@UAAXW4PShaderConstant@@ABVVector4@@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?SetPConstant@DxShaderMgr@@UAAXW4PShaderConstant@@H@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?SetPConstant@DxShaderMgr@@UAAXW4PShaderConstant@@PAVRndCubeTex@@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?SetPConstant@DxShaderMgr@@UAAXW4PShaderConstant@@_N@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?SetPadProperty@PlatformMgr@@QBAXHHPBG@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?SetPan@SfxInst@@UAAXM@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?SetPaused@BinkMovieImpl@@UAA_N_N@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?SetPreFrame@HamCamShot@@UAAXMM@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?SetSend@Voice@@QAAXPAVFxSend360@@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?SetTranspose@SfxInst@@UAAXM@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?SetVConstant4x3@DxShaderMgr@@UAAXW4VShaderConstant@@ABVMatrix4@Hmx@@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?SetVConstant@DxShaderMgr@@UAAXW4VShaderConstant@@ABVMatrix4@Hmx@@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?SetVConstant@DxShaderMgr@@UAAXW4VShaderConstant@@ABVVector4@@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?SetVConstant@DxShaderMgr@@UAAXW4VShaderConstant@@H@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?SetVConstant@DxShaderMgr@@UAAXW4VShaderConstant@@PBMI@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?SetVConstant@DxShaderMgr@@UAAXW4VShaderConstant@@_N@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?SetVolume@BinkMovieImpl@@UAAXM@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?SetWidthHeight@BinkMovieImpl@@UAAXHH@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?SetupCharacter@FontMap3d@RndText@@UAAXGAAMMABVStyleState@2@GMW4FitType@2@M@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?SetupCharacter@FontMap@RndText@@UAAXGAAMMABVStyleState@2@GMW4FitType@2@M@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?ShowControllerRequiredUI@PlatformMgr@@QAAXPAVObject@Hmx@@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?SignInUsers@PlatformMgr@@QAAXHK@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?SongBlock@SongMetadata@@QBAPAVSongInfo@@XZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?Start@SingleItemEnumJob@@UAAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?StartImpl@RandomIntervalGroupSeqInst@@UAAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?StartImpl@SfxInst@@UAAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?StartPlayback@MicXbox@@UAAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?StartSynchronizedVoices@@YAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?Stop@RandomIntervalGroupSeqInst@@UAAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?StopPlayback@MicXbox@@UAAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?StopRecording@FreestyleMoveRecorder@@QAAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?StoreProfile@StorePanel@@UBAPAVProfile@@XZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?SyncBitmap@DxTex@@UAAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?SyncProperty@CharBonesSamples@@UAA_NAAVDataNode@@PAVDataArray@@HW4PropOp@@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?SyncProperty@CharDriver@@UAA_NAAVDataNode@@PAVDataArray@@HW4PropOp@@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?SyncProperty@RndFont3d@@UAA_NAAVDataNode@@PAVDataArray@@HW4PropOp@@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?SynthPoll@MidiInstrument@@UAAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?TessellateMesh@@YAXPAVRndMesh@@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?TexelsLock@DxTex@@UAA_NAAPAX@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?Text@AppMiniLeaderboardDisplay@@UBAXHHPAVUIListLabel@@PAVUILabel@@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?Truncate@MemHeap@@QAAPAHPAHHAAH@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?UnhookShadow@Character@@IAAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?Unload@HamStorePanel@@UAAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?UnlockBitmap@DxTex@@UAAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?UnlockThread@BinkMovieImpl@@UAAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?UpdateApproxLighting@RndEnviron@@UAAXPBVVector3@@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?UpdateBoxMap@SpotlightDrawer@@QAAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?UpdateList@PlaylistSortMgr@@QAAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?UpdateOverlay@MoveDir@@UAAMPAVRndOverlay@@M@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?UpdateOverlay@Synth@@UAAMPAVRndOverlay@@M@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?UpdateText@RndText@@QAAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?UpdateTime@StandardStream@@UAAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?UpdateTimeByFiltering@StandardStream@@UAAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?UpdateVolume@SfxInst@@UAAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?UtilDrawCylinder@@YAXABVTransform@@MMABVColor@Hmx@@H@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?UtilDrawSphere@@YAXABVVector3@@MABVColor@Hmx@@PAVRndMat@@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?pow@@YAMMH@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?sBlacklightModeEnabled@RndText@@1_NA=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?sEDRamChecksEnabled@DxTex@@0_NA=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?sGameRecord2Player@MoveDir@@2_NA=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?sGameRecord@MoveDir@@2_NA=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?sLastSelectInControllerMode@HamNavList@@2_NA=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?sMotdCheat@MetaPanel@@2_NA=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?sNeedDraw@SpotlightDrawer@@1_NA=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?sRequireFixedLength@UILabel@@2_NA=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?sShowing@PhysicsVolume@@2_NA=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?sUnlockAll@MetaPanel@@2_NA=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:DataInput=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:FileRecursePattern=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:XGSurfaceSize=__link_glue_noop")

// -- Remaining symbols missed due to substring overlap with ??__E entries --
#pragma comment(linker, "/ALTERNATENAME:??$PropSync@VRndTransformable@@@@YA_NAAV?$ObjPtrVec@VRndTransformable@@VObjectDir@@@@AAVDataNode@@PAVDataArray@@HW4PropOp@@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??$?6VEventTrigger@@@@YAAAVBinStream@@AAV0@ABV?$ObjPtrList@VEventTrigger@@VObjectDir@@@@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??$?6VHamCamShot@@@@YAAAVBinStream@@AAV0@ABV?$ObjPtrList@VHamCamShot@@VObjectDir@@@@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??$?6VHamScrollSpeedIndicator@@@@YAAAVBinStream@@AAV0@ABV?$ObjDirPtr@VHamScrollSpeedIndicator@@@@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??$?6VObjectDir@@@@YAAAVBinStream@@AAV0@ABV?$ObjDirPtr@VObjectDir@@@@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??$?6VRhythmDetector@@@@YAAAVBinStream@@AAV0@ABV?$ObjPtrVec@VRhythmDetector@@VObjectDir@@@@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??$?6VRndMat@@@@YAAAVBinStream@@AAV0@ABV?$ObjPtrList@VRndMat@@VObjectDir@@@@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??$?6VSequence@@@@YAAAVBinStream@@AAV0@ABV?$ObjPtrList@VSequence@@VObjectDir@@@@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??$?6VUIListDir@@@@YAAAVBinStream@@AAV0@ABV?$ObjDirPtr@VUIListDir@@@@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?sShadowSpots@SpotlightDrawer@@1V?$vector@PAVSpotlight@@V?$StlNodeAlloc@PAVSpotlight@@@stlpmtx_std@@@stlpmtx_std@@A=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:?sLights@SpotlightDrawer@@1V?$vector@VSpotlightEntry@SpotlightDrawer@@V?$StlNodeAlloc@VSpotlightEntry@SpotlightDrawer@@@stlpmtx_std@@@stlpmtx_std@@A=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:?sCans@SpotlightDrawer@@1V?$vector@VSpotMeshEntry@SpotlightDrawer@@V?$StlNodeAlloc@VSpotMeshEntry@SpotlightDrawer@@@stlpmtx_std@@@stlpmtx_std@@A=__link_glue_zero")
// -- Template instantiations (46 symbols) --
#pragma comment(linker, "/ALTERNATENAME:??$?6VCamShot@@@@YAAAVBinStream@@AAV0@ABV?$ObjPtrList@VCamShot@@VObjectDir@@@@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??$?6VCharBone@@@@YAAAVBinStream@@AAV0@ABV?$ObjPtrList@VCharBone@@VObjectDir@@@@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??$?6VCharClip@@@@YAAAVBinStream@@AAV0@ABV?$ObjPtrVec@VCharClip@@VObjectDir@@@@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??$?6VCharInterest@@@@YAAAVBinStream@@AAV0@ABV?$ObjOwnerPtr@VCharInterest@@@@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??$?6VCharLookAt@@@@YAAAVBinStream@@AAV0@ABV?$ObjOwnerPtr@VCharLookAt@@@@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??$?6VEventTrigger@@@@YAAAVBinStream@@AAV0@ABV?$ObjOwnerPtr@VEventTrigger@@@@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??$?6VFlow@@@@YAAAVBinStream@@AAV0@ABV?$ObjPtrVec@VFlow@@VObjectDir@@@@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??$?6VHamListRibbon@@@@YAAAVBinStream@@AAV0@ABV?$ObjDirPtr@VHamListRibbon@@@@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??$?6VObject@Hmx@@@@YAAAVBinStream@@AAV0@ABV?$ObjPtrVec@VObject@Hmx@@VObjectDir@@@@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??$?6VObjectDir@@@@YAAAVBinStream@@AAV0@ABV?$ObjOwnerPtr@VObjectDir@@@@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??$?6VRndAnimatable@@@@YAAAVBinStream@@AAV0@ABV?$ObjOwnerPtr@VRndAnimatable@@@@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??$?6VRndDrawable@@@@YAAAVBinStream@@AAV0@ABV?$ObjOwnerPtr@VRndDrawable@@@@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??$?6VRndDrawable@@@@YAAAVBinStream@@AAV0@ABV?$ObjPtrVec@VRndDrawable@@VObjectDir@@@@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??$?6VRndEnviron@@@@YAAAVBinStream@@AAV0@ABV?$ObjOwnerPtr@VRndEnviron@@@@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??$?6VRndEnviron@@@@YAAAVBinStream@@AAV0@ABV?$ObjPtrVec@VRndEnviron@@VObjectDir@@@@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??$?6VRndFont@@@@YAAAVBinStream@@AAV0@ABV?$ObjOwnerPtr@VRndFont@@@@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??$?6VRndFontBase@@@@YAAAVBinStream@@AAV0@ABV?$ObjPtrList@VRndFontBase@@VObjectDir@@@@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??$?6VRndLight@@@@YAAAVBinStream@@AAV0@ABV?$ObjPtrVec@VRndLight@@VObjectDir@@@@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??$?6VRndLightAnim@@@@YAAAVBinStream@@AAV0@ABV?$ObjOwnerPtr@VRndLightAnim@@@@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??$?6VRndMat@@@@YAAAVBinStream@@AAV0@ABV?$ObjPtrVec@VRndMat@@VObjectDir@@@@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??$?6VRndMatAnim@@@@YAAAVBinStream@@AAV0@ABV?$ObjOwnerPtr@VRndMatAnim@@@@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??$?6VRndMeshAnim@@@@YAAAVBinStream@@AAV0@ABV?$ObjOwnerPtr@VRndMeshAnim@@@@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??$?6VRndPartLauncher@@@@YAAAVBinStream@@AAV0@ABV?$ObjPtrList@VRndPartLauncher@@VObjectDir@@@@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??$?6VRndParticleSysAnim@@@@YAAAVBinStream@@AAV0@ABV?$ObjOwnerPtr@VRndParticleSysAnim@@@@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??$?6VRndTexBlendController@@@@YAAAVBinStream@@AAV0@ABV?$ObjPtrList@VRndTexBlendController@@VObjectDir@@@@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??$?6VSpotlight@@@@YAAAVBinStream@@AAV0@ABV?$ObjPtrVec@VSpotlight@@VObjectDir@@@@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??$?6VSpotlightDrawer@@@@YAAAVBinStream@@AAV0@ABV?$ObjPtrVec@VSpotlightDrawer@@VObjectDir@@@@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??$GatherObjectsFromGroup@VRndMesh@@@@YAIPAVRndGroup@@AAV?$vector@PAVRndMesh@@V?$StlNodeAlloc@PAVRndMesh@@@stlpmtx_std@@@stlpmtx_std@@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??$MakeString@HK@@YAPBDPBDABHABK@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??$MakeString@VString@@$$BY0BAE@D@@YAPBDPBDABVString@@AAY0BAE@$$CBD@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??$MakeString@VString@@PAD@@YAPBDPBDABVString@@ABQAD@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??$PropSync@VCharClip@@@@YA_NAAV?$ObjPtrVec@VCharClip@@VObjectDir@@@@AAVDataNode@@PAVDataArray@@HW4PropOp@@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??$PropSync@VFlow@@@@YA_NAAV?$ObjPtrVec@VFlow@@VObjectDir@@@@AAVDataNode@@PAVDataArray@@HW4PropOp@@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??$PropSync@VObject@Hmx@@@@YA_NAAV?$ObjPtrVec@VObject@Hmx@@VObjectDir@@@@AAVDataNode@@PAVDataArray@@HW4PropOp@@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??$PropSync@VRhythmDetector@@@@YA_NAAV?$ObjPtrVec@VRhythmDetector@@VObjectDir@@@@AAVDataNode@@PAVDataArray@@HW4PropOp@@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??$PropSync@VRndDrawable@@@@YA_NAAV?$ObjPtrVec@VRndDrawable@@VObjectDir@@@@AAVDataNode@@PAVDataArray@@HW4PropOp@@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??$PropSync@VRndMat@@@@YA_NAAV?$ObjPtrVec@VRndMat@@VObjectDir@@@@AAVDataNode@@PAVDataArray@@HW4PropOp@@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??$PropSync@VWorldInstance@@@@YA_NAAV?$ObjDirPtr@VWorldInstance@@@@AAVDataNode@@PAVDataArray@@HW4PropOp@@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??$_M_allocate_and_copy@PAVFace@RndMesh@@@?$vector@VFace@RndMesh@@V?$StlNodeAlloc@VFace@RndMesh@@@stlpmtx_std@@@stlpmtx_std@@IAAPAVFace@RndMesh@@IPAV23@0@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??$_Param_Construct@URecurseInfo@@U1@@stlpmtx_std@@YAXPAURecurseInfo@@ABU1@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??$__uninitialized_copy@PAVTriangle@@PAV1@@stlpmtx_std@@YAPAVTriangle@@PAV1@00ABU__false_type@0@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??$__uninitialized_copy@PBVSampleMarker@@PAV1@@stlpmtx_std@@YAPAVSampleMarker@@PBV1@0PAV1@ABU__false_type@0@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??$sort@PAH@stlpmtx_std@@YAXPAH0@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??$_Destroy_Range@PAULabel@?A0x81ddebd1@@@stlpmtx_std@@YAXPAULabel@?A0x81ddebd1@@0@Z=__link_glue_noop")

// -- Game/engine data (810 symbols) --
#pragma comment(linker, "/ALTERNATENAME:??_C@_02EAOCEIGI@?$DP?$CK?$AA@=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:??_C@_08MGFPAODM@?$CIactive?$CJ?$AA@=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:??_C@_09GFOGDJJM@c?4Owner?$CI?$CJ?$AA@=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:??_C@_0BB@LIEJFCBC@global_challenge?$AA@=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:??_C@_0CK@OGKHIDMB@e?3?2lazer_build_gmc1?2system?2src?2m@=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:??_C@_0CM@GHADNDAB@e?3?2lazer_build_gmc1?2lazer?2src?2ga@=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:??_C@_0CN@IDHNMBLG@e?3?2lazer_build_gmc1?2system?2src?2r@=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:??_C@_0CO@HMMLLAKJ@e?3?2lazer_build_gmc1?2system?2src?2o@=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:??_C@_0CP@OLAPBCHP@e?3?2lazer_build_gmc1?2lazer?2src?2ga@=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:??_C@_0DA@NEGCEHHH@e?3?2lazer_build_gmc1?2system?2src?2f@=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:??_C@_0DA@NLAEAONH@e?3?2lazer_build_gmc1?2system?2src?2c@=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:??_C@_0DB@KHGFLFH@e?3?2lazer_build_gmc1?2system?2src?2u@=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:??_C@_0DC@ILBKENPG@e?3?2lazer_build_gmc1?2system?2src?2o@=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:??_C@_0DE@IKCBDEP@e?3?2lazer_build_gmc1?2system?2src?2s@=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:??_C@_0DE@PPACJAPO@e?3?2lazer_build_gmc1?2system?2src?2h@=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:??_C@_0DF@DOEENNPO@e?3?2lazer_build_gmc1?2system?2src?2g@=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:??_C@_0DF@KFHICABA@e?3?2lazer_build_gmc1?2system?2src?2m@=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:??_C@_0DF@MALNIALN@e?3?2lazer_build_gmc1?2system?2src?2c@=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:??_C@_0DG@EIGPAHKL@e?3?2lazer_build_gmc1?2system?2src?2o@=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:??_C@_0L@DMFCCNFB@world_draw?$AA@=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:??_C@_0O@LGMFJNKF@dlc_challenge?$AA@=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:??_C@_0P@DGBENEDD@D3D?$CIphys?$CJ?3Mesh?$AA@=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:?gDebugDepth@@3PAEA=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:?lbl_82F14008@@3HA=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:?mModalCallback@RndShader@@1P6AXAAW4ModalType@Debug@@AAVFixedString@@_N@ZA=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:?sBlacklightPacketCount@RndText@@1HA=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:?sCurrent@RndPostProc@@1PAV1@A=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:?sCurrentShader@RndShader@@1W4ShaderType@@A=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:?sDOFOverride@RndPostProc@@1VDOFOverrideParams@@A=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:?sFileOpenCallback@LoadMgr@@0P6A_NPBD@ZA=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:?sGlowMat@WorldDir@@0PAVRndMat@@A=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:?sHamMaster@MetaPanel@@2PAVHamMaster@@A=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:?sInstance@LetterboxPanel@@2PAV1@A=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:?sLabelRot@InlineHelp@@0MA=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:?sLastUpdatedTime@InlineHelp@@0MA=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:?sLatencySeconds@MoveDir@@0MA=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:?sMaxScreenId@UIScreen@@1HA=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:?sMetaMaterials@RndMat@@1PAVObjectDir@@A=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:?sMutableSkinnedVertexDecl@DxMesh@@1PAUD3DVertexDeclaration@@A=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:?sMutableVertexDecl@DxMesh@@1PAUD3DVertexDeclaration@@A=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:?sPLFMinTimeError@MoveDir@@0MA=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:?sPostProcPanelCount@Rnd@@2HA=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:?sRotationTime@InlineHelp@@0MA=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:?sShaders@RndShader@@1PAPAV1@A=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:?sSlideSmoothAmount@HamNavList@@0MA=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:?sSlideTrendAmount@HamNavList@@0MA=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:?sSongDB@MetaPanel@@2PAVSongDB@@A=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:?sVertexDecl@DxMesh@@1PAUD3DVertexDeclaration@@A=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:__real@399d4952=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:__real@3f6b851f=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:__real@48927c00=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:__real@c11ccccd=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:__real@c2c80000=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_8200D100=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_8200F018=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_82010160=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_82011EC8=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_82013490=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_82013498=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_820159A8=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_820176D0=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_8201ABA8=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_8201C2E0=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_8201DA18=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_8201FF90=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_82024C40=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_820281B0=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_820289E8=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_82029918=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_8202A1E0=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_8202B530=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_8202C1C8=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_82040360=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_820492F0=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_8204AE30=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_8204BA00=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_8204C9A0=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_8204DAF4=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_8204DAFC=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_82052EC8=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_820576D0=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_82058538=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_82063CC0=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_82066CB4=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_8208C4F8=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_82091700=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_82096894=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_8209C8E8=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_820A0C64=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_820A6148=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_820A8F10=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_820AA640=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_820ADDB0=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_820AE758=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_820AF090=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_820B00C8=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_820B0D20=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_820B78B0=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_820BBC50=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_820BCD70=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_820BD650=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_820BDD88=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_820C7BC4=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_820CBB88=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_820D0138=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_820D2158=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_820D3370=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_820E34C8=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_820E68F0=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_820EC890=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_820EE690=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_820EE69C=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_82115D00=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_82115D04=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_82118680=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_8212CD70=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_8212CD80=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_8225C7D0=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_82262DB8=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_82265350=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_82F0BFD0=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_82F0C358=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_82F0C3A8=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_82F0C3F8=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_82F0C448=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_82F10324=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_82F1032C=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_82F1033C=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_82F136C8=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_82F15300=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_82F15308=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_82F16D28=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_82F499AC=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_82F5E698=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_82F5E69C=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_82F5E6A0=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_82F5E6A4=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_82F5E6A8=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_82F5E6AC=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_82F5E6B0=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_82F5E6B4=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_82F5E6B8=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_82F5E6BC=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_82F5EEC4=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_82F5EEC8=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_82F5EECC=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_82F5EED0=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_82F5EED4=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_82F5EED8=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_82F5EEDC=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_82F5EEE0=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_82F5EEE4=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_82F5EEE8=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_82F5EEEC=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_82F5EEF0=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_82F5EEF4=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_82F5EEF8=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_82F5EEFC=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_82F5F1B4=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_82F5F1B8=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_82F5F1BC=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_82F5F1C0=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_82F5F1C4=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_82F5F1C8=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_82F5F1CC=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_82F5F1D0=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_82F5F1D8=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_82F5F1DC=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_82F5F1E0=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_82F5F1E4=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_82F5F1E8=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_82F5F1EC=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_82F5F1F0=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_82F5F1F4=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_82F5F1F8=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_82F5F1FC=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_82F5F200=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_82F5F204=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_82F5F208=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_82F5F20C=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_82F5F210=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_82F5F214=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_82F5F218=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_82F5F21C=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_82F5F220=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_82F5F224=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_82F5F228=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_82F5F22C=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_82F5F230=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_82F5F234=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_82F5F238=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_82F5F23C=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_82F5F240=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_82F5F244=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_82F5F248=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_82F5F24C=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_82F5F250=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_82F5F3F8=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_82F5F3FC=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_82F5F5E0=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_82F5F5E4=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_82F5F68C=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_82F5F690=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_82F5F878=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_82F5F910=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_82F5F914=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_82F5F918=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_82F5F91C=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_82F5F920=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_82F60F54=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_82F60F5C=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_82F60F60=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_82F60F64=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_82F60F68=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_82F60F6C=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_82F60F70=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_82F60F74=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_82F60F78=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_82F60F7C=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_82F60F80=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_82F60F84=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_82F61028=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_82F6102C=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_82F61030=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_82F61034=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_82F61038=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_82F6103C=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_82F61040=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_82F610B8=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_82F610BC=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_82F610C0=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_82F610C4=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_82F61124=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_82F61128=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_82F617FC=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_82F61800=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_82F61804=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_82F61808=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_82F6180C=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_82F61810=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_82F61814=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_82F61818=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_82F63A88=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_82F63A8C=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_82F63ADC=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_82F63AE4=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_82F63AEC=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_82F63AF0=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_82F63AF8=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_82F63B00=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_82F63B1C=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_82F63B20=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_82F68AC0=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_82F68AC8=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_82F68B10=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_82F68B50=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_83010002=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_83010004=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_83010006=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_8301000A=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_8301000C=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_83010010=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_83010011=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_83010012=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_83099800=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_83099804=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_83099808=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_830A6794=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_830A679C=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_830A67A0=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_830A67B0=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_830A67C0=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_830DF990=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_830DF9D0=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_830DF9D4=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_830DF9D8=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_830DF9DC=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_830DF9E0=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_830DF9E4=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_830DF9E8=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_830DF9EC=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_830DF9F0=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_830DF9F4=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_830DF9F8=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_830DF9FC=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_830DFA00=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_830DFA04=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_830DFA08=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_830DFA0C=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_830DFA10=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_830DFA14=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_830DFA18=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_830DFA1C=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_830DFE28=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_830DFE2C=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_830DFE30=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_830DFE34=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_830E18C4=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_830E18C8=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_830E18CC=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_830E18D0=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_830E18D4=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_830E18D8=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_830E18DC=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_830E18E0=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_830E18E4=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_830E18E8=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_830E1914=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_830E1918=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_830E196C=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_830E1970=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_830E1A10=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_830E1A14=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_830E1CD4=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_830E1CD8=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_830E1CDC=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_830E1CE8=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_830E1F00=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_830E1F04=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_830E1F08=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_830E1F0C=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_830E1F10=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_830E1F14=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_830E1F18=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_830E1F1C=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_830E1F20=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_830E1F24=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_830E1F28=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_830E1F2C=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_830E1F30=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_830E1F34=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_830E1F38=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_830E1F3C=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_830E1F40=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_830E2138=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_830E213C=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_830E2140=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_830E2144=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_830E2148=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_830E214C=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_830E2340=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_830E2344=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_830E2348=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_830E234C=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_830E2350=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_830E2354=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_830E2358=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_830E4400=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_830E4404=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_830E5A68=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_830E5B68=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_830E5B70=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_830E5ED8=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_83116D74=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_83116D78=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_83116E70=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_83116E78=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_83116E7C=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_83116E80=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_83116E84=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_83116E88=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_83116E8C=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_83116E90=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_83116E94=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_83116E98=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_83116E9C=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_83116EA0=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_83116EA4=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_83116EA8=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_83116EAC=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_83116EB0=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_83116EB4=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_83116EB8=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_831178DC=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_831178E0=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_831178E4=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_831178E8=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_831178EC=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_83118BB4=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_83118BB8=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_83118BBC=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_83118BC0=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_83118BC4=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_83118BC8=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_83118BCC=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_83118BD0=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_83118BD4=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_83118BD8=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_83118BDC=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_83118BE0=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_83118BE4=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_83118BE8=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_83118BEC=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_83118BF0=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_83118BF4=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_83118BF8=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_83118BFC=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_83118C00=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_83118C04=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_83118C08=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_83118C0C=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_83118C10=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_83118C14=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_83118C18=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_83118C1C=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_83118C20=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_83118C24=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_83118C28=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_83118C2C=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_83118C30=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_83118C34=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_83118C38=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_83118C3C=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_83118C40=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_83118C44=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_83118C48=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_83118C4C=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_83118C50=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_83118C54=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_83118C58=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_83118C5C=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_83118C60=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_83118C64=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_83118C68=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_831190A0=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_831190A8=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_831190B0=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_831190B4=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_831190B8=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_831190BC=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_831190C0=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_831190C4=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_83119E78=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_83119E7C=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_83119E80=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_83119E84=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_83119E88=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_83119E8C=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_83119E90=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_83119E94=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_83119E98=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_83119E9C=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_83119EA0=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_83119EA4=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_83119EA8=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_83119EAC=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_83119EB0=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_83119EB4=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_83119EB8=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_83119EBC=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_83119EC0=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_83119EC4=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_83119EC8=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_83119ECC=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_83119ED0=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_83119ED4=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_83119ED8=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_83119EDC=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_83119EE0=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_83119EE4=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_83119EE8=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_83119EEC=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_83119EF0=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_83119EF4=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_83119EF8=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_83119EFC=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_83119F00=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_83119F04=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_83119F08=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_83119F0C=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_83119F10=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_83119F14=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_83119F18=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_83119F1C=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_83119F20=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_83119F24=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_83119F28=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_83119F2C=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_83119F30=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_83119F34=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_83119F38=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_83119F3C=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_83119F40=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_83119F44=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_83119F48=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_83119F4C=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_83119F50=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_83119F54=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_83119F58=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_83119F5C=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_83119F60=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_83119F64=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_83119F68=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_83119F6C=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_83119F70=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_83119F74=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_83119F78=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_83119F7C=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_83119F80=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_83119F84=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_83119F88=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_83119F8C=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_83119F90=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_83119F94=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_83119F98=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_83119F9C=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_83119FA0=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_83119FA4=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_83119FA8=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_83119FAC=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_83119FB0=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_83119FB4=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_83119FB8=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_83119FBC=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_83119FC0=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_83119FC4=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_83119FC8=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_83119FCC=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_83119FD0=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_83119FD4=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_83119FD8=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_83119FDC=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_83119FE0=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_83119FE4=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_83119FE8=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_83119FEC=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_83119FF0=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_83119FF4=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_83119FF8=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_83119FFC=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_8311A000=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_8311A004=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_8311A13C=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_8311A140=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_8311A144=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_8311A148=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_8311A14C=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_8311A150=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_8311A598=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_8311A59C=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_8311AA6C=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_8311AA70=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_8311AA74=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_8311AA78=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_8311AA7C=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_8311AA80=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_8311AA84=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_8311AA88=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_8311AA8C=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_8311AA90=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_8311AA94=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_8311AA98=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_8311AA9C=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_8311AAA0=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_8311AAA4=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_8311AAA8=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_8311AAAC=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_8311AAB0=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_8311AAB4=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_8311AAB8=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_8311AABC=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_8311AAC0=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_8311AAC8=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_8311AACC=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_8311AAD0=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_8311AAD4=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_8311AAD8=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_8311AADC=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_8311AAE0=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_8311AC2C=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_8311AC30=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_8311AC34=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_8311AC38=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_8311AC3C=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_8311AC40=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_8311AC44=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_8311AC48=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_8311AC4C=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_8311AC50=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_8311AC54=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_8311AC58=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_8311AC5C=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_8311AC60=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_8311AC64=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_8311AC68=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_8311AC6C=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_8311AC70=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_8311AC74=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_8311AC78=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_8311AC7C=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_8311AC80=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_8311AC84=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_8311AC88=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_8311AC8C=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_8311AC90=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_8311AC94=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_8311AC98=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_8311AC9C=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_8311ACA0=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_8311ACA4=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_8311ACA8=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_8311ACAC=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_8311ACB0=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_8311ACB4=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_8311ACB8=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_8311ACBC=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_8311ACC0=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_8311ACC4=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_8311ACC8=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_8311ACCC=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_8311ACD0=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_8311ACD4=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_8311ACD8=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_8311ACDC=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_8311ACE0=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_8311ACE4=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_8311ACE8=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_8311ACEC=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_8311ACF0=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_8311ACF4=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_8311ACF8=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_8311ACFC=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_8311AD00=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_8311AD04=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_8311AD08=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_8311AD0C=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_8311AD10=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_8311AD14=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_8311AD18=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_8311AD1C=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_8311AD20=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_8311AD24=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_8311AD28=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_8311AD2C=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_8311AD30=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_8311AD34=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_8311AD38=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_8311AD3C=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_8311AD40=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_8311AD44=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_8311ADB0=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_8311ADB4=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_8311ADB8=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_8311ADBC=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_8311ADC0=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_8311ADC8=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_8311ADCC=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_8311ADD0=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_8311ADD4=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_8311ADD8=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_8311ADDC=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_8311ADE0=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_8311ADE4=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_8311ADE8=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_8311ADEC=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_8311ADF0=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_8311ADF4=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_8311ADF8=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_8311ADFC=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_8311AE00=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_8311AE04=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_8311AE08=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_8311AE0C=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_8311AE10=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_8311AE14=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_8311AE18=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_8311AE1C=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_8311AE20=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_8311AE24=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_8311AE28=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_8311AE2C=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_8311AE30=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_8311AE34=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_8311AE38=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_8311AE3C=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_8311AE40=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_8311AE44=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_8311AE48=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_8311AE4C=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_8311AE50=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_8311AE54=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_8311AE58=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_8311AE5C=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_8311AE60=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_8311AE64=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_8311AE68=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_8311AE6C=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_8311AE70=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_8311AE74=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_8311AE78=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_8311AE7C=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_8311AE80=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_8311AE84=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_8311AE88=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_8311AE8C=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_8311AE90=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_8311AE94=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_8311AE98=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_8311AE9C=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_8311AEA0=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_8311AEA4=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_8311AEA8=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_8311AEAC=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_8311AEB0=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_8311AEB4=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_8311AEB8=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_8311AEBC=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_8311AEC0=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_8311AEC4=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_8311AEC8=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_8311AECC=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_8311AED0=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_8311AED4=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_8311AED8=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_8311AEDC=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_8311AEE0=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_8311AEE4=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_8311AEE8=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_8311AEEC=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_8311AEF0=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_8311AEF4=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_8311AEF8=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_8311AEFC=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_8311AF00=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_8311AF04=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_8311AF08=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_8311AF0C=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_8311AF10=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_8311AF14=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_8311AF18=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_8311AF1C=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_8311AF20=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_8311AF24=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_8311AF28=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_8311AF2C=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_8311AF30=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_8311AF34=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_8311AF38=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_8311AF3C=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_8311AF40=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_8311AF44=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_8311AF48=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_8311AF4C=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_8311AF50=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_8311AF54=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_8311AF58=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_8311AF5C=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_8311AF60=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_8311AF64=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_8311AF68=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_8311AF6C=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_8311AF70=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_8311AF74=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_8311AF78=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_8311AF7C=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_8311AF80=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_8311AF84=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_8311AFC0=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_8311AFC4=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_8311AFC8=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_8311AFCC=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_8311AFD0=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_8311AFD4=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_8311AFD8=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_8311AFDC=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_8311AFE4=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_8311AFE8=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_8311B06C=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_8311B070=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_8311B074=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_8311B078=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_8311B07C=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_8311B080=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_8311B084=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_8311B088=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_8311B08C=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_8311B090=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_8311B094=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_8311B098=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_8311B09C=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_8311B0A0=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_8311B0A4=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_8311B0A8=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_8311B0AC=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_8311B0B0=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_8311B0B4=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_8316B1FC=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_8316B200=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_8316B204=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_8316B208=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_8316B794=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_8316B798=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_8316B79C=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_8316B7A0=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_8316B7A4=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_8316B7A8=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_8316B7AC=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_8316B7B0=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_8316B7B4=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_8316B7B8=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_8316B7C0=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_8316C808=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_8316C80C=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_8316C860=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_8316C864=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_8316EB70=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_8316EBA8=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_83172BB0=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_83176BE8=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:lbl_83176BEC=__link_glue_zero")

// -- Game/engine functions (535 symbols) --
#pragma comment(linker, "/ALTERNATENAME:?$S1@?1??StrToCharacterSym@@YA?AVSymbol@@VString@@@Z@4IA=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?$S2@?1??StrToCrewSym@@YA?AVSymbol@@VString@@@Z@4IA=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?$S3@?1??CamShotVOData@@YAXVSymbol@@AAV2@111@Z@4IA=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?$S4@?1??IsRest@MoveVariant@@QBA_NXZ@4IA=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??0?$CSampleXAPOBase@VSynapseAPO@DSP@@USynapseAPOParams@2@@DSP@@QAA@XZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??0CXAPOBase@ATG@@QAA@XZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??0CXAPOParametersBase@ATG@@QAA@PBXPAXIE@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??0CacheIDXbox@@QAA@ABV0@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??0EventTask@@QAA@PAVFlowTimer@@PAV?$ObjPtrVec@VFlowNode@@VObjectDir@@@@W4TaskUnits@@M@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??0GranularSynth@Synapse@DSP@@QAA@ABV?$vector@MV?$StlNodeAlloc@M@stlpmtx_std@@@stlpmtx_std@@III@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??0HamListRibbonDrawState@@QAA@XZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??0HeadsetXferEffect@@QAA@XZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??0ID3DXInclude@@QAA@XZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??0NavListFunctionNode@@QAA@PAVNavListItemSortCmp@@VSymbol@@PBD@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??0PeakDetector@Synapse@DSP@@QAA@ABV?$vector@MV?$StlNodeAlloc@M@stlpmtx_std@@@stlpmtx_std@@II@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??0PitchCorrectedVoice@Synapse@DSP@@QAA@XZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??0PlaylistSortByType@@QAA@XZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??0Point@CharHair@@QAA@PAVObject@Hmx@@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??0RecurseInfo@@QAA@ABU0@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??0SongSortBySong@@QAA@XZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??1AppLabel@@UAA@XZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??1CXAPOBase@@UAA@XZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??1FitnessCalorieSortByCalorie@@UAA@XZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??1FitnessCalorieSortCmp@@UAA@XZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??1GranularSynth@Synapse@DSP@@QAA@XZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??1MQSongSortByCharacter@@UAA@XZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??1PeakDetector@Synapse@DSP@@QAA@XZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??1PitchCorrectedVoice@Synapse@DSP@@QAA@XZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??1PropertyTask@@UAA@XZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??1SingleItemEnumJob@@UAA@XZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??1SongSortByLocation@@UAA@XZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??4Skeleton@@QAAAAV0@ABV0@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??4SpotDrawParams@@QAAAAV0@ABV0@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??6@YAAAVBinStream@@AAV0@ABUConstraintSystem@CharBlendBone@@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??6@YAAAVBinStream@@AAV0@ABUTarget@HamCamShot@@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??8Rect@Hmx@@QBA_NABV01@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??R?$VectorSort@PAVRndMesh@@@@QAA_NPAVRndMesh@@0@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??_7ChallengeScoreCmp@@6B@=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??_R0PAUSongCollisionOutput@@@8=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?AcquireFontMap@RndText@@KAPAVFontMapBase@1@PAVRndFontBase@@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?ActivateVoiceContext@VoiceInputPanel@@AAAXVSymbol@@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?AddData@MicXbox@@QAAXPAXH@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?AddFrame@RhythmDetector@@AAAXABVBaseSkeleton@@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?AddInterestObject@CharEyes@@QAAXPAVCharInterest@@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?AddMic@MicManagerXbox@@QAAXPAVMicXbox@@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?AddPendingProfile@FitnessGoalMgr@@AAAXPAVHamProfile@@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?AddPolls@SharedGroup@@AAAXPAVRndGroup@@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?AddWeight@Generator@CharLipSync@@QAAXHM@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?Alloc@MemHeap@@QAAPAHHHAAH@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?Analyze@SpectralAnalysis@DSP@@QAAXPBMPAM@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?Animate@LightPreset@@IAAXM@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?AssignRandomColors@WorldCrowd@@IAAX_N@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?BinkClose@@YAXPAUBINK@@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?BinkCloseTrack@@YAXPAUBINKTRACK@@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?BinkFileIdle@@YAIPAUBINKIO@@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?BinkGetTrackData@@YAIPAUBINKTRACK@@PAX@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?BinkNextFrame@@YAXPAUBINK@@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?BinkOpenTrack@@YAPAUBINKTRACK@@PAUBINK@@E@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?BinkSetMemory@@YAXP6APAXH@ZP6AXPAX@Z@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?BinkStartAsyncThread@@YAHHH@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?BleedTest@RndFont@@QAAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?BuildBillboard@WorldCrowd@@IAAPAVRndMesh@@PAVCharacter@@M@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?BurnTransform@RndAmbientOcclusion@@IBAXPAVRndMesh@@AAV?$list@PAVRndMesh@@V?$StlNodeAlloc@PAVRndMesh@@@stlpmtx_std@@@stlpmtx_std@@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?BuySpecialOffer@HamStorePanel@@IAA_NVSymbol@@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?CHARACTERS@?1??StrToCharacterSym@@YA?AVSymbol@@VString@@@Z@4V2@A=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?CREWS@?1??StrToCrewSym@@YA?AVSymbol@@VString@@@Z@4V2@A=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?CacheFrames@LightPreset@@IAAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?CalcSpline@@YAMMQAM@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?CalculateAO@RndAmbientOcclusion@@QAAXPAM@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?CanDraw@DxMesh@@IBA_NXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?CheckRecommendedPracticeMove@MetaPerformer@@IBA_NVString@@H@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?Cleanup@EventTrigger@@KA?AVDataNode@@PAVDataArray@@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?ClearActionToken@InlineHelp@@QAAXW4JoypadAction@@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?ClearJump@DanceRemixer@@QAAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?ClearOldCrowds@HamCamTransform@@QAAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?Clip@@YAXABVPolygon@Hmx@@ABVRay@2@AAV12@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?CollideShowing@DrawPtrVec@@QBAPAVRndDrawable@@ABVSegment@@AAMAAVPlane@@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?CompressThread@@YAKPAX@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?ComputeCharWidthsForText@RndText@@QAAMVString@@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?ComputeHandPullAndQuat@HamIKEffector@@IAAXAAVQuatXfm@@AAVTransform@@ABV3@ABVVector3@@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?ComputeRandomChoiceSet@MoveMgr@@QAAHH@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?ConvertBonesToTranses@@YAXPAVObjectDir@@_N@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?Copy@DxMesh@@UAAXPBVObject@Hmx@@W4CopyType@23@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?Copy@Flow@@UAAXPBVObject@Hmx@@W4CopyType@23@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?Copy@HamCamTransform@@UAAXPBVObject@Hmx@@W4CopyType@23@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?CopyPostProcess@DxRnd@@AAAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?CopyRef@?$ObjRefConcrete@VADSR@@VObjectDir@@@@QAAXABV1@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?CopyRef@?$ObjRefConcrete@VCamShot@@VObjectDir@@@@QAAXABV1@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?CopyRef@?$ObjRefConcrete@VCharBonesObject@@VObjectDir@@@@QAAXABV1@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?CopyRef@?$ObjRefConcrete@VCharCollide@@VObjectDir@@@@QAAXABV1@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?CopyRef@?$ObjRefConcrete@VCharFaceServo@@VObjectDir@@@@QAAXABV1@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?CopyRef@?$ObjRefConcrete@VCharLipSync@@VObjectDir@@@@QAAXABV1@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?CopyRef@?$ObjRefConcrete@VCharLipSyncDriver@@VObjectDir@@@@QAAXABV1@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?CopyRef@?$ObjRefConcrete@VCharLookAt@@VObjectDir@@@@QAAXABV1@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?CopyRef@?$ObjRefConcrete@VCharWeightSetter@@VObjectDir@@@@QAAXABV1@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?CopyRef@?$ObjRefConcrete@VDancerSequence@@VObjectDir@@@@QAAXABV1@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?CopyRef@?$ObjRefConcrete@VFxSendMeterEffect@@VObjectDir@@@@QAAXABV1@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?CopyRef@?$ObjRefConcrete@VHamCamShot@@VObjectDir@@@@QAAXABV1@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?CopyRef@?$ObjRefConcrete@VHamIKEffector@@VObjectDir@@@@QAAXABV1@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?CopyRef@?$ObjRefConcrete@VHamIKSkeleton@@VObjectDir@@@@QAAXABV1@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?CopyRef@?$ObjRefConcrete@VHamNavProvider@@VObjectDir@@@@QAAXABV1@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?CopyRef@?$ObjRefConcrete@VLightHue@@VObjectDir@@@@QAAXABV1@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?CopyRef@?$ObjRefConcrete@VLightPreset@@VObjectDir@@@@QAAXABV1@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?CopyRef@?$ObjRefConcrete@VMetaMaterial@@VObjectDir@@@@QAAXABV1@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?CopyRef@?$ObjRefConcrete@VRndDir@@VObjectDir@@@@QAAXABV1@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?CopyRef@?$ObjRefConcrete@VRndLight@@VObjectDir@@@@QAAXABV1@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?CopyRef@?$ObjRefConcrete@VRndMultiMesh@@VObjectDir@@@@QAAXABV1@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?CopyRef@?$ObjRefConcrete@VRndPostProc@@VObjectDir@@@@QAAXABV1@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?CopyRef@?$ObjRefConcrete@VRndPropAnim@@VObjectDir@@@@QAAXABV1@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?CopyRef@?$ObjRefConcrete@VSeqInst@@VObjectDir@@@@QAAXABV1@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?CopyRef@?$ObjRefConcrete@VSequence@@VObjectDir@@@@QAAXABV1@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?CopyRef@?$ObjRefConcrete@VSfx@@VObjectDir@@@@QAAXABV1@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?CopyRef@?$ObjRefConcrete@VSkeletonClip@@VObjectDir@@@@QAAXABV1@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?CopyRef@?$ObjRefConcrete@VUILabelDir@@VObjectDir@@@@QAAXABV1@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?CopyTypeProperties@@YAXPAVObject@Hmx@@0@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?CreateDefaultTexture@Rnd@@IAAPAVRndTex@@W4DefaultTextureType@1@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?CreatePlaylistEditorGrammar@VoiceInputPanel@@QBAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?CurRecordedFrame@SkeletonClip@@IBAPBURecordedFrame@@AAH0@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?D3DFORMAT_BitsPerPixel@@YAHW4_D3DFORMAT@@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?DeSelect@SpotlightDrawer@@QAAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?DecodeThreadEntry@?A0xcb0871ef@@YAKPAX@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?DeleteChecksum@FileStream@@AAAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?DeleteParentDirs@CacheXbox@@IAA_NVString@@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?DeleteRemaining@CharClipGroup@@QAAXH@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?Detect@PeakDetector@Synapse@DSP@@QAAXI@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?DetectFrac@MoveDir@@QAAMHH@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?DetermineHighlightedItem@HamNavList@@AAAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?DiffTblReport@@YAXPBDAAVBlockStatTable@@1AAVTextStream@@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?Display@CharDriver@@IAAMM@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?DoCompress@DxTex@@QAAXPAX@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?DoFileRead@VorbisReader@@AAA_NXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?DoReset@CharHair@@IAAXH@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?Draw@HamListRibbon@@QAAXABVTransform@@ABV?$vector@UHamListRibbonDrawState@@V?$StlNodeAlloc@UHamListRibbonDrawState@@@stlpmtx_std@@@stlpmtx_std@@_N2@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?DrawBufferMat@@YAXPAVRndMat@@AAVRect@Hmx@@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?DrawDebug@HamNavList@@QBAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?DrawDebug@SkeletonIdentifier@@QAAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?DrawLight@SpotlightDrawer@@SAXPAVSpotlight@@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?DrawShowing@HamNavList@@UAAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?DrawShowing@InlineHelp@@UAAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?DrawShowing@RndGroup@@UAAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?DrawTimers@Rnd@@IAAMM@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?EvaluateChannel@CharBonesSamples@@QAAXPAXHHM@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?ExtractGranules@GranularSynth@Synapse@DSP@@QAAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?EyesOnTarget@CharEyes@@IAA_NM@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?FFTComplex@@YAHPAMJJ0@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?FFTRealForward@@YAHPAMK0@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?FillEnvPresetData@LightPreset@@IAAXPAVRndEnviron@@AAUEnvironmentEntry@1@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?FillMoveRatings@SkeletonClip@@QAAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?FillRoutineFromReplacer@MoveMgr@@QAAXH@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?FillRoutineFromVerses@MoveMgr@@QAAXH@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?FinalPoseStateMachine@MoveDir@@QAAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?FindBestNodeRecurse@ClipDistMap@@IAAXMMMMM@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?FindInterestObjects@Character@@QAAXPAVObjectDir@@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?FindRef@?$ObjPtrVec@VRndTransformable@@VObjectDir@@@@QAA?AViterator@1@PAVObjRef@@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?FindSplit_SAH@kdTreeNode@?$kdTree@VTriangle@@@@QAA_NABVBox@@ABV?$list@PAVTriangle@@V?$StlNodeAlloc@PAVTriangle@@@stlpmtx_std@@@stlpmtx_std@@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?FinishCompress@DxTex@@QAAXPAX@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?FinishPostProcess@DxRnd@@AAAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?FixVertOrder@@YAXPBVRndMesh@@PAV1@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?Flush@GranularSynth@Synapse@DSP@@QAAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?ForeachKeyframe@RndPropAnim@@QAA?AVDataNode@@PBVDataArray@@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?FreezePoseRaw@CharHair@@QAAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?Generate@RndGenerator@@QAAXM@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?GenerateDartOffset@CharEyes@@IAA?AVVector3@@XZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?GetAddr@Voice@@QAAHXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?GetAlignWords@MemHeap@@SAHH@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?GetAutoexposure@LiveCameraInput@@QBA_NXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?GetChallengerXp@ChallengeSortMgr@@QAAHH@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?GetClipStartAndEndBeats@HamDirector@@IAAPAVCharClip@@VSymbol@@AAM1PAU?$pair@MM@stlpmtx_std@@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?GetCorrection@PitchCorrectedVoice@Synapse@DSP@@QAAMXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?GetDisabledCount@HamNavList@@ABAHH@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?GetKey@LightPreset@@IBAXMAAH0AAM@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?GetLastResult@Cache@@QAA?AW4CacheResult@@XZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?GetName@?$ResourceDirPtr@VUILabelDir@@@@QBAPBDXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?GetPlaylistID@AddPlaylistJob@@QAAXPAVCustomPlaylist@@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?GetPotentialChallengeExp@ChallengeHeaderNode@@QAAHPAVNavListSortNode@@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?GetRenderTexturesNoZ@@YA?AVDataNode@@PAVObjectDir@@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?GetSongShortTitle@ChallengeHeaderNode@@QAA?AVString@@XZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?GetSustain@UsbMidiKeyboard@@QAA_NH@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?GetTargetSwellAmount@HamNavList@@AAAMH@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?GetTotalEarnedExp@ChallengeHeaderNode@@QAAHH@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?GetTweakedAutoexposure@LiveCameraInput@@QBA_NXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?GetWidthHeightBox@RndText@@QBAXAAVBox@@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?HandleCmdChangeProfileOnlineID@PlaylistSortMgr@@AAAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?HandleEventResponse@SaveLoadManager@@QAAXPAVHamProfile@@H@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?HighpassCoefficients@DSP@@YAXQAMMMM@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?Hookup@CharHair@@QAAXAAV?$ObjPtrList@VCharCollide@@VObjectDir@@@@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?INTRO_CAM_CATS@?1??CamShotVOData@@YAXVSymbol@@AAV2@111@Z@4V2@A=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?INTRO_PLAYLIST@?7??CamShotVOData@@YAXVSymbol@@AAV2@111@Z@4V2@A=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?INTRO_QUICK@?7??CamShotVOData@@YAXVSymbol@@AAV2@111@Z@4V2@A=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?INTRO_SKILLS@?7??CamShotVOData@@YAXVSymbol@@AAV2@111@Z@4V2@A=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?INTRO_SKILLS_LONG@?7??CamShotVOData@@YAXVSymbol@@AAV2@111@Z@4V2@A=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?Init@DxTex@@SAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?InitKeyCheats@@YAXPBVDataArray@@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?InitParticle@RndParticleSys@@IAAXMPAVRndParticle@@PBVTransform@@AAVPartOverride@@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?Intersect@@YA_NABVSegment@@ABVTriangle@@HAAM@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?Intersect@@YA_NABVTransform@@ABVPolygon@Hmx@@PBVBSPNode@@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?IsEquivalent@MetaMaterial@@QAA_NPAV1@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?IsMaxGain@RecipeTable@TrueColor@@QBA_NXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?IsPlaying@Voice@@QAA_NXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?IsProfileChanged@FitnessGoalMgr@@AAA_NXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?IsSpecialOfferOwned@HamStorePanel@@IBA_NVSymbol@@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?IsValid_Tessellate@RndAmbientOcclusion@@IBA_NPBVRndMesh@@PBVObjectDir@@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?JumpedBeat@DanceRemixer@@QBAMM@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?JumpedMeasureAdd@DanceRemixer@@QBAHHH@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?JumpedMeasureStepsBetween@DanceRemixer@@QBAHHHH@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?Link@?$ObjPtrList@VCharBone@@VObjectDir@@@@AAAXViterator@1@PAUNode@1@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?Link@?$ObjPtrList@VCharCollide@@VObjectDir@@@@AAAXViterator@1@PAUNode@1@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?Link@?$ObjPtrList@VCharInterest@@VObjectDir@@@@AAAXViterator@1@PAUNode@1@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?Link@?$ObjPtrList@VNoteVoiceInst@@VObjectDir@@@@AAAXViterator@1@PAUNode@1@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?Link@?$ObjPtrList@VRndFontBase@@VObjectDir@@@@AAAXViterator@1@PAUNode@1@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?Link@?$ObjPtrList@VRndPartLauncher@@VObjectDir@@@@AAAXViterator@1@PAUNode@1@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?Link@?$ObjPtrList@VRndTexBlendController@@VObjectDir@@@@AAAXViterator@1@PAUNode@1@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?Link@?$ObjPtrList@VSeqInst@@VObjectDir@@@@AAAXViterator@1@PAUNode@1@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?Link@?$ObjPtrList@VSfxInst@@VObjectDir@@@@AAAXViterator@1@PAUNode@1@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?Link@?$ObjPtrList@VUILabel@@VObjectDir@@@@AAAXViterator@1@PAUNode@1@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?Link@?$ObjPtrList@VWorldCrowd@@VObjectDir@@@@AAAXViterator@1@PAUNode@1@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?ListProperties@@YAXAAV?$list@VSymbol@@V?$StlNodeAlloc@VSymbol@@@stlpmtx_std@@@stlpmtx_std@@VSymbol@@1PAV12@_N@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?Load@CharEyes@@UAAXAAVBinStream@@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?Load@HamCamShot@@UAAXAAVBinStream@@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?Load@HamCamTransform@@UAAXAAVBinStream@@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?Load@LightPreset@@UAAXAAVBinStream@@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?Load@PhysicsVolume@@UAAXAAVBinStream@@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?Load@RndScreenMask@@UAAXAAVBinStream@@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?Load@WorldInstance@@UAAXAAVBinStream@@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?LoadArt@MainMenuPanel@@AAAXVString@@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?LoadCharacters@HamWardrobe@@QAAXVSymbol@@000W4HamBackupDancers@@00_N@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?LoadDebugDepthBuffer@?A0x8e584365@@YAXAAPAVRndTex@@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?LoadFrame@SkeletonClip@@KAXAAVBinStream@@AAURecordedFrame@@H@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?LoadStages@RndMatAnim@@IAAXAAVBinStreamRev@@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?LowpassCoefficients@DSP@@YAXQAMMMM@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?MakeBSPTree@@YA_NAAPAVBSPNode@@AAV?$list@VBSPFace@@V?$StlNodeAlloc@VBSPFace@@@stlpmtx_std@@@stlpmtx_std@@H@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?MakeSkeletonFrame@RecordedFrame@@QBAXAAUSkeletonFrame@@H@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?MapVerts@RndLine@@IAAXHAAVVertsMap@1@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?MergeObjectsRecurse@@YAXPAVObjectDir@@0AAVMergeFilter@@_N@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?Modal@Rnd@@QAAXAAW4ModalType@Debug@@AAVFixedString@@_N@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?MoveObject@RndGroup@@QAAHPAVObject@Hmx@@H@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?MoveVariantFromHamMove@DanceRemixer@@QBAPBVMoveVariant@@PBVHamMove@@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?New@DirLoader@@SAPAVLoader@@ABVFilePath@@W4LoaderPos@@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?NuiAudioDataCallback@LiveCameraInput@@SAXPAU_NUIAUDIO_RESULTS@@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?OUTRO_CAM_CATS@?1??CamShotVOData@@YAXVSymbol@@AAV2@111@Z@4V2@A=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?OnAddCrowd@HamWardrobe@@IAA?AVDataNode@@PAVDataArray@@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?OnAddInterest@CharEyes@@IAA?AVDataNode@@PAVDataArray@@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?OnAllowableNextShots@HamCamShot@@AAA?AVDataNode@@PBVDataArray@@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?OnBeat@HollaBackMinigame@@QAAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?OnCameraDebugDepth@@YA?AVDataNode@@PAVDataArray@@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?OnCameraDumpUnique@@YA?AVDataNode@@PAVDataArray@@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?OnCompletion@SingleItemEnumJob@@UAAXPAVObject@Hmx@@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?OnCycleAutoplay@@YA?AVDataNode@@PAVDataArray@@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?OnCycleTestDancer@@YA?AVDataNode@@PAVDataArray@@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?OnDumpMoves@@YA?AVDataNode@@PAVDataArray@@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?OnIterateFrac@WorldCrowd@@IAA?AVDataNode@@PAVDataArray@@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?OnLoadCharacters@HamWardrobe@@IAA?AVDataNode@@PAVDataArray@@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?OnMarkerEvent@FlowSound@@IAAXVSymbol@@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?OnMsg@CheatsManager@@AAA?AVDataNode@@ABVKeyboardKeyMsg@@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?OnMsg@CheatsManager@@AAAHABVButtonDownMsg@@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?OnMsg@FitnessGoalMgr@@AAA?AVDataNode@@ABVRCJobCompleteMsg@@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?OnMsg@HamNavList@@AAA?AVDataNode@@ABVButtonDownMsg@@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?OnMsg@HamStorePanel@@IAA?AVDataNode@@ABVRCJobCompleteMsg@@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?OnMsg@PlaylistSortMgr@@AAA?AVDataNode@@ABVRCJobCompleteMsg@@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?OnMsg@SaveLoadManager@@IAA?AVDataNode@@ABVMCResultMsg@@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?OnMsg@SaveLoadManager@@IAA?AVDataNode@@ABVSigninChangedMsg@@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?OnMsg@SkeletonIdentifier@@AAA?AVDataNode@@ABVSigninChangedMsg@@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?OnMsg@SkeletonIdentifier@@AAA?AVDataNode@@ABVSkeletonIdentifiedMsg@@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?OnMsg@VoiceInputPanel@@QAA?AVDataNode@@ABVSpeechRecoMessage@@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?OnPopulateFromMoveMgr@HamDirector@@IAAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?OnPopulateMoves@HamDirector@@IAAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?OnSelectCamera@HamDirector@@IAA?AVDataNode@@PAVDataArray@@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?OnSetKeyframe@LightPreset@@IAA?AVDataNode@@PAVDataArray@@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?OnSetVenue@HamWardrobe@@IAA?AVDataNode@@PAVDataArray@@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?OnSignInUsers@PlatformMgr@@AAA?AVDataNode@@PBVDataArray@@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?OnSmartGlassListen@FitnessGoalMgr@@AAAXH@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?OnTestDrawGroups@@YA?AVDataNode@@PAVDataArray@@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?OnToggleForceFocus@CharEyes@@IAA?AVDataNode@@PAVDataArray@@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?OnToggleHeap@Rnd@@IAA?AVDataNode@@PBVDataArray@@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?OnToggleInterestOverlay@CharEyes@@IAA?AVDataNode@@PAVDataArray@@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?ParseArray@@YAPAVDataArray@@XZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?Pause@SfxInst@@QAAX_N@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?Pause@Voice@@QAAX_N@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?PickNextIndex@RandomGroupSeq@@QAAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?PlayEnterAnim@HamNavList@@QAAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?PlayNextShot@HamDirector@@IAAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?Poll@CameraManager@@QAAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?Poll@FilterQueue@@QAAXABUSkeletonUpdateData@@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?Poll@MicManagerXbox@@QAAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?PollFrontLoader@LoadMgr@@AAAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?PostLoad@WorldInstance@@UAAXAAVBinStream@@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?PostUpdateFilters@MoveDir@@AAAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?PreLoad@InlineHelp@@UAAXAAVBinStream@@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?PreSave@WorldInstance@@UAAXAAVBinStream@@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?ProcessFrames@RhythmDetector@@AAAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?ProcessNextCommand@FitnessGoalMgr@@AAAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?QueueCmdChangeProfileOnlineID@PlaylistSortMgr@@AAAXVString@@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?QueueCmdGetPlaylistsFromRC@PlaylistSortMgr@@AAAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?RadAlloc@@YAPAXH@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?ReFitTextScroll@RndText@@QAAXVString@@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?RealRefresh@HamNavList@@AAAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?RecordedFrameAt@SkeletonClip@@SAPBURecordedFrame@@ABV?$vector@URecordedFrame@@V?$StlNodeAlloc@URecordedFrame@@@stlpmtx_std@@@stlpmtx_std@@MAAH1@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?RefreshSpecialOfferStatus@HamStorePanel@@IAAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?Release@VertexBufferData@DxMesh@@QAAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?ReleaseAutoRelease@DxRnd@@QAAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?RemoveFromLists@SpotlightDrawer@@SAXPAVSpotlight@@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?RemoveMic@MicManagerXbox@@QAAXPAVMicXbox@@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?Replace@CharBonesMeshes@@MAA_NPAVObjRef@@PAVObject@Hmx@@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?Replace@CharEyes@@UAA_NPAVObjRef@@PAVObject@Hmx@@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?Replace@LightPreset@@UAA_NPAVObjRef@@PAVObject@Hmx@@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?Replace@RndMatAnim@@UAA_NPAVObjRef@@PAVObject@Hmx@@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?Replace@RndMeshAnim@@UAA_NPAVObjRef@@PAVObject@Hmx@@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?ReportMemoryAlloc@MemTracker@@QAAXPBD@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?ReportMemoryUsage@MemTracker@@QAAXPBD@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?ReportMemoryUsageOverview@MemTracker@@QAAXPBD@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?Reset3DCrowd@WorldCrowd@@IAAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?ResetFontMapPageMeshFaces@@YAXPAVRndMesh@@H@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?Rest@?1??IsRest@MoveVariant@@QBA_NXZ@4VSymbol@@A=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?RndScaleObject@@YAXPAVObject@Hmx@@MM@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?Save@CharBonesSamples@@QAAXAAVBinStream@@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?SavePersistentObjects@WorldInstance@@AAAXAAVBinStream@@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?ScaleAddEq@@YAXAAVQuat@Hmx@@ABV12@M@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?ScaleAddEq@@YAXAAVTransform@@ABV1@M@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?ScanForOutPorts@@YAXAAV?$ObjPtrVec@VFlowOutPort@@VObjectDir@@@@PAVFlowNode@@PAVFlow@@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?ScrollToIndex@HamNavList@@QAAXHH@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?SendJoypadMessages@HolmesInput@@QAAIXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?SendPassiveMsg@PlaylistSortMgr@@AAAXVSymbol@@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?Set@BSPFace@@QAAXABVVector3@@00@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?SetAutoexposure@LiveCameraInput@@QAA_N_N@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?SetBloomBlurWeightsStreak@@YAX_NMMMHM@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?SetClipFlags@CharClipGroup@@QAAXH@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?SetClipWeightMap@CharDriver@@QAAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?SetClips@PlayBack@CharLipSync@@QAAXV?$ObjPtr@VObjectDir@@@@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?SetColorCameraProperty@?A0x8e584365@@YAXW4_NUI_CAMERA_PROPERTY@@J@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?SetColorWriteMask@@YAXABUShaderOptions@@PAVRndMat@@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?SetDefaultPattern@SongLayout@@QAAXH@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?SetDefaultReplacer@SongLayout@@QAAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?SetExposureRegion@LiveCameraInput@@QAAXMMMM@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?SetJump@DanceRemixer@@QAAXHH@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?SetKeyframe@LightPreset@@IAAXAAUKeyframe@1@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?SetLocalGain@@YA?AVDataNode@@PAVDataArray@@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?SetLowCut@@YA?AVDataNode@@PAVDataArray@@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?SetMode@SpectralAnalysis@DSP@@QAAXII@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?SetNavProvider@HamNavList@@QAAXPAVHamNavProvider@@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?SetNoiseGate@@YA?AVDataNode@@PAVDataArray@@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?SetPan@Sound@@QAAXMPAVObject@Hmx@@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?SetPointsColor@RndLine@@QAAXHHABVColor@Hmx@@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?SetReleaseSmoothing@PitchCorrectedVoice@Synapse@DSP@@QAAXM@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?SetRemoteGain@@YA?AVDataNode@@PAVDataArray@@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?SetReverbEnable@SfxInst@@QAAX_N@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?SetReverbMixDb@SfxInst@@QAAXM@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?SetRibbonMode@HamNavList@@AAAXW4RibbonMode@HamListRibbon@@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?SetRoot@Strand@CharHair@@QAAXPAVRndTransformable@@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?SetSelecting@HamNavList@@AAAX_N@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?SetSend@SfxInst@@QAAXPAVFxSend@@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?SetSetlistMode@SongSortMgr@@QAAX_N@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?SetSpeed@Sound@@QAAXMPAVObject@Hmx@@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?SetSpeed@Voice@@QAAXM@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?SetState@XLSPConnection@@AAAXW4State@1@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?SetTrackedSkeletons@LiveCameraInput@@QBAXHH@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?SetTransposition@PitchCorrectedVoice@Synapse@DSP@@QAAXM@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?SetTweakedAutoexposure@LiveCameraInput@@QAA_N_N@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?Setup@HamCamTransform@@IAAX_N@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?SetupQuasiRandomSongs@SongSortMgr@@QAAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?ShowGamercardForPadNum@PlatformMgr@@QAA?AW4ShowGamercardResult@@HPBVOnlineID@@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?ShrinkVerts@RndMeshAnim@@QAAXH@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?Shutdown@MicManagerXbox@@QAAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?SimulateLoops@CharHair@@IAAXHM@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?SongStartSeconds@SkeletonClip@@QBAMXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?SphereConeTest@NgLight@@IAA_NABVVector3@@M@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?SpitAllocInfo@MemTracker@@SAHPAU_iobuf@@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?StartCompress@DxTex@@QAAPAXW4AlphaCompress@RndTex@@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?StartVoiceThreadEntry@@YAKPAX@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?Stop@Voice@@QAAX_N@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?StopSynchronizedVoices@@YAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?StoreColorBufferClip@TextureStore@LiveCameraInput@@QAAXPAV2@MMMM@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?StoreDepthBufferClip@TextureStore@LiveCameraInput@@QAAXPAV2@MMMM@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?StoreTextureClip@CamTexClip@@QAAXPAVRndTex@@MMMM@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?SwapMoveRecord@SkeletonClip@@QAAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?Sync@BitmapOverride@WorldDir@@QAAX_N@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?SyncDir@WorldInstance@@AAAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?SyncShadow@Character@@IAAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?SyncState@Song@@QAAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?Synthesize@GranularSynth@Synapse@DSP@@QAAXIPBQAM@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?Terminate@BinkMovieImpl@@QAAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?TerminateVoiceThread@@YAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?Tessellate@RndAmbientOcclusion@@QAAXPAM0@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?TestMaterialTextures@@YAXPAVObjectDir@@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?TestPoint@Rnd@@QAAXABVVector3@@PAVRndFlare@@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?TestTexturePaths@@YAXPAVObjectDir@@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?TestTextureSize@@YAXPAVObjectDir@@HHHHH@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?ThreadDelete@CacheXbox@@IAAHXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?ThreadGetDir@CacheXbox@@IAAHVString@@0@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?ThreadRead@CacheXbox@@IAAHXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?TriggerSelf@EventTrigger@@IAAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?TryAlloc@MemHeap@@QAAPAHHHAAH@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?Unlink@?$ObjPtrList@VCamShot@@VObjectDir@@@@AAAPAUNode@1@PAU21@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?Update@HamNavList@@IAAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?Update@HamScrollSpeedIndicator@@QAAXMMM@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?UpdateActivations@FlowSlider@@IAAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?UpdateAndDrawWrapper@LabelShrinkWrapper@@IAAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?UpdateChars@RndFont@@IAAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?UpdateChase@HamRibbon@@QAAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?UpdateColorModulation@RndPostProc@@IAAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?UpdateCurrPlaylistWithRC@PlaylistSortMgr@@AAAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?UpdateFromColorBuffer@TextureStore@LiveCameraInput@@QAAXPAV2@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?UpdateFromDepthBuffer@TextureStore@LiveCameraInput@@QAAXPAV2@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?UpdateGestures@HamNavList@@AAAXPBVSkeleton@@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?UpdateLine@RndLine@@IAAXABVTransform@@M@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?UpdateMix@Voice@@AAAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?UpdateRate@Rnd@@IAAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?UpdateSelfInRows@AppMiniLeaderboardDisplay@@AAAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?UpdateSends@Voice@@AAAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?UpdateTargetsFlipped@HamCamShot@@IAAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?UpdateVarsObjects@ScriptTask@@IAAXPAVDataArray@@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?UpdateVolumes@StandardStream@@AAAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?UtilDrawCigar@@YAXABVTransform@@QBM1ABVColor@Hmx@@H@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?UtilDrawCircle2D@@YAXABVVector2@@MABVColor@Hmx@@H@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?UtilDrawPlane@@YAXABVPlane@@ABVVector3@@ABVColor@Hmx@@HM_N@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?VertFVF@DxMesh@@IBAIXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?VertSize@DxMesh@@IBAIXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?WIN_HYPE_DIFF_CREW@?EB@??CamShotVOData@@YAXVSymbol@@AAV2@111@Z@4V2@A=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?WIN_HYPE_SOLO@?EB@??CamShotVOData@@YAXVSymbol@@AAV2@111@Z@4V2@A=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?_Copy_str@exception@std@@AAAXPBD@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?_M_create_node@?$_Rb_tree@PAVCharClip@@U?$less@PAVCharClip@@@stlpmtx_std@@U?$pair@QAVCharClip@@M@3@U?$_Select1st@U?$pair@QAVCharClip@@M@stlpmtx_std@@@3@U?$_MapTraitsT@U?$pair@QAVCharClip@@M@stlpmtx_std@@@priv@3@V?$StlNodeAlloc@U?$pair@QAVCharClip@@M@stlpmtx_std@@@3@@stlpmtx_std@@IAAPAU_Rb_tree_node_base@2@ABU?$pair@QAVCharClip@@M@2@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?_M_erase@?$vector@VFace@RndMesh@@V?$StlNodeAlloc@VFace@RndMesh@@@stlpmtx_std@@@stlpmtx_std@@IAAPAVFace@RndMesh@@PAV34@ABU__false_type@2@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?_M_fill_insert_aux@?$vector@FV?$StlNodeAlloc@F@stlpmtx_std@@@stlpmtx_std@@AAAXPAFIABFABU__false_type@2@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?__pop_heap_aux@stlpmtx_std@@YAXPAUMemDiffEntry@@0HU?$less@UMemDiffEntry@@@1@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?active@?1??CamShotVOData@@YAXVSymbol@@AAV2@111@Z@4V2@A=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?all@?1??CamShotVOData@@YAXVSymbol@@AAV2@111@Z@4V2@A=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?battle_intro_crew@?1??CamShotVOData@@YAXVSymbol@@AAV2@111@Z@4V2@A=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?battle_outro_crew@?1??CamShotVOData@@YAXVSymbol@@AAV2@111@Z@4V2@A=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?blockingStart@Voice@@QAAX_N@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?camp_intro_crew@?1??CamShotVOData@@YAXVSymbol@@AAV2@111@Z@4V2@A=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?createFilter@@YAXW4FilterType@@W4FilterBand@@IMMPAUFILTER@@H@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?deallocate@?$StlNodeAlloc@USongCollisionOutput@@@stlpmtx_std@@QBAXPAUSongCollisionOutput@@I@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?dispose@Voice@@AAAXPAHI@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?erase@?$ObjPtrList@VCharBone@@VObjectDir@@@@QAA?AViterator@1@V21@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?erase@?$ObjPtrList@VCharCollide@@VObjectDir@@@@QAA?AViterator@1@V21@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?erase@?$ObjPtrList@VCharInterest@@VObjectDir@@@@QAA?AViterator@1@V21@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?erase@?$ObjPtrList@VNoteVoiceInst@@VObjectDir@@@@QAA?AViterator@1@V21@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?erase@?$ObjPtrList@VRndTexBlendController@@VObjectDir@@@@QAA?AViterator@1@V21@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?erase@?$ObjPtrList@VSeqInst@@VObjectDir@@@@QAA?AViterator@1@V21@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?erase@?$ObjPtrList@VSfxInst@@VObjectDir@@@@QAA?AViterator@1@V21@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?erase@?$ObjPtrList@VWorldCrowd@@VObjectDir@@@@QAA?AViterator@1@V21@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?erase@?$ObjPtrVec@VCharClip@@VObjectDir@@@@QAA?AViterator@1@V21@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?erase@?$ObjPtrVec@VFlow@@VObjectDir@@@@QAA?AViterator@1@V21@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?erase@?$ObjPtrVec@VFlowLabel@@VObjectDir@@@@QAA?AViterator@1@V21@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?erase@?$ObjPtrVec@VHamCharacter@@VObjectDir@@@@QAA?AViterator@1@V21@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?erase@?$ObjPtrVec@VRhythmDetector@@VObjectDir@@@@QAA?AViterator@1@V21@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?erase@?$ObjPtrVec@VRndDrawable@@VObjectDir@@@@QAA?AViterator@1@V21@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?erase@?$ObjPtrVec@VRndEnviron@@VObjectDir@@@@QAA?AViterator@1@V21@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?erase@?$ObjPtrVec@VRndGroup@@VObjectDir@@@@QAA?AViterator@1@V21@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?erase@?$ObjPtrVec@VRndLight@@VObjectDir@@@@QAA?AViterator@1@V21@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?erase@?$ObjPtrVec@VRndMat@@VObjectDir@@@@QAA?AViterator@1@V21@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?erase@?$ObjPtrVec@VSpotlight@@VObjectDir@@@@QAA?AViterator@1@V21@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?erase@?$ObjPtrVec@VSpotlightDrawer@@VObjectDir@@@@QAA?AViterator@1@V21@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?erase@?$vector@VLine@RndText@@V?$StlNodeAlloc@VLine@RndText@@@stlpmtx_std@@@stlpmtx_std@@QAAPAVLine@RndText@@PAV34@0@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?front@?$ObjPtrList@VObjectDir@@V1@@@QBAPAVObjectDir@@XZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?front@?$ObjPtrList@VRndFontBase@@VObjectDir@@@@QBAPAVRndFontBase@@XZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?front@?$ObjPtrList@VRndMat@@VObjectDir@@@@QBAPAVRndMat@@XZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?front@?$ObjPtrList@VRndTransformable@@VObjectDir@@@@QBAPAVRndTransformable@@XZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?front@?$ObjPtrList@VSeqInst@@VObjectDir@@@@QBAPAVSeqInst@@XZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?front@?$ObjPtrList@VTask@@VObjectDir@@@@QBAPAVTask@@XZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?groove@?1??IsRest@MoveVariant@@QBA_NXZ@4VSymbol@@A=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?high@?FG@??CamShotVOData@@YAXVSymbol@@AAV2@111@Z@4V2@A=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?insert@?$list@VBSPFace@@V?$StlNodeAlloc@VBSPFace@@@stlpmtx_std@@@stlpmtx_std@@QAA?AU?$_List_iterator@VBSPFace@@U?$_Nonconst_traits@VBSPFace@@@stlpmtx_std@@@2@U32@ABVBSPFace@@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?insert_unique@?$_Rb_tree@PAVCharClip@@U?$less@PAVCharClip@@@stlpmtx_std@@U?$pair@QAVCharClip@@M@3@U?$_Select1st@U?$pair@QAVCharClip@@M@stlpmtx_std@@@3@U?$_MapTraitsT@U?$pair@QAVCharClip@@M@stlpmtx_std@@@priv@3@V?$StlNodeAlloc@U?$pair@QAVCharClip@@M@stlpmtx_std@@@3@@stlpmtx_std@@QAA?AU?$pair@U?$_Rb_tree_iterator@U?$pair@QAVCharClip@@M@stlpmtx_std@@U?$_MapTraitsT@U?$pair@QAVCharClip@@M@stlpmtx_std@@@priv@2@@stlpmtx_std@@_N@2@ABU?$pair@QAVCharClip@@M@2@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?intro_playlist@?1??CamShotVOData@@YAXVSymbol@@AAV2@111@Z@4V2@A=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?intro_quick@?1??CamShotVOData@@YAXVSymbol@@AAV2@111@Z@4V2@A=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?intro_skills@?1??CamShotVOData@@YAXVSymbol@@AAV2@111@Z@4V2@A=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?kArkBlockSize@@3HB=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?kStreamEndMs@StandardStream@@2MB=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?lose_camp_char@?1??CamShotVOData@@YAXVSymbol@@AAV2@111@Z@4V2@A=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?low@?FG@??CamShotVOData@@YAXVSymbol@@AAV2@111@Z@4V2@A=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?med@?FG@??CamShotVOData@@YAXVSymbol@@AAV2@111@Z@4V2@A=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?merge@?$ObjPtrVec@VRndDrawable@@VObjectDir@@@@QAAXABV1@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?merged_82610090@@YAPAXPBXPAI@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?pow@@YAMMM@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?push_back@?$vector@V?$Key@VTransform@@@@V?$StlNodeAlloc@V?$Key@VTransform@@@@@stlpmtx_std@@@stlpmtx_std@@QAAXABV?$Key@VTransform@@@@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?push_back@?$vector@VFace@RndMesh@@V?$StlNodeAlloc@VFace@RndMesh@@@stlpmtx_std@@@stlpmtx_std@@QAAXABVFace@RndMesh@@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?resize@?$vector@V?$Key@VTransform@@@@V?$StlNodeAlloc@V?$Key@VTransform@@@@@stlpmtx_std@@@stlpmtx_std@@QAAXIABV?$Key@VTransform@@@@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?rest@?1??IsRest@MoveVariant@@QBA_NXZ@4VSymbol@@A=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?sCheatFinale@MetaPerformer@@0_NA=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?sCurrentSkinned@RndShader@@1_NA=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?sCurrentUseAO@RndShader@@1_NA=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?sDebugHighlight@UILabel@@1_NA=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?sDisableAll@FileMerger@@1_NA=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?sDisableEyeClamping@CharEyes@@1_NA=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?sDisableEyeDart@CharEyes@@1_NA=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?sDisableEyeJitter@CharEyes@@1_NA=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?sDisableInterestObjects@CharEyes@@1_NA=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?sDisableProceduralBlink@CharEyes@@1_NA=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?sHasFlippedTextThisRotation@InlineHelp@@0_NA=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?sMatShadersOK@RndShader@@1_NA=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?sNeedsTextUpdate@InlineHelp@@0_NA=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?sRotated@InlineHelp@@0_NA=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?setJumpSamplesFromMs@StandardStream@@AAAXMM@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?sort@?$ObjPtrList@VCharBone@@VObjectDir@@@@QAAXP6A_NPAVCharBone@@0@Z@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?sort@?$ObjPtrList@VRndDrawable@@VObjectDir@@@@QAAXP6A_NPAVRndDrawable@@0@Z@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?swap@?$ObjPtrVec@VCharClip@@VObjectDir@@@@QAAXHH@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?unique@?$ObjPtrVec@VObject@Hmx@@VObjectDir@@@@QAAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?win_camp_crew@?1??CamShotVOData@@YAXVSymbol@@AAV2@111@Z@4V2@A=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?win_dlg_char@?1??CamShotVOData@@YAXVSymbol@@AAV2@111@Z@4V2@A=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?win_hype_crew@?1??CamShotVOData@@YAXVSymbol@@AAV2@111@Z@4V2@A=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?win_hype_diff_crew@?1??CamShotVOData@@YAXVSymbol@@AAV2@111@Z@4V2@A=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?win_hype_solo@?1??CamShotVOData@@YAXVSymbol@@AAV2@111@Z@4V2@A=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?win_mov_char@?1??CamShotVOData@@YAXVSymbol@@AAV2@111@Z@4V2@A=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:BinkInit=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:D3DTexture_GetLevelDesc=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:D3DTexture_LockRect=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:D3DTexture_UnlockRect=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:D3DXSetDXT3DXT5=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:XGHierarchicalZSize=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:__real_0000000000000000=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:__real_3f50624dd2f1a9fc=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:__real_3fe0000000000000=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:__real_4000000000000000=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:__real_400921fb60000000=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:__real_401921fb60000000=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:__vmx_00000000000000000000000000000000=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:__vmx_bf8000003f800000bf8000003f800000=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:cexp=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:expand=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:expj=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:jumptable_8202C1D0=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:jumptable_82066CC0=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:jumptable_82066CD0=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:jumptable_82070798=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:jumptable_820707C0=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:jumptable_820707E8=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:jumptable_82070810=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:jumptable_8209C900=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:jumptable_8209C910=__link_glue_noop")

// ============================================================================
// Template instantiation stubs
// ALTERNATENAME doesn't work for ??$ template symbols. These need actual
// compiled code to satisfy the linker.
// ============================================================================

// -- Additional includes for template instantiations --
#include "world/CameraShot.h"
#include "char/CharClip.h"
#include "char/CharInterest.h"
#include "char/CharLookAt.h"
#include "flow/Flow.h"
#include "hamobj/HamCamShot.h"
#include "hamobj/HamListRibbon.h"
#include "hamobj/HamScrollSpeedIndicator.h"
#include "hamobj/RhythmDetector.h"
#include "rndobj/Env.h"
#include "rndobj/FontBase.h"
#include "rndobj/Font.h"
#include "rndobj/LitAnim.h"
#include "rndobj/Mat.h"
#include "rndobj/MatAnim.h"
#include "rndobj/MeshAnim.h"
#include "rndobj/PartAnim.h"
#include "rndobj/PartLauncher.h"
#include "rndobj/TexBlendController.h"
#include "rndobj/Group.h"
#include "world/SpotlightDrawer.h"
#include "world/Instance.h"
#include "ui/UIListDir.h"
#include "char/CharCollide.h"
#include "char/CharHair.h"
#include "char/CharClipSet.h"

// -- BinStream operator<< for ObjPtrList<T> --

#define BINSTREAM_OP_OBJPTRLIST(T) \
template <> \
BinStream &operator<<(BinStream &bs, const ObjPtrList<T, ObjectDir> &list) { \
    bs << list.size(); \
    for (ObjPtrList<T>::iterator it = list.begin(); it != list.end(); ++it) { \
        Hmx::Object *obj = *it; \
        const char *name = obj ? obj->Name() : ""; \
        bs << name; \
    } \
    return bs; \
}

BINSTREAM_OP_OBJPTRLIST(CamShot)
BINSTREAM_OP_OBJPTRLIST(CharBone)
BINSTREAM_OP_OBJPTRLIST(EventTrigger)
BINSTREAM_OP_OBJPTRLIST(HamCamShot)
BINSTREAM_OP_OBJPTRLIST(RndFontBase)
BINSTREAM_OP_OBJPTRLIST(RndMat)
BINSTREAM_OP_OBJPTRLIST(RndPartLauncher)
BINSTREAM_OP_OBJPTRLIST(RndTexBlendController)
BINSTREAM_OP_OBJPTRLIST(Sequence)

#undef BINSTREAM_OP_OBJPTRLIST

// -- BinStream operator<< for ObjPtrVec<T> --

#define BINSTREAM_OP_OBJPTRVEC(T) \
template <> \
BinStream &operator<<(BinStream &bs, const ObjPtrVec<T, ObjectDir> &vec) { \
    bs << (int)vec.size(); \
    for (int i = 0; i < (int)vec.size(); i++) { \
        const Hmx::Object *obj = vec[i]; \
        const char *name = obj ? obj->Name() : ""; \
        bs << name; \
    } \
    return bs; \
}

BINSTREAM_OP_OBJPTRVEC(CharClip)
BINSTREAM_OP_OBJPTRVEC(Flow)
BINSTREAM_OP_OBJPTRVEC(Hmx::Object)
BINSTREAM_OP_OBJPTRVEC(RhythmDetector)
BINSTREAM_OP_OBJPTRVEC(RndDrawable)
BINSTREAM_OP_OBJPTRVEC(RndEnviron)
BINSTREAM_OP_OBJPTRVEC(RndLight)
BINSTREAM_OP_OBJPTRVEC(RndMat)
BINSTREAM_OP_OBJPTRVEC(Spotlight)
BINSTREAM_OP_OBJPTRVEC(SpotlightDrawer)

#undef BINSTREAM_OP_OBJPTRVEC

// -- BinStream operator<< for ObjOwnerPtr<T> --

#define BINSTREAM_OP_OBJOWNERPTR(T) \
template <> \
BinStream &operator<<(BinStream &bs, const ObjOwnerPtr<T> &ptr) { \
    Hmx::Object *obj = ptr; \
    const char *name = obj ? obj->Name() : ""; \
    bs << name; \
    return bs; \
}

BINSTREAM_OP_OBJOWNERPTR(CharInterest)
BINSTREAM_OP_OBJOWNERPTR(CharLookAt)
BINSTREAM_OP_OBJOWNERPTR(EventTrigger)
BINSTREAM_OP_OBJOWNERPTR(ObjectDir)
BINSTREAM_OP_OBJOWNERPTR(RndAnimatable)
BINSTREAM_OP_OBJOWNERPTR(RndDrawable)
BINSTREAM_OP_OBJOWNERPTR(RndEnviron)
BINSTREAM_OP_OBJOWNERPTR(RndFont)
BINSTREAM_OP_OBJOWNERPTR(RndLightAnim)
BINSTREAM_OP_OBJOWNERPTR(RndMatAnim)
BINSTREAM_OP_OBJOWNERPTR(RndMeshAnim)
BINSTREAM_OP_OBJOWNERPTR(RndParticleSysAnim)

#undef BINSTREAM_OP_OBJOWNERPTR

// -- BinStream operator<< for ObjDirPtr<T> --

#define BINSTREAM_OP_OBJDIRPTR(T) \
template <> \
BinStream &operator<<(BinStream &bs, const ObjDirPtr<T> &ptr) { \
    T *dir = ptr; \
    const char *name = dir ? dir->Name() : ""; \
    bs << name; \
    return bs; \
}

BINSTREAM_OP_OBJDIRPTR(HamListRibbon)
BINSTREAM_OP_OBJDIRPTR(HamScrollSpeedIndicator)
BINSTREAM_OP_OBJDIRPTR(ObjectDir)
BINSTREAM_OP_OBJDIRPTR(UIListDir)

#undef BINSTREAM_OP_OBJDIRPTR

// -- PropSync<T> for ObjPtrVec<T> --
// These are stub implementations that just return false.

#define PROPSYNC_OBJPTRVEC(T) \
template <> \
bool PropSync(ObjPtrVec<T, ObjectDir> &, DataNode &, DataArray *, int, PropOp) { \
    return false; \
}

PROPSYNC_OBJPTRVEC(CharClip)
PROPSYNC_OBJPTRVEC(Flow)
PROPSYNC_OBJPTRVEC(Hmx::Object)
PROPSYNC_OBJPTRVEC(RhythmDetector)
PROPSYNC_OBJPTRVEC(RndDrawable)
PROPSYNC_OBJPTRVEC(RndMat)
PROPSYNC_OBJPTRVEC(RndTransformable)

#undef PROPSYNC_OBJPTRVEC

// -- PropSync<T> for ObjDirPtr<T> --

template <>
bool PropSync(ObjDirPtr<WorldInstance> &, DataNode &, DataArray *, int, PropOp) {
    return false;
}


// -- GatherObjectsFromGroup<RndMesh> --

template <class T>
unsigned int GatherObjectsFromGroup(RndGroup *, std::vector<T *> &);

template <>
unsigned int GatherObjectsFromGroup<RndMesh>(RndGroup *, std::vector<RndMesh *> &) {
    return 0;
}