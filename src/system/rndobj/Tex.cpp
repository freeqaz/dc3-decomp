#include "rndobj/Tex.h"
#include "Tex.h"
#include "obj/Data.h"
#include "obj/Object.h"
#include "os/File.h"
#include "os/System.h"
#include "os/Debug.h"
#include "rndobj/Bitmap.h"
#include "rndobj/Rnd.h"
#include "rndobj/Utl.h"
#include "utl/BinStream.h"
#include "utl/CRC.h"
#include "utl/FilePath.h"
#include "utl/Loader.h"

bool UseBottomMip() {
    DataArray *found = SystemConfig("rnd")->FindArray("use_bottom_mip", false);
    if (found)
        return found->Int(1);
    else
        return false;
}

void CopyBottomMip(RndBitmap &dst, const RndBitmap &src) {
    MILO_ASSERT(&src != &dst, 0x25);
    const RndBitmap *srcPtr = &src;
    while (srcPtr->nextMip())
        srcPtr = srcPtr->nextMip();
    dst.Create(*srcPtr, srcPtr->Bpp(), srcPtr->Order(), nullptr);
}

RndTex::RndTex()
    : mMipMapK(-8.0f), mType(kRegular), mWidth(0), mHeight(0), mBpp(32), mFilepath(),
      mNumMips(0), mOptimizeForPS3(0), mLoader(0) {}

RndTex::~RndTex() {
    delete mLoader;
}

BEGIN_HANDLERS(RndTex)
    HANDLE(set_bitmap, OnSetBitmap)
    HANDLE(set_rendered, OnSetRendered)
    HANDLE_EXPR(file_path, mFilepath.c_str())
    HANDLE_ACTION(set_file_path, mFilepath.Set(FilePath::Root().c_str(), _msg->Str(2)))
    HANDLE_EXPR(size_kb, SizeKb())
    HANDLE_EXPR(tex_type, mType)
    HANDLE_ACTION(save_bmp, SaveBitmap(_msg->Str(2)))
    HANDLE_ACTION(save_png, _msg->Str(2)) // musta got stubbed out
    HANDLE_SUPERCLASS(Hmx::Object)
END_HANDLERS

BEGIN_PROPSYNCS(RndTex)
    SYNC_PROP_SET(width, mWidth, OnSetSize(_val.Int(), mHeight))
    SYNC_PROP_SET(height, mHeight, OnSetSize(mWidth, _val.Int())) {
        static Symbol _s("bpp");
        if (sym == _s && _op & kPropGet)
            return PropSync(mBpp, _val, _prop, _i + 1, _op);
    }
    SYNC_PROP(mip_map_k, mMipMapK)
    SYNC_PROP(optimize_for_ps3, mOptimizeForPS3)
    SYNC_PROP_MODIFY(file_path, mFilepath, SetBitmap(mFilepath))
    SYNC_SUPERCLASS(Hmx::Object)
END_PROPSYNCS

DataNode RndTex::OnSetSize(int, int) { return 0; }

BEGIN_SAVES(RndTex)
    SAVE_REVS(11, 0)
    SAVE_SUPERCLASS(Hmx::Object)
    bs << mWidth << mHeight << mBpp << mFilepath << mMipMapK << mType;
    bs << (bool)mNumMips;
    bs << mOptimizeForPS3;
    if (bs.Cached()) {
        mBitmap.Save(bs);
    }
END_SAVES

BEGIN_COPYS(RndTex)
    COPY_SUPERCLASS(Hmx::Object)
    CREATE_COPY(RndTex)
    BEGIN_COPYING_MEMBERS
        if (ty != kCopyFromMax) {
            COPY_MEMBER(mMipMapK)
        } else if (mType != c->mType)
            return;
        PresyncBitmap();
        COPY_MEMBER(unk2c)
        COPY_MEMBER(mType)
        COPY_MEMBER(mWidth)
        COPY_MEMBER(mHeight)
        COPY_MEMBER(mBpp)
        COPY_MEMBER(mFilepath)
        COPY_MEMBER(mNumMips)
        COPY_MEMBER(mOptimizeForPS3)
        mBitmap.Create(c->mBitmap, c->mBitmap.Bpp(), c->mBitmap.Order(), nullptr);
        SyncBitmap();
    END_COPYING_MEMBERS
END_COPYS

void RndTex::Print() {
    TheDebug << "   width: " << mWidth << "\n";
    TheDebug << "   height: " << mHeight << "\n";
    TheDebug << "   bpp: " << mBpp << "\n";
    TheDebug << "   mipMapK: " << mMipMapK << "\n";
    TheDebug << "   file: " << mFilepath << "\n";
    TheDebug << "   type: " << mType << "\n";
}

void RndTex::LockBitmap(RndBitmap &bmap, int i) {
    if (mBitmap.Order() & 0x38) {
        bmap.Create(mBitmap, 0x20, 0, 0);
    } else {
        bmap.Create(
            mBitmap.Width(),
            mBitmap.Height(),
            mBitmap.RowBytes(),
            mBitmap.Bpp(),
            mBitmap.Order(),
            mBitmap.Palette(),
            mBitmap.Pixels(),
            0
        );
    }
}

TextStream &operator<<(TextStream &ts, RndTex::Type ty) {
    switch (ty) {
    case RndTex::kRegular:
        ts << "Regular";
        break;
    case RndTex::kRendered:
        ts << "Rendered";
        break;
    case RndTex::kMovie:
        ts << "Movie";
        break;
    case RndTex::kBackBuffer:
        ts << "BackBuffer";
        break;
    case RndTex::kFrontBuffer:
        ts << "FrontBuffer";
        break;
    case RndTex::kRenderedNoZ:
        ts << "RenderedNoZ";
        break;
    case RndTex::kShadowMap:
        ts << "ShadowMap";
        break;
    case RndTex::kDepthVolumeMap:
        ts << "DepthVolumeMap";
        break;
    case RndTex::kDensityMap:
        ts << "DensityMap";
        break;
    case RndTex::kScratch:
        ts << "Scratch";
        break;
    case RndTex::kDeviceTexture:
        ts << "DeviceTexture";
        break;
    case RndTex::kRegularLinear:
        ts << "RegularLinear";
        break;
    }
    return ts;
}

void RndTex::SaveBitmap(const char *bmp) {
    RndBitmap bitmap;
    LockBitmap(bitmap, 3);
    RndBitmap bitmap2;
    bitmap2.Create(bitmap, 32, 0, nullptr);
    bitmap2.SaveBmp(bmp);
    UnlockBitmap();
}

void RndTex::PlatformBppOrder(const char *path, int &bpp, int &order, bool hasAlpha) {
    bool bbb;

    switch (TheLoadMgr.GetPlatform()) {
    case kPlatformWii:
        order = 8;
        if (hasAlpha) {
            order = 0x148;
            bpp = 8;
        } else
            bpp = 4;
        order |= 0x40;
        break;

    case kPlatformXBox:
    case kPlatformPC:
    case kPlatformPS3:
        bbb = path && strstr(path, "_norm");

        if (bbb) {
            if (TheLoadMgr.GetPlatform() == kPlatformXBox)
                order = 0x20;
            else if (TheLoadMgr.GetPlatform() == kPlatformPS3)
                order = 8;
            else
                order = 0;
        } else {
            order = hasAlpha ? 0x18 : 8;
        }
        if (order == 8)
            bpp = 4;
        else if (order & 0x38U)
            bpp = 8;
        else if (bbb)
            bpp = 0x18;
        else if (bpp < 0x10)
            bpp = 0x10;
        break;

    case kPlatform3DS:
        order = 0x600;
        bpp = hasAlpha ? 8 : 4;
        break;

    case kPlatformNone:
        order = 0;
        break;

    default:
        MILO_FAIL("bad input platform value!");
        break;
    }
}

const char *CheckDim(int dim, RndTex::Type ty, bool b) {
    const char *ret = 0;
    if (dim == 0)
        return ret;
    if (b) {
        if (ty == RndTex::kMovie) {
            if ((dim % 16) != 0) {
                ret = "%s: dimensions not multiple of 16";
            }
        } else if ((dim % 8) != 0) {
            ret = "%s: dimensions not multiple of 8";
        }
    }
    if (GetGfxMode() == 0) {
        if ((b != 0) && (dim > 0x400)) {
            ret = "%s: dimensions greater than 1024";
        } else if (dim > 0x1000) {
            ret = "%s: dimensions greater than 4096";
        }
    }
    if (b) {
        bool isPowerOf2;
        if (dim < 0) {
            isPowerOf2 = false;
        } else {
            isPowerOf2 = (((dim - 1) & dim) == 0);
        }
        if (!isPowerOf2) {
            ret = "%s: dimensions are not power-of-2";
        }
    }
    return ret;
}

void RndTex::SetBitmap(FileLoader *fl) {
    PresyncBitmap();
    mType = kRegular;
    char *buffer;
    if (fl) {
        mFilepath = fl->LoaderFile();
        TheLoadMgr.PollUntilLoaded(fl, nullptr);
        buffer = fl->GetBuffer(nullptr);
        if (!TheLoadMgr.EditMode() && fl != mLoader) {
            if (!strstr(mFilepath.c_str(), "_keep")) {
                MILO_NOTIFY("%s will not be included on a disc build", mFilepath);
            }
        }
        delete fl;
    } else {
        mFilepath.Set(FilePath::Root().c_str(), "");
        buffer = nullptr;
    }

    if (buffer) {
        if (UseBottomMip()) {
            RndBitmap bmap;
            bmap.Create(buffer);
            CopyBottomMip(mBitmap, bmap);
        } else {
            mBitmap.Create(buffer);
        }
        if (!mBitmap.HasName()) {
            if (!mFilepath.empty()) {
                mBitmap.SetName(
                    Hmx::CRC(FileRelativePath(FileExecRoot(), mFilepath.c_str()))
                );
            }
        }
        if (!mBitmap.HasName() && mType == kRegular) {
            MILO_LOG(
                "Bitmap %s, does not have name set, it will not be cached!\n",
                FileRelativePath(FileRoot(), mFilepath.c_str())
            );
        }
        mWidth = mBitmap.Width();
        mHeight = mBitmap.Height();
        mBpp = mBitmap.Bpp();
        mNumMips = mBitmap.NumMips();
    } else {
        mBitmap.Reset();
        mWidth = mHeight = 0;
        mBpp = 32;
        mNumMips = 0;
    }
    SyncBitmap();
}

void RndTex::SetBitmap(const FilePath &path) {
    Loader *ldr = TheLoadMgr.ForceGetLoader(path);
    SetBitmap(dynamic_cast<FileLoader *>(ldr));
}

void RndTex::SetBitmap(int w, int h, int bpp, Type ty, bool useMips, const char *path) {
    PresyncBitmap();
    mWidth = w;
    mHeight = h;
    mBpp = bpp;
    mType = ty;
    mFilepath.Set(FilePath::Root().c_str(), "");
    mNumMips = 0;
    mBitmap.Reset();
    if (mType & kBackBuffer) {
        mWidth = TheRnd.Width();
        mHeight = TheRnd.Height();
        mBpp = TheRnd.Bpp();
    } else if (mType & kRendered) {
        if (mBpp & 0xF) {
            mBpp = (mBpp < 0x20) ? 0x10 : 0x20;
        }
        if (useMips) {
            for (int i = mWidth, j = mHeight; i > 0x10 && j > 0x10; i >>= 1, j >>= 1) {
                mNumMips++;
            }
        }
    } else {
        char *err = (char *)CheckSize(mWidth, mHeight, mBpp, mNumMips, mType, false);
        if (err)
            TheDebug.Notify(MakeString(err, Name()));
        else {
            if (!(mType & 0x204U)) {
                int bppOut = mBpp;
                int orderOut;
                PlatformBppOrder(path, bppOut, orderOut, true);
                mBitmap.Create(mWidth, mHeight, 0, bppOut, orderOut, 0, 0, 0);
                if (useMips) {
                    mBitmap.GenerateMips();
                    mNumMips = mBitmap.NumMips();
                }
            }
        }
    }
    SyncBitmap();
}

void RndTex::SetBitmap(const RndBitmap &bmap, const char *path, bool keepFormat) {
    PresyncBitmap();
    mWidth = bmap.Width();
    mHeight = bmap.Height();
    mBpp = bmap.Bpp();
    mType = kRegular;
    mFilepath.Set(FilePath::Root().c_str(), "");
    mNumMips = bmap.NumMips();
    MILO_ASSERT(!mNumMips, 0x111);
    const char *err = CheckSize(mWidth, mHeight, mBpp, mNumMips, mType, false);
    if (err) {
        MILO_WARN(err, Name());
        mBitmap.Reset();
    } else {
        int bppOut = bmap.Bpp();
        int orderOut = bmap.Order();
        if (!keepFormat) {
            PlatformBppOrder(path, bppOut, orderOut, bmap.IsTranslucent());
        }
        mBitmap.Create(bmap, bppOut, orderOut, 0);
    }
    SyncBitmap();
}

void RndTex::SetBitmap(const RndBitmap &bmap, const char *path, bool keepFormat, Type ty) {
    PresyncBitmap();
    mWidth = bmap.Width();
    mHeight = bmap.Height();
    mBpp = bmap.Bpp();
    mType = ty;
    mFilepath.Set(FilePath::Root().c_str(), "");
    mNumMips = bmap.NumMips();
    const char *err = CheckSize(mWidth, mHeight, mBpp, mNumMips, mType, false);
    if (err) {
        MILO_NOTIFY(err, Name());
        mBitmap.Reset();
    } else {
        int bppOut = bmap.Bpp();
        int orderOut = bmap.Order();
        if (!keepFormat) {
            PlatformBppOrder(path, bppOut, orderOut, bmap.IsTranslucent());
        }
        mBitmap.Create(bmap, bppOut, orderOut, 0);
    }
    SyncBitmap();
}

#ifndef HX_NATIVE
void RndTex::PresyncBitmap() {}

void RndTex::SyncBitmap() {}
#endif

void RndTex::SetPowerOf2() {
    // mIsPowerOf2 field was removed in DC3 - no-op
}

bool RndTex::PowerOf2() {
    auto& width = mWidth;
    bool hPow2 = mHeight <= 0 || (((mHeight - 1) & mHeight) == 0);
    bool wPow2 = width <= 0.0 || (((width - 1) & width) == 0);
    return wPow2 && hPow2;
}

#ifndef HX_NATIVE
// Load is in CubeTex.cpp (cross-unit)

void RndTex::PreLoad(BinStream &bs) {
    int revData;
    bs.ReadEndian(&revData, 4);
    int rev = revData & 0xffff;
    int altRev = (unsigned int)revData >> 0x10;
    if (rev > 11) {
        MILO_WARN(
            "%s can't load new %s version %d > %d",
            PathName(this),
            ClassName(),
            rev,
            (unsigned short)11
        );
    }
    if (altRev > 0) {
        MILO_FAIL(
            "%s can't load new %s alt version %d > %d",
            PathName(this),
            ClassName(),
            altRev,
            (unsigned short)0
        );
    }
    if (rev > 8) {
        Hmx::Object::Load(bs);
    }
    if (rev == 1) {
        short w, h;
        bs.ReadEndian(&w, 2);
        bs.ReadEndian(&h, 2);
        mWidth = w;
        mHeight = h;
    } else {
        bs >> mWidth >> mHeight;
    }
    bs >> mBpp;
    bs >> mFilepath;
    if (rev > 9 && !bs.Cached()) {
        mLoader = new FileLoader(
            mFilepath,
            CacheResource(mFilepath.c_str(), this),
            kLoadFront,
            0,
            false,
            true,
            nullptr,
            nullptr
        );
    }
    bs.PushRev(packRevs(altRev, rev), this);
}

void RndTex::PostLoad(BinStream &bs) {
    BinStreamRev d(bs, bs.PopRev(this));
    int rev = d.rev;

    // Old cubemap mask handling (pre-rev 5)
    if (rev < 5) {
        int cubemapMask;
        d >> cubemapMask;
        if (cubemapMask != 0 && !mFilepath.empty()) {
            if (cubemapMask & 1)
                MILO_WARN("%s: kTransparentWhite no longer supported", Name());
            else if (cubemapMask & 2) {
                mFilepath.insert(mFilepath.find('.'), "_tb");
            } else if (cubemapMask & 0x10) {
                mFilepath.insert(mFilepath.find('.'), "_ga");
            } else if (cubemapMask & 0x20) {
                mFilepath.insert(mFilepath.find('.'), "_gw");
            } else if (cubemapMask & 0x40) {
                MILO_WARN("%s: kCubeMap no longer supported", Name());
            }
        }
    }
    // Legacy bool (rev 1-2)
    if (rev != 0 && rev < 3) {
        bool b;
        d >> b;
    }
    // MipMapK
    if (rev > 7) {
        d >> mMipMapK;
    } else if (rev > 3) {
        int mipK;
        d >> mipK;
        mMipMapK = mipK / 16.0f;
    }
    // Type
    if (rev > 6) {
        d >> (int &)mType;
    } else if (rev > 5) {
        Type types[5] = { kRegular, kRendered, kMovie, kBackBuffer, kFrontBuffer };
        int idx;
        d >> idx;
        mType = types[idx];
    } else if (rev > 4) {
        bool wasRendered;
        d >> wasRendered;
        mType = wasRendered ? kRendered : kRegular;
    }
    // Skip extra bool (rev 7 era)
    if (rev > 7) {
        bool b;
        d >> b;
    }
    // OptimizeForPS3 (rev 10+)
    if (rev > 10) {
        bool b;
        d >> b;
        mOptimizeForPS3 = b;
    }

    if (bs.Cached()) {
        PresyncBitmap();
        if (UseBottomMip()) {
            RndBitmap tempBitmap;
            tempBitmap.Load(bs);
            CopyBottomMip(mBitmap, tempBitmap);
        } else {
            mBitmap.Load(bs);
        }
        if (mBitmap.Width() == 0 && mType == kRegular) {
            MILO_LOG(
                "Bitmap %s, does not have name set, it will not be cached!\n",
                mFilepath.c_str()
            );
        }
        mNumMips = mBitmap.NumMips();
        SyncBitmap();
    } else if (mFilepath.empty() || mType != kRegular) {
        SetBitmap(mWidth, mHeight, mBpp, mType, false, nullptr);
    } else if (TheLoadMgr.GetPlatform() != kPlatformNone) {
        SetBitmap(mLoader);
        mLoader = nullptr;
    } else {
        RELEASE(mLoader);
    }
}
#endif // !HX_NATIVE

const char *
RndTex::CheckSize(int width, int height, int bpp, int numMips, Type ty, bool file) {
    const char *ret = nullptr;
    if (ty == kDepthVolumeMap || ty == kDensityMap || (ty & kDeviceTexture)
        || (ty & kRegularLinear))
        return nullptr;
    ret = CheckDim(width, ty, file);
    if (!ret)
        ret = CheckDim(height, ty, file);
    if (!ret && bpp != 4 && bpp != 8 && bpp != 16 && bpp != 24 && bpp != 32) {
        ret = "%s: invalid bpp";
    }
    int sizeBytes = (width * height * bpp) >> 3;
    if (GetGfxMode() == 0) {
        if (!ret && sizeBytes > 0x7FFF0) {
            ret = "%s: size over 524,272 bytes";
        }
        if (!ret && (sizeBytes & 0xf)) {
            ret = "%s: size not multiple of 16 bytes";
        }
        if (!ret && numMips > 6) {
            ret = "%s: more than 6 mip levels";
        }
    }
    return ret;
}

DataNode RndTex::OnSetBitmap(const DataArray *arr) {
    if (arr->Size() == 3) {
        FilePath path(arr->Str(2));
        SetBitmap(path);
    } else {
        SetBitmap(
            arr->Int(2),
            arr->Int(3),
            arr->Int(4),
            (Type)arr->Int(5),
            arr->Int(6),
            nullptr
        );
    }
    return 0;
}

DataNode RndTex::OnSetRendered(const DataArray *a) {
    MILO_ASSERT(IsRenderTarget(), 0x3BB);
    SetBitmap(mWidth, mHeight, mBpp, mType, mNumMips > 0, nullptr);
    return 0;
}
