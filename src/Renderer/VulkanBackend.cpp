#include "VulkanBackend.hpp"

#include <GLFW/glfw3.h>

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <source_location>
#include <stdexcept>
#include <string>

namespace Renderer {
namespace {

[[noreturn]] void throwWithLocation(std::string message, std::source_location location = std::source_location::current()) {
    throw std::runtime_error(std::string(location.function_name()) + ": " + std::move(message));
}

}  // namespace

#ifdef NDEBUG
static constexpr bool kEnableValidation = false;
#else
static constexpr bool kEnableValidation = true;
#endif

struct UniformBufferObject {
    glm::mat4 view;
    glm::mat4 proj;
};

// Must mirror PushConstants in src/Shaders/box/shader.slang. The emitted SPIR-V
// places `model` at offset 0 and `jointOffset` at offset 64.
struct PushConstants {
    glm::mat4 model;
    uint32_t jointOffset;
};
static_assert(sizeof(PushConstants) == 68, "push constant layout must match the shader's std430 offsets");

static VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(VkDebugUtilsMessageSeverityFlagBitsEXT, VkDebugUtilsMessageTypeFlagsEXT,
                                                    const VkDebugUtilsMessengerCallbackDataEXT* callbackData, void*) {
    fprintf(stderr, "[vulkan] %s\n", callbackData->pMessage);
    return VK_FALSE;
}

static void framebufferResizeCallback(GLFWwindow* window, int, int) {
    auto* backend = static_cast<VulkanBackend*>(glfwGetWindowUserPointer(window));
    backend->notifyFramebufferResized();
}

VulkanBackend::VulkanBackend(std::shared_ptr<Surface> surface, std::string modelPath)
    : mSurface(std::move(surface)), mModelPath(std::move(modelPath)), mLogger{spdlog::get("VibeGame")} {

    mLogger->info("Initializing Vulkan backend...");
    mStartTime = std::chrono::steady_clock::now();
    mSurface->createWindow(800, 600, "Vibe Game");
    glfwSetWindowUserPointer(mSurface->getWindow(), this);
    glfwSetFramebufferSizeCallback(mSurface->getWindow(), framebufferResizeCallback);
    create_instance();
    mSurface->createSurface(mInstance);
    mDevice = Device{};
    mLogger->info("Creating Vulkan device...");
    mDevice.initialize(mInstance, mSurface->getSurface());
    mLogger->info("Creating Vulkan swap chain...");
    mSwapChain.initialize(mDevice.physicalDevice(), mDevice.logicalDevice(), *mSurface);
    create_command_pool_and_buffers();
    create_descriptor_set_layout();
    mLogger->info("Creating graphics pipeline...");
    create_graphics_pipeline();
    mLogger->info("Loading model from {}", mModelPath.empty() ? "default path" : mModelPath);
    try {
        load_model(mModelPath);
    } catch (const std::exception& e) {
        mLogger->error("Failed to load model: {}", e.what());
        throw;
    }
    mLogger->info("Creating vertex buffer...");
    create_vertex_buffer();
    mLogger->info("Creating index buffer...");
    create_index_buffer();
    mLogger->info("Creating texture image...");
    create_texture_image();
    mLogger->info("Creating texture image view...");
    create_texture_image_view();
    mLogger->info("Creating texture sampler...");
    create_texture_sampler();
    mLogger->info("Creating uniform buffers...");
    create_uniform_buffers();
    mLogger->info("Creating joint buffers for up to {} skinned instances...", kMaxSkinnedInstances);
    create_joint_buffers();
    create_descriptor_pool();
    create_descriptor_sets();
    create_sync_objects();
}

VulkanBackend::~VulkanBackend() {
    mLogger->info("Destroying Vulkan backend...");
    destroy_resources();
}

void VulkanBackend::destroy_resources() {
    if (mDevice.logicalDevice() != nullptr) {
        mDevice.logicalDevice().waitIdle();
    }

    mRenderFinishedSemaphores.clear();
    mPresentCompleteSemaphores.clear();
    mInFlightFences.clear();

    mDescriptorSets.clear();
    mLogger->info("Destroying descriptor pool...");
    mDescriptorPool = nullptr;

    mJointBuffers.clear();
    mJointBuffersMemory.clear();
    mJointBuffersMapped.clear();
    mUniformBuffers.clear();
    mUniformBuffersMemory.clear();
    mUniformBuffersMapped.clear();

    mLogger->info("Destroying texture sampler...");
    mTextureSampler = nullptr;
    mTextureImageView = nullptr;
    mTextureImage = nullptr;
    mTextureImageMemory = nullptr;

    mLogger->info("Destroying buffers...");
    mIndexBuffer = nullptr;
    mIndexBufferMemory = nullptr;
    mVertexBuffer = nullptr;
    mVertexBufferMemory = nullptr;
    mStagingBuffer = nullptr;
    mStagingBufferMemory = nullptr;

    mLogger->info("Destroying graphics pipeline...");
    mGraphicsPipeline = nullptr;
    mPipelineLayout = nullptr;
    mShaders.clear();
    mDescriptorSetLayout = nullptr;

    mCommandBuffers.clear();
    mCommandPool = nullptr;
    mSwapChain = SwapChain{};

    mDevice = Device{};

    if (mSurface) {
        mSurface->cleanup();
        mSurface.reset();
    }

    mDebugMessenger = nullptr;
    mInstance = nullptr;
}

void VulkanBackend::notifyFramebufferResized() {
    mFramebufferResized = true;
}

void VulkanBackend::setCameraPosition(float x, float y, float z) {
    mCameraPosition[0] = x;
    mCameraPosition[1] = y;
    mCameraPosition[2] = z;
}

void VulkanBackend::addObject(float x, float y, float z) {
    RenderObject object;
    object.transform.translation = glm::vec3(x, y, z);
    mObjects.push_back(object);
}

void VulkanBackend::setObjectClip(size_t objectIndex, const Core::AnimationClip* clip, float phaseOffset) {
    if (objectIndex >= mObjects.size()) {
        throwWithLocation("setObjectClip: object index out of range");
    }
    mObjects[objectIndex].placementClip = clip;
    mObjects[objectIndex].animationOffset = phaseOffset;
}

void VulkanBackend::switchModel(const std::string& modelPath) {
    std::string resolvedPath = modelPath;
    if (resolvedPath.empty()) {
        resolvedPath = mModelPath;
    }
    if (resolvedPath.empty()) {
        resolvedPath = std::string(MODEL_DIR) + "/SimpleSkin.gltf";
    }

    mLogger->info("Switching model to {}", resolvedPath);
    mDevice.logicalDevice().waitIdle();
    mModel = Core::loadModel(resolvedPath);
    mModelPath = resolvedPath;
    mSkinTime = 0.0f;
    mJointMatrices.assign(std::max<size_t>(mModel.skeleton.jointCount(), 1), glm::mat4(1.0f));

    recreate_model_resources();
}

void VulkanBackend::update(float deltaTime) {
    // Rigid (per-object placement) animation: sample each object's own clip.
    // Node 0 of a placement clip is the object's transform by convention.
    for (auto& object : mObjects) {
        if (object.placementClip == nullptr) {
            continue;
        }
        object.animationTime += deltaTime;
        const float duration = object.placementClip->duration;
        float time = object.animationTime + object.animationOffset;
        if (duration > 0.0f) {
            time = std::fmod(time, duration);
        }
        std::vector<Core::Transform> nodes(1, object.transform);
        Core::sample(*object.placementClip, time, nodes);
        object.transform = nodes[0];
    }

    // Skinned pose, per instance: each object plays the model's glTF clip at
    // its own phase, so every instance needs its own block of joint matrices.
    // They are concatenated into one buffer and selected at draw time via the
    // jointOffset push constant.
    if (!mModel.skeleton.isSkinned() || mModel.clips.empty()) {
        return;
    }
    if (mObjects.size() > kMaxSkinnedInstances) {
        throwWithLocation(
            "more skinned instances than the joint buffer was sized for; raise "
            "kMaxSkinnedInstances");
    }

    mSkinTime += deltaTime;
    const Core::AnimationClip& clip = mModel.clips[0];
    const size_t joints = mModel.skeleton.jointCount();
    mJointMatrices.assign(mObjects.size() * joints, glm::mat4(1.0f));

    std::vector<glm::mat4> instanceMatrices;
    for (size_t i = 0; i < mObjects.size(); ++i) {
        float time = mSkinTime + mObjects[i].animationOffset;
        if (clip.duration > 0.0f) {
            time = std::fmod(time, clip.duration);
        }
        mModel.skeleton.localPose = mModel.skeleton.restPose;
        Core::sample(clip, time, mModel.skeleton.localPose);
        Core::computeJointMatrices(mModel.skeleton, instanceMatrices);
        std::copy(instanceMatrices.begin(), instanceMatrices.end(), mJointMatrices.begin() + static_cast<ptrdiff_t>(i * joints));
    }
}

void VulkanBackend::create_instance() {
    constexpr vk::ApplicationInfo appInfo{.pApplicationName = "Hello Triangle",
                                          .applicationVersion = VK_MAKE_VERSION(1, 0, 0),
                                          .pEngineName = "No Engine",
                                          .engineVersion = VK_MAKE_VERSION(1, 0, 0),
                                          .apiVersion = vk::ApiVersion14};

    uint32_t glfwExtensionCount = 0;
    auto glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);
    if (!glfwExtensions) {
        const char* description = nullptr;
        glfwGetError(&description);
        throwWithLocation("glfwGetRequiredInstanceExtensions failed: " + std::string(description ? description : "unknown error"));
    }

    auto extensionProperties = mContext.enumerateInstanceExtensionProperties();
    for (uint32_t i = 0; i < glfwExtensionCount; ++i) {
        if (std::ranges::none_of(extensionProperties, [glfwExtension = glfwExtensions[i]](auto const& extensionProperty) {
                return strcmp(extensionProperty.extensionName, glfwExtension) == 0;
            })) {
            throwWithLocation("Required GLFW extension not supported: " + std::string(glfwExtensions[i]));
        }
    }

    std::vector<const char*> extensions(glfwExtensions, glfwExtensions + glfwExtensionCount);
    std::vector<const char*> layers;
    if (kEnableValidation) {
        extensions.push_back(vk::EXTDebugUtilsExtensionName);
        layers.push_back("VK_LAYER_KHRONOS_validation");
    }

    vk::InstanceCreateInfo createInfo{.pApplicationInfo = &appInfo,
                                      .enabledLayerCount = static_cast<uint32_t>(layers.size()),
                                      .ppEnabledLayerNames = layers.data(),
                                      .enabledExtensionCount = static_cast<uint32_t>(extensions.size()),
                                      .ppEnabledExtensionNames = extensions.data()};

    mInstance = std::move(vk::raii::Instance(mContext, createInfo));

    if (kEnableValidation) {
        vk::DebugUtilsMessengerCreateInfoEXT messengerInfo{
            .messageSeverity = vk::DebugUtilsMessageSeverityFlagBitsEXT::eWarning | vk::DebugUtilsMessageSeverityFlagBitsEXT::eError,
            .messageType = vk::DebugUtilsMessageTypeFlagBitsEXT::eGeneral | vk::DebugUtilsMessageTypeFlagBitsEXT::eValidation |
                           vk::DebugUtilsMessageTypeFlagBitsEXT::ePerformance,
            .pfnUserCallback = reinterpret_cast<vk::PFN_DebugUtilsMessengerCallbackEXT>(debugCallback)};
        mDebugMessenger = vk::raii::DebugUtilsMessengerEXT(mInstance, messengerInfo);
    }
}

void VulkanBackend::create_descriptor_set_layout() {
    vk::DescriptorSetLayoutBinding uboBinding{.binding = 0,
                                              .descriptorType = vk::DescriptorType::eUniformBuffer,
                                              .descriptorCount = 1,
                                              .stageFlags = vk::ShaderStageFlagBits::eVertex};
    vk::DescriptorSetLayoutBinding samplerBinding{.binding = 1,
                                                  .descriptorType = vk::DescriptorType::eCombinedImageSampler,
                                                  .descriptorCount = 1,
                                                  .stageFlags = vk::ShaderStageFlagBits::eFragment};
    // Reading a storage buffer in the vertex stage needs no device feature;
    // only stores/atomics would require vertexPipelineStoresAndAtomics.
    vk::DescriptorSetLayoutBinding jointBinding{.binding = 2,
                                                .descriptorType = vk::DescriptorType::eStorageBuffer,
                                                .descriptorCount = 1,
                                                .stageFlags = vk::ShaderStageFlagBits::eVertex};
    std::array<vk::DescriptorSetLayoutBinding, 3> bindings{uboBinding, samplerBinding, jointBinding};
    vk::DescriptorSetLayoutCreateInfo layoutInfo{.bindingCount = static_cast<uint32_t>(bindings.size()), .pBindings = bindings.data()};
    mDescriptorSetLayout = vk::raii::DescriptorSetLayout(mDevice.logicalDevice(), layoutInfo);
}

void VulkanBackend::create_graphics_pipeline() {
    mShaders.emplace_back(std::string(SHADER_DIR) + "/box.spv", mDevice.logicalDevice());

    auto bindingDescription = vertexBindingDescription();
    auto attributeDescriptions = vertexAttributeDescriptions();
    vk::PipelineVertexInputStateCreateInfo vertexInputInfo{
        .vertexBindingDescriptionCount = 1,
        .pVertexBindingDescriptions = &bindingDescription,
        .vertexAttributeDescriptionCount = static_cast<uint32_t>(attributeDescriptions.size()),
        .pVertexAttributeDescriptions = attributeDescriptions.data()};

    vk::PipelineInputAssemblyStateCreateInfo inputAssembly{.topology = vk::PrimitiveTopology::eTriangleList};

    std::vector<vk::DynamicState> dynamicStates = {vk::DynamicState::eViewport, vk::DynamicState::eScissor};
    vk::PipelineDynamicStateCreateInfo dynamicState{.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size()),
                                                    .pDynamicStates = dynamicStates.data()};

    vk::PipelineViewportStateCreateInfo viewportState{.viewportCount = 1, .scissorCount = 1};

    vk::PipelineRasterizationStateCreateInfo rasterizer{.depthClampEnable = vk::False,
                                                        .rasterizerDiscardEnable = vk::False,
                                                        .polygonMode = vk::PolygonMode::eFill,
                                                        .cullMode = vk::CullModeFlagBits::eBack,
                                                        .frontFace = vk::FrontFace::eCounterClockwise,
                                                        .depthBiasEnable = vk::False,
                                                        .lineWidth = 1.0f};

    vk::PipelineMultisampleStateCreateInfo multisampling{.rasterizationSamples = vk::SampleCountFlagBits::e1,
                                                         .sampleShadingEnable = vk::False};

    vk::PipelineColorBlendAttachmentState colorBlendAttachment{
        .blendEnable = vk::False,
        .colorWriteMask = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG | vk::ColorComponentFlagBits::eB |
                          vk::ColorComponentFlagBits::eA};
    vk::PipelineColorBlendStateCreateInfo colorBlending{
        .logicOpEnable = vk::False, .logicOp = vk::LogicOp::eCopy, .attachmentCount = 1, .pAttachments = &colorBlendAttachment};

    vk::PipelineDepthStencilStateCreateInfo depthStencil{.depthTestEnable = vk::True,
                                                         .depthWriteEnable = vk::True,
                                                         .depthCompareOp = vk::CompareOp::eLess,
                                                         .depthBoundsTestEnable = vk::False,
                                                         .stencilTestEnable = vk::False};

    vk::PushConstantRange pushConstantRange{.stageFlags = vk::ShaderStageFlagBits::eVertex, .offset = 0, .size = sizeof(PushConstants)};
    vk::PipelineLayoutCreateInfo pipelineLayoutInfo{
        .setLayoutCount = 1, .pSetLayouts = &*mDescriptorSetLayout, .pushConstantRangeCount = 1, .pPushConstantRanges = &pushConstantRange};
    mPipelineLayout = vk::raii::PipelineLayout(mDevice.logicalDevice(), pipelineLayoutInfo);

    vk::Format colorFormat = mSwapChain.surfaceFormat().format;
    vk::StructureChain<vk::GraphicsPipelineCreateInfo, vk::PipelineRenderingCreateInfo> pipelineCreateInfoChain = {
        {.stageCount = Shader::stageCount(),
         .pStages = mShaders[0].stages(),
         .pVertexInputState = &vertexInputInfo,
         .pInputAssemblyState = &inputAssembly,
         .pViewportState = &viewportState,
         .pRasterizationState = &rasterizer,
         .pMultisampleState = &multisampling,
         .pDepthStencilState = &depthStencil,
         .pColorBlendState = &colorBlending,
         .pDynamicState = &dynamicState,
         .layout = mPipelineLayout,
         .renderPass = nullptr},
        {.colorAttachmentCount = 1, .pColorAttachmentFormats = &colorFormat, .depthAttachmentFormat = mSwapChain.depthFormat()}};

    mGraphicsPipeline = vk::raii::Pipeline(mDevice.logicalDevice(), nullptr, pipelineCreateInfoChain.get<vk::GraphicsPipelineCreateInfo>());
}

void VulkanBackend::load_model(const std::string& modelPath) {
    // Asset loading lives in Core and returns Vulkan-free plain data; this
    // backend only uploads it. glTF primitives are already indexed, so no
    // vertex de-duplication pass is needed here.
    std::string resolvedPath = modelPath;
    if (resolvedPath.empty()) {
        resolvedPath = std::string(MODEL_DIR) + "/SimpleSkin.gltf";
    }
    mModelPath = resolvedPath;
    mModel = Core::loadModel(resolvedPath);
    if (mModel.mesh.vertices.empty() || mModel.mesh.indices.empty()) {
        throwWithLocation("loaded model has no geometry");
    }
    mJointMatrices.assign(std::max<size_t>(mModel.skeleton.jointCount(), 1), glm::mat4(1.0f));
}

uint32_t VulkanBackend::findMemoryType(uint32_t typeFilter, vk::MemoryPropertyFlags properties) {
    vk::PhysicalDeviceMemoryProperties memProperties = mDevice.physicalDevice().getMemoryProperties();
    for (uint32_t i = 0; i < memProperties.memoryTypeCount; ++i) {
        if ((typeFilter & (1u << i)) && (memProperties.memoryTypes[i].propertyFlags & properties) == properties) {
            return i;
        }
    }
    throwWithLocation("failed to find suitable memory type!");
}

void VulkanBackend::copyBuffer(vk::raii::Buffer const& srcBuffer, vk::raii::Buffer const& dstBuffer, vk::DeviceSize size) {
    vk::CommandBufferAllocateInfo allocInfo{
        .commandPool = mCommandPool, .level = vk::CommandBufferLevel::ePrimary, .commandBufferCount = 1};
    vk::raii::CommandBuffer commandBuffer = std::move(mDevice.logicalDevice().allocateCommandBuffers(allocInfo).front());

    commandBuffer.begin(vk::CommandBufferBeginInfo{.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit});
    commandBuffer.copyBuffer(srcBuffer, dstBuffer, {vk::BufferCopy{0, 0, size}});
    commandBuffer.end();

    vk::raii::Queue graphicsQueue = mDevice.graphicsQueue();
    graphicsQueue.submit({vk::SubmitInfo{.commandBufferCount = 1, .pCommandBuffers = &*commandBuffer}}, nullptr);
    graphicsQueue.waitIdle();
}

void VulkanBackend::copyBufferToImage(const vk::raii::Buffer& buffer, const vk::raii::Image& image, uint32_t width, uint32_t height) {
    vk::CommandBufferAllocateInfo allocInfo{
        .commandPool = mCommandPool, .level = vk::CommandBufferLevel::ePrimary, .commandBufferCount = 1};
    vk::raii::CommandBuffer commandBuffer = std::move(mDevice.logicalDevice().allocateCommandBuffers(allocInfo).front());

    commandBuffer.begin(vk::CommandBufferBeginInfo{.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit});

    vk::BufferImageCopy region{.bufferOffset = 0,
                               .bufferRowLength = 0,
                               .bufferImageHeight = 0,
                               .imageSubresource = {vk::ImageAspectFlagBits::eColor, 0, 0, 1},
                               .imageOffset = {0, 0, 0},
                               .imageExtent = {width, height, 1}};
    commandBuffer.copyBufferToImage(buffer, image, vk::ImageLayout::eTransferDstOptimal, {region});

    commandBuffer.end();

    vk::raii::Queue graphicsQueue = mDevice.graphicsQueue();
    graphicsQueue.submit({vk::SubmitInfo{.commandBufferCount = 1, .pCommandBuffers = &*commandBuffer}}, nullptr);
    graphicsQueue.waitIdle();
}

void VulkanBackend::recreate_model_resources() {
    mVertexBuffer = nullptr;
    mVertexBufferMemory = nullptr;
    mIndexBuffer = nullptr;
    mIndexBufferMemory = nullptr;
    mStagingBuffer = nullptr;
    mStagingBufferMemory = nullptr;

    mJointBuffers.clear();
    mJointBuffersMemory.clear();
    mJointBuffersMapped.clear();
    mDescriptorPool = nullptr;
    mDescriptorSets.clear();

    create_vertex_buffer();
    create_index_buffer();
    create_joint_buffers();
    create_descriptor_pool();
    create_descriptor_sets();
}

void VulkanBackend::create_vertex_buffer() {
    vk::DeviceSize bufferSize = sizeof(Core::Vertex) * mModel.mesh.vertices.size();

    std::tie(mStagingBuffer, mStagingBufferMemory) =
        create_buffer(bufferSize, vk::BufferUsageFlagBits::eTransferSrc,
                      vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);

    void* dataStaging = mStagingBufferMemory.mapMemory(0, bufferSize);
    memcpy(dataStaging, mModel.mesh.vertices.data(), static_cast<size_t>(bufferSize));
    mStagingBufferMemory.unmapMemory();

    std::tie(mVertexBuffer, mVertexBufferMemory) =
        create_buffer(bufferSize, vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eVertexBuffer,
                      vk::MemoryPropertyFlagBits::eDeviceLocal);
    copyBuffer(mStagingBuffer, mVertexBuffer, bufferSize);
}

void VulkanBackend::create_index_buffer() {
    vk::DeviceSize bufferSize = sizeof(uint32_t) * mModel.mesh.indices.size();

    std::tie(mStagingBuffer, mStagingBufferMemory) =
        create_buffer(bufferSize, vk::BufferUsageFlagBits::eTransferSrc,
                      vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);

    void* dataStaging = mStagingBufferMemory.mapMemory(0, bufferSize);
    memcpy(dataStaging, mModel.mesh.indices.data(), static_cast<size_t>(bufferSize));
    mStagingBufferMemory.unmapMemory();

    std::tie(mIndexBuffer, mIndexBufferMemory) =
        create_buffer(bufferSize, vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eIndexBuffer,
                      vk::MemoryPropertyFlagBits::eDeviceLocal);
    copyBuffer(mStagingBuffer, mIndexBuffer, bufferSize);
}

void VulkanBackend::create_texture_image() {
    int texWidth = 0, texHeight = 0, texChannels = 0;
    std::string texturePath = std::string(TEXTURE_DIR) + "/checkerboard.ppm";
    stbi_uc* pixels = stbi_load(texturePath.c_str(), &texWidth, &texHeight, &texChannels, STBI_rgb_alpha);
    if (!pixels) {
        throwWithLocation("failed to load texture image: " + texturePath);
    }
    vk::DeviceSize imageSize = static_cast<vk::DeviceSize>(texWidth) * texHeight * 4;

    std::tie(mStagingBuffer, mStagingBufferMemory) =
        create_buffer(imageSize, vk::BufferUsageFlagBits::eTransferSrc,
                      vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);

    void* data = mStagingBufferMemory.mapMemory(0, imageSize);
    memcpy(data, pixels, static_cast<size_t>(imageSize));
    mStagingBufferMemory.unmapMemory();
    stbi_image_free(pixels);

    std::tie(mTextureImage, mTextureImageMemory) = create_image(
        static_cast<uint32_t>(texWidth), static_cast<uint32_t>(texHeight), vk::Format::eR8G8B8A8Srgb, vk::ImageTiling::eOptimal,
        vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled, vk::MemoryPropertyFlagBits::eDeviceLocal);

    transition_texture_image_layout(mTextureImage, vk::ImageLayout::eUndefined, vk::ImageLayout::eTransferDstOptimal);
    copyBufferToImage(mStagingBuffer, mTextureImage, static_cast<uint32_t>(texWidth), static_cast<uint32_t>(texHeight));
    transition_texture_image_layout(mTextureImage, vk::ImageLayout::eTransferDstOptimal, vk::ImageLayout::eShaderReadOnlyOptimal);
}

void VulkanBackend::create_texture_image_view() {
    vk::ImageViewCreateInfo viewInfo{.image = mTextureImage,
                                     .viewType = vk::ImageViewType::e2D,
                                     .format = vk::Format::eR8G8B8A8Srgb,
                                     .subresourceRange = {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1}};
    mTextureImageView = vk::raii::ImageView(mDevice.logicalDevice(), viewInfo);
}

void VulkanBackend::create_texture_sampler() {
    vk::PhysicalDeviceProperties properties = mDevice.physicalDevice().getProperties();
    vk::SamplerCreateInfo samplerInfo{.magFilter = vk::Filter::eLinear,
                                      .minFilter = vk::Filter::eLinear,
                                      .mipmapMode = vk::SamplerMipmapMode::eLinear,
                                      .addressModeU = vk::SamplerAddressMode::eRepeat,
                                      .addressModeV = vk::SamplerAddressMode::eRepeat,
                                      .addressModeW = vk::SamplerAddressMode::eRepeat,
                                      .anisotropyEnable = vk::True,
                                      .maxAnisotropy = properties.limits.maxSamplerAnisotropy,
                                      .compareEnable = vk::False,
                                      .compareOp = vk::CompareOp::eAlways,
                                      .borderColor = vk::BorderColor::eIntOpaqueBlack,
                                      .unnormalizedCoordinates = vk::False};
    mTextureSampler = vk::raii::Sampler(mDevice.logicalDevice(), samplerInfo);
}

std::pair<vk::raii::Image, vk::raii::DeviceMemory> VulkanBackend::create_image(uint32_t width, uint32_t height, vk::Format format,
                                                                               vk::ImageTiling tiling, vk::ImageUsageFlags usage,
                                                                               vk::MemoryPropertyFlags properties) {
    vk::ImageCreateInfo imageInfo{.imageType = vk::ImageType::e2D,
                                  .format = format,
                                  .extent = {width, height, 1},
                                  .mipLevels = 1,
                                  .arrayLayers = 1,
                                  .samples = vk::SampleCountFlagBits::e1,
                                  .tiling = tiling,
                                  .usage = usage,
                                  .sharingMode = vk::SharingMode::eExclusive,
                                  .initialLayout = vk::ImageLayout::eUndefined};
    vk::raii::Image image(mDevice.logicalDevice(), imageInfo);

    vk::MemoryRequirements memRequirements = image.getMemoryRequirements();
    vk::MemoryAllocateInfo allocInfo{.allocationSize = memRequirements.size,
                                     .memoryTypeIndex = findMemoryType(memRequirements.memoryTypeBits, properties)};
    vk::raii::DeviceMemory memory(mDevice.logicalDevice(), allocInfo);
    image.bindMemory(memory, 0);

    return std::make_pair(std::move(image), std::move(memory));
}

void VulkanBackend::transition_texture_image_layout(const vk::raii::Image& image, vk::ImageLayout oldLayout, vk::ImageLayout newLayout) {
    vk::CommandBufferAllocateInfo allocInfo{
        .commandPool = mCommandPool, .level = vk::CommandBufferLevel::ePrimary, .commandBufferCount = 1};
    vk::raii::CommandBuffer commandBuffer = std::move(mDevice.logicalDevice().allocateCommandBuffers(allocInfo).front());

    commandBuffer.begin(vk::CommandBufferBeginInfo{.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit});

    vk::ImageMemoryBarrier2 barrier{.srcStageMask = vk::PipelineStageFlagBits2::eTopOfPipe,
                                    .srcAccessMask = {},
                                    .dstStageMask = vk::PipelineStageFlagBits2::eTransfer,
                                    .dstAccessMask = vk::AccessFlagBits2::eTransferWrite,
                                    .oldLayout = oldLayout,
                                    .newLayout = newLayout,
                                    .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                                    .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                                    .image = image,
                                    .subresourceRange = {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1}};
    if (newLayout == vk::ImageLayout::eShaderReadOnlyOptimal) {
        barrier.srcStageMask = vk::PipelineStageFlagBits2::eTransfer;
        barrier.srcAccessMask = vk::AccessFlagBits2::eTransferWrite;
        barrier.dstStageMask = vk::PipelineStageFlagBits2::eFragmentShader;
        barrier.dstAccessMask = vk::AccessFlagBits2::eShaderRead;
    }
    vk::DependencyInfo dependencyInfo{.imageMemoryBarrierCount = 1, .pImageMemoryBarriers = &barrier};
    commandBuffer.pipelineBarrier2(dependencyInfo);

    commandBuffer.end();

    vk::raii::Queue graphicsQueue = mDevice.graphicsQueue();
    graphicsQueue.submit({vk::SubmitInfo{.commandBufferCount = 1, .pCommandBuffers = &*commandBuffer}}, nullptr);
    graphicsQueue.waitIdle();
}

void VulkanBackend::create_uniform_buffers() {
    vk::DeviceSize bufferSize = sizeof(UniformBufferObject);
    for (uint32_t i = 0; i < kMaxFramesInFlight; ++i) {
        auto [buffer, memory] = create_buffer(bufferSize, vk::BufferUsageFlagBits::eUniformBuffer,
                                              vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);
        mUniformBuffersMapped.push_back(memory.mapMemory(0, bufferSize));
        mUniformBuffers.push_back(std::move(buffer));
        mUniformBuffersMemory.push_back(std::move(memory));
    }
}

void VulkanBackend::create_joint_buffers() {
    // One persistently-mapped storage buffer per frame in flight, holding the
    // joint matrices of every instance back-to-back. Same idiom as the uniform
    // buffers: mapped once, memcpy'd per frame, host-coherent so no flush.
    // Deliberately not using copyBuffer() here -- that helper does a blocking
    // one-shot submit and is init-only.
    mJointsPerInstance = static_cast<uint32_t>(std::max<size_t>(mModel.skeleton.jointCount(), 1));
    mJointBufferSize = sizeof(glm::mat4) * static_cast<vk::DeviceSize>(mJointsPerInstance) * kMaxSkinnedInstances;

    for (uint32_t i = 0; i < kMaxFramesInFlight; ++i) {
        auto [buffer, memory] = create_buffer(mJointBufferSize, vk::BufferUsageFlagBits::eStorageBuffer,
                                              vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);
        mJointBuffersMapped.push_back(memory.mapMemory(0, mJointBufferSize));
        mJointBuffers.push_back(std::move(buffer));
        mJointBuffersMemory.push_back(std::move(memory));
    }

    // Start from identity so an unposed frame renders the bind pose rather
    // than collapsing the mesh to the origin.
    for (uint32_t i = 0; i < kMaxFramesInFlight; ++i) {
        std::vector<glm::mat4> identity(static_cast<size_t>(mJointsPerInstance) * kMaxSkinnedInstances, glm::mat4(1.0f));
        memcpy(mJointBuffersMapped[i], identity.data(), static_cast<size_t>(mJointBufferSize));
    }
}

void VulkanBackend::update_joint_buffer(uint32_t frameIndex) {
    if (mJointBuffersMapped.empty() || mJointMatrices.empty()) {
        return;
    }
    const size_t bytes = std::min<size_t>(mJointMatrices.size() * sizeof(glm::mat4), static_cast<size_t>(mJointBufferSize));
    memcpy(mJointBuffersMapped[frameIndex], mJointMatrices.data(), bytes);
}

void VulkanBackend::create_descriptor_pool() {
    std::array<vk::DescriptorPoolSize, 3> poolSizes{
        vk::DescriptorPoolSize{.type = vk::DescriptorType::eUniformBuffer, .descriptorCount = kMaxFramesInFlight},
        vk::DescriptorPoolSize{.type = vk::DescriptorType::eCombinedImageSampler, .descriptorCount = kMaxFramesInFlight},
        vk::DescriptorPoolSize{.type = vk::DescriptorType::eStorageBuffer, .descriptorCount = kMaxFramesInFlight}};
    vk::DescriptorPoolCreateInfo poolInfo{
        .maxSets = kMaxFramesInFlight, .poolSizeCount = static_cast<uint32_t>(poolSizes.size()), .pPoolSizes = poolSizes.data()};
    mDescriptorPool = vk::raii::DescriptorPool(mDevice.logicalDevice(), poolInfo);
}

void VulkanBackend::create_descriptor_sets() {
    std::vector<vk::DescriptorSetLayout> layouts(kMaxFramesInFlight, *mDescriptorSetLayout);
    vk::DescriptorSetAllocateInfo allocInfo{
        .descriptorPool = mDescriptorPool, .descriptorSetCount = kMaxFramesInFlight, .pSetLayouts = layouts.data()};
    mDescriptorSets = mDevice.logicalDevice().allocateDescriptorSets(allocInfo);

    for (uint32_t i = 0; i < kMaxFramesInFlight; ++i) {
        vk::DescriptorBufferInfo bufferInfo{.buffer = mUniformBuffers[i], .offset = 0, .range = sizeof(UniformBufferObject)};
        vk::DescriptorImageInfo imageInfo{
            .sampler = mTextureSampler, .imageView = mTextureImageView, .imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal};
        vk::DescriptorBufferInfo jointInfo{.buffer = mJointBuffers[i], .offset = 0, .range = mJointBufferSize};
        std::array<vk::WriteDescriptorSet, 3> writes{vk::WriteDescriptorSet{.dstSet = mDescriptorSets[i],
                                                                            .dstBinding = 0,
                                                                            .descriptorCount = 1,
                                                                            .descriptorType = vk::DescriptorType::eUniformBuffer,
                                                                            .pBufferInfo = &bufferInfo},
                                                     vk::WriteDescriptorSet{.dstSet = mDescriptorSets[i],
                                                                            .dstBinding = 1,
                                                                            .descriptorCount = 1,
                                                                            .descriptorType = vk::DescriptorType::eCombinedImageSampler,
                                                                            .pImageInfo = &imageInfo},
                                                     vk::WriteDescriptorSet{.dstSet = mDescriptorSets[i],
                                                                            .dstBinding = 2,
                                                                            .descriptorCount = 1,
                                                                            .descriptorType = vk::DescriptorType::eStorageBuffer,
                                                                            .pBufferInfo = &jointInfo}};
        mDevice.logicalDevice().updateDescriptorSets(writes, nullptr);
    }
}

void VulkanBackend::update_uniform_buffer(uint32_t frameIndex) {
    UniformBufferObject ubo{};
    ubo.view = glm::lookAt(glm::vec3(mCameraPosition[0], mCameraPosition[1], mCameraPosition[2]), glm::vec3(0.0f, 1.0f, 0.0f),
                           glm::vec3(0.0f, 1.0f, 0.0f));
    ubo.proj = glm::perspective(
        glm::radians(45.0f), static_cast<float>(mSwapChain.extent().width) / static_cast<float>(mSwapChain.extent().height), 0.1f, 10.0f);
    ubo.proj[1][1] *= -1;

    memcpy(mUniformBuffersMapped[frameIndex], &ubo, sizeof(ubo));
}

std::pair<vk::raii::Buffer, vk::raii::DeviceMemory> VulkanBackend::create_buffer(vk::DeviceSize size, vk::BufferUsageFlags usage,
                                                                                 vk::MemoryPropertyFlags properties) {
    vk::BufferCreateInfo bufferInfo{.size = size, .usage = usage, .sharingMode = vk::SharingMode::eExclusive};
    vk::raii::Buffer buffer(mDevice.logicalDevice(), bufferInfo);

    vk::MemoryRequirements memRequirements = buffer.getMemoryRequirements();
    vk::MemoryAllocateInfo allocInfo{.allocationSize = memRequirements.size,
                                     .memoryTypeIndex = findMemoryType(memRequirements.memoryTypeBits, properties)};
    vk::raii::DeviceMemory memory(mDevice.logicalDevice(), allocInfo);
    buffer.bindMemory(memory, 0);

    return std::make_pair(std::move(buffer), std::move(memory));
}

void VulkanBackend::create_command_pool_and_buffers() {
    vk::CommandPoolCreateInfo poolInfo{.flags = vk::CommandPoolCreateFlagBits::eResetCommandBuffer,
                                       .queueFamilyIndex = mDevice.graphicsQueueFamilyIndex()};
    mCommandPool = vk::raii::CommandPool(mDevice.logicalDevice(), poolInfo);

    vk::CommandBufferAllocateInfo allocInfo{
        .commandPool = mCommandPool, .level = vk::CommandBufferLevel::ePrimary, .commandBufferCount = kMaxFramesInFlight};
    vk::raii::CommandBuffers buffers(mDevice.logicalDevice(), allocInfo);
    mCommandBuffers = std::vector<vk::raii::CommandBuffer>(std::move(buffers));
}

void VulkanBackend::create_sync_objects() {
    for (size_t i = 0; i < mSwapChain.images().size(); ++i) {
        mRenderFinishedSemaphores.emplace_back(mDevice.logicalDevice(), vk::SemaphoreCreateInfo{});
    }
    for (uint32_t i = 0; i < kMaxFramesInFlight; ++i) {
        mPresentCompleteSemaphores.emplace_back(mDevice.logicalDevice(), vk::SemaphoreCreateInfo{});
        mInFlightFences.emplace_back(mDevice.logicalDevice(), vk::FenceCreateInfo{.flags = vk::FenceCreateFlagBits::eSignaled});
    }
}

void VulkanBackend::recreate_swap_chain() {
    mDevice.logicalDevice().waitIdle();
    mSwapChain = SwapChain{};
    mSwapChain.initialize(mDevice.physicalDevice(), mDevice.logicalDevice(), *mSurface);
    mRenderFinishedSemaphores.clear();
    for (size_t i = 0; i < mSwapChain.images().size(); ++i) {
        mRenderFinishedSemaphores.emplace_back(mDevice.logicalDevice(), vk::SemaphoreCreateInfo{});
    }
}

void VulkanBackend::transition_image_layout(uint32_t frameIndex, uint32_t imageIndex, vk::ImageLayout oldLayout, vk::ImageLayout newLayout,
                                            vk::AccessFlags2 srcAccess, vk::AccessFlags2 dstAccess, vk::PipelineStageFlags2 srcStage,
                                            vk::PipelineStageFlags2 dstStage) {
    vk::ImageMemoryBarrier2 barrier{.srcStageMask = srcStage,
                                    .srcAccessMask = srcAccess,
                                    .dstStageMask = dstStage,
                                    .dstAccessMask = dstAccess,
                                    .oldLayout = oldLayout,
                                    .newLayout = newLayout,
                                    .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                                    .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                                    .image = mSwapChain.images()[imageIndex],
                                    .subresourceRange = {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1}};
    vk::DependencyInfo dependencyInfo{.imageMemoryBarrierCount = 1, .pImageMemoryBarriers = &barrier};
    mCommandBuffers[frameIndex].pipelineBarrier2(dependencyInfo);
}

void VulkanBackend::transition_depth_image_layout(uint32_t frameIndex) {
    vk::ImageMemoryBarrier2 barrier{
        .srcStageMask = vk::PipelineStageFlagBits2::eEarlyFragmentTests | vk::PipelineStageFlagBits2::eLateFragmentTests,
        .srcAccessMask = {},
        .dstStageMask = vk::PipelineStageFlagBits2::eEarlyFragmentTests | vk::PipelineStageFlagBits2::eLateFragmentTests,
        .dstAccessMask = vk::AccessFlagBits2::eDepthStencilAttachmentWrite,
        .oldLayout = vk::ImageLayout::eUndefined,
        .newLayout = vk::ImageLayout::eDepthAttachmentOptimal,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = mSwapChain.depthImage(),
        .subresourceRange = {vk::ImageAspectFlagBits::eDepth, 0, 1, 0, 1}};
    vk::DependencyInfo dependencyInfo{.imageMemoryBarrierCount = 1, .pImageMemoryBarriers = &barrier};
    mCommandBuffers[frameIndex].pipelineBarrier2(dependencyInfo);
}

void VulkanBackend::record_command_buffer(uint32_t imageIndex, uint32_t frameIndex) {
    auto& cmd = mCommandBuffers[frameIndex];
    cmd.begin(vk::CommandBufferBeginInfo{});

    transition_image_layout(frameIndex, imageIndex, vk::ImageLayout::eUndefined, vk::ImageLayout::eColorAttachmentOptimal, {},
                            vk::AccessFlagBits2::eColorAttachmentWrite, vk::PipelineStageFlagBits2::eColorAttachmentOutput,
                            vk::PipelineStageFlagBits2::eColorAttachmentOutput);
    transition_depth_image_layout(frameIndex);

    vk::ClearValue clearColor = vk::ClearColorValue(0.0f, 0.0f, 0.0f, 1.0f);
    vk::RenderingAttachmentInfo attachmentInfo{.imageView = mSwapChain.imageViews()[imageIndex],
                                               .imageLayout = vk::ImageLayout::eColorAttachmentOptimal,
                                               .loadOp = vk::AttachmentLoadOp::eClear,
                                               .storeOp = vk::AttachmentStoreOp::eStore,
                                               .clearValue = clearColor};
    vk::ClearValue clearDepth;
    clearDepth.depthStencil = vk::ClearDepthStencilValue{1.0f, 0};
    vk::RenderingAttachmentInfo depthAttachmentInfo{.imageView = mSwapChain.depthImageView(),
                                                    .imageLayout = vk::ImageLayout::eDepthAttachmentOptimal,
                                                    .loadOp = vk::AttachmentLoadOp::eClear,
                                                    .storeOp = vk::AttachmentStoreOp::eDontCare,
                                                    .clearValue = clearDepth};
    vk::RenderingInfo renderingInfo{.renderArea = {.offset = {0, 0}, .extent = mSwapChain.extent()},
                                    .layerCount = 1,
                                    .colorAttachmentCount = 1,
                                    .pColorAttachments = &attachmentInfo,
                                    .pDepthAttachment = &depthAttachmentInfo};

    cmd.beginRendering(renderingInfo);
    cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, mGraphicsPipeline);
    cmd.setViewport(0, vk::Viewport(0.0f, 0.0f, static_cast<float>(mSwapChain.extent().width),
                                    static_cast<float>(mSwapChain.extent().height), 0.0f, 1.0f));
    cmd.setScissor(0, vk::Rect2D(vk::Offset2D(0, 0), mSwapChain.extent()));
    cmd.bindVertexBuffers(0, {*mVertexBuffer}, {vk::DeviceSize(0)});
    cmd.bindIndexBuffer(mIndexBuffer, 0, vk::IndexType::eUint32);
    cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, mPipelineLayout, 0, *mDescriptorSets[frameIndex], nullptr);

    // Object transforms are sampled in update(), not derived here: animation
    // state must be settled before recording so it can also feed the mapped
    // joint-matrix buffer without a one-frame skew.
    for (size_t i = 0; i < mObjects.size(); ++i) {
        PushConstants push{};
        push.model = mObjects[i].transform.toMatrix();
        push.jointOffset = static_cast<uint32_t>(i) * mJointsPerInstance;
        cmd.pushConstants<PushConstants>(mPipelineLayout, vk::ShaderStageFlagBits::eVertex, 0, push);
        cmd.drawIndexed(static_cast<uint32_t>(mModel.mesh.indices.size()), 1, 0, 0, 0);
    }
    cmd.endRendering();

    transition_image_layout(frameIndex, imageIndex, vk::ImageLayout::eColorAttachmentOptimal, vk::ImageLayout::ePresentSrcKHR,
                            vk::AccessFlagBits2::eColorAttachmentWrite, {}, vk::PipelineStageFlagBits2::eColorAttachmentOutput,
                            vk::PipelineStageFlagBits2::eBottomOfPipe);
    cmd.end();
}

void VulkanBackend::render_frame() {
    if (mFramebufferResized) {
        mFramebufferResized = false;
        recreate_swap_chain();
        return;
    }

    auto fenceResult = mDevice.logicalDevice().waitForFences(*mInFlightFences[mFrameIndex], vk::True, UINT64_MAX);
    if (fenceResult != vk::Result::eSuccess) {
        throwWithLocation("failed to wait for fence!");
    }

    auto [acquireResult, imageIndex] = mSwapChain.handle().acquireNextImage(UINT64_MAX, *mPresentCompleteSemaphores[mFrameIndex], nullptr);
    if (acquireResult == vk::Result::eErrorOutOfDateKHR) {
        recreate_swap_chain();
        return;
    }

    mDevice.logicalDevice().resetFences(*mInFlightFences[mFrameIndex]);
    mCommandBuffers[mFrameIndex].reset();
    update_uniform_buffer(mFrameIndex);
    update_joint_buffer(mFrameIndex);
    record_command_buffer(imageIndex, mFrameIndex);

    vk::PipelineStageFlags waitDestinationStageMask(vk::PipelineStageFlagBits::eColorAttachmentOutput);
    vk::SubmitInfo submitInfo{.waitSemaphoreCount = 1,
                              .pWaitSemaphores = &*mPresentCompleteSemaphores[mFrameIndex],
                              .pWaitDstStageMask = &waitDestinationStageMask,
                              .commandBufferCount = 1,
                              .pCommandBuffers = &*mCommandBuffers[mFrameIndex],
                              .signalSemaphoreCount = 1,
                              .pSignalSemaphores = &*mRenderFinishedSemaphores[imageIndex]};
    mDevice.graphicsQueue().submit(submitInfo, *mInFlightFences[mFrameIndex]);

    vk::PresentInfoKHR presentInfo{.waitSemaphoreCount = 1,
                                   .pWaitSemaphores = &*mRenderFinishedSemaphores[imageIndex],
                                   .swapchainCount = 1,
                                   .pSwapchains = &*mSwapChain.handle(),
                                   .pImageIndices = &imageIndex};
    auto presentResult = mDevice.graphicsQueue().presentKHR(presentInfo);
    if (presentResult == vk::Result::eSuboptimalKHR || presentResult == vk::Result::eErrorOutOfDateKHR) {
        recreate_swap_chain();
    }

    mFrameIndex = (mFrameIndex + 1) % kMaxFramesInFlight;
}

}  // namespace Renderer
