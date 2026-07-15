# src/Application/CMakeLists.txt
set(application_sources)
# src/Application/CMakeLists.txt

set(application_sources)
if (EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/main.cpp")
    list(APPEND application_sources main.cpp)
else()
    message(FATAL_ERROR "main.cpp not found in ${CMAKE_CURRENT_SOURCE_DIR}. Please create it.")
endif()

add_executable(GameApp ${application_sources})

# Link everything needed for the game to run!
target_link_libraries(GameApp PRIVATE 
    Renderer 
    Physics 
    Core
    Audio
)

# Add include directories for dependencies so main.cpp can find headers like VulkanDevice.h
target_include_directories(GameApp PRIVATE 
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Renderer 
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Core 
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Physics 
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Audio
)
