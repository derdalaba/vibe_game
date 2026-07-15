# Vibe Game

## Build Layout

This workspace is set up with a CMake-based engine layout:

- `src/Core` for shared math and utility code
- `src/Renderer` for graphics integration
- `src/Application` for the executable entry point
- `extern/glfw` as a Git submodule for GLFW

## Current CMake Behavior

The root CMake configuration is intentionally tolerant of a partial workspace while the project is still being filled in:

- If `extern/SDL2` or `extern/VulkanSDK` are present, they are added as subdirectories.
- If they are missing, CMake creates stub interface targets so configuration can still complete.
- GLFW is expected at `extern/glfw` and is exposed to the rest of the project as `External::GLFW`.
- `src/Core` and `src/Application` fall back to generated placeholder sources when their real source files are not present yet.

## Notes

If you add the missing engine modules later, the root build will automatically use them once their `CMakeLists.txt` files and source files exist.
