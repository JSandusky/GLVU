//****************************************************************************
//
//  File:       Packing.h
//  License:    MIT
//  Project:    GLVU
//  Contents:   Helper classes for working with glBindBufferRange and the equivalent
//              ranges in Vulkan. Using these minimizes the number of discrete transfers,
//              albiet while potentially increasing the total transfer size to account
//              for padding.
//
//  Usage:      Pre-scan required data, pack and all upload once despite multiple
//              different use points and then reference the ranges each user needs.
//
//****************************************************************************

#pragma once

#include <glvu.h>
#include <Buffer.h>

#include <algorithm>
#include <array>
#include <cassert>
#include <map>
#include <numeric>
#include <vector>

namespace GLVU
{

class Buffer;
class GraphicsDevice;

/// Used to cope with `minUboAlignment`s nonsense ... 256 alignment, sheesh.
/// Only supports up to 8 objects packed into it.
/*
BufferPack packing(256);
packing.Pack<ViewData>();
packing.Pack<ViewSizes>();
packing.Allocate(ubo);

packing.Get<ViewData>(0).transform = Matrix(...); // (0, 0) default
packing.Get<ViewData>(0, 1).transform = Matrix(...);
packing.Transfer(ubo);
*/
struct BufferPack
{
    std::array<size_t, 8> offsets_;
    std::array<size_t, 8> recordSizes_;
    std::array<size_t, 8> tags_;
    std::unique_ptr<char> data_;
    uint32_t totalSize_;
    uint32_t allocSize_ = 0;
    uint32_t records_;
    uint32_t alignment_;

    /// Default to 256 for everyone, green is the only color.
    BufferPack(uint32_t alignment = 256) :
        alignment_(alignment),
        records_(0),
        totalSize_(0)
    {
    }

    template<typename T>
    void Pack(size_t tag = 0)
    {
        int sz = sizeof(T);

        int remainder = sz % alignment_;
        if (remainder != 0)
            sz += alignment_ - remainder;

        assert(records_ < 8);
        offsets_[records_] = totalSize_;
        recordSizes_[records_] = sizeof(T);
        tags_[records_] = tag;
        totalSize_ += sz;
        ++records_;

        assert(totalSize_ % alignment_ == 0);
    }

    /// Allocate data exclusively for CPU usage.
    void AllocateGeneral(uint32_t count = 1)
    {
        allocSize_ = totalSize_ * count;
        data_.reset(new char[allocSize_]);
    }

    size_t CalculateSize(size_t count = 1) const {
        return count * totalSize_;
    }

    /// Allocate data for both CPU-backing and UBO
    void AllocateUBO(std::shared_ptr<Buffer> ubo, uint32_t count = 1)
    {
        allocSize_ = totalSize_ * count;
        if (ubo && ubo->GetSize() != allocSize_)
            ubo->SetSize(allocSize_);
        data_.reset(new char[allocSize_]);
    }

    template<typename T>
    T& Get(uint32_t slot, uint32_t objectIndex = 0)
    {
        assert(slot < records_);
        return *(T*)(data_.get() + offsets_[slot] + totalSize_ * objectIndex);
    }

    size_t OffsetOf(uint32_t slot, uint32_t objectIndex = 0) const
    {
        assert(slot < records_);
        return offsets_[slot] + totalSize_ * objectIndex;
    }

    size_t OffsetOfTag(size_t tag)
    {
        for (uint32_t slot = 0; slot < records_; ++slot)
            if (tags_[slot] == tag)
                return offsets_[slot];
        return SIZE_MAX;
    }

    size_t SizeOf(uint32_t slot) const {
        assert(slot < records_);
        return recordSizes_[slot];
    }

    void Transfer(std::shared_ptr<Buffer> tgt, bool inFrame)
    {
#if defined(GLVU_VK)
        if (!inFrame)
            tgt->SetSubData(data_.get(), 0, allocSize_);
        else
        {
            assert(tgt->GetSize() == allocSize_);
            vezCmdUpdateBuffer(tgt->GetGPUObject(), 0, allocSize_, data_.get());
        }
#else
        tgt->SetSubData(data_.get(), 0, allocSize_);
#endif
    }
};

/// A less `hard` version of BufferPack, this is intended solely for use with `unknown` data, such as custom uniform buffers.
/// Statically known data (frame, camera, light, shadow, etc) should stick to BufferPack while this gets used for CustomMaterial data
/// and instancing buffers.
struct BufferPool
{
    std::vector<uint64_t> data_;
    size_t totalSize_;
    size_t alignment_;
    size_t maxSize_;

    /// Default to 200 records at nVidia's 256-byte alignment.
    BufferPool(size_t prealloc = 25600, size_t alignment = 256) :
        totalSize_(0),
        alignment_(alignment),
        maxSize_(prealloc),
        data_(prealloc / sizeof(uint64_t), 0ull)
    {
    }

	/// Address to write into, and the offset of that into the total data.
    std::pair<void*, size_t> Allocate(size_t sz)
    {
        int remainder = sz % alignment_;
        if (remainder != 0)
            sz += alignment_ - remainder;

        auto currentSize = totalSize_;

        // verify we have enough possible space
        if (currentSize + sz > maxSize_)
            return std::pair<void*, size_t>(nullptr, 0);

        // grow if we can
        if (AllocSize() < totalSize_ + sz)
            data_.resize((totalSize_ + sz + sizeof(uint64_t) - 1) / sizeof(uint64_t));

        void* dataPtr = ((char*)data_.data()) + totalSize_;
        totalSize_ += sz;

        return std::pair<void*, size_t>((void*)dataPtr, (size_t)currentSize);
    }

    /// Moves data from this cache object into an actual UBO.
    void Transfer(std::shared_ptr<Buffer> ubo) 
    { 
        ubo->SetSubData(data_.data(), 0, totalSize_); 
    }

private:
    size_t AllocSize() const { return data_.size() * sizeof(uint64_t); }
};

/// Obfuscates an indeterminate and potentially massive number of isolated writes into a single buffer intended for
/// use with glBindBufferRange
struct RollingBufferAllocator
{	
	struct AllocationRecord {
		size_t allocationOffset_;
		size_t allocationSize_;
		std::shared_ptr<Buffer> buffer_;
	};

	RollingBufferAllocator(GraphicsDevice* device);

	std::pair<void*, size_t> Allocate(size_t dataSize);

	/// It's critical to call finish so that any waiting writes can be completed.
	void Finish();

	const AllocationRecord& GetAllocation(uint32_t idx) const { return records_[idx]; }

	unsigned GetAllocCount() const { return records_.size(); }

private:
	std::pair<void*, size_t> AllocateInternal(size_t dataSize, bool firstTry);
	void Restart();

	void ApplyBuffer(std::shared_ptr<Buffer> buffer);

	GraphicsDevice* device_;
	BufferPool workingPool_;
	std::vector<AllocationRecord> records_;
};

static std::vector< std::pair<int, int> > SplitIndices(int total, int divisions)
{
    assert(divisions > 0);
    int ct = total / divisions;

    std::vector< std::pair<int, int> > r;
    r.resize(divisions);
    int cur = 0;
    for (int i = 0; i < divisions; ++i)
    {
        r[i] = { cur, cur + ct };
        cur += ct;
    }

    r[divisions - 1].second = r[divisions - 1].first + total - (ct * (divisions - 1));
    //assert(std::accumulate(r.begin(), r.end(), 0) == total);
    return r;
}

template<typename T>
void Partition(std::vector< std::vector<T> >& holder, const std::vector<T>& src, int divisions)
{
    if (divisions <= 0) return;
    auto packing = SplitIndices(src.size(), divisions);
    for (int i = 0; i < packing.size(); ++i)
    {
        std::vector<T> v;
        v.insert(v.end(), src.begin() + packing[i].first, src.begin() + packing[i].second);
        holder.push_back(v);
    }
}

struct IndexSpan
{
    int start_ = 0;
    int length_ = 0;

    inline int Length() const { return length_; }
    inline int End() const { return start_ + length_; }
    inline int IsEmpty() const { return length_ == 0; }

    IndexSpan() { }
    IndexSpan(int start, int len) : start_(start), length_(len) { }

    inline bool Contains(int value) const {
        return value >= start_ && value < End();
    }
};

// 
struct IndexSpans
{
    std::array<IndexSpan, 8> spans_;

    IndexSpans() { }

    IndexSpans(std::vector<int> slots)
    {
        std::sort(slots.begin(), slots.end());
        uint32_t cur = 0;
        for (auto v : slots)
        {
            if (spans_[cur].IsEmpty())
                spans_[cur] = IndexSpan(v, 1);
            else
            {
                if (spans_[cur].End() != v)
                {
                    ++cur;
                    spans_[cur] = IndexSpan(v, 1);
                }
                else 
                    spans_[cur].length_ += 1;
            }
        }
    }

    // Which span is this value a part of?
    // This is the reason for this type, identifies which root signature descriptor table slot
    uint32_t IndexFor(int value) const {
        for (uint32_t i = 0; i < spans_.size(); ++i)
            if (spans_[i].Contains(value))
                return i;
        return UINT_MAX;
    }

    // Total number of active spans
    uint32_t SpanCount() const {
        uint32_t i = 0;
        for (auto s : spans_)
            if (!s.IsEmpty())
                ++i;
        return i;
    }
};

}