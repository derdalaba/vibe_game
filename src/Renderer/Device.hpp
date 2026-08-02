#pragma once

#include <vector>

#define VULKAN_HPP_NO_STRUCT_CONSTRUCTORS
#include <vulkan/vulkan.hpp>
// or
#include <vulkan/vulkan_raii.hpp>

namespace Renderer {

class Device {
   public:
    void initialize(const vk::raii::Instance& instance,
                    const vk::raii::SurfaceKHR& surface);

    const vk::raii::PhysicalDevice& physicalDevice() const {
        return mPhysicalDevice;
    }
    const vk::raii::Device& logicalDevice() const { return mLogicalDevice; }
    const vk::raii::Queue& graphicsQueue() const { return mGraphicsQueue; }
    uint32_t graphicsQueueFamilyIndex() const {
        return mGraphicsQueueFamilyIndex;
    }
    vk::raii::CommandPool createCommandPool() const {
        vk::CommandPoolCreateInfo poolInfo{
            .flags = vk::CommandPoolCreateFlagBits::eResetCommandBuffer,
            .queueFamilyIndex = mGraphicsQueueFamilyIndex};
        return vk::raii::CommandPool(mLogicalDevice, poolInfo);
    }

   private:
    void select_physical_device(const vk::raii::Instance& instance,
                                const vk::raii::SurfaceKHR& surface);
    void create_logical_device(const vk::raii::SurfaceKHR& surface);

   private:
    vk::raii::PhysicalDevice mPhysicalDevice = nullptr;
    vk::raii::Device mLogicalDevice = nullptr;
    vk::raii::Queue mGraphicsQueue = nullptr;
    uint32_t mGraphicsQueueFamilyIndex = ~0u;

    std::vector<const char*> mRequiredDeviceExtension = {
        vk::KHRSwapchainExtensionName};
};
}  // namespace Renderer
