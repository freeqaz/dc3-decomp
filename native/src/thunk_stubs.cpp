// thunk_stubs.cpp - Proper C++ method definitions for functions that need
// non-virtual thunks (Itanium ABI). The asm-label stubs in engine_stubs_generated.cpp
// provide the mangled symbol but don't generate the vtable thunks that GCC/Clang need
// for classes with multiple inheritance.

#include "hamobj/CharFeedback.h"
#include "gesture/DepthBuffer3D.h"
#include "hamobj/HamCharacter.h"
#include "hamobj/HamListRibbon.h"
#include "hamobj/MoveDir.h"
#include "world/PhysicsVolume.h"
#include "rndobj/Env.h"
#include "rndobj/Flare.h"
#include "rndobj/Gen.h"
#include "rndobj/Mesh.h"
#include "rndobj/Part.h"
#include "gesture/SkeletonClip.h"
#include "gesture/SkeletonViz.h"
#include "world/Spotlight.h"
#include "gesture/StreamRecorder.h"
#include "synth/Emitter.h"
#include "world/Crowd3DCharHandle.h"
#include "world/Instance.h"
#include "world/Reflection.h"

// CharFeedback
void CharFeedback::Poll() {}

// DepthBuffer3D
void DepthBuffer3D::Save(BinStream &) {}
void DepthBuffer3D::Copy(const Hmx::Object *, Hmx::Object::CopyType) {}
void DepthBuffer3D::Load(BinStream &) {}

// HamCharacter
void HamCharacter::Poll() {}

// HamListRibbon
float HamListRibbon::EndFrame() { return 0; }

// MoveDir
float MoveDir::UpdateOverlay(RndOverlay *, float) { return 0; }

// PhysicsVolume
void PhysicsVolume::Load(BinStream &) {}

// RndEnviron
void RndEnviron::Load(BinStream &) {}

// RndFlare
void RndFlare::DrawShowing() {}
void RndFlare::Mats(std::list<RndMat *> &, bool) {}

// RndGenerator
void RndGenerator::DrawShowing() {}
bool RndGenerator::MakeWorldSphere(Sphere &, bool) { return false; }

// RndMesh
bool RndMesh::Replace(ObjRef *, Hmx::Object *) { return false; }

// RndParticleSys
void RndParticleSys::Load(BinStream &) {}
void RndParticleSys::Mats(std::list<RndMat *> &, bool) {}

// SkeletonClip
bool SkeletonClip::PrevSkeleton(const Skeleton &, int, ArchiveSkeleton &, int &) const { return false; }

// SkeletonViz
void SkeletonViz::Poll() {}

// Spotlight
void Spotlight::Poll() {}

// StreamRecorder
void StreamRecorder::Poll() {}

// SynthEmitter
void SynthEmitter::Poll() {}

// WorldCrowd3DCharHandle
bool WorldCrowd3DCharHandle::SyncProperty(DataNode &, DataArray *, int, PropOp) { return false; }

// WorldInstance — real impl in HamNavList.cpp

// WorldReflection
void WorldReflection::Highlight() {}

// ---------------------------------------------------------------------------
// rndobj/Utl.cpp stubs — excluded from native build due to API mismatches.
// Minimal stubs to satisfy linker; only SortPolls/SortDraws need real logic.
// ---------------------------------------------------------------------------
#include "rndobj/Poll.h"
#include "rndobj/Draw.h"
#include "rndobj/Trans.h"
#include "rndobj/Mat.h"
#include "rndobj/Mesh.h"
#include "rndobj/MultiMesh.h"
#include "rndobj/TransAnim.h"
#include "rndobj/MetaMaterial.h"
#include "math/Color.h"
#include "math/Vec.h"
#include "math/Geo.h"
#include "math/Mtx.h"
#include <cstring>
#include <vector>

bool SortPolls(const RndPollable *p1, const RndPollable *p2) {
    if (p1->PollEnabled() != p2->PollEnabled()) {
        return p1->PollEnabled();
    }
    return strcmp(p1->Name(), p2->Name()) < 0;
}

bool SortDraws(RndDrawable *draw1, RndDrawable *draw2) {
    return strcmp(draw1->Name(), draw2->Name()) < 0;
}

float ConvertFov(float a, float) { return a; }
void PreMultiplyAlpha(Hmx::Color &) {}
void RndUtlPreInit() {}
void RndUtlInit() {}
void RndUtlTerminate() {}
void RndSplasherPoll() {}
void RndSplasherSuspend() {}
void RndSplasherResume() {}

typedef void (*SplashFunc)(void);
void SetRndSplasherCallback(SplashFunc, SplashFunc, SplashFunc) {}

void SetLocalScale(RndTransformable *, const Vector3 &) {}
int GenerationCount(RndTransformable *, RndTransformable *) { return 0; }
void CreateAndSetMetaMat(RndMat *) {}

MatShaderOptions GetDefaultMatShaderOpts(const Hmx::Object *, RndMat *) {
    return MatShaderOptions();
}

void ResetColors(std::vector<Hmx::Color> &colors, int newNumColors) {
    Hmx::Color reset(1, 1, 1, 1);
    colors.resize(newNumColors);
    for (int i = 0; i < newNumColors; i++) {
        colors[i] = reset;
    }
}

void CalcBox(RndMesh *, Box &) {}
void ClearAO(RndMesh *) {}

// Draw utility stubs — no-op in viewer
void UtilDrawSphere(const Vector3 &, float, const Hmx::Color &, RndMat *) {}
void UtilDrawLine(const Vector2 &, const Vector2 &, const Hmx::Color &) {}
void UtilDrawString(const char *, const Vector3 &, const Hmx::Color &) {}
void UtilDrawAxes(const Transform &, float, const Hmx::Color &) {}
void UtilDrawBox(const Transform &, const Box &, const Hmx::Color &, bool) {}
void UtilDrawRect2D(const Vector2 &, const Vector2 &, const Hmx::Color &) {}
void UtilDrawCylinder(const Transform &, float, float, const Hmx::Color &, int) {}

// TransAnim key manipulation stubs
void TransformKeys(RndTransAnim *, const Transform &) {}
void SpliceKeys(RndTransAnim *, RndTransAnim *, float, float) {}
void LinearizeKeys(RndTransAnim *, float, float, float, float, float) {}

// MultiMesh transform stubs
void ScrambleXfms(RndMultiMesh *) {}
void SortXfms(RndMultiMesh *, const Vector3 &) {}
