#pragma once

#include <memory>
#include <vector>

#define VULKAN_HPP_NO_STRUCT_CONSTRUCTORS
#include <vulkan/vulkan.hpp>
// or
#include <vulkan/vulkan_raii.hpp>

#include "Device.hpp"
#include "Shader.hpp"
#include "Surface.hpp"
#include "SwapChain.hpp"

namespace Renderer {

class VulkanBackend {
   public:
    VulkanBackend(std::shared_ptr<Surface> surface);
    ~VulkanBackend();

    void render_frame();
    void notifyFramebufferResized();

   private:
    void create_instance();
    void create_graphics_pipeline();
    void create_command_pool_and_buffers();
    void create_sync_objects();
    void recreate_swap_chain();
    void record_command_buffer(uint32_t imageIndex, uint32_t frameIndex);
    void transition_image_layout(uint32_t frameIndex, uint32_t imageIndex,
                                  vk::ImageLayout oldLayout,
                                  vk::ImageLayout newLayout,
                                  vk::AccessFlags2 srcAccess,
                                  vk::AccessFlags2 dstAccess,
                                  vk::PipelineStageFlags2 srcStage,
                                  vk::PipelineStageFlags2 dstStage);

   private:
    static constexpr uint32_t kMaxFramesInFlight = 2;

    std::shared_ptr<Surface> mSurface;
    vk::raii::Context mContext;
    vk::raii::Instance mInstance = nullptr;
    Device mDevice;
    SwapChain mSwapChain;

    vk::raii::CommandPool mCommandPool = nullptr;
    std::vector<vk::raii::CommandBuffer> mCommandBuffers;
    vk::raii::PipelineLayout mPipelineLayout = nullptr;
    vk::raii::Pipeline mGraphicsPipeline = nullptr;

    std::vector<vk::raii::Semaphore> mPresentCompleteSemaphores;
    std::vector<vk::raii::Semaphore> mRenderFinishedSemaphores;
    std::vector<vk::raii::Fence> mInFlightFences;
    uint32_t mFrameIndex = 0;

    std::vector<Shader> mShaders;
    bool mFramebufferResized = false;
};
}  // namespace Renderer
