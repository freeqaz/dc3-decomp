// MaterialExporter — Export all materials from an ObjectDir as JSON files.

#pragma once

class ObjectDir;

namespace MaterialExporter {

struct Options {
    bool verbose = false;
};

// Export all BaseMaterial objects in the directory (and subdirs) as JSON files.
// Returns the number of materials exported.
int ExportAll(ObjectDir* dir, const char* outputDir, const Options& opts = {});

} // namespace MaterialExporter
