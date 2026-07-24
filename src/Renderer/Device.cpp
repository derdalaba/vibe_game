#include "Device.hpp"

#include <GLFW/glfw3.h>

#include <cstdio>  // Added for printf and fprintf
#include <map>
#include <utility>

#include "Shader.hpp"
#include "Surface.hpp"  // Assuming Surface is built using GLFW

namespace Renderer {

void Device::run() {
    if (!glfwInit()) {
        fprintf(stderr, "Failed to initialize GLFW\n");
        return;
    }
    while (true) {
        // Poll for and process events
        glfwPollEvents();

        if (glfwWindowShouldClose(mSurface.getWindow())) {
            break;
        }
    }
    mSurface.cleanup();
}

void Device::create_instance() {
    constexpr vk::ApplicationInfo appInfo{
        .pApplicationName = "Hello Triangle",
        .applicationVersion = VK_MAKE_VERSION(1, 0, 0),
        .pEngineName = "No Engine",
        .engineVersion = VK_MAKE_VERSION(1, 0, 0),
        .apiVersion = vk::ApiVersion14};

    // Get the required instance extensions from GLFW.
    uint32_t glfwExtensionCount = 0;
    auto glfwExtensions =
        glfwGetRequiredInstanceExtensions(&glfwExtensionCount);

    // Check if the required GLFW extensions are supported by the Vulkan
    // implementation.
    auto extensionProperties = mContext.enumerateInstanceExtensionProperties();
    for (uint32_t i = 0; i < glfwExtensionCount; ++i) {
        if (std::ranges::none_of(
                extensionProperties, [glfwExtension = glfwExtensions[i]](
                                         auto const& extensionProperty) {
                    return strcmp(extensionProperty.extensionName,
                                  glfwExtension) == 0;
                })) {
            throw std::runtime_error("Required GLFW extension not supported: " +
                                     std::string(glfwExtensions[i]));
        }
    }

    vk::InstanceCreateInfo createInfo{
        .pApplicationInfo = &appInfo,
        .enabledExtensionCount = glfwExtensionCount,
        .ppEnabledExtensionNames = glfwExtensions};

    mInstance = vk::raii::Instance(mContext, createInfo);
}

void Device::select_physical_device() {
    auto physicalDevices = vk::raii::PhysicalDevices(mInstance);
    if (physicalDevices.empty()) {
        fprintf(stderr, "Error: No Vulkan-capable devices found.\n");
        return;
    }
    // Use an ordered map to automatically sort candidates by increasing score
    std::multimap<int, vk::raii::PhysicalDevice> candidates;

    for (const auto& pd : physicalDevices) {
        auto deviceProperties = pd.getProperties();
        auto deviceFeatures = pd.getFeatures();
        uint32_t score = 0;

        // Discrete GPUs have a significant performance advantage
        if (deviceProperties.deviceType ==
            vk::PhysicalDeviceType::eDiscreteGpu) {
            score += 1000;
        }

        // Maximum possible size of textures affects graphics quality
        score += deviceProperties.limits.maxImageDimension2D;

        // Application can't function without geometry shaders
        if (!deviceFeatures.geometryShader) {
            continue;
        }
        candidates.insert(std::make_pair(score, pd));
    }

    // Check if the best candidate is suitable at all
    if (!candidates.empty() && candidates.rbegin()->first > 0) {
        mPhysicalDevice = candidates.rbegin()->second;
    } else {
        throw std::runtime_error("failed to find a suitable GPU!");
    }
}

void Device::create_image_views() {
    assert(mSwapChainImageViews.empty());

    vk::ImageViewCreateInfo imageViewCreateInfo{
        .viewType = vk::ImageViewType::e2D,
        .format = mSwapChainSurfaceFormat.format,
        .subresourceRange = {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1}};
    for (auto& image : mSwapChainImages) {
        imageViewCreateInfo.image = image;
        mSwapChainImageViews.emplace_back(mLogicalDevice, imageViewCreateInfo);
    }
}

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

vk::Extent2D Device::chooseSwapExtent(
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

void Device::create_swap_chain() {
    vk::SurfaceCapabilitiesKHR surfaceCapabilities =
        mPhysicalDevice.getSurfaceCapabilitiesKHR(mSurface.getSurface());
    mSwapChainExtent = chooseSwapExtent(surfaceCapabilities);
    uint32_t minImageCount = chooseSwapMinImageCount(surfaceCapabilities);

    std::vector<vk::SurfaceFormatKHR> availableFormats =
        mPhysicalDevice.getSurfaceFormatsKHR(mSurface.getSurface());
    mSwapChainSurfaceFormat = chooseSwapSurfaceFormat(availableFormats);

    std::vector<vk::PresentModeKHR> availablePresentModes =
        mPhysicalDevice.getSurfacePresentModesKHR(mSurface.getSurface());
    vk::PresentModeKHR presentMode =
        chooseSwapPresentMode(availablePresentModes);

    vk::SwapchainCreateInfoKHR swapChainCreateInfo{
        .surface = *mSurface.getSurface(),
        .minImageCount = minImageCount,
        .imageFormat = mSwapChainSurfaceFormat.format,
        .imageColorSpace = mSwapChainSurfaceFormat.colorSpace,
        .imageExtent = mSwapChainExtent,
        .imageArrayLayers = 1,
        .imageUsage = vk::ImageUsageFlagBits::eColorAttachment,
        .imageSharingMode = vk::SharingMode::eExclusive,
        .preTransform = surfaceCapabilities.currentTransform,
        .compositeAlpha = vk::CompositeAlphaFlagBitsKHR::eOpaque,
        .presentMode = presentMode,
        .clipped = true};

    mSwapChain = vk::raii::SwapchainKHR(mLogicalDevice, swapChainCreateInfo);
    mSwapChainImages = mSwapChain.getImages();
}
std::vector<const char*> getRequiredInstanceExtensions() {
    uint32_t glfwExtensionCount = 0;
    auto glfwExtensions =
        glfwGetRequiredInstanceExtensions(&glfwExtensionCount);

    std::vector extensions(glfwExtensions, glfwExtensions + glfwExtensionCount);
    if (enableValidationLayers) {
        extensions.push_back(vk::EXTDebugUtilsExtensionName);
    }

    return extensions;
}

void Device::initialize() {
    create_instance();

    mSurface = Surface();
    mSurface.initialize(mInstance);

    select_physical_device();
    create_device();
    create_swap_chain();
    create_image_views();
    create_graphics_pipeline();
}

void Device::create_device() {
    if (mPhysicalDevice == VK_NULL_HANDLE) {
        fprintf(stderr, "Error: No physical device found.\n");
        return;
    }

    std::vector<vk::QueueFamilyProperties> queueFamilyProperties =
        mPhysicalDevice.getQueueFamilyProperties();

    // get the first index into queueFamilyProperties which supports both
    // graphics and present
    uint32_t queueIndex = ~0;
    for (uint32_t qfpIndex = 0; qfpIndex < queueFamilyProperties.size();
         qfpIndex++) {
        if ((queueFamilyProperties[qfpIndex].queueFlags &
             vk::QueueFlagBits::eGraphics) &&
            mPhysicalDevice.getSurfaceSupportKHR(qfpIndex,
                                                 *mSurface.getSurface())) {
            // found a queue family that supports both graphics and present
            queueIndex = qfpIndex;
            break;
        }
    }
    if (queueIndex == ~0) {
        throw std::runtime_error(
            "Could not find a queue for graphics and present -> terminating");
    }

    // query for Vulkan 1.3 features
    vk::StructureChain<vk::PhysicalDeviceFeatures2,
                       vk::PhysicalDeviceVulkan11Features,
                       vk::PhysicalDeviceVulkan13Features,
                       vk::PhysicalDeviceExtendedDynamicStateFeaturesEXT>
        featureChain = {
            {},  // vk::PhysicalDeviceFeatures2
            {.shaderDrawParameters =
                 true},                  // vk::PhysicalDeviceVulkan11Features
            {.dynamicRendering = true},  // vk::PhysicalDeviceVulkan13Features
            {.extendedDynamicState =
                 true}  // vk::PhysicalDeviceExtendedDynamicStateFeaturesEXT
        };

    // create a Device
    float queuePriority = 0.5f;
    vk::DeviceQueueCreateInfo deviceQueueCreateInfo{
        .queueFamilyIndex = queueIndex,
        .queueCount = 1,
        .pQueuePriorities = &queuePriority};
    vk::DeviceCreateInfo deviceCreateInfo{
        .pNext = &featureChain.get<vk::PhysicalDeviceFeatures2>(),
        .queueCreateInfoCount = 1,
        .pQueueCreateInfos = &deviceQueueCreateInfo,
        .enabledExtensionCount =
            static_cast<uint32_t>(mRequiredDeviceExtension.size()),
        .ppEnabledExtensionNames = mRequiredDeviceExtension.data()};

    mLogicalDevice = vk::raii::Device(mPhysicalDevice, deviceCreateInfo);
    mGraphicsQueue = vk::raii::Queue(mLogicalDevice, queueIndex, 0);
}

void Device::create_graphics_pipeline() {
    mShaders.emplace_back(std::string(SHADER_DIR) + "/triangle.spv",
                          mLogicalDevice);
}
}  // namespace Renderer