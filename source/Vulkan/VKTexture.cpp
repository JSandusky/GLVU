#include <Texture.h>

#include "GraphicsDevice.h"

#include <algorithm>

namespace GLVU
{

//****************************************************************************
//
//  Function:   Texture::Texture
//
//  Purpose:    Construct, zero-init our vulkan objects
//
//****************************************************************************
Texture::Texture(GraphicsDevice* device) : GPUObject(device),
    image_(0),
    view_(0),
    buffer_(0), // smell the hack.
    bufferView_(0)
{
    traits_ = { };
}

//****************************************************************************
//
//  Function:   Texture::~Texture
//
//  Purpose:    Destruct, release data.
//
//****************************************************************************
Texture::~Texture()
{
    Release();
}

//****************************************************************************
//
//  Function:   Texture::Release
//
//  Purpose:    Destroy image/view and texel-buffer.
//
//****************************************************************************
void Texture::Release()
{
    auto vkDev = device_->GetVKDevice();
    if (view_)
        vezDestroyImageView(vkDev, view_);
    view_ = 0;
    if (image_)
        vezDestroyImage(vkDev, image_);
    image_ = 0;
    
    if (buffer_)
        vezDestroyBuffer(vkDev, buffer_);
    buffer_ = 0;
    if (bufferView_)
        vezDestroyBufferView(vkDev, bufferView_);
    bufferView_ = 0;

    traits_ = { };
}

//****************************************************************************
//
//  Function:   Texture::IsValid
//
//  Purpose:    Utility.
//
//  Return:     True, if we're likely valid.
//
//****************************************************************************
bool Texture::IsValid() const
{
    return (image_ != 0 && view_ != 0) || (buffer_ != 0 && bufferView_ != 0);
}

//****************************************************************************
//
//  Function:   Texture::SetData
//
//  Purpose:    Set texel data of a mip/layer. Designed for whole level
//              upload, NOT designed for partial data. This function is naive.
//
//****************************************************************************
void Texture::SetData(void* data, uint32_t width, uint32_t height, uint32_t depth, uint32_t mip, uint32_t layer)
{
    // sigh, hack.
    if (buffer_ != 0 && bufferView_ != 0)
    {
        auto result = vezBufferSubData(device_->GetVKDevice(), buffer_, 0, width, data);
        return;
    }

    if (image_ == 0 || view_ == 0)
        return;

    VezImageSubDataInfo info = { };
    info.imageOffset = { 0, 0, 0 };
    info.imageExtent = { width, std::max(height, 1u), std::max(depth, 1u) };
    info.imageSubresource.mipLevel = mip;
    info.imageSubresource.layerCount = std::max(layer, 1u);

    vezImageSubData(device_->GetVKDevice(), image_, &info, data);
}

//****************************************************************************
//
//  Function:   Texture::SetSubData
//
//  Purpose:    Uploads data into a single mip/layer of an image also that
//              is potentially partial data (a box).
//
//****************************************************************************
void Texture::SetSubData(void* data, uint32_t x, uint32_t y, uint32_t z, uint32_t width, uint32_t height, uint32_t depth, uint32_t mip, uint32_t layer)
{
    if (image_ == 0 || view_ == 0)
        return;

    VezImageSubDataInfo info = { };
    info.imageOffset = { (int)x, (int)y, (int)z };
    info.imageExtent = { width, std::max(height, 1u), std::max(depth, 1u) };
    info.imageSubresource.mipLevel = mip;
    info.imageSubresource.baseArrayLayer = layer;

    vezImageSubData(device_->GetVKDevice(), image_, &info, data);
}

//****************************************************************************
//
//  Function:   Texture::GenerateMipMaps
//
//  Purpose:    Generate the completion of an incomplete mip-chain,
//              based on VEZ example.
//
//****************************************************************************
void Texture::GenerateMipMaps()
{
    if (image_ == 0 || view_ == 0)
        return;

    // Generate mip levels on the graphics queue.
    VkQueue queue = 0;
    vezGetDeviceGraphicsQueue(device_->GetVKDevice(), 0, &queue);

    // Allocate a temporary command buffer.
    VezCommandBufferAllocateInfo allocInfo = {};
    allocInfo.queue = queue;
    allocInfo.commandBufferCount = 1;

    VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
    if (vezAllocateCommandBuffers(device_->GetVKDevice(), &allocInfo, &commandBuffer) != VK_SUCCESS)
        return;

    // Begin recording commands for mip level generation.
    if (vezBeginCommandBuffer(commandBuffer, VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT) != VK_SUCCESS)
        return;

    // Blit each level to the proceeding one.
    auto width = GetWidth();
    auto height = GetHeight();
    for (uint32_t layer = 0; layer < GetLayers(); ++layer)
    {
        for (uint32_t level = 1; level < GetMips(); ++level)
        {
            VezImageBlit blitInfo = {};
            blitInfo.srcSubresource.mipLevel = level - 1;
            blitInfo.srcSubresource.baseArrayLayer = layer;
            blitInfo.srcSubresource.layerCount = 1;
            blitInfo.srcOffsets[1].x = GetWidth() >> (level - 1);
            blitInfo.srcOffsets[1].y = GetHeight() >> (level - 1);
            blitInfo.srcOffsets[1].z = 1;

            blitInfo.dstSubresource.mipLevel = level;
            blitInfo.dstSubresource.baseArrayLayer = 0;
            blitInfo.dstSubresource.layerCount = 1;
            blitInfo.dstOffsets[1].x = GetWidth() >> level;
            blitInfo.dstOffsets[1].y = GetHeight() >> level;
            blitInfo.dstOffsets[1].z = 1;

            vezCmdBlitImage(image_, image_, 1, &blitInfo, VK_FILTER_LINEAR);
        }
    }

    // End recording and submit to the graphics queue and wait for all operations to complete.
    vezEndCommandBuffer();
    VezSubmitInfo submitInfo = { nullptr, 0, nullptr, nullptr, 1, &commandBuffer, 0, nullptr };
    vezQueueSubmit(queue, 1, &submitInfo, nullptr);
    vezQueueWaitIdle(queue);

    // Free the command buffer.
    vezFreeCommandBuffers(device_->GetVKDevice(), 1, &commandBuffer);
}
}