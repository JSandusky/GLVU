#pragma once

#include <glvu.h>

#include <unordered_map>
#include <vector>

namespace GLVU
{

/// Stick it in a unique_ptr and reset it when you need to close the transition zone.
class ScopedBarrier
{
    ID3D12GraphicsCommandList* cmdList_;
    std::vector<D3D12_RESOURCE_BARRIER> barriers_;
public:
    ScopedBarrier(ID3D12GraphicsCommandList* list, const std::vector<D3D12_RESOURCE_BARRIER>& barriers) :
        cmdList_(list),
        barriers_(barriers)
    {
        if (barriers_.size())
            cmdList_->ResourceBarrier(barriers_.size(), barriers_.data());
    }

    ~ScopedBarrier()
    {
        if (barriers_.size())
        {
            // DirectXTK-12, nifty
            for (auto& b : barriers_)
                std::swap(b.Transition.StateAfter, b.Transition.StateBefore);
            cmdList_->ResourceBarrier(barriers_.size(), barriers_.data());
        }
    }
};

class PSOCache
{
public:
private:
    struct PSORecord {
        ID3D12PipelineState* pipelineState_;
    };

    std::unordered_map<uint32_t, PSORecord> cacheTable_;
};

// The intent is that orphanable's will be released at the start of each frame.
struct Orphanable
{
    ID3D12Resource* resource_;
    D3D12MA::Allocation* allocation_;
    uint64_t identifier_;

    void Release() {
        allocation_->GetResource();
        resource_->Release();
        allocation_ = nullptr;
        resource_ = nullptr;

        ID3D12GraphicsCommandList* list;
        ID3D12Device* device;

        D3D12_CPU_DESCRIPTOR_HANDLE handle;
        D3D12_SHADER_RESOURCE_VIEW_DESC view;
        device->CreateShaderResourceView(0, &view, handle);

        D3D12_DESCRIPTOR_HEAP_DESC desc = { };
        desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        desc.NumDescriptors = 12;

        ID3D12DescriptorHeap* heap;
        device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&heap));
    }
};


}