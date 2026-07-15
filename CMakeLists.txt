# CMakeLists.txt (Root)
cmake_minimum_required(VERSION 3.18) # Use a modern version
project(MyCustomEngine VERSION 1.0 LANGUAGES CXX)

# Standard requirements for all modules
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# --- Configuration Setup ---
# If you use external libs, set them up here or in the module itself
# For example: FindPackage(SDL2 REQUIRED)

function(add_optional_module module_path fallback_target)
	if (EXISTS "${CMAKE_SOURCE_DIR}/${module_path}/CMakeLists.txt")
		add_subdirectory(${module_path})
	elseif (NOT TARGET ${fallback_target})
		if (fallback_target MATCHES "::")
			add_library(${fallback_target} INTERFACE IMPORTED)
		else()
			add_library(${fallback_target} INTERFACE)
		endif()
	endif()
endfunction()

# --- Module Inclusion (The Orchestration) ---
# By adding these subdirectories, CMake executes their respective CMakeLists.txt
add_optional_module(extern/SDL2 External::SDL2)    # External Dependencies first

find_package(Vulkan REQUIRED)
if (Vulkan_FOUND)
	message(STATUS "Vulkan SDK found and configured.")
else()
	message(FATAL_ERROR "Vulkan SDK not found. Please ensure it is installed and accessible to CMake.")
endif()
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR}/CMake")
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
add_optional_module(extern/glfw glfw)
if (TARGET glfw AND NOT TARGET External::GLFW)
	add_library(External::GLFW ALIAS glfw)
endif()
add_optional_module(src/Core Core)      # Core foundation
add_optional_module(src/Renderer Renderer)  # Graphics system
add_optional_module(src/Physics Physics)   # Physics engine
add_optional_module(src/Audio Audio)     # Audio handling
add_subdirectory(src/Application)# The final executable target

message(STATUS "Engine build configured successfully.")
