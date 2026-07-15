#pragma once
#include <vulkan/vulkan.h> 
#include <GLFW/glfw3.h>

namespace Renderer {

class Surface {
public:
// Engine call: Takes a window handle and the Vulkan instance to create the surface.
static VkSurfaceKHR create_surface(GLFWwindow* windowHandle, VkInstance instance); 

private:
    // Private members might hold internal state, but usually this is just static helper code.
};

} // namespace Renderer
