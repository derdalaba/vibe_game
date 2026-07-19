#pragma once

#include "Surface.hpp"

#define VULKAN_HPP_NO_STRUCT_CONSTRUCTORS
#include <vulkan/vulkan.hpp>
// or
#include <vulkan/vulkan_raii.hpp>

#include <GLFW/glfw3.h> // For window creation and surface handling

#ifdef NDEBUG
constexpr bool enableValidationLayers = false;
#else
constexpr bool enableValidationLayers = true;
#endif


namespace Renderer {

class Device {
public:
    void initialize();
    void run(); // Added run method to execute the test function
private:
    void create_instance();
    vk::raii::SurfaceKHR create_surface(GLFWwindow* window);
    void create_device();
    void select_physical_device();
    void create_image_views();
    void create_swap_chain();
    void create_graphics_pipeline();
    vk::Extent2D chooseSwapExtent(vk::SurfaceCapabilitiesKHR const &capabilities);

private:
    vk::raii::Context mContext;
    vk::raii::Instance mInstance = nullptr;
    vk::raii::PhysicalDevice mPhysicalDevice = nullptr;
    vk::raii::Device mLogicalDevice = nullptr;
    vk::raii::Queue mGraphicsQueue = nullptr;
    Renderer::Surface mSurface;
    std::vector<vk::Image> mSwapChainImages;
    std::vector<vk::raii::ImageView> mSwapChainImageViews;
    vk::raii::SwapchainKHR mSwapChain = nullptr;
    vk::SurfaceFormatKHR mSwapChainSurfaceFormat;
    vk::Extent2D mSwapChainExtent;

    std::vector<const char *> mRequiredDeviceExtension = {
	    vk::KHRSwapchainExtensionName};

    

};
} // namespace Renderer