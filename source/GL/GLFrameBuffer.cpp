//****************************************************************************
//
//  File:       GLFrameBuffer.cpp
//  License:    MIT
//  Project:    GLVU
//  Contents:   FBO release/bind for OpenGL.
//              Creation is handled in GLGraphicsDevice.cpp
//
//****************************************************************************

#include "Texture.h"
#include "GraphicsDevice.h"

namespace GLVU
{

//****************************************************************************
//
//  Function:   FrameBuffer::FrameBuffer
//
//  Purpose:    Construct, and zero-init
//
//****************************************************************************
FrameBuffer::FrameBuffer(GraphicsDevice* device, const std::vector<std::shared_ptr<Texture> >& textures) :
    GPUObject(device),
    textures_(textures),
    fbo_(0)
{

}

//****************************************************************************
//
//  Function:   FrameBuffer::FrameBuffer
//
//  Purpose:    Construct
//
//****************************************************************************
FrameBuffer::FrameBuffer(GraphicsDevice* device, const std::shared_ptr<Texture>& texture, int layer) :
    GPUObject(device),
    fbo_(0),
    layer_(layer)
{
    textures_.push_back(texture);
}

//****************************************************************************
//
//  Function:   FrameBuffer::~FrameBuffer
//
//  Purpose:    Destruct, free GPU objects.
//
//****************************************************************************
FrameBuffer::~FrameBuffer()
{
    Release();
}

//****************************************************************************
//
//  Function:   FrameBuffer::IsValid
//
//  Purpose:    Utility, here because GL backbuffer is a hack.
//
//  Return:     True if there's an fbo, or the whole thing is nil - which is a
//              an OpenGL hack.
//
//****************************************************************************
bool FrameBuffer::IsValid() const
{
    // the latter case is for the backbuffer
    return fbo_ != 0 || (fbo_ == 0 && textures_.size() == 0);
}

//****************************************************************************
//
//  Function:   FrameBuffer::Release
//
//  Purpose:    Delete the FBO, textures aren't the responsibility here (shared_ptr)
//
//****************************************************************************
void FrameBuffer::Release()
{
    if (fbo_ && fbo_ != 0xFFFFFFFF)
        glDeleteFramebuffers(1, &fbo_);
    fbo_ = 0;
}

//****************************************************************************
//
//  Function:   FrameBuffer::Bind
//
//  Purpose:    Binds the FBO and sets the draw-buffers, the repeated draw-buffer
//              setting is necessary for some Intel GPUs.
//
//****************************************************************************
void FrameBuffer::Bind()
{
    //
    if (fbo_ == 0)
    {
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        return;
    }

    uint32_t numColors = 0;
    bool hasDepth = false;
    for (const auto& b : textures_)
    {
        if (b->GetFormat() != TEX_DEPTH)
            ++numColors;
        else
            hasDepth = true;
    }

    GLenum buffers[8];
    uint32_t index = 0;

    uint32_t buffCt = 0;
    for (auto i = 0; i < textures_.size(); ++i)
    {
        // watch out for this, missing the ! can mangle all the things
        if (!IsDepth(textures_[i]->GetFormat()))
        {
            buffers[i] = GL_COLOR_ATTACHMENT0 + index;
            ++index;
            ++buffCt;
        }
    }

    glBindFramebuffer(GL_FRAMEBUFFER, fbo_);

    // Intel HD series is a jerk about this
    if (buffCt != 0)
        glDrawBuffers(buffCt, buffers);
    else
        glDrawBuffer(GL_NONE);
}

//****************************************************************************
//
//  Function:   FrameBuffer::Clear
//
//  Purpose:    Utility to wipe the FBO, stencil/depth and all.
//
//****************************************************************************
void FrameBuffer::Clear(const float* color, bool depth, bool stencil)
{
    Bind();
    device_->GetGLState().SetViewport(0, 0, textures_[0]->GetWidth(), textures_[0]->GetHeight());

    if (color)
        glClearColor(color[0], color[1], color[2], color[3]);
    glClearStencil(0);
    glClearDepth(1.0);

    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glStencilMask(GL_TRUE);
    glDepthMask(GL_TRUE);
        
    int mask = 0;
    if (color)
        mask |= GL_COLOR_BUFFER_BIT;
    if (depth)
        mask |= GL_DEPTH_BUFFER_BIT;
    if (stencil)
        mask |= GL_STENCIL_BUFFER_BIT;
    glClear(mask);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

}