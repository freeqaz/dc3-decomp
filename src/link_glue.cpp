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
#include "synth/Faders.h"
#include "rndobj/Lit.h"
#include "world/Spotlight.h"
#include "char/Waypoint.h"
#include "rndobj/Wind.h"
#include "char/CharPollable.h"
#include "char/CharWeightSetter.h"
#include "flow/FlowNode.h"
#include "rndobj/CamAnim.h"

// ============================================================================
// ICF-merged function definitions
// ============================================================================

void operator delete(void *v) { MemFree(v, "unknown", 0, "unknown"); }
void operator delete[](void *v) { MemFree(v, "unknown", 0, "unknown"); }

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

// -- PanelDir --
#include "ui/PanelDir.h"

// -- UIComponent --
#include "ui/UIComponent.h"

// -- UIList --
#include "ui/UIList.h"

// -- BufStream --
// Still needed: virtual method not exported from decomp .obj, referenced by other split .objs
#include "utl/BufStream.h"

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

// (DancerSkeleton removed — hamobj/DancerSkeleton is Matching)

// -- VenueProvider --
// Still needed: virtual NumData referenced by CharacterProvider, HamUI, LocalePanel split .objs
#include "meta_ham/VenueProvider.h"


// (Accomplishment removed — meta_ham/Accomplishment is Matching)

// (FxSendBitCrush removed — synth/FxSendBitCrush is Matching)

// -- WavMgr --
// Still needed: SyncProperty referenced from WavMgr.obj split
#include "synth/WavMgr.h"

// -- Achievements --
// Still needed: PlatformInit referenced from Achievements.obj itself
#include "meta/Achievements.h"

// (UIManager removed — ui/UI is Matching)

// -- SongMetadata --
// Still needed: inline in header, not exported from decomp .obj, referenced by SongMgr/SongRecord
#include "meta/SongMetadata.h"

// (HamProfile removed — meta_ham/HamProfile is Matching)

// (HamSongMetadata::Title removed — meta_ham/HamSongMetadata is Matching)

// (FixedSizeSaveableStream removed — meta/FixedSizeSaveableStream is Matching)

// (soundtouch::FIRFilter::getLength removed — synth_xbox/soundtouch FIRFilter.cpp is Matching)

// (Stream::kStreamEndMs removed — synth/Stream is Matching)

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
// ObjPtrList template instantiations for promoted types
// Generated by gen_templates.py
// ============================================================================

#include "char/CharBone.h"
#include "char/CharCollide.h"
#include "char/CharInterest.h"
#include "char/CharPollable.h"
#include "char/CharWeightable.h"
#include "char/CharWeightSetter.h"
#include "char/Waypoint.h"
#include "hamobj/HamCamShot.h"
#include "obj/Dir.h"
#include "rndobj/EventTrigger.h"
#include "rndobj/FontBase.h"
#include "rndobj/Lit.h"
#include "rndobj/Mat.h"
#include "rndobj/TexBlendController.h"
#include "synth/Faders.h"
#include "synth/MidiInstrument.h"
#include "synth/Sequence.h"
#include "synth/Sfx.h"
#include "synth/ThreeDSound.h"
#include "world/CameraShot.h"
#include "world/Crowd.h"

// -- ObjPtrList<CharBone, ObjectDir> --

template <>
void ObjPtrList<CharBone>::Link(iterator it, Node *node) {
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
Hmx::Object *ObjPtrList<CharBone>::RefOwner() const {
    return mOwner ? mOwner->RefOwner() : 0;
}

template <>
Hmx::Object *ObjPtrList<CharBone>::Node::RefOwner() const {
    ObjPtrList<CharBone> *list = static_cast<ObjPtrList<CharBone> *>(mOwner);
    return list->Owner();
}

template <>
bool ObjPtrList<CharBone>::Replace(ObjRef *ref, Hmx::Object *obj) {
    for (iterator it = begin(); it != end(); ++it) {
        if (it.mNode == ref) {
            ReplaceNode(it.mNode, obj);
            return true;
        }
    }
    return false;
}

template <>
ObjPtrList<CharBone>::Node *ObjPtrList<CharBone>::Unlink(Node *node) {
    Node *next = node->next;
    Node *prev = node->prev;
    if (prev) prev->next = next;
    if (next) next->prev = prev;
    if (mNodes == node) mNodes = next;
    mSize--;
    return next;
}

template <>
ObjPtrList<CharBone>::iterator ObjPtrList<CharBone>::erase(iterator it) {
    Node *node = it.mNode;
    Node *next = Unlink(node);
    delete node;
    return iterator(next);
}

template <>
void ObjPtrList<CharBone>::sort(SortFunc *) {}

// -- ObjPtrList<CharCollide, ObjectDir> --

template <>
void ObjPtrList<CharCollide>::Link(iterator it, Node *node) {
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
Hmx::Object *ObjPtrList<CharCollide>::RefOwner() const {
    return mOwner ? mOwner->RefOwner() : 0;
}

template <>
Hmx::Object *ObjPtrList<CharCollide>::Node::RefOwner() const {
    ObjPtrList<CharCollide> *list = static_cast<ObjPtrList<CharCollide> *>(mOwner);
    return list->Owner();
}

template <>
bool ObjPtrList<CharCollide>::Replace(ObjRef *ref, Hmx::Object *obj) {
    for (iterator it = begin(); it != end(); ++it) {
        if (it.mNode == ref) {
            ReplaceNode(it.mNode, obj);
            return true;
        }
    }
    return false;
}

template <>
ObjPtrList<CharCollide>::Node *ObjPtrList<CharCollide>::Unlink(Node *node) {
    Node *next = node->next;
    Node *prev = node->prev;
    if (prev) prev->next = next;
    if (next) next->prev = prev;
    if (mNodes == node) mNodes = next;
    mSize--;
    return next;
}

template <>
ObjPtrList<CharCollide>::iterator ObjPtrList<CharCollide>::erase(iterator it) {
    Node *node = it.mNode;
    Node *next = Unlink(node);
    delete node;
    return iterator(next);
}

// -- ObjPtrList<CharInterest, ObjectDir> --

template <>
void ObjPtrList<CharInterest>::Link(iterator it, Node *node) {
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
Hmx::Object *ObjPtrList<CharInterest>::RefOwner() const {
    return mOwner ? mOwner->RefOwner() : 0;
}

template <>
bool ObjPtrList<CharInterest>::Replace(ObjRef *ref, Hmx::Object *obj) {
    for (iterator it = begin(); it != end(); ++it) {
        if (it.mNode == ref) {
            ReplaceNode(it.mNode, obj);
            return true;
        }
    }
    return false;
}

template <>
ObjPtrList<CharInterest>::Node *ObjPtrList<CharInterest>::Unlink(Node *node) {
    Node *next = node->next;
    Node *prev = node->prev;
    if (prev) prev->next = next;
    if (next) next->prev = prev;
    if (mNodes == node) mNodes = next;
    mSize--;
    return next;
}

template <>
ObjPtrList<CharInterest>::iterator ObjPtrList<CharInterest>::erase(iterator it) {
    Node *node = it.mNode;
    Node *next = Unlink(node);
    delete node;
    return iterator(next);
}

// -- ObjPtrList<CharPollable, ObjectDir> --

template <>
void ObjPtrList<CharPollable>::Link(iterator it, Node *node) {
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
Hmx::Object *ObjPtrList<CharPollable>::RefOwner() const {
    return mOwner ? mOwner->RefOwner() : 0;
}

template <>
bool ObjPtrList<CharPollable>::Replace(ObjRef *ref, Hmx::Object *obj) {
    for (iterator it = begin(); it != end(); ++it) {
        if (it.mNode == ref) {
            ReplaceNode(it.mNode, obj);
            return true;
        }
    }
    return false;
}

template <>
ObjPtrList<CharPollable>::Node *ObjPtrList<CharPollable>::Unlink(Node *node) {
    Node *next = node->next;
    Node *prev = node->prev;
    if (prev) prev->next = next;
    if (next) next->prev = prev;
    if (mNodes == node) mNodes = next;
    mSize--;
    return next;
}

template <>
ObjPtrList<CharPollable>::iterator ObjPtrList<CharPollable>::erase(iterator it) {
    Node *node = it.mNode;
    Node *next = Unlink(node);
    delete node;
    return iterator(next);
}

// (ObjPtrList<CharPollable>::operator<< already defined above)
// -- ObjPtrList<CharWeightable, ObjectDir> --

template <>
void ObjPtrList<CharWeightable>::Link(iterator it, Node *node) {
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
Hmx::Object *ObjPtrList<CharWeightable>::RefOwner() const {
    return mOwner ? mOwner->RefOwner() : 0;
}

template <>
bool ObjPtrList<CharWeightable>::Replace(ObjRef *ref, Hmx::Object *obj) {
    for (iterator it = begin(); it != end(); ++it) {
        if (it.mNode == ref) {
            ReplaceNode(it.mNode, obj);
            return true;
        }
    }
    return false;
}

template <>
ObjPtrList<CharWeightable>::Node *ObjPtrList<CharWeightable>::Unlink(Node *node) {
    Node *next = node->next;
    Node *prev = node->prev;
    if (prev) prev->next = next;
    if (next) next->prev = prev;
    if (mNodes == node) mNodes = next;
    mSize--;
    return next;
}

template <>
ObjPtrList<CharWeightable>::iterator ObjPtrList<CharWeightable>::erase(iterator it) {
    Node *node = it.mNode;
    Node *next = Unlink(node);
    delete node;
    return iterator(next);
}

// -- ObjPtrList<CharWeightSetter, ObjectDir> --

template <>
void ObjPtrList<CharWeightSetter>::Link(iterator it, Node *node) {
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
Hmx::Object *ObjPtrList<CharWeightSetter>::RefOwner() const {
    return mOwner ? mOwner->RefOwner() : 0;
}

template <>
bool ObjPtrList<CharWeightSetter>::Replace(ObjRef *ref, Hmx::Object *obj) {
    for (iterator it = begin(); it != end(); ++it) {
        if (it.mNode == ref) {
            ReplaceNode(it.mNode, obj);
            return true;
        }
    }
    return false;
}

template <>
ObjPtrList<CharWeightSetter>::Node *ObjPtrList<CharWeightSetter>::Unlink(Node *node) {
    Node *next = node->next;
    Node *prev = node->prev;
    if (prev) prev->next = next;
    if (next) next->prev = prev;
    if (mNodes == node) mNodes = next;
    mSize--;
    return next;
}

template <>
ObjPtrList<CharWeightSetter>::iterator ObjPtrList<CharWeightSetter>::erase(iterator it) {
    Node *node = it.mNode;
    Node *next = Unlink(node);
    delete node;
    return iterator(next);
}

// (ObjPtrList<CharWeightSetter>::operator<< already defined above)
// -- ObjPtrList<Fader, ObjectDir> --

template <>
void ObjPtrList<Fader>::Link(iterator it, Node *node) {
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
Hmx::Object *ObjPtrList<Fader>::RefOwner() const {
    return mOwner ? mOwner->RefOwner() : 0;
}

template <>
bool ObjPtrList<Fader>::Replace(ObjRef *ref, Hmx::Object *obj) {
    for (iterator it = begin(); it != end(); ++it) {
        if (it.mNode == ref) {
            ReplaceNode(it.mNode, obj);
            return true;
        }
    }
    return false;
}

template <>
ObjPtrList<Fader>::Node *ObjPtrList<Fader>::Unlink(Node *node) {
    Node *next = node->next;
    Node *prev = node->prev;
    if (prev) prev->next = next;
    if (next) next->prev = prev;
    if (mNodes == node) mNodes = next;
    mSize--;
    return next;
}

template <>
ObjPtrList<Fader>::iterator ObjPtrList<Fader>::erase(iterator it) {
    Node *node = it.mNode;
    Node *next = Unlink(node);
    delete node;
    return iterator(next);
}

template <>
Fader *ObjPtrList<Fader>::front() const {
    return mNodes ? mNodes->Obj() : 0;
}

// (ObjPtrList<Fader>::operator<< already defined above)
// -- ObjPtrList<HamCamShot, ObjectDir> --

template <>
void ObjPtrList<HamCamShot>::Link(iterator it, Node *node) {
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
Hmx::Object *ObjPtrList<HamCamShot>::RefOwner() const {
    return mOwner ? mOwner->RefOwner() : 0;
}

template <>
Hmx::Object *ObjPtrList<HamCamShot>::Node::RefOwner() const {
    ObjPtrList<HamCamShot> *list = static_cast<ObjPtrList<HamCamShot> *>(mOwner);
    return list->Owner();
}

template <>
bool ObjPtrList<HamCamShot>::Replace(ObjRef *ref, Hmx::Object *obj) {
    for (iterator it = begin(); it != end(); ++it) {
        if (it.mNode == ref) {
            ReplaceNode(it.mNode, obj);
            return true;
        }
    }
    return false;
}

template <>
ObjPtrList<HamCamShot>::Node *ObjPtrList<HamCamShot>::Unlink(Node *node) {
    Node *next = node->next;
    Node *prev = node->prev;
    if (prev) prev->next = next;
    if (next) next->prev = prev;
    if (mNodes == node) mNodes = next;
    mSize--;
    return next;
}

template <>
ObjPtrList<HamCamShot>::iterator ObjPtrList<HamCamShot>::erase(iterator it) {
    Node *node = it.mNode;
    Node *next = Unlink(node);
    delete node;
    return iterator(next);
}

// (ObjPtrList<HamCamShot>::operator<< already defined above)
// -- ObjPtrList<CamShot, ObjectDir> --

template <>
void ObjPtrList<CamShot>::Link(iterator it, Node *node) {
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
Hmx::Object *ObjPtrList<CamShot>::RefOwner() const {
    return mOwner ? mOwner->RefOwner() : 0;
}

template <>
Hmx::Object *ObjPtrList<CamShot>::Node::RefOwner() const {
    ObjPtrList<CamShot> *list = static_cast<ObjPtrList<CamShot> *>(mOwner);
    return list->Owner();
}

template <>
ObjPtrList<CamShot>::Node *ObjPtrList<CamShot>::Unlink(Node *node) {
    Node *next = node->next;
    Node *prev = node->prev;
    if (prev) prev->next = next;
    if (next) next->prev = prev;
    if (mNodes == node) mNodes = next;
    mSize--;
    return next;
}

template <>
ObjPtrList<CamShot>::iterator ObjPtrList<CamShot>::erase(iterator it) {
    Node *node = it.mNode;
    Node *next = Unlink(node);
    delete node;
    return iterator(next);
}

// -- ObjPtrList<NoteVoiceInst, ObjectDir> --

template <>
void ObjPtrList<NoteVoiceInst>::Link(iterator it, Node *node) {
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
Hmx::Object *ObjPtrList<NoteVoiceInst>::RefOwner() const {
    return mOwner ? mOwner->RefOwner() : 0;
}

template <>
bool ObjPtrList<NoteVoiceInst>::Replace(ObjRef *ref, Hmx::Object *obj) {
    for (iterator it = begin(); it != end(); ++it) {
        if (it.mNode == ref) {
            ReplaceNode(it.mNode, obj);
            return true;
        }
    }
    return false;
}

template <>
ObjPtrList<NoteVoiceInst>::Node *ObjPtrList<NoteVoiceInst>::Unlink(Node *node) {
    Node *next = node->next;
    Node *prev = node->prev;
    if (prev) prev->next = next;
    if (next) next->prev = prev;
    if (mNodes == node) mNodes = next;
    mSize--;
    return next;
}

template <>
ObjPtrList<NoteVoiceInst>::iterator ObjPtrList<NoteVoiceInst>::erase(iterator it) {
    Node *node = it.mNode;
    Node *next = Unlink(node);
    delete node;
    return iterator(next);
}

template <>
NoteVoiceInst *ObjPtrList<NoteVoiceInst>::front() const {
    return mNodes ? mNodes->Obj() : 0;
}

// -- ObjPtrList<ObjectDir, ObjectDir> --

template <>
void ObjPtrList<ObjectDir>::Link(iterator it, Node *node) {
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
Hmx::Object *ObjPtrList<ObjectDir>::RefOwner() const {
    return mOwner ? mOwner->RefOwner() : 0;
}

template <>
bool ObjPtrList<ObjectDir>::Replace(ObjRef *ref, Hmx::Object *obj) {
    for (iterator it = begin(); it != end(); ++it) {
        if (it.mNode == ref) {
            ReplaceNode(it.mNode, obj);
            return true;
        }
    }
    return false;
}

template <>
ObjPtrList<ObjectDir>::Node *ObjPtrList<ObjectDir>::Unlink(Node *node) {
    Node *next = node->next;
    Node *prev = node->prev;
    if (prev) prev->next = next;
    if (next) next->prev = prev;
    if (mNodes == node) mNodes = next;
    mSize--;
    return next;
}

template <>
ObjPtrList<ObjectDir>::iterator ObjPtrList<ObjectDir>::erase(iterator it) {
    Node *node = it.mNode;
    Node *next = Unlink(node);
    delete node;
    return iterator(next);
}

template <>
ObjectDir *ObjPtrList<ObjectDir>::front() const {
    return mNodes ? mNodes->Obj() : 0;
}

// -- ObjPtrList<RndFontBase, ObjectDir> --

template <>
void ObjPtrList<RndFontBase>::Link(iterator it, Node *node) {
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
Hmx::Object *ObjPtrList<RndFontBase>::RefOwner() const {
    return mOwner ? mOwner->RefOwner() : 0;
}

template <>
Hmx::Object *ObjPtrList<RndFontBase>::Node::RefOwner() const {
    ObjPtrList<RndFontBase> *list = static_cast<ObjPtrList<RndFontBase> *>(mOwner);
    return list->Owner();
}

template <>
bool ObjPtrList<RndFontBase>::Replace(ObjRef *ref, Hmx::Object *obj) {
    for (iterator it = begin(); it != end(); ++it) {
        if (it.mNode == ref) {
            ReplaceNode(it.mNode, obj);
            return true;
        }
    }
    return false;
}

template <>
ObjPtrList<RndFontBase>::Node *ObjPtrList<RndFontBase>::Unlink(Node *node) {
    Node *next = node->next;
    Node *prev = node->prev;
    if (prev) prev->next = next;
    if (next) next->prev = prev;
    if (mNodes == node) mNodes = next;
    mSize--;
    return next;
}

template <>
ObjPtrList<RndFontBase>::iterator ObjPtrList<RndFontBase>::erase(iterator it) {
    Node *node = it.mNode;
    Node *next = Unlink(node);
    delete node;
    return iterator(next);
}

template <>
RndFontBase *ObjPtrList<RndFontBase>::front() const {
    return mNodes ? mNodes->Obj() : 0;
}

// -- ObjPtrList<RndLight, ObjectDir> --

template <>
void ObjPtrList<RndLight>::Link(iterator it, Node *node) {
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
Hmx::Object *ObjPtrList<RndLight>::RefOwner() const {
    return mOwner ? mOwner->RefOwner() : 0;
}

template <>
bool ObjPtrList<RndLight>::Replace(ObjRef *ref, Hmx::Object *obj) {
    for (iterator it = begin(); it != end(); ++it) {
        if (it.mNode == ref) {
            ReplaceNode(it.mNode, obj);
            return true;
        }
    }
    return false;
}

template <>
ObjPtrList<RndLight>::Node *ObjPtrList<RndLight>::Unlink(Node *node) {
    Node *next = node->next;
    Node *prev = node->prev;
    if (prev) prev->next = next;
    if (next) next->prev = prev;
    if (mNodes == node) mNodes = next;
    mSize--;
    return next;
}

template <>
ObjPtrList<RndLight>::iterator ObjPtrList<RndLight>::erase(iterator it) {
    Node *node = it.mNode;
    Node *next = Unlink(node);
    delete node;
    return iterator(next);
}

// (ObjPtrList<RndLight>::operator<< already defined above)
// -- ObjPtrList<RndMat, ObjectDir> --

template <>
void ObjPtrList<RndMat>::Link(iterator it, Node *node) {
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
Hmx::Object *ObjPtrList<RndMat>::RefOwner() const {
    return mOwner ? mOwner->RefOwner() : 0;
}

template <>
Hmx::Object *ObjPtrList<RndMat>::Node::RefOwner() const {
    ObjPtrList<RndMat> *list = static_cast<ObjPtrList<RndMat> *>(mOwner);
    return list->Owner();
}

template <>
bool ObjPtrList<RndMat>::Replace(ObjRef *ref, Hmx::Object *obj) {
    for (iterator it = begin(); it != end(); ++it) {
        if (it.mNode == ref) {
            ReplaceNode(it.mNode, obj);
            return true;
        }
    }
    return false;
}

template <>
ObjPtrList<RndMat>::Node *ObjPtrList<RndMat>::Unlink(Node *node) {
    Node *next = node->next;
    Node *prev = node->prev;
    if (prev) prev->next = next;
    if (next) next->prev = prev;
    if (mNodes == node) mNodes = next;
    mSize--;
    return next;
}

template <>
ObjPtrList<RndMat>::iterator ObjPtrList<RndMat>::erase(iterator it) {
    Node *node = it.mNode;
    Node *next = Unlink(node);
    delete node;
    return iterator(next);
}

template <>
RndMat *ObjPtrList<RndMat>::front() const {
    return mNodes ? mNodes->Obj() : 0;
}

// -- ObjPtrList<RndTexBlendController, ObjectDir> --

template <>
void ObjPtrList<RndTexBlendController>::Link(iterator it, Node *node) {
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
Hmx::Object *ObjPtrList<RndTexBlendController>::RefOwner() const {
    return mOwner ? mOwner->RefOwner() : 0;
}

template <>
Hmx::Object *ObjPtrList<RndTexBlendController>::Node::RefOwner() const {
    ObjPtrList<RndTexBlendController> *list = static_cast<ObjPtrList<RndTexBlendController> *>(mOwner);
    return list->Owner();
}

template <>
bool ObjPtrList<RndTexBlendController>::Replace(ObjRef *ref, Hmx::Object *obj) {
    for (iterator it = begin(); it != end(); ++it) {
        if (it.mNode == ref) {
            ReplaceNode(it.mNode, obj);
            return true;
        }
    }
    return false;
}

template <>
ObjPtrList<RndTexBlendController>::Node *ObjPtrList<RndTexBlendController>::Unlink(Node *node) {
    Node *next = node->next;
    Node *prev = node->prev;
    if (prev) prev->next = next;
    if (next) next->prev = prev;
    if (mNodes == node) mNodes = next;
    mSize--;
    return next;
}

template <>
ObjPtrList<RndTexBlendController>::iterator ObjPtrList<RndTexBlendController>::erase(iterator it) {
    Node *node = it.mNode;
    Node *next = Unlink(node);
    delete node;
    return iterator(next);
}

// -- ObjPtrList<SeqInst, ObjectDir> --

template <>
void ObjPtrList<SeqInst>::Link(iterator it, Node *node) {
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
Hmx::Object *ObjPtrList<SeqInst>::RefOwner() const {
    return mOwner ? mOwner->RefOwner() : 0;
}

template <>
bool ObjPtrList<SeqInst>::Replace(ObjRef *ref, Hmx::Object *obj) {
    for (iterator it = begin(); it != end(); ++it) {
        if (it.mNode == ref) {
            ReplaceNode(it.mNode, obj);
            return true;
        }
    }
    return false;
}

template <>
ObjPtrList<SeqInst>::Node *ObjPtrList<SeqInst>::Unlink(Node *node) {
    Node *next = node->next;
    Node *prev = node->prev;
    if (prev) prev->next = next;
    if (next) next->prev = prev;
    if (mNodes == node) mNodes = next;
    mSize--;
    return next;
}

template <>
ObjPtrList<SeqInst>::iterator ObjPtrList<SeqInst>::erase(iterator it) {
    Node *node = it.mNode;
    Node *next = Unlink(node);
    delete node;
    return iterator(next);
}

template <>
SeqInst *ObjPtrList<SeqInst>::front() const {
    return mNodes ? mNodes->Obj() : 0;
}

// -- ObjPtrList<SfxInst, ObjectDir> --

template <>
void ObjPtrList<SfxInst>::Link(iterator it, Node *node) {
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
Hmx::Object *ObjPtrList<SfxInst>::RefOwner() const {
    return mOwner ? mOwner->RefOwner() : 0;
}

template <>
bool ObjPtrList<SfxInst>::Replace(ObjRef *ref, Hmx::Object *obj) {
    for (iterator it = begin(); it != end(); ++it) {
        if (it.mNode == ref) {
            ReplaceNode(it.mNode, obj);
            return true;
        }
    }
    return false;
}

template <>
ObjPtrList<SfxInst>::Node *ObjPtrList<SfxInst>::Unlink(Node *node) {
    Node *next = node->next;
    Node *prev = node->prev;
    if (prev) prev->next = next;
    if (next) next->prev = prev;
    if (mNodes == node) mNodes = next;
    mSize--;
    return next;
}

template <>
ObjPtrList<SfxInst>::iterator ObjPtrList<SfxInst>::erase(iterator it) {
    Node *node = it.mNode;
    Node *next = Unlink(node);
    delete node;
    return iterator(next);
}

// -- ObjPtrList<ThreeDSound, ObjectDir> --

template <>
void ObjPtrList<ThreeDSound>::Link(iterator it, Node *node) {
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
Hmx::Object *ObjPtrList<ThreeDSound>::RefOwner() const {
    return mOwner ? mOwner->RefOwner() : 0;
}

template <>
bool ObjPtrList<ThreeDSound>::Replace(ObjRef *ref, Hmx::Object *obj) {
    for (iterator it = begin(); it != end(); ++it) {
        if (it.mNode == ref) {
            ReplaceNode(it.mNode, obj);
            return true;
        }
    }
    return false;
}

template <>
ObjPtrList<ThreeDSound>::Node *ObjPtrList<ThreeDSound>::Unlink(Node *node) {
    Node *next = node->next;
    Node *prev = node->prev;
    if (prev) prev->next = next;
    if (next) next->prev = prev;
    if (mNodes == node) mNodes = next;
    mSize--;
    return next;
}

template <>
ObjPtrList<ThreeDSound>::iterator ObjPtrList<ThreeDSound>::erase(iterator it) {
    Node *node = it.mNode;
    Node *next = Unlink(node);
    delete node;
    return iterator(next);
}

// -- ObjPtrList<Waypoint, ObjectDir> --

template <>
void ObjPtrList<Waypoint>::Link(iterator it, Node *node) {
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
Hmx::Object *ObjPtrList<Waypoint>::RefOwner() const {
    return mOwner ? mOwner->RefOwner() : 0;
}

template <>
bool ObjPtrList<Waypoint>::Replace(ObjRef *ref, Hmx::Object *obj) {
    for (iterator it = begin(); it != end(); ++it) {
        if (it.mNode == ref) {
            ReplaceNode(it.mNode, obj);
            return true;
        }
    }
    return false;
}

template <>
ObjPtrList<Waypoint>::Node *ObjPtrList<Waypoint>::Unlink(Node *node) {
    Node *next = node->next;
    Node *prev = node->prev;
    if (prev) prev->next = next;
    if (next) next->prev = prev;
    if (mNodes == node) mNodes = next;
    mSize--;
    return next;
}

template <>
ObjPtrList<Waypoint>::iterator ObjPtrList<Waypoint>::erase(iterator it) {
    Node *node = it.mNode;
    Node *next = Unlink(node);
    delete node;
    return iterator(next);
}

// (ObjPtrList<Waypoint>::operator<< already defined above)
// -- ObjPtrList<WorldCrowd, ObjectDir> --

template <>
void ObjPtrList<WorldCrowd>::Link(iterator it, Node *node) {
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
Hmx::Object *ObjPtrList<WorldCrowd>::RefOwner() const {
    return mOwner ? mOwner->RefOwner() : 0;
}

template <>
bool ObjPtrList<WorldCrowd>::Replace(ObjRef *ref, Hmx::Object *obj) {
    for (iterator it = begin(); it != end(); ++it) {
        if (it.mNode == ref) {
            ReplaceNode(it.mNode, obj);
            return true;
        }
    }
    return false;
}

template <>
ObjPtrList<WorldCrowd>::Node *ObjPtrList<WorldCrowd>::Unlink(Node *node) {
    Node *next = node->next;
    Node *prev = node->prev;
    if (prev) prev->next = next;
    if (next) next->prev = prev;
    if (mNodes == node) mNodes = next;
    mSize--;
    return next;
}

template <>
ObjPtrList<WorldCrowd>::iterator ObjPtrList<WorldCrowd>::erase(iterator it) {
    Node *node = it.mNode;
    Node *next = Unlink(node);
    delete node;
    return iterator(next);
}

// -- ObjPtrList<EventTrigger, ObjectDir> --

template <>
void ObjPtrList<EventTrigger>::Link(iterator it, Node *node) {
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

// (ObjPtrList<EventTrigger>::NodeRefOwner already defined above)
// -- ObjPtrList<FlowNode, ObjectDir> --

// (ObjPtrList<FlowNode>::NodeRefOwner already defined above)
template <>
bool ObjPtrList<FlowNode>::Replace(ObjRef *ref, Hmx::Object *obj) {
    for (iterator it = begin(); it != end(); ++it) {
        if (it.mNode == ref) {
            ReplaceNode(it.mNode, obj);
            return true;
        }
    }
    return false;
}

// -- ObjPtrList<RndDrawable, ObjectDir> --

template <>
void ObjPtrList<RndDrawable>::sort(SortFunc *) {}

// (ObjPtrList<RndDrawable>::operator<< already defined above)
// -- ObjPtrList<RndMesh, ObjectDir> --

template <>
void ObjPtrList<RndMesh>::Link(iterator it, Node *node) {
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

// -- ObjPtrList<RndTransformable, ObjectDir> --

template <>
RndTransformable *ObjPtrList<RndTransformable>::front() const {
    return mNodes ? mNodes->Obj() : 0;
}

// -- ObjPtrList<Sequence, ObjectDir> --

template <>
void ObjPtrList<Sequence>::Link(iterator it, Node *node) {
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

// (ObjPtrList<Sequence>::NodeRefOwner already defined above)
// -- ObjPtrList<Task, ObjectDir> --

template <>
Task *ObjPtrList<Task>::front() const {
    return mNodes ? mNodes->Obj() : 0;
}

// -- ObjPtrList<UILabel, ObjectDir> --

template <>
void ObjPtrList<UILabel>::Link(iterator it, Node *node) {
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

// (BeatMap::BeatMap removed — utl/BeatMap is Matching)

// (MidiReader::GetFilename removed — midi/MidiReader is Matching)

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
#include "rndobj/PartLauncher.h"

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
void ObjPtrList<RndPartLauncher>::Link(iterator it, Node *node) {
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
// ObjRefConcrete::CopyRef template instantiations (promoted from stubs)
// ============================================================================

#include "synth/ADSR.h"
#include "rndobj/BaseMaterial.h"
#include "char/CharBones.h"
#include "char/CharDriver.h"
#include "char/CharEyeDartRuleset.h"
#include "char/CharFaceServo.h"
#include "char/CharIKFoot.h"
#include "char/CharLipSync.h"
#include "char/CharLipSyncDriver.h"
#include "char/CharLookAt.h"
#include "hamobj/DancerSequence.h"
#include "flow/Flow.h"
#include "synth/FxSendMeterEffect.h"
#include "hamobj/HamIKEffector.h"
#include "hamobj/HamIKSkeleton.h"
#include "hamobj/HamLabel.h"
#include "hamobj/HamMove.h"
#include "hamobj/HamNavProvider.h"
#include "hamobj/HamPhraseMeter.h"
#include "world/LightHue.h"
#include "world/LightPreset.h"
#include "rndobj/MetaMaterial.h"
#include "hamobj/RhythmBattlePlayer.h"
#include "rndobj/Cam.h"
#include "rndobj/CubeTex.h"
#include "rndobj/Dir.h"
#include "rndobj/Env.h"
#include "rndobj/Fur.h"
#include "rndobj/Group.h"
#include "rndobj/MultiMesh.h"
#include "rndobj/PostProc.h"
#include "rndobj/PropAnim.h"
#include "rndobj/TransAnim.h"
#include "gesture/SkeletonClip.h"
#include "synth/Sound.h"
#include "world/Spotlight.h"
#include "synth/SynthSample.h"
#include "ui/UIColor.h"
#include "ui/UILabelDir.h"

#define OBJREFCONCRETE_COPYREF(T) \
template <> \
void ObjRefConcrete<T, ObjectDir>::CopyRef(const ObjRefConcrete<T, ObjectDir> &o) { \
    SetObjConcrete(o.mObject); \
}

OBJREFCONCRETE_COPYREF(ADSR)
OBJREFCONCRETE_COPYREF(BaseMaterial)
OBJREFCONCRETE_COPYREF(CamShot)
OBJREFCONCRETE_COPYREF(CharBonesObject)
OBJREFCONCRETE_COPYREF(CharClip)
OBJREFCONCRETE_COPYREF(CharCollide)
OBJREFCONCRETE_COPYREF(CharDriver)
OBJREFCONCRETE_COPYREF(CharEyeDartRuleset)
OBJREFCONCRETE_COPYREF(CharFaceServo)
OBJREFCONCRETE_COPYREF(CharIKFoot)
OBJREFCONCRETE_COPYREF(CharLipSync)
OBJREFCONCRETE_COPYREF(CharLipSyncDriver)
OBJREFCONCRETE_COPYREF(CharLookAt)
OBJREFCONCRETE_COPYREF(CharPollable)
OBJREFCONCRETE_COPYREF(CharWeightSetter)
OBJREFCONCRETE_COPYREF(Character)
OBJREFCONCRETE_COPYREF(DancerSequence)
OBJREFCONCRETE_COPYREF(Fader)
OBJREFCONCRETE_COPYREF(Flow)
OBJREFCONCRETE_COPYREF(FxSend)
OBJREFCONCRETE_COPYREF(FxSendMeterEffect)
OBJREFCONCRETE_COPYREF(HamCamShot)
OBJREFCONCRETE_COPYREF(HamIKEffector)
OBJREFCONCRETE_COPYREF(HamIKSkeleton)
OBJREFCONCRETE_COPYREF(HamLabel)
OBJREFCONCRETE_COPYREF(HamMove)
OBJREFCONCRETE_COPYREF(HamNavProvider)
OBJREFCONCRETE_COPYREF(HamPhraseMeter)
OBJREFCONCRETE_COPYREF(LightHue)
OBJREFCONCRETE_COPYREF(LightPreset)
OBJREFCONCRETE_COPYREF(MetaMaterial)
OBJREFCONCRETE_COPYREF(RhythmBattlePlayer)
OBJREFCONCRETE_COPYREF(RndCam)
OBJREFCONCRETE_COPYREF(RndCubeTex)
OBJREFCONCRETE_COPYREF(RndDir)
OBJREFCONCRETE_COPYREF(RndEnviron)
OBJREFCONCRETE_COPYREF(RndFontBase)
OBJREFCONCRETE_COPYREF(RndFur)
OBJREFCONCRETE_COPYREF(RndGroup)
OBJREFCONCRETE_COPYREF(RndLight)
OBJREFCONCRETE_COPYREF(RndMat)
OBJREFCONCRETE_COPYREF(RndMultiMesh)
OBJREFCONCRETE_COPYREF(RndPostProc)
OBJREFCONCRETE_COPYREF(RndPropAnim)
OBJREFCONCRETE_COPYREF(RndTransAnim)
OBJREFCONCRETE_COPYREF(SeqInst)
OBJREFCONCRETE_COPYREF(Sequence)
OBJREFCONCRETE_COPYREF(Sfx)
OBJREFCONCRETE_COPYREF(SkeletonClip)
OBJREFCONCRETE_COPYREF(Sound)
OBJREFCONCRETE_COPYREF(Spotlight)
OBJREFCONCRETE_COPYREF(SynthSample)
OBJREFCONCRETE_COPYREF(UIColor)
OBJREFCONCRETE_COPYREF(UIComponent)
OBJREFCONCRETE_COPYREF(UILabelDir)
OBJREFCONCRETE_COPYREF(UIList)
OBJREFCONCRETE_COPYREF(WorldCrowd)

// Hmx::Object uses Hmx:: namespace prefix
template <>
void ObjRefConcrete<Hmx::Object, ObjectDir>::CopyRef(const ObjRefConcrete<Hmx::Object, ObjectDir> &o) {
    SetObjConcrete(o.mObject);
}

#undef OBJREFCONCRETE_COPYREF

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

// -- CampaignEra stubs --

// -- PlatformMgr stubs --
// (PlatformMgr::DisableXMP removed — os/PlatformMgr_Xbox is Matching)

// -- AccomplishmentProgress stubs --

// -- AccomplishmentGroup stubs --

// -- Award stubs --

// -- ProfileMgr stubs --
#include "meta_ham/ProfileMgr.h"

// -- CharServoBone stubs --
#include "char/CharServoBone.h"

// -- CharBonesMeshes stubs (vtordisp thunk needs this) --
#include "char/CharBonesMeshes.h"

// -- AppLabel stubs --
#include "meta_ham/AppLabel.h"

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


// ============================================================================
// ObjPtrVec Node::RefOwner and erase template instantiations
// ============================================================================

#include "char/CharClip.h"
#include "flow/Flow.h"
#include "flow/FlowLabel.h"
#include "hamobj/HamCharacter.h"
#include "hamobj/HamMove.h"
#include "hamobj/RhythmDetector.h"
#include "rndobj/Env.h"
#include "rndobj/Group.h"
#include "rndobj/Tex.h"
#include "world/Spotlight.h"
#include "world/SpotlightDrawer.h"

// -- ObjPtrVec Node::RefOwner instantiations --

#define OBJPTRVEC_NODE_REFOWNER(T) \
template <> \
Hmx::Object *ObjPtrVec<T, ObjectDir>::Node::RefOwner() const { \
    return static_cast<Hmx::Object*>(mOwner); \
}

OBJPTRVEC_NODE_REFOWNER(CharClip)
OBJPTRVEC_NODE_REFOWNER(Flow)
OBJPTRVEC_NODE_REFOWNER(FlowLabel)
OBJPTRVEC_NODE_REFOWNER(FlowNode)
OBJPTRVEC_NODE_REFOWNER(HamCharacter)
OBJPTRVEC_NODE_REFOWNER(HamMove)
OBJPTRVEC_NODE_REFOWNER(Hmx::Object)
OBJPTRVEC_NODE_REFOWNER(RhythmDetector)
OBJPTRVEC_NODE_REFOWNER(RndDrawable)
OBJPTRVEC_NODE_REFOWNER(RndEnviron)
OBJPTRVEC_NODE_REFOWNER(RndGroup)
OBJPTRVEC_NODE_REFOWNER(RndLight)
OBJPTRVEC_NODE_REFOWNER(RndMat)
OBJPTRVEC_NODE_REFOWNER(RndTex)
OBJPTRVEC_NODE_REFOWNER(Spotlight)
OBJPTRVEC_NODE_REFOWNER(SpotlightDrawer)
OBJPTRVEC_NODE_REFOWNER(Waypoint)

#undef OBJPTRVEC_NODE_REFOWNER

// -- ObjPtrVec erase instantiations --

#define OBJPTRVEC_ERASE(T) \
template <> \
ObjPtrVec<T, ObjectDir>::iterator \
ObjPtrVec<T, ObjectDir>::erase(ObjPtrVec<T, ObjectDir>::iterator it) { \
    return mNodes.erase(&*it); \
}

OBJPTRVEC_ERASE(CharClip)
OBJPTRVEC_ERASE(Flow)
OBJPTRVEC_ERASE(FlowLabel)
OBJPTRVEC_ERASE(FlowNode)
OBJPTRVEC_ERASE(HamCharacter)
OBJPTRVEC_ERASE(HamMove)
OBJPTRVEC_ERASE(Hmx::Object)
OBJPTRVEC_ERASE(RhythmDetector)
OBJPTRVEC_ERASE(RndDrawable)
OBJPTRVEC_ERASE(RndEnviron)
OBJPTRVEC_ERASE(RndGroup)
OBJPTRVEC_ERASE(RndLight)
OBJPTRVEC_ERASE(RndMat)
OBJPTRVEC_ERASE(RndTex)
OBJPTRVEC_ERASE(Spotlight)
OBJPTRVEC_ERASE(SpotlightDrawer)
OBJPTRVEC_ERASE(Waypoint)

#undef OBJPTRVEC_ERASE

// -- CharDriver stubs --
#include "char/CharDriver.h"

// -- SongMetadata stubs --
#include "meta/SongMetadata.h"

// -- CacheMgr stubs --
#include "utl/CacheMgr.h"

// (PracticeSection::Steps removed — hamobj/PracticeSection is Matching)

// -- GestureMgr stubs --
#include "gesture/GestureMgr.h"

// -- CampaignProgress stubs --
#include "meta_ham/CampaignProgress.h"

// -- CampaignEra stubs (round 2) --

// -- UIListMesh stubs --
#include "ui/UIListMesh.h"

// -- Hmx::Object stubs --

// -- Award stubs (round 2) --

// -- FaderGroup stubs --
#include "synth/Faders.h"

// -- UIList stubs --
#include "ui/UIList.h"

// -- NetCacheMgr stubs --
#include "utl/NetCacheMgr.h"

// -- NetCacheMgrXbox stubs --
#include "utl/NetCacheMgr_Xbox.h"

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

// NavListHeaderNode::Handle (MQSongSortNode.obj)
#include "meta_ham/NavListNode.h"

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
// ObjPtrList::RefOwner and Node::RefOwner instantiations (promoted from stubs)
// ============================================================================

#include "flow/FlowNode.h"
#include "synth/MidiInstrument.h"
#include "synth/Sfx.h"
#include "world/Crowd.h"
#include "char/Waypoint.h"

// ObjPtrList<FlowNode>::RefOwner (list-level)
template <>
Hmx::Object *ObjPtrList<FlowNode>::RefOwner() const {
    return mOwner ? mOwner->RefOwner() : 0;
}

// Node::RefOwner instantiations
#define OBJPTRLIST_NODE_REFOWNER(T) \
template <> \
Hmx::Object *ObjPtrList<T>::Node::RefOwner() const { \
    ObjPtrList<T> *list = static_cast<ObjPtrList<T> *>(mOwner); \
    return list->Owner(); \
}

OBJPTRLIST_NODE_REFOWNER(CharPollable)
OBJPTRLIST_NODE_REFOWNER(CharWeightSetter)
OBJPTRLIST_NODE_REFOWNER(CharWeightable)
OBJPTRLIST_NODE_REFOWNER(Fader)
OBJPTRLIST_NODE_REFOWNER(NoteVoiceInst)
OBJPTRLIST_NODE_REFOWNER(ObjectDir)
OBJPTRLIST_NODE_REFOWNER(RndLight)
OBJPTRLIST_NODE_REFOWNER(SeqInst)
OBJPTRLIST_NODE_REFOWNER(SfxInst)
OBJPTRLIST_NODE_REFOWNER(ThreeDSound)
OBJPTRLIST_NODE_REFOWNER(Waypoint)
OBJPTRLIST_NODE_REFOWNER(WorldCrowd)

#undef OBJPTRLIST_NODE_REFOWNER

// ObjDirPtr<ObjectDir>::IsLoaded
template <>
bool ObjDirPtr<ObjectDir>::IsLoaded() const {
    if (mObject)
        return true;
    if (mLoader && mLoader->IsLoaded())
        return true;
    return false;
}

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
// Removed: NewBufStream@Synth — already implemented in Synth.cpp (matching unit)
// Removed: ~CriticalSection — implemented in CritSec.cpp (matching unit)
// Removed: Terminate@UILabel — implemented in UILabel.cpp
// Removed: gCheatsManager — defined in Cheats.cpp (matching unit)

// ============================================================================
// Auto-generated stubs for symbols lost when units promoted to Matching
// Generated from link errors after 339 units promoted via sync_match_percent.py
// ============================================================================

// -- ObjPtr/ObjRef template instantiations --
#pragma comment(linker, "/ALTERNATENAME:?merged_ObjPtrListPopBack@@YAXPAX@Z=__link_glue_noop")

// -- BinStream operators --
// Removed: operator>>(BinStream&, FlowTrigger::PropTriggerDefn&) — implemented in FlowTrigger.cpp (matching unit)
// Removed: PostLoad@HamDriver — implemented in HamDriver.cpp (matching unit)

// -- Data symbols --
#pragma comment(linker, "/ALTERNATENAME:?lbl_82F1AB98@@3IA=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:?lbl_82F1AB9C@@3IA=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:?lbl_82F1ABA0@@3IA=__link_glue_zero")
#pragma comment(linker, "/ALTERNATENAME:?lbl_82F5E180@@3JC=__link_glue_zero")
// Removed: sLoadingMaster@LoadingPanel — defined in LoadingPanel.cpp (matching unit)
// Removed: sSongDB@LoadingPanel — defined in LoadingPanel.cpp (matching unit)
#pragma comment(linker, "/ALTERNATENAME:?tf2cf@RndRenderState@@2PAW4_D3DCMPFUNC@@A=__link_glue_zero")

// -- Other functions --
// Removed: LocationCmp::LocationCmp — implemented in SongSortByLocation.cpp (matching unit)
// Removed: ~DifficultyCmp — implemented in SongSortByDiff.cpp (matching unit)
// Removed: ~MQSongSortNode — implemented in MQSongSortNode.cpp (matching unit)
// Removed: ~SongCmp — implemented in SongSortBySong.cpp (matching unit)
// Removed: SortCmp::operator() — implemented in StoreOffer.cpp (matching unit)
// Removed: DrawFixedZ@DrawString — implemented in Graph.cpp
// Removed: DrawShowing@SpotlightDrawer — implemented in SpotlightDrawer.cpp (matching unit)
// Removed: GetBufferSize@HttpGet — implemented in HttpGet.cpp
// Removed: GetColor@UIColor — implemented in UIColor.cpp (matching unit)
// Removed: GetNumRestarts@Game — implemented in Game.cpp
// Removed: GetSlipOffset@StreamReceiverFile — implemented in StreamReceiverFile.cpp
// Removed: Highlight@Waypoint — implemented in Waypoint.cpp
// Removed: OnSelect@NgPostProc — implemented in PostProc_NG.cpp (matching unit)
// Removed: OnSync@RndMesh — implemented in Mesh.cpp (matching unit)
// Removed: OnUnselect@NgPostProc — implemented in PostProc_NG.cpp (matching unit)
// Removed: PresyncBitmap@RndTex — implemented in Tex.cpp (matching unit)
// Removed: SpewInit — implemented in Spew.cpp
// Removed: SpewTerminate — implemented in Spew.cpp
// Removed: SyncBitmap@RndTex — implemented in Tex.cpp (matching unit)
// Removed: TerminateMakeString — implemented in MakeString.cpp
// Removed: ValidateCRC@CRC@Hmx — implemented in Crc.cpp
// Removed: Flush@HDCache — implemented in HDCache.cpp (matching unit)
// Removed: Handle@BustAMoveData — implemented in BustAMoveData.cpp (matching unit)
// Removed: Handle@OvershellSlot — implemented in Overshell.cpp (matching unit)
// Removed: InsertBreak@RndConsole — implemented in Console.cpp (matching unit)
// Removed: IsDifficultyUnlockedForProfile@HamProfile — implemented in HamProfile.cpp (matching unit)
// Removed: JointToVertexData — implemented in DepthBuffer3D.cpp (matching unit)
// Removed: OnMsg@HamUI — implemented in HamUI.cpp (matching unit)
// Removed: RemoveFromLists@Spotlight — implemented in Spotlight.cpp (matching unit)
// Removed: VertexToWorld — implemented in DepthBuffer3D.cpp (matching unit)
#pragma comment(linker, "/ALTERNATENAME:?altCfg@@YA?AVDataArrayPtr@@VDataNode@@0@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?merged_82610090@@YAPBDPBDPCH@Z=__link_glue_noop")

// -- BinStream operator<< template instantiations --

// -- C runtime / third-party library symbols --
#pragma comment(linker, "/ALTERNATENAME:Curl_if2ip=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:HIBYTE=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:LOBYTE=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:MAKEWORD=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:_fstati64=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:htons=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:ntohs=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:read=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:strncasecmp=__link_glue_noop")

// -- BinStream operator<< non-template targets (decomp compiler ALTERNATENAME chains) --

// -- Dynamic initializers (??__E) needed by auto_08_82F05C00_data.obj --
// These ??__E symbols are referenced from the CRT __xc_a section but their
// defining TUs are NonMatching split objects that lack the definitions.
#pragma comment(linker, "/ALTERNATENAME:??__E?mAssocMicXbox@ExternalMicClientMgr@@0V?$vector@PAVMicXbox@@V?$StlNodeAlloc@PAVMicXbox@@@stlpmtx_std@@@stlpmtx_std@@A@@YAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??__E?mDevToMicMaster@ExternalMicClientMgr@@0V?$vector@KV?$StlNodeAlloc@K@stlpmtx_std@@@stlpmtx_std@@A@@YAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??__E?mMicMasterToDev@ExternalMicClientMgr@@0V?$vector@KV?$StlNodeAlloc@K@stlpmtx_std@@@stlpmtx_std@@A@@YAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??__E?mMicMasters@ExternalMicClientMgr@@0V?$vector@PAVExternalMicClientProxy@@V?$StlNodeAlloc@PAVExternalMicClientProxy@@@stlpmtx_std@@@stlpmtx_std@@A@@YAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??__E?m_regProps@?$CSampleXAPOBase@VCompressionEffect@@UParams@1@@ATG@@0UXAPO_REGISTRATION_PROPERTIES@@A@@YAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??__EgInput@?A0x49b544a7@@YAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??__EgJoypadData@?A0xca10770b@@YAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??__EgMics@?A0x0c39da7f@@YAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??__Es_voiceGC@@YAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??__Es_voiceGCInProgress@@YAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??__EsFlipYZ@@YAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??__EsIdentityXfm@?A0x8e417309@@YAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??__E?m_regProps@?$CSampleXAPOBase@VBitCrushEffect@@UParams@1@@ATG@@0UXAPO_REGISTRATION_PROPERTIES@@A@@YAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??__E?m_regProps@?$CSampleXAPOBase@VDistortionEffect@@UParams@1@@ATG@@0UXAPO_REGISTRATION_PROPERTIES@@A@@YAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??__E?m_regProps@?$CSampleXAPOBase@VDelayEffect@@UParams@1@@ATG@@0UXAPO_REGISTRATION_PROPERTIES@@A@@YAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??__E?m_regProps@?$CSampleXAPOBase@VFlangerEffect@@UParams@1@@ATG@@0UXAPO_REGISTRATION_PROPERTIES@@A@@YAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??__E?m_regProps@?$CSampleXAPOBase@VEQEffect@@UParams@1@@ATG@@0UXAPO_REGISTRATION_PROPERTIES@@A@@YAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??__E?m_regProps@?$CSampleXAPOBase@VWahEffect@@UParams@1@@ATG@@0UXAPO_REGISTRATION_PROPERTIES@@A@@YAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??__E?m_regProps@?$CSampleXAPOBase@VMeterEffect@@UMeterEffectParams@@@ATG@@0UXAPO_REGISTRATION_PROPERTIES@@A@@YAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:except_data_82918780=__link_glue_zero")

// ============================================================================
// Additional stubs for remaining unresolved symbols
// Generated from link error analysis
// ============================================================================

// -- Dynamic initializers (76 symbols) --
#pragma comment(linker, "/ALTERNATENAME:??__E?m_regProps@?$CSampleXAPOBase@VEnvelopeGenerator@@UEnvelopeGeneratorParams@@@ATG@@0UXAPO_REGISTRATION_PROPERTIES@@A@@YAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??__E?m_regProps@?$CSampleXAPOBase@VGainEffect@@UGainEffectParams@@@ATG@@0UXAPO_REGISTRATION_PROPERTIES@@A@@YAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??__E?m_regProps@?$CSampleXAPOBase@VHeadsetPlaybackEffect@@UHeadsetPlaybackEffectParams@@@ATG@@0UXAPO_REGISTRATION_PROPERTIES@@A@@YAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??__E?m_regProps@?$CSampleXAPOBase@VHeadsetXferEffect@@UHeadsetXferEffectParams@@@ATG@@0UXAPO_REGISTRATION_PROPERTIES@@A@@YAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??__E?m_regProps@?$CSampleXAPOBase@VPitchShiftEffect@@UPitchShiftEffectParams@@@ATG@@0UXAPO_REGISTRATION_PROPERTIES@@A@@YAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??__E?m_regProps@?$CSampleXAPOBase@VSynapseAPO@DSP@@USynapseAPOParams@2@@ATG@@0UXAPO_REGISTRATION_PROPERTIES@@A@@YAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??__EgCrit@@YAXXZ=__link_glue_noop")
// Removed: ??__EgChildPolys — symbol exists in matching Utl.obj
// Removed: ??__EgParentPolys — symbol exists in matching Utl.obj
#pragma comment(linker, "/ALTERNATENAME:??__EgPhysicsVolumeBox@?A0x5ba00aca@@YAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??__EkConvLen@?A0x5c754947@@YAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??__EmFriendEnumRequests@?A0x8a9ffbf2@@YAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??__EmServiceIdMap@?A0x8a9ffbf2@@YAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??__EmTime@?A0x8a9ffbf2@@YAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??__EsOverlayWidth@?A0xe50ea9df@@YAXXZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??__EsSuperClassMap@@YAXXZ=__link_glue_noop")

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
// Removed: GetJumpBackTotalTime@StandardStream — implemented in StandardStream.cpp (matching unit)
// Removed: GetName@MicXbox — implemented in Mic.cpp
// Removed: Handle@FitnessCalorieSortMgr — implemented in FitnessCalorieSortMgr.cpp (matching unit)
// Removed: Handle@RndFont3d — implemented in Font3d.cpp (matching unit)
// Removed: Load@SynthSample — implemented in SynthSample.cpp (matching unit)
// Removed: Mat@RndFont3d — implemented in Font3d.cpp (matching unit)
// Removed: NewHeaderNode(2-arg)@ChallengeSortByScore — implemented in ChallengeSortByScore.cpp (matching unit)
// Removed: NewHeaderNode(2-arg)@FitnessCalorieSortByCalorie — implemented in FitnessCalorieSortByCalorie.cpp (matching unit)
// Removed: NewHeaderNode(2-arg)@MQSongSortByCharacter — implemented in MQSongSortByCharacter.cpp (matching unit)
// Removed: NewHeaderNode(2-arg)@SongSortByLocation — implemented in SongSortByLocation.cpp (matching unit)
// Removed: OldResourcePreload@LabelShrinkWrapper — implemented in LabelShrinkWrapper.cpp (matching unit)
// Removed: OnParametersChanged@FxSendFlanger360 — implemented in FxSendFlanger.cpp
// Removed: OnSync@DxMesh — implemented in rnddx9/Mesh.cpp (matching unit)
// Removed: Poll@LabelShrinkWrapper — implemented in LabelShrinkWrapper.cpp (matching unit)
// Removed: Poll@RandomIntervalGroupSeqInst — implemented in Sequence.cpp (matching unit)
// Removed: Select@ChallengeHeaderNode — implemented in ChallengeSortNode.cpp (matching unit)
// Removed: Set@NgDOFProc — implemented in DOFProc_NG.cpp (matching unit)
// Removed: SetPaused@BinkMovieImpl — implemented in BinkMovieImpl.cpp (matching unit)
// Removed: SetVConstant(float*)@DxShaderMgr — implemented in ShaderMgr.cpp (matching unit)
// Removed: StartImpl@RandomIntervalGroupSeqInst — implemented in Sequence.cpp (matching unit)
// Removed: StoreProfile@StorePanel — implemented in StorePanel.cpp (matching unit)
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
#pragma comment(linker, "/ALTERNATENAME:??0CXAPOParametersBase@ATG@@QAA@PBXPAXIE@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??0ID3DXInclude@@QAA@XZ=__link_glue_noop")
// Removed: ~AppLabel — implemented in AppLabel.cpp (matching unit)
// Removed: ~FitnessCalorieSortByCalorie — implemented in FitnessCalorieSortByCalorie.cpp (matching unit)
// Removed: ~FitnessCalorieSortCmp — implemented in FitnessCalorieSortByCalorie.cpp (matching unit)
// Removed: ~MQSongSortByCharacter — implemented in MQSongSortByCharacter.cpp (matching unit)
#pragma comment(linker, "/ALTERNATENAME:??1PeakDetector@Synapse@DSP@@QAA@XZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:??1PitchCorrectedVoice@Synapse@DSP@@QAA@XZ=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?BinkClose@@YAXPAUBINK@@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?BinkCloseTrack@@YAXPAUBINKTRACK@@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?BinkGetTrackData@@YAIPAUBINKTRACK@@PAX@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?BinkNextFrame@@YAXPAUBINK@@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?BinkOpenTrack@@YAPAUBINKTRACK@@PAUBINK@@E@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?BinkSetMemory@@YAXP6APAXH@ZP6AXPAX@Z@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?BinkStartAsyncThread@@YAHHH@Z=__link_glue_noop")
// Removed: GetLastResult@Cache — implemented in Cache.cpp
// Removed: Intersect(Segment,Triangle,int,float&) — fixed signature and already in Geo.cpp (matching unit)
// Removed: OnSmartGlassListen@FitnessGoalMgr — implemented in FitnessGoalMgr.cpp (matching unit)
#pragma comment(linker, "/ALTERNATENAME:?PreSave@WorldInstance@@UAAXAAVBinStream@@@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?RadAlloc@@YAPAXH@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:?SetReleaseSmoothing@PitchCorrectedVoice@Synapse@DSP@@QAAXM@Z=__link_glue_noop")
// Removed: UpdateGestures@HamNavList — implemented in HamNavList.cpp (matching unit)
#pragma comment(linker, "/ALTERNATENAME:?__pop_heap_aux@stlpmtx_std@@YAXPAUMemDiffEntry@@0HU?$less@UMemDiffEntry@@@1@@Z=__link_glue_noop")
// Removed: dispose@Voice — implemented in Voice.cpp (matching unit)
// Removed: kStreamEndMs@StandardStream — defined in StandardStream.cpp (matching unit)
#pragma comment(linker, "/ALTERNATENAME:?merged_82610090@@YAPAXPBXPAI@Z=__link_glue_noop")
// Removed: CopyRef@ObjRefConcrete<Hmx::Object,ObjectDir> — explicit instantiation in link_glue.cpp
// Removed: IsLoaded@ObjDirPtr<ObjectDir> — explicit instantiation in link_glue.cpp
// Removed: RefOwner@ObjPtrList<FlowNode> — explicit instantiation in link_glue.cpp
// Removed: Node::RefOwner for CharPollable, CharWeightSetter, CharWeightable, Fader,
//          NoteVoiceInst, ObjectDir, RndLight, SeqInst, SfxInst, ThreeDSound,
//          Waypoint, WorldCrowd — explicit instantiations in link_glue.cpp
#pragma comment(linker, "/ALTERNATENAME:?copy@?$char_traits@_W@stlpmtx_std@@SAPA_WPA_WPB_WI@Z=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:BinkInit=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:D3DTexture_GetLevelDesc=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:D3DTexture_LockRect=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:D3DTexture_UnlockRect=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:D3DXSetDXT3DXT5=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:FFTRealForward=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:JoypadSetActuatorsImp=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:__real_0000000000000000=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:__real_3f50624dd2f1a9fc=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:__real_3fe0000000000000=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:__real_4000000000000000=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:__real_400921fb60000000=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:__real_401921fb60000000=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:__vmx_00000000000000000000000000000000=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:__vmx_bf8000003f800000bf8000003f800000=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:_close=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:cexp=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:expand=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:expj=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:hypot=__link_glue_noop")
#pragma comment(linker, "/ALTERNATENAME:wmemcpy=__link_glue_noop")

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
// (WorldInstance specialization moved to Instance.cpp)


// -- GatherObjectsFromGroup<RndMesh> --

template <class T>
unsigned int GatherObjectsFromGroup(RndGroup *, std::vector<T *> &);

template <>
unsigned int GatherObjectsFromGroup<RndMesh>(RndGroup *, std::vector<RndMesh *> &) {
    return 0;
}
