#pragma once

#define VULKAN_HPP_NO_STRUCT_CONSTRUCTORS
#include <vulkan/vulkan.hpp>
// or
#include <GLFW/glfw3.h>  // For window creation and surface handling

#include <vulkan/vulkan_raii.hpp>

#include "Device.hpp"
#include "Surface.hpp"
#include "SwapChain.hpp"

namespace Renderer {

class VulkanBackend {
public:
    VulkanBackend(Surface* surface);
    ~VulkanBackend();

private:
    void create_instance();
    void create_graphics_pipeline();

private:
    std::shared_ptr<Surface> mSurface;
    std::shared_ptr<vk::raii::Context> mContext;
    std::shared_ptr<vk::raii::Instance> mInstance;   
    Device mDevice;
    vk::raii::Queue mGraphicsQueue;
    SwapChain mSwapChain;
    std::shared_ptr<Surface> mSurface;
    vk::Extent2D mSwapChainExtent;
    vk::SurfaceFormatKHR mSurfaceFormat;
    std::vector<vk::raii::ImageView> mSwapChainImageViews;
    std::vector<vk::Image> mSwapChainImages;
    std::vector<const char*> mRequiredDeviceExtensions = {
        vk::KHRSwapchainExtensionName
    };
    std::vector<vk::raii::Framebuffer> mFramebuffers;
    vk::raii::CommandPool mCommandPool;
    vk::raii::CommandBuffer mCommandBuffer;
    vk::raii::PipelineLayout mPipelineLayout;
    vk::raii::Pipeline mGraphicsPipeline;
    vk::raii::RenderPass mRenderPass;
    vk::raii::DescriptorSetLayout mDescriptorSetLayout;
    vk::raii::DescriptorPool mDescriptorPool;
    vk::raii::DescriptorSet mDescriptorSet;
};
} // namespace Renderer
