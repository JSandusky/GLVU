
#include "GraphicsDevice.h"

#include <d3d12.h>
#include <dxgi1_4.h>

using namespace std;

namespace GLVU
{

GraphicsDevice::GraphicsDevice() :
    effectCache_(this)
{

}

GraphicsDevice::~GraphicsDevice()
{

}

bool GraphicsDevice::OpenDevice(const char** requiredExt, uint32_t)
{
#define CHECK_HR(V) if ((V) != S_OK) { return false; }
#define SUCCESS(V) ((V) == S_OK)

    IDXGIFactory4* dxgiFactory;
    
    CHECK_HR(CreateDXGIFactory1(IID_PPV_ARGS(&dxgiFactory)));

    IDXGIAdapter1* adapter = nullptr;
    uint32_t adapterIndex = 0;
    bool adapterFound = false;

    while (dxgiFactory->EnumAdapters1(adapterIndex, &adapter) != DXGI_ERROR_NOT_FOUND)
    {
        DXGI_ADAPTER_DESC1 desc;
        adapter->GetDesc1(&desc);

        if ((desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) == 0)
        {
            HRESULT hr = D3D12CreateDevice(adapter, D3D_FEATURE_LEVEL_12_0, _uuidof(ID3D12Device), nullptr);
            if (SUCCEEDED(hr))
            {
                adapterFound = true;
                break;
            }
        }
        adapter->Release();
        adapterIndex++;
    }
    if (!adapterFound)
        return false;

    if (true)
    {
        ID3D12Debug* debug;
        if (SUCCESS(D3D12GetDebugInterface(IID_PPV_ARGS(&debug))))
        {
            debug->EnableDebugLayer();
        }
    }

    device_ = nullptr;
    CHECK_HR(D3D12CreateDevice(adapter, D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(&device_)));

    {
        D3D12MA::ALLOCATOR_DESC desc = {};
        desc.Flags = D3D12MA::ALLOCATOR_FLAG_NONE;
        desc.pDevice = device_;
        desc.pAdapter = adapter;

        //if (false) // CPU allocation callbacks
        //{
        //    //??g_AllocationCallbacks.pAllocate = &CustomAllocate;
        //    //??g_AllocationCallbacks.pFree = &CustomFree;
        //    //??g_AllocationCallbacks.pUserData = CUSTOM_ALLOCATION_USER_DATA;
        //    //??desc.pAllocationCallbacks = &g_AllocationCallbacks;
        //}

        CHECK_HR(D3D12MA::CreateAllocator(&desc, &amdAllocator_));

        switch (amdAllocator_->GetD3D12Options().ResourceHeapTier)
        {
        case D3D12_RESOURCE_HEAP_TIER_1:
            wprintf(L"ResourceHeapTier = D3D12_RESOURCE_HEAP_TIER_1\n");
            break;
        case D3D12_RESOURCE_HEAP_TIER_2:
            wprintf(L"ResourceHeapTier = D3D12_RESOURCE_HEAP_TIER_2\n");
            break;
        default: { assert(0); }
        }
    }

    D3D12_COMMAND_QUEUE_DESC cqDesc = {}; // we will be using all the default values

    commandQueue_ = nullptr;
    CHECK_HR(device_->CreateCommandQueue(&cqDesc, IID_PPV_ARGS(&commandQueue_))); // create the command queue
}

shared_ptr<Texture> GraphicsDevice::CreateTexture(TextureTraits t)
{
    auto r = make_shared<Texture>(this);
    if (r->Create(t))
        return r;
    return nullptr;
}

}