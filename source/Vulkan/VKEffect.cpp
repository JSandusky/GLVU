#include "Effect.h"

#include "GraphicsDevice.h"

using namespace std;

namespace GLVU
{

//****************************************************************************
//
//  Function:   Shader::Shader
//
//  Purpose:    Construct, zero-init and setup simple state for the code and
//              preprocessor definitions
//
//****************************************************************************
Shader::Shader(GraphicsDevice* device, const string& name, ShaderType type, ShaderCodeType codeType, const string& code, const vector<string>& defs) :
    GPUObject(device),
    kind_(type),
    codeType_(codeType),
    shader_(0),
    name_(name),
    code_(code),
    defines_(defs)
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
//  Purpose:    Destroys the shader-module
//
//****************************************************************************
void Shader::Release()
{
    code_.clear();
    if (shader_)
        vezDestroyShaderModule(device_->GetVKDevice(), shader_);
    shader_ = 0;
}

//****************************************************************************
//
//  Function:   Shader::IsValid
//
//  Purpose:    Utility
//
//  Return:     True if probably valid.
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
//  Purpose:    Compiles a GLSL shader-modules into SPIR-V and then an actual
//              vulkan shader via VEZ.
//
//  Return:     True if success, false if failed - see log for errors
//
//****************************************************************************
bool Shader::Compile()
{
    // are we already compiled?
    if (shader_ != 0)
        return true;

    VezShaderModuleCreateInfo createInfo = {};

    static const VkShaderStageFlagBits stageTable[] = {
        VK_SHADER_STAGE_VERTEX_BIT,
        VK_SHADER_STAGE_FRAGMENT_BIT,
        VK_SHADER_STAGE_GEOMETRY_BIT,
        VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT,
        VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT,
        VK_SHADER_STAGE_COMPUTE_BIT
    };

    static const char* entryPoints[] = {
        "main",
        "main",
        "main",
        "main",
        "main",
        "main"
    };

    createInfo.stage = stageTable[kind_];
    createInfo.codeSize = code_.length();
    //if (codeType_ == SCT_SPIRV)
    //    createInfo.pCode = (uint32_t*)data_.get();
    //else
    createInfo.pGLSLSource = code_.c_str();
    createInfo.pEntryPoint = entryPoints[kind_];

    VkResult result = vezCreateShaderModule(device_->GetVKDevice(), &createInfo, &shader_);
    if (result != VK_SUCCESS)
    {
        // If shader module creation failed but error is from GLSL compilation, get the error log.
        uint32_t infoLogSize = 0;
        vezGetShaderModuleInfoLog(shader_, &infoLogSize, nullptr);

        string infoLog(infoLogSize, '\0');
        vezGetShaderModuleInfoLog(shader_, &infoLogSize, &infoLog[0]);

        vezDestroyShaderModule(device_->GetVKDevice(), shader_);
        shader_ = 0;

        device_->LogFormat(2, "%s: %s", name_.c_str(), infoLog.c_str());
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
//  Return:     True if probably valid.
//
//****************************************************************************
bool ShaderPass::IsValid() const
{
    return pipeline_ != 0;
}

//****************************************************************************
//
//  Function:   ShaderPass::Release
//
//  Purpose:    Release the pipeline resource if needed.
//
//****************************************************************************
void ShaderPass::Release()
{
    if (pipeline_)
        vezDestroyPipeline(device_->GetVKDevice(), pipeline_);
    pipeline_ = 0;
}

//****************************************************************************
//
//  Function:   ShaderPass::Link
//
//  Purpose:    Links a compute-shader, which while nonsense in GL is pipeline
//              nonsense in Vulkan. Any errors/warnings are logged.
//
//  Return:     True if success, false if there was an error.
//
//****************************************************************************
bool ShaderPass::Link(shared_ptr<Shader> computeShader)
{
    if (!computeShader || !computeShader->IsValid())
    {
        device_->LogMessage("Provided invalid compute shader to ShaderPass::Link", 2);
        return false;
    }

    if (pipeline_)
        return true;

    VezComputePipelineCreateInfo pipeInfo = {};        

    VezPipelineShaderStageCreateInfo computeStage = { };
    computeStage.module = computeShader->GetGPUObject();
    computeStage.pEntryPoint = "main";
    pipeInfo.pStage = &computeStage;

    VkResult result = vezCreateComputePipeline(device_->GetVKDevice(), &pipeInfo, &pipeline_);

    if (result != VK_SUCCESS)
    {
        if (pipeline_)
            vezDestroyPipeline(device_->GetVKDevice(), pipeline_);
        return false;
    }

    BuildReflection();

    cs_ = computeShader;

    return true;
}

//****************************************************************************
//
//  Function:   ShaderPass::Link
//
//  Purpose:    Links the given programs into a pipeline object, logging encountered errors.
//
//  Return:     True if success, false if failed.
//
//****************************************************************************
bool ShaderPass::Link(shared_ptr<Shader> vs, shared_ptr<Shader> ps, shared_ptr<Shader> gs, shared_ptr<Shader> hs, shared_ptr<Shader> ds)
{
    // note: compute shaders will use a different path.
    //assert(vs && ps && vs->IsValid() && ps->IsValid());
    if (!vs || !ps || !vs->IsValid() || !ps->IsValid())
    {
        device_->LogMessage("Provided invalid shader to ShaderPass::Link", 2);
        return false;
    }

    if (pipeline_)
        return true;

    VezGraphicsPipelineCreateInfo pipeInfo = {};

    vector<shared_ptr<Shader>> shaders;
#define ADD(NAME) if (NAME) shaders.push_back(NAME);
    ADD(vs);
    ADD(ps);
    ADD(gs);
    ADD(hs);
    ADD(ds);
#undef ADD

    vector<VezPipelineShaderStageCreateInfo> stages;
    pipeInfo.stageCount = (uint32_t)shaders.size();
    for (auto& shader : shaders)
    {
        VezPipelineShaderStageCreateInfo stage = {};
        stage.module = shader->GetGPUObject();
        stage.pEntryPoint = "main";
        stages.push_back(stage);
    }
    pipeInfo.pStages = stages.data();

    VkResult result = vezCreateGraphicsPipeline(device_->GetVKDevice(), &pipeInfo, &pipeline_);

    if (result != VK_SUCCESS)
    {
        if (pipeline_)
            vezDestroyPipeline(device_->GetVKDevice(), pipeline_);
        return false;
    }

#define SET_SHADER(NAME) NAME ## _ = NAME;
    SET_SHADER(vs);
    SET_SHADER(ps);
    SET_SHADER(gs);
    SET_SHADER(hs);
    SET_SHADER(ds);
#undef SET_SHADER

    BuildReflection();

    return true;
}

//****************************************************************************
//
//  Function:   ShaderPass::BuildReflection
//
//  Purpose:    Build metadata from VEZ shader reflection. Only a limited
//              subset of the possibilities is actually supported.
//
//****************************************************************************
void ShaderPass::BuildReflection()
{
    assert(pipeline_);
    if (pipeline_ == 0)
        return;

    uint32_t resourceCount = 0;
    vezEnumeratePipelineResources(pipeline_, &resourceCount, nullptr);

    vector<VezPipelineResource> resources(resourceCount);
    vezEnumeratePipelineResources(pipeline_, &resourceCount, resources.data());

    for (const auto& resource : resources)
    {
        if (resource.resourceType == VEZ_PIPELINE_RESOURCE_TYPE_UNIFORM_BUFFER)
        {
            UBOInfo info;
            strcpy_s(info.name_, resource.name);
            info.totalSize_ = resource.size;
            info.bindingIndex_ = resource.binding;
            if (resource.pMembers)
            {
                auto member = resource.pMembers;
                while (member)
                {
                    UBORecord rec;
                    strcpy_s(rec.name_, member->name);
                    rec.blockIndex_ = info.bindingIndex_;
                    rec.offset_ = member->offset;
                    info.records_.push_back(rec);
                    member = member->pNext;
                }
            }
            uniformBuffers_.push_back(info);
        }
        else if (resource.resourceType == VEZ_PIPELINE_RESOURCE_TYPE_COMBINED_IMAGE_SAMPLER)
        {
            device_->LogFormat(GLVU_INFO, "Sampler %u:%u %s", resource.set, resource.binding, resource.name);
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
void Effect::BindTexture(std::shared_ptr<Texture> tex, uint32_t slot, ShaderPass* pass)
{
    assert(tex && tex->IsValid());
    if (tex && tex->IsValid())
    {
        if (tex->IsTextureBuffer())
            vezCmdBindBufferView(tex->GetTextureBufferView(), 1, slot, 0);
        else
        {
            bool boundSampler = false;
            for (auto& s : samplers_)
            {
                if (s.first == slot)
                {
                    vezCmdBindImageView(tex->GetView(), device_->GetSampler(s.second.filter_, s.second.wrap_), 1, slot, 0);
                    boundSampler = true;
                    break;
                }
            }

            if (!boundSampler)
            {
                if (IsShadow(tex->GetFormat()))
                    vezCmdBindImageView(tex->GetView(), device_->GetSampler(FILTER_SHADOW, false), 1, slot, 0);
                else
                    vezCmdBindImageView(tex->GetView(), device_->GetSampler(FILTER_POINT, true), 1, slot, 0);
            }
        }
    }
}

}