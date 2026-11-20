// GameFonts: the game's own bitmap fonts as GlyphSources.
//
// HOW THE GAME LOADS A FONT (what Load() does too)
//   A UILabel names a font RESOURCE ("eagle-light(47)"); ResourceDirPtr<
//   UILabelDir>::SetName turns it into ui/resource/fonts/<name>.milo (the
//   resources_path of HamLabel in ui/ui_objects.dta) and loads that directory
//   with the game's DirLoader. The directory is a UILabelDir whose subdir
//   chain leads to the atlas: ui/resource/fonts/gen/<name>.milo_xbox (1 KB)
//   -> <language>/<name>.milo (3 KB) -> ../default/<name>.milo (1.4-1.5 MB,
//   the RndFont, its RndMat pages and their DXT5 textures). The label then
//   asks the directory for its font (UILabelDir::FontObj). LoadGameFont() does
//   the same two steps -- DirLoader::LoadObjects on the resource's .milo path,
//   then the directory's bitmap RndFont -- and copies the font's numbers into
//   a BitmapFontData (BitmapGlyphSource.h). Nothing here parses a file.
//
//   RndFont3d faces (eagle-light-3d*.milo: a mesh per character) are not
//   bitmap fonts and cannot be drawn through the canvas; LoadGameFont() says
//   so and returns null for them.
//
// WHICH FONT IS WHICH nativeui::FontId
//   nativeui::kDc3MenuFont (1)  eagle-light(47)   the face of today's song select: its
//                               list rows and headings (ham_list_ribbon/main_ribbon and
//                               header_ribbon, and song_select.milo's own labels, all
//                               name ../fonts/eagle-light(47).milo)
//   kDc3HeavyFont (2)           eagle_light_heavy the same face, heavier
//   kDc3IconFont (3)            button_icon       the coloured pad-button glyphs of the
//                               game's help bars (a COLOURED atlas: drawn kColor)
//
// Load() blocks while the files are read (about 1.5 MB each): call it from a
// panel's load step or a loading screen, not from a frame that must not hitch.
// The fonts stay loaded until Unload() or destruction.
#pragma once

#include "nativeui/Theme.h"
#include "nativeui_host/BitmapGlyphSource.h"
#include "nativeui_host/FontSet.h"

#include <memory>
#include <string>
#include <vector>

class ObjectDir;
class RndFont;

namespace nativeui_host {

    inline constexpr nativeui::FontId kDc3HeavyFont = nativeui::kDc3HeavyFont;
    inline constexpr nativeui::FontId kDc3IconFont { 3 };

    // What was found in a font resource, for the docs table and the tests.
    struct GameFontInfo {
        std::string resource; // "eagle-light(47)"
        std::string path; // the .milo path given to the game's loader
        std::string fontName; // the RndFont object's name
        float cellWidth = 0.0f, cellHeight = 0.0f; // texels
        int pages = 0;
        int pageWidth = 0, pageHeight = 0; // texels, page 0
        int glyphs = 0;
        int kerningPairs = 0;
        float baseKerning = 0.0f;
        bool monospace = false;
        bool carriesColour = false;
        bool measuredFromAtlas = false; // capTop/baseline/descender read off the texels
        float capTop = 0.0f, baseline = 0.0f, descenderBottom = 0.0f; // fractions of the cell
    };

    // One loaded font resource. Owns the loaded directory (and with it the
    // textures the source's pages point at).
    class GameFont {
    public:
        ~GameFont();
        const BitmapGlyphSource &Source() const { return *source_; }
        const GameFontInfo &Info() const { return info_; }

    private:
        friend std::unique_ptr<GameFont> LoadGameFont(const char *, std::string *);
        ObjectDir *dir_ = nullptr;
        std::unique_ptr<BitmapGlyphSource> source_;
        GameFontInfo info_;
    };

    // Loads the font resource `resourceName` the way a UILabel does. Null (and
    // a reason in *why) if the file is missing, holds no bitmap font, or is a
    // 3D font.
    std::unique_ptr<GameFont> LoadGameFont(const char *resourceName, std::string *why = nullptr);

    // Copies a loaded RndFont's numbers into a BitmapFontData. `coloured`
    // decides how its pages are drawn; measures the ink rows off page texels
    // when the texture still has them (info, if given, records whether it did).
    BitmapFontData BuildFontData(RndFont *font, bool coloured, GameFontInfo *info = nullptr);

    // The fonts the native UI uses, by FontId.
    class GameFonts {
    public:
        GameFonts();
        ~GameFonts();
        GameFonts(const GameFonts &) = delete;
        GameFonts &operator=(const GameFonts &) = delete;

        // Loads every font of the table above that exists. True if the menu
        // font (kDc3MenuFont) loaded; the others are optional.
        bool Load();
        void Unload();
        bool Loaded() const { return !fonts_.empty(); }

        const FontSet &Fonts() const { return set_; }
        const GameFont *Find(nativeui::FontId font) const;
        // One row per loaded font.
        std::vector<GameFontInfo> Table() const;

    private:
        struct Slot {
            nativeui::FontId id;
            std::unique_ptr<GameFont> font;
        };
        std::vector<Slot> fonts_;
        FontSet set_;
    };

}
