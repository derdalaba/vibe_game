#pragma once

#include <fstream>
#include <string>
#include <utility>

#define VULKAN_HPP_NO_STRUCT_CONSTRUCTORS
#include <vulkan/vulkan.hpp>
// or
#include <vulkan/vulkan_raii.hpp>

namespace Renderer {

class Shader {
   public:
    Shader(const std::string& spirvFilePath, const vk::raii::Device& device);
    ~Shader();
    Shader(Shader&&) noexcept;
    Shader& operator=(Shader&&) noexcept;
    Shader(const Shader&) = delete;
    Shader& operator=(const Shader&) = delete;

   private:
    std::vector<char> readSPIRVFile(const std::string& filePath);

   private:
    vk::PipelineShaderStageCreateInfo shaderStages[2];
    vk::raii::ShaderModule mShaderModule = nullptr;
};
}  // namespace Renderer