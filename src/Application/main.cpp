#include <GLFW/glfw3.h>

#include <chrono>
#include <iostream>

#include "Renderer.hpp"

int main() {
    try {
        Renderer::Renderer renderer;

        float camX = 2.0f;
        float camY = 2.0f;
        const float camZ = 2.0f;
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

            renderer.render_frame();
        }
        renderer.shutdown();
    } catch (const std::exception& e) {
        std::cerr << "Fatal error: " << e.what() << std::endl;
        return -1;
    }

    return 0;
}
