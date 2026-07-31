#include "Surface.hpp"

#include <stdexcept>

namespace Renderer {

void Surface::createWindow(int width, int height, const char* title) {
    if (!glfwInit()) {
        throw std::runtime_error("Failed to initialize GLFW");
    }

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);  // No OpenGL context
    mWindowHandle = glfwCreateWindow(width, height, title, nullptr, nullptr);
    if (!mWindowHandle) {
        glfwTerminate();
        throw std::runtime_error("Failed to create GLFW window");
    }
}

void Surface::createSurface(const vk::raii::Instance& instance) {
    if (mWindowHandle == nullptr) {
        throw std::runtime_error(
            "GLFW window handle is null. Cannot create surface.");
    }

    VkSurfaceKHR surface = nullptr;
    VkResult result =
        glfwCreateWindowSurface(*instance, mWindowHandle, nullptr, &surface);
    if (result != VK_SUCCESS) {
        const char* description = nullptr;
        glfwGetError(&description);
        throw std::runtime_error(
            "Failed to create Vulkan surface. VkResult=" +
            std::to_string(result) + " glfwError=" +
            (description ? description : "none"));
    }

    mSurface = vk::raii::SurfaceKHR(instance, surface);
}

void Surface::cleanup() {
    if (mSurface != nullptr) {
        mSurface = nullptr;
    }
    if (mWindowHandle) {
        glfwDestroyWindow(mWindowHandle);
        mWindowHandle = nullptr;
        glfwTerminate();
    }
}

}  // namespace Renderer
