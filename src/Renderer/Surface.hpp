#pragma once
#define VULKAN_HPP_NO_STRUCT_CONSTRUCTORS
#include <vulkan/vulkan.hpp>
// or
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
    Surface(const Surface&) = delete;
    Surface& operator=(const Surface&) = delete;
    Surface(Surface&&) = delete;
    Surface& operator=(Surface&&) = delete;

    void createWindow(int width, int height, const char* title);
    void createSurface(const vk::raii::Instance& instance);
    const vk::raii::SurfaceKHR& getSurface() const { return mSurface; }
    GLFWwindow* getWindow() const { return mWindowHandle; }

   private:
    void cleanup();

   private:
    GLFWwindow* mWindowHandle = nullptr;
    vk::raii::SurfaceKHR mSurface = nullptr;
};

}  // namespace Renderer
