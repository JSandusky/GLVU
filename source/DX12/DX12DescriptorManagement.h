#pragma once
/*
 *  Copyright(c) 2018 Jeremiah van Oosten
 *
 *  Permission is hereby granted, free of charge, to any person obtaining a copy
 *  of this software and associated documentation files(the "Software"), to deal
 *  in the Software without restriction, including without limitation the rights
 *  to use, copy, modify, merge, publish, distribute, sublicense, and / or sell
 *  copies of the Software, and to permit persons to whom the Software is
 *  furnished to do so, subject to the following conditions :
 *
 *  The above copyright notice and this permission notice shall be included in
 *  all copies or substantial portions of the Software.
 *
 *  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 *  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 *  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.IN NO EVENT SHALL THE
 *  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 *  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 *  FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 *  IN THE SOFTWARE.
 */

// Moficiations by JSandusky: drop ComPtr and amalgamate into one file.

#include <d3d12.h>
#include <wrl.h>

#include <cstdint>
#include <memory>
#include <map>
#include <queue>
#include <set>
#include <mutex>

#include "D3D12x.h"

namespace GLVU
{
    class GraphicsDevice;
    class CommandList;
    class RootSignature;
}

class DescriptorAllocatorPage;

class DescriptorAllocation
{
public:
    // Creates a NULL descriptor.
    DescriptorAllocation();

    DescriptorAllocation(D3D12_CPU_DESCRIPTOR_HANDLE descriptor, uint32_t numHandles, uint32_t descriptorSize, std::shared_ptr<DescriptorAllocatorPage> page);

    // The destructor will automatically free the allocation.
    ~DescriptorAllocation();

    // Copies are not allowed.
    DescriptorAllocation(const DescriptorAllocation&) = delete;
    DescriptorAllocation& operator=(const DescriptorAllocation&) = delete;

    // Move is allowed.
    DescriptorAllocation(DescriptorAllocation&& allocation);
    DescriptorAllocation& operator=(DescriptorAllocation&& other);

    // Check if this a valid descriptor.
    bool IsNull() const;

    // Get a descriptor at a particular offset in the allocation.
    D3D12_CPU_DESCRIPTOR_HANDLE GetDescriptorHandle(uint32_t offset = 0) const;

    // Get the number of (consecutive) handles for this allocation.
    uint32_t GetNumHandles() const;

    // Get the heap that this allocation came from.
    // (For internal use only).
    std::shared_ptr<DescriptorAllocatorPage> GetDescriptorAllocatorPage() const;

private:
    // Free the descriptor back to the heap it came from.
    void Free();

    // The base descriptor.
    D3D12_CPU_DESCRIPTOR_HANDLE m_Descriptor;
    // The number of descriptors in this allocation.
    uint32_t m_NumHandles;
    // The offset to the next descriptor.
    uint32_t m_DescriptorSize;

    // A pointer back to the original page where this allocation came from.
    std::shared_ptr<DescriptorAllocatorPage> m_Page;
};

class DescriptorAllocator
{
public:
    DescriptorAllocator(D3D12_DESCRIPTOR_HEAP_TYPE type, uint32_t numDescriptorsPerHeap = 256);
    virtual ~DescriptorAllocator();

    /**
     * Allocate a number of contiguous descriptors from a CPU visible descriptor heap.
     *
     * @param numDescriptors The number of contiguous descriptors to allocate.
     * Cannot be more than the number of descriptors per descriptor heap.
     */
    DescriptorAllocation Allocate(uint32_t numDescriptors = 1);

    /**
     * When the frame has completed, the stale descriptors can be released.
     */
    void ReleaseStaleDescriptors(uint64_t frameNumber);

private:
    using DescriptorHeapPool = std::vector< std::shared_ptr<DescriptorAllocatorPage> >;

    // Create a new heap with a specific number of descriptors.
    std::shared_ptr<DescriptorAllocatorPage> CreateAllocatorPage();

    D3D12_DESCRIPTOR_HEAP_TYPE m_HeapType;
    uint32_t m_NumDescriptorsPerHeap;

    DescriptorHeapPool m_HeapPool;
    // Indices of available heaps in the heap pool.
    std::set<size_t> m_AvailableHeaps;

    std::mutex m_AllocationMutex;
};

class DescriptorAllocatorPage : public std::enable_shared_from_this<DescriptorAllocatorPage>
{
public:
    DescriptorAllocatorPage(D3D12_DESCRIPTOR_HEAP_TYPE type, uint32_t numDescriptors);

    D3D12_DESCRIPTOR_HEAP_TYPE GetHeapType() const;

    /**
    * Check to see if this descriptor page has a contiguous block of descriptors
    * large enough to satisfy the request.
    */
    bool HasSpace(uint32_t numDescriptors) const;

    /**
    * Get the number of available handles in the heap.
    */
    uint32_t NumFreeHandles() const;

    /**
    * Allocate a number of descriptors from this descriptor heap.
    * If the allocation cannot be satisfied, then a NULL descriptor
    * is returned.
    */
    DescriptorAllocation Allocate(uint32_t numDescriptors);

    /**
    * Return a descriptor back to the heap.
    * @param frameNumber Stale descriptors are not freed directly, but put
    * on a stale allocations queue. Stale allocations are returned to the heap
    * using the DescriptorAllocatorPage::ReleaseStaleAllocations method.
    */
    void Free(DescriptorAllocation&& descriptorHandle, uint64_t frameNumber);

    /**
    * Returned the stale descriptors back to the descriptor heap.
    */
    void ReleaseStaleDescriptors(uint64_t frameNumber);

protected:

    // Compute the offset of the descriptor handle from the start of the heap.
    uint32_t ComputeOffset(D3D12_CPU_DESCRIPTOR_HANDLE handle);

    // Adds a new block to the free list.
    void AddNewBlock(uint32_t offset, uint32_t numDescriptors);

    // Free a block of descriptors.
    // This will also merge free blocks in the free list to form larger blocks
    // that can be reused.
    void FreeBlock(uint32_t offset, uint32_t numDescriptors);

private:
    // The offset (in descriptors) within the descriptor heap.
    using OffsetType = uint32_t;
    // The number of descriptors that are available.
    using SizeType = uint32_t;

    struct FreeBlockInfo;
    // A map that lists the free blocks by the offset within the descriptor heap.
    using FreeListByOffset = std::map<OffsetType, FreeBlockInfo>;

    // A map that lists the free blocks by size.
    // Needs to be a multimap since multiple blocks can have the same size.
    using FreeListBySize = std::multimap<SizeType, FreeListByOffset::iterator>;

    struct FreeBlockInfo
    {
        FreeBlockInfo(SizeType size)
            : Size(size)
        {}

        SizeType Size;
        FreeListBySize::iterator FreeListBySizeIt;
    };

    struct StaleDescriptorInfo
    {
        StaleDescriptorInfo(OffsetType offset, SizeType size, uint64_t frame)
            : Offset(offset)
            , Size(size)
            , FrameNumber(frame)
        {}

        // The offset within the descriptor heap.
        OffsetType Offset;
        // The number of descriptors
        SizeType Size;
        // The frame number that the descriptor was freed.
        uint64_t FrameNumber;
    };

    // Stale descriptors are queued for release until the frame that they were freed
    // has completed.
    using StaleDescriptorQueue = std::queue<StaleDescriptorInfo>;

    FreeListByOffset m_FreeListByOffset;
    FreeListBySize m_FreeListBySize;
    StaleDescriptorQueue m_StaleDescriptors;

    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> m_d3d12DescriptorHeap;
    D3D12_DESCRIPTOR_HEAP_TYPE m_HeapType;
    CD3DX12_CPU_DESCRIPTOR_HANDLE m_BaseDescriptor;
    uint32_t m_DescriptorHandleIncrementSize;
    uint32_t m_NumDescriptorsInHeap;
    uint32_t m_NumFreeHandles;

    std::mutex m_AllocationMutex;
};

class DynamicDescriptorHeap
{
public:
    DynamicDescriptorHeap(
        GLVU::GraphicsDevice*,
        D3D12_DESCRIPTOR_HEAP_TYPE heapType,
        uint32_t numDescriptorsPerHeap = 1024);

    virtual ~DynamicDescriptorHeap();

    /**
     * Stages a contiguous range of CPU visible descriptors.
     * Descriptors are not copied to the GPU visible descriptor heap until
     * the CommitStagedDescriptors function is called.
     */
    void StageDescriptors(uint32_t rootParameterIndex, uint32_t offset, uint32_t numDescriptors, const D3D12_CPU_DESCRIPTOR_HANDLE srcDescriptors);

    /**
     * Copy all of the staged descriptors to the GPU visible descriptor heap and
     * bind the descriptor heap and the descriptor tables to the command list.
     * The passed-in function object is used to set the GPU visible descriptors
     * on the command list. Two possible functions are:
     *   * Before a draw    : ID3D12GraphicsCommandList::SetGraphicsRootDescriptorTable
     *   * Before a dispatch: ID3D12GraphicsCommandList::SetComputeRootDescriptorTable
     *
     * Since the DynamicDescriptorHeap can't know which function will be used, it must
     * be passed as an argument to the function.
     */
    void CommitStagedDescriptors(GLVU::CommandList& commandList, std::function<void(ID3D12GraphicsCommandList*, UINT, D3D12_GPU_DESCRIPTOR_HANDLE)> setFunc);
    void CommitStagedDescriptorsForDraw(GLVU::CommandList& commandList);
    void CommitStagedDescriptorsForDispatch(GLVU::CommandList& commandList);

    /**
     * Copies a single CPU visible descriptor to a GPU visible descriptor heap.
     * This is useful for the
     *   * ID3D12GraphicsCommandList::ClearUnorderedAccessViewFloat
     *   * ID3D12GraphicsCommandList::ClearUnorderedAccessViewUint
     * methods which require both a CPU and GPU visible descriptors for a UAV
     * resource.
     *
     * @param commandList The command list is required in case the GPU visible
     * descriptor heap needs to be updated on the command list.
     * @param cpuDescriptor The CPU descriptor to copy into a GPU visible
     * descriptor heap.
     *
     * @return The GPU visible descriptor.
     */
    D3D12_GPU_DESCRIPTOR_HANDLE CopyDescriptor(GLVU::CommandList& comandList, D3D12_CPU_DESCRIPTOR_HANDLE cpuDescriptor);

    /**
     * Parse the root signature to determine which root parameters contain
     * descriptor tables and determine the number of descriptors needed for
     * each table.
     */
    void ParseRootSignature(const GLVU::RootSignature& rootSignature);

    /**
     * Reset used descriptors. This should only be done if any descriptors
     * that are being referenced by a command list has finished executing on the
     * command queue.
     */
    void Reset();

protected:

private:
    // Request a descriptor heap if one is available.
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> RequestDescriptorHeap();
    // Create a new descriptor heap of no descriptor heap is available.
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> CreateDescriptorHeap();

    // Compute the number of stale descriptors that need to be copied
    // to GPU visible descriptor heap.
    uint32_t ComputeStaleDescriptorCount() const;

    /**
     * The maximum number of descriptor tables per root signature.
     * A 32-bit mask is used to keep track of the root parameter indices that
     * are descriptor tables.
     */
    static const uint32_t MaxDescriptorTables = 32;

    /**
     * A structure that represents a descriptor table entry in the root signature.
     */
    struct DescriptorTableCache
    {
        DescriptorTableCache()
            : NumDescriptors(0)
            , BaseDescriptor(nullptr)
        {}

        // Reset the table cache.
        void Reset()
        {
            NumDescriptors = 0;
            BaseDescriptor = nullptr;
        }

        // The number of descriptors in this descriptor table.
        uint32_t NumDescriptors;
        // The pointer to the descriptor in the descriptor handle cache.
        D3D12_CPU_DESCRIPTOR_HANDLE* BaseDescriptor;
    };

    // Describes the type of descriptors that can be staged using this 
    // dynamic descriptor heap.
    // Valid values are:
    //   * D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV
    //   * D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER
    // This parameter also determines the type of GPU visible descriptor heap to 
    // create.
    D3D12_DESCRIPTOR_HEAP_TYPE m_DescriptorHeapType;

    // The number of descriptors to allocate in new GPU visible descriptor heaps.
    uint32_t m_NumDescriptorsPerHeap;

    // The increment size of a descriptor.
    uint32_t m_DescriptorHandleIncrementSize;

    // The descriptor handle cache.
    std::unique_ptr<D3D12_CPU_DESCRIPTOR_HANDLE[]> m_DescriptorHandleCache;

    // Descriptor handle cache per descriptor table.
    DescriptorTableCache m_DescriptorTableCache[MaxDescriptorTables];

    // Each bit in the bit mask represents the index in the root signature
    // that contains a descriptor table.
    uint32_t m_DescriptorTableBitMask;
    // Each bit set in the bit mask represents a descriptor table
    // in the root signature that has changed since the last time the 
    // descriptors were copied.
    uint32_t m_StaleDescriptorTableBitMask;

    using DescriptorHeapPool = std::queue< Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> >;

    DescriptorHeapPool m_DescriptorHeapPool;
    DescriptorHeapPool m_AvailableDescriptorHeaps;

    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> m_CurrentDescriptorHeap;
    CD3DX12_GPU_DESCRIPTOR_HANDLE m_CurrentGPUDescriptorHandle;
    CD3DX12_CPU_DESCRIPTOR_HANDLE m_CurrentCPUDescriptorHandle;

    uint32_t m_NumFreeHandles;

    GLVU::GraphicsDevice* device_;

};