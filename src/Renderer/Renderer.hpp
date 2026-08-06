#pragma once

#include <memory>
#include <string>

#include "Surface.hpp"
#include "VulkanBackend.hpp"

namespace Renderer {

class Renderer {
   public:
    Renderer(std::string modelPath = {});
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
    void switchModel(const std::string& modelPath);
    const std::vector<Core::AnimationClip>& modelClips() const;

   private:
    std::shared_ptr<Surface> mSurface;
    std::unique_ptr<VulkanBackend> mBackend;
};

}  // namespace Renderer
