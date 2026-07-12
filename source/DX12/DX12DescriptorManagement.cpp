#include "DX12DescriptorManagement.h"

#include <glvu.h>

#include "GraphicsDevice.h"
#include "DX12AssistanceTypes.h"

#include <cassert>

using namespace GLVU;

#undef max

// ALLOCATION

DescriptorAllocation::DescriptorAllocation()
    : m_Descriptor{ 0 }
    , m_NumHandles(0)
    , m_DescriptorSize(0)
    , m_Page(nullptr)
{}

DescriptorAllocation::DescriptorAllocation(D3D12_CPU_DESCRIPTOR_HANDLE descriptor, uint32_t numHandles, uint32_t descriptorSize, std::shared_ptr<DescriptorAllocatorPage> page)
    : m_Descriptor(descriptor)
    , m_NumHandles(numHandles)
    , m_DescriptorSize(descriptorSize)
    , m_Page(page)
{}


DescriptorAllocation::~DescriptorAllocation()
{
    Free();
}

DescriptorAllocation::DescriptorAllocation(DescriptorAllocation&& allocation)
    : m_Descriptor(allocation.m_Descriptor)
    , m_NumHandles(allocation.m_NumHandles)
    , m_DescriptorSize(allocation.m_DescriptorSize)
    , m_Page(std::move(allocation.m_Page))
{
    allocation.m_Descriptor.ptr = 0;
    allocation.m_NumHandles = 0;
    allocation.m_DescriptorSize = 0;
}

DescriptorAllocation& DescriptorAllocation::operator=(DescriptorAllocation&& other)
{
    // Free this descriptor if it points to anything.
    Free();

    m_Descriptor = other.m_Descriptor;
    m_NumHandles = other.m_NumHandles;
    m_DescriptorSize = other.m_DescriptorSize;
    m_Page = std::move(other.m_Page);

    other.m_Descriptor.ptr = 0;
    other.m_NumHandles = 0;
    other.m_DescriptorSize = 0;

    return *this;
}

extern uint64_t GetGraphicsFrameCount();
void DescriptorAllocation::Free()
{
    if (!IsNull() && m_Page)
    {
        m_Page->Free(std::move(*this), GetGraphicsFrameCount());

        m_Descriptor.ptr = 0;
        m_NumHandles = 0;
        m_DescriptorSize = 0;
        m_Page.reset();
    }
}

// Check if this a valid descriptor.
bool DescriptorAllocation::IsNull() const
{
    return m_Descriptor.ptr == 0;
}

// Get a descriptor at a particular offset in the allocation.
D3D12_CPU_DESCRIPTOR_HANDLE DescriptorAllocation::GetDescriptorHandle(uint32_t offset) const
{
    assert(offset < m_NumHandles);
    return { m_Descriptor.ptr + (m_DescriptorSize * offset) };
}

uint32_t DescriptorAllocation::GetNumHandles() const
{
    return m_NumHandles;
}

std::shared_ptr<DescriptorAllocatorPage> DescriptorAllocation::GetDescriptorAllocatorPage() const
{
    return m_Page;
}

// ALLOCATOR

DescriptorAllocator::DescriptorAllocator(D3D12_DESCRIPTOR_HEAP_TYPE type, uint32_t numDescriptorsPerHeap)
    : m_HeapType(type)
    , m_NumDescriptorsPerHeap(numDescriptorsPerHeap)
{
}

DescriptorAllocator::~DescriptorAllocator()
{}

std::shared_ptr<DescriptorAllocatorPage> DescriptorAllocator::CreateAllocatorPage()
{
    auto newPage = std::make_shared<DescriptorAllocatorPage>(m_HeapType, m_NumDescriptorsPerHeap);

    m_HeapPool.emplace_back(newPage);
    m_AvailableHeaps.insert(m_HeapPool.size() - 1);

    return newPage;
}

DescriptorAllocation DescriptorAllocator::Allocate(uint32_t numDescriptors)
{
    std::lock_guard<std::mutex> lock(m_AllocationMutex);

    DescriptorAllocation allocation;

    auto iter = m_AvailableHeaps.begin();
    while (iter != m_AvailableHeaps.end())
    {
        auto allocatorPage = m_HeapPool[*iter];

        allocation = allocatorPage->Allocate(numDescriptors);

        if (allocatorPage->NumFreeHandles() == 0)
        {
            iter = m_AvailableHeaps.erase(iter);
        }
        else
        {
            ++iter;
        }

        // A valid allocation has been found.
        if (!allocation.IsNull())
        {
            break;
        }

    }

    // No available heap could satisfy the requested number of descriptors.
    if (allocation.IsNull())
    {
        m_NumDescriptorsPerHeap = std::max(m_NumDescriptorsPerHeap, numDescriptors);
        auto newPage = CreateAllocatorPage();

        allocation = newPage->Allocate(numDescriptors);
    }

    return allocation;
}

void DescriptorAllocator::ReleaseStaleDescriptors(uint64_t frameNumber)
{
    std::lock_guard<std::mutex> lock(m_AllocationMutex);

    for (size_t i = 0; i < m_HeapPool.size(); ++i)
    {
        auto page = m_HeapPool[i];

        page->ReleaseStaleDescriptors(frameNumber);

        if (page->NumFreeHandles() > 0)
        {
            m_AvailableHeaps.insert(i);
        }
    }
}

// AllocatorPage

DescriptorAllocatorPage::DescriptorAllocatorPage(D3D12_DESCRIPTOR_HEAP_TYPE type, uint32_t numDescriptors)
    : m_HeapType(type)
    , m_NumDescriptorsInHeap(numDescriptors)
{
    ID3D12Device* device = nullptr; //?? Application::Get().GetDevice();

    D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {};
    heapDesc.Type = m_HeapType;
    heapDesc.NumDescriptors = m_NumDescriptorsInHeap;

    ThrowIfFailed(device->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&m_d3d12DescriptorHeap)));

    m_BaseDescriptor = m_d3d12DescriptorHeap->GetCPUDescriptorHandleForHeapStart();
    m_DescriptorHandleIncrementSize = device->GetDescriptorHandleIncrementSize(m_HeapType);
    m_NumFreeHandles = m_NumDescriptorsInHeap;

    // Initialize the free lists
    AddNewBlock(0, m_NumFreeHandles);
}

D3D12_DESCRIPTOR_HEAP_TYPE DescriptorAllocatorPage::GetHeapType() const
{
    return m_HeapType;
}

uint32_t DescriptorAllocatorPage::NumFreeHandles() const
{
    return m_NumFreeHandles;
}

bool DescriptorAllocatorPage::HasSpace(uint32_t numDescriptors) const
{
    return m_FreeListBySize.lower_bound(numDescriptors) != m_FreeListBySize.end();
}

void DescriptorAllocatorPage::AddNewBlock(uint32_t offset, uint32_t numDescriptors)
{
    auto offsetIt = m_FreeListByOffset.emplace(offset, numDescriptors);
    auto sizeIt = m_FreeListBySize.emplace(numDescriptors, offsetIt.first);
    offsetIt.first->second.FreeListBySizeIt = sizeIt;
}

DescriptorAllocation DescriptorAllocatorPage::Allocate(uint32_t numDescriptors)
{
    std::lock_guard<std::mutex> lock(m_AllocationMutex);

    // There are less than the requested number of descriptors left in the heap.
    // Return a NULL descriptor and try another heap.
    if (numDescriptors > m_NumFreeHandles)
    {
        return DescriptorAllocation();
    }

    // Get the first block that is large enough to satisfy the request.
    auto smallestBlockIt = m_FreeListBySize.lower_bound(numDescriptors);
    if (smallestBlockIt == m_FreeListBySize.end())
    {
        // There was no free block that could satisfy the request.
        return DescriptorAllocation();
    }

    // The size of the smallest block that satisfies the request.
    auto blockSize = smallestBlockIt->first;

    // The pointer to the same entry in the FreeListByOffset map.
    auto offsetIt = smallestBlockIt->second;

    // The offset in the descriptor heap.
    auto offset = offsetIt->first;

    // Remove the existing free block from the free list.
    m_FreeListBySize.erase(smallestBlockIt);
    m_FreeListByOffset.erase(offsetIt);

    // Compute the new free block that results from splitting this block.
    auto newOffset = offset + numDescriptors;
    auto newSize = blockSize - numDescriptors;

    if (newSize > 0)
    {
        // If the allocation didn't exactly match the requested size,
        // return the left-over to the free list.
        AddNewBlock(newOffset, newSize);
    }

    // Decrement free handles.
    m_NumFreeHandles -= numDescriptors;

    return DescriptorAllocation(
        CD3DX12_CPU_DESCRIPTOR_HANDLE(m_BaseDescriptor, offset, m_DescriptorHandleIncrementSize),
        numDescriptors, m_DescriptorHandleIncrementSize, shared_from_this());
}

uint32_t DescriptorAllocatorPage::ComputeOffset(D3D12_CPU_DESCRIPTOR_HANDLE handle)
{
    return static_cast<uint32_t>(handle.ptr - m_BaseDescriptor.ptr) / m_DescriptorHandleIncrementSize;
}

void DescriptorAllocatorPage::Free(DescriptorAllocation&& descriptor, uint64_t frameNumber)
{
    // Compute the offset of the descriptor within the descriptor heap.
    auto offset = ComputeOffset(descriptor.GetDescriptorHandle());

    std::lock_guard<std::mutex> lock(m_AllocationMutex);

    // Don't add the block directly to the free list until the frame has completed.
    m_StaleDescriptors.emplace(offset, descriptor.GetNumHandles(), frameNumber);
}

void DescriptorAllocatorPage::FreeBlock(uint32_t offset, uint32_t numDescriptors)
{
    // Find the first element whose offset is greater than the specified offset.
    // This is the block that should appear after the block that is being freed.
    auto nextBlockIt = m_FreeListByOffset.upper_bound(offset);

    // Find the block that appears before the block being freed.
    auto prevBlockIt = nextBlockIt;
    // If it's not the first block in the list.
    if (prevBlockIt != m_FreeListByOffset.begin())
    {
        // Go to the previous block in the list.
        --prevBlockIt;
    }
    else
    {
        // Otherwise, just set it to the end of the list to indicate that no
        // block comes before the one being freed.
        prevBlockIt = m_FreeListByOffset.end();
    }

    // Add the number of free handles back to the heap.
    // This needs to be done before merging any blocks since merging
    // blocks modifies the numDescriptors variable.
    m_NumFreeHandles += numDescriptors;

    if (prevBlockIt != m_FreeListByOffset.end() &&
        offset == prevBlockIt->first + prevBlockIt->second.Size)
    {
        // The previous block is exactly behind the block that is to be freed.
        //
        // PrevBlock.Offset           Offset
        // |                          |
        // |<-----PrevBlock.Size----->|<------Size-------->|
        //

        // Increase the block size by the size of merging with the previous block.
        offset = prevBlockIt->first;
        numDescriptors += prevBlockIt->second.Size;

        // Remove the previous block from the free list.
        m_FreeListBySize.erase(prevBlockIt->second.FreeListBySizeIt);
        m_FreeListByOffset.erase(prevBlockIt);
    }

    if (nextBlockIt != m_FreeListByOffset.end() &&
        offset + numDescriptors == nextBlockIt->first)
    {
        // The next block is exactly in front of the block that is to be freed.
        //
        // Offset               NextBlock.Offset 
        // |                    |
        // |<------Size-------->|<-----NextBlock.Size----->|

        // Increase the block size by the size of merging with the next block.
        numDescriptors += nextBlockIt->second.Size;

        // Remove the next block from the free list.
        m_FreeListBySize.erase(nextBlockIt->second.FreeListBySizeIt);
        m_FreeListByOffset.erase(nextBlockIt);
    }

    // Add the freed block to the free list.
    AddNewBlock(offset, numDescriptors);
}

void DescriptorAllocatorPage::ReleaseStaleDescriptors(uint64_t frameNumber)
{
    std::lock_guard<std::mutex> lock(m_AllocationMutex);

    while (!m_StaleDescriptors.empty() && m_StaleDescriptors.front().FrameNumber <= frameNumber)
    {
        auto& staleDescriptor = m_StaleDescriptors.front();

        // The offset of the descriptor in the heap.
        auto offset = staleDescriptor.Offset;
        // The number of descriptors that were allocated.
        auto numDescriptors = staleDescriptor.Size;

        FreeBlock(offset, numDescriptors);

        m_StaleDescriptors.pop();
    }
}

// DynamicDescriptorHeap

DynamicDescriptorHeap::DynamicDescriptorHeap(GraphicsDevice* device, D3D12_DESCRIPTOR_HEAP_TYPE heapType, uint32_t numDescriptorsPerHeap)
    : device_(device)
    , m_DescriptorHeapType(heapType)
    , m_NumDescriptorsPerHeap(numDescriptorsPerHeap)
    , m_DescriptorTableBitMask(0)
    , m_StaleDescriptorTableBitMask(0)
    , m_CurrentCPUDescriptorHandle(D3D12_DEFAULT)
    , m_CurrentGPUDescriptorHandle(D3D12_DEFAULT)
    , m_NumFreeHandles(0)
{
    m_DescriptorHandleIncrementSize = device->GetD3D12()->GetDescriptorHandleIncrementSize(heapType);

    // Allocate space for staging CPU visible descriptors.
    m_DescriptorHandleCache = std::make_unique<D3D12_CPU_DESCRIPTOR_HANDLE[]>(m_NumDescriptorsPerHeap);
}

DynamicDescriptorHeap::~DynamicDescriptorHeap()
{
}

void DynamicDescriptorHeap::ParseRootSignature(const RootSignature& rootSignature)
{
    // If the root signature changes, all descriptors must be (re)bound to the
    // command list.
    m_StaleDescriptorTableBitMask = 0;

    const auto& rootSignatureDesc = rootSignature.GetRootSignatureDesc();

    // Get a bit mask that represents the root parameter indices that match the 
    // descriptor heap type for this dynamic descriptor heap.
    m_DescriptorTableBitMask = rootSignature.GetDescriptorTableBitMask(m_DescriptorHeapType);
    uint32_t descriptorTableBitMask = m_DescriptorTableBitMask;

    uint32_t currentOffset = 0;
    DWORD rootIndex;
    while (_BitScanForward(&rootIndex, descriptorTableBitMask) && rootIndex < rootSignatureDesc.NumParameters)
    {
        uint32_t numDescriptors = rootSignature.GetNumDescriptors(rootIndex);

        DescriptorTableCache& descriptorTableCache = m_DescriptorTableCache[rootIndex];
        descriptorTableCache.NumDescriptors = numDescriptors;
        descriptorTableCache.BaseDescriptor = m_DescriptorHandleCache.get() + currentOffset;

        currentOffset += numDescriptors;

        // Flip the descriptor table bit so it's not scanned again for the current index.
        descriptorTableBitMask ^= (1 << rootIndex);
    }

    // Make sure the maximum number of descriptors per descriptor heap has not been exceeded.
    assert(currentOffset <= m_NumDescriptorsPerHeap && "The root signature requires more than the maximum number of descriptors per descriptor heap. Consider increasing the maximum number of descriptors per descriptor heap.");
}

void DynamicDescriptorHeap::StageDescriptors(uint32_t rootParameterIndex, uint32_t offset, uint32_t numDescriptors, const D3D12_CPU_DESCRIPTOR_HANDLE srcDescriptor)
{
    // Cannot stage more than the maximum number of descriptors per heap.
    // Cannot stage more than MaxDescriptorTables root parameters.
    if (numDescriptors > m_NumDescriptorsPerHeap || rootParameterIndex >= MaxDescriptorTables)
    {
        throw std::bad_alloc();
    }

    DescriptorTableCache& descriptorTableCache = m_DescriptorTableCache[rootParameterIndex];

    // Check that the number of descriptors to copy does not exceed the number
    // of descriptors expected in the descriptor table.
    if ((offset + numDescriptors) > descriptorTableCache.NumDescriptors)
    {
        throw std::length_error("Number of descriptors exceeds the number of descriptors in the descriptor table.");
    }

    D3D12_CPU_DESCRIPTOR_HANDLE* dstDescriptor = (descriptorTableCache.BaseDescriptor + offset);
    for (uint32_t i = 0; i < numDescriptors; ++i)
    {
        dstDescriptor[i] = CD3DX12_CPU_DESCRIPTOR_HANDLE(srcDescriptor, i, m_DescriptorHandleIncrementSize);
    }

    // Set the root parameter index bit to make sure the descriptor table 
    // at that index is bound to the command list.
    m_StaleDescriptorTableBitMask |= (1 << rootParameterIndex);
}

uint32_t DynamicDescriptorHeap::ComputeStaleDescriptorCount() const
{
    uint32_t numStaleDescriptors = 0;
    DWORD i;
    DWORD staleDescriptorsBitMask = m_StaleDescriptorTableBitMask;

    while (_BitScanForward(&i, staleDescriptorsBitMask))
    {
        numStaleDescriptors += m_DescriptorTableCache[i].NumDescriptors;
        staleDescriptorsBitMask ^= (1 << i);
    }

    return numStaleDescriptors;
}

Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> DynamicDescriptorHeap::RequestDescriptorHeap()
{
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> descriptorHeap;
    if (!m_AvailableDescriptorHeaps.empty())
    {
        descriptorHeap = m_AvailableDescriptorHeaps.front();
        m_AvailableDescriptorHeaps.pop();
    }
    else
    {
        descriptorHeap = CreateDescriptorHeap();
        m_DescriptorHeapPool.push(descriptorHeap);
    }

    return descriptorHeap;
}

Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> DynamicDescriptorHeap::CreateDescriptorHeap()
{
    ID3D12Device* device = device_->GetD3D12();

    D3D12_DESCRIPTOR_HEAP_DESC descriptorHeapDesc = {};
    descriptorHeapDesc.Type = m_DescriptorHeapType;
    descriptorHeapDesc.NumDescriptors = m_NumDescriptorsPerHeap;
    descriptorHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;

    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> descriptorHeap;
    ThrowIfFailed(device->CreateDescriptorHeap(&descriptorHeapDesc, IID_PPV_ARGS(&descriptorHeap)));

    return descriptorHeap;
}

void DynamicDescriptorHeap::CommitStagedDescriptors(CommandList& commandList, std::function<void(ID3D12GraphicsCommandList*, UINT, D3D12_GPU_DESCRIPTOR_HANDLE)> setFunc)
{
    // Compute the number of descriptors that need to be copied 
    uint32_t numDescriptorsToCommit = ComputeStaleDescriptorCount();

    if (numDescriptorsToCommit > 0)
    {
        auto device = device_->GetD3D12();
        auto d3d12GraphicsCommandList = commandList.GetNative();
        assert(d3d12GraphicsCommandList != nullptr);

        if (!m_CurrentDescriptorHeap || m_NumFreeHandles < numDescriptorsToCommit)
        {
            m_CurrentDescriptorHeap = RequestDescriptorHeap();
            m_CurrentCPUDescriptorHandle = m_CurrentDescriptorHeap->GetCPUDescriptorHandleForHeapStart();
            m_CurrentGPUDescriptorHandle = m_CurrentDescriptorHeap->GetGPUDescriptorHandleForHeapStart();
            m_NumFreeHandles = m_NumDescriptorsPerHeap;

            commandList.SetDescriptorHeap(m_DescriptorHeapType, m_CurrentDescriptorHeap.Get());

            // When updating the descriptor heap on the command list, all descriptor
            // tables must be (re)recopied to the new descriptor heap (not just
            // the stale descriptor tables).
            m_StaleDescriptorTableBitMask = m_DescriptorTableBitMask;
        }

        DWORD rootIndex;
        // Scan from LSB to MSB for a bit set in staleDescriptorsBitMask
        while (_BitScanForward(&rootIndex, m_StaleDescriptorTableBitMask))
        {
            UINT numSrcDescriptors = m_DescriptorTableCache[rootIndex].NumDescriptors;
            D3D12_CPU_DESCRIPTOR_HANDLE* pSrcDescriptorHandles = m_DescriptorTableCache[rootIndex].BaseDescriptor;

            D3D12_CPU_DESCRIPTOR_HANDLE pDestDescriptorRangeStarts[] =
            {
                m_CurrentCPUDescriptorHandle
            };
            UINT pDestDescriptorRangeSizes[] =
            {
                numSrcDescriptors
            };

            // Copy the staged CPU visible descriptors to the GPU visible descriptor heap.
            device->CopyDescriptors(1, pDestDescriptorRangeStarts, pDestDescriptorRangeSizes,
                numSrcDescriptors, pSrcDescriptorHandles, nullptr, m_DescriptorHeapType);

            // Set the descriptors on the command list using the passed-in setter function.
            setFunc(d3d12GraphicsCommandList, rootIndex, m_CurrentGPUDescriptorHandle);

            // Offset current CPU and GPU descriptor handles.
            m_CurrentCPUDescriptorHandle.Offset(numSrcDescriptors, m_DescriptorHandleIncrementSize);
            m_CurrentGPUDescriptorHandle.Offset(numSrcDescriptors, m_DescriptorHandleIncrementSize);
            m_NumFreeHandles -= numSrcDescriptors;

            // Flip the stale bit so the descriptor table is not recopied again unless it is updated with a new descriptor.
            m_StaleDescriptorTableBitMask ^= (1 << rootIndex);
        }
    }
}

void DynamicDescriptorHeap::CommitStagedDescriptorsForDraw(CommandList& commandList)
{
    CommitStagedDescriptors(commandList, &ID3D12GraphicsCommandList::SetGraphicsRootDescriptorTable);
}

void DynamicDescriptorHeap::CommitStagedDescriptorsForDispatch(CommandList& commandList)
{
    CommitStagedDescriptors(commandList, &ID3D12GraphicsCommandList::SetComputeRootDescriptorTable);
}

D3D12_GPU_DESCRIPTOR_HANDLE DynamicDescriptorHeap::CopyDescriptor(CommandList& comandList, D3D12_CPU_DESCRIPTOR_HANDLE cpuDescriptor)
{
    if (!m_CurrentDescriptorHeap || m_NumFreeHandles < 1)
    {
        m_CurrentDescriptorHeap = RequestDescriptorHeap();
        m_CurrentCPUDescriptorHandle = m_CurrentDescriptorHeap->GetCPUDescriptorHandleForHeapStart();
        m_CurrentGPUDescriptorHandle = m_CurrentDescriptorHeap->GetGPUDescriptorHandleForHeapStart();
        m_NumFreeHandles = m_NumDescriptorsPerHeap;

        comandList.SetDescriptorHeap(m_DescriptorHeapType, m_CurrentDescriptorHeap.Get());

        // When updating the descriptor heap on the command list, all descriptor
        // tables must be (re)recopied to the new descriptor heap (not just
        // the stale descriptor tables).
        m_StaleDescriptorTableBitMask = m_DescriptorTableBitMask;
    }

    ID3D12Device* device = device_->GetD3D12();

    D3D12_GPU_DESCRIPTOR_HANDLE hGPU = m_CurrentGPUDescriptorHandle;
    device->CopyDescriptorsSimple(1, m_CurrentCPUDescriptorHandle, cpuDescriptor, m_DescriptorHeapType);

    m_CurrentCPUDescriptorHandle.Offset(1, m_DescriptorHandleIncrementSize);
    m_CurrentGPUDescriptorHandle.Offset(1, m_DescriptorHandleIncrementSize);
    m_NumFreeHandles -= 1;

    return hGPU;
}

void DynamicDescriptorHeap::Reset()
{
    m_AvailableDescriptorHeaps = m_DescriptorHeapPool;
    m_CurrentDescriptorHeap.Reset();
    m_CurrentCPUDescriptorHandle = CD3DX12_CPU_DESCRIPTOR_HANDLE(D3D12_DEFAULT);
    m_CurrentGPUDescriptorHandle = CD3DX12_GPU_DESCRIPTOR_HANDLE(D3D12_DEFAULT);
    m_NumFreeHandles = 0;
    m_DescriptorTableBitMask = 0;
    m_StaleDescriptorTableBitMask = 0;

    // Reset the table cache
    for (int i = 0; i < MaxDescriptorTables; ++i)
    {
        m_DescriptorTableCache[i].Reset();
    }
}

