# src/Core/CMakeLists.txt
set(core_sources
    Math.cpp
    Vector3.cpp
    Animation.cpp
    Skeleton.cpp
    GltfLoader.cpp
    tiny_gltf_impl.cpp
)

set(core_available_sources)
foreach(source_file IN LISTS core_sources)
    if (EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${source_file}")
        list(APPEND core_available_sources ${source_file})
    endif()
endforeach()

if (core_available_sources)
    add_library(Core STATIC ${core_available_sources})
else()
    set(core_placeholder_source "${CMAKE_CURRENT_BINARY_DIR}/core_placeholder.cpp")
    file(WRITE "${core_placeholder_source}" "void core_placeholder() {}\n")
    add_library(Core STATIC ${core_placeholder_source})
endif()

target_include_directories(Core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/extern/glm
    ${CMAKE_SOURCE_DIR}/extern/tinygltf
)

target_link_libraries(Core PUBLIC External::spdlog)

target_compile_options(Core PRIVATE -Wall -Wextra)

# tinygltf (and the nlohmann json.hpp it bundles) is third-party and noisy under
# -Wall -Wextra; silence just that TU rather than weakening Core's flags.
if (EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tiny_gltf_impl.cpp")
    set_source_files_properties(tiny_gltf_impl.cpp PROPERTIES COMPILE_OPTIONS "-w")
endif()
