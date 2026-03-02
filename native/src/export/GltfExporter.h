// GltfExporter — Export ObjectDir scene as glTF 2.0 (static meshes, skinned meshes, animations).

#pragma once

class ObjectDir;

namespace GltfExporter {

struct Options {
    bool binary = false;      // Output .glb instead of .gltf
    bool animations = true;   // Include RndTransAnim animations
    bool skins = true;        // Include skinned mesh data (joints/weights)
    bool verbose = false;
};

// Export the scene as a glTF file.
// outputPath should end in .gltf or .glb.
// Returns true on success.
bool Export(ObjectDir* dir, const char* outputPath, const Options& opts = {});

} // namespace GltfExporter
