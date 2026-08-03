# src/Renderer/CMakeLists.txt
add_library(Renderer STATIC
    Device.cpp
    Shader.cpp
    SwapChain.cpp
    Surface.cpp
    Renderer.cpp
    VulkanBackend.cpp
)

target_include_directories(Renderer PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${Vulkan_INCLUDE_DIRS}
    ${CMAKE_SOURCE_DIR}/extern/stb
    ${CMAKE_SOURCE_DIR}/extern/glm
    ${CMAKE_SOURCE_DIR}/extern/tinyobjloader
)

target_link_libraries(Renderer PUBLIC Core Vulkan::Vulkan External::GLFW) 

option(BUILD_SHADERS "Build Slang shaders" ON)

cmake_path(GET CMAKE_CURRENT_SOURCE_DIR PARENT_PATH PARENT_DIR)
if(BUILD_SHADERS)
    find_program(SLANGC_EXECUTABLE
            NAMES slangc slangc.exe
            HINTS "$ENV{VULKAN_SDK}/Bin"
    )
    if (NOT SLANGC_EXECUTABLE)
            message(FATAL_ERROR "slangc not found. Ensure the VULKAN_SDK environment variable points to a Vulkan SDK install containing Bin/slangc.exe")
    endif()
else()
    message(STATUS "BUILD_SHADERS=OFF: shader compilation will be skipped; existing .spv files will be used if present")
endif()

target_compile_definitions(Renderer PRIVATE
    SHADER_DIR="${CMAKE_CURRENT_SOURCE_DIR}/Shaders"
    TEXTURE_DIR="${PARENT_DIR}/Textures"
    MODEL_DIR="${PARENT_DIR}/Models"
)
function (add_slang_shader_target TARGET SHADER_NAME)
  cmake_parse_arguments ("SHADER" "" "" "SOURCES" ${ARGN})
  set (SHADERS_DIR ${CMAKE_CURRENT_LIST_DIR}/Shaders)
  set (ENTRY_POINTS -entry vertMain -entry fragMain)
    if(BUILD_SHADERS)
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
        set_source_files_properties(${SHADERS_DIR}/${SHADER_NAME}.spv PROPERTIES GENERATED TRUE)
        target_sources(Renderer PRIVATE ${SHADERS_DIR}/${SHADER_NAME}.spv)
        MESSAGE(STATUS "Added Slang shader as generated source: ${SHADERS_DIR}/${SHADER_NAME}.spv from sources: ${SHADER_SOURCES}")
    else()
        set(SPV_FILE ${SHADERS_DIR}/${SHADER_NAME}.spv)
        if(EXISTS ${SPV_FILE})
            target_sources(Renderer PRIVATE ${SPV_FILE})
            MESSAGE(STATUS "Using existing SPV for ${SHADER_NAME}: ${SPV_FILE}")
        else()
            MESSAGE(WARNING "BUILD_SHADERS=OFF and ${SPV_FILE} not found; ${SHADER_NAME}.spv will be missing")
        endif()
    endif()
endfunction()
MESSAGE(STATUS "PARENT_DIR: ${PARENT_DIR}")
MESSAGE(STATUS "SLANGC_EXECUTABLE: ${SLANGC_EXECUTABLE}")
set (SHADER_SLANG_SOURCES ${PARENT_DIR}/Shaders/triangle/shader.slang)
add_slang_shader_target( triangle triangle SOURCES ${SHADER_SLANG_SOURCES}  )
add_slang_shader_target( box box SOURCES ${PARENT_DIR}/Shaders/box/shader.slang )