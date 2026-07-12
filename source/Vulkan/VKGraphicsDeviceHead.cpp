//****************************************************************************
//
//  File:       DX11GraphicsDeviceHead.cpp
//  License:    MIT
//  Project:    GLVU
//  Contents:   Implementation of rendering head for DX11
//
//****************************************************************************

#include "GraphicsDeviceHead.h"

#include "GraphicsDevice.h"
#include "Texture.h"

#include <VEZ/VEZ.h>

#include <array>
#include <vector>

#define LOG_VULKAN(MSG, RESULT) device_->LogFormat(GLVU_ERROR, MSG ": %u", RESULT)

#pragma optimize("", off)

namespace GLVU
{

extern TextureFormat vk_FormatFor(VkFormat);

GraphicsDeviceHead::GraphicsDeviceHead(GraphicsDevice* device, uint2 dim, VkSurfaceKHR surface, intptr_t userData) :
    GPUObject(device),
    beginCallback_(nullptr),
    endCallback_(nullptr),
    backbufferWidth_(dim.x),
    backbufferHeight_(dim.y),
    userData_(userData)
{
    surface_ = surface;
    
    VezSwapchainCreateInfo swapchainCreateInfo = {};
    swapchainCreateInfo.surface = surface_;
    swapchainCreateInfo.format = { VK_FORMAT_R8G8B8A8_UNORM, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR };
    swapchainCreateInfo.tripleBuffer = VK_FALSE;
    auto result = vezCreateSwapchain(device_->GetVKDevice(), &swapchainCreateInfo, &swapchain_);
    
    VkSurfaceFormatKHR swapchainFormat = {};
    vezGetSwapchainSurfaceFormat(swapchain_, &swapchainFormat);
    
    TextureTraits colorTraits = { };
    colorTraits.kind_ = Texture2D;
    colorTraits.format_ = vk_FormatFor(swapchainFormat.format);
    colorTraits.colorAttachment_ = true;
    colorTraits.width_ = std::max(backbufferWidth_, 1u);
    colorTraits.height_ = std::max(backbufferHeight_, 1u);
    colorTraits.depth_ = 1;
    colorTraits.mips_ = 1;
    colorTraits.layers_ = 1;
    auto colorTex = device_->CreateTexture(colorTraits);
    
    TextureTraits depthTraits = { };
    depthTraits.kind_ = Texture2D;
    depthTraits.format_ = TEX_DEPTH;
    depthTraits.depthAttachment_ = true;
    depthTraits.width_ = std::max(backbufferWidth_, 1u);
    depthTraits.height_ = std::max(backbufferHeight_, 1u);
    depthTraits.depth_ = 1;
    depthTraits.mips_ = 1;
    depthTraits.layers_ = 1;
    auto depthTex = device_->CreateTexture(depthTraits);
    
    backbuffer_ = device_->CreateFrameBuffer({ colorTex, depthTex });
    if (backbuffer_ == nullptr)
        throw "Failed to initialize graphics device head";
}

//****************************************************************************
//
//  Function:   GraphicsDeviceHead::Release
//
//  Purpose:    Disposes of contained resources.
//
//****************************************************************************
void GraphicsDeviceHead::Release()
{
    backbuffer_.reset();
    backbufferDepth_.reset();

    if (swapchain_ != 0)
        vezDestroySwapchain(device_->GetVKDevice(), swapchain_);
    swapchain_ = 0;
}

//****************************************************************************
//
//  Function:   GraphicsDeviceHead::Resize
//
//  Purpose:    Changes the swapchain buffer size, also creating the swapcahin
//              if it is null for some reason, and updates the GLVU side
//              backbuffer targets.
//
//****************************************************************************
void GraphicsDeviceHead::Resize(uint32_t w, uint32_t h)
{
    w = std::max(1u, w);
    h = std::max(1u, h);
    
    if (w == backbufferWidth_ && h == backbufferHeight_)
        return;
    
    if (swapchain_)
        vezDestroySwapchain(device_->GetVKDevice(), swapchain_);
    swapchain_ = 0;
    
    backbufferWidth_ = w;
    backbufferHeight_ = h;
    CreateBackbuffers();
}

//****************************************************************************
//
//  Function:   GraphicsDeviceHead::CreateBackbuffers
//
//  Purpose:    Constructs the D3D11 resources and GLVU side resources required
//              for the current swapchain.
//
//****************************************************************************
void GraphicsDeviceHead::CreateBackbuffers()
{
    if (backbuffer_)
        backbuffer_->Release();
    
    backbuffer_.reset();
    
    VezSwapchainCreateInfo swapchainCreateInfo = {};
    swapchainCreateInfo.surface = surface_;
    swapchainCreateInfo.format = { VK_FORMAT_R8G8B8A8_UNORM, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR };
    swapchainCreateInfo.tripleBuffer = VK_FALSE;
    auto result = vezCreateSwapchain(device_->GetVKDevice(), &swapchainCreateInfo, &swapchain_);
    
    VkSurfaceFormatKHR swapchainFormat = {};
    vezGetSwapchainSurfaceFormat(swapchain_, &swapchainFormat);
    
    TextureTraits colorTraits = { };
    colorTraits.kind_ = Texture2D;
    colorTraits.format_ = vk_FormatFor(swapchainFormat.format);
    colorTraits.colorAttachment_ = true;
    colorTraits.width_ = backbufferWidth_;
    colorTraits.height_ = backbufferHeight_;
    colorTraits.depth_ = 1;
    colorTraits.mips_ = 1;
    colorTraits.layers_ = 1;
    auto colorTex = device_->CreateTexture(colorTraits);
    
    TextureTraits depthTraits = { };
    depthTraits.kind_ = Texture2D;
    depthTraits.format_ = TEX_DEPTH;
    depthTraits.depthAttachment_ = true;
    depthTraits.width_ = backbufferWidth_;
    depthTraits.height_ = backbufferHeight_;
    depthTraits.depth_ = 1;
    depthTraits.mips_ = 1;
    depthTraits.layers_ = 1;
    auto depthTex = device_->CreateTexture(depthTraits);
    
    backbuffer_ = device_->CreateFrameBuffer({ colorTex, depthTex });
    backbuffer_->reportWidth_ = backbufferWidth_;
    backbuffer_->reportHeight_ = backbufferHeight_;
}

//****************************************************************************
//
//  Function:   GraphicsDeviceHead::Flush
//
//  Purpose:    Submits the queued command-buffers and then presents.
//
//****************************************************************************
void GraphicsDeviceHead::Flush()
{

}

void GraphicsDeviceHead::FinishRendering()
{
    VkQueue graphicsQueue = 0;
    vezGetDeviceGraphicsQueue(device_->GetVKDevice(), 0, &graphicsQueue);
    
    std::array<VkSemaphore, 3> semaphores;
    if (!bufferQueue_.empty())
    {
        VezSubmitInfo submission = {};
        submission.commandBufferCount = (uint32_t)bufferQueue_.size();
        submission.pCommandBuffers = bufferQueue_.data();
        submission.signalSemaphoreCount = 1;
        submission.pSignalSemaphores = &semaphores[0];
    
        VkResult result = vezQueueSubmit(graphicsQueue, 1, &submission, nullptr);
        if (result != VK_SUCCESS)
            LOG_VULKAN("Failed to submit device-head buffer-queue", result);
    }
    
    VkPipelineStageFlags waitDstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    auto srcImage = backbuffer_->GetTexture(0)->GetImage();
    
    VezPresentInfo presentInfo = {};
    presentInfo.waitSemaphoreCount = bufferQueue_.empty() ? 0 : 1;
    presentInfo.pWaitSemaphores = bufferQueue_.empty() ? nullptr : &semaphores[0];
    presentInfo.pWaitDstStageMask = &waitDstStageMask;
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = &swapchain_;
    presentInfo.pImages = &srcImage;
    VkResult result = vezQueuePresent(graphicsQueue, &presentInfo);
    if (result != VK_SUCCESS)
        LOG_VULKAN("Failed to present swap-chain", result);
}

//****************************************************************************
//
//  Function:   GraphicsDeviceHead::GetCommandBuffer
//
//  Purpose:    Routes to the device for getting the command-buffer.
//
//****************************************************************************
VkCommandBuffer GraphicsDeviceHead::GetCommandBuffer()
{
    return device_->GetGraphicsCmdBuffer();
}

}
