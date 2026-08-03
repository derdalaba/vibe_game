#pragma once

#define VULKAN_HPP_NO_STRUCT_CONSTRUCTORS
#include <vulkan/vulkan.hpp>

#include <array>
#include <cstddef>
#include <cstring>
#include <string_view>

namespace Renderer {

struct Vertex {
    float pos[3];
    float texCoord[2];

    static vk::VertexInputBindingDescription getBindingDescription() {
        return vk::VertexInputBindingDescription{
            .binding = 0,
            .stride = sizeof(Vertex),
            .inputRate = vk::VertexInputRate::eVertex};
    }

    static std::array<vk::VertexInputAttributeDescription, 2>
    getAttributeDescriptions() {
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
                .offset = offsetof(Vertex, texCoord)}};
    }

    bool operator==(const Vertex& other) const {
        return memcmp(this, &other, sizeof(Vertex)) == 0;
    }
};

struct VertexHash {
    size_t operator()(const Vertex& vertex) const {
        std::string_view bytes(reinterpret_cast<const char*>(&vertex),
                               sizeof(Vertex));
        return std::hash<std::string_view>{}(bytes);
    }
};

}  // namespace Renderer
