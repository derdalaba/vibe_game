#pragma once

#define VULKAN_HPP_NO_STRUCT_CONSTRUCTORS
#include <vulkan/vulkan.hpp>

#include <array>
#include <cstddef>

#include "Mesh.hpp"

namespace Renderer {

// Core owns the vertex data layout; this header only describes that layout to
// Vulkan. Attribute locations must match the declaration order of VertexInput
// in src/Shaders/box/shader.slang, which Slang assigns positionally
// (verified: position=0, texCoord=1, joints=2, weights=3).
using Vertex = Core::Vertex;
using VertexHash = Core::MeshVertexHash;

inline vk::VertexInputBindingDescription vertexBindingDescription() {
    return vk::VertexInputBindingDescription{
        .binding = 0,
        .stride = sizeof(Vertex),
        .inputRate = vk::VertexInputRate::eVertex};
}

inline std::array<vk::VertexInputAttributeDescription, 4>
vertexAttributeDescriptions() {
    return {
        vk::VertexInputAttributeDescription{
            .location = 0,
            .binding = 0,
            .format = vk::Format::eR32G32B32Sfloat,
            .offset = offsetof(Vertex, pos)},
        vk::VertexInputAttributeDescription{
            .location = 1,
            .binding = 0,
            .format = vk::Format::eR32G32Sfloat,
            .offset = offsetof(Vertex, texCoord)},
        // uint4 in the shader; must be fed by an integer-typed format.
        vk::VertexInputAttributeDescription{
            .location = 2,
            .binding = 0,
            .format = vk::Format::eR32G32B32A32Uint,
            .offset = offsetof(Vertex, joints)},
        vk::VertexInputAttributeDescription{
            .location = 3,
            .binding = 0,
            .format = vk::Format::eR32G32B32A32Sfloat,
            .offset = offsetof(Vertex, weights)}};
}

}  // namespace Renderer
