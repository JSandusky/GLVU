//****************************************************************************
//
//  File:       DX11GraphicsDevice.cpp
//  License:    MIT
//  Project:    GLVU
//  Contents:   Device management and GPU object creation for DirectX11.
//
//****************************************************************************

#include <GraphicsDevice.h>
#include <Renderer.h>

#include "DX11StateCache.h"
#include "ShaderCache.h"

#include <d3d11.h>
#include <d3d11_1.h>
#include <d3d11_2.h>

#ifdef GLVU_NVAPI
	#include <nvapi/nvapi.h>
#endif

#include <algorithm>

#undef max

#define DXERROR(MESSAGE) LogFormat(GLVU_ERROR, MESSAGE " HRESULT %X", hr)
#define DXWARNING(MESSAGE) LogFormat(GLVU_WARNING, MESSAGE " HRESULT %X", hr)

using namespace std;

namespace GLVU
{

DXGI_FORMAT dx_InternalFormat[] = {
	DXGI_FORMAT_R8G8B8A8_UNORM,			// TEX_RGB8,
    DXGI_FORMAT_R8G8B8A8_UNORM,         // TEX_RGBA8,
    DXGI_FORMAT_R16G16B16A16_FLOAT,     // TEX_RGBA16F,
    DXGI_FORMAT_R16G16_FLOAT,           // TEX_RG16F,
	DXGI_FORMAT_BC1_UNORM,				// DXT1
    DXGI_FORMAT_BC2_UNORM,				// DXT3
	DXGI_FORMAT_BC3_UNORM,				// DXT5
    DXGI_FORMAT_BC4_UNORM,				// BC4
    DXGI_FORMAT_BC5_UNORM,				// BC5
    DXGI_FORMAT_R16_TYPELESS,           // TEX_SHADOW16,
    DXGI_FORMAT_R32_TYPELESS,           // TEX_SHADOW32,
	DXGI_FORMAT_R24G8_TYPELESS,			// TEX_DEPTH
    DXGI_FORMAT_B8G8R8A8_UNORM,         // TEX_BGRA8
    DXGI_FORMAT_R32_FLOAT,              // TEX_R32F
    DXGI_FORMAT_R32_UINT,               // TEX_R32U
    DXGI_FORMAT_R8G8B8A8_UINT,          // TEX_RGBA8UI
	DXGI_FORMAT_R8_UINT,				// TEX_R8U
};

bool dx_FormatCanUAV[] = {
	true,	// TEX_RGB8,
	true,   // TEX_RGBA8,
	true,	// TEX_RGBA16F,
	true,	// TEX_RG16F,
	false,	// DXT1
	false,	// DXT3
	false,	// DXT5
    false,  // BC4
    false,  // BC5
	false,  // TEX_SHADOW16,
	false,  // TEX_SHADOW32,
	false,	// TEX_DEPTH
	true,   // TEX_BGRA8
	true,   // TEX_R32F
	true,   // TEX_R32U
	true,   // TEX_RGBA8UI
	true,   // TEX_R8U
};

DXGI_FORMAT dx_SRVInternalFormat[] = {
	DXGI_FORMAT_R8G8B8A8_UNORM,			// TEX_RGB8,
	DXGI_FORMAT_R8G8B8A8_UNORM,         // TEX_RGBA8,
	DXGI_FORMAT_R16G16B16A16_FLOAT,     // TEX_RGBA16F,
	DXGI_FORMAT_R16G16_FLOAT,           // TEX_RG16F,
	DXGI_FORMAT_BC1_UNORM,				// DXT1
	DXGI_FORMAT_BC2_UNORM,				// DXT3
	DXGI_FORMAT_BC3_UNORM,				// DXT5
    DXGI_FORMAT_BC4_UNORM,				// BC4
    DXGI_FORMAT_BC5_UNORM,				// BC5
	DXGI_FORMAT_R16_UNORM,              // TEX_SHADOW16,
	DXGI_FORMAT_R32_FLOAT,              // TEX_SHADOW32,
	DXGI_FORMAT_R24_UNORM_X8_TYPELESS,  // TEX_DEPTH
	DXGI_FORMAT_B8G8R8A8_UNORM,         // TEX_BGRA8
	DXGI_FORMAT_R32_FLOAT,              // TEX_R32F
	DXGI_FORMAT_R32_UINT,               // TEX_R32U
	DXGI_FORMAT_R8G8B8A8_UINT,          // TEX_RGBA8UI
	DXGI_FORMAT_R8_UINT,				// TEX_R8U
};

DXGI_FORMAT dx_GetInternalFormat(TextureFormat fmt)
{
    return dx_InternalFormat[fmt];
}

unsigned dx_RowDataSize(DXGI_FORMAT glFormat, int width)
{
	switch (glFormat)
	{
	case DXGI_FORMAT_R8_UINT:
	case DXGI_FORMAT_R8_UNORM:
		return (unsigned)width;

	case DXGI_FORMAT_R8G8_SINT:
	case DXGI_FORMAT_R8G8_UINT:
	case DXGI_FORMAT_R8G8_UNORM:
	case DXGI_FORMAT_R8G8_SNORM:
	case DXGI_FORMAT_R16_UINT:
	case DXGI_FORMAT_R16_FLOAT:
	case DXGI_FORMAT_R16_SINT:
		return (unsigned)(width * 2);

	//??case DXGI
	//??	return (unsigned)(width * 3);

	case DXGI_FORMAT_R8G8B8A8_UNORM:
	case DXGI_FORMAT_R8G8B8A8_SNORM:
	case DXGI_FORMAT_R8G8B8A8_TYPELESS:
	case DXGI_FORMAT_R8G8B8A8_UINT:
	case DXGI_FORMAT_R16G16_FLOAT:
	case DXGI_FORMAT_R32_FLOAT:
	case DXGI_FORMAT_R32_UINT:
		return (unsigned)(width * 4);

	case DXGI_FORMAT_R16G16B16A16_FLOAT:
	case DXGI_FORMAT_R16G16B16A16_UINT:
		return (unsigned)(width * 8);

	case DXGI_FORMAT_R32G32B32A32_FLOAT:
	case DXGI_FORMAT_R32G32B32A32_UINT:
		return (unsigned)(width * 16);

	case DXGI_FORMAT_BC1_UNORM:
    case DXGI_FORMAT_BC4_UNORM:
		return ((unsigned)(width + 3) >> 2u) * 8;

	case DXGI_FORMAT_BC2_UNORM:
	case DXGI_FORMAT_BC3_UNORM:
    case DXGI_FORMAT_BC5_UNORM:
		return ((unsigned)(width + 3) >> 2u) * 16;

	default:
		return 0;
	}
}

vector<ID3D11DeviceChild*> dx_releaseLater[2];

void dx_FlushLateRelease()
{
    for (auto& r : dx_releaseLater[1])
        r->Release();
    dx_releaseLater[1].clear();

    std::swap(dx_releaseLater[0], dx_releaseLater[1]);
}

void dx_PushLateRelease(ID3D11DeviceChild* child)
{
    dx_releaseLater[0].push_back(child);
}

//****************************************************************************
//
//  Function:   GraphicsDevice::GraphicsDevice
//
//  Purpose:    Construct, grab current GL state and setup caches.
//
//****************************************************************************
GraphicsDevice::GraphicsDevice() :
    effectCache_(this)
{
	dx11StateCache_ = new DX11StateCache(this);
    shaderCache_.reset(new ShaderCache(this));

	// DX11, its all the same
	graphicsFeatures_.clipControl_ = true;
	graphicsFeatures_.compute_ = true;
	graphicsFeatures_.geometryShader_ = true;
	graphicsFeatures_.tessellation_ = true;
	graphicsFeatures_.transformFeedback_ = true;
	graphicsFeatures_.shaderStorageBuffer_ = true;
    graphicsFeatures_.multipleHeads_ = true;
	graphicsFeatures_.minUBOAlignment_ = 256;  // it's not really worth fussing about, only AMD support less
	graphicsFeatures_.maxUBOSize_ = 64 * 1024; // DX11 requires at least this, in practice it's also the max
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
	for (auto s : clampSamplers_)
		s->Release();
	for (auto s : wrapSamplers_)
		s->Release();

	if (dx11StateCache_)
		delete dx11StateCache_;
	dx11StateCache_ = nullptr;

	DX_RELEASE(d3dContext_);
	DX_RELEASE(d3dDevice_);
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
	IDXGIFactory* factory;
	auto hr = CreateDXGIFactory(__uuidof(IDXGIFactory), (void**)&factory);
	if (FAILED(hr))
	{
		DX_RELEASE(factory);
		LogFormat(GLVU_ERROR, "Failed to create DXGI factory: HRESULT %X", hr);
		return false;
	}

	IDXGIAdapter* adapter;
	hr = factory->EnumAdapters(0, &adapter);
	if (FAILED(hr))
	{
		DX_RELEASE(adapter);
		DX_RELEASE(factory);
		DXERROR("Failed to create DXGI Adapter");
		return false;
	}

	IDXGIOutput* adapterOutput;
	hr = adapter->EnumOutputs(0, &adapterOutput);
	if (FAILED(hr))
	{
		DX_RELEASE(adapter);
		DX_RELEASE(adapterOutput);
		DX_RELEASE(factory);
		DXERROR("Failed to create DXGI Output");
		return false;
	}

	unsigned numModes = 0;
	hr = adapterOutput->GetDisplayModeList(DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_ENUM_MODES_INTERLACED, &numModes, nullptr);
	if (FAILED(hr))
	{
		DX_RELEASE(adapter);
		DX_RELEASE(adapterOutput);
		DX_RELEASE(factory);
		DXERROR("Failed to enumerate display modes for RGBA8");
		return false;
	}

	DXGI_ADAPTER_DESC adapterDesc;
	hr = adapter->GetDesc(&adapterDesc);
	if (FAILED(hr))
	{
		DXERROR("Failed to acquire adapter description");
		return false;
	}

	char name[128];
	for (int i = 0; i < 128; ++i)
		name[i] = adapterDesc.Description[i];

	// Example: "Intel(R) HD Graphics 4000, 32mb"
	LogFormat(GLVU_INFO, "%s, %umb", name, adapterDesc.DedicatedVideoMemory / 1048576);

	DX_RELEASE(adapter);
	DX_RELEASE(adapterOutput);
	DX_RELEASE(factory);

    D3D_FEATURE_LEVEL levels[] = {
        D3D_FEATURE_LEVEL_11_1,
        D3D_FEATURE_LEVEL_11_0,
    };
	D3D_FEATURE_LEVEL gotLevel;
	hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, 0, 
		0, levels, 2, 
		D3D11_SDK_VERSION, 
		&d3dDevice_, &gotLevel, &d3dContext_);
	if (FAILED(hr))
	{
		DX_RELEASE(d3dDevice_);
		DX_RELEASE(d3dContext_);
		DXERROR("Failed to create DX11.1 device");
		return false;
	}

    hr = d3dDevice_->QueryInterface(__uuidof(ID3D11Device1), (void**)&d3dDevice1_);
    if (FAILED(hr))
    {
        DXERROR("Failed to acquire ID3D11Device1 interface");
        return false;
    }


	hr = d3dContext_->QueryInterface(__uuidof(ID3D11DeviceContext1), (void**)&d3dContext1_);
	if (FAILED(hr))
	{
		DXERROR("Failed to acquire ID3D11DeviceContext1 interface");
		return false;
	}

    hr = d3dContext_->QueryInterface(__uuidof(ID3DUserDefinedAnnotation), (void**)&d3dAnnote_);
    if (FAILED(hr))
    {
        DXWARNING("Unable to acquire annotation interface");
    }

	// DirectX guarantees that we'll have all the things we want.
    if (gotLevel == D3D_FEATURE_LEVEL_11_1)
        LogMessage("DirectX 11.1", GLVU_INFO);
    else if (gotLevel == D3D_FEATURE_LEVEL_11_0)
        LogMessage("DirectX 11.0", GLVU_INFO);

    LogMessage("+ Geometry Shader 4.0", GLVU_INFO);
    LogMessage("+ Tessellation", GLVU_INFO);
    LogMessage("+ Transform Feedback", GLVU_INFO);
	LogMessage("+ Instanced Transform Feedback", GLVU_INFO);
    LogMessage("+ Clip Control", GLVU_INFO);
    LogMessage("+ Compute Shader", GLVU_INFO);
	LogMessage("+ Shader Storage Buffer", GLVU_INFO);

    // To get around compiling on < Windows 10.0 (like an old W7 or W8.1 laptop) 
    // where D3D11.h lacks D3D11_2 and D3D11_3 enums + structures
    // just redefine the enum and structs here
    const int EXTRA_D3D11_OPTIONS3 = 15;
    struct EXTRA_D3D11_FEATURE_DATA_D3D11_OPTIONS3 {
        BOOL VPAndRTArrayIndexFromAnyShaderFeedingRasterizer;
    } d3d_opt3_data;
    hr = d3dDevice_->CheckFeatureSupport((D3D11_FEATURE)EXTRA_D3D11_OPTIONS3, &d3d_opt3_data, sizeof(EXTRA_D3D11_FEATURE_DATA_D3D11_OPTIONS3));
    if (FAILED(hr) || d3d_opt3_data.VPAndRTArrayIndexFromAnyShaderFeedingRasterizer == FALSE) {
        LogMessage("- VPAndRTArrayIndexFromAnyShaderFeedingRasterizer", GLVU_ERROR);
        graphicsFeatures_.multiview_ = false;
    }
    else {
        LogMessage("+ VPAndRTArrayIndexFromAnyShaderFeedingRasterizer", GLVU_INFO);
        graphicsFeatures_.multiview_ = true;
    }

#ifdef GLVU_NVAPI
	auto nvStatus = NvAPI_Initialize();
	if (nvStatus == NVAPI_LIBRARY_NOT_FOUND)
	{
		LogMessage("- NVAPI", GLVU_ERROR);
		NvAPI_ShortString str;
	}
	else
	{
		LogMessage("+ NVAPI", GLVU_INFO);
		graphicsFeatures_.nvapi_ = true;

		//Check VRS Support
		NV_D3D1x_GRAPHICS_CAPS nvcaps = {};
		if (NvAPI_D3D1x_GetGraphicsCapabilities(d3dDevice_, NV_D3D1x_GRAPHICS_CAPS_VER, &nvcaps) == NVAPI_OK)
		{
			if (nvcaps.bVariablePixelRateShadingSupported)
			{
				graphicsFeatures_.variableRateShading_ = true;
				LogMessage("+ NV VRS", GLVU_INFO);
			}
			else
				LogMessage("- NV VRS", GLVU_ERROR);
		}
	}
#endif

    D3D11_FEATURE_DATA_D3D11_OPTIONS1 featureData_11_2;
    
    hr = d3dDevice_->CheckFeatureSupport(D3D11_FEATURE_D3D11_OPTIONS1, &featureData_11_2, sizeof(featureData_11_2));
    if (FAILED(hr))
        LogMessage("- Level 11.3", GLVU_ERROR);
    else
    {
        if (featureData_11_2.TiledResourcesTier == D3D11_TILED_RESOURCES_NOT_SUPPORTED)
            LogMessage("- D3D11_TILED_RESOURCES_NOT_SUPPORTED", GLVU_ERROR);
#define T(V) if (featureData_11_2.TiledResourcesTier == V) LogMessage("+ " #V, GLVU_INFO);
        T(D3D11_TILED_RESOURCES_TIER_1);
        T(D3D11_TILED_RESOURCES_TIER_2);
#undef T
    }


	// Create sampler states
    static D3D11_FILTER filterModes[] = {
        D3D11_FILTER_MIN_MAG_MIP_POINT,				// FILTER_POINT
        D3D11_FILTER_MIN_MAG_LINEAR_MIP_POINT,		// FILTER_LINEAR
        D3D11_FILTER_MIN_MAG_MIP_LINEAR,			// FILTER_TRILINEAR
        D3D11_FILTER_ANISOTROPIC,					// FILTER_ANISOTROPIC
        D3D11_FILTER_COMPARISON_MIN_MAG_MIP_POINT,	// FILTER_SHADOW
    };
    for (int i = 0; i < COUNT_TEXTURE_FILTER; ++i)
    {
		D3D11_SAMPLER_DESC clampDesc, wrapDesc;
		ZeroMemory(&clampDesc, sizeof(clampDesc));
		ZeroMemory(&wrapDesc, sizeof(wrapDesc));

		clampDesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
		clampDesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
		clampDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
		clampDesc.Filter = filterModes[i];

		wrapDesc.AddressU = D3D11_TEXTURE_ADDRESS_WRAP;
		wrapDesc.AddressV = D3D11_TEXTURE_ADDRESS_WRAP;
		wrapDesc.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
		wrapDesc.Filter = filterModes[i];

        if (i == FILTER_ANISOTROPIC)
        {
			clampDesc.MaxAnisotropy = 4.0f;
			wrapDesc.MaxAnisotropy = 4.0f;
        }

        if (i == FILTER_SHADOW)
        {
			clampDesc.Filter = D3D11_FILTER_COMPARISON_MIN_MAG_MIP_LINEAR;
			wrapDesc.Filter = D3D11_FILTER_COMPARISON_MIN_MAG_MIP_LINEAR;
			clampDesc.ComparisonFunc = D3D11_COMPARISON_LESS_EQUAL;
			wrapDesc.ComparisonFunc = D3D11_COMPARISON_LESS_EQUAL;
        }

		auto hr = d3dDevice_->CreateSamplerState(&clampDesc, &clampSamplers_[i]);
		if (FAILED(hr))
		{
			DXERROR("Failed to create sampler state");
		}

		hr = d3dDevice_->CreateSamplerState(&wrapDesc, &wrapSamplers_[i]);
		if (FAILED(hr))
		{
			DXERROR("Failed to create sampler state");
		}
    }

    CreateDefaultObjects();

    backbuffer_.reset(new FrameBuffer(this, vector<shared_ptr<Texture> >{ }));

    uboCache_.reset(new ScratchBufferCache(this));

	dx11StateCache_->CreateDeviceObjects();

    return true;
}

//****************************************************************************
//
//  Function:   GraphicsDevice::InitSurface
//
//  Purpose:    Construct swap chain for an HWND.
//
//****************************************************************************
void GraphicsDevice::InitSurface(uint32_t width, uint32_t height, HWND wnd)
{
    throw "deprecated";
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
    //??
}

//****************************************************************************
//
//  Function:   GraphicsDevice::OnResize
//
//  Purpose:    Updates the *hack* backbuffer.
//
//	WARNING:	It's important to forcibly call FrameBuffer::Release on the backbuffer
//				here in order to release all handles to the swap-buffer provided.
//
//****************************************************************************
void GraphicsDevice::OnResize(uint32_t width, uint32_t height)
{
	
}

//****************************************************************************
//
//  Function:   GraphicsDevice::BeginFrame
//
//  Purpose:    Sets us back up to the backbuffer.
//
//****************************************************************************
void GraphicsDevice::BeginFrame()
{
    dx_FlushLateRelease();
    for (int i = 0; i < STAT_COUNT; ++i)
        stats_[i] = 0;
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
	//swapChain_->Present(0, 0);
	uboCache_->FrameFinished();
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
    
	if (traits.autoMip_)
		traits.mips_ = Texture::NumLevels(traits.width_, traits.height_, traits.depth_);
    tex->traits_ = traits;

    const auto format = dx_InternalFormat[traits.format_];
               
    if (traits.kind_ == TextureBuffer)
    {
        //TODO
    }
    else
    {
        if (tex->GetTextureKind() == Texture2D || tex->GetTextureKind() == TextureCube || tex->GetTextureKind() == Texture2DArray)
        {
			D3D11_TEXTURE2D_DESC desc;
			ZeroMemory(&desc, sizeof(desc));
			desc.MipLevels = std::max(tex->traits_.mips_, 1u);
			desc.ArraySize = tex->traits_.layers_;
			
            desc.SampleDesc.Count = tex->traits_.samples_;
            desc.SampleDesc.Quality = 0;
            if (tex->traits_.samples_ > 1) 
                desc.SampleDesc.Quality = 0xFFFFFFFF;

			desc.Width = tex->GetWidth();
			desc.Height = tex->GetHeight();
			desc.Format = format;
			desc.Usage = D3D11_USAGE_DEFAULT;
			desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

            // don't UAV bind a MSAA texture.
			if (dx_FormatCanUAV[tex->GetFormat()] && tex->traits_.samples_ == 1)
				desc.BindFlags |= D3D11_BIND_UNORDERED_ACCESS;

			if (tex->traits_.colorAttachment_)
				desc.BindFlags |= D3D11_BIND_RENDER_TARGET | D3D11_BIND_UNORDERED_ACCESS;
			else if (tex->traits_.depthAttachment_)
				desc.BindFlags |= D3D11_BIND_DEPTH_STENCIL;
			
			if (tex->GetTextureKind() == TextureCube)
				desc.MiscFlags |= D3D11_RESOURCE_MISC_TEXTURECUBE;
			
			if (tex->traits_.autoMip_)
			{
				desc.MiscFlags |= D3D11_RESOURCE_MISC_GENERATE_MIPS;
				desc.BindFlags |= D3D11_BIND_RENDER_TARGET;
			}

			auto hr = d3dDevice_->CreateTexture2D(&desc, nullptr, (ID3D11Texture2D**)(&tex->texture_));
			if (FAILED(hr))
			{

                if (tex->GetTextureKind() == Texture2D) { DXERROR("Failed to create Texture2D"); }
				if (tex->GetTextureKind() == Texture2DArray) { DXERROR("Failed to create Texture2DArray"); }
				if (tex->GetTextureKind() == TextureCube) { DXERROR("Failed to create TextureCube"); }
				if (tex->GetTextureKind() == TextureCubeArray) { DXERROR("Failed to create TextureCubeArray"); }
                if (tex) tex->Release();
				return nullptr;
			}

			D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc;
			ZeroMemory(&srvDesc, sizeof(srvDesc));
			srvDesc.Format = dx_SRVInternalFormat[tex->GetFormat()];
			if (tex->GetTextureKind() == Texture2D)
			{
				srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
				srvDesc.Texture2D.MipLevels = tex->GetMips();
			}
			else if (tex->GetTextureKind() == TextureCube)
			{
				srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURECUBE;
				srvDesc.TextureCube.MipLevels = tex->GetMips();
			}
			else if (tex->GetTextureKind() == Texture2DArray)
			{
				srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2DARRAY;
				srvDesc.Texture2DArray.ArraySize = tex->GetLayers();
				srvDesc.Texture2DArray.FirstArraySlice = 0;
				srvDesc.Texture2DArray.MipLevels = tex->GetMips();
			}

			hr = d3dDevice_->CreateShaderResourceView(tex->texture_, &srvDesc, &tex->srv_);
			if (FAILED(hr))
			{
				if (tex->GetTextureKind() == Texture2D) { DXERROR("Failed to create SRV for Texture2D"); }
				if (tex->GetTextureKind() == Texture2DArray) { DXERROR("Failed to create SRV for Texture2DArray"); }
				if (tex->GetTextureKind() == TextureCube) { DXERROR("Failed to create SRV for TextureCube"); }
				if (tex->GetTextureKind() == TextureCubeArray) { DXERROR("Failed to create SRV for TextureCubeArray"); }
				return nullptr;
			}
        }
		else if (tex->GetTextureKind() == Texture3D)
		{
			D3D11_TEXTURE3D_DESC desc;
			ZeroMemory(&desc, sizeof(desc));
			desc.MipLevels = std::max(tex->traits_.mips_, 1u);
			desc.Width = tex->GetWidth();
			desc.Height = tex->GetHeight();
			desc.Depth = tex->GetDepth();
			desc.Format = format;
			desc.Usage = D3D11_USAGE_DEFAULT;
			desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
			
			auto hr = d3dDevice_->CreateTexture3D(&desc, nullptr, (ID3D11Texture3D**)&tex->texture_);
			if (FAILED(hr))
			{
				DXERROR("Failed to create Texture3D");
				return nullptr;
			}

			D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc;
			ZeroMemory(&srvDesc, sizeof(srvDesc));
			srvDesc.Format = format;
			srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE3D;
			srvDesc.Texture3D.MipLevels = tex->GetMips();

			hr = d3dDevice_->CreateShaderResourceView(tex->texture_, &srvDesc, &tex->srv_);
			if (FAILED(hr))
			{
				DXERROR("Failed to create SRV for Texture3D");
				return nullptr;
			}
		}
		else
		{
			LogMessage("Attempted to construct unsupported texture kind", GLVU_WARNING);
		}
    }

    if (tex && traits.samples_ > 1)
    {
        auto resolveTraits = traits;
        resolveTraits.samples_ = 1;
        tex->resolveTexture_ = CreateTexture(resolveTraits);
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
static const uint32_t dx_ImagePitch[] = {
	sizeof(char)*3, // TEX_RGB8
	sizeof(char) * 4, // TEX_RGBA8

};

void GraphicsDevice::UpdateTexture(Texture* tex, void* data, uint32_t width, uint32_t height, uint32_t depth, uint32_t mip, uint32_t layer)
{
    assert(tex);
    if (tex)
    {
        D3D11_BOX box;
		ZeroMemory(&box, sizeof(box));
		box.front = 0;
		box.left = 0;
		box.top = 0;
		box.right = width;
		box.bottom = height;
		box.back = std::max(depth, 1u);

		const unsigned rowSize = dx_RowDataSize(dx_GetInternalFormat(tex->GetFormat()), width);
		d3dContext_->UpdateSubresource(tex->texture_, D3D11CalcSubresource(mip, layer, tex->GetMips()), &box, data, rowSize, height * rowSize);
    }
}

//****************************************************************************
//
//  Function:   GraphicsDevice::UpdateSubTexture
//
//  Purpose:    Updates only a portion of a texture, handled as Pos + Size.
//
//****************************************************************************
void GraphicsDevice::UpdateSubTexture(Texture* tex, void* data, uint32_t x, uint32_t y, uint32_t z, uint32_t width, uint32_t height, uint32_t depth, uint32_t mip, uint32_t layer)
{
    assert(tex);
    if (tex)
    {
		D3D11_BOX box;
		ZeroMemory(&box, sizeof(box));
		box.front = x;
		box.left = y;
		box.top = z;
		box.right = width;
		box.bottom = height;
		box.back = depth + 1;

		const unsigned rowSize = dx_RowDataSize(dx_GetInternalFormat(tex->GetFormat()), width);
		d3dContext_->UpdateSubresource(tex->texture_, D3D11CalcSubresource(mip, layer, tex->GetMips()), &box, data, rowSize, 
            height * rowSize);
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

	for (auto t : textures)
	{
		if (!t->traits_.colorAttachment_ && !t->traits_.depthAttachment_)
		{
			LogMessage("Cannot create FrameBuffer object with non-renderable texture", GLVU_ERROR);
			return nullptr;
		}
	}

    // Constructor did it.

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

std::shared_ptr<Buffer> GraphicsDevice::CreateIndirectArgsBuffer()
{
    auto ret = shared_ptr<Buffer>(new Buffer(this, IndirectArgsBufferObject));
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

//****************************************************************************
//
//  Function:   GraphicsDevice::ExecuteCompute
//
//  Purpose:    API filler, just delegates to buffer.
//
//****************************************************************************
void GraphicsDevice::ExecuteCompute(const ComputeTask& task, bool block)
{
    ID3D11Buffer* cbuffers[7];
    ID3D11ShaderResourceView* srvs[16];
    ID3D11UnorderedAccessView* uavs[7];
    unsigned ctr[7];
    ZeroMemory(cbuffers, sizeof cbuffers);
    ZeroMemory(srvs, sizeof srvs);
    ZeroMemory(uavs, sizeof uavs);
    ZeroMemory(ctr, sizeof ctr);

    for (unsigned i = 0; i < task.constBuffers_.size(); ++i)
        cbuffers[task.constBuffers_[i].slot_] = task.constBuffers_[i].buffer_ ? task.constBuffers_[i].buffer_->buffer_ : nullptr;

    for (unsigned i = 0; i < task.readTextures_.size(); ++i)
        srvs[task.readTextures_[i].slot_] = task.readTextures_[i].texture_ ? task.readTextures_[i].texture_->srv_ : nullptr;

    for (unsigned i = 0; i < task.readBuffers_.size(); ++i)
        srvs[task.readBuffers_[i].slot_] = task.readBuffers_[i].buffer_ ? task.readBuffers_[i].buffer_->srv_ : nullptr;

    for (unsigned i = 0; i < task.writeTextures_.size(); ++i)
    {
        uavs[task.writeTextures_[i].bindSlot_] = task.writeTextures_[i].texture_->GetUAV(
            task.writeTextures_[i].layer_,
            task.writeTextures_[i].mip_
        );
    }

    for (unsigned i = 0; i < task.writeBuffers_.size(); ++i)
        uavs[task.writeBuffers_[i].slot_] = task.writeBuffers_[i].buffer_->GetUAV();

    // important, 
    auto fbo = dx11StateCache_->GetFBO();
    bool texInRT = false;
    if (fbo)
    {
        for (auto& t : task.readTextures_)
            texInRT |= fbo->UsesTexture(t.texture_);
        for (auto& t : task.writeTextures_)
            texInRT |= fbo->UsesTexture(t.texture_);
    }
    if (texInRT)
        dx11StateCache_->PushTargets(nullptr);
    
    d3dContext_->CSSetConstantBuffers(0, 7, cbuffers);
    d3dContext_->CSSetShaderResources(0, 16, srvs);
    d3dContext_->CSSetUnorderedAccessViews(0, 7, uavs, ctr);

    d3dContext_->CSSetShader(task.computeProgram_->GetShader<ID3D11ComputeShader>(), nullptr, 0);
    d3dContext_->Dispatch(task.dispatch_[0], task.dispatch_[1], task.dispatch_[2]);
    
    if (texInRT)
        dx11StateCache_->PopTargets();
}

}