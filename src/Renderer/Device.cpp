#include "Device.hpp"
#include <vulkan/vulkan.h>
#include "Surface.hpp" // Assuming Surface is built using GLFW
#include <cstdio> // Added for printf and fprintf

// Helper function to define application info (can be static or local)
VkApplicationInfo getAppInfo() {
    return VkApplicationInfo{.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
                             .pApplicationName   = "Vibe Game",
                             .applicationVersion = VK_MAKE_VERSION( 1, 0, 0 ),
                             .pEngineName        = "MyCustomEngine",
                             .engineVersion      = VK_MAKE_VERSION( 1, 0, 0 ),
                             .apiVersion         = VK_API_VERSION_1_2}; // Using a modern API version
}

VkInstance create_instance() {
    VkApplicationInfo appInfo = getAppInfo();
    VkInstance instance = VK_NULL_HANDLE;
    
    // Setup creation info structure
    VkInstanceCreateInfo createInfo{.pApplicationInfo = &appInfo};
    
    // In a real application, you would also add required extensions here (e.g., for GLFW/Wayland)
    // For this example, we proceed with minimal setup as per the tutorial's initial step.

    VkResult result = vkCreateInstance(&createInfo, nullptr, &instance);
    if (result != VK_SUCCESS) {
        fprintf(stderr, "Error: Failed to create Vulkan instance. VkResult: %d\n", result);
        return VK_NULL_HANDLE;
    }
    return instance;
}

void initialize(GLFWwindow* window) {
    // 1. Create Instance (The entry point to the API)
    VkInstance instance = create_instance();

    // 2. Get Platform-Specific Surface Handle (Crucial link between OS and Vulkan)
    VkSurfaceKHR surface = Renderer::Surface::create_surface(window, instance);

    // 3. Enumerate Physical Devices and select one
    VkPhysicalDevice physicalDevice = VK_NULL_HANDLE; // Placeholder: Implement device finding logic here

    // 4. Create Logical Device (The usable handle for rendering commands)
    VkDevice logicalDevice = create_device(physicalDevice, surface);
    
    // --- Test Function for Vulkan API Access ---
    if (logicalDevice != VK_NULL_HANDLE) {
        uint32_t apiVersion;
        vkEnumerateInstanceVersion(&apiVersion);
        printf("Vulkan API Version successfully queried: %u\n", apiVersion);
    } else {
        fprintf(stderr, "Error: Logical Device creation failed. Cannot test Vulkan API.\n");
    }

    // ... setup command buffers, swapchain, etc. ...
}