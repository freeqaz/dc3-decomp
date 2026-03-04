#include "rndobj/Text.h"
#include "Text.h"
#include "obj/Object.h"
#include "os/Debug.h"
#include "os/System.h"
#include "rndobj/Draw.h"
#include "rndobj/Font.h"
#include "rndobj/FontBase.h"
#include "rndobj/Mat.h"
#include "rndobj/Mesh.h"
#include "rndobj/Trans.h"
#include "rndobj/Cam.h"
#include "rndobj/Rnd.h"
#include "math/Trig.h"
#include "utl/BinStream.h"
#include "utl/MemMgr.h"
#include "utl/UTF8.h"
#include "wordwrap.h"
#include "ui/UI.h"

std::vector<RndText::BlacklightPacket> RndText::sBlacklightPacketPool;
int RndText::sBlacklightPacketCount;
bool RndText::sBlacklightModeEnabled;
std::list<RndText::FontMapBase *> RndText::sFontMapCache;
int TEXT_REV = 0;
float gSuperscriptScale = 0.7f;
float gGuitarScale = 0.7f;
float gGuitarZOffset = 0.2f;

float SegmentLength(
    int start, int end, const float *widths, const unsigned short *chars, float scale
) {
    while (chars[start] == ' ' && start < end)
        start++;
    while (chars[end - 1] == ' ' && start < end)
        end--;
    return (widths[end] - widths[start]) * scale;
}

Transform XfmOnCircleEdge(float circumference, float pos) {
    Transform xfm;
    float sign = circumference >= 0.0f ? 1.0f : -1.0f;

    xfm.m.z.Set(0.0f, 0.0f, 1.0f);

    float offset = sign * -1.5707964f;
    float angle = (pos / circumference) * 6.2831855f + offset;

    float cosA = Cosine(angle);
    float sinA = Sine(angle);

    xfm.v.Set(cosA, sinA, 0.0f);

    float negSign = -sign;
    xfm.m.y.y = sinA * negSign;
    xfm.m.y.x = cosA * negSign;
    xfm.m.y.z = 0.0f * negSign;

    xfm.m.x.z = -(xfm.m.y.x * xfm.m.z.y - xfm.m.z.x * xfm.m.y.y);
    xfm.m.x.x = xfm.m.z.z * xfm.m.y.y - xfm.m.y.z * xfm.m.z.y;
    xfm.m.x.y = xfm.m.y.z * xfm.m.z.x - xfm.m.z.z * xfm.m.y.x;

    float radius = (sign * (circumference * 0.15915494f));
    xfm.v.y *= radius;
    xfm.v.x *= radius;
    xfm.v.z *= radius;

    return xfm;
}

bool CalcScreenHeight(float size, RndMesh *mesh, float &heightOut) {
    if (!mesh->Showing())
        return false;

    const Transform &worldXfm = mesh->WorldXfm();
    RndCam *cam = RndCam::Current();

    Vector3 pts[2];
    pts[0].Set(0.0f, 0.0f, size * -0.5f);
    pts[1].Set(0.0f, 0.0f, size * 0.5f);

    Vector2 screens[2];
    for (int i = 0; i < 2; i++) {
        Vector3 world;
        Multiply(pts[i], worldXfm, world);
        cam->WorldToScreen(world, screens[i]);
    }

    float dx = (float)TheRnd.Width() * (screens[0].x - screens[1].x);
    float dy = (float)TheRnd.Height() * (screens[0].y - screens[1].y);
    heightOut = std::sqrt(dx * dx + dy * dy);
    return true;
}

RndText::RndText()
    : mWidth(0), mHeight(0), mCircle(0), mAlignment(kMiddleCenter), mFitType(kFitWrap),
      mCapsMode(kCapsModeNone), mLeading(1), mFixedLength(0), mMarkup(true),
      mBasicMarkup(true), mScrollDelay(0), mScrollRate(1), mScrollPause(0), mWrapEnabled(0),
      mLineHeight(0), mTotalHeight(0), mTotalWidth(0), mIndentation(0), mAltStyle(nullptr), mZeroAlphaTime(0), mDirtyFlags(-1),
      mLastSyncFlags(-1), mStyles(this), mBoundsLeft(0), mBoundsTop(0), mBoundsRight(0), mBoundsBottom(0), mCurScrollChars(0),
      mScrollSpeed(0) {
    mStyles.resize(1);
    mFontMaps.reserve(1);
}

RndText::~RndText() {
    FOREACH (it, mFontMaps) {
        delete *it;
    }
}

BEGIN_HANDLERS(RndText)
    HANDLE_EXPR(get_text_size, GetTextSize())
    HANDLE_ACTION(update_text, UpdateText())
    HANDLE_SUPERCLASS(RndDrawable)
    HANDLE_SUPERCLASS(RndTransformable)
    HANDLE_SUPERCLASS(Hmx::Object)
END_HANDLERS

BEGIN_CUSTOM_PROPSYNC(RndText::Style)
    SYNC_PROP(font, o.mFont)
    SYNC_PROP(size, o.mSize)
    SYNC_PROP_SET(text_color, o.mTextColor.Pack(), o.mTextColor.Unpack(_val.Int()))
    SYNC_PROP_SET(text_alpha, o.mTextColor.alpha, o.mTextColor.alpha = _val.Float())
    SYNC_PROP(font_color_override, o.mFontColorOverride)
    SYNC_PROP_SET(font_color, o.mFontColor.Pack(), o.mFontColor.Unpack(_val.Int()))
    SYNC_PROP_SET(font_alpha, o.mFontColor.alpha, o.mFontColor.alpha = _val.Float())
    SYNC_PROP(italics, o.mItalics)
    SYNC_PROP(kerning, o.mKerning)
    SYNC_PROP(z_offset, o.mZOffset)
    SYNC_PROP(blacklight, o.mBlacklight)
END_CUSTOM_PROPSYNC

BEGIN_PROPSYNCS(RndText)
    SYNC_PROP_SET(text, TextASCII(), SetTextASCII(_val.Str()))
    SYNC_PROP_SET(fixed_length, mFixedLength, SetFixedLength(_val.Int()))
    SYNC_PROP(align, (int &)mAlignment)
    SYNC_PROP(caps_mode, (int &)mCapsMode)
    SYNC_PROP(width, mWidth)
    SYNC_PROP(height, mHeight)
    SYNC_PROP(circle, mCircle)
    SYNC_PROP(fit_type, (int &)mFitType)
    SYNC_PROP(leading, mLeading)
    SYNC_PROP(indentation, mIndentation)
    SYNC_PROP(basic_markup, mBasicMarkup)
    SYNC_PROP(markup, mMarkup)
    SYNC_PROP(scroll_delay, mScrollDelay)
    SYNC_PROP(scroll_rate, mScrollRate)
    SYNC_PROP(scroll_pause, mScrollPause)
    SYNC_PROP(styles, mStyles)
    SYNC_SUPERCLASS(RndDrawable)
    SYNC_SUPERCLASS(RndTransformable)
    SYNC_SUPERCLASS(Hmx::Object)
END_PROPSYNCS

RndText::Style::Style(Hmx::Object *owner)
    : mSize(30), mTextColor(1, 1, 1), mFontColorOverride(false), mFontColor(1, 1, 1),
      mItalics(0), mKerning(0), mZOffset(0), mFont(owner), mBlacklight(false) {}

RndText::Style::Style(const Style &s)
    : mFont((memcpy(this, &s, 0x34), s.mFont)) {
    mBlacklight = s.mBlacklight;
}

RndText::StyleState::StyleState(RndText *text, float size) {
    memcpy(this, &text->mStyles[0], 0x34);
    mStyle = &text->mStyles[0];
    mFontMapIdx = text->FontMapIndex(mStyle->mFont, mStyle->mBlacklight);
    mBaseSize = size;
    mSize *= size;
    mActive = true;
}

BinStream &operator<<(BinStream &bs, const RndText::Style &s) {
    bs << s.mFont;
    bs << s.mSize;
    bs << s.mTextColor;
    bs << s.mFontColorOverride;
    bs << s.mFontColor;
    bs << s.mItalics;
    bs << s.mKerning;
    bs << s.mZOffset;
    bs << s.mBlacklight;
    return bs;
}

BEGIN_SAVES(RndText)
    SAVE_REVS(0x1C, 1)
    SAVE_SUPERCLASS(Hmx::Object)
    SAVE_SUPERCLASS(RndDrawable)
    SAVE_SUPERCLASS(RndTransformable)
    bs << mAlignment;
    bs << mText;
    bs << mWidth;
    bs << mLeading;
    bs << mFixedLength;
    bs << mMarkup;
    bs << mCapsMode;
    bs << mHeight;
    bs << mCircle;
    bs << mFitType;
    bs << mStyles;
    bs << mScrollDelay;
    bs << mScrollRate;
    bs << mScrollPause;
    bs << mIndentation;
    bs << mBasicMarkup;
END_SAVES

BEGIN_COPYS(RndText)
    COPY_SUPERCLASS(Hmx::Object)
    COPY_SUPERCLASS(RndDrawable)
    COPY_SUPERCLASS(RndTransformable)
    if (ty != kCopyFromMax) {
        CREATE_COPY(RndText)
        BEGIN_COPYING_MEMBERS
            COPY_MEMBER(mAlignment)
            COPY_MEMBER(mCapsMode)
            COPY_MEMBER(mFitType)
            COPY_MEMBER(mWidth)
            COPY_MEMBER(mHeight)
            COPY_MEMBER(mCircle)
            COPY_MEMBER(mLeading)
            COPY_MEMBER(mMarkup)
            SetFixedLength(c->mFixedLength);
            SetText(c->mText.c_str());
            COPY_MEMBER(mStyles)
            COPY_MEMBER(mScrollDelay)
            COPY_MEMBER(mScrollRate)
            COPY_MEMBER(mScrollPause)
            COPY_MEMBER(mIndentation)
        END_COPYING_MEMBERS
        UpdateText();
    }
END_COPYS

BinStream &operator>>(BinStream &bs, RndText::Style &s) {
    bs >> s.mFont;
    bs >> s.mSize;
    bs >> s.mTextColor;
    bs >> s.mFontColorOverride;
    bs >> s.mFontColor;
    bs >> s.mItalics;
    bs >> s.mKerning;
    bs >> s.mZOffset;
    if (TEXT_REV >= 0x19) {
        bs >> s.mBlacklight;
    }
    return bs;
}

INIT_REVS(0x1C, 1)

BEGIN_LOADS(RndText)
    LOAD_REVS(bs)
    ASSERT_REVS(0x1C, 1)
    Style style(this);
    TEXT_REV = d.rev;
    if (d.rev > 0xF) {
        Hmx::Object::Load(bs);
    }
    RndDrawable::Load(bs);
    if (d.rev < 7) {
        ObjPtrList<Hmx::Object> objects(this);
        int x;
        bs >> x;
        bs >> objects;
    }
    if (d.rev > 1) {
        RndTransformable::Load(bs);
    }
    if (d.rev < 0x16) {
        bs >> style.mFont;
    }
    if (d.rev < 3) {
        int idx;
        bs >> idx;
        Alignment align_choices[6] = { kTopLeft,    kTopCenter,    kTopRight,
                                       kBottomLeft, kBottomCenter, kBottomRight };
        mAlignment = align_choices[idx];
    } else {
        bs >> (int &)mAlignment;
    }
    if (d.rev < 2) {
        Vector2 v2;
        bs >> v2;
        SetLocalPos(Vector3(v2.x, 0, -v2.y * 0.75f));
    }
    bs >> mText;
    if (d.rev < 0x14) {
        std::vector<unsigned short> vec;
        ASCIItoWideVector(vec, mText.c_str());
        WideVectorToUTF8(vec, mText);
    }
    if (d.rev > 0 && d.rev < 0x16) {
        bs >> style.mTextColor;
    }
    if (d.rev > 0xC) {
        bs >> mWidth;
    } else if (d.rev > 3) {
        bool b;
        d >> b;
        bs >> mWidth;
        if (!b)
            mWidth = 0.0f;
        if (d.rev < 5 && (mWidth < 0.0f || mWidth > 1000.0f))
            mWidth = 0.0f;
    }
    if (d.rev == 5) {
        String str;
        bs >> str;
    }
    if (d.rev > 4 && d.rev < 11) {
        bool b;
        d >> b;
        if (style.mFont) {
            RndFont *oldfont2d = dynamic_cast<RndFont *>(style.mFont.Ptr());
            MILO_ASSERT(oldfont2d, 0xBC1);
            if (oldfont2d->NumMats() > 0 && oldfont2d->Mat(0)) {
                int zMode = !mMarkup ? 2 : 0;
                oldfont2d->Mat(0)->SetZMode((ZMode)zMode);
            }
        }
    }
    if (d.rev > 7) {
        bs >> mLeading;
    }
    if (d.rev > 0xB) {
        int len;
        bs >> len;
        SetFixedLength(len);
    } else if (d.rev > 8) {
        bool b;
        d >> b;
        if (b) {
            SetFixedLength(mText.length());
        } else if (mFixedLength != 0) {
            mFixedLength = 0;
        }
    }
    if (d.rev > 9 && d.rev < 0x16) {
        bs >> style.mItalics;
    }
    if (d.rev < 0x16) {
        if (d.rev > 0xB) {
            bs >> style.mSize;
        } else if (style.mFont) {
            RndFont *oldfont2d = dynamic_cast<RndFont *>(style.mFont.Ptr());
            MILO_ASSERT(oldfont2d, 0xBE9);
            style.mSize = oldfont2d->DeprecatedSize();
        }
        if (d.rev < 0xD) {
            style.mItalics /= style.mSize;
        }
    }
    if (d.rev > 0xD) {
        LOAD_BITFIELD(bool, mMarkup)
    }
    if (d.rev > 0xE) {
        bs >> (int &)mCapsMode;
    } else {
        mCapsMode = kCapsModeNone;
    }
    if (d.rev > 0xF) {
        bs >> mHeight;
        bs >> mCircle;
        bs >> (int &)mFitType;
    }
    if (d.rev >= 0x12 && d.rev < 0x15) {
        bool b;
        d >> b;
    }
    if (d.rev >= 0x13 && d.rev < 0x15) {
        int i, j, k;
        bs >> i;
        bs >> j;
        bs >> k;
    }
    if (d.rev >= 0x16) {
        if (d.rev == 0x17) {
            TheDebug.Notify(
                MakeString("%s was bad version 23, suggest resave", PathName(this))
            );
        }
        // mFitType already read in the d.rev > 0xF block above
        if (d.rev < 0x18) {
            String str;
            bs >> str;
        }
        // altRev > 0: original binary doesn't write mDirtyFlags/mLastSyncFlags
        // (confirmed: Save writes altRev=1 but no corresponding data)
        d >> mStyles;
    } else {
        mStyles.resize(1);
        memcpy(&mStyles[0], &style, 0x34);
        mStyles[0].mFont = style.mFont;
    }
    if (d.rev >= 0x1A) {
        bs >> mScrollDelay;
        bs >> mScrollRate;
        bs >> mScrollPause;
    }
    if (d.rev >= 0x1B) {
        bs >> mIndentation;
    }
    if (d.rev >= 0x1C) {
        d >> mBasicMarkup;
    }
    UpdateText();
END_LOADS

void RndText::UpdateSphere() {
    Sphere s;
    s.Zero();
    FOREACH (it, mFontMaps) {
        for (int i = 0; i < (*it)->NumMeshes(); i++) {
            RndMesh *mesh = (*it)->Mesh(i);
            if (mesh) {
                mesh->UpdateSphere();
                s.GrowToContain(mesh->GetSphere());
            }
        }
    }
    SetSphere(s);
}

void RndText::Mats(std::list<class RndMat *> &mats, bool) {
    FOREACH (it, mFontMaps) {
        for (int i = 0; i < (*it)->NumMaterials(); i++) {
            RndMat *mat = (*it)->Material(i);
            if (mat) {
                mats.push_back(mat);
            }
        }
    }
}

RndDrawable *RndText::CollideShowing(const Segment &s, float &f, Plane &p) {
    FOREACH (it, mFontMaps) {
        for (int i = 0; i < (*it)->NumMeshes(); i++) {
            RndMesh *mesh = (*it)->Mesh(i);
            if (mesh && mesh->CollideShowing(s, f, p)) {
                return this;
            }
        }
    }
    return nullptr;
}

int RndText::CollidePlane(const Plane &p) {
    int ret = 0;
    FOREACH (it, mFontMaps) {
        for (int i = 0; i < (*it)->NumMeshes(); i++) {
            RndMesh *mesh = (*it)->Mesh(i);
            if (mesh) {
                int meshCol = mesh->CollidePlane(p);
                if (meshCol == 0) {
                    return 0;
                }
                if (meshCol > 0) {
                    if (ret < 0) {
                        return 0;
                    } else {
                        ret = meshCol;
                    }
                } else if (ret > 0) {
                    return 0;
                } else {
                    ret = meshCol;
                }
            }
        }
    }
    return ret;
}

float RndText::GetDistanceToPlane(const Plane &p, Vector3 &v) {
    if (mFontMaps.empty())
        return 0;
    float ret = 0;
    bool first = true;
    FOREACH (it, mFontMaps) {
        for (int i = 0; i < (*it)->NumMeshes(); i++) {
            RndMesh *mesh = (*it)->Mesh(i);
            if (mesh) {
                Vector3 vec;
                float dist = mesh->GetDistanceToPlane(p, vec);
                if (first || std::fabs(dist) < std::fabs(ret)) {
                    first = false;
                    v = vec;
                    ret = dist;
                }
            }
        }
    }
    return ret;
}

bool RndText::MakeWorldSphere(Sphere &s, bool b) {
    s.Zero();
    FOREACH (it, mFontMaps) {
        for (int i = 0; i < (*it)->NumMeshes(); i++) {
            RndMesh *mesh = (*it)->Mesh(i);
            if (mesh) {
                Sphere localSphere;
                if (b) {
                    mesh->MakeWorldSphere(localSphere, true);
                } else {
                    if (mesh->GetSphere().GetRadius() != 0.0f) {
                        Multiply(mesh->GetSphere(), mesh->WorldXfm(), localSphere);
                    }
                }
                s.GrowToContain(localSphere);
            }
        }
    }
    return s.GetRadius() != 0.0f;
}

void RndText::Init() {
    REGISTER_OBJ_FACTORY(RndText)
    SystemConfig("rnd")->FindData("text_superscript_scale", gSuperscriptScale, false);
    SystemConfig("rnd")->FindData("text_guitar_scale", gGuitarScale, false);
    SystemConfig("rnd")->FindData("text_guitar_z_offset", gGuitarZOffset, false);
    unsigned int ui = 1;
    static Symbol kor("kor");
    if (SystemLanguage() == kor)
        ui = 5;
    WordWrap_SetOption(ui);
}

RndText::FontMap::~FontMap() {
    while (mPages.size() != 0) {
        delete mPages.back();
        mPages.pop_back();
    }
}

void RndText::FontMap::SetFont(RndFontBase *f) {
    MILO_ASSERT(f->ClassName() == RndFont::StaticClassName(), 0x75);
    mFont = static_cast<RndFont *>(f);
    while (mPages.size() > mFont->NumMats()) {
        delete mPages.back();
        mPages.pop_back();
    }
    mPages.reserve(mFont->NumMats());
    while (mPages.size() < mFont->NumMats()) {
        mPages.push_back(new Page());
    }
}

void RndText::FontMap::ResetDisplayableChars() {
    for (int i = 0; i < mPages.size(); i++) {
        mPages[i]->displayableChars = 0;
    }
}

void RndText::FontMap::IncrementDisplayableChars(unsigned short num) {
    int page = mFont->CharPage(num);
    if (page >= 0) {
        mPages[page]->displayableChars++;
    }
}

void ResetFontMapPageMeshFaces(RndMesh *mesh, int numFaces) {
    MILO_ASSERT(mesh, 0x96);
    mesh->Faces().resize(numFaces);
    std::vector<RndMesh::Face>::iterator it = mesh->Faces().begin();
    std::vector<RndMesh::Face>::iterator itEnd = mesh->Faces().end();
    int num = 0;
    for (; it != itEnd; it += 2, num += 4) {
        it[0].Set(num, num + 1, num + 2);
        it[1].Set(num, num + 2, num + 3);
    }
}

void RndText::FontMap::AllocateMeshes(RndText *text, int fixedLength) {
    for (int i = 0; i < mPages.size(); i++) {
        Page &page = *(mPages[i]);
        if (!page.mesh && mFont && page.displayableChars > 0) {
            page.mesh = Hmx::Object::New<RndMesh>();
        }
        RndMesh *mesh = page.mesh;
        page.mSyncFlags = 0x1F;
        page.mVertStart = 0;
        if (mesh) {
            mesh->SetTransParent(text, false);
            mesh->SetTransConstraint(
                RndTransformable::kConstraintParentWorld, nullptr, false
            );
            if (mFont) {
                auto fontMat = mFont->Mat(i);
                mesh->SetMat(fontMat);
            }
            mesh->SetShowing(page.displayableChars > 0);
            if ((unsigned int)fixedLength == 0) {
                mesh->SetMutable(0);
                ResetFontMapPageMeshFaces(mesh, page.displayableChars * 2);
                page.mSyncFlags |= 0xA0;
                mesh->Verts().resize(page.displayableChars * 4);
            } else if (mesh->Mutable() == 0 || mesh->Verts().size() != fixedLength * 4) {
                mesh->SetMutable(0x1F);
                ResetFontMapPageMeshFaces(mesh, page.displayableChars * 2);
                page.mSyncFlags |= 0xA0;
                mesh->Verts().resize(page.displayableChars * 4);
            }
            MILO_ASSERT(mesh->Verts().size() >= page.displayableChars * 4, 0xD2);
#ifdef HX_NATIVE
            page.mVertStart = mesh->Verts().begin();
#endif
        }
        MILO_ASSERT(!fixedLength || (page.displayableChars <= fixedLength), 0xD5);
    }
}

void RndText::FontMap::CleanupSyncMeshes() {
    for (int i = 0; i < mPages.size(); i++) {
        Page &page = *(mPages[i]);
        RndMesh *mesh = page.mesh;
        if (mesh) {
            while (page.mVertStart != mesh->Verts().end()) {
                RndMesh::Vert *old = page.mVertStart++;
                old->pos.x = 0.0f;
                old->pos.y = 0.0f;
                old->pos.z = 0.0f;
            }
            mesh->Sync(page.mSyncFlags);
        }
    }
}

void RndText::FontMap::SetupScrolling() {
    for (int i = 0; i < NumMeshes(); i++) {
        RndMesh *mesh = Mesh(i);
        if (mesh) {
            mesh->SetTransConstraint(RndTransformable::kConstraintNone, nullptr, false);
        }
    }
}

void RndText::FontMap::UpdateScrolling(float f1) {
    for (int i = 0; i < NumMeshes(); i++) {
        RndMesh *mesh = Mesh(i);
        if (mesh) {
#ifdef HX_NATIVE
            // Friend access to RndTransformable members via RndText friendship
            mesh->mLocalXfm.v.x = f1;
            if (!mesh->mDirty) {
                mesh->SetDirty_Force();
            }
#else
            Hmx::Quat q = *(Hmx::Quat *)((char *)mesh + 0x78);
            q.x = f1;
            *(Hmx::Quat *)((char *)mesh + 0x78) = q;
            if (!*(bool *)((char *)mesh + 0xfd)) {
                mesh->SetDirty_Force();
            }
#endif
        }
    }
}

RndText::FontMap3d::~FontMap3d() {
    for (int i = 0; i < mMeshes.size(); i++) {
        if (mMeshes[i]) {
            delete mMeshes[i];
        }
    }
}

void RndText::FontMap3d::SetFont(RndFontBase *f) {
    MILO_ASSERT(f->ClassName() == RndFont3d::StaticClassName(), 0x17D);
    mFont = static_cast<RndFont3d *>(f);
}

void RndText::SetFixedLength(int len) {
    if (mFixedLength != len) {
        mFixedLength = len;
        if (mFixedLength != 0) {
            const char *p = mText.c_str();
            int newLen;
            for (newLen = 0; *p != '\0' && newLen < mFixedLength; newLen++) {
                unsigned short us;
                p += DecodeUTF8(us, p);
            }
            mText.resize((intptr_t)p + mFixedLength - newLen - (intptr_t)mText.c_str());
        }
    }
}

void RndText::DoBasicMarkup() {
    while (mText.contains("\\q")) {
        mText.replace(mText.find("\\q"), 2, "\"");
    }
}

int RndText::FontMapIndex(RndFontBase *f, bool b) {
    for (int i = 0; i < mFontMaps.size(); i++) {
        if (mFontMaps[i]->Font() == f && mFontMaps[i]->mBlacklight == b) {
            return i;
        }
    }
    return -1;
}

float RndText::ComputeHeight(int i1, float f2, float &f3) {
    float f1;
    if (mStyles[0].mFont) {
        f1 = mStyles[0].mFont->AspectRatio() * mStyles[0].mSize * f2;
    } else {
        f1 = 0;
    }
    f3 = mLeading * f1;
    return ((i1 - 1) * mLeading + 1.0f) * f1;
}

void RndText::SetText(const char *str) {
    if (mFixedLength != 0) {
        MILO_ASSERT(mText.capacity() >= mFixedLength, 0x75E);
        const char *p = str;
        for (int newLen = 0; *p != '\0' && newLen < mFixedLength; newLen++) {
            unsigned short us;
            p += DecodeUTF8(us, p);
        }
        int newLen = p - str;
        if (mText.capacity() < newLen) {
            mText.resize(newLen);
        }
        strncpy((char *)mText.c_str(), str, newLen);
        char *last = (char *)mText.c_str() + newLen;
        *last = '\0';
    } else {
        mText = str;
    }
    if (mBasicMarkup) {
        DoBasicMarkup();
    }
}

String RndText::TextASCII() const {
    String str;
    {
        MemTemp tmp;
        str.resize(UTF8StrLen(mText.c_str()) + 1);
    }
    UTF8toASCIIs((char *)str.c_str(), str.capacity(), mText.c_str(), '*');
    return str;
}

void RndText::BuildFontMaps(bool b1) {
    if (b1) {
        for (auto it = mFontMaps.begin(); it != mFontMaps.end();
             it = mFontMaps.erase(it)) {
            sFontMapCache.push_back(*it);
        }
    }
    if (mFontMaps.empty()) {
        for (int i = 0; i < mStyles.size(); i++) {
            RndFontBase *font = mStyles[i].mFont;
            if (font) {
                if (FontMapIndex(font, mStyles[i].mBlacklight) == -1) {
                    FontMapBase *map = AcquireFontMap(font);
                    map->mBlacklight = mStyles[i].mBlacklight;
                    mFontMaps.push_back(map);
                }
            }
        }
    }
}

void RndText::SetTextASCII(const char *cstr) {
    String str;
    {
        MemTemp tmp;
        std::vector<unsigned short> vec;
        ASCIItoWideVector(vec, cstr);
        WideVectorToUTF8(vec, str);
    }
    SetText(str.c_str());
}

void RndText::QueueBlacklightPacket(RndMesh *mesh, float f2, int i3) {
    u32 cursize = sBlacklightPacketPool.capacity();
    if ((u32)sBlacklightPacketCount >= cursize) {
        int newsize = 8;
        if (cursize != 0) {
            newsize = cursize * 2;
        }
        BlacklightPacket packet;
        sBlacklightPacketPool.resize(newsize, packet);
    }
#ifdef HX_NATIVE
    int idx = sBlacklightPacketCount++;
    BlacklightPacket &pkt = sBlacklightPacketPool[idx];
    pkt.mMesh = mesh;
    RndMat *mat = mesh->Mat();
    if (mat) {
        pkt.mSavedColor = mat->GetColor();
    }
    pkt.mSize = f2;
    pkt.mSyncFlags = i3;
    pkt.mCam = RndCam::Current();
#else
    int idx = sBlacklightPacketCount++;
    int *pkt_ptr = (int *)&sBlacklightPacketPool[0] + (idx << 3);
    pkt_ptr[0] = (int)mesh;
    int *mat = *(int **)((char *)mesh + 0x128);
    pkt_ptr[1] = *(int *)((char *)mat + 0x2C);
    pkt_ptr[2] = *(int *)((char *)mat + 0x30);
    pkt_ptr[3] = *(int *)((char *)mat + 0x34);
    pkt_ptr[4] = *(int *)((char *)mat + 0x38);
    *(float *)(pkt_ptr + 5) = f2;
    pkt_ptr[6] = i3;
    pkt_ptr[7] = (int)RndCam::Current();
#endif
}

void RndText::ClearBlacklight() { sBlacklightPacketCount = 0; }

void RndText::DrawBlacklight() {
    RndCam *savedCam = RndCam::Current();
    for (int i = 0; i < sBlacklightPacketCount; i++) {
#ifdef HX_NATIVE
        BlacklightPacket &pkt = sBlacklightPacketPool[i];
        if (pkt.mCam && pkt.mCam != RndCam::Current()) {
            pkt.mCam->Select();
        }
        RndMat *mat = pkt.mMesh->Mat();
        if (mat) {
            Hmx::Color &color = mat->GetColor();
            color.red = pkt.mSavedColor.red;
            color.green = pkt.mSavedColor.green;
            color.blue = pkt.mSavedColor.blue;
            mat->MarkDirty(1);
        }
        DrawMesh(pkt.mMesh, pkt.mSize, pkt.mSyncFlags);
#else
        int *pkt = (int *)((char *)&sBlacklightPacketPool[0] + i * 0x20);
        RndCam *cam = (RndCam *)pkt[7];
        if (cam != 0 && cam != RndCam::Current()) {
            cam->Select();
        }
        float savedB = *(float *)(pkt + 3);
        float savedG = *(float *)(pkt + 2);
        float savedR = *(float *)(pkt + 1);
        int *mat = *(int **)((char *)pkt[0] + 0x128);
        *(float *)((char *)mat + 0x2c) = savedR;
        *(float *)((char *)mat + 0x30) = savedG;
        *(float *)((char *)mat + 0x34) = savedB;
        *(int *)((char *)mat + 0x228) |= 1;
        DrawMesh((RndMesh *)pkt[0], *(float *)(pkt + 5), pkt[6]);
#endif
    }
    if (savedCam != 0 && savedCam != RndCam::Current()) {
        savedCam->Select();
    }
}

void RndText::SizeCheck() {
    // The original checks mDirtyFlags against screen size, font, and text changes.
    // On native we always rebuild — correct but slower than dirty-flag tracking.
#ifdef HX_NATIVE
    UpdateText();
#endif
}

void RndText::UpdateScrollOffsets() {
    // Update scroll mesh positions for scrolling fit types
    if (mFitType < kFitScrollMarqueeWrap || mFitType > kFitScrollMarqueeWrapAlways)
        return;

    FOREACH (it, mFontMaps) {
        if ((*it)->SupportsScrolling()) {
            (*it)->UpdateScrolling(mScrollSpeed);
        }
    }
}

void RndText::FitTextScroll() {
    // Scrolling text layout — sets up mesh scrolling constraints
    if (mFitType < kFitScrollMarqueeWrap)
        return;

    FOREACH (it, mFontMaps) {
        if ((*it)->SupportsScrolling()) {
            (*it)->SetupScrolling();
        }
    }
    mWrapEnabled = true;
}

void RndText::DrawMesh(RndMesh *mesh, float size, int syncFlags) {
    mesh->DrawShowing();
    if (size != 0.0f && syncFlags > 0) {
        float offset = size;
        do {
            Vector3 pos = mesh->LocalXfm().v;
            pos.x += offset;
            mesh->SetLocalPos(pos);
            mesh->DrawShowing();
            pos.x -= offset;
            mesh->SetLocalPos(pos);
            syncFlags--;
            offset += size;
        } while (syncFlags != 0);
    }
}

RndText::FontMapBase *RndText::AcquireFontMap(RndFontBase *font) {
    // Check cache first
    for (auto it = sFontMapCache.begin(); it != sFontMapCache.end(); ++it) {
        FontMapBase *map = *it;
        if (map->Font() == font) {
            sFontMapCache.erase(it);
            return map;
        }
    }
    // Create new FontMap based on font type
    if (font->ClassName() == RndFont3d::StaticClassName()) {
        FontMap3d *map3d = new FontMap3d();
        map3d->SetFont(font);
        return map3d;
    } else {
        FontMap *map = new FontMap();
        map->SetFont(font);
        return map;
    }
}

void RndText::UpdateText() {
    if (mFitType == kFitEllipsis) {
        FitTextJust();
        return;
    }
    if (mStyles[0].mSize > 0.0f && mWidth > 0.0f) {
        if (mFitType == kFitScrollMarqueeWrap) {
        do_ellipsis:
            FitTextEllipsis();
            return;
        }
        if (mFitType == kFitScrollPingPong
            || mFitType == kFitScrollMarqueeReset
            || mFitType == kFitStretch
            || mFitType == kFitScrollMarqueeWrapAlways) {
            for (unsigned int i = 0; i < (unsigned int)mStyles.size(); i++) {
                RndFontBase *font = mStyles[i].mFont;
                const char *fontName;
                if (font == 0) {
                    fontName = "NULL";
                } else if (font->ClassName() != RndFont::StaticClassName()) {
                    fontName = font->Name();
                } else {
                    continue;
                }
                MILO_NOTIFY(
                    "%s %s requests scrolling, but uses a font that does not support it (%s)",
                    PathName(this), ClassName().Str(), fontName
                );
                mFitType = kFitScrollMarqueeWrap;
                goto do_ellipsis;
            }
            FitTextScroll();
            return;
        }
    }
    // Normal wrap path
    {
        HX_VECTOR(Line) lines;
        BuildFontMaps(true);
        HX_VECTOR(unsigned short) wideChars;
        int numChars = ConvertTextToWide(mText.c_str(), wideChars);
        float *charWidths = (float *)_alloca((numChars + 2) * sizeof(float));
        OnComputeCharWidths(&wideChars[0], charWidths, false);
        Hmx::Rect bounds;
        WrapText(&wideChars[0], numChars, charWidths, lines, bounds, 1.0f);
        ConstructMeshes(lines, bounds, 1.0f);
    }
}

void RndText::DrawShowing() {
    SizeCheck();


    // Count total materials across all font maps for VLA allocation
    int totalMats = 0;
    for (auto it = mFontMaps.begin(); it != mFontMaps.end(); ++it) {
        totalMats += (*it)->NumMaterials();
    }

    // Allocate VLA on stack to save material colors (one Hmx::Color per material)
    Hmx::Color *savedColors = (Hmx::Color *)_alloca(totalMats * sizeof(Hmx::Color));

    // Save material colors
    int vlaIdx = 0;
    for (auto it = mFontMaps.begin(); it != mFontMaps.end(); ++it) {
        FontMapBase *fontMap = *it;
        for (int i = 0; i < fontMap->NumMaterials(); i++) {
            RndMat *mat = fontMap->Material(i);
#ifdef HX_NATIVE
            savedColors[vlaIdx] = mat->GetColor();
#else
            int *src = (int *)((char *)mat + 0x2c);
            int *dst = (int *)&savedColors[vlaIdx];
            dst[0] = src[0];
            dst[1] = src[1];
            dst[2] = src[2];
            dst[3] = src[3];
#endif
            vlaIdx++;
        }
    }

    // Apply font color overrides from styles
    bool hasOverride = false;
    auto stylesEnd = mStyles.end();
    for (auto it = mStyles.begin(); it != stylesEnd; ++it) {
        Style &style = *it;
        if (style.mFont && style.mFontColorOverride) {
            int fmIdx = FontMapIndex(style.mFont, style.mBlacklight);
            if (fmIdx != -1) {
                hasOverride = true;
                FontMapBase *fontMap = mFontMaps[fmIdx];
                int numMats = fontMap->NumMaterials();
                for (int i = 0; i < numMats; i++) {
                    RndMat *mat = fontMap->Material(i);
#ifdef HX_NATIVE
                    mat->GetColor() = style.mFontColor;
                    mat->MarkDirty(1);
#else
                    int *dst = (int *)((char *)mat + 0x2c);
                    int *src = (int *)&style.mFontColor;
                    dst[0] = src[0];
                    dst[1] = src[1];
                    dst[2] = src[2];
                    dst[3] = src[3];
                    *(int *)((char *)mat + 0x228) |= 1;
#endif
                }
            }
        }
    }

    // Update scroll offsets if wrapping is enabled
    if (mWrapEnabled) {
        UpdateScrollOffsets();
    }

    // Draw each mesh
    for (auto it = mFontMaps.begin(); it != mFontMaps.end(); ++it) {
        FontMapBase *fontMap = *it;
        int numMeshes = fontMap->NumMeshes();
        for (int i = 0; i < numMeshes; i++) {
            RndMesh *mesh = fontMap->Mesh(i);
            if (mesh) {
                auto blacklightDisabled = TheUI->DisableScreenBlacklight();
                if (!sBlacklightModeEnabled || !fontMap->mBlacklight ||
                    blacklightDisabled) {
                    DrawMesh(mesh, mStyles[0].mSize, 0);
                } else {
                    QueueBlacklightPacket(mesh, mStyles[0].mSize, 0);
                }
            }
        }
    }

    // Restore material colors (r, g, b only — not alpha)
    if (hasOverride) {
        vlaIdx = 0;
        auto fontMapsEnd = mFontMaps.end();
        for (auto it = mFontMaps.begin(); it != fontMapsEnd; ++it) {
            FontMapBase *fontMap = *it;
            auto numMaterials = fontMap->NumMaterials();
            for (int i = 0; i < numMaterials; i++) {
                RndMat *mat = fontMap->Material(i);
#ifdef HX_NATIVE
                Hmx::Color &color = mat->GetColor();
                color.red = savedColors[vlaIdx].red;
                color.green = savedColors[vlaIdx].green;
                color.blue = savedColors[vlaIdx].blue;
                mat->MarkDirty(1);
#else
                int *src = (int *)&savedColors[vlaIdx];
                int *dst = (int *)((char *)mat + 0x2c);
                dst[0] = src[0];
                dst[1] = src[1];
                dst[2] = src[2];
                *(int *)((char *)mat + 0x228) |= 1;
#endif
                vlaIdx++;
            }
        }
    }
}

void RndText::GetWidthHeightBox(Box &box) const {
    if (mAlignment & 1) {
        box.mMin.x = 0;
    } else if (mAlignment & 2) {
        box.mMin.x = mWidth * -0.5f;
    } else {
        box.mMin.x = -mWidth;
    }

    if (mAlignment & 0x10) {
        box.mMin.z = -mHeight;
    } else if (mAlignment & 0x20) {
        box.mMin.z = mHeight * -0.5f;
    } else {
        box.mMin.z = 0;
    }

    box.mMax.x = mWidth + box.mMin.x;
    box.mMax.z = mHeight + box.mMin.z;
    box.mMax.y = 0;
    box.mMin.y = 0;
}

void RndText::ReFitTextScroll(String str) {
    if (mFitType != kFitScrollMarqueeWrapAlways) {
    } else {
        SetText(str.c_str());
        FitTextScroll();
        *(float *)&mScrollPos = 0.0f;
        mZeroAlphaTime = 0.0f;
        float width = mWidth;
        while (*mLineWidths.begin() <= width) {
            mDirtyFlags++;
            if (mDirtyFlags >= mTotalWidth) {
                mDirtyFlags = 0;
            }
            if (*mLineWidths.begin() == *(float *)&mNumLines) {
                mZeroAlphaTime += mWidth;
            }
            unsigned int count = 0;
            for (auto it = mLineWidths.begin(); it != mLineWidths.end(); ++it) {
                count++;
            }
            if ((unsigned int)mTotalWidth == count) {
                mLineWidths.insert(mLineWidths.end(), *mLineWidths.begin());
            }
            mLineWidths.erase(mLineWidths.begin());
            width = mWidth - mZeroAlphaTime;
        }
        *(float *)&mScrollState = *(float *)&mScrollOffset;
    }
}

float RndText::ComputeCharWidthsForText(String str) {
    BuildFontMaps(false);
#ifdef HX_NATIVE
    std::vector<unsigned short> wideChars;
#else
    std::vector<unsigned short, std::StlNodeAlloc<unsigned short> > wideChars;
#endif
    int numChars = ConvertTextToWide(str.c_str(), wideChars);
    float *widths = (float *)_alloca((numChars + 2) * sizeof(float));
    OnComputeCharWidths(wideChars.data(), widths, true);
    return widths[numChars];
}

void RndText::FontMap3d::IncrementDisplayableChars(unsigned short us) {
    if (mFont && mFont->CharDefined(us)) {
        mDisplayableChars++;
    }
}

void RndText::FontMap3d::AllocateMeshes(RndText *text, int fixedLength) {
    // Resize the mesh array to match displayable chars
    // Each char gets one mesh from the 3d font
    // For now just ensure we don't have more meshes than chars
    while ((int)mMeshes.size() > mDisplayableChars) {
        if (mMeshes.back()) {
            delete mMeshes.back();
        }
        mMeshes.pop_back();
    }
    // Reset mesh cursor for SetupCharacter
    mMeshCursor = mMeshes.data();
}

void RndText::FontMap3d::CleanupSyncMeshes() {
    for (; mMeshCursor != &mMeshes.back() + 1; mMeshCursor++) {
        (*mMeshCursor)->SetShowing(false);
    }
}

void RndText::FontMap::SetupCharacter(
    unsigned short charCode,
    float &xPos,
    float yPos,
    const StyleState &state,
    unsigned short prevChar,
    float size,
    FitType fitType,
    float leading
) {
    // Setup a character quad in the mesh
    if (!mFont) return;
    int page = mFont->CharPage(charCode);
    if (page < 0 || page >= (int)mPages.size()) {
        return;
    }
    Page &pg = *(mPages[page]);
    if (!pg.mesh || !pg.mVertStart || pg.mVertStart == pg.mesh->Verts().end()) {
        return;
    }

    float charW, advW;
    Vector2 uvMin, uvMax;
    if (!mFont->CharWidthAdvanceCoords(charCode, charW, advW, uvMin, uvMax)) {
        xPos += mFont->CharAdvance(charCode) * size;
        return;
    }

    float cellH = mFont->AspectRatio() * size;
    float x0 = xPos;
    float x1 = x0 + charW * size;
    float z0 = yPos;
    float z1 = z0 - cellH;

    RndMesh::Vert *v = pg.mVertStart;
    v[0].pos.Set(x0, 0.0f, z0);
    v[0].tex.Set(uvMin.x, uvMin.y);
    v[1].pos.Set(x1, 0.0f, z0);
    v[1].tex.Set(uvMax.x, uvMin.y);
    v[2].pos.Set(x1, 0.0f, z1);
    v[2].tex.Set(uvMax.x, uvMax.y);
    v[3].pos.Set(x0, 0.0f, z1);
    v[3].tex.Set(uvMin.x, uvMax.y);
    pg.mVertStart += 4;

    // Advance x position
    xPos += advW * size;
    if (prevChar) {
        xPos += mFont->Kerning(prevChar, charCode) * size;
    }
}

void RndText::FontMap3d::SetupCharacter(
    unsigned short charCode,
    float &xPos,
    float yPos,
    const StyleState &state,
    unsigned short prevChar,
    float size,
    FitType fitType,
    float leading
) {
    float width, advance;
    RndMesh *charMesh;
    if (!mFont->CharWidthAdvanceMesh(charCode, width, advance, &charMesh))
        return;

    // Apply kerning + style kerning
    xPos += (mFont->Kerning(prevChar, charCode) + state.mKerning) * state.mSize;

    // Use advance as display width if width <= 0
    if (width <= 0.0f) {
        width = advance;
    }

    // Monospace centering
    float centerOffset = 0.0f;
    if (mFont->IsMonospace()) {
        centerOffset = Max((advance - width) * 0.5f, 0.0f);
    }

    float scaledWidth = state.mSize * width;
    float scaledCenter = state.mSize * centerOffset;

    if (scaledWidth <= 0.0f)
        return;

    yPos += state.mZOffset * state.mSize;

    if (charMesh && mMeshCursor != mMeshes.end()) {
        RndMesh *mesh = *mMeshCursor;
        mMeshCursor++;
        mesh->SetGeomOwner(charMesh);

        // Copy origin to transform position, then scale in-place
        Vector3 origin = mFont->CharOriginOffset();

        Transform xfm;
        xfm.v = origin;
        xfm.v.x = xfm.v.x * state.mSize + scaledCenter + xPos;
        xfm.v.y *= state.mSize;
        xfm.v.z = xfm.v.z * state.mSize + yPos;

        // Scale matrix by cell height
        float cellHeight = mFont->FontUnitInverse() * state.mSize;
        xfm.m.x.Set(cellHeight, 0.0f, 0.0f);
        xfm.m.y.Set(0.0f, cellHeight, 0.0f);
        xfm.m.z.Set(0.0f, 0.0f, cellHeight);

        if (size != 0.0f) {
            float circlePos = scaledWidth * 0.5f + xfm.v.x;
            Transform circleXfm = XfmOnCircleEdge(circlePos, size);
            xfm.v.x -= circlePos;
            Multiply(xfm, circleXfm, xfm);
        }

        memcpy(&mesh->mWorldXfm, &xfm, sizeof(Transform));
        if (!mesh->mDirty) {
            mesh->SetDirty_Force();
        }
    }

    xPos += state.mSize * advance;
}

#ifndef HX_NATIVE
// Template instantiation for map<RndFontBase*, set<unsigned short>>
#include <map>
#include <set>
#include "utl/StlAlloc.h"
namespace stlpmtx_std {
typedef set<unsigned short, less<unsigned short>, StlNodeAlloc<unsigned short> > _FontCharSet;
typedef pair<RndFontBase* const, _FontCharSet> _FontMapValue;
template class _Rb_tree<RndFontBase*,
    less<RndFontBase*>,
    _FontMapValue,
    _Select1st<_FontMapValue>,
    priv::_MapTraitsT<_FontMapValue>,
    StlNodeAlloc<_FontMapValue> >;
}
#endif
