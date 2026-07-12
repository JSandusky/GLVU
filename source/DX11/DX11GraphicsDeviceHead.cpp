//****************************************************************************
//
//  File:       DX11GraphicsDeviceHead.cpp
//  License:    MIT
//  Project:    GLVU
//  Contents:   Implementation of rendering head for DX11
//
//****************************************************************************

#include "GraphicsDeviceHead.h"

#include "GraphicsDevice.h"
#include "Texture.h"

#include <vector>

using namespace std;

#define DXERROR(MESSAGE) GetDevice()->LogFormat(GLVU_ERROR, MESSAGE " HRESULT %X", hr)

#pragma optimize("", off)

namespace GLVU
{

//****************************************************************************
//
//  Function:   GraphicsDeviceHead::GraphicsDeviceHead
//
//  Purpose:    Constructor.
//
//****************************************************************************
GraphicsDeviceHead::GraphicsDeviceHead(GraphicsDevice* device, uint2 size, HWND window, intptr_t userData) :
    GPUObject(device),
    userData_(userData),
    backbufferWidth_(0),
    backbufferHeight_(0),
    beginCallback_(nullptr),
    endCallback_(nullptr),
    wnd_(window)
{
    Resize(size.x, size.y);
}

//****************************************************************************
//
//  Function:   GraphicsDeviceHead::Release
//
//  Purpose:    Disposes of contained resources.
//
//****************************************************************************
void GraphicsDeviceHead::Release()
{
    backbuffer_.reset();
    backbufferDepth_.reset();

    if (swapChain_)
        swapChain_->Release();
    swapChain_ = nullptr;
    swapChainBuffer_ = nullptr;
}

//****************************************************************************
//
//  Function:   GraphicsDeviceHead::Resize
//
//  Purpose:    Changes the swapchain buffer size, also creating the swapcahin
//              if it is null for some reason, and updates the GLVU side
//              backbuffer targets.
//
//****************************************************************************
void GraphicsDeviceHead::Resize(uint32_t w, uint32_t h)
{
    w = std::max(1u, w);
    h = std::max(1u, h);

    if (w == backbufferWidth_ && h == backbufferHeight_)
        return;

    if (swapChain_)
    {
        backbufferWidth_ = w;
        backbufferHeight_ = h;

        backbuffer_->Release();
        backbuffer_.reset();
        swapChain_->ResizeBuffers(1, backbufferWidth_, backbufferHeight_, DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH);
        CreateBackbuffers();
        return;
    }

    auto d3dDevice = GetDevice()->GetD3DDevice();

    IDXGIDevice * dxgiDevice = 0;
    d3dDevice->QueryInterface(__uuidof(IDXGIDevice), (void**)&dxgiDevice);

    IDXGIAdapter * dxgiAdapter = 0;
    dxgiDevice->GetParent(__uuidof(IDXGIAdapter), (void**)&dxgiAdapter);

    IDXGIFactory * dxgiFactory = 0;
    dxgiAdapter->GetParent(__uuidof(IDXGIFactory), (void**)&dxgiFactory);

    DXGI_SWAP_CHAIN_DESC swapChainDesc;
    ZeroMemory(&swapChainDesc, sizeof(swapChainDesc));
    swapChainDesc.OutputWindow = wnd_;

    swapChainDesc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    swapChainDesc.BufferDesc.Width = backbufferWidth_;
    swapChainDesc.BufferDesc.Height = backbufferHeight_;
    swapChainDesc.BufferDesc.Scaling = DXGI_MODE_SCALING_CENTERED;

    swapChainDesc.SampleDesc.Count = 1;
    swapChainDesc.SampleDesc.Quality = 0;
    swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
    swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    swapChainDesc.BufferCount = 1;
    swapChainDesc.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
    swapChainDesc.Windowed = TRUE;

    auto hr = dxgiFactory->CreateSwapChain(d3dDevice, &swapChainDesc, &swapChain_);
    if (FAILED(hr))
    {
        DXERROR("Failed to create swapchain");
        return;
    }

    backbufferWidth_ = w;
    backbufferHeight_ = h;
    CreateBackbuffers();
}

//****************************************************************************
//
//  Function:   GraphicsDeviceHead::CreateBackbuffers
//
//  Purpose:    Constructs the D3D11 resources and GLVU side resources required
//              for the current swapchain.
//
//****************************************************************************
void GraphicsDeviceHead::CreateBackbuffers()
{
    if (backbuffer_)
        backbuffer_->Release();

    backbuffer_.reset();

    auto d3dDevice = GetDevice()->GetD3DDevice();

    ID3D11Texture2D* backbuffer = nullptr;
    auto hr = swapChain_->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&backbuffer);
    if (FAILED(hr))
    {
        DXERROR("Failed to get backbuffer from swapchain");
        return;
    }

    ID3D11RenderTargetView* view = nullptr;
    hr = d3dDevice->CreateRenderTargetView(backbuffer, nullptr, &view);
    if (FAILED(hr))
    {
        DXERROR("Failed to construct render target for backbuffer");
        return;
    }

    TextureTraits depthBufferSettings = { };
    depthBufferSettings.depthAttachment_ = true;
    depthBufferSettings.format_ = TEX_DEPTH;
    depthBufferSettings.width_ = backbufferWidth_;
    depthBufferSettings.height_ = backbufferHeight_;

    backbufferDepth_ = GetDevice()->CreateTexture(depthBufferSettings);

    D3D11_DEPTH_STENCIL_VIEW_DESC depthStencilViewDesc;
    ZeroMemory(&depthStencilViewDesc, sizeof(depthStencilViewDesc));
    depthStencilViewDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
    depthStencilViewDesc.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
    depthStencilViewDesc.Texture2D.MipSlice = 0;

    ID3D11DepthStencilView* depthStencilView = nullptr;
    hr = d3dDevice->CreateDepthStencilView(backbufferDepth_->texture_, &depthStencilViewDesc, &depthStencilView);
    if (FAILED(hr))
    {
        DXERROR("Failed to construct depth-stencil view for swapchain");
        return;
    }

    backbuffer_.reset(new FrameBuffer(GetDevice(), vector<shared_ptr<Texture> > { }));
    backbuffer_->extraResources_.push_back(backbuffer);
    backbuffer_->FromBackBuffer(view, depthStencilView);
    backbuffer_->reportWidth_ = backbufferWidth_;
    backbuffer_->reportHeight_ = backbufferHeight_;
}

//****************************************************************************
//
//  Function:   GraphicsDeviceHead::Flush
//
//  Purpose:    Calls out to present, handling VSync as needed.
//
//****************************************************************************
void GraphicsDeviceHead::Flush()
{
    if (swapChain_)
        swapChain_->Present(vsync_ ? 1 : 0, 0);
}

}
