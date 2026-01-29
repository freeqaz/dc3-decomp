#include "rndobj/PostProc_NG.h"
#include "Memory.h"
#include "Tex.h"
#include "math/Rand.h"
#include "obj/Object.h"
#include "os/Debug.h"
#include "rndobj/PostProc.h"
#include "rndobj/Rnd.h"
#include "rndobj/Rnd_NG.h"
#include "rndobj/Tex.h"
#include "rndobj/VelocityBuffer.h"
#include "rndobj/HiResScreen.h"
#include "rndobj/ShaderMgr.h"
#include "utl/Loader.h"

extern void merged_ObjPtrListPopBack(void *);

Hmx::Color NgPostProc::s_prevBloomColor(-1, -1, -1, -1);
float NgPostProc::s_prevBloomIntensity = -1;
NgPostProc::BloomTextures<3> NgPostProc::sBloom;

NgPostProc::BloomTextureSet::BloomTextureSet() {
    mBloomTexture[0] = (RndTex*)0;
    mBloomTexture[1] = (RndTex*)0;
}

NgPostProc::BloomTextureSet::~BloomTextureSet() { FreeTextures(); }

void NgPostProc::BloomTextureSet::AllocateTextures(unsigned int w, unsigned int h) {
    MILO_ASSERT(mBloomTexture[0] == NULL, 0x48);
    mBloomTexture[0] = Hmx::Object::New<RndTex>();
    mBloomTexture[0]->SetBitmap(w, h, TheRnd.Bpp(), RndTex::kRenderedNoZ, false, nullptr);
    mBloomTexture[1] = mBloomTexture[0];
}

void NgPostProc::BloomTextureSet::FreeTextures() { RELEASE(mBloomTexture[0]); }

NgPostProc::NgPostProc()
    : unk22c(RandomFloat()), unk230(RandomFloat()), unk234(0), unk238(0), unk23c(this),
      unk250(1) {}

NgPostProc::~NgPostProc() {}

void NgPostProc::Select() {
    RndPostProc::Select();
    unk22c = RandomFloat();
    unk230 = RandomFloat();
}

void NgPostProc::Init() {
    REGISTER_OBJ_FACTORY(NgPostProc);
    PhysMemTypeTracker tracker("D3D(phys):NgPostProc");
    RebuildTex();
}

void NgPostProc::RebuildTex() {
    ReleaseTex();
    int w = 0x80;
    int h = 0x80;
    if (TheLoadMgr.GetPlatform() != kPlatformNone) {
        MILO_ASSERT(TheNgRnd.PreProcessTexture(), 0x3AB );
        w = TheNgRnd.PreProcessTexture()->Width();
        h = TheNgRnd.PreProcessTexture()->Height();
    }
    RndVelocityBuffer::Singleton().AllocateData(w, h, TheRnd.Bpp());
    sBloom.AllocateTextures(w * 4, h * 4);
}

void NgPostProc::DoVelocity() {
    *(s8*)((u8*)&TheShaderMgr + 0x39) = 0;
    if ((mMotionBlurVelocity) && (*(u8*)((u8*)&TheHiResScreen + 0x4) == 0) &&
        (RndVelocityBuffer::Singleton().Draw(*(RndCam**)((u8*)&TheRnd + 0xE4), unk23c) != 0)) {
        *(s8*)((u8*)&TheShaderMgr + 0x39) = 1;
        float sp50 = *(float*)((u8*)&RndVelocityBuffer::Singleton() + 0x36BE8);
        float sp54 = *(float*)((u8*)&RndVelocityBuffer::Singleton() + 0x36BE8);
        float sp58 = *(float*)((u8*)&RndVelocityBuffer::Singleton() + 0x36BE8);
        float sp5C = *(float*)((u8*)&RndVelocityBuffer::Singleton() + 0x36BE8);
        typedef void (*ShaderFunc)(void*, int, float*);
        void* shaderMgrVTable = *(void**)&TheShaderMgr;
        ShaderFunc func = *(ShaderFunc*)((u8*)shaderMgrVTable + 0x40);
        func(&TheShaderMgr, 0x7A, &sp50);
    }
    void* pList = (u8*)this + 0x23C;
    int head = *(int*)((u8*)this + 0x240);
    if (head != 0) {
        int temp = head;
        if (temp != 0) {
            do {
                merged_ObjPtrListPopBack(pList);
                temp = *(int*)((u8*)pList + 4);
            } while (temp != 0);
        }
    }
}
