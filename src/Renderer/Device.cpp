#include "Device.hpp"

#include <map>
#include <stdexcept>

namespace Renderer {

void Device::initialize(const vk::raii::Instance& instance,
                         const vk::raii::SurfaceKHR& surface) {
    select_physical_device(instance, surface);
    create_logical_device(surface);
}

void Device::select_physical_device(const vk::raii::Instance& instance,
                                     const vk::raii::SurfaceKHR& surface) {
    auto physicalDevices = vk::raii::PhysicalDevices(instance);
    if (physicalDevices.empty()) {
        throw std::runtime_error("Error: No Vulkan-capable devices found.");
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

void Device::create_logical_device(const vk::raii::SurfaceKHR& surface) {
    std::vector<vk::QueueFamilyProperties> queueFamilyProperties =
        mPhysicalDevice.getQueueFamilyProperties();

    // get the first index into queueFamilyProperties which supports both
    // graphics and present
    uint32_t queueIndex = ~0;
    for (uint32_t qfpIndex = 0; qfpIndex < queueFamilyProperties.size();
         ++qfpIndex) {
        if ((queueFamilyProperties[qfpIndex].queueFlags &
             vk::QueueFlagBits::eGraphics) &&
            mPhysicalDevice.getSurfaceSupportKHR(qfpIndex, *surface)) {
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
            {.features = {.samplerAnisotropy = true}},  // vk::PhysicalDeviceFeatures2
            {.shaderDrawParameters =
                 true},  // vk::PhysicalDeviceVulkan11Features
            {.synchronization2 = true,
             .dynamicRendering = true},  // vk::PhysicalDeviceVulkan13Features
            {.extendedDynamicState =
                 true}  // vk::PhysicalDeviceExtendedDynamicStateFeaturesEXT
        };

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
    mGraphicsQueueFamilyIndex = queueIndex;
}

}  // namespace Renderer
