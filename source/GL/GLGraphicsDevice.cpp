//****************************************************************************
//
//  File:       GLGraphicsDevice.cpp
//  License:    MIT
//  Project:    GLVU
//  Contents:   Device management and GPU object creation for OpenGL.
//              Because GL APIs typically require a lot of `marker` state (ie. GL_TEXTURE_2D/3D/CUBE_MAP/etc)
//              creation is handled here where those details are at their thickest.
//
//****************************************************************************

#include <GraphicsDevice.h>
#include <ShaderCache.h>

#include <GLEW/GL/glew.h>

#include <algorithm>

using namespace std;

namespace GLVU
{

GLenum gl_TextureTarget(const Texture* tex)
{
    if (tex == nullptr)
        return GL_TEXTURE_2D;
    switch (tex->GetTextureKind())
    {
    case Texture3D:
        return GL_TEXTURE_3D;
    case Texture2DArray:
        return GL_TEXTURE_2D_ARRAY;
    case TextureCube:
        return GL_TEXTURE_CUBE_MAP;
    case Texture1D:
        return GL_TEXTURE_1D;
    case TextureCubeArray:
        return GL_TEXTURE_CUBE_MAP_ARRAY;
    case TextureBuffer:
        return GL_TEXTURE_BUFFER;
    }
    return GL_TEXTURE_2D;
}

GLenum gl_InternalFormat[] = {
    GL_RGB,                             // TEX_RGB8,
    GL_RGBA,                            // TEX_RGBA8,
    GL_RGBA16F,                         // TEX_RGBA16F,
    GL_RG16F,                           // TEX_RG16F,
    GL_COMPRESSED_RGBA_S3TC_DXT1_EXT,   // DXT1
    GL_COMPRESSED_RGBA_S3TC_DXT3_EXT,   // DXT3
    GL_COMPRESSED_RGBA_S3TC_DXT5_EXT,   // DXT5
    GL_COMPRESSED_RED_RGTC1,            // BC4
    GL_COMPRESSED_RG_RGTC2,             // BC5
    GL_DEPTH_COMPONENT16,               // TEX_SHADOW16,
    GL_DEPTH_COMPONENT32,               // TEX_SHADOW32,
    GL_DEPTH24_STENCIL8,                // TEX_DEPTH
    GL_BGRA,                            // TEX_BGRA8
    GL_R32F,                            // TEX_R32F
    GL_R32UI,                           // TEX_R32U
    GL_RGBA8UI,                         // TEX_RGBA8UI
};

GLenum gl_GetInternalFormat(TextureFormat fmt)
{
    return gl_InternalFormat[fmt];
}

GLenum gl_ComputeFormat(TextureFormat fmt)
{
    auto r = gl_InternalFormat[fmt];
    if (r == GL_RGBA)
        return GL_RGBA8;
    return r;
}

GLenum gl_DataType(GLenum format)
{
    if (format == GL_DEPTH24_STENCIL8_EXT)
        return GL_UNSIGNED_INT_24_8_EXT;
    else if (format == GL_R32UI)
        return GL_UNSIGNED_INT;
    else if (format == GL_RG16 || format == GL_RGBA16)
        return GL_UNSIGNED_SHORT;
    else if (format == GL_RGBA32F_ARB || format == GL_RG32F || format == GL_R32F)
        return GL_FLOAT;
    else if (format == GL_RGBA16F_ARB || format == GL_RG16F || format == GL_R16F)
        return GL_HALF_FLOAT;
    else
        return GL_UNSIGNED_BYTE;
}

GLenum gl_ExternalFormat(GLenum format)
{
    if (format == GL_DEPTH_COMPONENT16 || format == GL_DEPTH_COMPONENT24 || format == GL_DEPTH_COMPONENT32)
        return GL_DEPTH_COMPONENT;
    else if (format == GL_DEPTH24_STENCIL8)
        return GL_DEPTH_STENCIL_EXT;
    else if (format == GL_R8 || format == GL_R16F || format == GL_R32F)
        return GL_RED;
    else if (format == GL_RG8 || format == GL_RG16 || format == GL_RG16F || format == GL_RG32F)
        return GL_RG;
    else if (format == GL_RGBA16 || format == GL_RGBA16F || format == GL_RGBA32F)
        return GL_RGBA;
    else if (format == GL_SRGB_EXT)
        return GL_RGB;
    else
        return format;
}

unsigned gl_RowDataSize(GLuint glFormat, int width)
{
    switch (glFormat)
    {
    case GL_ALPHA:
    case GL_LUMINANCE:
        return (unsigned)width;

    case GL_LUMINANCE_ALPHA:
        return (unsigned)(width * 2);

    case GL_RGB:
        return (unsigned)(width * 3);

    case GL_RGBA:
    case GL_RGBA8UI:
    case GL_DEPTH24_STENCIL8_EXT:
    case GL_RG16:
    case GL_RG16F:
    case GL_R32F:
    case GL_R32UI:
        return (unsigned)(width * 4);

    case GL_R8:
        return (unsigned)width;

    case GL_RG8:
    case GL_R16F:
        return (unsigned)(width * 2);

    case GL_RGBA16:
    case GL_RGBA16F_ARB:
        return (unsigned)(width * 8);

    case GL_RGBA32F_ARB:
    case GL_RGBA32UI:
        return (unsigned)(width * 16);

    case GL_COMPRESSED_RGBA_S3TC_DXT1_EXT:
    case GL_COMPRESSED_RED_RGTC1:
        return ((unsigned)(width + 3) >> 2u) * 8;

    case GL_COMPRESSED_RGBA_S3TC_DXT3_EXT:
    case GL_COMPRESSED_RGBA_S3TC_DXT5_EXT:
    case GL_COMPRESSED_RG_RGTC2:
        return ((unsigned)(width + 3) >> 2u) * 16;

    default:
        return 0;
    }
}

size_t gl_GetBlockSize(GLuint glFormat, unsigned width, unsigned height)
{
    if (glFormat == GL_COMPRESSED_RGBA_S3TC_DXT1_EXT || 
        glFormat == GL_COMPRESSED_RGBA_S3TC_DXT3_EXT || 
        glFormat == GL_COMPRESSED_RGBA_S3TC_DXT5_EXT ||
        glFormat == GL_COMPRESSED_RED_RGTC1 || 
        glFormat == GL_COMPRESSED_RG_RGTC2)
        return gl_RowDataSize(glFormat, width) * ((height + 3) >> 2u);
    else
        return gl_RowDataSize(glFormat, width) * height;
}

GLuint gl_CompressedFormat(TextureFormat format)
{
    if (format == TextureFormat::TEX_DXT1)
        return GL_COMPRESSED_RGBA_S3TC_DXT1_EXT;
    if (format == TextureFormat::TEX_DXT3)
        return GL_COMPRESSED_RGBA_S3TC_DXT3_EXT;
    if (format == TextureFormat::TEX_DXT5)
        return GL_COMPRESSED_RGBA_S3TC_DXT5_EXT;
    return 0;
}

//****************************************************************************
//
//  Function:   GraphicsDevice::GraphicsDevice
//
//  Purpose:    Construct, grab current GL state and setup caches.
//
//****************************************************************************
GraphicsDevice::GraphicsDevice() :
    effectCache_(this),
    vao_(0)
{
    shaderCache_.reset(new ShaderCache(this));
}

//****************************************************************************
//
//  Function:   GraphicsDevice::~GraphicsDevice
//
//  Purpose:    Destruct, should have nothing to do after GraphicsDevice::Shutdown()
//
//****************************************************************************
GraphicsDevice::~GraphicsDevice()
{

}

//****************************************************************************
//
//  Function:   GraphicsDevice::OpenDevice
//
//  Purpose:    Queries device capabilities, creates shared sampler objects,
//              creates the default objects, and then sets up some caching.
//
//****************************************************************************
bool GraphicsDevice::OpenDevice(const char** requiredExt, uint32_t item)
{
    auto ret = glewInit();
    assert(ret == GLEW_OK);

    LogFormat(0, "OpenGL version: %s", (char*)glGetString(GL_VERSION));
    if (graphicsFeatures_.geometryShader_ = glewIsExtensionSupported("GL_ARB_geometry_shader4"))
        LogMessage("+ Geometry Shader 4.0", GLVU_INFO);
    else
        LogMessage("- Geometry Shader 4.0", GLVU_ERROR);

    if (graphicsFeatures_.tessellation_ = glewIsExtensionSupported("GL_ARB_tessellation_shader"))
        LogMessage("+ Tessellation", GLVU_INFO);
    else
        LogMessage("- Tessellation", GLVU_ERROR);

    if (graphicsFeatures_.transformFeedback_ |= glewIsExtensionSupported("GL_ARB_transform_feedback2"))
        LogMessage("+ Transform Feedback", GLVU_INFO);
    else
        LogMessage("- Transform Feedback", GLVU_ERROR);

    if (graphicsFeatures_.transformFeedback_ |= glewIsExtensionSupported("GL_ARB_transform_feedback_instanced"))
        LogMessage("+ Instanced Transform Feedback", GLVU_INFO);
    else
        LogMessage("- Instanced Transform Feedback", GLVU_ERROR);

    if (graphicsFeatures_.clipControl_ = glewIsExtensionSupported("GL_ARB_clip_control"))
        LogMessage("+ Clip Control", GLVU_INFO);
    else
        LogMessage("- Clip Control", GLVU_ERROR);

    if (graphicsFeatures_.compute_= glewIsExtensionSupported("GL_ARB_compute_shader"))
        LogMessage("+ Compute Shader", GLVU_INFO);
    else
        LogMessage("- Compute Shader", GLVU_ERROR);

    if (graphicsFeatures_.shaderStorageBuffer_ = glewIsExtensionSupported("GL_ARB_shader_storage_buffer_object"))
        LogMessage("+ Shader Storage Buffer", GLVU_INFO);
    else
        LogMessage("- Shader Storage Buffer", GLVU_ERROR);

    // Query our required information values.
    glGetIntegerv(GL_MAX_UNIFORM_BLOCK_SIZE, (GLint*)&graphicsFeatures_.maxUBOSize_);
    glGetIntegerv(GL_UNIFORM_BUFFER_OFFSET_ALIGNMENT, (GLint*)&graphicsFeatures_.minUBOAlignment_);

    // capture our natural state
    glState_.CaptureCurrent();

    auto glDebugCall = [](GLenum source, GLenum type, GLuint id, GLenum severity, GLsizei length, const GLchar* message, const void* userParam) {
        switch (severity) {
        case GL_DEBUG_TYPE_ERROR:
            ((GraphicsDevice*)userParam)->LogMessage(message, GLVU_ERROR);
            break;
        case GL_DEBUG_TYPE_PERFORMANCE:
            ((GraphicsDevice*)userParam)->LogMessage(message, GLVU_ERROR);
            break;
        case GL_DEBUG_TYPE_MARKER:
            ((GraphicsDevice*)userParam)->LogMessage(message, GLVU_WARNING);
            break;
        default:
            ((GraphicsDevice*)userParam)->LogMessage(message, GLVU_INFO);
            break;
        }
    };
    glDebugMessageCallback(glDebugCall, this);

    glGenVertexArrays(1, &vao_);
    glBindVertexArray(vao_);

    static GLint filterModes[] = {
        GL_NEAREST, // POINT
        GL_LINEAR_MIPMAP_NEAREST, // LINEAR
        GL_LINEAR_MIPMAP_LINEAR, // TRILINEAR
        GL_LINEAR_MIPMAP_LINEAR, // ANISOTROPIC
        GL_LINEAR, // SHADOW
        GL_LINEAR
    };

    for (int i = 0; i < COUNT_TEXTURE_FILTER; ++i)
    {
        glGenSamplers(1, &clampSamplers_[i]);
        glGenSamplers(1, &wrapSamplers_[i]);

        glSamplerParameteri(clampSamplers_[i], GL_TEXTURE_MIN_FILTER, filterModes[i]);
        glSamplerParameteri(wrapSamplers_[i], GL_TEXTURE_MIN_FILTER, filterModes[i]);

        glSamplerParameteri(clampSamplers_[i], GL_TEXTURE_MAG_FILTER, filterModes[i]);
        glSamplerParameteri(wrapSamplers_[i], GL_TEXTURE_MAG_FILTER, filterModes[i]);

        glSamplerParameteri(clampSamplers_[i], GL_TEXTURE_CUBE_MAP_SEAMLESS, 1);
        glSamplerParameteri(wrapSamplers_[i], GL_TEXTURE_CUBE_MAP_SEAMLESS, 1);

        if (i == FILTER_ANISOTROPIC)
        {
            glSamplerParameterf(clampSamplers_[i], GL_TEXTURE_MAX_ANISOTROPY, 4.0f);
            glSamplerParameterf(wrapSamplers_[i], GL_TEXTURE_MAX_ANISOTROPY, 4.0f);
        }

        if (i == FILTER_SHADOW)
        {
            glSamplerParameteri(clampSamplers_[i], GL_TEXTURE_COMPARE_MODE, GL_COMPARE_REF_TO_TEXTURE);
            glSamplerParameteri(clampSamplers_[i], GL_TEXTURE_COMPARE_FUNC, GL_LESS);
            glSamplerParameteri(wrapSamplers_[i], GL_TEXTURE_COMPARE_MODE, GL_COMPARE_REF_TO_TEXTURE);
            glSamplerParameteri(wrapSamplers_[i], GL_TEXTURE_COMPARE_FUNC, GL_LESS);
        }

        glSamplerParameteri(clampSamplers_[i], GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glSamplerParameteri(wrapSamplers_[i], GL_TEXTURE_WRAP_S, GL_REPEAT);

        glSamplerParameteri(clampSamplers_[i], GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glSamplerParameteri(wrapSamplers_[i], GL_TEXTURE_WRAP_T, GL_REPEAT);

        glSamplerParameteri(clampSamplers_[i], GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);            
        glSamplerParameteri(wrapSamplers_[i], GL_TEXTURE_WRAP_R, GL_REPEAT);

        // TODO:
        //glSamplerParameterf(wrapSamplers_[i], GL_TEXTURE_LOD_BIAS, 7.0);
        //glSamplerParameterf(clampSamplers_[i], GL_TEXTURE_LOD_BIAS, 7.0);
    }

    CreateDefaultObjects();

    backbuffer_.reset(new FrameBuffer(this, vector<shared_ptr<Texture> >{ }));

    uboCache_.reset(new ScratchBufferCache(this));

    return true;
}

//****************************************************************************
//
//  Function:   GraphicsDevice::InitSurface
//
//  Purpose:    No-op on GL
//
//****************************************************************************
void GraphicsDevice::InitSurface(uint32_t width, uint32_t height)
{

}

//****************************************************************************
//
//  Function:   GraphicsDevice::PlatformShutdown
//
//  Purpose:    Destroy samplers and the global VAO.
//
//****************************************************************************
void GraphicsDevice::PlatformShutdown()
{
    for (int i = 0; i < COUNT_TEXTURE_FILTER; ++i)
    {
        glDeleteSamplers(1, &wrapSamplers_[i]);
        glDeleteSamplers(1, &clampSamplers_[i]);
    }
    glDeleteVertexArrays(1, &vao_);
}

//****************************************************************************
//
//  Function:   GraphicsDevice::OnResize
//
//  Purpose:    Updates the *hack* backbuffer.
//
//****************************************************************************
void GraphicsDevice::OnResize(uint32_t width, uint32_t height)
{
    // OpenGL doesn't have meaningful information
    backbuffer_->reportWidth_ = width;
    backbuffer_->reportHeight_ = height;
}

//****************************************************************************
//
//  Function:   GraphicsDevice::BeginFrame
//
//  Purpose:    Restores GL-state to what was seen at initialization,
//              ensures the VAO is active, and that our backbuffer is bound.
//
//****************************************************************************
void GraphicsDevice::BeginFrame()
{
    for (int i = 0; i < STAT_COUNT; ++i)
        stats_[i] = 0;

    glState_.Apply(nullptr);

    // bind our VAO
    glBindVertexArray(vao_);

    backbuffer_->Bind();

    glClearColor(1, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
}

//****************************************************************************
//
//  Function:   GraphicsDevice::EndFrame
//
//  Purpose:    Resets scratch caches and calls glFinish
//
//****************************************************************************
void GraphicsDevice::EndFrame()
{
    uboCache_->FrameFinished();
    glFinish();
}

//****************************************************************************
//
//  Function:   GraphicsDevice::CreateTexture
//
//  Purpose:    Given the traits creates a texture to match them (if possible).
//              TextureBuffers are a bit of hack, though not as bad here as in Vulkan.
//
//  Return:     Newly created texture object.
//
//****************************************************************************
shared_ptr<Texture> GraphicsDevice::CreateTexture(TextureTraits traits)
{
    auto tex = shared_ptr<Texture>(new Texture(this));
    glGenTextures(1, &tex->texture_);
    tex->traits_ = traits;

    const auto format = gl_InternalFormat[traits.format_];
    const auto externalFormat = gl_ExternalFormat(format);
    const auto dataType = gl_DataType(format);
               
    if (traits.kind_ == TextureBuffer)
    {
        glGenBuffers(1, &tex->buffer_);
        glBindBuffer(GL_TEXTURE_BUFFER, tex->buffer_);
        glBufferData(GL_TEXTURE_BUFFER, traits.width_, nullptr, GL_STATIC_DRAW);
        glGenTextures(1, &tex->texture_);
        glBindBuffer(GL_TEXTURE_BUFFER, 0);
    }
    else
    {
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(gl_TextureTarget(tex.get()), tex->texture_);

        // eh? GL hates compressed textures?
        if (tex->IsCompressed())
            return tex;

        if (tex->GetTextureKind() == Texture2D)
        {
            if (tex->IsCompressed())
                glCompressedTexImage2D(gl_TextureTarget(tex.get()), 0, format, traits.width_, traits.height_, 0, 0, nullptr);
            else
                glTexImage2D(gl_TextureTarget(tex.get()), 0, format, traits.width_, traits.height_, 0, externalFormat, dataType, nullptr);
        }
        else if (tex->GetTextureKind() == TextureCube)
            glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X, 0, format, traits.width_, traits.height_, 0, externalFormat, dataType, nullptr);
        else
            glTexImage3D(gl_TextureTarget(tex.get()), 0, format, traits.width_, traits.height_, std::max(traits.depth_, traits.layers_), 0, externalFormat, dataType, nullptr);
    }

    return tex;
}

//****************************************************************************
//
//  Function:   GraphicsDevice::UpdateTexture
//
//  Purpose:    Pumps texel-data into the given mip/layer. This function is only
//              intended for whole-level/layer updates.
//
//****************************************************************************
void GraphicsDevice::UpdateTexture(Texture* tex, void* data, uint32_t width, uint32_t height, uint32_t depth, uint32_t mip, uint32_t layer)
{
    assert(tex);
    if (tex)
    {
        const auto format = gl_InternalFormat[tex->traits_.format_];
        const auto externalFormat = gl_ExternalFormat(format);
        const auto dataType = gl_DataType(format);
        const auto compFormat = gl_CompressedFormat(tex->traits_.format_);

        if (tex->buffer_)
        {
            glBindBuffer(GL_TEXTURE_BUFFER, tex->buffer_);
            glBufferData(GL_TEXTURE_BUFFER, width, data, GL_STATIC_DRAW);
            glBindBuffer(GL_TEXTURE_BUFFER, 0);
        }
        else
        {
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(gl_TextureTarget(tex), tex->GetGPUObject());

            const uint32_t blockSize = (format == GL_COMPRESSED_RGBA_S3TC_DXT1_EXT || format == GL_COMPRESSED_RED_RGTC1) ? 8 : 16;
            const uint32_t tileSize = ((width + 3) / 4)*((height + 3) / 4) * blockSize;

            if (tex->GetTextureKind() == Texture2D)
            {
                if (tex->IsCompressed())
                {
                    glCompressedTexImage2D(gl_TextureTarget(tex), mip, format, width, height, 0, tileSize, data);
                    //glCompressedTexSubImage2D(gl_TextureTarget(tex), mip, 0, 0, width, height, format, totalSize, data);
                }
                else
                    glTexImage2D(gl_TextureTarget(tex), mip, format, width, height, 0, externalFormat, dataType, data);
            }
            else if (tex->GetTextureKind() == TextureCube)
            {
                if (tex->IsCompressed())
                    glCompressedTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + layer, mip, format, width, height, 0, tileSize, data);
                else
                    glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + layer, mip, format, width, height, 0, externalFormat, dataType, data);
            }
            else // Texture3D or Texture2D array
            {
                auto maxDim = std::max(depth, layer);
                if (tex->IsCompressed())
                    glCompressedTexImage3D(gl_TextureTarget(tex), mip, format, width, height, maxDim, 0, tileSize * maxDim, data);
                else
                    glTexImage3D(gl_TextureTarget(tex), mip, format, width, height, maxDim, 0, externalFormat, dataType, data);
            }
        }
    }
}

void GraphicsDevice::UpdateSubTexture(Texture* tex, void* data, uint32_t x, uint32_t y, uint32_t z, uint32_t width, uint32_t height, uint32_t depth, uint32_t mip, uint32_t layer)
{
    assert(tex);
    if (tex)
    {
        const auto format = gl_InternalFormat[tex->traits_.format_];
        const auto externalFormat = gl_ExternalFormat(format);
        const auto dataType = gl_DataType(format);

        if (tex->buffer_)
        {
            glBindBuffer(GL_TEXTURE_BUFFER, tex->buffer_);
            glBufferData(GL_TEXTURE_BUFFER, width, data, GL_STATIC_DRAW);
            glBindBuffer(GL_TEXTURE_BUFFER, 0);
        }
        else
        {
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(gl_TextureTarget(tex), tex->GetGPUObject());
            if (tex->GetTextureKind() == Texture2D)
            {
                if (tex->IsCompressed())
                    glCompressedTexSubImage2D(gl_TextureTarget(tex), mip, x, y, width, height, format, gl_GetBlockSize(externalFormat, width, height), data);
                else
                    glTexSubImage2D(gl_TextureTarget(tex), mip, x, y, width, height, externalFormat, dataType, data);
            }
            else if (tex->GetTextureKind() == TextureCube)
            {
                if (tex->IsCompressed())
                    glCompressedTexSubImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + layer, mip, x, y, width, height, format, gl_GetBlockSize(externalFormat, width, height), data);
                else
                    glTexSubImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + layer, mip, x, y, width, height, externalFormat, dataType, data);
            }
            else // Texture3D or Texture2D array
            {
                auto maxDim = std::max(depth, layer);
                if (tex->IsCompressed())
                    glCompressedTexSubImage3D(gl_TextureTarget(tex), mip, x, y, z, width, height, depth, format, gl_GetBlockSize(externalFormat, width, height) * depth, data);
                else
                    glTexSubImage3D(gl_TextureTarget(tex), mip, x, y, z, width, height, depth, externalFormat, dataType, data);
            }
        }
    }
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
    assert(textures.size());
    auto ret = shared_ptr<FrameBuffer>(new FrameBuffer(this, textures));
    glGenFramebuffers(1, &ret->fbo_);
    glBindFramebuffer(GL_FRAMEBUFFER, ret->fbo_);

    GLuint buffers[16] = { };
    int colCt = 0;
    for (uint32_t i = 0; i < textures.size(); ++i)
    {
        if (!IsDepth(textures[i]->GetFormat()))
        {
            glBindFramebuffer(GL_FRAMEBUFFER, ret->fbo_);
            glFramebufferTexture(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0 + colCt, textures[i]->GetGPUObject(), 0);
            buffers[i] = GL_COLOR_ATTACHMENT0 + colCt;
            ++colCt;
        }
        else
        {
            glBindFramebuffer(GL_FRAMEBUFFER, ret->fbo_);
            if (IsShadow(textures[i]->GetFormat()))
            {
                glFramebufferTexture(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, textures[i]->GetGPUObject(), 0);
                //buffers[i] == GL_DEPTH_ATTACHMENT;
                //++colCt;
            }
            else {
                glFramebufferTexture(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, textures[i]->GetGPUObject(), 0);
                //buffers[i] == GL_DEPTH_ATTACHMENT;
                //++colCt;
            }
        }
    }

    glBindFramebuffer(GL_FRAMEBUFFER, ret->fbo_);
    if (colCt > 0)
        glDrawBuffers(colCt, buffers);

    auto status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (status != GL_FRAMEBUFFER_COMPLETE)
        LogFormat(GLVU_ERROR, "Failed to create GL framebuffer: code %u", status);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
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
    auto ret = shared_ptr<Buffer>(new Buffer(this, VertexBufferObject));
    return ret;
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
    auto ret = shared_ptr<Buffer>(new Buffer(this, IndexBufferObject));
    return ret;
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
    auto ret = shared_ptr<Buffer>(new Buffer(this, UniformBufferObject));
    return ret;
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
    auto ret = shared_ptr<Buffer>(new Buffer(this, ShaderDataBufferObject));
    return ret;
}

//****************************************************************************
//
//  Function:   GraphicsDevice::UpdateBuffer
//
//  Purpose:    API filler, just delegates to buffer.
//
//****************************************************************************
void GraphicsDevice::UpdateBuffer(Buffer* buffer, void* data, uint32_t size)
{
    assert(buffer);
    if (buffer)
        buffer->SetData(data, size);
}

void GraphicsDevice::ExecuteCompute(const ComputeTask& task, bool block)
{
    if (graphicsFeatures_.compute_ == false)
    {
        LogMessage("Compute is unsupported on this device", GLVU_ERROR);
        return;
    }
    auto& state = GetGLState();
    for (auto& t : task.readTextures_)
    {
        if (state.SetTexture(t.slot_, t.texture_->GetGPUObject(), t.texture_->GetTarget()))
        {
            glActiveTexture(GL_TEXTURE0 + t.slot_);
            glBindTexture(t.texture_->GetTarget(), t.slot_);
            glBindSampler(t.slot_, GetSampler(t.sampling_.filter_, t.sampling_.wrap_));
        }
    }

    for (auto& t : task.constBuffers_)
        glBindBufferBase(GL_UNIFORM_BUFFER, t.slot_, t.buffer_->GetGPUObject());

    for (auto& t : task.writeTextures_)
    {
        if (state.TextureIsActive(t.texture_->texture_))
        {
            state.SetTexture(t.bindSlot_, t.texture_->texture_, t.texture_->GetTarget());
            glActiveTexture(GL_TEXTURE0 + t.bindSlot_);
            glBindTexture(t.texture_->GetTarget(), 0);
        }
        glBindImageTexture(t.bindSlot_, t.texture_->texture_, t.mip_, 
            t.layer_ != UINT_MAX, t.layer_ != UINT_MAX ? t.layer_ : 0, 
            GL_READ_WRITE, gl_ComputeFormat(t.texture_->GetFormat()));
    }

    for (auto& t : task.readBuffers_)
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, t.slot_, t.buffer_->GetGPUObject());

    for (auto& t : task.writeBuffers_)
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, t.slot_, t.buffer_->GetGPUObject());

    glUseProgram(task.computeProgram_->GetGPUObject());
    glDispatchCompute(task.dispatch_[0], task.dispatch_[1], task.dispatch_[2]);
    glUseProgram(0);

    if (block)
        glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT | GL_SHADER_STORAGE_BARRIER_BIT);

    for (auto t : task.writeTextures_)
        glBindImageTexture(t.bindSlot_, 0, 0, GL_FALSE, 0, 0, 0);
    for (auto t : task.writeBuffers_)
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, t.slot_, 0);
}

}