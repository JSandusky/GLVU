//****************************************************************************
//
//  File:       DX11StateCache.cpp
//  License:    MIT
//  Project:    GLVU
//  Contents:   Implementation of DX11 state caching.
//
//****************************************************************************

#include "DX11StateCache.h"

#include "Effect.h"
#include "Geometry.h"
#include "GraphicsDevice.h"
#include "Texture.h"
#include "glvu_math.h"
#include "Renderer.h"

#include <d3d11.h>

#ifdef GLVU_NVAPI
    #include <nvapi/nvapi.h>
#endif

using namespace std;

namespace GLVU
{

DX11StateCache::DX11StateCache(GraphicsDevice* device) :
	device_(device)
{
}

DX11StateCache::~DX11StateCache()
{
	for (auto rs : rasterizerStates_)
		rs.second->Release();
	for (auto ds : depthStencilStates_)
		ds.second->Release();
	for (auto layout : inputLayouts_)
		layout.second->Release();
}

void DX11StateCache::CreateDeviceObjects()
{
	D3D11_DEPTH_STENCIL_DESC noDepth;
	ZeroMemory(&noDepth, sizeof(noDepth));
	noDepth.DepthEnable = false;
	noDepth.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
	noDepthState_ = GetDepthStencilState(noDepth);

	D3D11_DEPTH_STENCIL_DESC defDepth;
	ZeroMemory(&defDepth, sizeof(defDepth));
	defDepth.DepthEnable = true;
	defDepth.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
	defaultDepth_ = GetDepthStencilState(defDepth);

	D3D11_RASTERIZER_DESC rasterDesc = Default<D3D11_RASTERIZER_DESC>();
	rasterDesc.CullMode = D3D11_CULL_NONE;
	noCullState_ = GetRasterState(rasterDesc);
}

void DX11StateCache::SetGeometryBuffers(const shared_ptr<Buffer>& idxBuffer, const vector<shared_ptr<Buffer> >& vertexBuffers, const vector<uint32_t>& sizes)
{
	if (idxBuffer && idxBuffer->buffer_ != indexBuffer_)
	{
		indexBuffer_ = idxBuffer->buffer_;
		device_->GetD3DContext()->IASetIndexBuffer(indexBuffer_, idxBuffer->HasTag(BufferTag_32Bit) ? DXGI_FORMAT_R32_UINT : DXGI_FORMAT_R16_UINT, 0);
		
	}

	if (vertexBuffers_.size() < vertexBuffers.size())
		vertexBuffers_.resize(vertexBuffers.size());
	for (unsigned i = 0; i < vertexBuffers.size(); ++i)
	{
		if (vertexBuffers_[i] != vertexBuffers[i]->buffer_)
		{
			unsigned offset = 0;
			vertexBuffers_[i] = vertexBuffers[i]->buffer_;
			device_->GetD3DContext()->IASetVertexBuffers(i, 1, &vertexBuffers_[i], &sizes[i], &offset); // FUCK!
		}
	}
}

void DX11StateCache::SetPrimitive(PrimitiveType state, bool tess)
{
	static const D3D11_PRIMITIVE_TOPOLOGY topo[] = {
		D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST,
		D3D11_PRIMITIVE_TOPOLOGY_POINTLIST,
		D3D11_PRIMITIVE_TOPOLOGY_LINELIST,
		D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST_ADJ,
		D3D11_PRIMITIVE_TOPOLOGY_LINELIST_ADJ,
	};
	auto s = tess ? D3D11_PRIMITIVE_TOPOLOGY_3_CONTROL_POINT_PATCHLIST : topo[state];
	if (s != topology_)
		device_->GetD3DContext()->IASetPrimitiveTopology(s);
	topology_ = s;
}

ID3D11InputLayout* DX11StateCache::GetInputLayout(const GeometryLayout* geoLayout)
{
	auto crc = CRC32::Calculate(geoLayout->vertexData_, sizeof(VertexInfo) * geoLayout->vertexDataCount_);
    auto key = make_pair(crc, shaders_[VertexShader]->GetSignatureHash());
	auto found = inputLayouts_.find(key);
	if (found != inputLayouts_.end())
		return found->second;

	vector<D3D11_INPUT_ELEMENT_DESC> desc;

	unsigned instCt = 0;
	for (uint32_t i = 0; i < geoLayout->vertexDataCount_; ++i)
	{
		D3D11_INPUT_ELEMENT_DESC v;
		ZeroMemory(&v, sizeof(v));

		const auto& elem = geoLayout->vertexData_[i];
		v.InstanceDataStepRate = elem.instanceStride_;
		v.AlignedByteOffset = D3D11_APPEND_ALIGNED_ELEMENT;
		//v.AlignedByteOffset = elem.offset_;
		v.InputSlot = elem.bufferSlot_;
		v.InputSlotClass = elem.instanceStride_ != 0 ? D3D11_INPUT_PER_INSTANCE_DATA : D3D11_INPUT_PER_VERTEX_DATA;

		static const char* semanticNames[] = {
			"POSITION",
			"NORMAL",
			"TANGENT",

			"TEXCOORD",
			"TEXCOORD",
			"TEXCOORD",

			"COORD",
			"COORD",
			"COORD",

			"SINGLE",
			"SINGLE",
			"SINGLE",

			"COLOR",
			"COLOR",
			"COLOR",

			"INSTANCE",
			"BLENDINDICES",
			"BLENDWEIGHTS",
		};

		static int semanticIndices[] = {
			0, // Position
			0, // Normal
			0, // Tangent

			0, // TEXCOORD
			1, // TEXCOORD
			2, // TEXCOORD

			0, // COORD
			1, // COORD
			2, // COORD

			0, // SINGLE
			1, // SINGLE
			2, // SINGLE

			0, // COLOR
			1, // COLOR
			2, // COLOR

			0, // INSTANCE
			0, // BONEINDICES
			0  // BONEWEIGHTS
		};

		v.SemanticName = semanticNames[elem.attribute_];
		v.SemanticIndex = semanticIndices[elem.attribute_];

		// HACK!
		if (elem.attribute_ == VA_INSTANCE)
			v.SemanticIndex = instCt++;

		switch (elem.type_)
		{
		case VDT_FLOAT:
			if (elem.elementCount == 4)
				v.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
			else if (elem.elementCount == 3)
				v.Format = DXGI_FORMAT_R32G32B32_FLOAT;
			else if (elem.elementCount == 2)
				v.Format = DXGI_FORMAT_R32G32_FLOAT;
			else
				v.Format = DXGI_FORMAT_R32_FLOAT;
			break;
        case VDT_HALF:
            if (elem.elementCount == 4)
                v.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
            else if (elem.elementCount == 2)
                v.Format = DXGI_FORMAT_R16G16_FLOAT;
            else if (elem.elementCount == 1)
                v.Format = DXGI_FORMAT_R16_FLOAT;
            break;
		case VDT_UINT:
			if (elem.elementCount == 4)
				v.Format = DXGI_FORMAT_R32G32B32A32_UINT;
			else if (elem.elementCount == 3)
				v.Format = DXGI_FORMAT_R32G32B32_UINT;
			else if (elem.elementCount == 3)
				v.Format = DXGI_FORMAT_R32G32_UINT;
			else
				v.Format = DXGI_FORMAT_R32_UINT;
			break;
		case VDT_UBYTE:
			if (elem.elementCount == 4)
				v.Format = elem.normalized_ ? DXGI_FORMAT_R8G8B8A8_UNORM : DXGI_FORMAT_R8G8B8A8_UINT;
			break;
        case VDT_SBYTE:
            if (elem.elementCount == 4)
                v.Format = elem.normalized_ ? DXGI_FORMAT_R8G8B8A8_SNORM : DXGI_FORMAT_R8G8B8A8_SINT;
            break;
		}

		desc.push_back(v);
	}

	ID3D11InputLayout* ret = nullptr;
	ID3DBlob* byteCodeBlob = shaders_[VertexShader]->GetShaderByteCode();
	auto hr = device_->GetD3DDevice()->CreateInputLayout(desc.data(), desc.size(), byteCodeBlob->GetBufferPointer(), byteCodeBlob->GetBufferSize(), &ret);
	if (FAILED(hr))
	{
		device_->LogFormat(GLVU_ERROR, "Failed to create input layout: HRESULT %X", hr);
	}
	inputLayouts_[key] = ret;
	return ret;
}

void DX11StateCache::SetInputLayout(ID3D11InputLayout* layout) 
{ 
	if (layout != inputLayout_) 
	{ 
		inputLayout_ = layout; 
		device_->GetD3DContext()->IASetInputLayout(inputLayout_);
	}
}

ID3D11BlendState* DX11StateCache::GetBlendState(const D3D11_BLEND_DESC& state)
{
	auto crc = CRC32::Calculate(&state, sizeof(state));

	auto found = blendStates_.find(crc);
	if (found != blendStates_.end())
		return found->second;

	ID3D11BlendState* result;
	auto hr = device_->GetD3DDevice()->CreateBlendState(&state, &result);
	if (FAILED(hr))
	{
		device_->LogFormat(GLVU_ERROR, "Failed to create blend state: %X", hr);
		return nullptr;
	}
	blendStates_[crc] = result;
	return result;
}

ID3D11RasterizerState* DX11StateCache::GetRasterState(const D3D11_RASTERIZER_DESC& state)
{
	auto crc = CRC32::Calculate(&state, sizeof(state));

	auto found = rasterizerStates_.find(crc);
	if (found != rasterizerStates_.end())
		return found->second;

	ID3D11RasterizerState* result;
	auto hr = device_->GetD3DDevice()->CreateRasterizerState(&state, &result);
	if (FAILED(hr))
	{
		return nullptr;
	}

	rasterizerStates_[crc] = result;
	return result;
}

ID3D11DepthStencilState* DX11StateCache::GetDepthStencilState(const D3D11_DEPTH_STENCIL_DESC& state)
{
	auto crc = CRC32::Calculate(&state, sizeof(state));

	auto found = depthStencilStates_.find(crc);
	if (found != depthStencilStates_.end())
		return found->second;

	ID3D11DepthStencilState* result;
	auto hr = device_->GetD3DDevice()->CreateDepthStencilState(&state, &result);
	if (FAILED(hr))
	{
		return nullptr;
	}

	depthStencilStates_[crc] = result;
	return result;
}

void DX11StateCache::SetBlendState(ID3D11BlendState* s)
{
	if (s == blendState_)
		return;

	blendState_ = s;
	float blendFactor[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
    D3D11_BLEND_DESC1 desc;
	device_->GetD3DContext()->OMSetBlendState(s, blendFactor, 0xFFFFFFFF);
}

void DX11StateCache::SetRasterState(ID3D11RasterizerState* s)
{
	if (s == currentRasterState_)
		return;
	currentRasterState_ = s;
	device_->GetD3DContext()->RSSetState(s);
}

void DX11StateCache::SetDepthStencilState(ID3D11DepthStencilState* s)
{
	if (s == currentDepthStencilState_)
		return;
	device_->GetD3DContext()->OMSetDepthStencilState(s, 0);
	currentDepthStencilState_ = s;
}

void DX11StateCache::SetShaders(shared_ptr<Shader> vs, shared_ptr<Shader> ps, shared_ptr<Shader> hs, shared_ptr<Shader> ds, shared_ptr<Shader> gs)
{
#define DO_SHADER(STAGE, VAR, NAME) if (VAR != shaders_[NAME]) { \
		shaders_[NAME] = VAR; \
		device_->GetD3DContext()-> ## STAGE ## SetShader(VAR ->GetShader<ID3D11 ## NAME>(), nullptr, 0); }

	DO_SHADER(VS, vs, VertexShader);
	DO_SHADER(PS, ps, PixelShader);
	DO_SHADER(HS, hs, HullShader);
	DO_SHADER(DS, ds, DomainShader);
	DO_SHADER(GS, gs, GeometryShader);

#undef DO_SHADER
}

void DX11StateCache::SetTargets(const FrameBuffer* buffer)
{
	// easy path, safer than null checking below
	if (buffer == nullptr)
	{
		bool dirty = false;
		if (depthTarget_ != nullptr)
			dirty |= true;
		if (!colorTargets_.empty())
			dirty |= true;

		if (dirty)
		{
            fbo_ = nullptr;
            fboStack_.clear();
			device_->GetD3DContext()->OMSetRenderTargets(0, nullptr, nullptr);
			colorTargets_.clear();
			depthTarget_ = nullptr;
			return;
		}
		return;
	}

	vector<ID3D11RenderTargetView*> colorTargets = buffer->GetViews();
	ID3D11DepthStencilView* ds = buffer->GetDepthStencilView();
    fbo_ = buffer;

	bool dirty = false;
	if (colorTargets.size() != colorTargets_.size())
		dirty = true;
	if (depthTarget_ != ds)
		dirty = true;
	for (uint32_t i = 0; i < std::min(colorTargets.size(), colorTargets_.size()); ++i)
	{
		if (colorTargets_[i] != colorTargets[i])
			dirty = true;
	}

	if (dirty)
	{
		device_->GetD3DContext()->OMSetRenderTargets(colorTargets.size(), colorTargets.data(), ds);
		colorTargets_ = colorTargets;
		depthTarget_ = ds;
	}
}

void DX11StateCache::PushTargets(const FrameBuffer* buffer)
{
    fboStack_.push_back(fbo_);
    SetTargets(buffer);
}

void DX11StateCache::PopTargets()
{
    if (fboStack_.size())
    {
        SetTargets(fboStack_.back());
        fboStack_.pop_back();
    }
}

#ifdef GLVU_NVAPI

// lower, higher
NV_PIXEL_SHADING_RATE mappingTable[][2] = {
    { NV_PIXEL_X0_CULL_RASTER_PIXELS, NV_PIXEL_X1_PER_RASTER_PIXEL          }, // NV_PIXEL_X0_CULL_RASTER_PIXELS
{ NV_PIXEL_X8_PER_RASTER_PIXEL, NV_PIXEL_X16_PER_RASTER_PIXEL           }, // NV_PIXEL_X16_PER_RASTER_PIXEL
{ NV_PIXEL_X4_PER_RASTER_PIXEL, NV_PIXEL_X16_PER_RASTER_PIXEL,          }, // NV_PIXEL_X8_PER_RASTER_PIXEL
{ NV_PIXEL_X2_PER_RASTER_PIXEL, NV_PIXEL_X8_PER_RASTER_PIXEL,           }, // NV_PIXEL_X4_PER_RASTER_PIXEL
{ NV_PIXEL_X1_PER_RASTER_PIXEL, NV_PIXEL_X4_PER_RASTER_PIXEL,           }, // NV_PIXEL_X2_PER_RASTER_PIXEL
{ NV_PIXEL_X1_PER_2X2_RASTER_PIXELS, NV_PIXEL_X2_PER_RASTER_PIXEL,      }, // NV_PIXEL_X1_PER_RASTER_PIXEL
    { NV_PIXEL_X1_PER_1X2_RASTER_PIXELS, NV_PIXEL_X1_PER_RASTER_PIXEL,      }, // NV_PIXEL_X1_PER_2X1_RASTER_PIXELS
    { NV_PIXEL_X1_PER_2X2_RASTER_PIXELS, NV_PIXEL_X1_PER_2X1_RASTER_PIXELS  }, // NV_PIXEL_X1_PER_1X2_RASTER_PIXELS 
{ NV_PIXEL_X1_PER_4X4_RASTER_PIXELS, NV_PIXEL_X1_PER_RASTER_PIXEL  }, // NV_PIXEL_X1_PER_2X2_RASTER_PIXELS
    { NV_PIXEL_X1_PER_2X4_RASTER_PIXELS, NV_PIXEL_X1_PER_2X2_RASTER_PIXELS  }, // NV_PIXEL_X1_PER_4X2_RASTER_PIXELS
    { NV_PIXEL_X1_PER_4X4_RASTER_PIXELS, NV_PIXEL_X1_PER_4X2_RASTER_PIXELS, }, // NV_PIXEL_X1_PER_2X4_RASTER_PIXELS
{ NV_PIXEL_X1_PER_4X4_RASTER_PIXELS, NV_PIXEL_X1_PER_2X2_RASTER_PIXELS  }  // NV_PIXEL_X1_PER_4X4_RASTER_PIXELS
};

static inline NV_PIXEL_SHADING_RATE Lower(NV_PIXEL_SHADING_RATE r) { return mappingTable[r][0]; }
static inline NV_PIXEL_SHADING_RATE Higher(NV_PIXEL_SHADING_RATE r) { return mappingTable[r][1]; }

#endif

void DX11StateCache::SetPixelRate(int rate)
{
#ifdef GLVU_NVAPI
    if (device_->GetGPUFeatures().nvapi_ && device_->GetGPUFeatures().variableRateShading_)
    {
    retry_rate:
        if (pixelRate_ != rate)
        {
            NV_PIXEL_SHADING_RATE sr = NV_PIXEL_X1_PER_RASTER_PIXEL;
            switch (rate)
            {
            case 1:
                sr = NV_PIXEL_X1_PER_RASTER_PIXEL;
                break;
            case 2:
                sr = NV_PIXEL_X1_PER_2X2_RASTER_PIXELS;
                break;
            case 4:
                sr = NV_PIXEL_X1_PER_4X4_RASTER_PIXELS;
                break;
            case -2:
                sr = NV_PIXEL_X2_PER_RASTER_PIXEL;
                break;
            case -4:
                sr = NV_PIXEL_X4_PER_RASTER_PIXEL;
                break;
            case -8:
                sr = NV_PIXEL_X8_PER_RASTER_PIXEL;
                break;
            case -16:
                sr = NV_PIXEL_X16_PER_RASTER_PIXEL;
                break;
            default:
                rate = 1;
                goto retry_rate;
            }

            NV_D3D11_VIEWPORTS_SHADING_RATE_DESC rateDesc;
            rateDesc.version = NV_D3D11_VIEWPORTS_SHADING_RATE_DESC_VER1;
            rateDesc.numViewports = rate != 1 ? 1 : 0;

            NV_D3D11_VIEWPORT_SHADING_RATE_DESC viewDesc;
            viewDesc.enableVariablePixelShadingRate = rate != 1 ? true : false;
            for (int i = 0; i < 16; ++i)
                viewDesc.shadingRateTable[i] = sr;
            rateDesc.pViewports = &viewDesc;

            NvAPI_D3D11_RSSetViewportsPixelShadingRates(device_->GetD3DContext(), &rateDesc);
        }
    }
#endif
}

}