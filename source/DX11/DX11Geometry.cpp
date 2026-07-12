//****************************************************************************
//
//  File:       DX11Geometry.cpp
//  License:    MIT
//  Project:    GLVU
//  Contents:   Manages the abstraction for vertex-signatures which is a little `special`
//              since GLVU uses a common layouts mapped to whatever shader it can.
//				This class therefore mostly just does the prep work for DX11StateCache.
//
//****************************************************************************

#include "Geometry.h"

#include "GraphicsDevice.h"
#include "DX11StateCache.h"

#include <set>

namespace GLVU
{

//****************************************************************************
//
//  Function:   GeometryLayout::IsValid
//
//  Purpose:    Nothing to do on DX11
//
//****************************************************************************
bool GeometryLayout::IsValid() const
{
    // DX11StateCache makes this pointless, validity is not a combination of
	// shader+signature.
    return true;
}

//****************************************************************************
//
//  Function:   GeometryLayout::Release
//
//  Purpose:    Nothing to do on DX11.
//
//****************************************************************************
void GeometryLayout::Release()
{
	// DX11 has nothing to do because DX11StateCache manages these.
}

//****************************************************************************
//
//  Function:   GeometryLayout::Bind
//
//  Purpose:    Create an input layout if required and then binds the provided
//				buffers and geometry for use.
//
//              This function is an asshole.
//
//****************************************************************************
void GeometryLayout::Bind(Geometry* forGeo, const std::vector<std::shared_ptr<Buffer>>& extraBuffers, bool instanceDataOnly)
{
	{
		auto device = GetDevice()->GetD3DDevice();
		auto ctx = GetDevice()->GetD3DContext();
		auto cache = GetDevice()->GetDX11State();

		if (cache->GetVS())
			cache->SetInputLayout(cache->GetInputLayout(this));
		else
			device_->LogMessage("Attempted to bind geometry layout without vertex shader", GLVU_ERROR);
		
	}

	std::vector<unsigned> offsets;
	std::vector<unsigned> strides;
	std::vector<ID3D11Buffer*> buffers;

	if (!instanceDataOnly)
	{
		for (auto& v : forGeo->vertexBuffers_)
		{
			offsets.push_back(0);
			strides.push_back(ElementSize(buffers.size()));
			buffers.push_back(v->buffer_);
		}

		for (auto& v : extraBuffers)
		{
			offsets.push_back(0);
			strides.push_back(ElementSize(buffers.size()));
			buffers.push_back(v->buffer_);
		}

		if (forGeo->indexBuffer_)
			GetDevice()->GetD3DContext()->IASetIndexBuffer(forGeo->indexBuffer_->buffer_, forGeo->indexBuffer_->HasTag(BufferTag_32Bit) ? DXGI_FORMAT_R32_UINT : DXGI_FORMAT_R16_UINT, 0);
		GetDevice()->GetD3DContext()->IASetVertexBuffers(0, buffers.size(), buffers.data(), strides.data(), offsets.data());
	}
	else
	{
		uint32_t stride = ElementSize(instanceBufferIndex_);
		uint32_t offset = 0;
		ID3D11Buffer* buff = extraBuffers[0]->buffer_;
		GetDevice()->GetD3DContext()->IASetVertexBuffers(instanceBufferIndex_, 1, &buff, &stride, &offset);
	}
}

}