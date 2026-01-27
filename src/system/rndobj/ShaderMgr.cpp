#include "rndobj/ShaderMgr.h"
#include "Shader.h"
#include "macros.h"
#include "math/Mtx.h"
#include "obj/Data.h"
#include "obj/Object.h"
#include "os/Debug.h"
#include "os/File.h"
#include "os/Platform.h"
#include "os/System.h"
#include "rndobj/ShaderOptions.h"
#include "rndobj/ShaderProgram.h"
#include "rndobj/Utl.h"
#include "utl/FileStream.h"
#include "utl/Loader.h"
#include "utl/MemMgr.h"

RndShaderMgr::RndShaderMgr()
    : mShaderPoolCount(0), unk5c(0), mConstantCache(0), unk68(0), unk6d(0),
      mShowShaderErrors(1), mShowMetaMatErrors(0) {}

void RndShaderMgr::PreInit() {
    if (!unk6d) {
        unkc = 0;
        unk6d = true;
        unk10 = 0;
        unk14 = 1;
        unk18 = 0;
        unk1c = 0;
        unk20 = 0;
        unk24 = 0;
        unk25 = 0;
        unk26 = 0;
        unk27 = 0;
        unk28 = 0;
        unk29 = 0;
        unk2b = 0;
        unk2c = 0;
        unk2d = 0;
        unk2e = 0;
        unk2f = 0;
        unk30 = 0;
        unk31 = 0;
        unk34 = 0;
        unk38 = 0;
        unk39 = 0;
        unk3a = 0;
        unk2a = 0;
        unk3b = 0;
        unk3c = 0;
        unk3d = 0;
        unk3e = 0;
        unk3f = 0;
        mAllowPerPixel = 1;
        unk41 = 1;
        mDisplayShaderError = true;
        RELEASE(mWorkMat);
        RELEASE(mPostProcMat);
        RELEASE(mDrawHighlightMat);
        RELEASE(mDrawRectMat);
        mWorkMat = Hmx::Object::New<RndMat>();
        mPostProcMat = Hmx::Object::New<RndMat>();
        mDrawHighlightMat = Hmx::Object::New<RndMat>();
        mDrawRectMat = Hmx::Object::New<RndMat>();
        CreateAndSetMetaMat(mWorkMat);
        CreateAndSetMetaMat(mPostProcMat);
        CreateAndSetMetaMat(mDrawHighlightMat);
        CreateAndSetMetaMat(mDrawRectMat);
        MILO_ASSERT(mConstantCache == NULL, 104);
        unk68 = 516;
        {
            MemTemp tmp;
            mConstantCache = new float[unk68];
        }
        LoadShaders("%s_preinit_shaders");
    }
}

void RndShaderMgr::Init() {
    PreInit();
    LoadShaders("%s_shaders");
}

void RndShaderMgr::Terminate() {
    Invalidate(kMaxShaderTypes);
    RELEASE(mConstantCache);
    unk68 = 0;
}

void RndShaderMgr::UpdateCache(const Transform &xfm, int idx) {
    float *p = (float *)(*(int *)((char *)this + 0x64) + idx * 0x30);
    float f0 = *(float *)&xfm;
    float f4 = *(float *)((char *)&xfm + 4);
    float f8 = *(float *)((char *)&xfm + 8);
    float f1 = *(float *)((char *)&xfm + 0x10);
    float f5 = *(float *)((char *)&xfm + 0x14);
    float f9 = *(float *)((char *)&xfm + 0x18);
    float f2 = *(float *)((char *)&xfm + 0x20);
    float f6 = *(float *)((char *)&xfm + 0x24);
    float f10 = *(float *)((char *)&xfm + 0x28);
    float f3 = *(float *)((char *)&xfm + 0x30);
    float f7 = *(float *)((char *)&xfm + 0x34);
    float f11 = *(float *)((char *)&xfm + 0x38);
    p[0] = f0;
    p[1] = f1;
    p[2] = f2;
    p[3] = f3;
    p[4] = f4;
    p[5] = f5;
    p[6] = f6;
    p[7] = f7;
    p[8] = f8;
    p[9] = f9;
    p[10] = f10;
    p[11] = f11;
}

void RndShaderMgr::ShaderPoolAlloc(int i) { unk5c = i; }

void RndShaderMgr::SetMeshInfo(int i, bool b) {
    unk10 = i;
    unkc = b;
}

void RndShaderMgr::SetShaderErrorDisplay(bool disp) { mDisplayShaderError = disp; }
bool RndShaderMgr::GetShaderErrorDisplay() { return mDisplayShaderError; }

unsigned long RndShaderMgr::InitShaders() {
    if (UsingCD() || GetGfxMode() == kOldGfx)
        mCacheShaders = false;
    else {
        DataArray *cfg = SystemConfig("rnd", "cache_shaders");
        mCacheShaders = cfg->Int(1);
    }
    RndShader::Init();
    return RndShaderProgram::InitModTime();
}

void RndShaderMgr::LoadShaders(const char *cc) {
    unsigned long shaders = InitShaders();
    if (TheLoadMgr.GetPlatform() != kPlatformNone) {
        String str(MakeString(cc, PlatformSymbol(TheLoadMgr.GetPlatform())));
        FileStat stat;
        if (!mCacheShaders || !FileGetStat(str.c_str(), &stat)) {
            if (stat.st_mtime > shaders || strstr(cc, "preinit")) {
                FileStream stream(str.c_str(), FileStream::kRead, true);
                if (!stream.Fail()) {
                    // this check is made somewhere in here according to the asm
                    // TheLoadMgr.GetPlatform() == kPlatformXBox;
                    LoadShaderFile(stream);
                } else {
                    if (UsingCD() && GetGfxMode() == kNewGfx) {
                        MILO_NOTIFY("Can't load shader file %s!!", str.c_str());
                    }
                }
            }
        }
    }
}

void RndShaderMgr::SetTransform(const Transform &xfm) {
    unk10 = 0;
    SetVConstant4x3((VShaderConstant)0x5c, Hmx::Matrix4(xfm));
}

void RndShaderMgr::Invalidate(ShaderType t) {
    for (std::list<ShaderTree>::iterator it = mShaderTrees.begin();
         it != mShaderTrees.end();) {
        if (it == mShaderTrees.begin() && it->shaderType != t) {
            ++it;
        } else {
            delete it->obj;
            it = mShaderTrees.erase(it);
        }
    }
    RndShaderProgram::InitModTime();
}

void RndShaderMgr::LoadShaderFile(FileStream &fs) {
    if (TheLoadMgr.GetPlatform() == kPlatformPS3) {
        RndSplasherResume();
        unsigned int fileType, fileVersion;
        fs >> fileType;
        fs >> fileVersion;
        MILO_ASSERT(fileType == PS3_SHADERS_TYPE, 0xBF);
        MILO_ASSERT(fileVersion == PS3_SHADERS_VERSION, 0xC0);
        RndSplasherSuspend();
    }
    int num;
    fs >> num;
    while (num--) {
        Symbol name;
        fs >> name;
        ShaderType shaderType = ShaderTypeFromName(name.Str());
        int alloc;
        fs >> alloc;
        unk5c = alloc;
        while (alloc--) {
            u64 u50;
            fs >> u50;
            RndShaderProgram &program = FindShader(shaderType, ShaderOptions(u50));
            int i6c;
            fs >> i6c;
            RndShaderBuffer *buf1;
            program.LoadShaderBuffer(fs, i6c, buf1);
            fs >> i6c;
            RndShaderBuffer *buf2;
            program.LoadShaderBuffer(fs, i6c, buf2);
            program.Cache(shaderType, ShaderOptions(u50), buf1, buf2);
            delete buf1;
            delete buf2;
            RndSplasherPoll();
        }
    }
}

void *RndShaderMgr::AllocShader() {
    if (mShaderPoolCount == 0 && unk5c > 0) {
        mShaderPoolCount = unk5c;
        unk5c = 0;
        mShaderPool = MemAlloc(unk60 * mShaderPoolCount, __FILE__, 0x11c, "ShaderPool");
    }
    if (mShaderPoolCount <= 0) {
        if (UsingCD()) {
            MILO_NOTIFY_ONCE("Shader Pool is allocating dynamically");
        }
        unk5c = 0;
        mShaderPoolCount = 0x100;
        mShaderPool = MemAlloc(unk60 << 8, __FILE__, 0x127, "ShaderPool");
    }
    MILO_ASSERT(mShaderPoolCount-- > 0, 0x12A);
    // increment mShaderPool by unk60
    void *old = mShaderPool;
    char *pool = (char *)mShaderPool;
    pool += unk60;
    mShaderPool = pool;
    unk5c--;
    return old;
}

RndShaderProgram &RndShaderMgr::FindShader(ShaderType t, const ShaderOptions &opts) {
    for (std::list<ShaderTree>::iterator it = mShaderTrees.begin();
         it != mShaderTrees.end() && it->shaderType != t;
         ++it) {
        // more stuff here
    }
    ShaderTree tree;
    tree.shaderType = t;
    RndShaderProgram *p = NewShaderProgram();
    p->unk8 = opts.flags;
    tree.obj = p;
    if (t == kStandardShader) {
        mShaderTrees.push_front(tree);
    } else {
        mShaderTrees.push_back(tree);
    }
    return *p;
}
