#include "DX12AssistanceTypes.h"

#include "Buffer.h"
#include "Texture.h"
#include "GraphicsDevice.h"

#include "DX12DescriptorManagement.h"

#include <d3dx12.h>

#include <intsafe.h>

namespace GLVU
{

Orphan::Orphan(Buffer* b) :
    resource_(b->buffer_),
    alloc_(b->bufferMem_)
{

}
Orphan::Orphan(Texture* t) :
    resource_(t->texture_),
    alloc_(t->textureMem_)
{

}

Orphan::Orphan(ID3D12Resource* r, D3D12MA::Allocation* a) :
    resource_(r),
    alloc_(a)
{

}

Orphan::Orphan(const Orphan& r) :
    resource_(r.resource_),
    alloc_(r.alloc_)
{

}

void Orphan::Release()
{
    resource_->Release();
    resource_ = nullptr;
    alloc_->Release();
    alloc_ = nullptr;
}


void OrphanList::Add(const Orphan& o)
{
    std::lock_guard<std::mutex> g(lock_);
    push_back(o);
}

void OrphanList::Flush()
{
    std::lock_guard<std::mutex> g(lock_);
    for (auto& o : *this)
        o.Release();
    clear();
}

RootSignature::RootSignature(GraphicsDevice* device)
    : GPUObject(device)
    , m_RootSignatureDesc{}
    , m_NumDescriptorsPerTable{ 0 }
    , m_SamplerTableBitMask(0)
    , m_DescriptorTableBitMask(0)
{}

RootSignature::RootSignature(
    GraphicsDevice* device,
    const D3D12_ROOT_SIGNATURE_DESC1& rootSignatureDesc, D3D_ROOT_SIGNATURE_VERSION rootSignatureVersion)
    : GPUObject(device)
    , m_RootSignatureDesc{}
    , m_NumDescriptorsPerTable{ 0 }
    , m_SamplerTableBitMask(0)
    , m_DescriptorTableBitMask(0)
{
    SetRootSignatureDesc(rootSignatureDesc, rootSignatureVersion);
}

RootSignature::~RootSignature()
{
    Release();
}

void RootSignature::Release()
{
    for (UINT i = 0; i < m_RootSignatureDesc.NumParameters; ++i)
    {
        const D3D12_ROOT_PARAMETER1& rootParameter = m_RootSignatureDesc.pParameters[i];
        if (rootParameter.ParameterType == D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE)
            delete[] rootParameter.DescriptorTable.pDescriptorRanges;
    }

    if (m_RootSignatureDesc.pParameters)
        delete[] m_RootSignatureDesc.pParameters;
    m_RootSignatureDesc.pParameters = nullptr;
    m_RootSignatureDesc.NumParameters = 0;

    if (m_RootSignatureDesc.pStaticSamplers)
        delete[] m_RootSignatureDesc.pStaticSamplers;
    m_RootSignatureDesc.pStaticSamplers = nullptr;
    m_RootSignatureDesc.NumStaticSamplers = 0;

    m_DescriptorTableBitMask = 0;
    m_SamplerTableBitMask = 0;

    memset(m_NumDescriptorsPerTable, 0, sizeof(m_NumDescriptorsPerTable));
}

void RootSignature::SetRootSignatureDesc(
    const D3D12_ROOT_SIGNATURE_DESC1& rootSignatureDesc,
    D3D_ROOT_SIGNATURE_VERSION rootSignatureVersion
)
{
    // Make sure any previously allocated root signature description is cleaned 
    // up first.
    Release();

    auto device = device_->GetD3D12();

    UINT numParameters = rootSignatureDesc.NumParameters;
    D3D12_ROOT_PARAMETER1* pParameters = numParameters > 0 ? new D3D12_ROOT_PARAMETER1[numParameters] : nullptr;

    for (UINT i = 0; i < numParameters; ++i)
    {
        const D3D12_ROOT_PARAMETER1& rootParameter = rootSignatureDesc.pParameters[i];
        pParameters[i] = rootParameter;

        if (rootParameter.ParameterType == D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE)
        {
            UINT numDescriptorRanges = rootParameter.DescriptorTable.NumDescriptorRanges;
            D3D12_DESCRIPTOR_RANGE1* pDescriptorRanges = numDescriptorRanges > 0 ? new D3D12_DESCRIPTOR_RANGE1[numDescriptorRanges] : nullptr;

            memcpy(pDescriptorRanges, rootParameter.DescriptorTable.pDescriptorRanges,
                sizeof(D3D12_DESCRIPTOR_RANGE1) * numDescriptorRanges);

            pParameters[i].DescriptorTable.NumDescriptorRanges = numDescriptorRanges;
            pParameters[i].DescriptorTable.pDescriptorRanges = pDescriptorRanges;

            // Set the bit mask depending on the type of descriptor table.
            if (numDescriptorRanges > 0)
            {
                switch (pDescriptorRanges[0].RangeType)
                {
                case D3D12_DESCRIPTOR_RANGE_TYPE_CBV:
                case D3D12_DESCRIPTOR_RANGE_TYPE_SRV:
                case D3D12_DESCRIPTOR_RANGE_TYPE_UAV:
                    m_DescriptorTableBitMask |= (1 << i);
                    break;
                case D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER:
                    m_SamplerTableBitMask |= (1 << i);
                    break;
                }
            }

            // Count the number of descriptors in the descriptor table.
            for (UINT j = 0; j < numDescriptorRanges; ++j)
            {
                m_NumDescriptorsPerTable[i] += pDescriptorRanges[j].NumDescriptors;
            }
        }
    }

    m_RootSignatureDesc.NumParameters = numParameters;
    m_RootSignatureDesc.pParameters = pParameters;

    UINT numStaticSamplers = rootSignatureDesc.NumStaticSamplers;
    D3D12_STATIC_SAMPLER_DESC* pStaticSamplers = numStaticSamplers > 0 ? new D3D12_STATIC_SAMPLER_DESC[numStaticSamplers] : nullptr;

    if (pStaticSamplers)
    {
        memcpy(pStaticSamplers, rootSignatureDesc.pStaticSamplers,
            sizeof(D3D12_STATIC_SAMPLER_DESC) * numStaticSamplers);
    }

    m_RootSignatureDesc.NumStaticSamplers = numStaticSamplers;
    m_RootSignatureDesc.pStaticSamplers = pStaticSamplers;

    D3D12_ROOT_SIGNATURE_FLAGS flags = rootSignatureDesc.Flags;
    m_RootSignatureDesc.Flags = flags;

    CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC versionRootSignatureDesc;
    versionRootSignatureDesc.Init_1_1(numParameters, pParameters, numStaticSamplers, pStaticSamplers, flags);

    // Serialize the root signature.
    Microsoft::WRL::ComPtr<ID3DBlob> rootSignatureBlob;
    Microsoft::WRL::ComPtr<ID3DBlob> errorBlob;
    ThrowIfFailed(D3DX12SerializeVersionedRootSignature(&versionRootSignatureDesc,
        rootSignatureVersion, &rootSignatureBlob, &errorBlob));

    // Create the root signature.
    ThrowIfFailed(device->CreateRootSignature(0, rootSignatureBlob->GetBufferPointer(),
        rootSignatureBlob->GetBufferSize(), IID_PPV_ARGS(&m_RootSignature)));
}

uint32_t RootSignature::GetDescriptorTableBitMask(D3D12_DESCRIPTOR_HEAP_TYPE descriptorHeapType) const
{
    uint32_t descriptorTableBitMask = 0;
    switch (descriptorHeapType)
    {
    case D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV:
        descriptorTableBitMask = m_DescriptorTableBitMask;
        break;
    case D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER:
        descriptorTableBitMask = m_SamplerTableBitMask;
        break;
    }

    return descriptorTableBitMask;
}

uint32_t RootSignature::GetNumDescriptors(uint32_t rootIndex) const
{
    assert(rootIndex < 32);
    return m_NumDescriptorsPerTable[rootIndex];
}


CommandQueue::CommandQueue(GraphicsDevice* device, D3D12_COMMAND_LIST_TYPE type) :
    GPUObject(device),
    type_(type)
{
    D3D12_COMMAND_QUEUE_DESC queueDesc = { };
    queueDesc.Type = type;
    queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
    queueDesc.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;
    queueDesc.NodeMask = 0;

    queue_ = nullptr;
    auto hr = device->GetD3D12()->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&queue_));
    if (hr != S_OK)
    {
        queue_ = nullptr;
    }

    Microsoft::WRL::ComPtr<ID3D12CommandQueue> queue(queue_);

    device->GetD3D12()->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence_));
    fenceEvent_ = ::CreateEvent(NULL, FALSE, FALSE, NULL);
}

CommandQueue::~CommandQueue() 
{
    Release();
}

void CommandQueue::Release()
{
    if (queue_)
        queue_->Release();
    queue_ = nullptr;
}

std::shared_ptr<CommandList> CommandQueue::CreateCmdList()
{
    ID3D12CommandList* list;
    auto hr = device_->GetD3D12()->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, nullptr, nullptr, IID_PPV_ARGS(&list));
    if (hr != S_OK)
    {
    }
    return std::make_shared<CommandList>(device_, list, nullptr);
}

uint64_t CommandQueue::ExecuteCommandLists(CommandList* cmdList, bool wantFence)
{
    ID3D12CommandList* l = cmdList->GetNative();
    queue_->ExecuteCommandLists(1, &l);
    queue_->Signal(fence_, ++fenceValue_);
    return fenceValue_;
}

void CommandQueue::WaitForFence(uint64_t fenceValue)
{
    if (!(fence_->GetCompletedValue() >= fenceValue_))
    {
        fence_->SetEventOnCompletion(fenceValue, fenceEvent_);
        ::WaitForSingleObject(fenceEvent_, DWORD_MAX);
    }
}

CommandList::CommandList(GraphicsDevice* device, ID3D12GraphicsCommandList* list, ResourceStateTracker* tracker) :
    GPUObject(device),
    cmdList_(list),
    tracker_(tracker)
{
    
}

void CommandList::CopyBuffer(Buffer* src, uint32_t srcOffset, Buffer* dest, uint32_t destOffset, uint32_t size)
{
    tracker_->Transition(src, 0, D3D12_RESOURCE_STATE_COPY_SOURCE);
    tracker_->Transition(dest, 0, D3D12_RESOURCE_STATE_COPY_DEST);
    tracker_->Flush(cmdList_);

    cmdList_->CopyBufferRegion(dest->buffer_, destOffset, src->buffer_, srcOffset, size);

    tracker_->Transition(src, 0, D3D12_RESOURCE_STATE_COMMON);
    tracker_->Transition(dest, 0, D3D12_RESOURCE_STATE_COMMON);
    tracker_->Flush(cmdList_);
}

void CommandList::CopyTexture(Texture* src, uint32_t srcOffset, Texture* dest, uint32_t destOffset, uint32_t size)
{
    tracker_->Transition(src, 0, D3D12_RESOURCE_STATE_COPY_SOURCE);
    tracker_->Transition(dest, 0, D3D12_RESOURCE_STATE_COPY_DEST);

    D3D12_SUBRESOURCE_DATA data;
    data.pData = nullptr;
    data.RowPitch = 0;
    data.SlicePitch = 0;
    UpdateSubresources<1>(cmdList_, dest->texture_, src->texture_, 0, 0, 1, &data);

    tracker_->Transition(src, 0, D3D12_RESOURCE_STATE_COMMON);
    tracker_->Transition(dest, 0, D3D12_RESOURCE_STATE_COMMON);
}

void CommandList::SetVertexBuffer(Buffer* buff, uint32_t slot)
{
    tracker_->Transition(buff, 0, D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER);

    D3D12_VERTEX_BUFFER_VIEW view = { };
    view.BufferLocation = buff->buffer_->GetGPUVirtualAddress();
    view.SizeInBytes = buff->GetSize();
    view.StrideInBytes = buff->GetStride();
    cmdList_->IASetVertexBuffers(slot, 1, &view);
}

void CommandList::SetVertexBuffers(const std::vector<Buffer*> buffs)
{
    std::vector<D3D12_VERTEX_BUFFER_VIEW> views;
    for (auto& b : buffs)
    {
        D3D12_VERTEX_BUFFER_VIEW view = { };
        view.BufferLocation = b->buffer_->GetGPUVirtualAddress();
        view.SizeInBytes = b->GetSize();
        view.StrideInBytes = b->GetStride();
    }
    cmdList_->IASetVertexBuffers(0, views.size(), views.data());
}

void CommandList::SetIndexBuffer(Buffer* buff)
{
    D3D12_INDEX_BUFFER_VIEW view = { };
    if (buff)
    {
        view.BufferLocation = buff->buffer_->GetGPUVirtualAddress();
        view.Format = buff->HasTag(BufferTag_32Bit) ? DXGI_FORMAT_R32_UINT : DXGI_FORMAT_R16_UINT;
        view.SizeInBytes = buff->GetSize();
    }
    cmdList_->IASetIndexBuffer(buff ? &view : nullptr);
}

void CommandList::SetConstantBuffer(Buffer*, uint32_t slot)
{
    ID3D12PipelineState* state;
    D3D12_GRAPHICS_PIPELINE_STATE_DESC desc = { };
    
    
}

void CommandList::SetDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE type, ID3D12DescriptorHeap* heap) {
    
    if (heaps_[type] != heap)
    {
        heaps_[type] = heap;
        BindDescriptorHeaps();
    }
}

void CommandList::BindDescriptorHeaps()
{
    UINT numDescriptorHeaps = 0;
    ID3D12DescriptorHeap* descriptorHeaps[D3D12_DESCRIPTOR_HEAP_TYPE_NUM_TYPES] = {};

    for (uint32_t i = 0; i < D3D12_DESCRIPTOR_HEAP_TYPE_NUM_TYPES; ++i)
    {
        ID3D12DescriptorHeap* descriptorHeap = heaps_[i];
        if (descriptorHeap)
            descriptorHeaps[numDescriptorHeaps++] = descriptorHeap;
    }

    cmdList_->SetDescriptorHeaps(numDescriptorHeaps, descriptorHeaps);
}

void ResourceStateTracker::Track(GPUObject* obj, D3D12_RESOURCE_STATES initialState)
{
    stateTable_[obj] = initialState;
}

void ResourceStateTracker::Remove(GPUObject* obj)
{
    stateTable_.erase(obj);
}

void ResourceStateTracker::Transition(GPUObject* object, uint64_t subResource, D3D12_RESOURCE_STATES toState)
{
    std::lock_guard<std::mutex> lock(lock_);

    auto curState = stateTable_[object];
    if (curState == toState)
        return;

    D3D12_RESOURCE_BARRIER barr = { };
    barr.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    if (auto b = dynamic_cast<Buffer*>(object))
        barr.Transition.pResource = b->buffer_;
    else if (auto t = dynamic_cast<Texture*>(object))
        barr.Transition.pResource = t->texture_;

    barr.Transition.StateBefore = curState;
    barr.Transition.StateAfter = toState;
    barr.Transition.Subresource = subResource;

    stateTable_[object] = toState;
    pending_.push_back(barr);
}

void ResourceStateTracker::Flush(ID3D12GraphicsCommandList* cmdList)
{
    if (pending_.size())
    {
        cmdList->ResourceBarrier(pending_.size(), pending_.data());
        pending_.clear();
    }
}

class CommandEncoder
{
    enum CMD {
        CMD_CopyBuffer,
        CMD_Transition,
        CMD_UAVBarrier,

        CMD_SetRenderTarget,

        CMD_SetPipeline,
        CMD_SetConstantBuffer,
        CMD_SetShaderResource,
        CMD_SetUAV,

        CMD_SetViewport,
        CMD_SetScissor,
        CMD_SetTopology,

        CMD_SetVertexBuffer,
        CMD_SetIndexBuffer,

        CMD_DrawIndexed,
        CMD_DrawIndexedInstanced,
        CMD_DrawIndexedIndirect,
        CMD_DrawIndexedInstancedIndirect,
    };


    struct CopyBuffer {
        Buffer* src;
        Buffer* dest;
        uint64_t srcOffset;
        uint64_t destOffset;
        uint64_t size;
    };
    struct Transition {
        Buffer* buff;
        Texture* tex;
        D3D12_RESOURCE_STATES toState;
        D3D12_RESOURCE_STATES fromState;
    };

    struct Command {
        CMD type_;
        union {
            CopyBuffer buffCopy_;

        };
    };

    void Execute()
    {
        ID3D12GraphicsCommandList* cmdList;

        std::vector<Command> commands;
        for (auto& cmd : commands)
        {
            switch (cmd.type_)
            {
            case CMD_Transition: {

            } break;
            case CMD_CopyBuffer: {
                cmdList->CopyBufferRegion(cmd.buffCopy_.src->buffer_, cmd.buffCopy_.srcOffset, cmd.buffCopy_.dest->buffer_, cmd.buffCopy_.destOffset, cmd.buffCopy_.size);
            } break;
            }
        }
    }
};

}