#pragma once

#include <memory>

#include "Surface.hpp"
#include "VulkanBackend.hpp"

namespace Renderer {

class Renderer {
   public:
    Renderer();
    ~Renderer();

    bool shouldClose() const;
    void render_frame();
    void shutdown();

   private:
    std::shared_ptr<Surface> mSurface;
    std::unique_ptr<VulkanBackend> mBackend;
};

}  // namespace Renderer
