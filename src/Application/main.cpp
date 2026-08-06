#include <GLFW/glfw3.h>

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include <spdlog/common.h>
#include <spdlog/sinks/base_sink.h>
#include <spdlog/spdlog.h>

#include "Animation.hpp"
#include "Renderer.hpp"

namespace {

class LevelStyledSink : public spdlog::sinks::base_sink<std::mutex> {
   protected:
    void sink_it_(const spdlog::details::log_msg& msg) override {
        const char* color = "\033[38;5;177m";
        const char* label = "INFO";

        switch (msg.level) {
            case spdlog::level::warn:
                color = "\033[38;5;226m";
                label = "WARN";
                break;
            case spdlog::level::err:
            case spdlog::level::critical:
                color = "\033[38;5;196m";
                label = "ERROR";
                break;
            default:
                break;
        }

        std::fputs(color, stdout);
        std::fputs("[", stdout);
        std::fputs(label, stdout);
        std::fputs("] ", stdout);
        std::fwrite(msg.payload.data(), 1, msg.payload.size(), stdout);
        std::fputs("\033[0m\n", stdout);
        std::fflush(stdout);
    }

    void flush_() override { std::fflush(stdout); }
};

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

std::string resolveModelPath(const std::string& requested) {
    if (requested.empty()) {
        return {};
    }

    const std::filesystem::path requestedPath(requested);
    if (std::filesystem::exists(requestedPath)) {
        return requestedPath.string();
    }

    const std::filesystem::path repoRelative = std::filesystem::path("src") / "Models" / requestedPath.filename();
    if (std::filesystem::exists(repoRelative)) {
        return repoRelative.string();
    }

    const std::filesystem::path parentRelative = std::filesystem::path("..") / "src" / "Models" / requestedPath.filename();
    if (std::filesystem::exists(parentRelative)) {
        return parentRelative.string();
    }

    const std::vector<std::filesystem::path> defaults = {
        std::filesystem::path("src/Models/SimpleSkin.gltf"),
        std::filesystem::path("../src/Models/SimpleSkin.gltf"),
        std::filesystem::path("Models/SimpleSkin.gltf"),
    };
    for (const auto& candidate : defaults) {
        if (std::filesystem::exists(candidate)) {
            return candidate.string();
        }
    }
    return std::string("src/Models/SimpleSkin.gltf");
}

}  // namespace

int main(int argc, char** argv) {
    auto sink = std::make_shared<LevelStyledSink>();
    auto logger = std::make_shared<spdlog::logger>("VibeGame", sink);
    logger->set_level(spdlog::level::trace);
    logger->flush_on(spdlog::level::err);
    spdlog::register_logger(logger);
    spdlog::set_default_logger(logger);
    try {
        const std::string requestedPath = argc > 1 ? argv[1] : "";
        const std::string alternatePath = argc > 2 ? argv[2] : "";
        const std::string modelPath = resolveModelPath(requestedPath);
        logger->info("Loading model: {}", modelPath);

        Renderer::Renderer renderer(modelPath);

        logger->info("Renderer initialized.");
        // Three instances of the loaded model, each running the same
        // hand-authored placement clip at a different phase.
        const Core::AnimationClip bob = makeBobClip();
        renderer.addObject(-1.5f, 0.0f, 0.0f);
        renderer.addObject(0.0f, 0.0f, 0.0f);
        renderer.addObject(1.5f, 0.0f, 0.0f);
        renderer.setObjectClip(0, &bob, 0.0f);
        renderer.setObjectClip(1, &bob, 0.66f);
        renderer.setObjectClip(2, &bob, 1.33f);

        logger->info("glTF clips loaded from model: {}", renderer.modelClips().size());

        float camX = 0.0f;
        float camY = 1.0f;
        const float camZ = 4.0f;
        const float moveSpeed = 1.2f;  // units per second
        bool lastReloadPressed = false;
        bool lastAltReloadPressed = false;

        auto lastFrameTime = std::chrono::steady_clock::now();

        while (!renderer.shouldClose()) {
            auto now = std::chrono::steady_clock::now();
            float deltaTime = std::chrono::duration<float>(now - lastFrameTime).count();
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

            const bool reloadPressed = renderer.isKeyPressed(GLFW_KEY_R);
            if (reloadPressed && !lastReloadPressed) {
                renderer.switchModel(modelPath);
            }
            lastReloadPressed = reloadPressed;

            const bool altReloadPressed = renderer.isKeyPressed(GLFW_KEY_T);
            if (altReloadPressed && !lastAltReloadPressed && !alternatePath.empty()) {
                renderer.switchModel(alternatePath);
            }
            lastAltReloadPressed = altReloadPressed;

            renderer.setCameraPosition(camX, camY, camZ);

            renderer.update(deltaTime);
            renderer.render_frame();
        }
        renderer.shutdown();
    } catch (const std::exception& e) {
        logger->error("Fatal error: {}", e.what());
        return -1;
    }

    return 0;
}
