#include "nativeui_host/GameFonts.h"

#include "obj/Dir.h"
#include "obj/DirLoader.h"
#include "rndobj/Bitmap.h"
#include "rndobj/Font.h"
#include "rndobj/Mat.h"
#include "rndobj/Tex.h"
#include "ui/UILabelDir.h"
#include "utl/FilePath.h"

#include <algorithm>
#include <cstdio>

namespace nativeui_host {

    namespace {

        // The rows of a glyph's cell that hold ink, read from the page's
        // texels: first and last row (0 = cell top) with alpha above a
        // threshold. False if the glyph or the texels are not available.
        bool InkRows(
            const RndFont *font, RndBitmap &bitmap, int page, unsigned short c,
            float cellHeight, int &firstRow, int &lastRow
        ) {
            if (font->CharPage(c) != page) {
                return false;
            }
            float width = 0.0f, advance = 0.0f;
            Vector2 uvMin, uvMax;
            if (!font->CharWidthAdvanceCoords(c, width, advance, uvMin, uvMax)) {
                return false;
            }
            const int x0 = (int)(uvMin.x * bitmap.Width() + 0.5f);
            const int x1 = std::min((int)(uvMax.x * bitmap.Width() + 0.5f), (int)bitmap.Width());
            const int y0 = (int)(uvMin.y * bitmap.Height() + 0.5f);
            const int rows = std::min((int)(cellHeight + 0.5f), (int)bitmap.Height() - y0);
            firstRow = -1;
            lastRow = -1;
            for (int row = 0; row < rows; row++) {
                bool ink = false;
                for (int x = x0; x < x1 && !ink; x++) {
                    unsigned char r, g, b, a;
                    bitmap.PixelColor(x, y0 + row, r, g, b, a);
                    ink = a >= 96;
                }
                if (ink) {
                    if (firstRow < 0) {
                        firstRow = row;
                    }
                    lastRow = row;
                }
            }
            return firstRow >= 0;
        }

        // Cap top, baseline and descender bottom as fractions of the cell,
        // from the ink of flat-topped, flat-footed capitals and of descending
        // lower-case letters on page 0.
        bool MeasureInk(const RndFont *font, BitmapFontData &data) {
            RndTex *tex = font->ValidTexture(0);
            if (!tex) {
                return false;
            }
            RndBitmap bitmap;
            tex->LockBitmap(bitmap, 3);
            bool measured = false;
            if (bitmap.Pixels() && bitmap.Width() > 0 && bitmap.Height() > 0) {
                int capTop = -1, baseline = -1, descender = -1;
                for (unsigned short c : { (unsigned short)'H', (unsigned short)'E',
                                          (unsigned short)'T', (unsigned short)'I' }) {
                    int first, last;
                    if (InkRows(font, bitmap, 0, c, data.cellHeight, first, last)) {
                        capTop = first;
                        baseline = last + 1;
                        break;
                    }
                }
                for (unsigned short c : { (unsigned short)'p', (unsigned short)'g',
                                          (unsigned short)'y', (unsigned short)'q',
                                          (unsigned short)'j', (unsigned short)'Q',
                                          (unsigned short)',' }) {
                    int first, last;
                    if (InkRows(font, bitmap, 0, c, data.cellHeight, first, last)) {
                        descender = last + 1;
                        break;
                    }
                }
                if (capTop >= 0 && baseline > capTop) {
                    data.capTop = capTop / data.cellHeight;
                    data.baseline = baseline / data.cellHeight;
                    data.descenderBottom = descender > baseline
                        ? descender / data.cellHeight
                        : std::min(1.0f, data.baseline + 0.2f);
                    measured = true;
                }
            }
            tex->UnlockBitmap();
            return measured;
        }

    }

    BitmapFontData BuildFontData(RndFont *font, bool coloured, GameFontInfo *info) {
        BitmapFontData data;
        // Every number is asked of the font itself, as RndText asks it: a
        // font that shares another's atlas (a material variation) answers
        // for itself and passes on what it shares (the glyph rectangles).
        const RndFont *owner = font;
        const RndFont *shared = dynamic_cast<const RndFont *>(font->DataOwner());
        data.cellWidth = owner->FontUnit();
        data.cellHeight = owner->FontUnit() * owner->AspectRatio();
        data.monospace = owner->IsMonospace();
        data.pagesCarryColour = coloured;
        // Kerning(a, b) is base kerning plus the pair's table entry; a pair
        // that cannot be in the table gives the base alone.
        data.baseKerning = owner->Kerning(0, 0);

        for (int page = 0; page < owner->NumMats(); page++) {
            RndTex *tex = owner->ValidTexture(page);
            if (!tex && shared) {
                tex = shared->ValidTexture(page);
            }
            data.pages.push_back(tex);
        }

        const std::vector<unsigned short> &chars = owner->Chars();
        for (unsigned short c : chars) {
            float width = 0.0f, advance = 0.0f;
            Vector2 uvMin, uvMax;
            if (!owner->CharWidthAdvanceCoords(c, width, advance, uvMin, uvMax)) {
                continue; // listed but never laid out: not a glyph
            }
            int page = owner->CharPage(c);
            if (page < 0 && shared) {
                page = shared->CharPage(c);
            }
            if (page < 0 || page >= (int)data.pages.size()) {
                continue;
            }
            BitmapGlyph glyph;
            glyph.page = page;
            // RndText: a glyph with no width of its own is as wide as its advance.
            // A glyph with no width of its own (the space) has no ink: it
            // only advances the pen.
            glyph.width = width > 0.0f ? width : 0.0f;
            glyph.advance = advance;
            glyph.u0 = uvMin.x;
            glyph.v0 = uvMin.y;
            glyph.u1 = uvMax.x;
            glyph.v1 = uvMax.y;
            data.glyphs[(char32_t)c] = glyph;
        }

        std::vector<RndFontBase::KernInfo> pairs;
        owner->GetKerning(pairs);
        for (const RndFontBase::KernInfo &pair : pairs) {
            const float extra =
                owner->Kerning(pair.mFirstChar, pair.mSecondChar) - data.baseKerning;
            if (extra != 0.0f) {
                data.pairKerning[(std::uint32_t)pair.mFirstChar
                                 | ((std::uint32_t)pair.mSecondChar << 16)] = extra;
            }
        }

        const bool measured = MeasureInk(owner, data);
        if (info) {
            info->fontName = owner->Name();
            info->cellWidth = data.cellWidth;
            info->cellHeight = data.cellHeight;
            info->pages = (int)data.pages.size();
            RndTex *page0 = data.pages.empty() ? nullptr : data.pages[0];
            info->pageWidth = page0 ? page0->Width() : 0;
            info->pageHeight = page0 ? page0->Height() : 0;
            info->glyphs = (int)data.glyphs.size();
            info->kerningPairs = (int)data.pairKerning.size();
            info->baseKerning = data.baseKerning;
            info->monospace = data.monospace;
            info->carriesColour = coloured;
            info->measuredFromAtlas = measured;
            info->capTop = data.capTop;
            info->baseline = data.baseline;
            info->descenderBottom = data.descenderBottom;
        }
        return data;
    }

    GameFont::~GameFont() {
        source_.reset();
        delete dir_;
    }

    std::unique_ptr<GameFont> LoadGameFont(const char *resourceName, std::string *why) {
        char path[256];
        snprintf(path, sizeof(path), "ui/resource/fonts/%s.milo", resourceName);
        ObjectDir *dir = DirLoader::LoadObjects(FilePath(path), nullptr, nullptr);
        if (!dir) {
            if (why) {
                *why = std::string("could not load ") + path;
            }
            return nullptr;
        }
        // The font a label draws with: the resource is a UILabelDir, and
        // UILabel::SetFontMat asks it for the generated font of a material
        // variation ("" is the default one).  The directory also holds the
        // artist's source font ("... old kerning.font"), whose glyph table
        // does NOT describe the generated atlas -- measured: drawing with it
        // puts '&' where 'C' should be.
        UILabelDir *labelDir = dynamic_cast<UILabelDir *>(dir);
        RndFontBase *base = labelDir ? labelDir->FontObj(Symbol("")) : nullptr;
        RndFont *font = dynamic_cast<RndFont *>(base);
        if (!font) {
            if (why) {
                *why = std::string(path)
                    + (!labelDir ? " is not a UILabelDir"
                           : !base ? " has no generated font"
                                   : " holds a 3D font (a mesh per character), not a bitmap font");
            }
            delete dir;
            return nullptr;
        }

        std::unique_ptr<GameFont> loaded(new GameFont());
        loaded->dir_ = dir;
        loaded->info_.resource = resourceName;
        loaded->info_.path = path;
        // The pad-button and icon atlases keep colour in their RGB; the text
        // faces are drawn as a flat tint.
        const std::string name(resourceName);
        const bool coloured = name.find("icon") != std::string::npos;
        loaded->source_.reset(
            new BitmapGlyphSource(BuildFontData(font, coloured, &loaded->info_))
        );
        return loaded;
    }

    GameFonts::GameFonts() {}
    GameFonts::~GameFonts() { Unload(); }

    bool GameFonts::Load() {
        if (Loaded()) {
            return Find(nativeui::kDc3MenuFont) != nullptr;
        }
        static const struct {
            nativeui::FontId id;
            const char *resource;
        } kTable[] = {
            { nativeui::kDc3MenuFont, "eagle-light(47)" },
            { kDc3HeavyFont, "eagle_light_heavy" },
            { kDc3IconFont, "button_icon" },
        };
        for (const auto &row : kTable) {
            std::string why;
            std::unique_ptr<GameFont> font = LoadGameFont(row.resource, &why);
            if (!font) {
                printf("nativeui: font %s not loaded: %s\n", row.resource, why.c_str());
                continue;
            }
            set_.Set(row.id, &font->Source());
            if (row.id == nativeui::kDc3MenuFont) {
                set_.SetDefault(&font->Source());
            }
            fonts_.push_back(Slot { row.id, std::move(font) });
        }
        return Find(nativeui::kDc3MenuFont) != nullptr;
    }

    void GameFonts::Unload() {
        set_ = FontSet();
        fonts_.clear();
    }

    const GameFont *GameFonts::Find(nativeui::FontId font) const {
        for (const Slot &slot : fonts_) {
            if (slot.id == font) {
                return slot.font.get();
            }
        }
        return nullptr;
    }

    std::vector<GameFontInfo> GameFonts::Table() const {
        std::vector<GameFontInfo> table;
        for (const Slot &slot : fonts_) {
            table.push_back(slot.font->Info());
        }
        return table;
    }

}
