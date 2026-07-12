#include "Texture.h"

#include "GraphicsDevice.h"

using namespace std;

namespace GLVU
{

extern VkImageViewType vk_viewTypes[];
extern VkImageViewType vk_viewTypes[];
extern VkFormat vk_TextureFormat(TextureFormat format);

//****************************************************************************
//
//  Function:   FrameBuffer::FrameBuffer
//
//  Purpose:    Construct, doesn't actually create an FBO object due to Vulkan
//              cluelessness
//
//****************************************************************************
FrameBuffer::FrameBuffer(GraphicsDevice* device, const std::vector<std::shared_ptr<Texture> >& textures) :
    GPUObject(device),
    textures_(textures),
    ownedView_(0)
{
}

//****************************************************************************
//
//  Function:   FrameBuffer::FrameBuffer
//
//  Purpose:    Construct, doesn't actually create an FBO object due to Vulkan
//              cluelessness
//
//****************************************************************************
FrameBuffer::FrameBuffer(GraphicsDevice* device, const std::shared_ptr<Texture>& texture, int layer) :
    GPUObject(device),
    ownedView_(0),
    layer_(layer)
{
    textures_.push_back(texture);
}

//****************************************************************************
//
//  Function:   FrameBuffer::~FrameBuffer
//
//  Purpose:    Destruct
//
//****************************************************************************
FrameBuffer::~FrameBuffer()
{

}

//****************************************************************************
//
//  Function:   FrameBuffer::IsValid
//
//  Purpose:    Utility
//
//  Return:     True if likely valid.
//
//****************************************************************************
bool FrameBuffer::IsValid() const
{
    return fbo_ != 0;
}

//****************************************************************************
//
//  Function:   FrameBuffer::Release
//
//  Purpose:    Destroy the FBO object if it exists.
//
//****************************************************************************
void FrameBuffer::Release()
{
    if (fbo_)
        vezDestroyFramebuffer(device_->GetVKDevice(), fbo_);
    fbo_ = 0;

    if (ownedView_)
        vezDestroyImageView(device_->GetVKDevice(), ownedView_);
    ownedView_ = 0;

    textures_.clear();
    ownedDepth_.reset();
}

//****************************************************************************
//
//  Function:   FrameBuffer::Bind
//
//  Purpose:    Unused on Vulkan, boiler-plate is specific
//
//****************************************************************************
void FrameBuffer::Bind()
{

}

//****************************************************************************
//
//  Function:   FrameBuffer::Clear
//
//  Purpose:    Not-ideal utility for wiping color, stencil, and depth of the FBO.
//
//****************************************************************************
void FrameBuffer::Clear(const float* c, bool depth, bool stencil)
{
    VkCommandBuffer cmd = device_->GetGraphicsCmdBuffer();

    vezBeginCommandBuffer(cmd, VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);

    VkClearColorValue color;
    color.float32[0] = c ? c[0] : 0.0f;
    color.float32[1] = c ? c[1] : 0.0f;
    color.float32[2] = c ? c[2] : 0.0f;
    color.float32[3] = c ? c[3] : 0.0f;

    VkClearDepthStencilValue dsv;
    dsv.depth = 1.0f;
    dsv.stencil = 0;

    VezImageSubresourceRange range = { };
    range.layerCount = 1;
    range.levelCount = 1;

    for (auto t : textures_)
    {
        if (IsDepth(t->GetFormat()) && depth)
            vezCmdClearDepthStencilImage(t->GetImage(), &dsv, 1, &range);
        else
            vezCmdClearColorImage(t->GetImage(), &color, 1, &range);
    }

    vezEndCommandBuffer();

    VkQueue queue;
    vezGetDeviceGraphicsQueue(device_->GetVKDevice(), 0, &queue);

    VezSubmitInfo submitInfo = {};
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &cmd;
    auto result = vezQueueSubmit(queue, 1, &submitInfo, 0);
    //vezQueueWaitIdle(alloc.queue);
}

}