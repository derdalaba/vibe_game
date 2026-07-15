#include <iostream>
#include "Device.hpp" // Assuming this header exists and is accessible via Renderer/Core includes
#include "GLFW/glfw3.h" // For window creation

int main() {
    // 1. Initialize GLFW (required for Vulkan surface)
    if (!glfwInit()) {
        std::cerr << "Failed to initialize GLFW" << std::endl;
        return -1;
    }

    // 2. Create a dummy window for the device initialization test
    GLFWwindow* window = glfwCreateWindow(800, 600, "Vibe Game", NULL, NULL);
    if (!window) {
        std::cerr << "Failed to create GLFW window" << std::endl;
        glfwTerminate();
        return -1;
    }

    // 3. Initialize the Renderer/Vulkan Device
    Renderer::Device device; // Assuming VulkanDevice is accessible like this
    device.initialize(window);

    std::cout << "Application initialization complete. Vulkan test function was called." << std::endl;

    // Cleanup (simplified)
    glfwDestroyWindow(window);
    glfwTerminate();

    return 0;
}