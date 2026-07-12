//****************************************************************************
//
//  File:       GLEffect.cpp
//  License:    MIT
//  Project:    GLVU
//  Contents:   Implementation of GL specific shader compilation and other
//              shader FX needs.
//
//****************************************************************************

#include "Effect.h"

#include "GraphicsDevice.h"

#include "GLHelpers.h"

using namespace std;

namespace GLVU
{

//****************************************************************************
//
//  Function:   gl_ShaderTypeID
//
//  Purpose:    Maps a ShaderType to the GL stage target.
//
//****************************************************************************
GLenum gl_ShaderTypeID(ShaderType t)
{
    switch (t)
    {
    case VertexShader:
        return GL_VERTEX_SHADER;
    case PixelShader:
        return GL_FRAGMENT_SHADER;
    case GeometryShader:
        return GL_GEOMETRY_SHADER;
    case HullShader:
        return GL_TESS_CONTROL_SHADER;
    case DomainShader:
        return GL_TESS_EVALUATION_SHADER;
    }
    return GL_COMPUTE_SHADER;
}

//****************************************************************************
//
//  Function:   Shader::Shader
//
//  Purpose:    Construct, zero-init and setup code/defs
//
//****************************************************************************
Shader::Shader(GraphicsDevice* device, const string& name, ShaderType kind, ShaderCodeType codeType, const string& code, const vector<string>& defines) :
    GPUObject(device),
    kind_(kind),
    codeType_(codeType),
    code_(code),
    shader_(0),
    name_(name),
    defines_(defines)
{
}

//****************************************************************************
//
//  Function:   Shader::~Shader
//
//  Purpose:    Destruct
//
//****************************************************************************
Shader::~Shader()
{
    Release();
}

//****************************************************************************
//
//  Function:   Shader::Release
//
//  Purpose:    Destroy shader and wipe memory used by code/defines.
//
//****************************************************************************
void Shader::Release()
{
    if (shader_)
        glDeleteShader(shader_);
    shader_ = 0;
    code_.clear();
    defines_.clear();
}

//****************************************************************************
//
//  Function:   Shader::IsValid
//
//  Purpose:    Utility
//
//  Return:     True if this shader is probably good.
//
//****************************************************************************
bool Shader::IsValid() const
{
    return shader_ != 0;
}

//****************************************************************************
//
//  Function:   Shader::Compile
//
//  Purpose:    Attempts to compile the shader, logging GL compiler output.
//
//  Return:     True if success, read log for errors.
//
//****************************************************************************
bool Shader::Compile()
{
    shader_ = glCreateShader(gl_ShaderTypeID(kind_));
    int len = code_.length();
    const GLchar* data = code_.c_str();
    glShaderSource(shader_, 1, &data, &len);
    glCompileShader(shader_);

    int compiled;
    glGetShaderiv(shader_, GL_COMPILE_STATUS, &compiled);

    if (!compiled)
    {
        int length;
        glGetShaderiv(shader_, GL_INFO_LOG_LENGTH, &length);
        if (length > 0)
        {
            vector<char> msgData(length + 1);
            memset(msgData.data(), 0, length + 1);
            glGetShaderInfoLog(shader_, length, &length, msgData.data());
            device_->LogFormat(GLVU_ERROR, "%s: shader compile failure: %s", name_.c_str(), msgData.data());
        }

        Release();
        return false;
    }

    return true;
}

//****************************************************************************
//
//  Function:   ShaderPass::IsValid
//
//  Purpose:    Utility
//
//  Return:     True if the shader-program for the pass is probably healthy.
//
//****************************************************************************
bool ShaderPass::IsValid() const
{
    return shaderProgram_ != 0;
}

//****************************************************************************
//
//  Function:   ShaderPass::Release
//
//  Purpose:    Delete the program and zero-reinit
//
//****************************************************************************
void ShaderPass::Release()
{
    if (shaderProgram_)
        glDeleteProgram(shaderProgram_);
    shaderProgram_ = 0;
}

//****************************************************************************
//
//  Function:   ShaderPass::Link
//
//  Purpose:    Overload for compute shader ... it's really a stupid GL
//              fossil thing. Errors/warnings are logged.
//
//  Return:     True if successfully linked.
//
//****************************************************************************
bool ShaderPass::Link(shared_ptr<Shader> computeShader)
{
    assert(computeShader && computeShader->GetStage() == ComputeShader);
    if (!computeShader)
    {
        device_->LogMessage("Provided invalid compute shader to ShaderPass::Link", 2);
        return false;
    }
    if (computeShader->GetStage() != ComputeShader)
    {
        device_->LogMessage("Attempted to link a compute program with a shader that isn't a compute shader in ShaderPass::Link", 2);
        return false;
    }

    shaderProgram_ = glCreateProgram();
    glAttachShader(shaderProgram_, computeShader->GetGPUObject());
    cs_ = computeShader;
    glLinkProgram(shaderProgram_);

    int linked;
    glGetProgramiv(shaderProgram_, GL_LINK_STATUS, &linked);
    if (!linked)
    {
        int length, outLength;
        glGetProgramiv(shaderProgram_, GL_INFO_LOG_LENGTH, &length);

        if (length > 0)
        {
            vector<char> str(length);
            glGetProgramInfoLog(shaderProgram_, length, &outLength, str.data());
            device_->LogFormat(GLVU_ERROR, "Shader link failure: %s", str.data());
        }
        else
            device_->LogMessage("Shader link failure: unknown cause", GLVU_ERROR);

        Release();
        return false;
    }
    else
        BuildReflection();

    return true;
}

//****************************************************************************
//
//  Function:   ShaderPass::Link
//
//  Purpose:    Links together the given shader-programs into a complete program,
//              logging any warnings/errors.
//
//  Return:     True if link was a success.
//
//****************************************************************************
bool ShaderPass::Link(shared_ptr<Shader> vs, shared_ptr<Shader> ps, shared_ptr<Shader> gs, shared_ptr<Shader> hs, shared_ptr<Shader> ds)
{
    // for the moment it's always required that there be a VS and PS (stream-out support will change this)
    // but will likely use a different path
    assert(vs && ps && vs->IsValid() && ps->IsValid());
    if (!vs || !ps || !vs->IsValid() || !ps->IsValid())
    {
        device_->LogMessage("Provided invalid shader to ShaderPass::Link", 2);
        return false;
    }

    if (shaderProgram_)
        return true;

    shaderProgram_ = glCreateProgram();

#define ATTACH(SHADER) if (SHADER) glAttachShader(shaderProgram_, SHADER->GetGPUObject())
    ATTACH(vs);
    ATTACH(ps);
    ATTACH(gs);
    ATTACH(hs);
    ATTACH(ds);
#undef ATTACH

    glLinkProgram(shaderProgram_);

    int linked;
    glGetProgramiv(shaderProgram_, GL_LINK_STATUS, &linked);
    if (!linked)
    {
        int length, outLength;
        glGetProgramiv(shaderProgram_, GL_INFO_LOG_LENGTH, &length);

        if (length > 0)
        {
            vector<char> str(length);
            glGetProgramInfoLog(shaderProgram_, length, &outLength, str.data());
            device_->LogFormat(GLVU_ERROR, "Shader link failure: %s", str.data());
        }
        else
            device_->LogMessage("Shader link failure: unknown cause", GLVU_ERROR);

        Release();
        return false;
    }
    else
        BuildReflection();

    vs_ = vs;
    ps_ = ps;
    gs_ = gs;
    hs_ = hs;
    ds_ = ds;

    return true;
}

//****************************************************************************
//
//  Function:   ShaderPass::BuildReflection
//
//  Purpose:    Populates meta-data using GL's reflection API.
//
//****************************************************************************
void ShaderPass::BuildReflection()
{
    uniformBuffers_.clear();

    const int MAX_NAME_LENGTH = 128;
    char nameBuffer[MAX_NAME_LENGTH];

    if (IsCompute())
    {
        GLint dispatch[3];
        glGetProgramiv(shaderProgram_, GL_COMPUTE_WORK_GROUP_SIZE, dispatch);
        cs_->dispatchGroupSize_ = math::uint3(dispatch[0], dispatch[1], dispatch[2]);
    }

    int numUniformBlocks = 0;
    int nameLength = 0;
    glGetProgramiv(shaderProgram_, GL_ACTIVE_UNIFORM_BLOCKS, &numUniformBlocks);
    for (int i = 0; i < numUniformBlocks; ++i)
    {
        UBOInfo ubo;

        glGetActiveUniformBlockName(shaderProgram_, (GLuint)i, MAX_NAME_LENGTH, &nameLength, nameBuffer);

        memset(ubo.name_, 0, 128);
        strcpy_s(ubo.name_, nameBuffer);

        ubo.bindingIndex_ = glGetUniformBlockIndex(shaderProgram_, ubo.name_);

        int dataSize;
        glGetActiveUniformBlockiv(shaderProgram_, ubo.bindingIndex_, GL_UNIFORM_BLOCK_DATA_SIZE, &dataSize);
        ubo.totalSize_ = dataSize;

        uniformBuffers_.push_back(ubo);
    }

    int elementCount = 0;
    GLenum type;
    int uniformCount = 0;
    glGetProgramiv(shaderProgram_, GL_ACTIVE_UNIFORMS, &uniformCount);
    for (int i = 0; i < uniformCount; ++i)
    {
        glGetActiveUniform(shaderProgram_, (GLuint)i, MAX_NAME_LENGTH, nullptr, &elementCount, &type, nameBuffer);
        int location = glGetUniformLocation(shaderProgram_, nameBuffer);

        if (location < 0)
        {
            UBORecord rec;
            memset(rec.name_, 0, 128);
            strcpy_s(rec.name_, nameBuffer);

            int blockIndex, blockOffset;
            glGetActiveUniformsiv(shaderProgram_, 1, (const GLuint*)&i, GL_UNIFORM_BLOCK_INDEX, &blockIndex);
            glGetActiveUniformsiv(shaderProgram_, 1, (const GLuint*)&i, GL_UNIFORM_OFFSET, &blockOffset);
            rec.offset_ = blockOffset;
            rec.blockIndex_ = blockIndex;

            GetUBO(blockIndex)->records_.push_back(rec);
        }
        else if (type == GL_SAMPLER_2D || type == GL_SAMPLER_3D || type == GL_SAMPLER_2D_ARRAY || type == GL_SAMPLER_CUBE || type == GL_SAMPLER_2D_SHADOW)
        {
            // only allowing samplers as loose uniforms
            TexInfo t;
            t.blockIndex_ = location;
            strcpy_s(t.name_, nameBuffer);
            textures_.push_back(t);
        }
    }
}

//****************************************************************************
//
//  Function:   Effect::BindTexture
//
//  Purpose:    Hides the boiler plate of binding a texture and setting up the
//              sampler-object - including the validity checks.
//
//****************************************************************************
extern GLenum gl_TextureTarget(const Texture* tex);
extern GLenum gl_GetInternalFormat(TextureFormat fmt);
void Effect::BindTexture(shared_ptr<Texture> tex, uint32_t slot, ShaderPass* pass)
{
    assert(tex && tex->IsValid());
    if (tex && tex->IsValid())
    {
        // apply to our state information, but 
        auto& glState = device_->GetGLState();
        const bool needsBind = glState.SetTexture(slot, tex->GetGPUObject(), tex->GetTarget());
        if (!needsBind)
            return;

        glActiveTexture(GL_TEXTURE0 + slot);
        glBindTexture(tex->GetTarget(), tex->GetGPUObject());

        if (tex->IsTextureBuffer())
        {
            glTexBuffer(GL_TEXTURE_BUFFER, gl_GetInternalFormat(tex->GetFormat()), tex->GetTextureBuffer());
        }
        else
        {
            bool boundSampler = false;
            for (auto& s : samplers_)
            {
                if (s.first == slot)
                {
                    auto sampler = device_->GetSampler(s.second.filter_, s.second.wrap_);
                    //if (glState.SetSampler(slot, sampler))
                        glBindSampler(slot, sampler);
                    boundSampler = true;
                    break;
                }
            }
            if (!boundSampler)
            {
                if (IsShadow(tex->GetFormat()))
                {
                    auto sampler = device_->GetSampler(FILTER_SHADOW, false);
                    //if (glState.SetSampler(slot, sampler))
                        glBindSampler(slot, sampler);
                }
                else
                {
                    auto sampler = device_->GetSampler(FILTER_POINT, true);
                    //if (glState.SetSampler(slot, sampler))
                        glBindSampler(slot, sampler);
                }
            }
        }
    }
}

}