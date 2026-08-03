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
    void update(float deltaTime);
    void render_frame();
    void shutdown();

    bool isKeyPressed(int key) const;
    void setCameraPosition(float x, float y, float z);
    void addObject(float x, float y, float z);
    void setObjectClip(size_t objectIndex, const Core::AnimationClip* clip,
                       float phaseOffset = 0.0f);
    const std::vector<Core::AnimationClip>& modelClips() const;

   private:
    std::shared_ptr<Surface> mSurface;
    std::unique_ptr<VulkanBackend> mBackend;
};

}  // namespace Renderer
