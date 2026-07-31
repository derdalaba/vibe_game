#include <iostream>

#include "Renderer.hpp"

int main() {
    try {
        Renderer::Renderer renderer;
        while (!renderer.shouldClose()) {
            renderer.render_frame();
        }
        renderer.shutdown();
    } catch (const std::exception& e) {
        std::cerr << "Fatal error: " << e.what() << std::endl;
        return -1;
    }

    return 0;
}
