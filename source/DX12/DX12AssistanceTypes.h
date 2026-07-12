#pragma once

#include <glvu.h>

#include <d3d12.h>
#include <D3D12MemAlloc.h>

#include <map>

#include <mutex>
#include <thread>

namespace GLVU
{

class CommandList;
class GraphicsDevice;
class ResourceStateTracker;

class Buffer;
class Texture;

// D3D12 doesn't take care of orphaning for us.
struct Orphan
{
    ID3D12Resource* resource_;
    D3D12MA::Allocation* alloc_;

    Orphan(Buffer*);
    Orphan(Texture*);
    Orphan(ID3D12Resource*, D3D12MA::Allocation*);
    Orphan(const Orphan&);

    void Release();
};

struct OrphanList : private std::vector<Orphan>
{
    void Add(const Orphan& o);
    void Flush();
private:
    std::mutex lock_;
};

class RootSignature : public GPUObject
{
public:
    // TODO: Add (deep) copy/move constructors and assignment operators!
    RootSignature(GraphicsDevice*);
    RootSignature(
        GraphicsDevice*,
        const D3D12_ROOT_SIGNATURE_DESC1& rootSignatureDesc,
        D3D_ROOT_SIGNATURE_VERSION rootSignatureVersion
    );

    virtual ~RootSignature();

    void Release();

    Microsoft::WRL::ComPtr<ID3D12RootSignature> GetRootSignature() const
    {
        return m_RootSignature;
    }

    void SetRootSignatureDesc(
        const D3D12_ROOT_SIGNATURE_DESC1& rootSignatureDesc,
        D3D_ROOT_SIGNATURE_VERSION rootSignatureVersion
    );

    const D3D12_ROOT_SIGNATURE_DESC1& GetRootSignatureDesc() const
    {
        return m_RootSignatureDesc;
    }

    uint32_t GetDescriptorTableBitMask(D3D12_DESCRIPTOR_HEAP_TYPE descriptorHeapType) const;
    uint32_t GetNumDescriptors(uint32_t rootIndex) const;

protected:

private:
    D3D12_ROOT_SIGNATURE_DESC1 m_RootSignatureDesc;
    Microsoft::WRL::ComPtr<ID3D12RootSignature> m_RootSignature;

    // Need to know the number of descriptors per descriptor table.
    // A maximum of 32 descriptor tables are supported (since a 32-bit
    // mask is used to represent the descriptor tables in the root signature.
    uint32_t m_NumDescriptorsPerTable[32];

    // A bit mask that represents the root parameter indices that are 
    // descriptor tables for Samplers.
    uint32_t m_SamplerTableBitMask;
    // A bit mask that represents the root parameter indices that are 
    // CBV, UAV, and SRV descriptor tables.
    uint32_t m_DescriptorTableBitMask;
};

class CommandQueue : public GPUObject
{
public:
    CommandQueue(GraphicsDevice*, D3D12_COMMAND_LIST_TYPE type);
    ~CommandQueue();

    virtual void Release() override;

    std::shared_ptr<CommandList> CreateCmdList();
    uint64_t ExecuteCommandLists(CommandList*, bool wantFence);
    void WaitForFence(uint64_t fenceValue);

private:
    ID3D12CommandQueue* queue_;
    ID3D12Fence* fence_;
    uint64_t fenceValue_ = 0;
    HANDLE fenceEvent_;
    D3D12_COMMAND_LIST_TYPE type_;
};

class CommandList : public GPUObject
{
protected:
    friend class CommandQueue;
    CommandList(GraphicsDevice*, ID3D12GraphicsCommandList*, ResourceStateTracker* tracker);
public:

    ID3D12GraphicsCommandList* GetNative() { return cmdList_; }

    void CopyBuffer(Buffer* src, uint32_t srcOffset, Buffer* dest, uint32_t destOffset, uint32_t size);
    void CopyTexture(Texture* src, uint32_t srcOffset, Texture* dest, uint32_t destOffset, uint32_t size);

    void SetVertexBuffer(Buffer*, uint32_t slot);
    void SetVertexBuffers(const std::vector<Buffer*>);
    void SetIndexBuffer(Buffer*);
    void SetConstantBuffer(Buffer*, uint32_t slot);
    
    void SetDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE, ID3D12DescriptorHeap*);

private:
    void BindDescriptorHeaps();

    ID3D12GraphicsCommandList* cmdList_;
    ResourceStateTracker* tracker_;
    ID3D12DescriptorHeap* heaps_[D3D12_DESCRIPTOR_HEAP_TYPE::D3D12_DESCRIPTOR_HEAP_TYPE_NUM_TYPES] = { 0 };
};

class ResourceStateTracker
{
public:

    void Track(GPUObject*, D3D12_RESOURCE_STATES initialState);
    void Remove(GPUObject*);

    void Transition(GPUObject* object, uint64_t subResource, D3D12_RESOURCE_STATES toState);

    void Flush(ID3D12GraphicsCommandList* cmdList);

private:
    std::map<GPUObject*, D3D12_RESOURCE_STATES> stateTable_;
    std::vector<D3D12_RESOURCE_BARRIER> pending_;
    std::mutex lock_;
};

}
