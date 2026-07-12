//****************************************************************************
//
//  File:       GLTexture.coo
//  License:    MIT
//  Project:    GLVU
//  Contents:   Because of the thickness of targets GLGraphicsDevice.cpp
//              does most of the real work for textures, mips are done here
//              though.
//
//****************************************************************************

#include "Texture.h"

#include "GraphicsDevice.h"

namespace GLVU
{

//****************************************************************************
//
//  Function:   Texture::Texture
//
//  Purpose:    Construct, inits the GL object to zero
//
//****************************************************************************
Texture::Texture(GraphicsDevice* device) :
    GPUObject(device),
    texture_(0),
    buffer_(0)
{
    traits_ = { };
}

//****************************************************************************
//
//  Function:   Texture::~Texture
//
//  Purpose:    Destruct, release GL object.
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
//  Purpose:    Delete the object(s) and clear out state.
//
//****************************************************************************
void Texture::Release()
{
    if (texture_)
        glDeleteTextures(1, &texture_);
    if (buffer_)
        glDeleteBuffers(1, &buffer_);

    buffer_ = 0;
    texture_ = 0;
    traits_ = { };
}

//****************************************************************************
//
//  Function:   Texture::IsValid
//
//  Purpose:    Utility
//
//  Return:     True if we have a texture object (even texel-buffers need one)
//
//****************************************************************************
bool Texture::IsValid() const 
{ 
    return texture_ != 0; 
}

//****************************************************************************
//
//  Function:   Texture::SetData
//
//  Purpose:    Writes a blob of texture data into the given target.
//              Because of targets+formats+datatypes OpenGL defers this responsibility
//              to the GLGraphicsDevice.cpp file rather than the opposite.
//              Just avoids a stack of `extern`
//
//****************************************************************************
void Texture::SetData(void* data, uint32_t width, uint32_t height, uint32_t depth, uint32_t mip, uint32_t layer)
{
    if (IsValid())
        device_->UpdateTexture(this, data, width, height, depth, mip, layer);
}

//****************************************************************************
//
//  Function:   Texture::SetData
//
//  Purpose:    Writes a blob of texture data into the given target.
//              Because of targets+formats+datatypes OpenGL defers this responsibility
//              to the GLGraphicsDevice.cpp file rather than the opposite.
//              Just avoids a stack of `extern`
//
//****************************************************************************
void Texture::SetSubData(void* data, uint32_t x, uint32_t y, uint32_t z, uint32_t width, uint32_t height, uint32_t depth, uint32_t mip, uint32_t layer)
{
    if (IsValid())
        device_->UpdateSubTexture(this, data, x, y, z, width, height, depth, mip, layer);
}

//****************************************************************************
//
//  Function:   Texture::GetTarget
//
//  Purpose:    Utility.
//
//  Return:     Returns the GL_ target for this texture.
//
//****************************************************************************
extern GLenum gl_TextureTarget(const Texture* tex);
const GLenum Texture::GetTarget() const
{
    return gl_TextureTarget(this);
}

//****************************************************************************
//
//  Function:   Texture::GenerateMipMaps
//
//  Purpose:    For a texture with incomplete mip-maps this function will
//              generate the mipmaps.
//
//****************************************************************************
void Texture::GenerateMipMaps()
{
    // cannot generate for buffers
    if (buffer_ != 0)
        return;

    assert(texture_);
    if (!IsValid() && GetMips() > 0)
        return;

    auto& state = device_->GetGLState();

    glActiveTexture(GL_TEXTURE0);
    if (state.SetTexture(0, texture_, GetTarget()))
        glBindTexture(GetTarget(), texture_);
    glGenerateMipmap(GetTarget());

    // TODO: is this accurate?
    uint32_t maxDim = std::max(GetWidth(), std::max(GetHeight(), GetDepth()));
    uint32_t cycle = 1;
    while (maxDim > 0)
    {
        maxDim /= 2;
        ++cycle;
    }
    traits_.mips_ = cycle;
}

//****************************************************************************
//
//  Function:   Texture::Resolve
//
//  Purpose:    Perform MSAA resolve if required.
//
//****************************************************************************
void Texture::Resolve() const
{
    if (texture_ == 0)
        return;
    if (resolveTexture_ == 0)
        return;

    //??glActiveTexture(GL_TEXTURE0);
    //??glBindTexture(GetTarget(), texture_);
    //??glCopyTexSubImage2D(GetTarget(), 0, 0, 0, 0 /*left*/, 0 /*top*/, resolveTexture_->GetWidth(), resolveTexture_->GetHeight());
    //??glBindTexture(GetTarget(), 0);
}

}