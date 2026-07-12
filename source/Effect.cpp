//****************************************************************************
//
//  File:       Effect.cpp
//  License:    MIT
//  Project:    GLVU
//
//****************************************************************************

#include "Effect.h"

#include "GraphicsDevice.h"

using namespace std;

namespace GLVU
{

//****************************************************************************
//
//  Function:   ShaderPass::ShaderPass
//
//  Purpose:    Construct, sets up identifiers and their precomputed hashes.
//
//****************************************************************************
ShaderPass::ShaderPass(GraphicsDevice* device, const char* name, PrimitiveType forPrim) : 
    GPUObject(device),
    forPrim_(forPrim)
#if defined(GLVU_VK)
    , pipeline_(0)
#elif defined (GLVU_GL) || defined(GLVU_GLES3)
    , shaderProgram_(0)
#elif defined(GLVU_DX11)
#elif defined(GLVU_DX12)
#endif
{
    identifier_ = MakeID(name);
}

//****************************************************************************
//
//  Function:   ShaderPass::~ShaderPass
//
//  Purpose:    Destruct, release owned resources.
//
//****************************************************************************
ShaderPass::~ShaderPass()
{
    Release();
}

//****************************************************************************
//
//  Function:   ShaderPass::GetUBO
//
//  Purpose:    Gets meta-data by binding-index.
//
//  Return:     UBO meta-data if we have any.
//
//****************************************************************************
UBOInfo* ShaderPass::GetUBO(uint32_t blockIndex)
{
    for (auto& ubo : uniformBuffers_)
        if (ubo.bindingIndex_ == blockIndex)
            return &ubo;
    return nullptr;
}

//****************************************************************************
//
//  Function:   ShaderPass::GetUBO
//
//  Purpose:    Tries to get meta-data by an identifying name. High risk.
//
//  Return:     UBO meta-data if we have any
//
//****************************************************************************
UBOInfo* ShaderPass::GetUBO(const char* name)
{
    for (auto& ubo : uniformBuffers_)
        if (strcmp(ubo.name_, name) == 0)
            return &ubo;
    return nullptr;
}

//****************************************************************************
//
//  Function:   ShaderPass:GetTexInfo
//
//  Purpose:    Gets texture metadata by binding-index.
//
//  Return:     Texture metadata if found.
//
//****************************************************************************
TexInfo* ShaderPass::GetTexInfo(uint32_t blockIndex)
{
    for (auto& tex : textures_)
        if (tex.blockIndex_ == blockIndex)
            return &tex;
    return nullptr;
}

//****************************************************************************
//
//  Function:   ShaderPass:GetTexInfo
//
//  Purpose:    Tries to get texture meta by identifier.
//
//  Return:     Texture meta if found.
//
//****************************************************************************
TexInfo* ShaderPass::GetTexInfo(const char* name)
{
    for (auto& tex : textures_)
        if (strcmp(tex.name_, name) == 0)
            return &tex;
    return nullptr;
}

//****************************************************************************
//
//  Function:   Effect::~Effect
//
//  Purpose:    Destruct and release resources.
//
//****************************************************************************
void Effect::Release()
{
    passes_.clear();
}

//****************************************************************************
//
//  Function:   Effect::GetPass
//
//  Purpose:    Finds a pass by the hash value of its string ID and for a given
//              primitive if possible. Primitive type is mostly optional, when
//              used it allows a `context` to be overloaded based on the primitive
//              to be drawn. This is very very useful for simplifying how to use
//              an effect from a RenderScript. Though in practice the value
//              is mostly about Debug-geometry rendering and special FX ... whoop whoop.
//
//  Return:     ShaderPass found, or null.
//
//****************************************************************************
shared_ptr<ShaderPass> Effect::GetPass(uint32_t contextHash, PrimitiveType forPrim) const
{
    for (auto pass : passes_)
    {
        if (pass->GetNameHash() == contextHash && (forPrim == PRIM_UNKNOWN || pass->GetPrimitive() == PRIM_UNKNOWN || pass->GetPrimitive() == forPrim))
            return pass;
    }
    return nullptr;
}

//****************************************************************************
//
//  Function:   Effect::GetPass
//
//  Purpose:    Tries to find a pass by the string ID. First hashes the string
//              then tries to find it by that hash instead of strcmp'ing a ton.
//
//  Return:     ShaderPass found, or null.
//
//****************************************************************************
shared_ptr<ShaderPass> Effect::GetPass(const char* name, PrimitiveType forPrim) const
{
    return GetPass(Hash(name), forPrim);
}

//****************************************************************************
//
//  Function:   Effect::AddPass
//
//  Purpose:    Adds the given pass to this one, naively pumps UBO and texture
//              info into this effect.
//
//****************************************************************************
void Effect::AddPass(shared_ptr<ShaderPass> pass)
{
    passes_.push_back(pass);

    for (uint32_t slot = 0; slot < 16; ++slot)
    {
        if (auto ubo = pass->GetUBO(slot))
            uboFields_.insert(uboFields_.end(), ubo->records_.begin(), ubo->records_.end());
    }

    for (uint32_t slot = 0; slot < 16; ++slot)
    {
        if (auto tex = pass->GetTexInfo(slot))
            usedTextureSlots_.push_back({ tex->blockIndex_, tex->name_ });
    }
}

//****************************************************************************
//
//  Function:   Effect::GetUBORecord
//
//  Purpose:    Looks for the binding/offset info on a named member of any UBO.
//
//  Return:     Metadata found, or null
//
//****************************************************************************
UBORecord* Effect::GetUBORecord(const char* name)
{
	auto foundAlias = aliasNames_.find(name);
	auto key = foundAlias != aliasNames_.end() ? foundAlias->second : name;

	for (auto& rec : uboFields_)
		if (key == rec.name_)
			return &rec;
	return nullptr;

    //OLD for (auto& rec : uboFields_)
    //OLD     if (strncmp(rec.name_, name, 128) == 0)
    //OLD         return &rec;
    //OLD return nullptr;
}

//****************************************************************************
//
//  Function:   Effect::GetTextureSlot
//
//  Purpose:    Try to find where a named-texture is bound to. Bindings
//              are not unique between stages, while can easily auto-assign
//              them, in Vulkan this gets scary as resources use the same
//              index-spaces and require sets to differentiate them.
//				It handily deals with alias' as well for convenience.
//
//  Return:     The binding index, or UINT_MAX
//
//  WARNING:    Blindly using the results of this function is HIGH RISK.
//
//****************************************************************************
uint32_t Effect::GetTextureSlot(const char* name)
{
    auto foundAlias = aliasNames_.find(name);
    auto key = foundAlias != aliasNames_.end() ? foundAlias->second : name;
    for (auto& tex : usedTextureSlots_)
    {
        if (tex.second == key)
            return tex.first;
    }
    return UINT_MAX;
}

//****************************************************************************
//
//  Function:   Effect::GetSamplerTraits
//
//  Purpose:    Return the sampler traits for a given index. The default
//              is to return wrapped+point (nearest-neighbor) filtering.
//
//  Return:     The sampler traits, or a sensible default.
//
//****************************************************************************
SamplerTraits Effect::GetSamplerTraits(uint32_t slot)
{
    for (auto rec : samplers_)
        if (rec.first == slot)
            return rec.second;

	const auto& deviceDefaults = device_->GetDefaults();
    return { deviceDefaults.textureFilter_, deviceDefaults .textureWrap_ };
}

//****************************************************************************
//
//  Function:   Effect::GetDefaultTexture
//
//  Purpose:    Checks a binding-index for a default texture that the effect
//              might have specified for use if the material doesn't provide one.
//              The use of default textures triggers texture-loads while an
//              Effect is being read from data.
//
//  Return:     A texture, or null
//
//****************************************************************************
shared_ptr<Texture> Effect::GetDefaultTexture(uint32_t slot) const
{
    for (auto rec : defaultTextures_)
        if (rec.first == slot)
            return rec.second;
    return nullptr;
}

//****************************************************************************
//
//  Function:   ShaderPass::GetVariation
//
//  Purpose:    Uses this ShaderPass's information and appends new #preprocess
//              definitions to it in order to compile a new shader with different
//              parameters. This process goes through the cache's in GraphicsDevice,
//              thus avoiding repeated loads of shader-sources from the file-system.
//
//  Return:     A new shader-pass or null if there was an error.
//
//****************************************************************************
shared_ptr<ShaderPass> ShaderPass::GetVariation(const string& name, const vector<string>& defines) const
{
    shared_ptr<Shader> shaders[5];
    shared_ptr<Shader> myShaders[5] = { vs_, ps_, gs_, hs_, ds_ };
        
    for (int i = 0; i < 5; ++i)
    {
        if (myShaders[i])
        {
            vector<string> defs = myShaders[i]->GetDefines();
            defs.insert(defs.end(), defines.begin(), defines.end());
            shaders[i] = device_->GetShader((ShaderType)i, myShaders[i]->GetName().c_str(), defs);
            if (shaders[i])
                shaders[i]->Compile();
        }
    }
        
    shared_ptr<ShaderPass> ret(new ShaderPass(device_, name.c_str(), forPrim_));
	ret->drawState_ = drawState_;

    if (ret->Link(shaders[0], shaders[1], shaders[2], shaders[3], shaders[4]))
        return ret;
    else
        device_->LogFormat(GLVU_ERROR, "ShaderPass::GetVariation, failure on %s", name.c_str());

    return nullptr;
}

}