#include "Shader.hpp"

namespace Renderer {

Shader::Shader(const std::string& spirvFilePath,
               const vk::raii::Device& device) {
    auto spirvCode = readSPIRVFile(spirvFilePath);

    vk::ShaderModuleCreateInfo createInfo{
        .codeSize = spirvCode.size() * sizeof(char),
        .pCode = reinterpret_cast<const uint32_t*>(spirvCode.data())};
    mShaderModule = vk::raii::ShaderModule(device, createInfo);

    vk::PipelineShaderStageCreateInfo vertShaderStageInfo{
        .stage = vk::ShaderStageFlagBits::eVertex,
        .module = mShaderModule,
        .pName = "vertMain"};
    vk::PipelineShaderStageCreateInfo fragShaderStageInfo{
        .stage = vk::ShaderStageFlagBits::eFragment,
        .module = mShaderModule,
        .pName = "fragMain"};
    vk::PipelineShaderStageCreateInfo mShaderStages[] = {vertShaderStageInfo,
                                                         fragShaderStageInfo};
}

Shader::~Shader() {
    // The vk::raii::ShaderModule will automatically clean up when it goes out
    // of scope.
}

Shader::Shader(Shader&& other) noexcept {
    mShaderModule = std::move(other.mShaderModule);
}

Shader& Shader::operator=(Shader&& other) noexcept {
    if (this != &other) {
        mShaderModule = std::move(other.mShaderModule);
    }
    return *this;
}

std::vector<char> Shader::readSPIRVFile(const std::string& filePath) {
    std::ifstream file(filePath, std::ios::ate | std::ios::binary);
    if (!file.is_open()) {
        throw std::runtime_error("Failed to open SPIR-V file: " + filePath);
    }

    size_t fileSize = file.tellg();
    std::vector<char> buffer(fileSize);
    file.seekg(0);
    file.read(buffer.data(), fileSize);
    file.close();

    return buffer;
}
}  // namespace Renderer