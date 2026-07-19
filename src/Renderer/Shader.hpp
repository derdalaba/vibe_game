#pragma once

#include <string>
#include <fstream>

#define VULKAN_HPP_NO_STRUCT_CONSTRUCTORS
#include <vulkan/vulkan.hpp>
// or
#include <vulkan/vulkan_raii.hpp>

namespace Renderer {

class Shader {
public:
    Shader(const std::string& spirvFilePath, const vk::raii::Device& device);
    ~Shader();

private:

    std::vector<char> readSPIRVFile(const std::string& filePath); 

private:
    vk::PipelineShaderStageCreateInfo shaderStages[2];
    vk::raii::ShaderModule mShaderModule;
    
};
} // namespace Renderer