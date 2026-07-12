//****************************************************************************
//
//  File:       Material.cpp
//  License:    MIT
//  Project:    GLVU
//
//****************************************************************************

#include "Material.h"

#include "GraphicsDevice.h"
#include "Effect.h"
#include "Texture.h"

using namespace std;
using namespace math;

namespace GLVU
{

//****************************************************************************
//
//  Function:   Material::Material
//
//  Purpose:    Construct with a given effect and *apply* that effect,
//              pulling the information required for UBOs and textures.
//
//****************************************************************************
Material::Material(shared_ptr<Effect> effect) :
    effect_(effect)
{
    ApplyEffect();
}

//****************************************************************************
//
//  Function:   Material::Material
//
//  Purpose:    Copy-construct to create a matching duplicate.
//
//****************************************************************************
Material::Material(const Material& src) :
    effect_(src.effect_)
{
    ApplyEffect();
}

//****************************************************************************
//
//  Function:   Material::~Material
//
//  Purpose:    Destruct.
//
//****************************************************************************
Material::~Material()
{

}

//****************************************************************************
//
//  Function:   Material::ApplyEffect
//
//  Purpose:    Gets the meta-data from the effect and instantiates UBOs
//              as needed, loads default textures/UBOs, etc.
//
//              The natural behaviour is for only the the UBO in slot=0 to be used
//              as the material traits UBO, with other UBOs assumed to be system
//              properties.
//
//              Care must be taken to ApplyEffect() before any desired changes
//              are made to the material, otherwise those will be overwritten.
//
//****************************************************************************
void Material::ApplyEffect()
{
    assert(effect_);

    // wipe our UBOs
    uniformBuffers_.clear();

    // wipe our textures
    textures_.clear();

    auto device = effect_->GetDevice();

    // identify and construct our UBOs
    // the assumption is that all passes will use the same UBOs (though a pass may possibly not use all of them)
    for (uint32_t processing = 0; processing < 16; ++processing)
    {
        uint32_t maxSize = 0;
        for (auto pass : effect_->GetPasses())
        {
            if (auto ubo = pass->GetUBO(processing))
            {
                // Only take buffers at index-0|1 or prefixed with `user`, everything else will come from the renderer
                if (ubo->bindingIndex_ >= 1 && strncmp(ubo->name_, "user", 4) != 0)
                    continue;
                maxSize = std::max(maxSize, ubo->totalSize_);
            }
        }

        if (maxSize > 0)
        {
            auto ubo = device->CreateUniformBuffer();
            ubo->SetShadowed(true);
            ubo->SetSize(maxSize);
            uniformBuffers_.push_back({ processing, 0, UINT_MAX, ubo });
        }
    }

    // Go through all of the textures, only push slots for anything that is actually used.
    for (uint32_t processing = 0; processing < 16; ++processing)
    {
        bool used = false;
        for (auto pass : effect_->GetPasses())
        {
            if (auto tex = pass->GetTexInfo(processing))
            {
                used = true;
                break;
            }
        }
        if (used)
            textures_.push_back({ processing, { FILTER_LINEAR, true }, effect_->GetDefaultTexture(processing) });
    }
}

//****************************************************************************
//
//  Function:   Material::GetEffect
//
//  Purpose:    Getter function for utility.
//
//  Return:     the effect used
//
//****************************************************************************
shared_ptr<Effect> Material::GetEffect()
{
    return effect_;
}

//****************************************************************************
//
//  Function:   Material::SetShaderParameter
//
//  Purpose:    Uses the metadata in the material's effect to set just part of a UBO's data.
//
//              This function should only be used in cases where it touches UBOs owned by this material.
//
//****************************************************************************
void Material::SetShaderParameter(const char* name, uint2 vec)
{
    if (auto found = effect_->GetUBORecord(name))
    {
        if (auto ubo = GetUBO(found->blockIndex_))
            ubo->WriteIntoShadow(&vec, found->offset_, sizeof(vec));
    }
}

//****************************************************************************
//
//  Function:   Material::SetShaderParameter
//
//  Purpose:    Uses the metadata in the material's effect to set just part of a UBO's data.
//
//              This function should only be used in cases where it touches UBOs owned by this material.
//
//****************************************************************************
void Material::SetShaderParameter(const char* name, uint3 vec)
{
    if (auto found = effect_->GetUBORecord(name))
    {
        if (auto ubo = GetUBO(found->blockIndex_))
            ubo->WriteIntoShadow(&vec, found->offset_, sizeof(vec));
    }
}

//****************************************************************************
//
//  Function:   Material::SetShaderParameter
//
//  Purpose:    Uses the metadata in the material's effect to set just part of a UBO's data.
//
//              This function should only be used in cases where it touches UBOs owned by this material.
//
//****************************************************************************
void Material::SetShaderParameter(const char* name, uint4 vec)
{
    if (auto found = effect_->GetUBORecord(name))
    {
        if (auto ubo = GetUBO(found->blockIndex_))
            ubo->WriteIntoShadow(&vec, found->offset_, sizeof(vec));
    }
}

//****************************************************************************
//
//  Function:   Material::SetShaderParameter
//
//  Purpose:    Uses the metadata in the material's effect to set just part of a UBO's data.
//
//              This function should only be used in cases where it touches UBOs owned by this material.
//
//****************************************************************************
void Material::SetShaderParameter(const char* name, float2 vec)
{
    if (auto found = effect_->GetUBORecord(name))
    {
        if (auto ubo = GetUBO(found->blockIndex_))
            ubo->WriteIntoShadow(&vec, found->offset_, sizeof(vec));
    }
}

//****************************************************************************
//
//  Function:   Material::SetShaderParameter
//
//  Purpose:    Uses the metadata in the material's effect to set just part of a UBO's data.
//
//              This function should only be used in cases where it touches UBOs owned by this material.
//
//****************************************************************************
void Material::SetShaderParameter(const char* name, float3 vec)
{
    if (auto found = effect_->GetUBORecord(name))
    {
        if (auto ubo = GetUBO(found->blockIndex_))
            ubo->WriteIntoShadow(&vec, found->offset_, sizeof(vec));
    }
}

//****************************************************************************
//
//  Function:   Material::SetShaderParameter
//
//  Purpose:    Uses the metadata in the material's effect to set just part of a UBO's data.
//
//              This function should only be used in cases where it touches UBOs owned by this material.
//
//****************************************************************************
void Material::SetShaderParameter(const char* name, float4 vec)
{
    if (auto found = effect_->GetUBORecord(name))
    {
        if (auto ubo = GetUBO(found->blockIndex_))
            ubo->WriteIntoShadow(&vec, found->offset_, sizeof(vec));
    }
}

//****************************************************************************
//
//  Function:   Material::SetShaderParameter
//
//  Purpose:    Uses the metadata in the material's effect to set just part of a UBO's data.
//
//              This function should only be used in cases where it touches UBOs owned by this material.
//
//****************************************************************************
void Material::SetShaderParameter(const char* name, Quat vec)
{
    if (auto found = effect_->GetUBORecord(name))
    {
        if (auto ubo = GetUBO(found->blockIndex_))
            ubo->WriteIntoShadow(&vec, found->offset_, sizeof(vec));
    }
}

//****************************************************************************
//
//  Function:   Material::SetShaderParameter
//
//  Purpose:    Uses the metadata in the material's effect to set just part of a UBO's data.
//
//              This function should only be used in cases where it touches UBOs owned by this material.
//
//****************************************************************************
void Material::SetShaderParameter(const char* name, float4x4 vec)
{
    if (auto found = effect_->GetUBORecord(name))
    {
        if (auto ubo = GetUBO(found->blockIndex_))
            ubo->WriteIntoShadow(&vec, found->offset_, sizeof(vec));
    }
}

//****************************************************************************
//
//  Function:   Material::CommitUniforms
//
//  Purpose:    Checks for dirty UBOs and if found applies their shadow-data into
//              their GPU present object.
//
//****************************************************************************
void Material::CommitUniforms()
{
    for (size_t i = 0; i < uniformBuffers_.size(); ++i)
    {
        auto& ubo = uniformBuffers_[i];
        if (ubo.buffer_->IsShadowDirty())
            ubo.buffer_->ApplyShadowData();
    }
}

//****************************************************************************
//
//  Function:   Material::SetUBO
//
//  Purpose:    Replaces a UBO in this material, the UBO MUST exist.
//
//  Return:     True if there was a UBO to replace, false if not.
//
//****************************************************************************
bool Material::SetUBO(uint32_t slot, shared_ptr<Buffer> ubo)
{
    for (auto& rec : uniformBuffers_)
        if (rec.slot_ == slot)
        {
            rec.buffer_ = ubo;
            return true;
        }
    return false;
}

//****************************************************************************
//
//  Function:   Material::GetUBO
//
//  Purpose:    Check UBO records for a buffer at the given slot.
//
//  Return:     The buffer if found, null if not found.
//
//****************************************************************************
shared_ptr<Buffer> Material::GetUBO(uint32_t slot)
{
    for (auto& rec : uniformBuffers_)
        if (rec.slot_ == slot)
            return rec.buffer_;
    return nullptr;
}

//****************************************************************************
//
//  Function:   Material::SetTexture
//
//  Purpose:    Queries for the slot of a named texture and then sets the texture
//              if that slot was found.
//
//  Return:     True if success, false if failure (no texture found with that name)
//
//****************************************************************************
bool Material::SetTexture(const char* name, shared_ptr<Texture> tex)
{
    assert(effect_);
    auto idx = effect_->GetTextureSlot(name);
    assert(idx != UINT_MAX);
    if (idx != UINT_MAX)
    {
        SetTexture(idx, tex);
        return true;
    }
    return false;
}

//****************************************************************************
//
//  Function:   Material::SetTexture
//
//  Purpose:    Sets the texture at the given slot.
//
//  Return:     True if able to set that texture, false otherwise.
//
//****************************************************************************
bool Material::SetTexture(uint32_t slot, shared_ptr<Texture> tex)
{
    for (auto& rec : textures_)
        if (rec.slot_ == slot)
        {
            rec.texture_ = tex;
            return true;
        }
    return false;
}

//****************************************************************************
//
//  Function:   Material::GetTexture
//
//  Purpose:    Return a texture bound to this material.
//
//  Return:     The texture at the given binding-index or null.
//
//****************************************************************************
shared_ptr<Texture> Material::GetTexture(uint32_t slot)
{
    for (auto& rec : textures_)
        if (rec.slot_ == slot)
            return rec.texture_;
    return nullptr;
}

//****************************************************************************
//
//  Function:   Material::Clone
//
//  Purpose:    Constructs a copy of this material.
//
//  Return:     Copy of this material, referencing the same effect.
//
//****************************************************************************
std::shared_ptr<Material> Material::Clone() const
{
    auto mat = std::make_shared<Material>(effect_);
    
    mat->viewMask_ = viewMask_;
    mat->lightMask_ = lightMask_;
    mat->shadowMask_ = shadowMask_;
    mat->castShadows_ = castShadows_;
    mat->receiveShadows_ = receiveShadows_;
    mat->lit_ = lit_;

    mat->ApplyEffect();

    // Material may have textures *after* that are set after Effect application.
    mat->textures_ = textures_;

    return mat;
}

}