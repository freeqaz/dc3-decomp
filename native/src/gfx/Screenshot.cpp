// DC3 Native Port — Screenshot Utilities
// PNG writing via stb_image_write, PPM as fallback.

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb/stb_image_write.h"

#include "gfx/Screenshot.h"
#include <cstdio>

bool WritePNG(const char* path, const uint8_t* rgba, int w, int h) {
    int stride = w * 4;
    int ok = stbi_write_png(path, w, h, 4, rgba, stride);
    return ok != 0;
}

bool WritePPM(const char* path, const uint8_t* rgba, int w, int h) {
    FILE* f = fopen(path, "wb");
    if (!f) return false;
    fprintf(f, "P6\n%d %d\n255\n", w, h);
    for (int i = 0; i < w * h; i++) {
        fputc(rgba[i * 4 + 0], f);  // R
        fputc(rgba[i * 4 + 1], f);  // G
        fputc(rgba[i * 4 + 2], f);  // B
    }
    fclose(f);
    return true;
}

bool WriteScreenshot(const char* path, const uint8_t* rgba, int w, int h) {
    return WritePNG(path, rgba, w, h);
}
