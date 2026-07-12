//****************************************************************************
//
//  File:       DX11Texture.coo
//  License:    MIT
//  Project:    GLVU
//  Contents:   Texture implementation.
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
    texture_(nullptr),
    srv_(nullptr)
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
    if (srv_)
        ((ID3D11Resource*)srv_)->Release();
    if (texture_)
        ((ID3D11Resource*)texture_)->Release();

	for (auto uav : uavs_)
		if (uav.uav_)
			uav.uav_->Release();

	uavs_.clear();
    srv_ = nullptr;
    texture_ = nullptr;
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
    return texture_ != nullptr; 
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
//  Function:   Texture::GenerateMipMaps
//
//  Purpose:    For a texture with incomplete mip-maps this function will
//              generate the mipmaps.
//
//****************************************************************************
void Texture::GenerateMipMaps()
{
    assert(srv_);
    if (!IsValid() && GetMips() > 0)
        return;

    GetDevice()->GetD3DContext()->GenerateMips((ID3D11ShaderResourceView*)srv_);

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
//  Function:   Texture::GetUAV
//
//  Purpose:    Returns a UAV for the given layer and mip level. If the UAV
//              does not exist then it will be created.
//
//  Notes:      Use UINT_MAX for layer to bind all layers of a cube/array.
//
//****************************************************************************
extern DXGI_FORMAT dx_GetInternalFormat(TextureFormat fmt);
ID3D11UnorderedAccessView* Texture::GetUAV(uint32_t layer, uint32_t mip)
{
    if (!IsComputeWriteable(GetFormat()))
        return nullptr;

	for (auto& uav : uavs_)
		if (uav.layer_ == layer && uav.mip_ == mip)
			return uav.uav_;

	D3D11_UNORDERED_ACCESS_VIEW_DESC viewDesc;
	ZeroMemory(&viewDesc, sizeof(viewDesc));
	viewDesc.Format = dx_GetInternalFormat(GetFormat());
	if (GetTextureKind() == Texture2D)
	{
		viewDesc.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D;
		viewDesc.Texture2D.MipSlice = mip;
	}
	else if (GetTextureKind() == Texture2DArray)
	{
		viewDesc.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2DARRAY;
		viewDesc.Texture2DArray.FirstArraySlice = layer == UINT_MAX ? 0 : layer;
		viewDesc.Texture2DArray.ArraySize = layer == UINT_MAX ? GetLayers() : 1;
        viewDesc.Texture2DArray.MipSlice = mip;
	}
	else if (GetTextureKind() == TextureCube)
	{
		viewDesc.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2DARRAY;
		viewDesc.Texture2DArray.FirstArraySlice = layer == UINT_MAX ? 0 : layer;
		viewDesc.Texture2DArray.ArraySize = layer == UINT_MAX ? 6 : 1;
		viewDesc.Texture2DArray.MipSlice = mip;
	}
	else if (GetTextureKind() == Texture3D)
	{
		viewDesc.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE3D;
		viewDesc.Texture3D.FirstWSlice = 0;
		viewDesc.Texture3D.WSize = GetDepth();
		viewDesc.Texture3D.MipSlice = mip;
	}

	ID3D11UnorderedAccessView* uav = nullptr;
	auto hr = device_->GetD3DDevice()->CreateUnorderedAccessView(texture_, &viewDesc, &uav);
	if (FAILED(hr))
	{
		device_->LogFormat(GLVU_ERROR, "Failed to create UAV: %X", hr);
		return nullptr;
	}

	uavs_.push_back({ uav, layer, mip });
	return uav;
}

//****************************************************************************
//
//  Function:   Texture::Resolve
//
//  Purpose:    Resolves MSAA main texture into it's resolve target.
//
//****************************************************************************
void Texture::Resolve() const
{
    auto ctx = device_->GetD3DContext();
    if (!ctx || !resolveTexture_)
        return;

    if (traits_.kind_ == Texture2D || traits_.kind_ == Texture3D)
        ctx->ResolveSubresource(resolveTexture_->texture_, 0, texture_, 0, dx_GetInternalFormat(traits_.format_));
    else if (traits_.kind_ == TextureCube)
    {
        for (int i = 0; i < 6; ++i)
        {
            unsigned subResource = D3D11CalcSubresource(0, i, traits_.mips_);
            ctx->ResolveSubresource(resolveTexture_->texture_, subResource, texture_, subResource, dx_GetInternalFormat(traits_.format_));
        }
    }
    else if (traits_.kind_ == Texture2DArray)
    {
        for (int i = 0; i < traits_.layers_; ++i)
        {
            unsigned subResource = D3D11CalcSubresource(0, i, traits_.mips_);
            ctx->ResolveSubresource(resolveTexture_->texture_, subResource, texture_, subResource, dx_GetInternalFormat(traits_.format_));
        }
    }

    resolveDirty_ = false;
}

}