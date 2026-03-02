// TextureExporter — Iterates RndTex objects in an ObjectDir and writes PNGs.

#include "export/TextureExporter.h"
#include "export/BitmapExport.h"
#include "gfx/Screenshot.h"

#include "obj/Dir.h"
#include "rndobj/Tex.h"

#include <cstdio>
#include <string>
#include <sys/stat.h>

namespace TextureExporter {

int ExportAll(ObjectDir* dir, const char* outputDir, const Options& opts) {
    if (!dir || !outputDir) return 0;

    // Ensure output directory exists
    mkdir(outputDir, 0755);

    int count = 0;
    ObjDirItr<RndTex> it(dir, true);
    while (it) {
        RndTex* tex = it;
        const char* name = tex->Name();
        if (!name || !name[0]) {
            ++it;
            continue;
        }

        const RndBitmap& bmp = tex->Bitmap();
        if (bmp.Width() == 0 || bmp.Height() == 0 || !bmp.Pixels()) {
            if (opts.verbose)
                printf("  skip: %s (no bitmap data)\n", name);
            ++it;
            continue;
        }

        std::vector<uint8_t> rgba = BitmapExport::ToRGBA(bmp);
        if (rgba.empty()) {
            if (opts.verbose)
                printf("  skip: %s (decode failed)\n", name);
            ++it;
            continue;
        }

        std::string path = std::string(outputDir) + "/" + name + ".png";
        if (WritePNG(path.c_str(), rgba.data(), bmp.Width(), bmp.Height())) {
            count++;
            if (opts.verbose)
                printf("  exported: %s (%dx%d)\n", path.c_str(), bmp.Width(), bmp.Height());
        } else {
            fprintf(stderr, "  error writing %s\n", path.c_str());
        }

        ++it;
    }

    return count;
}

} // namespace TextureExporter
