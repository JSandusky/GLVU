#include "GraphicsDeviceHead.h"

#include "GraphicsDevice.h"

#include <array>

using namespace std;

namespace GLVU
{

//****************************************************************************
//
//  Function:   GraphicsDeviceHead::GraphicsDeviceHead
//
//  Purpose:    Constructor.
//
//****************************************************************************
GraphicsDeviceHead::GraphicsDeviceHead(GraphicsDevice* device, uint2 dim, intptr_t userData) :
    GPUObject(device),
    beginCallback_(nullptr),
    endCallback_(nullptr),
    userData_(userData)
{
#if defined(GLVU_GL) || defined(GLVU_GLES3)
    backbufferWidth_ = dim.x;
    backbufferHeight_ = dim.y;
#else
    // should not get here
    assert(0);
#endif
}

//****************************************************************************
//
//  Function:   GraphicsDeviceHead::~GraphicsDeviceHead
//
//  Purpose:    Destructor.
//
//****************************************************************************
GraphicsDeviceHead::~GraphicsDeviceHead()
{
    if (backbuffer_)
        backbuffer_->Release();
    backbuffer_.reset();

    if (backbufferDepth_)
        backbufferDepth_->Release();
    backbufferDepth_.reset();

#if defined(GLVU_VK)
    vezDestroySwapchain(device_->GetVKDevice(), swapchain_);
    backbuffer_.reset();
#endif

#if defined(GLVU_DX11)

    if (swapChainBuffer_)
        swapChainBuffer_->Release();
    if (swapChain_)
        swapChain_->Release();
#endif
}

//****************************************************************************
//
//  Function:   GraphicsDeviceHead::BeginHead
//
//  Purpose:    Invokes callbacks and sets up the required data for beginning
//              a frame.
//
//  Todo:       Has to set the master backbuffer, which is a hack that
//              needs to be fixed eventually.
//
//****************************************************************************
void GraphicsDeviceHead::BeginHead()
{
    if (beginCallback_)
        beginCallback_(this, userData_);

#if defined(GLVU_DX11)
    backbuffer_->Bind();
    float clear[] = { 1.0f, 1.0f, 1.0f, 1.0f };
    backbuffer_->Clear(clear);
#endif
#if defined(GLVU_DX11) || defined(GLVU_VK)
    device_->SetBackbuffer(backbuffer_);
#endif
}

//****************************************************************************
//
//  Function:   GraphicsDeviceHead::EndHead
//
//  Purpose:    Last command for rendering to a head, responsible for flush
//              and reseting the master backbuffer.
//
//****************************************************************************
void GraphicsDeviceHead::EndHead()
{
    if (endCallback_)
        endCallback_(this, userData_);
    Flush();
    device_->SetBackbuffer(nullptr);
}

#if defined(GLVU_GL)
//****************************************************************************
//
//  Function:   GraphicsDeviceHead::Flush
//
//  Purpose:    Just calls glFinish.
//
//****************************************************************************
void GraphicsDeviceHead::Flush()
{
    glFinish();
}
#endif

}