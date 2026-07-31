#include "Renderer.hpp"

#include <GLFW/glfw3.h>

namespace Renderer {

Renderer::Renderer()
    : mSurface(std::make_shared<Surface>()),
      mBackend(std::make_unique<VulkanBackend>(mSurface)) {}

Renderer::~Renderer() = default;

bool Renderer::shouldClose() const {
    return glfwWindowShouldClose(mSurface->getWindow());
}

void Renderer::render_frame() {
    glfwPollEvents();
    mBackend->render_frame();
}

void Renderer::shutdown() {
    mBackend.reset();
    mSurface.reset();
}

}  // namespace Renderer
