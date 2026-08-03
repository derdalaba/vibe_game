#pragma once

#include <string>
#include <vector>

#include "Animation.hpp"
#include "Mesh.hpp"
#include "Skeleton.hpp"

namespace Core {

struct LoadedModel {
    MeshData mesh;
    Skeleton skeleton;
    std::vector<AnimationClip> clips;
};

// Loads a .gltf (JSON, optionally with base64 data URIs) or .glb, returning
// Vulkan-free plain data. Throws std::runtime_error on failure.
//
// Current limitations, all deliberate for a first implementation:
//  - Only the first skinned mesh (or first mesh, if none is skinned) is used.
//  - Nodes using the `matrix` form instead of TRS are rejected; glTF requires
//    TRS for any animated node, so this only affects static nodes.
//  - Sparse accessors are rejected rather than silently misread.
//  - Only JOINTS_0 / WEIGHTS_0 (4 influences) are read; additional sets are
//    ignored.
LoadedModel loadGltf(const std::string& path);

}  // namespace Core
