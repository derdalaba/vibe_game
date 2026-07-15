# src/Renderer/CMakeLists.txt
add_library(Renderer STATIC
    Device.cpp
    Shader.cpp
    SwapChain.cpp
    Surface.cpp
)

# 1. Define public includes for Renderer itself (e.g., Shader.h location)
target_include_directories(Renderer PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${Vulkan_INCLUDE_DIRS}
)

# 2. Link dependencies! This is the crucial step.
# It ensures that:
# a) The compiler knows where Core headers are.
# b) The linker knows about all the symbols defined in the Core library.
target_link_libraries(Renderer PUBLIC Core Vulkan::Vulkan External::SDL2 External::GLFW) 
