#include "VulkanBackend.hpp"

#include <GLFW/glfw3.h>

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#define TINYOBJLOADER_IMPLEMENTATION
#include <tiny_obj_loader.h>

#include <algorithm>
#include <cstring>
#include <stdexcept>
#include <string>
#include <unordered_map>

namespace Renderer {

struct UniformBufferObject {
    glm::mat4 model;
    glm::mat4 view;
    glm::mat4 proj;
};

static void framebufferResizeCallback(GLFWwindow* window, int, int) {
    auto* backend =
        static_cast<VulkanBackend*>(glfwGetWindowUserPointer(window));
    backend->notifyFramebufferResized();
}

VulkanBackend::VulkanBackend(std::shared_ptr<Surface> surface)
    : mSurface(std::move(surface)) {
    mStartTime = std::chrono::steady_clock::now();
    mSurface->createWindow(800, 600, "Vibe Game");
    glfwSetWindowUserPointer(mSurface->getWindow(), this);
    glfwSetFramebufferSizeCallback(mSurface->getWindow(),
                                   framebufferResizeCallback);
    create_instance();
    mSurface->createSurface(mInstance);
    mDevice.initialize(mInstance, mSurface->getSurface());
    mSwapChain.initialize(mDevice.physicalDevice(), mDevice.logicalDevice(),
                          *mSurface);
    create_command_pool_and_buffers();
    create_descriptor_set_layout();
    create_graphics_pipeline();
    load_model();
    create_vertex_buffer();
    create_index_buffer();
    create_texture_image();
    create_texture_image_view();
    create_texture_sampler();
    create_uniform_buffers();
    create_descriptor_pool();
    create_descriptor_sets();
    create_sync_objects();
}

VulkanBackend::~VulkanBackend() {
    mDevice.logicalDevice().waitIdle();
}

void VulkanBackend::notifyFramebufferResized() {
    mFramebufferResized = true;
}

void VulkanBackend::setCameraPosition(float x, float y, float z) {
    mCameraPosition[0] = x;
    mCameraPosition[1] = y;
    mCameraPosition[2] = z;
}

void VulkanBackend::create_instance() {
    constexpr vk::ApplicationInfo appInfo{
        .pApplicationName = "Hello Triangle",
        .applicationVersion = VK_MAKE_VERSION(1, 0, 0),
        .pEngineName = "No Engine",
        .engineVersion = VK_MAKE_VERSION(1, 0, 0),
        .apiVersion = vk::ApiVersion14};

    uint32_t glfwExtensionCount = 0;
    auto glfwExtensions =
        glfwGetRequiredInstanceExtensions(&glfwExtensionCount);
    if (!glfwExtensions) {
        const char* description = nullptr;
        glfwGetError(&description);
        throw std::runtime_error(
            "glfwGetRequiredInstanceExtensions failed: " +
            std::string(description ? description : "unknown error"));
    }

    auto extensionProperties = mContext.enumerateInstanceExtensionProperties();
    for (uint32_t i = 0; i < glfwExtensionCount; ++i) {
        if (std::ranges::none_of(
                extensionProperties, [glfwExtension = glfwExtensions[i]](
                                         auto const& extensionProperty) {
                    return strcmp(extensionProperty.extensionName,
                                  glfwExtension) == 0;
                })) {
            throw std::runtime_error("Required GLFW extension not supported: " +
                                     std::string(glfwExtensions[i]));
        }
    }

    vk::InstanceCreateInfo createInfo{
        .pApplicationInfo = &appInfo,
        .enabledExtensionCount = glfwExtensionCount,
        .ppEnabledExtensionNames = glfwExtensions};

    mInstance = vk::raii::Instance(mContext, createInfo);
}

void VulkanBackend::create_descriptor_set_layout() {
    vk::DescriptorSetLayoutBinding uboBinding{
        .binding = 0,
        .descriptorType = vk::DescriptorType::eUniformBuffer,
        .descriptorCount = 1,
        .stageFlags = vk::ShaderStageFlagBits::eVertex};
    vk::DescriptorSetLayoutBinding samplerBinding{
        .binding = 1,
        .descriptorType = vk::DescriptorType::eCombinedImageSampler,
        .descriptorCount = 1,
        .stageFlags = vk::ShaderStageFlagBits::eFragment};
    std::array<vk::DescriptorSetLayoutBinding, 2> bindings{uboBinding,
                                                           samplerBinding};
    vk::DescriptorSetLayoutCreateInfo layoutInfo{
        .bindingCount = static_cast<uint32_t>(bindings.size()),
        .pBindings = bindings.data()};
    mDescriptorSetLayout =
        vk::raii::DescriptorSetLayout(mDevice.logicalDevice(), layoutInfo);
}

void VulkanBackend::create_graphics_pipeline() {
    mShaders.emplace_back(std::string(SHADER_DIR) + "/box.spv",
                          mDevice.logicalDevice());

    auto bindingDescription = Vertex::getBindingDescription();
    auto attributeDescriptions = Vertex::getAttributeDescriptions();
    vk::PipelineVertexInputStateCreateInfo vertexInputInfo{
        .vertexBindingDescriptionCount = 1,
        .pVertexBindingDescriptions = &bindingDescription,
        .vertexAttributeDescriptionCount =
            static_cast<uint32_t>(attributeDescriptions.size()),
        .pVertexAttributeDescriptions = attributeDescriptions.data()};

    vk::PipelineInputAssemblyStateCreateInfo inputAssembly{
        .topology = vk::PrimitiveTopology::eTriangleList};

    std::vector<vk::DynamicState> dynamicStates = {vk::DynamicState::eViewport,
                                                   vk::DynamicState::eScissor};
    vk::PipelineDynamicStateCreateInfo dynamicState{
        .dynamicStateCount = static_cast<uint32_t>(dynamicStates.size()),
        .pDynamicStates = dynamicStates.data()};

    vk::PipelineViewportStateCreateInfo viewportState{.viewportCount = 1,
                                                      .scissorCount = 1};

    vk::PipelineRasterizationStateCreateInfo rasterizer{
        .depthClampEnable = vk::False,
        .rasterizerDiscardEnable = vk::False,
        .polygonMode = vk::PolygonMode::eFill,
        .cullMode = vk::CullModeFlagBits::eBack,
        .frontFace = vk::FrontFace::eClockwise,
        .depthBiasEnable = vk::False,
        .lineWidth = 1.0f};

    vk::PipelineMultisampleStateCreateInfo multisampling{
        .rasterizationSamples = vk::SampleCountFlagBits::e1,
        .sampleShadingEnable = vk::False};

    vk::PipelineColorBlendAttachmentState colorBlendAttachment{
        .blendEnable = vk::False,
        .colorWriteMask =
            vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG |
            vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA};
    vk::PipelineColorBlendStateCreateInfo colorBlending{
        .logicOpEnable = vk::False,
        .logicOp = vk::LogicOp::eCopy,
        .attachmentCount = 1,
        .pAttachments = &colorBlendAttachment};

    vk::PipelineDepthStencilStateCreateInfo depthStencil{
        .depthTestEnable = vk::True,
        .depthWriteEnable = vk::True,
        .depthCompareOp = vk::CompareOp::eLess,
        .depthBoundsTestEnable = vk::False,
        .stencilTestEnable = vk::False};

    vk::PipelineLayoutCreateInfo pipelineLayoutInfo{
        .setLayoutCount = 1,
        .pSetLayouts = &*mDescriptorSetLayout,
        .pushConstantRangeCount = 0};
    mPipelineLayout =
        vk::raii::PipelineLayout(mDevice.logicalDevice(), pipelineLayoutInfo);

    vk::Format colorFormat = mSwapChain.surfaceFormat().format;
    vk::StructureChain<vk::GraphicsPipelineCreateInfo,
                       vk::PipelineRenderingCreateInfo>
        pipelineCreateInfoChain = {{.stageCount = Shader::stageCount(),
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
                                   {.colorAttachmentCount = 1,
                                    .pColorAttachmentFormats = &colorFormat,
                                    .depthAttachmentFormat =
                                        mSwapChain.depthFormat()}};

    mGraphicsPipeline = vk::raii::Pipeline(
        mDevice.logicalDevice(), nullptr,
        pipelineCreateInfoChain.get<vk::GraphicsPipelineCreateInfo>());
}

void VulkanBackend::load_model() {
    tinyobj::attrib_t attrib;
    std::vector<tinyobj::shape_t> shapes;
    std::vector<tinyobj::material_t> materials;
    std::string warn, err;

    std::string modelPath = std::string(MODEL_DIR) + "/cube.obj";
    if (!tinyobj::LoadObj(&attrib, &shapes, &materials, &warn, &err,
                         modelPath.c_str())) {
        throw std::runtime_error("failed to load model: " + warn + err);
    }

    std::unordered_map<Vertex, uint32_t, VertexHash> uniqueVertices;

    for (const auto& shape : shapes) {
        for (const auto& index : shape.mesh.indices) {
            Vertex vertex{};
            vertex.pos[0] = attrib.vertices[3 * index.vertex_index + 0];
            vertex.pos[1] = attrib.vertices[3 * index.vertex_index + 1];
            vertex.pos[2] = attrib.vertices[3 * index.vertex_index + 2];

            vertex.texCoord[0] = attrib.texcoords[2 * index.texcoord_index + 0];
            vertex.texCoord[1] =
                1.0f - attrib.texcoords[2 * index.texcoord_index + 1];

            auto it = uniqueVertices.find(vertex);
            if (it == uniqueVertices.end()) {
                uint32_t newIndex = static_cast<uint32_t>(mVertices.size());
                uniqueVertices[vertex] = newIndex;
                mVertices.push_back(vertex);
                mIndices.push_back(newIndex);
            } else {
                mIndices.push_back(it->second);
            }
        }
    }
}

uint32_t VulkanBackend::findMemoryType(uint32_t typeFilter,
                                       vk::MemoryPropertyFlags properties) {
    vk::PhysicalDeviceMemoryProperties memProperties =
        mDevice.physicalDevice().getMemoryProperties();
    for (uint32_t i = 0; i < memProperties.memoryTypeCount; ++i) {
        if ((typeFilter & (1u << i)) &&
            (memProperties.memoryTypes[i].propertyFlags & properties) ==
                properties) {
            return i;
        }
    }
    throw std::runtime_error("failed to find suitable memory type!");
}

void VulkanBackend::copyBuffer(vk::raii::Buffer const& srcBuffer,
                               vk::raii::Buffer const& dstBuffer,
                               vk::DeviceSize size) {
    vk::CommandBufferAllocateInfo allocInfo{
        .commandPool = mCommandPool,
        .level = vk::CommandBufferLevel::ePrimary,
        .commandBufferCount = 1};
    vk::raii::CommandBuffer commandBuffer = std::move(
        mDevice.logicalDevice().allocateCommandBuffers(allocInfo).front());

    commandBuffer.begin(vk::CommandBufferBeginInfo{
        .flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit});
    commandBuffer.copyBuffer(srcBuffer, dstBuffer,
                             {vk::BufferCopy{0, 0, size}});
    commandBuffer.end();

    vk::raii::Queue graphicsQueue = mDevice.graphicsQueue();
    graphicsQueue.submit({vk::SubmitInfo{.commandBufferCount = 1,
                                         .pCommandBuffers = &*commandBuffer}},
                         nullptr);
    graphicsQueue.waitIdle();
}

void VulkanBackend::copyBufferToImage(const vk::raii::Buffer& buffer,
                                      const vk::raii::Image& image,
                                      uint32_t width, uint32_t height) {
    vk::CommandBufferAllocateInfo allocInfo{
        .commandPool = mCommandPool,
        .level = vk::CommandBufferLevel::ePrimary,
        .commandBufferCount = 1};
    vk::raii::CommandBuffer commandBuffer = std::move(
        mDevice.logicalDevice().allocateCommandBuffers(allocInfo).front());

    commandBuffer.begin(vk::CommandBufferBeginInfo{
        .flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit});

    vk::BufferImageCopy region{
        .bufferOffset = 0,
        .bufferRowLength = 0,
        .bufferImageHeight = 0,
        .imageSubresource = {vk::ImageAspectFlagBits::eColor, 0, 0, 1},
        .imageOffset = {0, 0, 0},
        .imageExtent = {width, height, 1}};
    commandBuffer.copyBufferToImage(buffer, image,
                                    vk::ImageLayout::eTransferDstOptimal,
                                    {region});

    commandBuffer.end();

    vk::raii::Queue graphicsQueue = mDevice.graphicsQueue();
    graphicsQueue.submit({vk::SubmitInfo{.commandBufferCount = 1,
                                         .pCommandBuffers = &*commandBuffer}},
                         nullptr);
    graphicsQueue.waitIdle();
}

void VulkanBackend::create_vertex_buffer() {
    vk::DeviceSize bufferSize = sizeof(Vertex) * mVertices.size();

    std::tie(mStagingBuffer, mStagingBufferMemory) =
        create_buffer(bufferSize, vk::BufferUsageFlagBits::eTransferSrc,
                      vk::MemoryPropertyFlagBits::eHostVisible |
                          vk::MemoryPropertyFlagBits::eHostCoherent);

    void* dataStaging = mStagingBufferMemory.mapMemory(0, bufferSize);
    memcpy(dataStaging, mVertices.data(), static_cast<size_t>(bufferSize));
    mStagingBufferMemory.unmapMemory();

    std::tie(mVertexBuffer, mVertexBufferMemory) =
        create_buffer(bufferSize,
                      vk::BufferUsageFlagBits::eTransferDst |
                          vk::BufferUsageFlagBits::eVertexBuffer,
                      vk::MemoryPropertyFlagBits::eDeviceLocal);
    copyBuffer(mStagingBuffer, mVertexBuffer, bufferSize);
}

void VulkanBackend::create_index_buffer() {
    vk::DeviceSize bufferSize = sizeof(uint32_t) * mIndices.size();

    std::tie(mStagingBuffer, mStagingBufferMemory) =
        create_buffer(bufferSize, vk::BufferUsageFlagBits::eTransferSrc,
                      vk::MemoryPropertyFlagBits::eHostVisible |
                          vk::MemoryPropertyFlagBits::eHostCoherent);

    void* dataStaging = mStagingBufferMemory.mapMemory(0, bufferSize);
    memcpy(dataStaging, mIndices.data(), static_cast<size_t>(bufferSize));
    mStagingBufferMemory.unmapMemory();

    std::tie(mIndexBuffer, mIndexBufferMemory) =
        create_buffer(bufferSize,
                      vk::BufferUsageFlagBits::eTransferDst |
                          vk::BufferUsageFlagBits::eIndexBuffer,
                      vk::MemoryPropertyFlagBits::eDeviceLocal);
    copyBuffer(mStagingBuffer, mIndexBuffer, bufferSize);
}

void VulkanBackend::create_texture_image() {
    int texWidth = 0, texHeight = 0, texChannels = 0;
    std::string texturePath = std::string(TEXTURE_DIR) + "/checkerboard.ppm";
    stbi_uc* pixels = stbi_load(texturePath.c_str(), &texWidth, &texHeight,
                               &texChannels, STBI_rgb_alpha);
    if (!pixels) {
        throw std::runtime_error("failed to load texture image: " +
                                 texturePath);
    }
    vk::DeviceSize imageSize =
        static_cast<vk::DeviceSize>(texWidth) * texHeight * 4;

    std::tie(mStagingBuffer, mStagingBufferMemory) =
        create_buffer(imageSize, vk::BufferUsageFlagBits::eTransferSrc,
                      vk::MemoryPropertyFlagBits::eHostVisible |
                          vk::MemoryPropertyFlagBits::eHostCoherent);

    void* data = mStagingBufferMemory.mapMemory(0, imageSize);
    memcpy(data, pixels, static_cast<size_t>(imageSize));
    mStagingBufferMemory.unmapMemory();
    stbi_image_free(pixels);

    std::tie(mTextureImage, mTextureImageMemory) = create_image(
        static_cast<uint32_t>(texWidth), static_cast<uint32_t>(texHeight),
        vk::Format::eR8G8B8A8Srgb, vk::ImageTiling::eOptimal,
        vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled,
        vk::MemoryPropertyFlagBits::eDeviceLocal);

    transition_texture_image_layout(mTextureImage, vk::ImageLayout::eUndefined,
                                    vk::ImageLayout::eTransferDstOptimal);
    copyBufferToImage(mStagingBuffer, mTextureImage,
                      static_cast<uint32_t>(texWidth),
                      static_cast<uint32_t>(texHeight));
    transition_texture_image_layout(mTextureImage,
                                    vk::ImageLayout::eTransferDstOptimal,
                                    vk::ImageLayout::eShaderReadOnlyOptimal);
}

void VulkanBackend::create_texture_image_view() {
    vk::ImageViewCreateInfo viewInfo{
        .image = mTextureImage,
        .viewType = vk::ImageViewType::e2D,
        .format = vk::Format::eR8G8B8A8Srgb,
        .subresourceRange = {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1}};
    mTextureImageView = vk::raii::ImageView(mDevice.logicalDevice(), viewInfo);
}

void VulkanBackend::create_texture_sampler() {
    vk::PhysicalDeviceProperties properties =
        mDevice.physicalDevice().getProperties();
    vk::SamplerCreateInfo samplerInfo{
        .magFilter = vk::Filter::eLinear,
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

std::pair<vk::raii::Image, vk::raii::DeviceMemory> VulkanBackend::create_image(
    uint32_t width, uint32_t height, vk::Format format,
    vk::ImageTiling tiling, vk::ImageUsageFlags usage,
    vk::MemoryPropertyFlags properties) {
    vk::ImageCreateInfo imageInfo{
        .imageType = vk::ImageType::e2D,
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
    vk::MemoryAllocateInfo allocInfo{
        .allocationSize = memRequirements.size,
        .memoryTypeIndex =
            findMemoryType(memRequirements.memoryTypeBits, properties)};
    vk::raii::DeviceMemory memory(mDevice.logicalDevice(), allocInfo);
    image.bindMemory(memory, 0);

    return std::make_pair(std::move(image), std::move(memory));
}

void VulkanBackend::transition_texture_image_layout(
    const vk::raii::Image& image, vk::ImageLayout oldLayout,
    vk::ImageLayout newLayout) {
    vk::CommandBufferAllocateInfo allocInfo{
        .commandPool = mCommandPool,
        .level = vk::CommandBufferLevel::ePrimary,
        .commandBufferCount = 1};
    vk::raii::CommandBuffer commandBuffer = std::move(
        mDevice.logicalDevice().allocateCommandBuffers(allocInfo).front());

    commandBuffer.begin(vk::CommandBufferBeginInfo{
        .flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit});

    vk::ImageMemoryBarrier2 barrier{
        .srcStageMask = vk::PipelineStageFlagBits2::eTopOfPipe,
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
    vk::DependencyInfo dependencyInfo{.imageMemoryBarrierCount = 1,
                                      .pImageMemoryBarriers = &barrier};
    commandBuffer.pipelineBarrier2(dependencyInfo);

    commandBuffer.end();

    vk::raii::Queue graphicsQueue = mDevice.graphicsQueue();
    graphicsQueue.submit({vk::SubmitInfo{.commandBufferCount = 1,
                                         .pCommandBuffers = &*commandBuffer}},
                         nullptr);
    graphicsQueue.waitIdle();
}

void VulkanBackend::create_uniform_buffers() {
    vk::DeviceSize bufferSize = sizeof(UniformBufferObject);
    for (uint32_t i = 0; i < kMaxFramesInFlight; ++i) {
        auto [buffer, memory] =
            create_buffer(bufferSize, vk::BufferUsageFlagBits::eUniformBuffer,
                          vk::MemoryPropertyFlagBits::eHostVisible |
                              vk::MemoryPropertyFlagBits::eHostCoherent);
        mUniformBuffersMapped.push_back(memory.mapMemory(0, bufferSize));
        mUniformBuffers.push_back(std::move(buffer));
        mUniformBuffersMemory.push_back(std::move(memory));
    }
}

void VulkanBackend::create_descriptor_pool() {
    std::array<vk::DescriptorPoolSize, 2> poolSizes{
        vk::DescriptorPoolSize{.type = vk::DescriptorType::eUniformBuffer,
                              .descriptorCount = kMaxFramesInFlight},
        vk::DescriptorPoolSize{.type = vk::DescriptorType::eCombinedImageSampler,
                              .descriptorCount = kMaxFramesInFlight}};
    vk::DescriptorPoolCreateInfo poolInfo{
        .maxSets = kMaxFramesInFlight,
        .poolSizeCount = static_cast<uint32_t>(poolSizes.size()),
        .pPoolSizes = poolSizes.data()};
    mDescriptorPool = vk::raii::DescriptorPool(mDevice.logicalDevice(), poolInfo);
}

void VulkanBackend::create_descriptor_sets() {
    std::vector<vk::DescriptorSetLayout> layouts(kMaxFramesInFlight,
                                                 *mDescriptorSetLayout);
    vk::DescriptorSetAllocateInfo allocInfo{
        .descriptorPool = mDescriptorPool,
        .descriptorSetCount = kMaxFramesInFlight,
        .pSetLayouts = layouts.data()};
    mDescriptorSets = mDevice.logicalDevice().allocateDescriptorSets(allocInfo);

    for (uint32_t i = 0; i < kMaxFramesInFlight; ++i) {
        vk::DescriptorBufferInfo bufferInfo{
            .buffer = mUniformBuffers[i],
            .offset = 0,
            .range = sizeof(UniformBufferObject)};
        vk::DescriptorImageInfo imageInfo{
            .sampler = mTextureSampler,
            .imageView = mTextureImageView,
            .imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal};
        std::array<vk::WriteDescriptorSet, 2> writes{
            vk::WriteDescriptorSet{.dstSet = mDescriptorSets[i],
                                  .dstBinding = 0,
                                  .descriptorCount = 1,
                                  .descriptorType =
                                      vk::DescriptorType::eUniformBuffer,
                                  .pBufferInfo = &bufferInfo},
            vk::WriteDescriptorSet{
                .dstSet = mDescriptorSets[i],
                .dstBinding = 1,
                .descriptorCount = 1,
                .descriptorType = vk::DescriptorType::eCombinedImageSampler,
                .pImageInfo = &imageInfo}};
        mDevice.logicalDevice().updateDescriptorSets(writes, nullptr);
    }
}

void VulkanBackend::update_uniform_buffer(uint32_t frameIndex) {
    float elapsed = std::chrono::duration<float>(
                        std::chrono::steady_clock::now() - mStartTime)
                        .count();

    UniformBufferObject ubo{};
    ubo.model = glm::rotate(glm::mat4(1.0f), elapsed * glm::radians(90.0f),
                            glm::vec3(0.0f, 0.0f, 1.0f));
    ubo.view = glm::lookAt(glm::vec3(mCameraPosition[0], mCameraPosition[1],
                                    mCameraPosition[2]),
                           glm::vec3(0.0f), glm::vec3(0.0f, 0.0f, 1.0f));
    ubo.proj = glm::perspective(
        glm::radians(45.0f),
        static_cast<float>(mSwapChain.extent().width) /
            static_cast<float>(mSwapChain.extent().height),
        0.1f, 10.0f);
    ubo.proj[1][1] *= -1;

    memcpy(mUniformBuffersMapped[frameIndex], &ubo, sizeof(ubo));
}

std::pair<vk::raii::Buffer, vk::raii::DeviceMemory>
VulkanBackend::create_buffer(vk::DeviceSize size, vk::BufferUsageFlags usage,
                             vk::MemoryPropertyFlags properties) {
    vk::BufferCreateInfo bufferInfo{.size = size,
                                    .usage = usage,
                                    .sharingMode = vk::SharingMode::eExclusive};
    vk::raii::Buffer buffer(mDevice.logicalDevice(), bufferInfo);

    vk::MemoryRequirements memRequirements = buffer.getMemoryRequirements();
    vk::MemoryAllocateInfo allocInfo{
        .allocationSize = memRequirements.size,
        .memoryTypeIndex =
            findMemoryType(memRequirements.memoryTypeBits, properties)};
    vk::raii::DeviceMemory memory(mDevice.logicalDevice(), allocInfo);
    buffer.bindMemory(memory, 0);

    return std::make_pair(std::move(buffer), std::move(memory));
}

void VulkanBackend::create_command_pool_and_buffers() {
    vk::CommandPoolCreateInfo poolInfo{
        .flags = vk::CommandPoolCreateFlagBits::eResetCommandBuffer,
        .queueFamilyIndex = mDevice.graphicsQueueFamilyIndex()};
    mCommandPool = vk::raii::CommandPool(mDevice.logicalDevice(), poolInfo);

    vk::CommandBufferAllocateInfo allocInfo{
        .commandPool = mCommandPool,
        .level = vk::CommandBufferLevel::ePrimary,
        .commandBufferCount = kMaxFramesInFlight};
    vk::raii::CommandBuffers buffers(mDevice.logicalDevice(), allocInfo);
    mCommandBuffers = std::vector<vk::raii::CommandBuffer>(std::move(buffers));
}

void VulkanBackend::create_sync_objects() {
    for (size_t i = 0; i < mSwapChain.images().size(); ++i) {
        mRenderFinishedSemaphores.emplace_back(mDevice.logicalDevice(),
                                               vk::SemaphoreCreateInfo{});
    }
    for (uint32_t i = 0; i < kMaxFramesInFlight; ++i) {
        mPresentCompleteSemaphores.emplace_back(mDevice.logicalDevice(),
                                                vk::SemaphoreCreateInfo{});
        mInFlightFences.emplace_back(
            mDevice.logicalDevice(),
            vk::FenceCreateInfo{.flags = vk::FenceCreateFlagBits::eSignaled});
    }
}

void VulkanBackend::recreate_swap_chain() {
    mDevice.logicalDevice().waitIdle();
    mSwapChain = SwapChain{};
    mSwapChain.initialize(mDevice.physicalDevice(), mDevice.logicalDevice(),
                          *mSurface);
    mRenderFinishedSemaphores.clear();
    for (size_t i = 0; i < mSwapChain.images().size(); ++i) {
        mRenderFinishedSemaphores.emplace_back(mDevice.logicalDevice(),
                                               vk::SemaphoreCreateInfo{});
    }
}

void VulkanBackend::transition_image_layout(
    uint32_t frameIndex, uint32_t imageIndex, vk::ImageLayout oldLayout,
    vk::ImageLayout newLayout, vk::AccessFlags2 srcAccess,
    vk::AccessFlags2 dstAccess, vk::PipelineStageFlags2 srcStage,
    vk::PipelineStageFlags2 dstStage) {
    vk::ImageMemoryBarrier2 barrier{
        .srcStageMask = srcStage,
        .srcAccessMask = srcAccess,
        .dstStageMask = dstStage,
        .dstAccessMask = dstAccess,
        .oldLayout = oldLayout,
        .newLayout = newLayout,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = mSwapChain.images()[imageIndex],
        .subresourceRange = {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1}};
    vk::DependencyInfo dependencyInfo{.imageMemoryBarrierCount = 1,
                                      .pImageMemoryBarriers = &barrier};
    mCommandBuffers[frameIndex].pipelineBarrier2(dependencyInfo);
}

void VulkanBackend::transition_depth_image_layout(uint32_t frameIndex) {
    vk::ImageMemoryBarrier2 barrier{
        .srcStageMask = vk::PipelineStageFlagBits2::eEarlyFragmentTests |
                        vk::PipelineStageFlagBits2::eLateFragmentTests,
        .srcAccessMask = {},
        .dstStageMask = vk::PipelineStageFlagBits2::eEarlyFragmentTests |
                        vk::PipelineStageFlagBits2::eLateFragmentTests,
        .dstAccessMask = vk::AccessFlagBits2::eDepthStencilAttachmentWrite,
        .oldLayout = vk::ImageLayout::eUndefined,
        .newLayout = vk::ImageLayout::eDepthAttachmentOptimal,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = mSwapChain.depthImage(),
        .subresourceRange = {vk::ImageAspectFlagBits::eDepth, 0, 1, 0, 1}};
    vk::DependencyInfo dependencyInfo{.imageMemoryBarrierCount = 1,
                                      .pImageMemoryBarriers = &barrier};
    mCommandBuffers[frameIndex].pipelineBarrier2(dependencyInfo);
}

void VulkanBackend::record_command_buffer(uint32_t imageIndex,
                                          uint32_t frameIndex) {
    auto& cmd = mCommandBuffers[frameIndex];
    cmd.begin(vk::CommandBufferBeginInfo{});

    transition_image_layout(frameIndex, imageIndex, vk::ImageLayout::eUndefined,
                            vk::ImageLayout::eColorAttachmentOptimal, {},
                            vk::AccessFlagBits2::eColorAttachmentWrite,
                            vk::PipelineStageFlagBits2::eColorAttachmentOutput,
                            vk::PipelineStageFlagBits2::eColorAttachmentOutput);
    transition_depth_image_layout(frameIndex);

    vk::ClearValue clearColor = vk::ClearColorValue(0.0f, 0.0f, 0.0f, 1.0f);
    vk::RenderingAttachmentInfo attachmentInfo{
        .imageView = mSwapChain.imageViews()[imageIndex],
        .imageLayout = vk::ImageLayout::eColorAttachmentOptimal,
        .loadOp = vk::AttachmentLoadOp::eClear,
        .storeOp = vk::AttachmentStoreOp::eStore,
        .clearValue = clearColor};
    vk::ClearValue clearDepth;
    clearDepth.depthStencil = vk::ClearDepthStencilValue{1.0f, 0};
    vk::RenderingAttachmentInfo depthAttachmentInfo{
        .imageView = mSwapChain.depthImageView(),
        .imageLayout = vk::ImageLayout::eDepthAttachmentOptimal,
        .loadOp = vk::AttachmentLoadOp::eClear,
        .storeOp = vk::AttachmentStoreOp::eDontCare,
        .clearValue = clearDepth};
    vk::RenderingInfo renderingInfo{
        .renderArea = {.offset = {0, 0}, .extent = mSwapChain.extent()},
        .layerCount = 1,
        .colorAttachmentCount = 1,
        .pColorAttachments = &attachmentInfo,
        .pDepthAttachment = &depthAttachmentInfo};

    cmd.beginRendering(renderingInfo);
    cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, mGraphicsPipeline);
    cmd.setViewport(
        0, vk::Viewport(
               0.0f, 0.0f, static_cast<float>(mSwapChain.extent().width),
               static_cast<float>(mSwapChain.extent().height), 0.0f, 1.0f));
    cmd.setScissor(0, vk::Rect2D(vk::Offset2D(0, 0), mSwapChain.extent()));
    cmd.bindVertexBuffers(0, {*mVertexBuffer}, {vk::DeviceSize(0)});
    cmd.bindIndexBuffer(mIndexBuffer, 0, vk::IndexType::eUint32);
    cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, mPipelineLayout, 0,
                           *mDescriptorSets[frameIndex], nullptr);
    cmd.drawIndexed(static_cast<uint32_t>(mIndices.size()), 1, 0, 0, 0);
    cmd.endRendering();

    transition_image_layout(frameIndex, imageIndex,
                            vk::ImageLayout::eColorAttachmentOptimal,
                            vk::ImageLayout::ePresentSrcKHR,
                            vk::AccessFlagBits2::eColorAttachmentWrite, {},
                            vk::PipelineStageFlagBits2::eColorAttachmentOutput,
                            vk::PipelineStageFlagBits2::eBottomOfPipe);
    cmd.end();
}

void VulkanBackend::render_frame() {
    if (mFramebufferResized) {
        mFramebufferResized = false;
        recreate_swap_chain();
        return;
    }

    auto fenceResult = mDevice.logicalDevice().waitForFences(
        *mInFlightFences[mFrameIndex], vk::True, UINT64_MAX);
    if (fenceResult != vk::Result::eSuccess) {
        throw std::runtime_error("failed to wait for fence!");
    }

    auto [acquireResult, imageIndex] = mSwapChain.handle().acquireNextImage(
        UINT64_MAX, *mPresentCompleteSemaphores[mFrameIndex], nullptr);
    if (acquireResult == vk::Result::eErrorOutOfDateKHR) {
        recreate_swap_chain();
        return;
    }

    mDevice.logicalDevice().resetFences(*mInFlightFences[mFrameIndex]);
    mCommandBuffers[mFrameIndex].reset();
    update_uniform_buffer(mFrameIndex);
    record_command_buffer(imageIndex, mFrameIndex);

    vk::PipelineStageFlags waitDestinationStageMask(
        vk::PipelineStageFlagBits::eColorAttachmentOutput);
    vk::SubmitInfo submitInfo{
        .waitSemaphoreCount = 1,
        .pWaitSemaphores = &*mPresentCompleteSemaphores[mFrameIndex],
        .pWaitDstStageMask = &waitDestinationStageMask,
        .commandBufferCount = 1,
        .pCommandBuffers = &*mCommandBuffers[mFrameIndex],
        .signalSemaphoreCount = 1,
        .pSignalSemaphores = &*mRenderFinishedSemaphores[imageIndex]};
    mDevice.graphicsQueue().submit(submitInfo, *mInFlightFences[mFrameIndex]);

    vk::PresentInfoKHR presentInfo{
        .waitSemaphoreCount = 1,
        .pWaitSemaphores = &*mRenderFinishedSemaphores[imageIndex],
        .swapchainCount = 1,
        .pSwapchains = &*mSwapChain.handle(),
        .pImageIndices = &imageIndex};
    auto presentResult = mDevice.graphicsQueue().presentKHR(presentInfo);
    if (presentResult == vk::Result::eSuboptimalKHR ||
        presentResult == vk::Result::eErrorOutOfDateKHR) {
        recreate_swap_chain();
    }

    mFrameIndex = (mFrameIndex + 1) % kMaxFramesInFlight;
}

}  // namespace Renderer
