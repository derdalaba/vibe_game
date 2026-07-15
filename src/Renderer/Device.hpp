#include <vulkan/vulkan.h>
#include <GLFW/glfw3.h> // For window creation and surface handling

namespace Renderer {

class Device {
public:
    void initialize(GLFWwindow* window);
private:
    VkInstance create_instance();
    VkSurfaceKHR create_surface(GLFWwindow* window);
    VkDevice create_device(VkPhysicalDevice physicalDevice, VkSurfaceKHR surface);
    VkApplicationInfo getAppInfo();

private:
    VkInstance mInstance;
    VkDevice mLogicalDevice;
    VkPhysicalDevice mPhysicalDevice;
    VkSurfaceKHR mSurface;
};
} // namespace Renderer