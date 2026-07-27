#include "Renderer.hpp"
#include <GLFW/glfw3.h>
#include <cstdio>
#include <map>
#include <utility>
#include <algorithm>
#include <stdexcept>
#include <limits>

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

static vk::PresentModeKHR chooseSwapPresentMode(
    std::vector<vk::PresentModeKHR> const& availablePresentModes) {
    assert(std::ranges::any_of(availablePresentModes, [](auto presentMode) {
        return presentMode == vk::PresentModeKHR::eFifo;
    }));
    return std::ranges::any_of(availablePresentModes,
                               [](const vk::PresentModeKHR value) {
                                   return vk::PresentModeKHR::eMailbox == value;
                               })
               ? vk::PresentModeKHR::eMailbox
               : vk::PresentModeKHR::eFifo;
}

vk::Extent2D chooseSwapExtent(
    vk::SurfaceCapabilitiesKHR const& capabilities) {
    if (capabilities.currentExtent.width !=
        std::numeric_limits<uint32_t>::max()) {
        return capabilities.currentExtent;
    }
    int width, height;
    glfwGetFramebufferSize(mSurface.getWindow(), &width, &height);

    return {std::clamp<uint32_t>(width, capabilities.minImageExtent.width,
                                 capabilities.maxImageExtent.width),
            std::clamp<uint32_t>(height, capabilities.minImageExtent.height,
                                 capabilities.maxImageExtent.height)};
}

Renderer::Renderer::Renderer() : mSurface(std::make_shared<Surface>()), mBackend(std::make_unique<VulkanBackend>(mSurface.get())) {
}
} // namespace Renderer