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

bool MakeBSPTree(BSPNode *&, std::list<BSPFace> &, int) { return false; }

void BSPFace::Set(const Vector3 &, const Vector3 &, const Vector3 &) {}

bool Intersect(const Vector3 &, const Vector3 &, const Box &, float &, float &) { return false; }
bool Intersect(const Triangle &, const Box &) { return false; }

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

// ============================================================================
// Debug::Modal — shows dialog on Xbox, just prints on web
// ============================================================================

#include "os/Debug.h"

void Debug::Modal(Debug::ModalType &type, const char *msg, void *) {
    const char *prefix = "MODAL";
    if (type == kModalWarn) prefix = "WARN";
    else if (type == kModalNotify) prefix = "NOTIFY";
    else if (type == kModalFail) prefix = "FAIL";
    printf("DC3 Web [%s]: %s\n", prefix, msg ? msg : "(null)");
}

// ============================================================================
// System stubs — functions only implemented in Xbox/Win platform code
// ============================================================================

#include "os/System.h"

void NormalizeSystemArgs() {}

// ============================================================================
// Xbox Debug Monitor (xbdm) stubs
// ============================================================================

#include "xdk/xbdm/xbdm.h"

extern "C" {
HRESULT DmGetSystemInfo(DM_SYSTEM_INFO *) { return -1; }  // E_FAIL
}

#endif // __EMSCRIPTEN__
