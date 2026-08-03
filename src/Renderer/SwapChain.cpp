#include "SwapChain.hpp"

#include <algorithm>
#include <cassert>
#include <limits>
#include <stdexcept>

#include <GLFW/glfw3.h>

namespace Renderer {

static uint32_t chooseSwapMinImageCount(
    vk::SurfaceCapabilitiesKHR const& surfaceCapabilities) {
    auto minImageCount = std::max(3u, surfaceCapabilities.minImageCount);
    if ((0 < surfaceCapabilities.maxImageCount) &&
        (surfaceCapabilities.maxImageCount < minImageCount)) {
        minImageCount = surfaceCapabilities.maxImageCount;
    }
    return minImageCount;
}

static vk::SurfaceFormatKHR chooseSwapSurfaceFormat(
    std::vector<vk::SurfaceFormatKHR> const& availableFormats) {
    assert(!availableFormats.empty());
    const auto formatIt =
        std::ranges::find_if(availableFormats, [](const auto& format) {
            return format.format == vk::Format::eB8G8R8A8Srgb &&
                   format.colorSpace == vk::ColorSpaceKHR::eSrgbNonlinear;
        });
    return formatIt != availableFormats.end() ? *formatIt : availableFormats[0];
}

static vk::Format findDepthFormat(const vk::raii::PhysicalDevice& physicalDevice) {
    static const std::vector<vk::Format> candidates = {
        vk::Format::eD32Sfloat, vk::Format::eD32SfloatS8Uint,
        vk::Format::eD24UnormS8Uint};
    for (vk::Format format : candidates) {
        vk::FormatProperties properties =
            physicalDevice.getFormatProperties(format);
        if (properties.optimalTilingFeatures &
            vk::FormatFeatureFlagBits::eDepthStencilAttachment) {
            return format;
        }
    }
    throw std::runtime_error("failed to find a supported depth format!");
}

static uint32_t findMemoryType(const vk::raii::PhysicalDevice& physicalDevice,
                               uint32_t typeFilter,
                               vk::MemoryPropertyFlags properties) {
    vk::PhysicalDeviceMemoryProperties memProperties =
        physicalDevice.getMemoryProperties();
    for (uint32_t i = 0; i < memProperties.memoryTypeCount; ++i) {
        if ((typeFilter & (1u << i)) &&
            (memProperties.memoryTypes[i].propertyFlags & properties) ==
                properties) {
            return i;
        }
    }
    throw std::runtime_error("failed to find suitable memory type!");
}

static vk::PresentModeKHR chooseSwapPresentMode(
    std::vector<vk::PresentModeKHR> const& availablePresentModes) {
    return std::ranges::any_of(availablePresentModes,
                               [](const vk::PresentModeKHR value) {
                                   return vk::PresentModeKHR::eMailbox == value;
                               })
               ? vk::PresentModeKHR::eMailbox
               : vk::PresentModeKHR::eFifo;
}

vk::Extent2D SwapChain::chooseExtent(
    const Surface& surface,
    vk::SurfaceCapabilitiesKHR const& capabilities) const {
    if (capabilities.currentExtent.width !=
        std::numeric_limits<uint32_t>::max()) {
        return capabilities.currentExtent;
    }
    int width, height;
    glfwGetFramebufferSize(surface.getWindow(), &width, &height);

    return {std::clamp<uint32_t>(width, capabilities.minImageExtent.width,
                                 capabilities.maxImageExtent.width),
            std::clamp<uint32_t>(height, capabilities.minImageExtent.height,
                                 capabilities.maxImageExtent.height)};
}

void SwapChain::initialize(const vk::raii::PhysicalDevice& physicalDevice,
                            const vk::raii::Device& device,
                            const Surface& surface) {
    vk::SurfaceCapabilitiesKHR surfaceCapabilities =
        physicalDevice.getSurfaceCapabilitiesKHR(surface.getSurface());
    mExtent = chooseExtent(surface, surfaceCapabilities);
    uint32_t minImageCount = chooseSwapMinImageCount(surfaceCapabilities);

    std::vector<vk::SurfaceFormatKHR> availableFormats =
        physicalDevice.getSurfaceFormatsKHR(surface.getSurface());
    mSurfaceFormat = chooseSwapSurfaceFormat(availableFormats);

    std::vector<vk::PresentModeKHR> availablePresentModes =
        physicalDevice.getSurfacePresentModesKHR(surface.getSurface());
    vk::PresentModeKHR presentMode =
        chooseSwapPresentMode(availablePresentModes);

    vk::SwapchainCreateInfoKHR swapChainCreateInfo{
        .surface = *surface.getSurface(),
        .minImageCount = minImageCount,
        .imageFormat = mSurfaceFormat.format,
        .imageColorSpace = mSurfaceFormat.colorSpace,
        .imageExtent = mExtent,
        .imageArrayLayers = 1,
        .imageUsage = vk::ImageUsageFlagBits::eColorAttachment,
        .imageSharingMode = vk::SharingMode::eExclusive,
        .preTransform = surfaceCapabilities.currentTransform,
        .compositeAlpha = vk::CompositeAlphaFlagBitsKHR::eOpaque,
        .presentMode = presentMode,
        .clipped = true};

    mSwapChain = vk::raii::SwapchainKHR(device, swapChainCreateInfo);
    mImages = mSwapChain.getImages();
    createImageViews(device);
    createDepthResources(physicalDevice, device);
}

void SwapChain::createImageViews(const vk::raii::Device& device) {
    assert(mImageViews.empty());

    vk::ImageViewCreateInfo imageViewCreateInfo{
        .viewType = vk::ImageViewType::e2D,
        .format = mSurfaceFormat.format,
        .subresourceRange = {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1}};
    for (auto& image : mImages) {
        imageViewCreateInfo.image = image;
        mImageViews.emplace_back(device, imageViewCreateInfo);
    }
}

void SwapChain::createDepthResources(
    const vk::raii::PhysicalDevice& physicalDevice,
    const vk::raii::Device& device) {
    mDepthFormat = findDepthFormat(physicalDevice);

    vk::ImageCreateInfo imageInfo{
        .imageType = vk::ImageType::e2D,
        .format = mDepthFormat,
        .extent = {mExtent.width, mExtent.height, 1},
        .mipLevels = 1,
        .arrayLayers = 1,
        .samples = vk::SampleCountFlagBits::e1,
        .tiling = vk::ImageTiling::eOptimal,
        .usage = vk::ImageUsageFlagBits::eDepthStencilAttachment,
        .sharingMode = vk::SharingMode::eExclusive,
        .initialLayout = vk::ImageLayout::eUndefined};
    mDepthImage = vk::raii::Image(device, imageInfo);

    vk::MemoryRequirements memRequirements = mDepthImage.getMemoryRequirements();
    vk::MemoryAllocateInfo allocInfo{
        .allocationSize = memRequirements.size,
        .memoryTypeIndex =
            findMemoryType(physicalDevice, memRequirements.memoryTypeBits,
                          vk::MemoryPropertyFlagBits::eDeviceLocal)};
    mDepthImageMemory = vk::raii::DeviceMemory(device, allocInfo);
    mDepthImage.bindMemory(mDepthImageMemory, 0);

    vk::ImageViewCreateInfo viewInfo{
        .image = mDepthImage,
        .viewType = vk::ImageViewType::e2D,
        .format = mDepthFormat,
        .subresourceRange = {vk::ImageAspectFlagBits::eDepth, 0, 1, 0, 1}};
    mDepthImageView = vk::raii::ImageView(device, viewInfo);
}

}  // namespace Renderer
