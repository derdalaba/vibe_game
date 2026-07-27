#pragma once

#include <memory>
#include <vector>
#include <string>

#define VULKAN_HPP_NO_STRUCT_CONSTRUCTORS
#include <vulkan/vulkan.hpp>
// or
#include <GLFW/glfw3.h>  // For window creation and surface handling

#include <vulkan/vulkan_raii.hpp>

#include "Device.hpp"
#include "Surface.hpp"
#include "SwapChain.hpp"
#include "Shader.hpp"
#include "VulkanBackend.hpp"

namespace Renderer {

class Renderer {
public:
    Renderer();
    ~Renderer();

    void render_frame();
    void shutdown();

private:
    std::shared_ptr<Surface> mSurface;
    std::unique_ptr<VulkanBackend> mBackend;

};

} // namespace Renderer