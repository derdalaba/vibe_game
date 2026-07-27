#pragma once

#include "Shader.hpp"
#include "Surface.hpp"

#define VULKAN_HPP_NO_STRUCT_CONSTRUCTORS
#include <vulkan/vulkan.hpp>
// or
#include <GLFW/glfw3.h>  // For window creation and surface handling

#include <vulkan/vulkan_raii.hpp>

#ifdef NDEBUG
constexpr bool enableValidationLayers = false;
#else
constexpr bool enableValidationLayers = true;
#endif

namespace Renderer {

class Device {
   public:
    void initialize();
    
   private:
    vk::raii::SurfaceKHR create_surface(GLFWwindow* window);
    void create_device();
    void select_physical_device();
    vk::Extent2D chooseSwapExtent(
        vk::SurfaceCapabilitiesKHR const& capabilities);
    void create_logical_device();

   private:
    std::shared_ptr<vk::raii::Context> mContext;
    vk::raii::PhysicalDevice mPhysicalDevice = nullptr;
    vk::raii::Device mLogicalDevice = nullptr;
    vk::raii::Queue mGraphicsQueue = nullptr;
    Surface mSurface;
    std::vector<vk::Image> mSwapChainImages;
    std::vector<vk::raii::ImageView> mSwapChainImageViews;
    vk::raii::SwapchainKHR mSwapChain = nullptr;
    vk::SurfaceFormatKHR mSwapChainSurfaceFormat;
    vk::Extent2D mSwapChainExtent;

    std::vector<const char*> mRequiredDeviceExtension = {
        vk::KHRSwapchainExtensionName};

    std::vector<Renderer::Shader> mShaders{};
};
}  // namespace Renderer