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

// -- String --
#include "utl/Str.h"

String::String(const char *s) : FixedString(), TextStream() {
    // minimal stub - real implementation allocates and copies
    mStr = (char *)"";
    if (s) *this += s;
}

String::String(Symbol s) : FixedString(), TextStream() {
    mStr = (char *)"";
    if (s.Str()) *this += s.Str();
}

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

// -- DancerSkeleton --
#include "hamobj/DancerSkeleton.h"

void DancerSkeleton::CameraToPlayerXfm(SkeletonCoordSys, Transform &) const {}

// -- VenueProvider --
#include "meta_ham/VenueProvider.h"

int VenueProvider::NumData() const { return mVenues.size(); }

// -- Accomplishment --
#include "meta_ham/Accomplishment.h"
#include "hamobj/Difficulty.h"

Difficulty Accomplishment::GetRequiredDifficulty() const { return mDifficulty; }
Symbol Accomplishment::GetName() const { return mName; }

// -- FxSendBitCrush --
#include "synth/FxSendBitCrush.h"

DataNode FxSendBitCrush::Handle(DataArray *msg, bool warn) {
    return FxSend::Handle(msg, warn);
}

// -- CharSignalApplier --
#include "char/CharSignalApplier.h"

DataNode CharSignalApplier::Handle(DataArray *msg, bool warn) {
    return Hmx::Object::Handle(msg, warn);
}

// -- WorldCrowd3DCharHandle --
#include "world/Crowd3DCharHandle.h"

bool WorldCrowd3DCharHandle::SyncProperty(DataNode &, DataArray *, int, PropOp) {
    return false;
}

// -- WavMgr --
#include "synth/WavMgr.h"

bool WavMgr::SyncProperty(DataNode &, DataArray *, int, PropOp) { return false; }

// -- Achievements --
#include "meta/Achievements.h"

void Achievements::PlatformInit() {}

// -- UIManager --
#include "ui/UI.h"

int UIManager::PushDepth() const { return mPushedScreens.size(); }

// -- Synth --
#include "synth/Synth.h"

int Synth::GetNumMics() const { return mNumMics; }

// -- SongMetadata --
#include "meta/SongMetadata.h"

int SongMetadata::ID() const { return mID; }
Symbol SongMetadata::GameOrigin() const { return mGameOrigin; }

// -- HamProfile --
#include "meta_ham/HamProfile.h"

SongStatusMgr *HamProfile::GetSongStatusMgr() const { return mSongStatusMgr; }

// -- HamSongMetadata --
#include "meta_ham/HamSongMetadata.h"

const char *HamSongMetadata::Title() const { return mName.c_str(); }

// -- FixedSizeSaveableStream --
#include "meta/FixedSizeSaveableStream.h"

std::map<Symbol, int> &FixedSizeSaveableStream::GetSymbolToIDMap() {
    return m_mapSymbolToID;
}

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

// ============================================================================
// FixedString default constructor stub
// ============================================================================

static char sLinkGlueEmpty[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };

FixedString::FixedString() : mStr((char *)(sLinkGlueEmpty + 4)) {
    *(int *)(mStr - 4) = 0;
    mStr[0] = '\0';
}

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

