#pragma once

#include <chrono>
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
#include "Vertex.hpp"

namespace Renderer {

class VulkanBackend {
   public:
    VulkanBackend(std::shared_ptr<Surface> surface);
    ~VulkanBackend();

    void render_frame();
    void notifyFramebufferResized();
    void setCameraPosition(float x, float y, float z);

   private:
    void create_instance();
    void create_descriptor_set_layout();
    void create_graphics_pipeline();
    void load_model();
    void create_vertex_buffer();
    void create_index_buffer();
    void create_texture_image();
    void create_texture_image_view();
    void create_texture_sampler();
    void create_uniform_buffers();
    void create_descriptor_pool();
    void create_descriptor_sets();
    void update_uniform_buffer(uint32_t frameIndex);
    void copyBuffer(const vk::raii::Buffer& srcBuffer,
                    const vk::raii::Buffer& dstBuffer,
                    vk::DeviceSize size);
    void copyBufferToImage(const vk::raii::Buffer& buffer,
                           const vk::raii::Image& image, uint32_t width,
                           uint32_t height);
    std::pair<vk::raii::Buffer, vk::raii::DeviceMemory> create_buffer(vk::DeviceSize size, vk::BufferUsageFlags usage, vk::MemoryPropertyFlags properties);
    std::pair<vk::raii::Image, vk::raii::DeviceMemory> create_image(
        uint32_t width, uint32_t height, vk::Format format,
        vk::ImageTiling tiling, vk::ImageUsageFlags usage,
        vk::MemoryPropertyFlags properties);
    void transition_texture_image_layout(const vk::raii::Image& image,
                                         vk::ImageLayout oldLayout,
                                         vk::ImageLayout newLayout);
    uint32_t findMemoryType(uint32_t typeFilter,
                             vk::MemoryPropertyFlags properties);
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
    void transition_depth_image_layout(uint32_t frameIndex);

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

    std::vector<Vertex> mVertices;
    std::vector<uint32_t> mIndices;

    vk::raii::Buffer mStagingBuffer = nullptr;
    vk::raii::Buffer mVertexBuffer = nullptr;
    vk::raii::DeviceMemory mStagingBufferMemory = nullptr;
    vk::raii::DeviceMemory mVertexBufferMemory = nullptr;

    vk::raii::Buffer mIndexBuffer = nullptr;
    vk::raii::DeviceMemory mIndexBufferMemory = nullptr;

    vk::raii::Image mTextureImage = nullptr;
    vk::raii::DeviceMemory mTextureImageMemory = nullptr;
    vk::raii::ImageView mTextureImageView = nullptr;
    vk::raii::Sampler mTextureSampler = nullptr;

    vk::raii::DescriptorSetLayout mDescriptorSetLayout = nullptr;
    vk::raii::DescriptorPool mDescriptorPool = nullptr;
    std::vector<vk::raii::DescriptorSet> mDescriptorSets;

    std::vector<vk::raii::Buffer> mUniformBuffers;
    std::vector<vk::raii::DeviceMemory> mUniformBuffersMemory;
    std::vector<void*> mUniformBuffersMapped;
    std::chrono::steady_clock::time_point mStartTime;
    float mCameraPosition[3] = {2.0f, 2.0f, 2.0f};

    std::vector<vk::raii::Semaphore> mPresentCompleteSemaphores;
    std::vector<vk::raii::Semaphore> mRenderFinishedSemaphores;
    std::vector<vk::raii::Fence> mInFlightFences;
    uint32_t mFrameIndex = 0;

    std::vector<Shader> mShaders;
    bool mFramebufferResized = false;
};
}  // namespace Renderer
