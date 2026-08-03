#include <GLFW/glfw3.h>

#include <chrono>
#include <iostream>

#include "Animation.hpp"
#include "Renderer.hpp"

namespace {

// A hand-authored keyframe clip, built entirely in code with no glTF file
// involved. Node 0 is the object's own transform by convention. This bobs the
// object up and down and swings it about Y, to show that the same interpolation
// core that plays glTF clips also plays programmatic tracks.
Core::AnimationClip makeBobClip() {
    Core::AnimationClip clip;
    const float quarter = glm::radians(35.0f);

    // Translation: down -> up -> down.
    clip.addKey(0, Core::Path::Translation, 0.0f, glm::vec4(0, 0.0f, 0, 0));
    clip.addKey(0, Core::Path::Translation, 1.0f, glm::vec4(0, 0.35f, 0, 0));
    clip.addKey(0, Core::Path::Translation, 2.0f, glm::vec4(0, 0.0f, 0, 0));

    // Rotation: quaternions are stored xyzw, matching glTF's convention.
    const glm::quat left = glm::angleAxis(quarter, glm::vec3(0, 1, 0));
    const glm::quat right = glm::angleAxis(-quarter, glm::vec3(0, 1, 0));
    const glm::quat none = glm::quat(1, 0, 0, 0);
    auto asVec4 = [](const glm::quat& q) {
        return glm::vec4(q.x, q.y, q.z, q.w);
    };
    clip.addKey(0, Core::Path::Rotation, 0.0f, asVec4(none));
    clip.addKey(0, Core::Path::Rotation, 0.5f, asVec4(left));
    clip.addKey(0, Core::Path::Rotation, 1.5f, asVec4(right));
    clip.addKey(0, Core::Path::Rotation, 2.0f, asVec4(none));

    return clip;
}

}  // namespace

int main() {
    try {
        Renderer::Renderer renderer;

        // Three instances of the loaded model, each running the same
        // hand-authored placement clip at a different phase.
        const Core::AnimationClip bob = makeBobClip();
        renderer.addObject(-1.5f, 0.0f, 0.0f);
        renderer.addObject(0.0f, 0.0f, 0.0f);
        renderer.addObject(1.5f, 0.0f, 0.0f);
        renderer.setObjectClip(0, &bob, 0.0f);
        renderer.setObjectClip(1, &bob, 0.66f);
        renderer.setObjectClip(2, &bob, 1.33f);

        std::cout << "glTF clips loaded from model: "
                  << renderer.modelClips().size() << std::endl;

        float camX = 0.0f;
        float camY = 1.0f;
        const float camZ = 4.0f;
        const float moveSpeed = 1.2f;  // units per second

        auto lastFrameTime = std::chrono::steady_clock::now();

        while (!renderer.shouldClose()) {
            auto now = std::chrono::steady_clock::now();
            float deltaTime =
                std::chrono::duration<float>(now - lastFrameTime).count();
            lastFrameTime = now;

            if (renderer.isKeyPressed(GLFW_KEY_W)) {
                camY += moveSpeed * deltaTime;
            }
            if (renderer.isKeyPressed(GLFW_KEY_S)) {
                camY -= moveSpeed * deltaTime;
            }
            if (renderer.isKeyPressed(GLFW_KEY_A)) {
                camX -= moveSpeed * deltaTime;
            }
            if (renderer.isKeyPressed(GLFW_KEY_D)) {
                camX += moveSpeed * deltaTime;
            }
            renderer.setCameraPosition(camX, camY, camZ);

            renderer.update(deltaTime);
            renderer.render_frame();
        }
        renderer.shutdown();
    } catch (const std::exception& e) {
        std::cerr << "Fatal error: " << e.what() << std::endl;
        return -1;
    }

    return 0;
}
