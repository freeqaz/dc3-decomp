#include "rndobj\Text.h"
#include "Text.h"
#include "obj\Msg.h"
#include "obj/Object.h"
#include "os\Debug.h"
#include "os\System.h"
#include "rndobj\Draw.h"
#include "rndobj\Font.h"
#include "rndobj\FontBase.h"
#include "rndobj\Mat.h"
#include "rndobj\Mesh.h"
#include "rndobj\Trans.h"
#include "rndobj/Cam.h"
#include "rndobj\Rnd.h"
#include "math\Trig.h"
#include "utl/BinStream.h"
#include "utl\FilePath.h"
#include "utl\MemMgr.h"
#include "utl\UTF8.h"
#include "wordwrap.h"
#include "ui\UI.h"
#include <algorithm>
#include <map>
#include <set>
#ifdef HX_NATIVE
#include <cstdio>
#include <cstdlib>
#include <cstring>
#endif

// Explicit template instantiation for MakeString<char>
template const char *MakeString<char>(const char *, const char &);

std::vector<RndText::BlacklightPacket> RndText::sBlacklightPacketPool;
int RndText::sBlacklightPacketCount;
bool RndText::sBlacklightModeEnabled;
std::list<RndText::FontMapBase *> RndText::sFontMapCache;
int TEXT_REV = 0;
// TU-local: the image's references to all three carry NO symbol name (dtk emits
// placeholder `lbl_82F14D14` relocations), and ParseMarkup's gtr arm addresses
// gGuitarScale/gGuitarZOffset as 0x4/0x8 off a single anchor at gSuperscriptScale
// -- an offset the compiler can only know for internal-linkage data in one section.
static float gSuperscriptScale = 0.7f;
static float gGuitarScale = 0.7f;
static float gGuitarZOffset = 0.2f;

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
    float sign;
    if (circumference >= 0.0f) {
        sign = 1.0f;
    } else {
        sign = -1.0f;
    }

    xfm.m.z.Set(0.0f, 0.0f, 1.0f);

    float offset = sign * -1.5707964f;
    float angle = (pos / circumference) * 6.2831855f + offset;

    float cosA = Cosine(angle);
    float sinA = Sine(angle);

    float negSign = -sign;
    xfm.v.Set(cosA, sinA, 0.0f);
    xfm.m.y.y = xfm.v.y * negSign;
    xfm.m.y.x = xfm.v.x * negSign;
    xfm.m.y.z = xfm.v.z * negSign;

    xfm.m.x.z = xfm.m.y.x * xfm.m.z.y - xfm.m.z.x * xfm.m.y.y;
    xfm.m.x.x = xfm.m.z.z * xfm.m.y.y - xfm.m.y.z * xfm.m.z.y;
    xfm.m.x.y = xfm.m.y.z * xfm.m.z.x - xfm.m.z.z * xfm.m.y.x;

    float radius = (sign * circumference) * 0.15915494f;
    xfm.v *= radius;

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
      mLineHeight(0), mScrollCopies(0), mNumLines(0), mIndentation(0),
      mAltStyle(nullptr), mScrollOffset(0), mCurScrollChars(-1), mScrollOutIndex(-1),
      mStyles(this), mBounds(0, 0, 0, 0),
      mNumLinesRendered(0), mConstructScale(0) {
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

RndText::Style::Style(Hmx::Object *owner) : mFont(owner), mBlacklight(false) {}

RndText::Style::Style(const Style &s) : StyleData(s), mFont(s.mFont) {
    mBlacklight = s.mBlacklight;
}

RndText::StyleState::StyleState(RndText *text, float size) {
    memcpy(this, &text->mStyles[0], 0x34);
    mStyle = &text->mStyles[0];
    mFontMapIdx = text->FontMapIndex(mStyle->mFont, mStyle->mBlacklight);
    mBaseSize = size;
    mSize *= size;
    brk = true;
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
    if (TEXT_REV >= 25) {
        bs >> s.mBlacklight;
    }
    return bs;
}

INIT_REVS(28, 1)

BEGIN_LOADS(RndText)
    LOAD_REVS(bs)
    ASSERT_REVS(28, 1)
    TEXT_REV = d.rev;
    // NOTE: `style` MUST be declared before `font`. Swapping the two
    // declarations costs 5.6pp (99.4 -> 93.8): it moves the whole frame by
    // 0x10 and de-schedules the TEXT_REV block. The target's slot order is
    // style-then-font.
    StyleData style;
    ObjPtr<RndFontBase> font(this);
    if (d.rev > 15) {
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
    if (d.rev < 22) {
        bs >> font;
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
    if (d.rev < 20) {
        std::vector<unsigned short> vec;
        ASCIItoWideVector(vec, mText.c_str());
        WideVectorToUTF8(vec, mText);
    }
    if (d.rev > 0 && d.rev < 22) {
        bs >> style.mTextColor;
    }
    if (d.rev > 12) {
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
        if (font) {
            RndFont *oldfont2d = dynamic_cast<RndFont *>(font.Ptr());
            MILO_ASSERT(oldfont2d, 0xBC1);
            if (oldfont2d->NumMats() != 0 && oldfont2d->Mat(0)) {
                int zMode = b ? 2 : 0;
                font->Mat()->SetZMode((ZMode)zMode);
            }
        }
    }
    if (d.rev > 7) {
        bs >> mLeading;
    }
    if (d.rev > 11) {
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
    if (d.rev > 9 && d.rev < 22) {
        bs >> style.mItalics;
    }
    if (d.rev < 22) {
        if (d.rev > 12) {
            bs >> style.mSize;
        } else if (font) {
            RndFont *oldfont2d = dynamic_cast<RndFont *>(font.Ptr());
            MILO_ASSERT(oldfont2d, 0xBE9);
            style.mSize = oldfont2d->DeprecatedSize();
        }
        if (d.rev < 13) {
            style.mItalics /= style.mSize;
        }
    }
    if (d.rev > 13) {
        d >> mMarkup;
    }
    if (d.rev > 14) {
        bs >> (int &)mCapsMode;
    } else {
        mCapsMode = kCapsModeNone;
    }
    if (d.rev >= 18 && d.rev < 21) {
        bool b;
        d >> b;
    }
    if (d.rev >= 19 && d.rev < 21) {
        int i, j, k;
        bs >> i;
        bs >> j;
        bs >> k;
    }
    if (d.rev >= 22) {
        if (d.rev > 22) {
            if (d.rev == 23) {
                TheDebug.Notify(MakeString(
                    "%s was bad version 23, suggest reverting and resaving, lost [height] and [fit_type]",
                    PathName(this)
                ));
            } else {
                bs >> mHeight;
                if (d.rev < 24) {
                    String str;
                    bs >> str;
                }
                if (d.altRev > 0) {
                    bs >> mCircle;
                }
                bs >> (int &)mFitType;
            }
        }
        d >> mStyles;
    } else {
        mStyles.resize(1);
        // The image re-zeroes mZOffset here, after resize() and before the
        // memcpy, even though StyleData's constructor already did
        // (`stfs f31, 0xf0(r31)` with style at r31+0xc0 and mZOffset at +0x30).
        style.mZOffset = 0;
        memcpy(&mStyles[0], &style, sizeof(StyleData));
        mStyles[0].mFont = font;
    }
    if (d.rev >= 26) {
        bs >> mScrollDelay;
        bs >> mScrollRate;
        bs >> mScrollPause;
    }
    if (d.rev >= 27) {
        bs >> mIndentation;
    }
    if (d.rev >= 28) {
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

void RndText::Highlight() { RndDrawable::Highlight(); }

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
                } else if (GetSphere().GetRadius() != 0.0f) {
                    // NOT a typo and NOT `mesh->`: the image reads THIS RndText's
                    // own sphere and world transform here, through its own
                    // vbtable (`lwz r11, -0x108(r31)` / `add r10, r10, r31` /
                    // `lfs f0, -0xec(r10)` at 0x8269089C, r31 = this), so every
                    // glyph mesh is grown by the same text-level sphere.  RB3's
                    // shared-engine source spells it `mSphere` / `WorldXfm()`
                    // too.  Decompiling it as `mesh->GetSphere()` /
                    // `mesh->WorldXfm()` "fixed" a bug the engine really has.
                    Multiply(GetSphere(), WorldXfm(), localSphere);
                }
                s.GrowToContain(localSphere);
            }
        }
    }
    return s.GetRadius();
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
#ifdef HX_NATIVE
    if (numFaces <= 0 || numFaces > 100000) return;
#endif
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
#ifdef HX_NATIVE
        // Guard against garbage displayableChars from font loading issues
        if (page.displayableChars < 0 || page.displayableChars > 10000) {
            page.displayableChars = 0;
        }
#endif
        if (!page.mesh && mFont && page.displayableChars > 0) {
            page.mesh = Hmx::Object::New<RndMesh>();
#ifdef HX_NATIVE
            // Label text meshes for GPU debug (buffer names + frame capture).
            // Uses a side-table label instead of SetName to avoid registering
            // in ObjectDir, which would cause double-draws during traversal.
            {
                extern void SetMeshDebugLabel(RndMesh*, const char*);
                char label[64];
                snprintf(label, sizeof(label), "TEXT_%.48s_p%d", text->Name(), i);
                SetMeshDebugLabel(page.mesh, label);
            }
#endif
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
            if (fixedLength == 0) {
                int numFaces = page.displayableChars * 2;
                mesh->SetMutable(0);
                ResetFontMapPageMeshFaces(mesh, numFaces);
                page.mSyncFlags |= 0xA0;
                mesh->Verts().resize(numFaces * 2);
            } else if ((mesh->Mutable() & 0x1F) == 0
                       || mesh->Verts().size() != fixedLength * 4) {
                mesh->SetMutable(0x1F);
                ResetFontMapPageMeshFaces(mesh, fixedLength * 2);
                page.mSyncFlags |= 0xA0;
                mesh->Verts().resize(fixedLength * 4);
            }
#ifdef HX_NATIVE
            // Clamp to available verts in native builds
            if (mesh->Verts().size() < page.displayableChars * 4)
                page.displayableChars = mesh->Verts().size() / 4;
#endif
            page.mVertStart = mesh->Verts().begin();
#ifndef HX_NATIVE
            MILO_ASSERT(mesh->Verts().size() >= page.displayableChars * 4, 0xD2);
#endif
        }
#ifndef HX_NATIVE
        MILO_ASSERT(!fixedLength || (page.displayableChars <= fixedLength), 0xD5);
#else
        if (fixedLength && page.displayableChars > fixedLength)
            page.displayableChars = fixedLength;
#endif
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
            Vector3 pos = mesh->LocalXfm().v;
            pos.x = f1;
            mesh->SetLocalPos(pos);
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

struct WrapPoint {
    int charIdx;
    unsigned int cost;
    int bestPrevIdx;
    int nextIdx;
    float lineWidth;
    bool isLineEnd;
    bool isHardBreak;
};

void RndText::WrapText(
    const unsigned short *wideChars, int wLen, float *charWidths,
    HX_VECTOR(Line) &lines, Hmx::Rect &bounds, float scale
) {
    MemPushTemp();
    lines.reserve(100);
    auto _tmp0 = lines.begin();
    lines.erase(_tmp0, lines.end());
    MemPopTemp();
    StyleState style(this, scale);
    auto& _ref0 = mWidth;
    auto& _ref1 = mAlignment;
    // NOTE: the image materialises `this + 0xa8` into a register and homes it
    // at 0x78(r31) before testing emptiness (rows 31/33). Spelling that as a
    // reference local `auto& _ref2 = mFontMaps;` is byte-for-byte INERT --
    // MSVC folds the reference back into the member access -- so the
    // _ref0/_ref1 lever that works for mWidth/mAlignment does not extend here.
    if (mFontMaps.empty() || wLen == 0 || mStyles[0].mFont == 0) {
        Line emptyLine;
        emptyLine.mStart = 0;
        emptyLine.mEnd = 0;
        emptyLine.mWidth = 0.0f;
        if (lines.size() > 1) lines.erase(lines.begin() + 1, lines.end());
        else lines.insert(lines.end(), 1 - lines.size(), emptyLine);
        Line &l0 = lines[0];
        l0.mWidth = 0.0f;
        l0.mStart = wideChars;
        l0.mEnd = wideChars;
    } else if (_ref0 == 0.0f) {
        Line emptyLine;
        emptyLine.mStart = 0;
        emptyLine.mEnd = 0;
        emptyLine.mWidth = 0.0f;
        if (lines.size() > 1) lines.erase(lines.begin() + 1, lines.end());
        else lines.insert(lines.end(), 1 - lines.size(), emptyLine);
        Line &l0 = lines[0];
        l0.mXStart = 0.0f;
        l0.mStart = wideChars;
        l0.mYPos = 0.0f;
        l0.mEnd = wideChars + wLen;
        l0.mWidth = SegmentLength(0, wLen, charWidths, wideChars, scale);
    } else {
        WrapPoint *wps = (WrapPoint *)_alloca((wLen + 1) * sizeof(WrapPoint));
        bool activeMarkup = style.brk;
        int numWp = 1;
        int cCount = 0;
        wps[0].lineWidth = 0.0f;
        wps[0].charIdx = 0;
        wps[0].cost = 0;
        wps[0].bestPrevIdx = -1;
        wps[0].nextIdx = -1;
        wps[0].isLineEnd = true;
        wps[0].isHardBreak = true;
        float minW = _ref0 * 0.7f;
        float goodW = _ref0 * 0.95f;
        const unsigned short *brkChars = wideChars;
        if (mMarkup) {
            unsigned short *stripped = (unsigned short *)_alloca((wLen + 1) * 2);
            brkChars = stripped;
            const unsigned short *s = wideChars;
            while (*s != 0) {
                unsigned short c = *s;
                unsigned short t = c;
                if (c == 0x3c) {
                    do { if (t == 0x3e) break; s++; t = *s; } while (t != 0);
                    if (t == 0) continue;
                } else { *stripped = c; stripped++; }
                s++;
            }
            *stripped = 0;
        }
#ifdef HX_NATIVE
        // On Linux, wchar_t is 4 bytes but our text buffers are unsigned short (2 bytes).
        // Convert brkChars to wchar_t for WordWrap_CanBreakLineAt.
        // Allocate +2 padding: WordWrap_CanBreakLineAt reads cur[1] (lookahead)
        // which can read one past the null terminator.
        int brkLen = 0.0f;
        { const unsigned short *t = brkChars; while (*t++) brkLen++; }
        wchar_t *brkWideBuf = (wchar_t *)_alloca((brkLen + 2) * sizeof(wchar_t));
        for (int bi = 0; brkLen - bi >= 0; bi++) brkWideBuf[bi] = (wchar_t)brkChars[bi];
        brkWideBuf[brkLen + 1] = 0;
#define BRKWIDE_BASE brkWideBuf
#else
        // On PPC, wchar_t is 2 bytes (same as unsigned short) — cast directly.
#define BRKWIDE_BASE ((const wchar_t *)brkChars)
#endif

        // Declared HERE, after the markup strip: the image reuses the same
        // callee-saved register for the strip loop's `s` and for `cur`, and
        // emits an explicit `mr r21, r15` reset between them. That only
        // happens if `cur` is not live across the strip block.
        //
        // Two refuted follow-ons, both measured against this 94.8 baseline:
        //   - declaring `int wpI = 0;` BEFORE `cur` (the image's emission
        //     order at rows 176/180/181) holds 94.8 but raises the register
        //     swap count 185 -> 187: inert at best.
        //   - writing `wpI++; numWp++;` instead of `numWp++; wpI++;` at both
        //     WrapPoint-commit sites -- which is the order the image's three
        //     induction updates appear in (+1, +0x18, +0x18 vs our +0x18, +1,
        //     +0x18) -- is a REGRESSION, 94.8 -> 94.5.
        const unsigned short *cur = wideChars;
        int wpI = 0;
        for (;;) {
            unsigned short ch = *cur;
            const wchar_t *brkW = &BRKWIDE_BASE[cCount];
            int prevI = wpI;
            WrapPoint *nxt = &wps[numWp];
            if (ch == 0 || ch == '\n') {
                int bestWp = -1, bestC = 100000;
                bool ovf = false;
                float bestLineLen = 0.0f;
                for (int wi = wpI; wi >= 0; wi--) {
                    float lineLen = SegmentLength(
                        wps[wi].charIdx, (int)(cur - wideChars), charWidths, wideChars, scale
                    );
                    unsigned int pen = 10;
                    if (lineLen > _ref0) {
                        if (wi != prevI) {
                            pen = 2010;
                            if (bestWp != -1) { ovf = true; }
                        }
                    } else {
                        if (_ref1 & 0x20) {
                            float fw = SegmentLength(wps[wi].charIdx, wLen, charWidths, wideChars, scale);
                            if (fw >= _ref0) {
                                pen = (unsigned int)(int)((1.0f - lineLen / _ref0) * 30.0f);
                                if (lineLen < minW) pen += 100;
                            }
                        } else {
                            if ((int)(cur - wideChars) - wps[wi].charIdx <= 4) pen = 50;
                        }
                    }
                    int tc = (int)pen + wps[wi].cost;
                    if (tc < bestC) { bestWp = wi; bestLineLen = lineLen; bestC = tc; }
                    if (wps[wi].isHardBreak || ovf) break;
                }
                MILO_ASSERT(bestWp != -1, 0x6ed);
                MILO_ASSERT(numWp < wLen + 1, 0x6ef);
                nxt->lineWidth = bestLineLen;
                nxt->cost = bestC;
                nxt->bestPrevIdx = bestWp;
                nxt->isLineEnd = true;
                nxt->nextIdx = -1;
                nxt->charIdx = (int)(cur - wideChars);
                nxt->isHardBreak = true;
                numWp++; wpI++;
                wps[bestWp].isLineEnd = false;
                if (*cur == 0) goto buildLines;
            } else {
                unsigned short mc = ch;
                if (ch == 0x3c) {
                    if (mMarkup) {
                    cur = ParseMarkup(cur, style, mc);
                    cCount--;
                    brkW = &BRKWIDE_BASE[cCount];
                    cur--;
                    if (style.brk) {
                        activeMarkup = true;
                    }
                }
                }
                if (mc != 0) {
                    int ci = (int)(cur - wideChars);
                    if (activeMarkup) {
                        bool canBrk;
                        if (cCount <= 0) canBrk = false;
                        else canBrk = WordWrap_CanBreakLineAt(brkW, BRKWIDE_BASE) != 0;
                        if (canBrk) {
                            int bestWp = -1, bestC = 100000;
                            bool ovf = false;
                            float bestLineLen = 0.0f;
                            for (int wi = wpI; wi >= 0; wi--) {
                                float lineLen = SegmentLength(wps[wi].charIdx, ci, charWidths, wideChars, scale);
                                MILO_ASSERT(lineLen >= bestLineLen, 0x65d);
                                unsigned int pen = 10;
                                float mxW2 = _ref0;
                                if (lineLen > mxW2) {
                                    if (wi != prevI) {
                                        pen = 2010;
                                        if (bestWp != -1) { ovf = true; }
                                    }
                                } else {
                                    if (lineLen < goodW) {
                                        float fw = SegmentLength(wps[wi].charIdx, wLen, charWidths, wideChars, scale);
                                        if (fw >= mxW2) {
                                            pen = (unsigned int)(int)((1.0f - lineLen / mxW2) * 60.0f);
                                            if (lineLen < minW) pen += 200;
                                        }
                                    }
                                }
                                int tc2 = (int)(wps[wi].cost + pen);
                                if (tc2 <= bestC) {
                                    bestWp = wi; bestLineLen = lineLen; bestC = tc2;
                                }
                                if (wps[wi].isHardBreak || ovf) break;
                            }
                            MILO_ASSERT(bestWp != -1, 0x690);
                            MILO_ASSERT(numWp < wLen + 1, 0x693);
                            nxt->lineWidth = bestLineLen;
                            nxt->charIdx = ci;
                            nxt->cost = bestC;
                            nxt->bestPrevIdx = bestWp;
                            nxt->nextIdx = -1;
                            nxt->isLineEnd = true;
                            numWp++; wpI++;
                            nxt->isHardBreak = false;
                            wps[bestWp].isLineEnd = false;
                        }
                    }
                    if (activeMarkup != style.brk) {
                        MILO_ASSERT(style.brk == false, 0x6a2);
                        activeMarkup = false;
                    }
                }
            }
            cur++; cCount++;
        }
    buildLines: {
            int idx = numWp - 1;
        if (idx != 0) {
            do {
                unsigned int pi = wps[idx].bestPrevIdx;
                wps[pi].nextIdx = idx;
                idx = pi;
            } while (idx != 0);
        }
        Line ol;
        if (wps[0].nextIdx != -1) {
        WrapPoint *wp = &wps[0];
        do {
            // NOTE: hoisting `ol.mStart = wideChars + wp->charIdx;` above the
            // `nx` computation (which is the image's emission order) is
            // byte-for-byte INERT, and re-deriving the successor as
            // `wp = &wps[wp->nextIdx]` instead of `wp = nx` at the bottom of
            // this loop is a REGRESSION (94.5 -> 93.9, +5 instructions).
            WrapPoint *nx = &wps[wp->nextIdx];
            ol.mWidth = nx->lineWidth;
            ol.mEnd = wideChars + nx->charIdx;
            ol.mStart = wideChars + wp->charIdx;
            for (;;) {
                unsigned short p = *ol.mStart;
                if (p != ' ' && p != '\t') break;
                if (ol.mEnd <= ol.mStart) break;
                ol.mStart++;
            }
            for (;;) {
                unsigned short p = ol.mEnd[-1];
                if (p != ' ' && p != '\n' && p != '\t') break;
                if (ol.mEnd <= ol.mStart) break;
                ol.mEnd--;
            }
            lines.push_back(ol);
            wp = nx;
        } while (wp->nextIdx != -1);
        }
        if (lines.size() == 0) {
            ol.mWidth = 0.0f;
            ol.mStart = wideChars;
            ol.mEnd = wideChars;
            lines.push_back(ol);
        }
    } }
    float topY = 0.0f;
    float ls;
    float th = ComputeHeight((int)lines.size(), scale, ls);
    bounds.h = th;
    if (_ref1 & 0x20) topY = th * 0.5f;
    else if (_ref1 & 0x40) topY = th;
    bounds.w = 0.0f;
    for (unsigned int i = 0; i < lines.size(); i++) {
        Line &l = lines[i];
        bounds.w = Max(bounds.w, l.mWidth);
        if (_ref1 & 0x2) l.mXStart = l.mWidth * -0.5f;
        else if (_ref1 & 0x4) l.mXStart = -l.mWidth;
        else l.mXStart = 0.0f;
        l.mYPos = topY;
        topY -= ls;
    }
    bounds.x = lines[0].mXStart;
    bounds.y = lines[0].mYPos - bounds.h;
}
#undef BRKWIDE_BASE

void RndText::SetText(const char *str) {
    unsigned short us;
    if (mFixedLength != 0) {
        MILO_ASSERT(mText.capacity() >= mFixedLength, 0x75E);
        const char *p = str;
        for (int newLen = 0; *p != '\0' && newLen < mFixedLength; newLen++) {
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
        MemDoTempAllocations tmp;
        str.resize(UTF8StrLen(mText.c_str()) + 1);
    }
    UTF8toASCIIs((char *)str.c_str(), str.capacity(), mText.c_str(), '*');
    return str;
}

void RndText::BuildFontMaps(bool b1) {
    if (b1) {
        for (auto it = mFontMaps.begin(); it != mFontMaps.end();
             it = mFontMaps.erase(it)) {
            // The image copies the element out first and passes the address of
            // that copy to list::insert -- `lwz r11,0x0(r31)` / `stw
            // r11,0x58(r1)` / `addi r6,r1,0x58` -- rather than binding the
            // const& straight to the vector slot (`mr r6, r31`).
            FontMapBase *map = *it;
            sFontMapCache.push_back(map);
        }
    }
    if (mFontMaps.empty()) {
        for (int i = 0; i < mStyles.size(); i++) {
            // The image falls back to style 0's font when style i has none:
            // `lwz r9,0x40(r10)` / `addi r10,r10,0x34` / `cmpwi cr6,r9,0x0` /
            // `bne` / `addi r10,r11,0x34` / `lwz r29,0xc(r10)` -- the ternary
            // selects between the two ObjPtr objects (at +0x34) and only then
            // reads the raw pointer out of the selected one (+0xc).
            RndFontBase *font = mStyles[i].mFont ? mStyles[i].mFont : mStyles[0].mFont;
            if (font) {
                if (FontMapIndex(font, mStyles[i].mBlacklight) == -1) {
                    FontMapBase *map = AcquireFontMap(font);
                    map->mBlacklight = mStyles[i].mBlacklight;
                    mFontMaps.push_back(map);
                }
            }
        }
#ifdef HX_NATIVE
#endif
    }
}

void RndText::SetTextASCII(const char *cstr) {
    String str;
    {
        MemDoTempAllocations tmp;
        std::vector<unsigned short> vec;
        ASCIItoWideVector(vec, cstr);
        WideVectorToUTF8(vec, str);
    }
    SetText(str.c_str());
}

int RndText::ConvertTextToWide(const char *str, HX_VECTOR(unsigned short) &wideChars) {
    char emptyStr = 0;
    if (str == 0) {
        str = &emptyStr;
    }

    // Manual strlen to match target inline loop
    const char *s = str;
    // RESIDUAL (w7-am, 91.9 canonical): this function is instruction-for-
    // instruction the image's -- same blocks, same order, same seven live
    // values (this, 0, 0x53, limit, str, &wideChars, out) -- with ONE
    // difference that renames every register: 8269A004 `subi r31, r1, 0xb0`
    // dedicates r31 to a frame pointer and addresses every local off it, so
    // the image saves r24-r31 and its frame is 0xb0.  We get no frame pointer,
    // address the same locals off r1, save r25-r31 and use r31 for `out`, so
    // all seven values sit one register lower.  Nothing in this function's
    // source chooses that; the other structural rows are the image's two spills
    // of `_M_finish` to 0x54 around the resize.
    // NEGATIVE RESULT (w7-am, 2026-09-14): reducing the manual strlen to an
    // `int len` BEFORE MemPushTemp(), which is what 8269A030-8269A034 do (the
    // count, not the walking pointer, lives across the call), is inert -- MSVC
    // already sinks it.  Identical 79-row profile, still 91.9.
    while ('\0' != *s++) {}
    MemPushTemp();
    wideChars.resize(((s - str) - 1) * 2 + 1, (0));
    wideChars[0] = 0;
    MemPopTemp();

    unsigned short *out = &wideChars[0];

    if (mCapsMode == kForceUpper) {
        unsigned short ssChar;
        DecodeUTF8(ssChar, "\xC3\x9F");
        int fixedLen = mFixedLength;
        unsigned short *limit;
        if (fixedLen != 0) {
            limit = out + fixedLen;
        } else {
            limit = 0;
        }
        while (*str != '\0') {
            if (out == limit) break;
            unsigned short ch;
            str += DecodeUTF8(ch, str);
            if (ch == ssChar) {
                *out = 0x53;
                out++;
                if (out == limit) break;
                *out = 0x53;
            } else {
                *out = WToUpper(ch);
            }
            out++;
        }
    } else if (mCapsMode == kForceLower) {
        while (*str != '\0') {
            unsigned short ch;
            str += DecodeUTF8(ch, str);
            *out = WToLower(ch);
            out++;
        }
    } else {
        while (*str != '\0') {
            str += DecodeUTF8(*out, str);
            out++;
        }
    }

    *out = 0;
    ReplaceMissingCharacters(wideChars);
    return (int)(out - &wideChars[0]);
}

void RndText::ReplaceMissingCharacters(HX_VECTOR(unsigned short) &wideChars) {
    std::map<RndFontBase *, std::set<unsigned short> > missingMap;
    unsigned short curChar;
    HX_VECTOR(unsigned short) origChars;
    bool copied = false;
    StyleState styleState(this, 1.0f);
    unsigned short *p = &wideChars[0];
    curChar = *p;
    while (curChar != 0) {
        curChar = *p;
        if (curChar == 0x3c && mMarkup) {
            p = (unsigned short *)ParseMarkup(p, styleState, curChar);
            p = p - 1;
        } else if (curChar != 10 && styleState.mFontMapIdx != -1) {
            FontMapBase *fm = mFontMaps[styleState.mFontMapIdx];
            RndFontBase *font;
            if (fm != 0 && (font = fm->Font()) != 0
                && !font->CharDefined(curChar)) {
                missingMap[font].insert(curChar);

                unsigned short replacements[] = {0x25a1, 0x3f, 0x23, 0x2a, 0x21, 0x39};
                curChar = 0;
                int i = 0;
                unsigned short *rp = replacements;
                do {
                    unsigned short tryChar = *rp;
                    if (font->CharDefined(tryChar)) {
                        curChar = tryChar;
                        break;
                    }
                    i = i + 1;
                    rp = rp + 1;
                } while (i < 6);

                if (curChar == 0) {
                    std::vector<unsigned short> fontChars(font->mChars);
                    // curChar is written ONLY on the break, and the counter is
                    // zeroed before the emptiness test: the image sets j from its
                    // zero register at .L_82699a00, ahead of the `srawi.`/`beq`,
                    // and jumps PAST the `mr r26, r7` (.L_82699a4c `b .L_82699a54`)
                    // when the loop runs out. Carrying the character in a local that
                    // is re-seeded from curChar each iteration added a `mr r11, r26`
                    // inside the loop and dropped that skip branch.
                    unsigned int j = 0;
                    unsigned int count = fontChars.size();
                    if (count != 0) {
                        unsigned short *fp = &fontChars[0];
                        do {
                            unsigned short c = *fp;
                            bool skip;
                            if (c == 0x20 || c == 0xa0) {
                                skip = true;
                            } else {
                                skip = false;
                            }
                            if (!skip) {
                                curChar = c;
                                break;
                            }
                            j = j + 1;
                            fp = fp + 1;
                        } while (j < count);
                    }
                }

                if (curChar != 0) {
                    if (!copied) {
                        origChars = wideChars;
                        copied = true;
                    }
                    *p = curChar;
                }
            }
        }
        p = p + 1;
        curChar = *p;
    }

    {
        std::map<RndFontBase *, std::set<unsigned short> >::iterator mapIt = missingMap.begin();
        if (mapIt != missingMap.end()) {
        unsigned int origSize = origChars.size();
        do {
            // The set reference and the font are both taken at the TOP of the loop
            // body: the image emits `addi r29, r17, 0x14` (&mapIt->second) and
            // `lwz r28, 0x10(r17)` (mapIt->first) at .L_82699b1c/.L_82699b20, ahead
            // of the `cmplwi cr6, r11, 0x1` size test, and then addresses the set's
            // begin/end as `0x8(r29)` / `r29` rather than recomputing r17+0x14 each
            // time round the inner loop.
            std::set<unsigned short> &missing = mapIt->second;
            RndFontBase *font = mapIt->first;
            const char *pluralS = "s";
            if (missing.size() <= 1) {
                pluralS = "";
            }
            auto headerMsg = MakeString("%s:%s char%s (", PathName(this), TextToken(), pluralS);
            {
                String msg(headerMsg);

                for (std::set<unsigned short>::iterator setIt = missing.begin();
                     setIt != missing.end(); ++setIt) {
                    unsigned short ch = *setIt;
                    // if/else, not `= true` then a conditional `= false`: the image
                    // materialises the 1 only on the fall-through of the four tests
                    // (`li r11, 0x1` at .L_82699bcc, immediately before the last
                    // `bne`), and copies its zero register on the other path.
                    bool printable;
                    if (ch < 0x20 || ch >= 0xff || ch == 0x25 || ch == 0x7f) {
                        printable = false;
                    } else {
                        printable = true;
                    }
                    char displayChar;
                    if (printable) {
                        displayChar = (char)ch;
                    } else {
                        displayChar = '?';
                    }
                    const char *sep = "";
                    if (setIt != mapIt->second.begin()) {
                        sep = ", ";
                    }
                    msg += MakeString("%s\'%c\' 0x%02X", sep, displayChar, ch);
                }

                msg += MakeString(") missing from %s in string \"", PathName(font));

                unsigned int k = 0;
                unsigned short *qp = &origChars[0];
                if (origSize != 0) {
                    do {
                        unsigned short qch = *qp;
                        if (qch == 0)
                            break;
                        bool printable;
                        if (qch < 0x20 || qch >= 0xff || qch == 0x25 || qch == 0x7f) {
                            printable = false;
                        } else {
                            printable = true;
                        }
                        if (printable) {
                            msg += MakeString("%c", (char)qch);
                        } else {
                            msg += MakeString("\\x%02X", qch);
                        }
                        k = k + 1;
                        qp = qp + 1;
                    } while (k < origSize);
                }

                msg += "\"";
                MILO_NOTIFY(msg.c_str());
            }
            ++mapIt;
        } while (mapIt != missingMap.end());
        }
    }
}

int RndText::OnComputeCharWidths(const unsigned short *wideChars, float *widths, bool marqueeWrap) {
#ifdef HX_NATIVE
    // Reset displayable char counts before recomputing — prevents unbounded growth
    // when UpdateText() is called every frame (ResetDisplayableChars was never called)
    for (auto *fm : mFontMaps) {
        fm->ResetDisplayableChars();
    }
#endif
    StyleState styleState(this, 1.0f);
    unsigned short prevChar = 0;
    std::vector<unsigned short> negWidthChars;
    std::vector<unsigned short> missingChars;
    std::vector<RndFontBase *> missingFonts;
    float cumWidth = 0.0f;
    widths[0] = 0.0f;
    const unsigned short *p = wideChars;
    float *w = widths + 1;
    for (;;) {
        if (*p == 0) {
            if (missingChars.size()) {
                auto pathStr = PathName(this);
                String msg = MakeString("%s:%s '", pathStr, ClassName());
                FilePath hexMsg;
                unsigned short tmp[2];
                tmp[1] = 0;
                for (unsigned int i = 0; i < missingChars.size(); i++) {
                    tmp[0] = missingChars[i];
                    msg += MakeString("%s", WideCharToChar(tmp));
                    hexMsg += MakeString("0x%02x ", missingChars[i]);
                }
                msg += MakeString("' (%s", hexMsg);
                String fontNames;
                for (unsigned int i = 0; i < missingFonts.size(); i++) {
                    fontNames += MakeString("%s ", PathName(missingFonts[i]));
                }
                msg += MakeString(") missing from font(s) (%s) in string \"", fontNames);
                const unsigned short *q = wideChars;
                tmp[1] = 0;
                while (*q != 0) {
                    tmp[0] = *q;
                    msg += MakeString("%s", WideCharToChar(tmp));
                    q++;
                }
                msg += "\"";
                for (unsigned int i = 0; i < msg.length(); i++) {
                    if (msg[i] == '%') {
                        msg.replace(i, 1, "<PCNT>");
                        i++;
                    }
                }
                MILO_NOTIFY(msg.c_str());
            }
            if (negWidthChars.size()) {
                String msg = MakeString("%s: '", PathName(this));
                FilePath hexMsg;
                unsigned short tmp[2];
                tmp[1] = 0;
                for (unsigned int i = 0; i < negWidthChars.size(); i++) {
                    tmp[0] = negWidthChars[i];
                    msg += MakeString("%s", WideCharToChar(tmp));
                    hexMsg += MakeString("0x%02x ", negWidthChars[i]);
                }
                msg += MakeString("' (%s", hexMsg);
                // The image indexes mFontMaps[mFontMapIdx] here with NO -1 test:
                // 826954AC lwz r11,0xf8(r31) / lwz r10,0xa8(r25) / slwi / lwzx.
                // Compare the main-loop site at 82694EE4, which loads the same
                // slot and DOES `cmpwi cr6, r11, -1 / beq`. The guard is only
                // missing on this notify path, so mFontMaps[-1] is what the
                // original reads when a run of negative widths is reported for
                // text with no active font map.
                // Native keeps the guard: this function is compiled for the
                // native port, mFontMaps[-1] is a genuine out-of-bounds read
                // followed by a virtual call, and MILO_ASSERT is non-fatal
                // there, so the PPC-faithful spelling would corrupt rather than
                // trap.
#ifdef HX_NATIVE
                if (styleState.mFontMapIdx != -1)
#endif
                {
                    RndFontBase *font = mFontMaps[styleState.mFontMapIdx]->Font();
                    msg += MakeString(") have negative widths from %s in string \"", PathName(font));
                }
                const unsigned short *q = wideChars;
                tmp[1] = 0;
                while (*q != 0) {
                    tmp[0] = *q;
                    msg += MakeString("%s", WideCharToChar(tmp));
                    q++;
                }
                msg += "\"";
                MILO_NOTIFY(msg.c_str());
            }
            *w = cumWidth;
            return (int)(w - widths) - 1;
        }
        unsigned short ch = *p;
        if (ch == '<' && mMarkup) {
            unsigned short replaceChar = 0;
            const unsigned short *next = ParseMarkup(p, styleState, replaceChar);
            if (p < next) {
                *w = cumWidth;
                int numSkipped = (int)(next - p);
                for (int i = 1; i < numSkipped; i++) {
                    *(w + i) = cumWidth;
                }
                w += numSkipped;
                p = next;
            }
            if (replaceChar != 0) {
                w--;
                p--;
                ch = replaceChar;
                goto process_char;
            }
        } else {
process_char:
            if (ch != 0 && styleState.mFontMapIdx != -1) {
                FontMapBase *fontMap = mFontMaps[styleState.mFontMapIdx];
                RndFontBase *font = fontMap->Font();
                if (font) {
                    if (mFitType == kFitScrollMarqueeWrapAlways && ch == '\n') {
                        if (!marqueeWrap) {
                            mNumLines++;
                            float lw = (float)((double)cumWidth + (double)mNumLines * (double)mIndentation);
                            mLineWidths.insert(mLineWidths.end(), lw);
                            float lo = (float)((double)mNumLines * (double)mIndentation + (double)cumWidth);
                            mLineOffsets.insert(mLineOffsets.end(), lo);
                        } else {
                            cumWidth = (float)((double)mIndentation + (double)cumWidth);
                        }
                        float charWidth;
                        if (font->CharAdvance(prevChar, (unsigned short)'\n', charWidth)) {
                            fontMap->IncrementDisplayableChars('\n');
                        }
                    } else {
                        float charWidth;
                        bool found = font->CharAdvance(prevChar, ch, charWidth);
                        if (!found) {
                            if (ch != '\n') {
                                if (std::find(missingChars.begin(), missingChars.end(), ch) == missingChars.end()) {
                                    missingChars.push_back(ch);
                                }
                                if (std::find(missingFonts.begin(), missingFonts.end(), font) == missingFonts.end()) {
                                    missingFonts.push_back(font);
                                }
                            }
                        } else {
                            charWidth = (charWidth + styleState.mKerning) * styleState.mSize;
                            if ((double)charWidth < 0.0) {
                                if (ch != '\n') {
                                    if (std::find(negWidthChars.begin(), negWidthChars.end(), ch) == negWidthChars.end()) {
                                        negWidthChars.push_back(ch);
                                    }
                                }
                            } else {
                                cumWidth = (float)((double)charWidth + (double)cumWidth);
                            }
                            fontMap->IncrementDisplayableChars(ch);
                            prevChar = ch;
                        }
                    }
                }
            }
            *w = cumWidth;
            w++;
            p++;
            continue;
        }
    }
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

void RndText::UpdateScrollOffsets() {
    float fVar1 = TheTaskMgr.DeltaUISeconds();
    mScrollTimer += fVar1;

    if (!(mScrollTimer < mScrollState)) {
        float fVar2 = mScrollSpeed;
        int iVar9 = mFitType;
        float fVar3 = mTotalWidth;
        bool bVar10 = false;
        float dVar12 = fVar2 * fVar1 * 1000.0f;
        float fVar13 = mScrollPos + dVar12;
        mScrollPos = fVar13;
        float widthDiff = fVar3 - mWidth;

        switch (iVar9) {
        default:
            mScrollPos = 0.0f;
            break;

        case kFitScrollMarqueeWrapAlways: {
            static Message textScrolledIn("text_scrolled_in", -1);
            static Message textScrolledOut("text_scrolled_out", -1);

            mScrollOffset += dVar12;

            // One local reused for both halves: the target keeps a single 4-byte
            // frame slot (0x50) for the width and then the offset, with the list
            // iterator temporaries on 0x54.  Two named locals give them a slot
            // each and swap the second pair.
            float first = *mLineWidths.begin();
            if (!((mWidth - mScrollOffset) < first)) {
                mCurScrollChars++;
                if (mCurScrollChars >= mNumLines) {
                    mCurScrollChars = 0;
                }
                textScrolledIn[0] = DataNode(mCurScrollChars);
                if (first == mTotalWidth) {
                    mScrollOffset = mWidth;
                }
                unsigned int count = 0;
                for (auto it = mLineWidths.begin(); it != mLineWidths.end(); ++it) {
                    count++;
                }
                if ((unsigned int)mNumLines == count) {
                    mLineWidths.insert(mLineWidths.end(), first);
                }
                mLineWidths.erase(mLineWidths.begin());
                if (mAltStyle != nullptr) {
                    mAltStyle->Handle(textScrolledIn, false);
                }
            }

            first = *mLineOffsets.begin();
            if (!(mScrollPos > -first)) {
                mScrollOutIndex++;
                textScrolledOut[0] = DataNode(mScrollOutIndex);
                if (first == mTotalWidth) {
                    mScrollPos = 0.0f;
                    mScrollOutIndex = -1;
                }
                mLineOffsets.insert(mLineOffsets.end(), first);
                mLineOffsets.erase(mLineOffsets.begin());
                if (mAltStyle != nullptr) {
                    mAltStyle->Handle(textScrolledOut, false);
                }
            }
            break;
        }

        case kFitScrollPingPong:
            if (mScrollPos < -(fVar3 + 20.0f)) {
                mScrollPos = 0.0f;
                bVar10 = true;
            }
            break;

        case kFitScrollMarqueeReset:
            if (mScrollPos < -fVar3) {
                mScrollPos = 0.0f;
                bVar10 = true;
            }
            mLineHeight = fVar3;
            break;

        case kFitScrollMarqueeWrap: {
            if (fVar2 < 0.0f) {
                float fVar13_2 = -widthDiff;
                if (fVar13 < fVar13_2) {
                    mScrollPos = fVar13_2;
                    mScrollSpeed = -fVar2;
                    bVar10 = true;
                }
            } else if ((fVar2 > 0.0f) && (!(fVar13 < 0.0f))) {
                mScrollSpeed = -fVar2;
                mScrollPos = 0.0f;
                bVar10 = true;
            }
            break;
        }
        }

        if (bVar10) {
            mScrollTimer = 0.0f;
        }
    }

    for (auto puVar7 = mFontMaps.begin(); puVar7 != mFontMaps.end(); ++puVar7) {
        (*puVar7)->UpdateScrolling(mScrollPos);
    }
}

#ifdef HX_NATIVE
// On Linux, wchar_t is 4 bytes but our text buffers use unsigned short (2 bytes).
// Use manual u16 operations instead of wchar_t string functions to avoid buffer overflow.
static const unsigned short kEllipsisU16[] = {'.', '.', '.', 0};
static const unsigned short kBreakCharsU16[] = {' ', '\t', '\n', 0};

static int u16len(const unsigned short *s) {
    int n = 0;
    while (s[n])
        n++;
    return n;
}
static void u16cpy(unsigned short *dst, const unsigned short *src) {
    while ((*dst++ = *src++))
        ;
}
static const unsigned short *u16chr(const unsigned short *s, unsigned short ch) {
    while (*s) {
        if (*s == ch)
            return s;
        s++;
    }
    return nullptr;
}

// Tag name constants for ParseMarkup (2 bytes per char, matching unsigned short buffers)
static const unsigned short kTag_sup[] = {'s', 'u', 'p', 0};
static const unsigned short kTag_gtr[] = {'g', 't', 'r', 0};
static const unsigned short kTag_it[] = {'i', 't', 0};
static const unsigned short kTag_color[] = {'c', 'o', 'l', 'o', 'r', 0};
static const unsigned short kTag_hash[] = {'&', '#', 0};
static const unsigned short kTag_nobreak[] = {'n', 'o', 'b', 'r', 'e', 'a', 'k', 0};
static const unsigned short kTag_alt[] = {'a', 'l', 't', 0};

// Parse up to 'max_vals' space-separated decimal integers from a u16 string buffer.
// Returns the number of values successfully parsed.
static int u16_scan_ints(const unsigned short *s, int *vals, int max_vals) {
    int count = 0;
    while (count < max_vals) {
        // skip spaces
        while (*s == ' ' || *s == '\t')
            s++;
        if (*s == 0 || *s == '>')
            break;
        // parse optional sign + digits
        bool neg = false;
        if (*s == '-') {
            neg = true;
            s++;
        }
        if (*s < '0' || *s > '9')
            break;
        int v = 0;
        while (*s >= '0' && *s <= '9') {
            v = v * 10 + (*s - '0');
            s++;
        }
        vals[count++] = neg ? -v : v;
    }
    return count;
}
#endif

void RndText::FitTextJust() {
    BuildFontMaps(true);

    HX_VECTOR(unsigned short) wideChars;
    HX_VECTOR(Line) lines;
    int numChars = ConvertTextToWide(mText.c_str(), wideChars);
    float *charWidths = (float *)_alloca(sizeof(float) * (numChars + 2));
    OnComputeCharWidths(&wideChars[0], charWidths, false);

    Hmx::Rect bounds;
    float scale = 1.0f;
    WrapText(&wideChars[0], numChars, charWidths, lines, bounds, scale);

    float hi = mStyles[0].mSize;
    float lo = 0.2f;
    float cur = hi;

    if ((mWidth != 0.0f && mWidth < bounds.w) || (mHeight != 0.0f && mHeight < bounds.h)) {
        if (hi - lo > 0.2f) {
            do {
                cur = (lo + hi) * 0.5f;
                scale = cur / mStyles[0].mSize;
                WrapText(&wideChars[0], numChars, charWidths, lines, bounds, scale);
                if ((mWidth != 0.0f && mWidth < bounds.w) || (mHeight != 0.0f && mHeight < bounds.h)) {
                    hi = cur;
                } else {
                    lo = cur;
                }
            } while (hi - lo > 0.2f);
        }
        if (hi == cur) {
            scale = lo / mStyles[0].mSize;
            WrapText(&wideChars[0], numChars, charWidths, lines, bounds, scale);
        }
    }

    ConstructMeshes(lines, bounds, scale);
}

void RndText::FitTextEllipsis() {
    BuildFontMaps(true);

    HX_VECTOR(Line) lines;
    HX_VECTOR(unsigned short) wideChars;
    auto textStr = mText.c_str();
    int numChars = ConvertTextToWide(textStr, wideChars);

    // charWidths must also hold widths for the truncated+ellipsis buffer, so
    // size it numChars + ellipsisLen(3) + 2 to avoid overrunning in the
    // OnComputeCharWidths pass over buf below.
    float *charWidths = (float *)_alloca((numChars + 5) * sizeof(float));
    OnComputeCharWidths(&wideChars[0], charWidths, false);

    float scale = 1.0f;
    Hmx::Rect bounds;

    if (!(charWidths[numChars] <= mWidth)) {
        // Text doesn't fit — need to truncate and add ellipsis
#ifdef HX_NATIVE
        int ellipsisLen = u16len(kEllipsisU16);
#else
        int ellipsisLen = wcslen(L"...");
#endif

        // Binary search for how many chars fit
        int hi = numChars;
        int lo = 1;
        if (numChars > 2) {
            do {
                int mid = (lo + hi) >> 1;
                if (charWidths[mid] >= mWidth)
                    hi = mid;
                else
                    lo = mid;
            } while (hi > lo + 1);
        }

        // Total length = truncated text + ellipsis
        int totalLen = lo + ellipsisLen;

        // Allocate buffer for truncated text + ellipsis
        unsigned short *buf =
            (unsigned short *)_alloca((totalLen + 1) * sizeof(unsigned short));
        memcpy(buf, &wideChars[0], lo * sizeof(unsigned short));

        // Respect mFixedLength if set
        if (mFixedLength > ellipsisLen + 1 && totalLen > mFixedLength) {
            totalLen = mFixedLength;
        }

        // Place ellipsis at the truncation point
        int truncPos = totalLen - ellipsisLen;
#ifdef HX_NATIVE
        u16cpy(&buf[truncPos], kEllipsisU16);
#else
        wcscpy((wchar_t *)&buf[truncPos], L"...");
#endif

        BuildFontMaps(true);
        OnComputeCharWidths(buf, charWidths, false);
        WrapText(buf, totalLen, charWidths, lines, bounds, 1.0f);

        // Iteratively shrink if text still doesn't fit
        while (truncPos > 1
               && (lines.size() > 1 || bounds.w >= mWidth
#ifdef HX_NATIVE
                   || u16chr(kBreakCharsU16, buf[truncPos - 1]) != 0
#else
                   || wcschr(L" .,", (wchar_t)buf[truncPos - 1]) != 0
#endif
                   )) {
            // Try to find a space to break at (within ~87.5% of current length)
            int minPos = (int)totalLen * 0xe >> 4;
            totalLen = totalLen - 1;
            int searchPos = totalLen;
            if (searchPos >= minPos) {
                unsigned short *searchPtr = &buf[searchPos];
                do {
                    if (*searchPtr == 0x20) {
                        // Found a space — break here
                        totalLen = searchPos + ellipsisLen;
                        break;
                    }
                    searchPos--;
                    searchPtr--;
                } while (searchPos >= minPos);
            }

            truncPos = totalLen - ellipsisLen;
#ifdef HX_NATIVE
            u16cpy(&buf[truncPos], kEllipsisU16);
#else
            wcscpy((wchar_t *)&buf[truncPos], L"...");
#endif
            BuildFontMaps(true);
            OnComputeCharWidths(buf, charWidths, false);
            WrapText(buf, totalLen, charWidths, lines, bounds, 1.0f);
        }
    } else {
        // Text fits, just wrap normally
        WrapText(&wideChars[0], numChars, charWidths, lines, bounds, 1.0f);
    }

    ConstructMeshes(lines, bounds, scale);
}

void RndText::FitTextScroll() {
    BuildFontMaps(true);

    HX_VECTOR(Line) lines;
    HX_VECTOR(unsigned short) wideChars;
    int numChars = ConvertTextToWide(mText.c_str(), wideChars);
    float *charWidths = (float *)_alloca((numChars + 2) * sizeof(float));

    mNumLines = 0;
    mLineWidths.clear();
    mLineOffsets.clear();

    OnComputeCharWidths(&wideChars[0], charWidths, false);

    mWrapEnabled = false;
    float scrollCharWidth = 0.0f;

    Hmx::Rect bounds;
    float savedWidth = mWidth;
    if ((charWidths[numChars] > mWidth) || (mFitType == kFitScrollMarqueeWrapAlways)) {
        mWidth = 0.0f;
        mWrapEnabled = true;

        RndFontBase *font = mStyles[0].mFont;
        MILO_ASSERT(font, 2718);
        if (font) {
            unsigned short charCode;
            float w;
            DecodeUTF8(charCode, "8");
            font->CharAdvance(charCode, charCode, w);
            scrollCharWidth = (mStyles[0].mKerning + w) * mStyles[0].mSize;
        }
    }

    WrapText(&wideChars[0], numChars, charWidths, lines, bounds, 1.0f);
    ConstructMeshes(lines, bounds, 1.0f);

    if (mWrapEnabled) {
        mWidth = savedWidth;
        mScrollCopies = 1;
        mScrollTimer = 0.0f;
        mScrollSpeed = (mScrollRate * scrollCharWidth) * -0.001f;

        if (mFitType == kFitScrollMarqueeWrapAlways) {
            mScrollPos = savedWidth;
            mScrollOffset = savedWidth;
            mNumLines = mNumLines + 1;
            mTotalWidth = (mIndentation * (float)mNumLines) + charWidths[numChars];
            mLineWidths.push_back(mTotalWidth);
            mLineWidths.push_front(0.0f);
            mLineOffsets.push_back(mTotalWidth);
            mLineHeight = mTotalWidth;

            float f = mTotalWidth;
            while (mTotalWidth > 0.0f && !(f > mWidth)) {
                mScrollCopies += 1;
                f += mTotalWidth;
            }
            mCurScrollChars = -1;
            mScrollOutIndex = -1;
        } else {
            mScrollPos = 0.0f;
            mTotalWidth = charWidths[numChars];
            mLineHeight = 0.0f;
        }

        mScrollState = mScrollDelay;
        for (size_t i = 0; i < mFontMaps.size(); ++i) {
            mFontMaps[i]->SetupScrolling();
        }
    }
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
    Symbol fontMapClassName;

    if (font->ClassName() == RndFont::StaticClassName()) {
        fontMapClassName = FontMap::StaticClassName();
    } else if (font->ClassName() == RndFont3d::StaticClassName()) {
        fontMapClassName = FontMap3d::StaticClassName();
    } else {
        TheDebug.Fail(MakeString("Unknown Font type: %s", font->ClassName()), 0);
        fontMapClassName = FontMap::StaticClassName();
    }

    FontMapBase *result = nullptr;

    for (auto it = sFontMapCache.begin(); it != sFontMapCache.end(); ++it) {
        if ((*it)->ClassName() == fontMapClassName) {
            result = *it;
            sFontMapCache.erase(it);
            break;
        }
    }

    if (!result) {
        if (fontMapClassName == FontMap::StaticClassName()) {
            result = new FontMap();
        } else if (fontMapClassName == FontMap3d::StaticClassName()) {
            result = new FontMap3d();
        } else {
            TheDebug.Fail(MakeString("Unknown FontMap type: %s", fontMapClassName), 0);
            result = new FontMap();
        }
    }

    result->SetFont(font);
    result->ResetDisplayableChars();

    return result;
}

void RndText::ConstructMeshes(
    const HX_VECTOR(Line) &lines, const Hmx::Rect &bounds, float scale
) {
    // Store scale and number of lines
    mConstructScale = scale;
    // NEGATIVE RESULT: the image loads lines.mFinish (0x4) BEFORE lines.mStart
    // (0x0) for this size computation; spelling it `lines.end() - lines.begin()`
    // instead of `lines.size()` is exactly inert.  Two rows.
    mNumLinesRendered = lines.size();

    // Copy bounds using integer word copies (matching target codegen)
#ifdef HX_NATIVE
    mBounds.x = bounds.x;
    mBounds.y = bounds.y;
    mBounds.w = bounds.w;
    mBounds.h = bounds.h;
#else
    {
        const int *bsrc = (const int *)&bounds;
        int *bdst = (int *)&mBounds.x;
        bdst[0] = bsrc[0];
        bdst[1] = bsrc[1];
        bdst[2] = bsrc[2];
        bdst[3] = bsrc[3];
    }
#endif

    // Allocate meshes for each font map
    // NEGATIVE RESULT (loop rotation).  The image tests this loop once up front
    // -- `lwz r30, 0xa8(r3)` / `lwz r10, 0xac(r3)` / `cmplw cr6, r30, r10` /
    // `beq cr6, 0x826866c0`, all hoisted into the prologue -- and then falls
    // into a body that reloads the end each iteration.  We emit the
    // branch-to-bottom shape (`b` to the test).  Spelling the rotation out as
    // `it = begin(); if (it != end()) do { ... } while (it != end());` is
    // EXACTLY inert: MSVC un-rotates it straight back.  The SAME source shape
    // gives the image both lowerings -- the CleanupSyncMeshes loop at the end of
    // this function is branch-to-bottom on both sides -- so this is a scheduler
    // heuristic, not a source difference.  Eight rows, left alone.
    for (std::vector<FontMapBase *>::iterator it = mFontMaps.begin(); it != mFontMaps.end();
         ++it) {
        (*it)->AllocateMeshes(this, mFixedLength);
    }

    // Build character meshes if we have a font set
    if (mStyles[0].mFont != NULL) {
        StyleState state(this, scale);

        for (unsigned int i = 0; i < (unsigned int)lines.size(); i++) {
            const Line &line = lines[i];
            float yPos = line.mYPos;
            float xPos = line.mXStart;

            const unsigned short *cur = line.mStart;
            unsigned short prevChar = 0;
            int charIdx = 0;

            // `cur != line.mEnd`, NOT `cur < line.mEnd`.  The extra `&& cur <
            // line.mEnd` was decomp-introduced: it is redundant with the `!=`
            // and MSVC collapsed the pair to the signed `<`, which shows up as
            // `bge cr6` where the image guards with `cmplw cr6, r30, r10` /
            // `beq cr6` on the raw pointers (0x82686718).  The overshoot that
            // guard was defending against is already handled by the explicit
            // `if (cur > line.mEnd) break;` on the markup path below.
            while (cur != line.mEnd) {
                unsigned short ch = *cur;

                if (ch == 0x3c && mMarkup) {
                    cur = ParseMarkup(cur, state, ch);
                    // The `cur--` compensation is CONDITIONAL on ch, and there
                    // is no `cur > line.mEnd` bail-out in the image -- that was
                    // decomp-introduced.  0x82686758:
                    //     mr.  r11, r29        ; ch
                    //     beq  0x82686768      ; ch == 0 -> shared `if (ch)`
                    //     subi r30, r30, 0x2   ; cur--
                    if (ch != 0) {
                        cur--; // compensate for the cur++ below
                    }
                }

                if (ch != 0) {
                    FontMapBase *fontMap = mFontMaps[state.mFontMapIdx];
                    fontMap->SetupCharacter(
                        ch,
                        xPos,
                        yPos,
                        state,
                        prevChar,
                        mCircle,
                        mFitType,
                        mIndentation
                    );
                    prevChar = ch;
                    charIdx++;
                    // cur++ lives INSIDE this arm: `beq cr6, 0x826867b8` at
                    // 0x8268676c jumps PAST the `addi r30, r30, 0x2` straight to
                    // the loop test, so a ch of 0 (only reachable when
                    // ParseMarkup consumed a tag and already advanced cur) does
                    // not advance the cursor a second time.
                    cur++;
                }
            }
        }
    }

    // Cleanup and sync meshes
    for (std::vector<FontMapBase *>::iterator it = mFontMaps.begin(); it != mFontMaps.end();
         ++it) {
        (*it)->CleanupSyncMeshes();
    }
}

const unsigned short *
RndText::ParseMarkup(const unsigned short *cur, StyleState &state, unsigned short &ch) {
    // The cursor IS the parameter -- the image promotes r4 straight into r31
    // (`mr r31, r4`) and folds the pre-increment into `lhzu r11, 0x2(r31)`.
    // A separate `const unsigned short *cur = str;` local makes MSVC keep str in
    // r4 and emit `lhz r11, 0x2(r4)` + a lazy `addi r31, r4, 0x2` instead.
    unsigned int isClosing = (unsigned int)(*++cur - 0x2f) == 0;
    if (isClosing) {
        cur++;
    }
    ch = 0;

    float fVar12;
    auto& _ref0 = mStyles;
#ifdef HX_NATIVE
    if (WStrniCmp(cur, kTag_sup, 3) == 0) {
#else
    if (WStrniCmp(cur, (const unsigned short *)L"sup", 3) == 0) {
#endif
        // The `cur += 3` lands AFTER the if/else in the image: target emits the
        // `cmplwi cr6, r24, 0x0` / `beq` pair first and only reaches
        // `addi r31, r31, 0x6` on the join block at .L_82695854.
        if (isClosing) {
            fVar12 = state.mStyle->mSize;
        } else {
            fVar12 = state.mStyle->mSize * gSuperscriptScale;
        }
        cur += 3;
        goto set_size;
    }
#ifdef HX_NATIVE
    else if (WStrniCmp(cur, kTag_gtr, 3) == 0) {
#else
    else if (WStrniCmp(cur, (const unsigned short *)L"gtr", 3) == 0) {
#endif
        cur += 3;
        Style *style = state.mStyle;
        float scale;
        if (isClosing) {
            scale = style->mSize;
        } else {
            scale = style->mSize * gGuitarScale;
        }
        state.mSize = state.mBaseSize * scale;
        // if/else, NOT `zOff = gGuitarZOffset; if (isClosing) zOff = ...`: the image
        // branches on isClosing and loads exactly one of the two (target .L_826958b4
        // `beq cr6, .L_826958c0` with `lfs f0, 0x30(r11)` on the fallthrough), where
        // the seeded form loads gGuitarZOffset unconditionally before the branch.
        float zOff;
        if (isClosing) {
            zOff = style->mZOffset;
        } else {
            zOff = gGuitarZOffset;
        }
        state.mZOffset = zOff;
    }
#ifdef HX_NATIVE
    else if (WStrniCmp(cur, kTag_it, 2) == 0) {
#else
    else if (WStrniCmp(cur, (const unsigned short *)L"it", 2) == 0) {
#endif
        cur += 2;
        if (isClosing) {
            state.mItalics = state.mStyle->mItalics;
        } else {
            state.mItalics = state.mStyle->mItalics + 0.1f;
        }
    }
#ifdef HX_NATIVE
    else if (WStrniCmp(cur, kTag_color, 5) == 0) {
#else
    else if (WStrniCmp(cur, (const unsigned short *)L"color", 5) == 0) {
#endif
        cur += 5;
        if (isClosing) {
                        state.mTextColor = state.mStyle->mTextColor;
        } else {
            // Declared blue-first: MSVC lays the three out in reverse declaration
            // order, and the image's swscanf out-params are &r=0x58, &g=0x60,
            // &b=0x68 (r5/r6/r7 at .L_82695998).
            int b = 0, g = 0, r = 0;
            int a = (int)(state.mTextColor.alpha * 255.999f);
            cur++;
#ifdef HX_NATIVE
            int rgba[4] = {0, 0, 0, a};
            u16_scan_ints(cur, rgba, 4);
            r = rgba[0]; g = rgba[1]; b = rgba[2]; a = rgba[3];
#else
            swscanf((const wchar_t *)cur, L"%d %d %d %d", &r, &g, &b, &a);
#endif
            state.mTextColor.blue = (float)b * (1.0f / 255.0f);
            state.mTextColor.green = (float)g * (1.0f / 255.0f);
            state.mTextColor.red = (float)r * (1.0f / 255.0f);
            state.mTextColor.alpha = (float)a * (1.0f / 255.0f);
        }
    }
#ifdef HX_NATIVE
    else if (WStrniCmp(cur, kTag_hash, 2) == 0) {
#else
    else if (WStrniCmp(cur, (const unsigned short *)L"&#", 2) == 0) {
#endif
        cur += 2;
        int code = 0x3f;
#ifdef HX_NATIVE
        u16_scan_ints(cur, &code, 1);
#else
        swscanf((const wchar_t *)cur, L"%d", &code);
#endif
        ch = (unsigned short)code;
    }
#ifdef HX_NATIVE
    else if (WStrniCmp(cur, kTag_nobreak, 7) == 0) {
#else
    else if (WStrniCmp(cur, (const unsigned short *)L"nobreak", 7) == 0) {
#endif
        cur += 7;
        if (isClosing) {
            state.brk = true;
        } else {
            state.brk = false;
        }
    }
#ifdef HX_NATIVE
    else if (WStrniCmp(cur, kTag_alt, 3) == 0) {
#else
    else if (WStrniCmp(cur, (const unsigned short *)L"alt", 3) == 0) {
#endif
        cur += 3;

        if (isClosing) {
            unsigned short scanChar = *cur;
            while (scanChar != 0x3e && scanChar != 0) {
                cur++;
                scanChar = *cur;
            }
        }

        unsigned short markupChar = *cur;
        // Declared AFTER the closing scan and after markupChar is read: the image
        // emits `mr r25, r23` / `li r26, 0x4c` / `li r30, 0x1` at .L_82695b0c, i.e.
        // between the `lhz r11, 0x0(r31)` and the 0x32/0x39 range test.
        bool bBlacklight = false;
        unsigned int styleIdx = 1;
        if ((markupChar >= 0x32) && (markupChar <= 0x39)) {
            styleIdx = markupChar - 0x30;
            cur++;
        } else if ((markupChar == 0x62) || (markupChar == 0x42)) {
            bBlacklight = true;
            // `styleIdx &= ...`, not a ternary: the image emits `and r30, r10, r30`
            // at .L_82695b60 (styleIdx AND the 0/1 size predicate), where the
            // ternary lowers to `clrlwi r29, r10, 31`.
            styleIdx &= (unsigned int)(1 < (unsigned int)_ref0.size());
            Style *fallback = &_ref0[0];
            Style *stylePtr = &_ref0[styleIdx];
            if (stylePtr->mFont != nullptr) {
                fallback = stylePtr;
            }
            RndFontBase *font = fallback->mFont;
            if (font != nullptr) {
                if (FontMapIndex(font, true) == -1) {
                    FontMapBase *fm = AcquireFontMap(font);
                    fm->mBlacklight = true;
                    mFontMaps.push_back(fm);
                }
            }
            cur++;
        }

        styleIdx = styleIdx & -(isClosing == 0);

        // The INDEX is clamped, not the pointer: the image computes the address
        // once (`mulli r10, r10, 0x4c` / `add r4, r10, r11` at .L_82695c04) after a
        // branchless `subfc`/`subfe`/`and` select of the index, where a
        // pointer-valued if/else lowers to a real `cmplw`/`bge` and two addresses.
        unsigned int numStyles = (unsigned int)_ref0.size();
        if (styleIdx >= numStyles) {
            styleIdx = 0;
        }
        state.mStyle = &_ref0[styleIdx];

        memcpy(&state, state.mStyle, 0x34);

        // BUG FIX: the blacklight flag comes from Style::mBlacklight (Style+0x48,
        // Text.h:127), NOT StyleState::mFontColorOverride (StyleState+0x14). The
        // image reloads state.mStyle after the memcpy and reads `lbz r10, 0x48(r11)`
        // at .L_82695c18; we were reading `lbz r10, 0x14(r28)` off the freshly
        // memcpy'd StyleState instead, so <alt=b> styles whose Style had
        // mBlacklight set resolved to the wrong font map.
        //
        // BUG FIX: the no-font fallback does NOT write back to state.mStyle. The
        // image keeps state.mStyle pointing at the selected style and only
        // substitutes _ref0[0] for the FontMapIndex argument (.L_82695c40 loads
        // mStyles.begin into r11 and falls into the shared `addi r11, r11, 0x34` /
        // `lwz r4, 0xc(r11)`; there is no `stw` to 0x34(r28) on that path). We were
        // clobbering state.mStyle, which changed every later tag in the same run.
        Style *chosen = state.mStyle;
        bool blacklight = chosen->mBlacklight || bBlacklight;
        if (!chosen->mFont) {
            chosen = &_ref0[0];
        }
        state.mFontMapIdx = FontMapIndex(chosen->mFont, blacklight);

        fVar12 = state.mSize;
        goto set_size;
    }

    goto scan_close;

set_size:
    state.mSize = state.mBaseSize * fVar12;
scan_close:
    {
        // UNSIGNED, and the post-loop test is `!= 0`, not `== 0x3e`: the image ends
        // with `lhz`/`cmplwi` throughout (.L_82695c68 onward) and closes with
        // `cmplwi cr6, r11, 0x0` + `beq`. A `short` here gave lha/lhau/cmpwi.
        unsigned short scanChar = *cur;
        while (scanChar != 0x3e && scanChar != 0) {
            cur++;
            scanChar = *cur;
        }
        if (scanChar != 0) {
            cur++;
        }
    }
    return cur;
}

void RndText::UpdateText() {
    if (mFitType == kFitEllipsis) {
        FitTextJust();
        return;
    }
    if (mStyles[0].mSize > 0.0f && mWidth > 0.0f) {
        if (mFitType == kFitStretch) {
            FitTextEllipsis();
            return;
        }
        if (mFitType == kFitScrollPingPong
            || mFitType == kFitScrollMarqueeReset
            || mFitType == kFitScrollMarqueeWrap
            || mFitType == kFitScrollMarqueeWrapAlways) {
            for (unsigned int i = 0; i < (unsigned int)mStyles.size(); i++) {
                RndFontBase *font =
                    mStyles[i].mFont ? mStyles[i].mFont : mStyles[0].mFont;
                const char *fontName;
                if (font != 0) {
                    if (font->ClassName() != RndFont::StaticClassName()) {
                        fontName = font->Name();
                    } else {
                        continue;
                    }
                } else {
                    fontName = "NULL";
                }
                MILO_NOTIFY(
                    "%s %s requests scrolling, but uses a font that does not support it (%s)",
                    PathName(this), Name(), fontName
                );
                mFitType = kFitStretch;
                FitTextEllipsis();
                return;
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
            // Whole-Color copy, alpha included: the image stores all four words
            // (0x2c/0x30/0x34/0x38 of the mat) at 0x826992F0..0x82699330.  The
            // RESTORE loop below is deliberately asymmetric and puts back only
            // red/green/blue.
            savedColors[vlaIdx] = mat->GetColor();
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
                for (int i = 0; i < fontMap->NumMaterials(); i++) {
                    RndMat *mat = fontMap->Material(i);
                    mat->GetColor() = style.mFontColor;
                    mat->MarkDirty(1);
                }
            }
        }
    }

    // Update scroll offsets if wrapping is enabled
    if (mWrapEnabled) {
        UpdateScrollOffsets();
    }

    // Draw each mesh — text inherits the current camera (PanelDir's CamOverride).
    // On Xbox, text was drawn in 3D world space under the active camera.
    for (auto it = mFontMaps.begin(); it != mFontMaps.end(); ++it) {
        for (int i = 0; i < (*it)->NumMeshes(); i++) {
            RndMesh *mesh = (*it)->Mesh(i);
            if (mesh) {
#ifdef HX_NATIVE
                if (getenv("DC3_TEXT_DIAG")) {
                    static int sTD = 0;
                    String ascii = TextASCII();
                    if (sTD < 500 && ascii.length() > 0 && mesh->Verts().size() >= 4) {
                        sTD++;
                        RndCam *cc = RndCam::Current();
                        Vector2 scr(0, 0);
                        Vector3 wv;
                        Multiply(mesh->Verts()[0].pos, mesh->WorldXfm(), wv);
                        if (cc) cc->WorldToScreen(wv, scr);
                        Vector3 bbMin = mesh->Verts()[0].pos, bbMax = bbMin;
                        int nv = mesh->Verts().size();
                        for (int vi = 1; vi < nv; vi++) {
                            const Vector3 &p = mesh->Verts()[vi].pos;
                            bbMin.x = p.x < bbMin.x ? p.x : bbMin.x;
                            bbMin.y = p.y < bbMin.y ? p.y : bbMin.y;
                            bbMin.z = p.z < bbMin.z ? p.z : bbMin.z;
                            bbMax.x = p.x > bbMax.x ? p.x : bbMax.x;
                            bbMax.y = p.y > bbMax.y ? p.y : bbMax.y;
                            bbMax.z = p.z > bbMax.z ? p.z : bbMax.z;
                        }
                        RndMat *mat = mesh->Mat();
                        fprintf(stderr,
                                "DC3_TEXT_DIAG '%.24s' cam=%s scr=(%.3f,%.3f) nv=%d "
                                "bbox=(%.2f,%.2f,%.2f) mat=%s\n",
                                ascii.c_str(), cc ? PathName(cc) : "<null>", scr.x, scr.y,
                                nv, bbMax.x - bbMin.x, bbMax.y - bbMin.y, bbMax.z - bbMin.z,
                                mat ? PathName(mat) : "<null>");
                    }
                }
#endif
                // mLineHeight / mScrollCopies, NOT mStyles[0].mSize / 0.  The
                // image loads `lfs f1, -0xb4(r28)` and `lwz r5, -0xb0(r28)`
                // straight off `this` at 0x826994E4 and 0x826994F4 (object
                // offsets 0x58 and 0x5c, since the body's `this` is
                // object+0x10c).  Those are the marquee repeat spacing and copy
                // count that FitTextScroll sets -- mLineHeight is a misnomer,
                // it is assigned mTotalWidth for a wrapping marquee and 0.0f
                // otherwise, right beside mScrollCopies.  Passing a literal 0
                // copy count meant DrawMesh's repeat loop never ran, so a
                // marquee drew exactly one copy and left a gap instead of
                // tiling across the label.
                if (!(!sBlacklightModeEnabled || !(*it)->mBlacklight ||
                    TheUI->DisableScreenBlacklight())) {
                    QueueBlacklightPacket(mesh, mLineHeight, mScrollCopies);
                } else {
                    DrawMesh(mesh, mLineHeight, mScrollCopies);
                }
            }
        }
    }

    // Restore material colors (r, g, b only — not alpha)
    if (hasOverride) {
        vlaIdx = 0;
        auto fontMapsEnd = mFontMaps.end();
        for (auto it = mFontMaps.begin(); fontMapsEnd != it; ++it) {
            FontMapBase *fontMap = *it;
            for (int i = 0; i < fontMap->NumMaterials(); i++) {
                RndMat *mat = fontMap->Material(i);
                Hmx::Color &color = mat->GetColor();
                color.red = savedColors[vlaIdx].red;
                color.green = savedColors[vlaIdx].green;
                color.blue = savedColors[vlaIdx].blue;
                mat->MarkDirty(1);
                vlaIdx++;
            }
        }
    }
}

void RndText::SizeCheck() {
#ifdef HX_NATIVE
    // On Xbox this hook only emitted an "oversized font" warning; the native
    // port repurposed it to re-lay-out the text every frame. That is safe for
    // static labels but destructive for the scrolling fit types: FitTextScroll()
    // resets mScrollTimer to 0 and mScrollPos to the start offset, so redoing it
    // once per frame pins a marquee at frame 0 forever. (DC3 main_screen's
    // motd.lbl therefore never scrolled and permanently showed the message with
    // its head parked under the authored left-edge gradient mask.)
    //
    // Once a scrolling label has been fitted (mWrapEnabled, set only by
    // FitTextScroll) leave it alone — UILabel::LabelUpdate() and the LOAD/COPY
    // paths still call UpdateText() explicitly whenever the string or a layout
    // property actually changes.
    if (!mWrapEnabled)
        UpdateText();
#else
    static float sLastHeight;
    static RndText *sLastText;

    StyleState ss(this, mConstructScale);
    for (FontMapBase **it = mFontMaps.begin(); it != mFontMaps.end(); ++it) {
        RndFontBase *font = (*it)->Font();
        if (font != nullptr && font->BitmapFont()) {
            for (int i = 0; i < (*it)->NumMeshes(); i++) {
                RndMesh *mesh = (*it)->Mesh(i);
                if (mesh != nullptr) {
                    float screenHeight;
                    if (!CalcScreenHeight(
                            ss.mSize * font->AspectRatio(), mesh, screenHeight
                        )) {
                        return;
                    }
                    float fontSize = font->FontUnit() * font->AspectRatio();
                    float cap = 127.5f;
                    if (screenHeight < 127.5f) {
                        cap = screenHeight;
                    }
                    if (fontSize * 1.25f >= cap) {
                        return;
                    }
                    if (sLastText == this && sLastHeight >= screenHeight) {
                        return;
                    }
                    int heightInt = (int)screenHeight;
                    int productInt = (int)fontSize;
                    MILO_NOTIFY(
                        "oversized: %s font: %s token:'%s' text:'%s' %d < %d",
                        PathName(this),
                        font->Name(),
                        TextToken(),
                        mText,
                        productInt,
                        heightInt
                    );
                    sLastHeight = screenHeight;
                    sLastText = this;
                    return;
                }
            }
        }
    }
#endif
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
    auto& maxWidth = mWidth;
    if (mFitType != kFitScrollMarqueeWrapAlways) {
        return;
    }
    SetText(str.c_str());
    FitTextScroll();
    mScrollPos = 0.0f;
    mScrollOffset = 0.0f;
    float width = maxWidth;
    while (width >= *mLineWidths.begin()) {
        mCurScrollChars++;
        if (mCurScrollChars >= mNumLines) {
            mCurScrollChars = 0;
        }
        if (*mLineWidths.begin() == mTotalWidth) {
            mScrollOffset += maxWidth;
        }
        if ((unsigned int)mNumLines == (unsigned int)mLineWidths.size()) {
            mLineWidths.insert(mLineWidths.end(), *mLineWidths.begin());
        }
        mLineWidths.erase(mLineWidths.begin());
        width = maxWidth - mScrollOffset;
    }
    mScrollTimer = mScrollState;
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
    RndFont3d::CharInfo *info = mFont->GetCharInfo(us);
    if (info != nullptr && info->mMesh != nullptr) {
        mDisplayableChars++;
    }
}

void RndText::FontMap3d::AllocateMeshes(RndText *text, int fixedLength) {
    unsigned int targetSize = 0;
    if (mFont != NULL) {
        targetSize = fixedLength;
        if (fixedLength == 0) {
            targetSize = mDisplayableChars;
        }
    }

    unsigned int oldSize = (unsigned int)mMeshes.size();

    if (targetSize < oldSize) {
        unsigned int i = targetSize;
        do {
            RndMesh *mesh = mMeshes[i];
            if (mesh != NULL) {
                delete mesh;
            }
            i++;
        } while (i < (unsigned int)mMeshes.size());
    }

    RndMesh *nullMesh = NULL;

    if (targetSize < oldSize) {
        mMeshes.erase(mMeshes.begin() + targetSize, mMeshes.end());
    } else {
        mMeshes.insert(mMeshes.end(), (int)targetSize - (int)oldSize, nullMesh);
    }

    if ((unsigned int)mMeshes.size() > 0) {
        unsigned int i = 0;
        do {
            if ((int)i >= (int)oldSize) {
                mMeshes[i] = Hmx::Object::New<RndMesh>();
            }
            RndMesh *mesh = mMeshes[i];
            RndTransformable *parent = NULL;
            if (text != NULL) {
                parent = text;
            }
            mesh->SetTransParent(parent, false);
            mesh->SetTransConstraint(RndTransformable::kConstraintNone, NULL, false);
            mesh->SetMat(mFont->Mat());
            mesh->SetShowing(true);
            i++;
        } while (i < (unsigned int)mMeshes.size());
    }

    mMeshCursor = mMeshes.data();
}

void RndText::FontMap3d::CleanupSyncMeshes() {
#ifdef HX_NATIVE
    if (mMeshes.empty())
        return;
#endif
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
    float circle,
    FitType fitType,
    float leading
) {
    if ((fitType == kFitScrollMarqueeWrapAlways) && ((charCode & 0xffff) == 10)) {
        xPos = leading + xPos;
        return;
    }

    int page = mFont->CharPage(charCode);
    if (page < 0) return;
    Page &pg = *(mPages[page]);

    float charW, advW;
    if (!mFont->CharWidthAdvanceCoords(charCode, charW, advW, pg.mVertStart[0].tex, pg.mVertStart[2].tex)) {
        return;
    }

    xPos += (mFont->Kerning(prevChar, charCode) + state.mKerning) * state.mSize;

    // charW is REUSED as the glyph width and then as the scaled width.  It is
    // address-taken (the out-param above), so every assignment to it is a store
    // to its frame slot and every read is a reload -- which is exactly what the
    // image does:
    //     stfs f0,  0x50(r1)   ; charW = advW        (0x25a4)
    //     stfs f12, 0x50(r1)   ; charW *= state.mSize (0x25dc)
    //     lfs  f13, 0x50(r1)   ; reload at vert[2].x  (0x266c)
    //     lfs  f0,  0x50(r1)   ; reload at vert[3].x  (0x2680)
    // Separate `width` / `scaledW` locals stay in FPRs and lose all four rows.
    if (charW <= 0.0f) {
        charW = advW;
    }

    float centerOfs = 0.0f;
    if (mFont->IsMonospace()) {
        centerOfs = Max((advW - charW) * 0.5f, 0.0f);
    }

    float scaledCenter = state.mSize * centerOfs;
    charW = state.mSize * charW;
    if (charW <= 0.0f) return;

    float z0 = yPos + state.mZOffset * state.mSize;
    // The image parks state.mSize in a callee-saved FPR across the virtual
    // AspectRatio() call (`fmr f26, f0` at 0x25f4) instead of reloading it
    // afterwards; naming it here is what produces that copy.
    float size = state.mSize;
    auto _tmp1 = mFont->AspectRatio();
    float italics = state.mItalics * size;
    // NOTE (bug 1B fix): keep glyph height CONSTANT (z0 - z1 == aspect*size)
    // regardless of yPos. Permuter sweep f5f704d6 flipped this subtraction to
    // `_tmp1*state.mSize - z0`, which reflects the quad about aspect*size/2 and
    // COLLAPSES it to zero height for vertically-centered text (yPos == th/2 ==
    // aspect*size/2 for a single centered line) — making all menu/HUD text
    // invisible. Subtraction is not commutative; the swap was match-neutral
    // (objdiff 83.8% either way) but behaviorally wrong. Reverted to og form.
    // NEGATIVE RESULT: the image keeps `_tmp1 * size` and the subtraction apart
    // (`fmuls f12, f1, f26` at 0x261c, `fsubs f12, f27, f12` at 0x2638) where we
    // contract to one fnmsubs.  Splitting it into a named `aspectH` temporary is
    // exactly neutral -- MSVC re-fuses across the statement boundary.  Two rows.
    float z1 = z0 - _tmp1 * size;

    // xPos is read straight out of the reference each time (`lfs f13, 0x0(r29)`
    // at 0x2618 / 0x2648 / 0x2660 / 0x2688); caching it in a local `x` folds
    // those four reloads into one.
    pg.mVertStart[0].pos.Set(italics + scaledCenter + xPos, 0.0f, z0);
    pg.mVertStart[1].pos.Set(scaledCenter + xPos - italics, 0.0f, z1);
    pg.mVertStart[2].pos.Set(scaledCenter + xPos - italics + charW, 0.0f, z1);
    pg.mVertStart[3].pos.Set(italics + scaledCenter + charW + xPos, 0.0f, z0);

    if (circle != 0.0f) {
        float midX = (pg.mVertStart[3].pos.x - pg.mVertStart[1].pos.x) * 0.5f
            + pg.mVertStart[1].pos.x;
        Transform xfm = XfmOnCircleEdge(circle, midX);
        // Three separate `fmuls` followed by three `fsubs`, not three fused
        // `fnmsubs` (0x26cc-0x2700): the image scales the whole basis row into
        // a temporary first, then subtracts it componentwise.  Written as
        // `xfm.v.x -= xfm.m.x.x * midX;` MSVC contracts each line into one
        // fnmsubs and the row count drops by three.
        Vector3 offset;
        Scale(xfm.m.x, midX, offset);
        Subtract(xfm.v, offset, xfm.v);
        Multiply(pg.mVertStart[0].pos, xfm, pg.mVertStart[0].pos);
        Multiply(pg.mVertStart[1].pos, xfm, pg.mVertStart[1].pos);
        Multiply(pg.mVertStart[2].pos, xfm, pg.mVertStart[2].pos);
        Multiply(pg.mVertStart[3].pos, xfm, pg.mVertStart[3].pos);
    }

    // One `lwz r11, 0x8(r31)` at 0x82691240 serves all six statements below.
    // Spelled `pg.mVertStart[...]` MSVC cannot prove the float stores miss the
    // pointer member and reloads it before each one (four extra rows); the
    // integer struct copies further down DO reload in the image too, so they
    // deliberately keep the member spelling.
    RndMesh::Vert *verts = pg.mVertStart;
    verts[1].tex.y = verts[2].tex.y;
    verts[1].tex.x = verts[0].tex.x;
    verts[3].tex.y = verts[0].tex.y;
    verts[3].tex.x = verts[2].tex.x;

    verts[0].norm.Set(0.0f, -1.0f, 0.0f);
    verts[3].norm = verts[0].norm;
    pg.mVertStart[2].norm = pg.mVertStart[3].norm;
    pg.mVertStart[1].norm = pg.mVertStart[2].norm;

    pg.mVertStart[3].color = state.mTextColor;
    pg.mVertStart[2].color = pg.mVertStart[3].color;
    pg.mVertStart[1].color = pg.mVertStart[2].color;
    pg.mVertStart[0].color = pg.mVertStart[1].color;

    pg.mVertStart += 4;

    xPos = advW * state.mSize + xPos;
}

static const float _kFloat0_0 = 0.0f;
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
    if (width <= _kFloat0_0) {
        width = advance;
    }

    // Monospace centering
    float centerOffset = _kFloat0_0;
    if (mFont->IsMonospace()) {
        centerOffset = Max((advance - width) * 0.5f, _kFloat0_0);
    }

    float scaledCenter = state.mSize * centerOffset;

    // Same reuse as the 2d overload: `width` is the CharWidthAdvanceMesh
    // out-param, so it lives in a frame slot and the scaled width is written
    // back over it (`fmuls f12, f0, f12` / `stfs f12, 0x50(r1)` at 0x8268ffd4)
    // and reloaded at the circle-edge midpoint (`lfs f13, 0x50(r1)`).
    width = state.mSize * width;
    if (width <= _kFloat0_0)
        return;

    yPos += state.mZOffset * state.mSize;

    if (charMesh && mMeshCursor != mMeshes.end()) {
        RndMesh *mesh = *mMeshCursor;
        mMeshCursor++;
        mesh->SetGeomOwner(charMesh);

        // The whole position vector is scaled (three `fmuls` by state.mSize,
        // 0x8268ff58/5c/64), then z and x are adjusted; dead-store elimination
        // leaves exactly one store per component, in the order y (0x94), z
        // (0x98), x (0x90).  The CharOriginOffset() result is consumed straight
        // out of the returned sret pointer (`lwz r9, 0x0(r3)` at 0x82690018) --
        // naming it `Vector3 origin` makes MSVC address the buffer through its
        // own `addi r11, r1, 0xa0` and forward origin.x past the copy.
        Transform xfm;
        xfm.v = mFont->CharOriginOffset();
        xfm.v *= state.mSize;
        // NEGATIVE RESULT: the image keeps z's scale and its +yPos apart
        // (`fmuls f10, f0, f10` at 0x8268ff5c, `fadds f0, f10, f30` at
        // 0x8268ff6c) where we contract to one fmadds.  Spelling the scale as
        // Scale(xfm.v, state.mSize, xfm.v) instead of `*=` is exactly inert.
        xfm.v.z += yPos;
        xfm.v.x = xfm.v.x + scaledCenter + xPos;

        // Scale matrix by cell height
        float cellHeight = mFont->FontUnitInverse() * state.mSize;
        // NEGATIVE RESULT: the image writes the three diagonal slots (0x60,
        // 0x74, 0x88) BEFORE the six zeros, and materialises the zero as
        // `fmuls f0, f0, f31` -- cellHeight times the 0.0 it already holds in a
        // callee-saved FPR (0x82690064) -- rather than storing the literal.
        // Writing the nine fields as individual assignments in the image's
        // order is EXACTLY inert (96.0% and an identical row table): MSVC sinks
        // and groups the stores by value, not by statement order.  The source
        // shape that produces a multiply by zero here is still unidentified.
        xfm.m.x.Set(cellHeight, _kFloat0_0, _kFloat0_0);
        xfm.m.y.Set(_kFloat0_0, cellHeight, _kFloat0_0);
        xfm.m.z.Set(_kFloat0_0, _kFloat0_0, cellHeight);

        if (size != _kFloat0_0) {
            float circlePos = width * 0.5f + xfm.v.x;
            Transform circleXfm = XfmOnCircleEdge(size, circlePos);
            xfm.v.x -= circlePos;
            Multiply(xfm, circleXfm, xfm);
        }

        // mLocalXfm, NOT mWorldXfm.  The image materialises the mesh's
        // RndTransformable base once and addresses both uses off it:
        //     addi r31, r28, 0x40      ; (RndTransformable*)mesh
        //     addi r3,  r31, 0x8       ; &mLocalXfm  (mesh + 0x48)
        //     li   r5,  0x40
        //     bl   memcpy
        //     lbz  r11, 0xfd(r28)      ; mesh->mDirty
        //     bne  ...
        //     mr   r3,  r31            ; SetDirty_Force on the same base
        // We were writing mWorldXfm (RndTransformable + 0x48 = mesh + 0x88),
        // which the very next WorldXfm_Force() recomputes from the local
        // transform -- so the glyph placement this function computes was being
        // thrown away on the next sync.  Confirmed at the instruction level:
        // we now emit `addi r3, r29, 0x48` (mesh + 0x48) where we used to emit
        // `addi r3, r29, 0x88`; target's `addi r31, r28, 0x40` + `addi r3, r31,
        // 0x8` is the same address.
        //
        // NEGATIVE RESULT on the r31 hoist itself: naming the upcast so the
        // base is materialised once -- either `RndTransformable *t = mesh;` or
        // `RndTransformable &t = *mesh;` -- REGRESSES 84.1 -> 83.2 (158 -> 161
        // instructions, +3 inserts).  The named upcast makes MSVC keep the
        // pointer in a frame slot across the XfmOnCircleEdge/Multiply calls
        // instead of folding it into the two addressing modes.  Both spellings
        // measured, both identical; lever exhausted, leave the two-row residual.
        memcpy(&mesh->mLocalXfm, &xfm, sizeof(Transform));
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
