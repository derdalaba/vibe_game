#include "Surface.hpp"
#include <GLFW/glfw3.h> // Assuming GLFW is available and linked correctly
#include <cstdio>

namespace Renderer {

VkSurfaceKHR Surface::create_surface(GLFWwindow* glfwWindow, VkInstance instance) {
    if (!glfwWindow) {
        fprintf(stderr, "Error: Invalid GLFW window handle provided for surface creation.\n");
        return VK_NULL_HANDLE;
    }

    VkSurfaceKHR surface = VK_NULL_HANDLE;
    // Use the standard function provided by GLFW to create a Vulkan surface
    VkResult result = glfwCreateWindowSurface(instance, glfwWindow, nullptr, &surface);

    if (result != VK_SUCCESS) {
        fprintf(stderr, "Error: Failed to create Vulkan surface. VkResult: %d\n", result);
        return VK_NULL_HANDLE;
    }

    printf("Vulkan Surface successfully created.\n");
    return surface;
}

} // namespace Renderer