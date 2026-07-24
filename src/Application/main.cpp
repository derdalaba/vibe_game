#include <iostream>

#include "Device.hpp"  // Assuming this header exists and is accessible via Renderer/Core includes
#include "GLFW/glfw3.h"  // For window creation

int main() {
    if (!glfwInit()) {
        std::cerr << "Failed to initialize GLFW" << std::endl;
        return -1;
    }

    Renderer::Device device;
    device.initialize();

    device.run();

    std::cout << "Application initialization complete. Vulkan test function "
                 "was called."
              << std::endl;

    return 0;
}