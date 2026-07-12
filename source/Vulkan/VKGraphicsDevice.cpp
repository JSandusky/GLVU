#include "GraphicsDevice.h"

#include "Buffer.h"
#include "ShaderCache.h"

using namespace std;

namespace GLVU
{

static pair<TextureFormat, VkFormat> vk_textureFormatTable[] = {
    { TEX_BGRA8, VK_FORMAT_B8G8R8A8_UNORM },
    { TEX_RGB8, VK_FORMAT_R8G8B8_UNORM },
    { TEX_RGBA8, VK_FORMAT_R8G8B8A8_UNORM },
    { TEX_RGBA16F, VK_FORMAT_R16G16B16A16_SFLOAT },
    { TEX_DEPTH, VK_FORMAT_D24_UNORM_S8_UINT },
    { TEX_SHADOW16 , VK_FORMAT_D16_UNORM },
    { TEX_SHADOW32, VK_FORMAT_D32_SFLOAT },
    { TEX_RG16F, VK_FORMAT_R16G16_SFLOAT },
    { TEX_DXT1, VK_FORMAT_BC1_RGB_UNORM_BLOCK },
    { TEX_DXT3, VK_FORMAT_BC2_UNORM_BLOCK },
    { TEX_DXT5, VK_FORMAT_BC3_UNORM_BLOCK },
    { TEX_BC4, VK_FORMAT_BC4_UNORM_BLOCK },
    { TEX_BC5, VK_FORMAT_BC5_UNORM_BLOCK },
    { TEX_R32F, VK_FORMAT_R32_SFLOAT },
    { TEX_RG16U, VK_FORMAT_R16G16_UINT },
    { TEX_RGBA16U, VK_FORMAT_R16G16B16A16_UINT }
};

static VkImageType vk_imageTypes[] = {
        VK_IMAGE_TYPE_2D, // Texture2D
        VK_IMAGE_TYPE_3D, // Texture3D
        VK_IMAGE_TYPE_2D, // TextureCube
        VK_IMAGE_TYPE_2D, // Texure2DArray
        VK_IMAGE_TYPE_2D, // TextureCubeArray
        VK_IMAGE_TYPE_1D, // Texture1D,
        VK_IMAGE_TYPE_1D, // TextureBuffer
};

static VkImageViewType vk_viewTypes[] = {
    VK_IMAGE_VIEW_TYPE_2D,
    VK_IMAGE_VIEW_TYPE_3D,
    VK_IMAGE_VIEW_TYPE_CUBE,
    VK_IMAGE_VIEW_TYPE_2D_ARRAY,
    VK_IMAGE_VIEW_TYPE_CUBE_ARRAY,
    VK_IMAGE_VIEW_TYPE_1D,
    VK_IMAGE_VIEW_TYPE_1D
};

VkFormat vk_TextureFormat(TextureFormat format)
{
    for (auto& r : vk_textureFormatTable)
        if (r.first == format)
            return r.second;
    return VK_FORMAT_R8G8B8_UNORM;
}

TextureFormat vk_FormatFor(VkFormat format)
{
    for (auto& r : vk_textureFormatTable)
        if (r.second == format)
            return r.first;
    return TEX_RGB8;
}

VkResult CreateDebugUtilsMessengerEXT(VkInstance instance, const VkDebugUtilsMessengerCreateInfoEXT* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkDebugUtilsMessengerEXT* pDebugMessenger) {
    auto func = (PFN_vkCreateDebugUtilsMessengerEXT)vkGetInstanceProcAddr(instance, "vkCreateDebugUtilsMessengerEXT");
    if (func != nullptr)
        return func(instance, pCreateInfo, pAllocator, pDebugMessenger);
    else
        return VK_ERROR_EXTENSION_NOT_PRESENT;
}

//****************************************************************************
//
//  Function:   GraphicsDevice::GraphicsDevice
//
//  Purpose:    Construct, create neutral caches.
//
//****************************************************************************
GraphicsDevice::GraphicsDevice() :
    effectCache_(this)
{
    shaderCache_.reset(new ShaderCache(this));
    uboCache_.reset(new ScratchBufferCache(this));
    graphicsCommandPool_.reset(new CommandBufferPool(this, true));
    computeCommandPool_.reset(new CommandBufferPool(this, false));
}

//****************************************************************************
//
//  Function:   GraphicsDevice::~GraphicsDevice
//
//  Purpose:    Shutdown should've done everything
//
//****************************************************************************
GraphicsDevice::~GraphicsDevice()
{
}

//****************************************************************************
//
//  Function:   GraphicsDevice::OpenDevice
//
//  Purpose:    Initializes the vulkan device, creates default objects,
//              and queries for our capabilities and sizing/alignment rules.
//
//  Return:     True if healthy
//
//****************************************************************************
static GraphicsDevice* g_activeLocalDevice;
bool GraphicsDevice::OpenDevice(const char** requiredExt, uint32_t requiredExtCt)
{
    vector<const char*> instanceLayers;
    //instanceLayers.push_back("VK_LAYER_LUNARG_standard_validation");

    vector<const char*> requiredExtensions;
    for (auto i = 0; i < requiredExtCt; ++i)
        requiredExtensions.push_back(requiredExt[i]);
    requiredExtensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    requiredExtensions.push_back("VK_EXT_debug_report");


    VezApplicationInfo appInfo = { nullptr, "GLVU", VK_MAKE_VERSION(1, 0, 0), "", VK_MAKE_VERSION(0, 0, 0) };
    VezInstanceCreateInfo createInfo = { nullptr, &appInfo, (unsigned)instanceLayers.size(), instanceLayers.data(), (unsigned)requiredExtensions.size(), requiredExtensions.data() };
    auto result = vezCreateInstance(&createInfo, &instance_);
    if (result != VK_SUCCESS)
        return false;

    uint32_t physicalDeviceCount = 0;
    vezEnumeratePhysicalDevices(instance_, &physicalDeviceCount, nullptr);
    if (physicalDeviceCount == 0)
        return false;

    vector<VkPhysicalDevice> physicalDevices(physicalDeviceCount);
    vezEnumeratePhysicalDevices(instance_, &physicalDeviceCount, physicalDevices.data());

    uint32_t physIndex = 0;
    size_t largest = 0;
    for (size_t deviceIdx = 0; deviceIdx < physicalDevices.size(); ++deviceIdx)
    {
        auto& phyDevice = physicalDevices[deviceIdx];
        VkPhysicalDeviceMemoryProperties mem;
        vkGetPhysicalDeviceMemoryProperties(phyDevice, &mem);

        size_t total = 0;
        for (uint32_t i = 0; i < mem.memoryHeapCount; ++i)
            total += mem.memoryHeaps[i].size;
        if (total > largest)
        {
            largest = total;
            physIndex = deviceIdx;
        }
    }

    physicalDevice_ = physicalDevices[physIndex];

    VkPhysicalDeviceProperties props = {  };
    vezGetPhysicalDeviceProperties(physicalDevice_, &props);
    
    graphicsFeatures_.compute_ = true;
    graphicsFeatures_.geometryShader_ = true;
    graphicsFeatures_.tessellation_ = true;
    graphicsFeatures_.clipControl_ = true;
    graphicsFeatures_.transformFeedback_ = false;
    graphicsFeatures_.shaderStorageBuffer_ = true;
    graphicsFeatures_.maxUBOSize_ = props.limits.maxUniformBufferRange;
    graphicsFeatures_.minUBOAlignment_ = props.limits.minUniformBufferOffsetAlignment;

    // Get the physical device information.
    VkPhysicalDeviceProperties properties = {};
    vezGetPhysicalDeviceProperties(physicalDevice_, &properties);
    LogFormat(GLVU_INFO, "GPU: %s", properties.deviceName);

    vector<const char*> deviceExtensions = { VK_KHR_SWAPCHAIN_EXTENSION_NAME };
    VezDeviceCreateInfo deviceCreateInfo = { nullptr, 0, nullptr, static_cast<uint32_t>(deviceExtensions.size()), deviceExtensions.data() };
    result = vezCreateDevice(physicalDevice_, &deviceCreateInfo, &device_);

    vezGetDeviceGraphicsQueue(device_, 0, &graphicsQueue_);

    LogMessage("Device opened successfully", 0);

    CreateDefaultObjects();

    g_activeLocalDevice = this;
    auto debug_call = [] (
        VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
        VkDebugUtilsMessageTypeFlagsEXT messageType,
        const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
        void* pUserData) -> VkBool32 {

        g_activeLocalDevice->LogMessage(pCallbackData->pMessage, messageSeverity > VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT ? GLVU_ERROR : GLVU_WARNING);
        return VK_FALSE;
    };

    {
        VkDebugUtilsMessengerCreateInfoEXT createInfo{ };
        createInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
        createInfo.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
        createInfo.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
        createInfo.pfnUserCallback = debug_call;
        createInfo.pUserData = nullptr; // Optional

        VkDebugUtilsMessengerEXT debugMessenger;
        if (CreateDebugUtilsMessengerEXT(instance_, &createInfo, nullptr, &debugMessenger) != VK_SUCCESS) {
            throw std::runtime_error("failed to set up debug messenger!");
        }
    }

    return true;
}

//****************************************************************************
//
//  Function:   GraphicsDevice::Resize
//
//  Purpose:    Handles swap-chain buffer resizes and safety checks so that 0-by-0
//              resizing (such as minimize) doesn't crash us.
//
//****************************************************************************
void GraphicsDevice::OnResize(uint32_t width, uint32_t height)
{
    //if (width != backbufferWidth_ || height != backbufferHeight_)
    //{
    //    // it is in theory possible for the swap-chain to change formats
    //    // ie. moving from a regular screen to a 3d vision screen or some such probably
    //    VkSurfaceFormatKHR swapchainFormat = {};
    //    vezGetSwapchainSurfaceFormat(swapchain_, &swapchainFormat);
    //
    //    TextureTraits colorTraits = {};
    //    colorTraits.kind_ = Texture2D;
    //    colorTraits.format_ = vk_FormatFor(swapchainFormat.format);
    //    colorTraits.colorAttachment_ = true;
    //    colorTraits.width_ = width;
    //    colorTraits.height_ = height;
    //    colorTraits.depth_ = 1;
    //    colorTraits.mips_ = 1;
    //    colorTraits.layers_ = 1;
    //    auto colorTex = CreateTexture(colorTraits);
    //
    //    TextureTraits depthTraits = {};
    //    depthTraits.kind_ = Texture2D;
    //    depthTraits.format_ = TEX_DEPTH;
    //    depthTraits.depthAttachment_ = true;
    //    depthTraits.width_ = width;
    //    depthTraits.height_ = height;
    //    depthTraits.depth_ = 1;
    //    depthTraits.mips_ = 1;
    //    depthTraits.layers_ = 1;
    //    auto depthTex = CreateTexture(depthTraits);
    //
    //    backbuffer_ = CreateFrameBuffer({ colorTex, depthTex });
    //    backbufferWidth_ = width;
    //    backbufferHeight_ = height;
    //}
}

//****************************************************************************
//
//  Function:   GraphicsDevice::InitSurface
//
//  Purpose:    Setup swapchain and backbuffer for window.
//
//****************************************************************************
bool GraphicsDevice::InitSurface(uint32_t width, uint32_t height, VkSurfaceKHR surface)
{     
    /*backbufferWidth_ = width;
    backbufferHeight_ = height;
    surface_ = surface;

    VezSwapchainCreateInfo swapchainCreateInfo = {};
    swapchainCreateInfo.surface = surface_;
    swapchainCreateInfo.format = { VK_FORMAT_B8G8R8A8_UNORM, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR };
    swapchainCreateInfo.tripleBuffer = VK_FALSE;
    auto result = vezCreateSwapchain(device_, &swapchainCreateInfo, &swapchain_);
    if (result != VK_SUCCESS)
        throw "Failed to create swapchain";

    VkSurfaceFormatKHR swapchainFormat = {};
    vezGetSwapchainSurfaceFormat(swapchain_, &swapchainFormat);

    TextureTraits colorTraits = { };
    colorTraits.kind_ = Texture2D;
    colorTraits.format_ = vk_FormatFor(swapchainFormat.format);
    colorTraits.colorAttachment_ = true;
    colorTraits.width_ = width;
    colorTraits.height_ = height;
    colorTraits.depth_ = 1;
    colorTraits.mips_ = 1;
    colorTraits.layers_ = 1;
    auto colorTex = CreateTexture(colorTraits);

    TextureTraits depthTraits = { };
    depthTraits.kind_ = Texture2D;
    depthTraits.format_ = TEX_DEPTH;
    depthTraits.depthAttachment_ = true;
    depthTraits.width_ = width;
    depthTraits.height_ = height;
    depthTraits.depth_ = 1;
    depthTraits.mips_ = 1;
    depthTraits.layers_ = 1;
    auto depthTex = CreateTexture(depthTraits);

    backbuffer_ = CreateFrameBuffer({ colorTex, depthTex });
    if (backbuffer_ == nullptr)
        return false;

    LogMessage("Backbuffer created", 2);*/

    VkFilter filterTable[] = {
        VK_FILTER_NEAREST,
        VK_FILTER_LINEAR,
        VK_FILTER_LINEAR,
        VK_FILTER_LINEAR, // aniso
        VK_FILTER_LINEAR, // shadow
    };
    VkSamplerMipmapMode mipTable[] = {
        VK_SAMPLER_MIPMAP_MODE_NEAREST,
        VK_SAMPLER_MIPMAP_MODE_NEAREST,
        VK_SAMPLER_MIPMAP_MODE_LINEAR,
        VK_SAMPLER_MIPMAP_MODE_LINEAR,  // aniso
        VK_SAMPLER_MIPMAP_MODE_NEAREST, // shadow
    };

    // Create our sampler objects
    for (int i = 0; i < COUNT_TEXTURE_FILTER; ++i)
    {
        VezSamplerCreateInfo info = { };
        info.minFilter = filterTable[i];
        info.magFilter = filterTable[i];
        info.mipmapMode = mipTable[i];
        info.anisotropyEnable = i == FILTER_ANISOTROPIC;
        info.maxAnisotropy = 4;
        info.compareEnable = i == FILTER_SHADOW;
        info.compareOp = VK_COMPARE_OP_LESS;
        info.addressModeU = info.addressModeV = info.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        auto hr = vezCreateSampler(device_, &info, &clampSamplers_[i]);
        if (hr != VK_SUCCESS)
            LogMessage("Failed to create sampler", GLVU_ERROR);

        info.addressModeU = info.addressModeV = info.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        hr = vezCreateSampler(device_, &info, &wrapSamplers_[i]);
        if (hr != VK_SUCCESS)
            LogMessage("Failed to create sampler", GLVU_ERROR);
    }

    return true;
}

//****************************************************************************
//
//  Function:   GraphicsDevice::PlatformShutdown
//
//  Purpose:    Vulkan needs to wait for GPU, then destroy pools, samplers, and
//              device parts themselves.
//
//****************************************************************************
void GraphicsDevice::PlatformShutdown()
{
    vezDeviceWaitIdle(device_);

    graphicsCommandPool_.reset();
    computeCommandPool_.reset();

    for (int i = 0; i < COUNT_TEXTURE_FILTER; ++i)
    {
        vezDestroySampler(device_, clampSamplers_[i]);
        vezDestroySampler(device_, wrapSamplers_[i]);
    }

    vezDestroyDevice(device_);
    vezDestroyInstance(instance_);
}

//****************************************************************************
//
//  Function:   GraphicsDevice::BeginFrame
//
//  Purpose:    Resets pools.
//
//****************************************************************************
void GraphicsDevice::BeginFrame()
{
    for (int i = 0; i < STAT_COUNT; ++i)
        stats_[i] = 0;

    //vezDeviceWaitIdle(device_);
    graphicsCommandPool_->Reset();
    computeCommandPool_->Reset();
}

//****************************************************************************
//
//  Function:   GraphicsDevice::EndFrame
//
//  Purpose:    Resets caches and waits for an available next frame.
//
//****************************************************************************
void GraphicsDevice::EndFrame()
{
    //auto backbuffer = GetBackbuffer();
    //
    //VkPipelineStageFlags waitDstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    //
    //VkImage colorImage = backbuffer->textures_[0]->GetImage();
    //
    //VezPresentInfo presentInfo = {};
    //presentInfo.waitSemaphoreCount = 0;
    //presentInfo.pWaitSemaphores = nullptr;
    //presentInfo.pWaitDstStageMask = &waitDstStageMask;
    //presentInfo.swapchainCount = 1;
    //presentInfo.pSwapchains = &swapchain_;
    //presentInfo.pImages = &colorImage;
    //if (vezQueuePresent(graphicsQueue_, &presentInfo) != VK_SUCCESS)
    //{
    //
    //}

    //???
    //vezDeviceWaitIdle(device_);

    // return all of our UBOs to the pool
    uboCache_->FrameFinished();
}

//****************************************************************************
//
//  Function:   GraphicsDevice::CreateTexture
//
//  Purpose:    Given the traits creates a texture to match them (if possible).
//              TextureBuffers are a giant hack.
//
//  Return:     Newly created texture object.
//
//****************************************************************************
shared_ptr<Texture> GraphicsDevice::CreateTexture(TextureTraits traits)
{
    shared_ptr<Texture> tex(new Texture(this));
    tex->traits_ = traits;

    if (traits.kind_ == TextureBuffer)
    {
        VezBufferCreateInfo info = { };
        info.size = traits.width_;
        info.usage = VK_BUFFER_USAGE_UNIFORM_TEXEL_BUFFER_BIT;
        VkResult result = vezCreateBuffer(device_, VEZ_MEMORY_CPU_TO_GPU, &info, &tex->buffer_);

        VezBufferViewCreateInfo viewInfo = { };
        viewInfo.format = vk_TextureFormat(traits.format_);
        viewInfo.offset = 0;
        viewInfo.range = VK_WHOLE_SIZE;
        viewInfo.buffer = tex->buffer_;
        result = vezCreateBufferView(device_, &viewInfo, &tex->bufferView_);

        return tex;
    }

    VezImageCreateInfo info = { };
    info.format = vk_TextureFormat(traits.format_);
    info.imageType = vk_imageTypes[traits.kind_];
    info.extent = { traits.width_, traits.height_, traits.depth_ };
    info.mipLevels = traits.mips_;
    if (traits.kind_ == TextureCube)
        tex->traits_.layers_ = 6;
    else if (traits.kind_ == TextureCubeArray)
        tex->traits_.layers_ *= 6;
    else
        tex->traits_.layers_ = traits.layers_;
    info.arrayLayers = tex->traits_.layers_;
    info.samples = VK_SAMPLE_COUNT_1_BIT;
    info.tiling = VK_IMAGE_TILING_OPTIMAL; 

    info.usage = VK_IMAGE_USAGE_SAMPLED_BIT;
    if (traits.colorAttachment_)
        info.usage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT;
    else if (traits.depthAttachment_)
        info.usage |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    else
        info.usage |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    
    if (IsShadow(traits.format_))
        info.usage |= VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT;

    auto result = vezCreateImage(GetVKDevice(), VEZ_MEMORY_GPU_ONLY, &info, &tex->image_);
    assert(result != VK_SUCCESS);

    VezImageViewCreateInfo viewInfo = { };
    viewInfo.image = tex->image_;
    viewInfo.format = info.format;
    viewInfo.viewType = vk_viewTypes[traits.kind_];
    viewInfo.subresourceRange.layerCount = tex->GetLayers();
    viewInfo.subresourceRange.levelCount = traits.mips_;
    
    result = vezCreateImageView(GetVKDevice(), &viewInfo, &tex->view_);
    assert(result == VK_SUCCESS);

    return tex;
}

//****************************************************************************
//
//  Function:   GraphicsDevice::UpdateTexture
//
//  Purpose:    Pumps texel-data into the given mip/layer. This function is only
//              intended for whole-level/layer updates. Just wraps to texture.
//
//****************************************************************************
void GraphicsDevice::UpdateTexture(Texture* tex, void* data, uint32_t width, uint32_t height, uint32_t depth, uint32_t mip, uint32_t layer)
{
    if (tex)
        tex->SetData(data, width, height, depth, mip, layer);

}

//****************************************************************************
//
//  Function:   GraphicsDevice::UpdateTexture
//
//  Purpose:    Pumps texel-data into the given mip/layer. This function only
//              writes into a specific mip+layer combination along with a box
//              region into which to write.
//
//****************************************************************************
void GraphicsDevice::UpdateSubTexture(Texture* tex, void* data, uint32_t x, uint32_t y, uint32_t z, uint32_t width, uint32_t height, uint32_t depth, uint32_t mip, uint32_t layer)
{
    if (tex)
        tex->SetSubData(data, x, y, z, width, height, depth, mip, layer);
}

//****************************************************************************
//
//  Function:   GraphicsDevice::CreateFrameBuffer
//
//  Purpose:    Uses the given textures to create an FBO. Currently doesn't attempt
//              error handling so be conservative with the expected target-textures.
//
//  Return:     Newly created FBO.
//
//****************************************************************************
shared_ptr<FrameBuffer> GraphicsDevice::CreateFrameBuffer(const vector< shared_ptr<Texture> >& textures)
{
    auto ret = make_shared<FrameBuffer>(this, textures);

    VezFramebufferCreateInfo bufferCreate = { };
    vector<VkImageView> attachments;
    for (auto t : textures)
        attachments.push_back(t->GetView());
    bufferCreate.attachmentCount = (uint32_t)attachments.size();
    bufferCreate.pAttachments = attachments.data();
    bufferCreate.layers = 1;
    bufferCreate.height = textures[0]->GetHeight();
    bufferCreate.width = textures[0]->GetWidth();
    
    auto result = vezCreateFramebuffer(device_, &bufferCreate, &ret->fbo_);
    if (result == VK_SUCCESS)
        return ret;

    ret.reset();
    return ret;
}

//****************************************************************************
//
//  Function:   GraphicsDevice::CreateVertexBuffer
//
//  Purpose:    API filler
//
//  Return:     Buffer object setup as a VBO
//
//****************************************************************************
shared_ptr<Buffer> GraphicsDevice::CreateVertexBuffer()
{
    return make_shared<Buffer>(this, VertexBufferObject);
}

//****************************************************************************
//
//  Function:   GraphicsDevice::CreateIndexBuffer
//
//  Purpose:    API filler
//
//  Return:     Buffer object setup as a IBO
//
//****************************************************************************
shared_ptr<Buffer> GraphicsDevice::CreateIndexBuffer()
{
    return make_shared<Buffer>(this, IndexBufferObject);
}

//****************************************************************************
//
//  Function:   GraphicsDevice::CreateUniformBuffer
//
//  Purpose:    API filler
//
//  Return:     Buffer object setup as a UBO
//
//****************************************************************************
shared_ptr<Buffer> GraphicsDevice::CreateUniformBuffer()
{
    return make_shared<Buffer>(this, UniformBufferObject);
}

//****************************************************************************
//
//  Function:   GraphicsDevice::CreateShaderStorageBuffer
//
//  Purpose:    API filler
//
//  Return:     Buffer object setup as a SSBO
//
//****************************************************************************
shared_ptr<Buffer> GraphicsDevice::CreateShaderStorageBuffer()
{
    return make_shared<Buffer>(this, ShaderDataBufferObject);
}

//****************************************************************************
//
//  Function:   GraphicsDevice::UpdateBuffer
//
//  Purpose:    API filler, just delegates to buffer.
//
//****************************************************************************
void GraphicsDevice::UpdateBuffer(Buffer* buffer, void* data, uint32_t dataSize)
{
    if (buffer)
        buffer->SetData(data, dataSize);
}

//****************************************************************************
//
//  Function:   GraphicsDevice::GetGraphicsCmdBuffer
//
//  Purpose:    Calls into the graphics pool to try for a thread-safe command buffer.
//
//  Return:     VkCommandBuffer
//
//****************************************************************************
VkCommandBuffer GraphicsDevice::GetGraphicsCmdBuffer()
{
    return graphicsCommandPool_->Get();
}

//****************************************************************************
//
//  Function:   GraphicsDevice::GetComputeCmdBuffer
//
//  Purpose:    Calls into the compute pool to try for a thread-safe command buffer.
//
//  Return:     VkCommandBuffer
//
//****************************************************************************
VkCommandBuffer GraphicsDevice::GetComputeCmdBuffer()
{
    return computeCommandPool_->Get();
}

void GraphicsDevice::ExecuteCompute(const ComputeTask& task, bool block)
{
    VkCommandBuffer cmd = GetGraphicsCmdBuffer();
    vezBeginCommandBuffer(cmd, VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);

    for (auto& rb : task.readBuffers_)
        vezCmdBindBuffer(rb.buffer_->GetGPUObject(), rb.startBytes_, rb.size_ == UINT_MAX ? VK_WHOLE_SIZE : rb.size_, 0, rb.slot_, 0);
    for (auto& wb : task.writeBuffers_)
        vezCmdBindBuffer(wb.buffer_->GetGPUObject(), wb.startBytes_, wb.size_ == UINT_MAX ? VK_WHOLE_SIZE : wb.size_, 0, wb.slot_, 0);
    for (auto& rt : task.readTextures_)
        vezCmdBindImageView(rt.texture_->GetView(), GetSampler(rt.sampling_.filter_, rt.sampling_.wrap_), 1, rt.slot_, 0);
    for (auto& wt : task.writeTextures_)
    {
        
    }
    
    vezCmdDispatch(task.dispatch_[0], task.dispatch_[1], task.dispatch_[2]);

    vezEndCommandBuffer();
}

//****************************************************************************
//
//  Function:   CommandBufferPool::CommandBufferPool
//
//  Purpose:    Construct, need to know which queue things will go to regarding
//              graphics or compute queues.
//
//****************************************************************************
CommandBufferPool::CommandBufferPool(GraphicsDevice* device, bool forGraphics) :
    GPUObject(device),
    forGraphics_(forGraphics)
{

}

//****************************************************************************
//
//  Function:   CommandBufferPool::~CommandBufferPool
//
//  Purpose:    Destruct, and free VkCommandBuffers
//
//****************************************************************************
CommandBufferPool::~CommandBufferPool()
{
    Release();
}

//****************************************************************************
//
//  Function:   CommandBufferPool::Release
//
//  Purpose:    Frees all acquired command-buffers.
//
//****************************************************************************
void CommandBufferPool::Release()
{
    for (auto& p : pools_)
    {
        auto pool = p.second;
        if (pool->total_.size() > 0)
            vezFreeCommandBuffers(device_->GetVKDevice(), (uint32_t)pool->total_.size(), pool->total_.data());
        pool->total_.clear();
        pool->available_.clear();
        delete pool;
    }
    pools_.clear();
}

//****************************************************************************
//
//  Function:   CommandBufferPool::Get
//
//  Purpose:    Thread-aware source for pooling command-buffers. Command-buffer
//              allocation is slow so they're pooled.
//
//  Return:     VkCommandBuffer
//
//****************************************************************************
VkCommandBuffer CommandBufferPool::Get()
{
    PoolData* pool = nullptr;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto id = std::this_thread::get_id();

        auto foundPool = pools_.find(id);
        if (foundPool == pools_.end())
        {
            pool = new PoolData();
            pools_.insert({ id, pool });
        }
        else
            pool = foundPool->second;
    }

    if (pool->available_.empty())
    {
        const uint32_t BUFFER_BLOCK_SIZE = 64;

        VezCommandBufferAllocateInfo info = { };
        info.commandBufferCount = BUFFER_BLOCK_SIZE;
        if (forGraphics_)
            vezGetDeviceGraphicsQueue(device_->GetVKDevice(), 0, &info.queue);
        else
            vezGetDeviceComputeQueue(device_->GetVKDevice(), 0, &info.queue);

        VkCommandBuffer buffers[BUFFER_BLOCK_SIZE];
        VkResult result = vezAllocateCommandBuffers(device_->GetVKDevice(), &info, buffers);
        assert(result == VK_SUCCESS);

        // do this all in a single copy?
        pool->total_.reserve(pool->total_.size() + BUFFER_BLOCK_SIZE);
        pool->available_.reserve(BUFFER_BLOCK_SIZE);
        for (auto b : buffers)
        {
            pool->total_.emplace_back(b);
            pool->available_.emplace_back(b);
        }
    }

    VkCommandBuffer buffer = pool->available_.back();
    pool->available_.pop_back();
    pool->dished_.push_back(buffer);
    return buffer;
}

//****************************************************************************
//
//  Function:   CommandBufferPool::Reset
//
//  Purpose:    Wipes the consume command-buffers and returns them to the pool.
//
//****************************************************************************
void CommandBufferPool::Reset()
{
    for (auto& p : pools_)
    {
        p.second->available_ = p.second->total_;
        for (auto& vk : p.second->dished_)
            vezResetCommandBuffer(vk);
        p.second->dished_.clear();
    }
}

}
