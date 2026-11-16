// The load-time pipeline warm's object classification (PipelineWarm.cpp; PLAN
// H-104, cost H-118).
//
// The walk asks every object of every loaded dir, and every live
// reflection's draws on every later load, which of ~16 classes it is. Asked
// with dynamic_cast that was nearly all of the walk: at song end the
// reflection re-walk was 3-5 ms of each loader frame, almost all in
// __dynamic_cast, for keys that were already warm. Whether a dynamic_cast to a
// class succeeds depends only on the object's dynamic type, so each type is
// asked once and its answers kept as these bits; the walk then casts only to
// the class it has already found.
#pragma once

#include <cstdint>

namespace Hmx {
class Object;
}
class ObjectDir;

enum NativeWarmClass : uint32_t {
    kWarmIsMesh = 1u << 0,
    kWarmIsText = 1u << 1,
    kWarmIsHamRibbon = 1u << 2,
    kWarmIsRibbon = 1u << 3,
    kWarmIsCharFeedback = 1u << 4,
    kWarmIsFlare = 1u << 5,
    kWarmIsScreenMask = 1u << 6,
    kWarmIsTexBlender = 1u << 7,
    kWarmIsParticleSys = 1u << 8,
    kWarmIsFont = 1u << 9,
    kWarmIsPropAnim = 1u << 10,
    kWarmIsReflection = 1u << 11,
    kWarmIsDir = 1u << 12,
    kWarmIsGroup = 1u << 13,
    kWarmIsPanelDir = 1u << 14,
    kWarmIsCharacter = 1u << 15,
};

// The classes `obj` is (0 for null): from the cache, or asked once and kept.
uint32_t NativeWarmClassOf(Hmx::Object *obj);
// How many dynamic types have been asked (each once, ever): for the test.
int NativeWarmTypesClassified();

void NativeWarmLoadedDir(ObjectDir *dir);
