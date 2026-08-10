#pragma once

#include <cstdint>
#include <cstring>
#include <string_view>
#include <vector>

namespace Core {

// Plain-data vertex. Deliberately Vulkan-free: Core owns what the data is,
// Renderer owns how it reaches the GPU (see Renderer/Vertex.hpp for the
// vk::VertexInputAttributeDescription mapping).
//
// Layout is 12 + 8 + 16 + 16 = 52 bytes with alignment 4, so there is no
// padding anywhere in the struct. That matters: the byte-wise operator== and
// MeshVertexHash below would produce spurious mismatches on uninitialised
// padding bytes. Keep every member 4-byte aligned if you extend this.
struct Vertex {
    float pos[3];
    float texCoord[2];
    uint32_t joints[4];  // might cause a bug when more the 4 bones are used, but for now we only support 4 bones per vertex
    float weights[4];

    bool operator==(const Vertex& other) const { return memcmp(this, &other, sizeof(Vertex)) == 0; }
};

static_assert(sizeof(Vertex) == 52,
              "Core::Vertex gained padding; the byte-wise hash/compare and the "
              "Renderer attribute offsets both assume a tightly packed layout");

struct MeshVertexHash {
    size_t operator()(const Vertex& vertex) const {
        std::string_view bytes(reinterpret_cast<const char*>(&vertex), sizeof(Vertex));
        return std::hash<std::string_view>{}(bytes);
    }
};

struct MeshData {
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
};

}  // namespace Core
