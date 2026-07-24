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

cmake_path(GET CMAKE_CURRENT_SOURCE_DIR PARENT_PATH PARENT_DIR)
find_program(SLANGC_EXECUTABLE
    NAMES slangc slangc.exe
    HINTS "$ENV{VULKAN_SDK}/Bin"
)
if (NOT SLANGC_EXECUTABLE)
    message(FATAL_ERROR "slangc not found. Ensure the VULKAN_SDK environment variable points to a Vulkan SDK install containing Bin/slangc.exe")
endif()

target_compile_definitions(Renderer PRIVATE SHADER_DIR="${CMAKE_CURRENT_SOURCE_DIR}/Shaders")
function (add_slang_shader_target TARGET SHADER_NAME)
  cmake_parse_arguments ("SHADER" "" "" "SOURCES" ${ARGN})
  set (SHADERS_DIR ${CMAKE_CURRENT_LIST_DIR}/Shaders)
  set (ENTRY_POINTS -entry vertMain -entry fragMain)
  add_custom_command (
          OUTPUT ${SHADERS_DIR}
          COMMAND ${CMAKE_COMMAND} -E make_directory ${SHADERS_DIR}
  )
  add_custom_command (
          OUTPUT  ${SHADERS_DIR}/${SHADER_NAME}.spv
          COMMAND ${SLANGC_EXECUTABLE} ${SHADER_SOURCES} -target spirv -profile spirv_1_4 -emit-spirv-directly -fvk-use-entrypoint-name ${ENTRY_POINTS} -o ${SHADER_NAME}.spv
          WORKING_DIRECTORY ${SHADERS_DIR}
          DEPENDS ${SHADERS_DIR} ${SHADER_SOURCES}
          COMMENT "Compiling Slang Shaders"
          VERBATIM
  )
  add_custom_target (${TARGET} DEPENDS ${SHADERS_DIR}/${SHADER_NAME}.spv)
  MESSAGE(STATUS "Added Slang shader target: ${TARGET} with sources: ${SHADER_SOURCES}")
endfunction()
MESSAGE(STATUS "PARENT_DIR: ${PARENT_DIR}")
MESSAGE(STATUS "SLANGC_EXECUTABLE: ${SLANGC_EXECUTABLE}")
set (SHADER_SLANG_SOURCES ${PARENT_DIR}/Shaders/triangle/shader.slang)
add_slang_shader_target( triangle triangle SOURCES ${SHADER_SLANG_SOURCES}  )
add_dependencies(Renderer triangle)