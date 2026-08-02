#include "VulkanBackend.hpp"

#include <GLFW/glfw3.h>

#include <algorithm>
#include <cstring>
#include <stdexcept>
#include <string>

namespace Renderer {

static const std::vector<Vertex> kTriangleVertices = {
    {{0.0f, -0.5f}, {1.0f, 0.0f, 0.0f}},
    {{0.5f, 0.5f}, {0.0f, 1.0f, 0.0f}},
    {{-0.5f, 0.5f}, {0.0f, 0.0f, 1.0f}}};

static void framebufferResizeCallback(GLFWwindow* window, int, int) {
    auto* backend =
        static_cast<VulkanBackend*>(glfwGetWindowUserPointer(window));
    backend->notifyFramebufferResized();
}

VulkanBackend::VulkanBackend(std::shared_ptr<Surface> surface)
    : mSurface(std::move(surface)) {
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
    create_graphics_pipeline();
    create_vertex_buffer();
    create_sync_objects();
}

VulkanBackend::~VulkanBackend() {
    mDevice.logicalDevice().waitIdle();
}

void VulkanBackend::notifyFramebufferResized() {
    mFramebufferResized = true;
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

void VulkanBackend::create_graphics_pipeline() {
    mShaders.emplace_back(std::string(SHADER_DIR) + "/triangle.spv",
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

    vk::PipelineLayoutCreateInfo pipelineLayoutInfo{
        .setLayoutCount = 0, .pushConstantRangeCount = 0};
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
                                    .pColorBlendState = &colorBlending,
                                    .pDynamicState = &dynamicState,
                                    .layout = mPipelineLayout,
                                    .renderPass = nullptr},
                                   {.colorAttachmentCount = 1,
                                    .pColorAttachmentFormats = &colorFormat}};

    mGraphicsPipeline = vk::raii::Pipeline(
        mDevice.logicalDevice(), nullptr,
        pipelineCreateInfoChain.get<vk::GraphicsPipelineCreateInfo>());
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

void VulkanBackend::create_vertex_buffer() {
    vk::DeviceSize bufferSize = sizeof(Vertex) * kTriangleVertices.size();

    std::tie(mStagingBuffer, mStagingBufferMemory) =
        create_buffer(bufferSize, vk::BufferUsageFlagBits::eTransferSrc,
                      vk::MemoryPropertyFlagBits::eHostVisible |
                          vk::MemoryPropertyFlagBits::eHostCoherent);

    void* dataStaging = mStagingBufferMemory.mapMemory(0, bufferSize);
    memcpy(dataStaging, kTriangleVertices.data(),
           static_cast<size_t>(bufferSize));
    mStagingBufferMemory.unmapMemory();

    std::tie(mVertexBuffer, mVertexBufferMemory) =
        create_buffer(bufferSize,
                      vk::BufferUsageFlagBits::eTransferDst |
                          vk::BufferUsageFlagBits::eVertexBuffer,
                      vk::MemoryPropertyFlagBits::eDeviceLocal);
    copyBuffer(mStagingBuffer, mVertexBuffer, bufferSize);
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

void VulkanBackend::record_command_buffer(uint32_t imageIndex,
                                          uint32_t frameIndex) {
    auto& cmd = mCommandBuffers[frameIndex];
    cmd.begin(vk::CommandBufferBeginInfo{});

    transition_image_layout(frameIndex, imageIndex, vk::ImageLayout::eUndefined,
                            vk::ImageLayout::eColorAttachmentOptimal, {},
                            vk::AccessFlagBits2::eColorAttachmentWrite,
                            vk::PipelineStageFlagBits2::eColorAttachmentOutput,
                            vk::PipelineStageFlagBits2::eColorAttachmentOutput);

    vk::ClearValue clearColor = vk::ClearColorValue(0.0f, 0.0f, 0.0f, 1.0f);
    vk::RenderingAttachmentInfo attachmentInfo{
        .imageView = mSwapChain.imageViews()[imageIndex],
        .imageLayout = vk::ImageLayout::eColorAttachmentOptimal,
        .loadOp = vk::AttachmentLoadOp::eClear,
        .storeOp = vk::AttachmentStoreOp::eStore,
        .clearValue = clearColor};
    vk::RenderingInfo renderingInfo{
        .renderArea = {.offset = {0, 0}, .extent = mSwapChain.extent()},
        .layerCount = 1,
        .colorAttachmentCount = 1,
        .pColorAttachments = &attachmentInfo};

    cmd.beginRendering(renderingInfo);
    cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, mGraphicsPipeline);
    cmd.setViewport(
        0, vk::Viewport(
               0.0f, 0.0f, static_cast<float>(mSwapChain.extent().width),
               static_cast<float>(mSwapChain.extent().height), 0.0f, 1.0f));
    cmd.setScissor(0, vk::Rect2D(vk::Offset2D(0, 0), mSwapChain.extent()));
    cmd.bindVertexBuffers(0, {*mVertexBuffer}, {vk::DeviceSize(0)});
    cmd.draw(kTriangleVertices.size(), 1, 0, 0);
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
