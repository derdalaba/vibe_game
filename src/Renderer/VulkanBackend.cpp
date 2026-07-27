#include "VulkanBackend.hpp"

namespace Renderer {

VulkanBackend::VulkanBackend(Surface* surface) : mSurface(surface) {
    mInstance = std::make_shared<vk::raii::Instance>(vk::raii::Context(), vk::InstanceCreateInfo{});
    mDevice.initialize();
}
VulkanBackend::~VulkanBackend() {}

void VulkanBackend::create_instance() {
    constexpr vk::ApplicationInfo appInfo{
        .pApplicationName = "Hello Triangle",
        .applicationVersion = VK_MAKE_VERSION(1, 0, 0),
        .pEngineName = "No Engine",
        .engineVersion = VK_MAKE_VERSION(1, 0, 0),
        .apiVersion = vk::ApiVersion14};

    uint32_t glfwExtensionCount = 0;
    auto glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);

    auto extensionProperties = mContext.enumerateInstanceExtensionProperties();
    for (uint32_t i = 0; i < glfwExtensionCount; ++i) {
        if (std::ranges::none_of(extensionProperties, [glfwExtension = glfwExtensions[i]](
                                     auto const& extensionProperty) {
            return strcmp(extensionProperty.extensionName, glfwExtension) == 0;
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

} // namespace Renderer