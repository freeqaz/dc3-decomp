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

// WorldInstance
void WorldInstance::Load(BinStream &) {}

// WorldReflection
void WorldReflection::Highlight() {}
