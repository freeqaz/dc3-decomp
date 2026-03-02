// TextureExporter — Export all textures from an ObjectDir as PNG files.

#pragma once

class ObjectDir;

namespace TextureExporter {

struct Options {
    bool verbose = false;
};

// Export all RndTex objects in the directory (and subdirs) as PNG files.
// Returns the number of textures exported.
int ExportAll(ObjectDir* dir, const char* outputDir, const Options& opts = {});

} // namespace TextureExporter
