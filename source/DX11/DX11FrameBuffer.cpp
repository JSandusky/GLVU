//****************************************************************************
//
//  File:       DX11FrameBuffer.cpp
//  License:    MIT
//  Project:    GLVU
//  Contents:   Constructs RTVs and DSVs for a configuration of a textures in
//				the FrameBuffer type.
//
//	Note:		SwapChain surfaces are handled outside of this, and the extraResources_
//				member will likely never hold anything except the buffer acquired
//				from the swap-chain and sharing objects (ie. DX9/DX12 interop).
//
//****************************************************************************

#include"DX11StateCache.h"
#include "GraphicsDevice.h"
#include "Texture.h"

namespace GLVU
{

extern DXGI_FORMAT dx_GetInternalFormat(TextureFormat fmt);

//****************************************************************************
//
//  Function:   FrameBuffer::FrameBuffer
//
//  Purpose:    Construct, and zero-init
//
//****************************************************************************
FrameBuffer::FrameBuffer(GraphicsDevice* device, const std::vector<std::shared_ptr<Texture> >& textures) :
    GPUObject(device),
    textures_(textures)
{
	Create();
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
    layer_(layer)
{
    textures_.push_back(texture);
	Create();
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
//  Function:   FrameBuffer::Create
//
//  Purpose:    Constructs the required views.
//
//****************************************************************************
void FrameBuffer::Create()
{
	auto d3dDevice = GetDevice()->GetD3DDevice();
	for (auto& t : textures_)
	{
		if (IsDepth(t->GetFormat()))
		{
			D3D11_DEPTH_STENCIL_VIEW_DESC desc;
			ZeroMemory(&desc, sizeof(desc));
			desc.Texture2D.MipSlice = 0;
			if (t->GetFormat() == TEX_DEPTH)
				desc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
			else if (t->GetFormat() == TEX_SHADOW32)
				desc.Format = DXGI_FORMAT_D32_FLOAT;
			else
				desc.Format = DXGI_FORMAT_D16_UNORM;

			desc.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
			desc.Flags = 0;

			auto hr = d3dDevice->CreateDepthStencilView(t->texture_, &desc, &depthView_);
			if (FAILED(hr))
			{
				GetDevice()->LogFormat(GLVU_ERROR, "Failed to create depth-stencil view: %X", hr);
				return;
			}
		}
		else
		{
			D3D11_RENDER_TARGET_VIEW_DESC viewDesc;
			ZeroMemory(&viewDesc, sizeof(viewDesc));
			viewDesc.Format = dx_GetInternalFormat(t->GetFormat());

			switch (t->GetTextureKind())
			{
			case Texture2D:
				viewDesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
				viewDesc.Texture2D.MipSlice = 0;
				break;
			case TextureCube:
				viewDesc.Texture2DArray.MipSlice = 0;
				viewDesc.Texture2DArray.FirstArraySlice = layer_ == -1 ? 0 : layer_;
				viewDesc.Texture2DArray.ArraySize = layer_ == -1 ? 6 : 1;
				viewDesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2DARRAY;
				break;
			case Texture2DArray:
				viewDesc.Texture2DArray.MipSlice = 0;
				viewDesc.Texture2DArray.FirstArraySlice = layer_ == -1 ? 0 : layer_;
				viewDesc.Texture2DArray.ArraySize = layer_ == -1 ? t->GetLayers() : 1;
				viewDesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2DARRAY;
			}

			ID3D11RenderTargetView* view;
			auto hr = d3dDevice->CreateRenderTargetView(t->texture_, &viewDesc, &view);
			if (FAILED(hr))
			{
				GetDevice()->LogFormat(GLVU_ERROR, "Failed to create render target view: %X", hr);
				return;
			}
			views_.push_back(view);
		}
	}
}

//****************************************************************************
//
//  Function:   FrameBuffer::IsValid
//
//  Purpose:    Utility, here because GL backbuffer is a hack.
//
//  Return:     True if probably valid, this is mostly nonsense on DX11
//
//****************************************************************************
bool FrameBuffer::IsValid() const
{
	return !views_.empty() || depthView_ != nullptr;
}

//****************************************************************************
//
//  Function:   FrameBuffer::Release
//
//  Purpose:    Delete the FBO, DX11 doesn't have anything to do because these
//				are a fake object.
//
//****************************************************************************
void FrameBuffer::Release()
{
	for (auto& v : views_)
		v->Release();
	if (depthView_)
		depthView_->Release();
	for (auto r : extraResources_)
		r->Release();

	extraResources_.clear();
	views_.clear();
	depthView_ = nullptr;
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
	device_->GetDX11State()->SetTargets(this);
	// Previously this class just directly called, but no longer does that to keep cleaner calls
	// so that GPU debuggers/frame-analyzers don't have stupid looking results.
	//GetDevice()->GetD3DContext()->OMSetRenderTargets(views_.size(), views_.data(), depthView_);
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
    //if (!IsDirty())
    //    return;

	auto ctx = GetDevice()->GetD3DContext();
	float col[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
	if (color)
	{
		memcpy(col, color, sizeof(float) * 4);
		for (auto& v : views_)
			ctx->ClearRenderTargetView(v, col);
	}

	if (depthView_ && (depth || stencil))
		ctx->ClearDepthStencilView(depthView_, (depth ? D3D11_CLEAR_DEPTH : 0) | (stencil ? D3D11_CLEAR_STENCIL : 0), 1.0f, 0);

    dirty_ = false;
}

}