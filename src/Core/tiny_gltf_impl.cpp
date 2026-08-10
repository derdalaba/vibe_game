// Single translation unit that compiles tinygltf v3's implementation.
//
// stb is deliberately disabled here: Renderer/VulkanBackend.cpp already defines
// STB_IMAGE_IMPLEMENTATION against the extern/stb submodule. Letting tinygltf
// emit its own copy would produce duplicate stbi_* symbols at link time, and
// tinygltf bundles an older stb (v2.08) which could silently win on the include
// path. glTF image decoding is not needed yet; if it becomes needed, register a
// decoder via TinyGLTF::SetImageLoader rather than re-enabling these.
#define TINYGLTF3_IMPLEMENTATION
#define TINYGLTF_NO_STB_IMAGE
#define TINYGLTF_NO_STB_IMAGE_WRITE
#define TINYGLTF_NO_INCLUDE_STB_IMAGE
#define TINYGLTF_NO_INCLUDE_STB_IMAGE_WRITE
#define TINYGLTF3_ENABLE_FS
#include <tiny_gltf_v3.h>
