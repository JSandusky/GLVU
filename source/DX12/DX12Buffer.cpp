#include "Buffer.h"

#include "GraphicsDevice.h"

#include <intsafe.h> //DWORD_MAX

namespace GLVU
{

Buffer::Buffer(GraphicsDevice* device, BufferKind kind) :
    GPUObject(device),
    size_(0),
    buffer_(nullptr),
    bufferMem_(nullptr),
    kind_(kind),
    shadowed_(false),
    shadowDirty_(false),
    tags_(0)
{

}

Buffer::~Buffer()
{
    Release();
}

void Buffer::Release()
{
    if (buffer_)
        buffer_->Release();
    buffer_ = nullptr;

    if (bufferMem_)
        bufferMem_->Release();
    bufferMem_ = nullptr;
}

bool Buffer::IsValid() const
{
    return buffer_ != nullptr && bufferMem_ != nullptr;
}

void Buffer::SetData(void* data, uint32_t size)
{
    SetSubData(data, 0, size);
}

void Buffer::SetSubData(void* data, uint32_t offset, uint32_t size)
{
    if (offset + size > size_)
    {
        Release();
        SetSize(offset + size);
    }

    auto cmdList = device_->GetGraphicsCmdBuffer();

    if (HasTag(BufferTag_Dynamic))
    {
        D3D12_RANGE rdRange = { 0, 0 };
        D3D12_RANGE writeRange = { offset, offset + size };
        void* d = nullptr;
        if (buffer_->Map(0, &rdRange, &d) == S_OK)
        {
            memcpy(d, data, size);
            buffer_->Unmap(0, &writeRange);
        }
        else
        {
            device_->LogMessage("Failed to map buffer", GLVU_ERROR);
            return;
        }
    }
    else
    {
        auto alloc = GetDevice()->GetAlloc();

        

        auto stagingBuff = device_->CreateVertexBuffer();
        stagingBuff->SetTag(BufferTag_Dynamic);
        stagingBuff->SetData(data, size);

        D3D12_RESOURCE_BARRIER barr[2] = { { }, { } };
        barr[0].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barr[0].Transition.pResource = buffer_;
        barr[0].Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
        barr[0].Transition.Subresource = 0;

        barr[1].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barr[1].Transition.pResource = stagingBuff->buffer_;
        barr[1].Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
        barr[1].Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
        barr[1].Transition.Subresource = 0;

        cmdList->ResourceBarrier(2, barr);
        cmdList->CopyBufferRegion(buffer_, 0, stagingBuff->buffer_, 0, size);

        barr[0].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barr[0].Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
        barr[0].Transition.StateAfter = D3D12_RESOURCE_STATE_COMMON;
        cmdList->ResourceBarrier(1, barr);
    }
}

void Buffer::SetSize(uint32_t size)
{
    if (size_ == size)
        return;

    auto alloc = GetDevice()->GetAlloc();

    size_ = size;

    D3D12MA::ALLOCATION_DESC allocDesc = { };
    allocDesc.HeapType = HasTag(BufferTag_Dynamic) | HasTag(BufferTag_Staging) ? D3D12_HEAP_TYPE_UPLOAD : D3D12_HEAP_TYPE_DEFAULT;
    if (HasTag(BufferTag_Readback))
        allocDesc.HeapType = D3D12_HEAP_TYPE_READBACK;

    D3D12_RESOURCE_DESC resDesc = { };
    resDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    resDesc.Alignment = 0;
    resDesc.Width = size_;
    resDesc.Height = 1;
    resDesc.DepthOrArraySize = 1;
    resDesc.MipLevels = 1;
    resDesc.Format = kind_ == IndexBufferObject ? (HasTag(BufferTag_32Bit) ? DXGI_FORMAT_R32_UINT : DXGI_FORMAT_R16_UINT) : DXGI_FORMAT_UNKNOWN;
    resDesc.SampleDesc.Count = 1;
    resDesc.SampleDesc.Quality = 0;
    resDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    resDesc.Flags = HasTag(BufferTag_Compute) || kind_ == ShaderDataBufferObject ? D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS : D3D12_RESOURCE_FLAG_NONE;

    auto hr = alloc->CreateResource(&allocDesc, &resDesc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, &bufferMem_, IID_PPV_ARGS(&buffer_));
    if (hr != S_OK)
    {
        if (buffer_)
            buffer_->Release();
        buffer_ = nullptr;
        return;
    }
}

Blob Buffer::GetGPUData() const
{
    if (buffer_)
    {
        auto readBuff = device_->CreateVertexBuffer();
        readBuff->SetTag(BufferTag_Readback);
        readBuff->SetSize(size_);

        auto cmdList = device_->GetGraphicsCmdBuffer();
        
        D3D12_RESOURCE_BARRIER barr;
        barr.Transition.pResource = buffer_;
        barr.Transition.StateBefore = D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER;
        barr.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
        barr.Transition.Subresource = 0;

        cmdList->ResourceBarrier(1, &barr);
        cmdList->CopyBufferRegion(readBuff->buffer_, 0, buffer_, 0, size_);
        cmdList->ResourceBarrier(1, &barr);

        ID3D12Fence* fence = nullptr;
        device_->GetD3D12()->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence));

        device_->GetCmdQueue()->ExecuteCommandLists(1, (ID3D12CommandList**)&cmdList);
        device_->GetCmdQueue()->Signal(fence, 1);
        
        if (fence->GetCompletedValue() != 1)
        {
            auto event = ::CreateEvent(NULL, FALSE, FALSE, NULL);
            assert(event && "Failed to create fence event handle.");

            // Is this function thread safe?
            fence->SetEventOnCompletion(1, event);
            ::WaitForSingleObject(event, DWORD_MAX);

            ::CloseHandle(event);
        }
    }
    return Blob(nullptr, 0, false);
}

}