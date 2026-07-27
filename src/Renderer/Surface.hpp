#pragma once
#define VULKAN_HPP_NO_STRUCT_CONSTRUCTORS
#include <vulkan/vulkan.hpp>
// or
#include <GLFW/glfw3.h>

#include <vulkan/vulkan_raii.hpp>
#define VK_USE_PLATFORM_WIN32_KHR
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>

namespace Renderer {

class Surface {
   public:
    Surface() = default;
    ~Surface() { cleanup(); }

    void initialize(const vk::raii::Instance& instance);
    const vk::raii::SurfaceKHR& getSurface() const;
    GLFWwindow* getWindow() const;


   private:
    void initialize(const vk::raii::Instance& instance) {
        createWindow(800, 600, "Vibe Game");
        createSurface(instance);
    }
    void createWindow(int width, int height, const char* title) {
        if (!glfwInit()) {
            throw std::runtime_error("Failed to initialize GLFW");
        }

        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);  // No OpenGL context
        mWindowHandle =
            glfwCreateWindow(width, height, title, nullptr, nullptr);
        if (!mWindowHandle) {
            glfwTerminate();
            throw std::runtime_error("Failed to create GLFW window");
        }
    }

    const vk::raii::SurfaceKHR& getSurface() const { return mSurface; }
    GLFWwindow* getWindow() const { return mWindowHandle; }

    void createSurface(const vk::raii::Instance& instance) {
        if (mWindowHandle == nullptr) {
            throw std::runtime_error(
                "GLFW window handle is null. Cannot create surface.");
        }

        VkSurfaceKHR surface = nullptr;
        if (glfwCreateWindowSurface(*instance, mWindowHandle, nullptr,
                                    &surface) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create Vulkan surface.");
        }

        mSurface = vk::raii::SurfaceKHR(instance, surface);
    }
    void cleanup() {
        if (mSurface != nullptr) {
            mSurface =
                nullptr;
        }
        if (mWindowHandle) {
            glfwDestroyWindow(mWindowHandle);
            mWindowHandle = nullptr;
            glfwTerminate();
        }
    }


   private:
    GLFWwindow* mWindowHandle = nullptr;
    vk::raii::SurfaceKHR mSurface = nullptr;
};

}  // namespace Renderer
