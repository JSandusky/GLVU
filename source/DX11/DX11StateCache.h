//****************************************************************************
//
//  File:       DX11StateCache.h
//  License:    MIT
//  Project:    GLVU
//  Contents:   Caches and records state so that DX11 context operations
//				that are potentially flush inducing are not made excessively.
//				Has the convenient side effect that GPU debuggers are now easier to read.
//
//****************************************************************************

#pragma once

#include <glvu.h>

#include <map>
#include <vector>
#include <memory>

namespace GLVU
{

class Buffer;
class FrameBuffer;
class GraphicsDevice;
class Shader;

/// A nasty manager of CRC32s and active resource states to minimize binding calls.
/// Although DX11 itself does do a pretty good job most of the time, some calls
///	like IASetInputLayout() are still murderous at scale.
///
///	Most of those are results of API usage patterns in GLVU. Everything that's automipped
///	is a render-target ID3D11InputLayouts are fudged, etc.
struct DX11StateCache
{
	DX11StateCache(GraphicsDevice*);
	~DX11StateCache();

	/// Must be called before the cache can be used in order to initialize default state objects.
	void CreateDeviceObjects();

	/// Sets the active depth-stencil state to a safe default.
	void SetDefaultDepth() { SetDepthStencilState(defaultDepth_); }
	/// Sets the active depth-stencil state to a no-depth test|write mode.
	void SetNoDepth() { SetDepthStencilState(noDepthState_); }

	void SetNoCull() { SetRasterState(noCullState_); }

	void SetGeometryBuffers(const std::shared_ptr<Buffer>& idxBuffer, const std::vector<std::shared_ptr<Buffer> >& vertexBuffers, const std::vector<uint32_t>& sizes);
	void SetInputLayout(ID3D11InputLayout* layout);

	ID3D11InputLayout* GetInputLayout(const class GeometryLayout* geoLayout);
	ID3D11BlendState* GetBlendState(const D3D11_BLEND_DESC& state);
	ID3D11RasterizerState* GetRasterState(const D3D11_RASTERIZER_DESC& state);
	ID3D11DepthStencilState* GetDepthStencilState(const D3D11_DEPTH_STENCIL_DESC& state);

	void SetPrimitive(PrimitiveType, bool tess);
	void SetBlendState(const D3D11_BLEND_DESC& state) { SetBlendState(GetBlendState(state)); }
	void SetRasterState(const D3D11_RASTERIZER_DESC& state) { SetRasterState(GetRasterState(state)); }
	void SetDepthStencilState(const D3D11_DEPTH_STENCIL_DESC& state) { SetDepthStencilState(GetDepthStencilState(state)); }

	void SetBlendState(ID3D11BlendState*);
	void SetRasterState(ID3D11RasterizerState*);
	void SetDepthStencilState(ID3D11DepthStencilState*);

	void SetShaders(std::shared_ptr<Shader> vs, std::shared_ptr<Shader> ps, std::shared_ptr<Shader> hs, std::shared_ptr<Shader> ds, std::shared_ptr<Shader> gs);

#define SHADER_GETTER(NAME, STAGE) const std::shared_ptr<Shader>& Get ## NAME() { return shaders_[STAGE]; }
	SHADER_GETTER(VS, VertexShader);
	SHADER_GETTER(HS, HullShader);
	SHADER_GETTER(DS, DomainShader);
	SHADER_GETTER(GS, GeometryShader);
	SHADER_GETTER(PS, PixelShader);
#undef SHADER_GETTER

	void SetTargets(const FrameBuffer* buffer);
    void PushTargets(const FrameBuffer* buffer);
    void PopTargets();
    const FrameBuffer* GetFBO() const { return fbo_; }

	int GetPixelRate() const { return pixelRate_; }
	void SetPixelRate(int rate);

private:
	GraphicsDevice* device_;
	ID3D11BlendState* blendState_ = nullptr;
	ID3D11RasterizerState* currentRasterState_ = nullptr;
	ID3D11DepthStencilState* currentDepthStencilState_ = nullptr;
	ID3D11DepthStencilState* noDepthState_ = nullptr;
	ID3D11DepthStencilState* defaultDepth_ = nullptr;
	ID3D11RasterizerState* noCullState_ = nullptr;
	ID3D11InputLayout* inputLayout_ = nullptr;
	ID3D11Buffer* indexBuffer_ = nullptr;
	std::vector<ID3D11Buffer*> vertexBuffers_;
	ID3D11BlendState* blendStateTable_[COUNT_BLEND_MODE];
	std::map<uint32_t, ID3D11BlendState*> blendStates_;
	std::map<uint32_t, ID3D11RasterizerState*> rasterizerStates_;
	std::map<uint32_t, ID3D11DepthStencilState*> depthStencilStates_;
	std::map<std::pair<uint32_t, uint32_t>, ID3D11InputLayout*> inputLayouts_;
	D3D11_PRIMITIVE_TOPOLOGY topology_ = D3D11_PRIMITIVE_TOPOLOGY_10_CONTROL_POINT_PATCHLIST;
	std::shared_ptr<Shader> shaders_[COUNT_SHADER_TYPE];

    const FrameBuffer* fbo_ = nullptr;
    std::vector<const FrameBuffer*> fboStack_;
	std::vector<ID3D11RenderTargetView*> colorTargets_;
	ID3D11DepthStencilView* depthTarget_ = nullptr;
	int pixelRate_ = 1;

public:
	template<typename T>
	T Default();

	template<>
	D3D11_RASTERIZER_DESC Default() {
		D3D11_RASTERIZER_DESC desc; 
		ZeroMemory(&desc, sizeof(desc));
		desc.CullMode = D3D11_CULL_BACK;
		desc.DepthBias = 0;
		desc.SlopeScaledDepthBias = 0.0f;
		desc.DepthClipEnable = TRUE;
		desc.FillMode = D3D11_FILL_SOLID;
		desc.FrontCounterClockwise = TRUE;
		return desc;
	}

	template<>
	D3D11_DEPTH_STENCIL_DESC Default() {
		D3D11_DEPTH_STENCIL_DESC desc; 
		ZeroMemory(&desc, sizeof(desc));
		desc.DepthEnable = TRUE;
		desc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
		desc.BackFace.StencilFunc = D3D11_COMPARISON_NEVER;
		desc.FrontFace.StencilFunc = D3D11_COMPARISON_NEVER;
		desc.StencilEnable = FALSE;
		desc.StencilReadMask = 0;
		desc.StencilWriteMask = 0;
		return desc;
	}
};

}