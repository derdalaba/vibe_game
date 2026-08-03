#pragma once

#include <vector>

#define VULKAN_HPP_NO_STRUCT_CONSTRUCTORS
#include <vulkan/vulkan.hpp>
// or
#include <vulkan/vulkan_raii.hpp>

#include "Surface.hpp"

namespace Renderer {

class SwapChain {
   public:
    SwapChain() = default;
    ~SwapChain() = default;
    SwapChain(const SwapChain&) = delete;
    SwapChain& operator=(const SwapChain&) = delete;
    SwapChain(SwapChain&&) = default;
    SwapChain& operator=(SwapChain&&) = default;

    void initialize(const vk::raii::PhysicalDevice& physicalDevice,
                     const vk::raii::Device& device, const Surface& surface);

    const vk::raii::SwapchainKHR& handle() const { return mSwapChain; }
    const vk::Extent2D& extent() const { return mExtent; }
    const vk::SurfaceFormatKHR& surfaceFormat() const {
        return mSurfaceFormat;
    }
    const std::vector<vk::raii::ImageView>& imageViews() const {
        return mImageViews;
    }
    const std::vector<vk::Image>& images() const { return mImages; }
    const vk::Image& depthImage() const { return *mDepthImage; }
    const vk::raii::ImageView& depthImageView() const { return mDepthImageView; }
    vk::Format depthFormat() const { return mDepthFormat; }

   private:
    vk::Extent2D chooseExtent(
        const Surface& surface,
        vk::SurfaceCapabilitiesKHR const& capabilities) const;
    void createImageViews(const vk::raii::Device& device);
    void createDepthResources(const vk::raii::PhysicalDevice& physicalDevice,
                               const vk::raii::Device& device);

   private:
    vk::raii::SwapchainKHR mSwapChain = nullptr;
    std::vector<vk::Image> mImages;
    std::vector<vk::raii::ImageView> mImageViews;
    vk::SurfaceFormatKHR mSurfaceFormat;
    vk::Extent2D mExtent;

    vk::raii::Image mDepthImage = nullptr;
    vk::raii::DeviceMemory mDepthImageMemory = nullptr;
    vk::raii::ImageView mDepthImageView = nullptr;
    vk::Format mDepthFormat = vk::Format::eUndefined;
};
}  // namespace Renderer
