//****************************************************************************
//
//  File:       Blob.h
//  License:    MIT
//  Project:    GLVU
//  Contents:   Generic raw data handle for transfering sized data chunks.
//
//****************************************************************************

#pragma once

#include "glvu.h"

#include <MathGeoLib/Math/MathAll.h>
#include <MathGeoLib/Math/float4.h>

#include <algorithm>

namespace GLVU
{

/// Trivial object for storing a chunk of data.
struct Blob
{
    char* data_;
    size_t size_;
    size_t offset_;
    bool ownsData_;

    /// Construct.
    Blob(char* data, size_t size, bool copy) : 
        data_(data), 
        size_(size), 
        offset_(0)
    {
        if (copy)
        {
            ownsData_ = true;
            data_ = new char[size];
            memcpy(data_, data, size);
        }
    }

    /// Construct and copy.
    Blob(const Blob& rhs) : 
        size_(rhs.size_), 
        offset_(rhs.offset_), 
        ownsData_(rhs.ownsData_)
    {
        if (rhs.ownsData_)
        {
            data_ = new char[size_];
            memcpy(data_, rhs.data_, size_);
        }
    }

	Blob(size_t alloc) :
		data_(new char[alloc]),
		size_(alloc),
		offset_(0),
		ownsData_(true)
	{

	}

    /// Destruct, free if data is own.
    ~Blob()
    {
        if (ownsData_)
            delete[] data_;
        data_ = nullptr;
        size_ = 0;
        offset_ = 0;
        ownsData_ = false;
    }

    /// Read arbitrary POD type.
    template<typename T>
    T Read()
    {
        T ret;
        memcpy(&ret, data_ + offset_, sizeof(T));
        offset_ += sizeof(T);
    }

    /// Read a vector of arbitrary POD types.
    template<typename T>
    std::vector<T> ReadVector()
    {
        std::vector<T> ret;
        uint32_t ct = Read();
        ret.resize(ct);
        for (uint32_t i = 0; i < ct; ++i)
            ret[i] = Read<T>();
        return ret;
    }

    /// Reads a null-terminated string.
    template<>
    std::string Read<std::string>()
    {
        std::string ret = std::string(data_ + offset_);
        offset_ += (uint32_t)ret.length() + 1;
    }
};

/// Writable scaling blob.
struct VaryingBlob
{
    /// Blob data.
    std::vector<char> data_;
    /// Size that was actually written, data_ may be larger.
    size_t writtenSize_;
    /// Current offset for reading/writing.
    size_t offset_;

    /// Construct and specify initial size.
    VaryingBlob(size_t alloc) :
        offset_(0), 
        writtenSize_(0)
    {
        if (alloc > 0)
            data_.resize(alloc);
    }

    /// Construct and copy from a blob.
    VaryingBlob(const Blob& src) :
        offset_(0),
        writtenSize_(src.size_)
    {
        data_.resize(src.size_);
        memcpy(data_.data(), src.data_, src.size_);
    }

    /// Construct and copy from arbitrary memory.
    VaryingBlob(void* data, uint32_t dataSize) :
        writtenSize_(dataSize),
        offset_(0)
    {
        data_.resize(dataSize);
        memcpy(data_.data(), data, dataSize);
    }

    /// Read a POD
    template<typename T>
    T Read()
    {
        T ret;
        memcpy(&ret, data_.data() + offset_, sizeof(T));
        offset_ += sizeof(T);
        return ret;
    }

    /// Read a vector of PODs
    template<typename T>
    std::vector<T> ReadVector()
    {
        std::vector<T> ret;
        uint32_t ct = Read<uint32_t>();
        ret.resize(ct);
        for (uint32_t i = 0; i < ct; ++i)
            ret[i] = Read<T>();
        return ret;
    }

    /// Reads a null-terminated string.
    std::string ReadString()
    {
        std::string ret = std::string(data_.data() + offset_);
        offset_ += (uint32_t)ret.length() + 1;
        return ret;
    }

    /// Write a POD
    template<typename T>
    void Write(const T & data)
    {
        if (data_.size() < offset_ + sizeof(T))
            data_.resize(std::max<uint32_t>(data_.size() + sizeof(T), data_.size() * 0.5f));
        memcpy(data_.data() + offset_, &data, sizeof(T));
        offset_ += sizeof(T);
        writtenSize_ += sizeof(T);
    }

    /// Write a null-terminated string.
    template<>
    void Write(const std::string& string)
    {
        size_t strLen = string.length() + 1;
        if (data_.size() < offset_ + string.length() + 1)
            data_.resize(std::max<uint64_t>((uint64_t)(data_.size() * 0.5f), data_.size() + strLen));

        strcpy_s(data_.data() + offset_, strLen, string.c_str());

        offset_ += (uint32_t)strLen;
        writtenSize_ += (uint32_t)strLen;
    }

    /// Write a vector of PODs
    template<typename T>
    void WriteVector(const std::vector<T>& vec)
    {
        Write((uint32_t)vec.size());
        for (uint32_t i = 0; i < vec.size(); ++i)
            Write<T>(vec[i]);
    }
};

template<typename T>
struct BlockMapFilter {
    T operator()(T lhs, T rhs, float);
};

template<>
struct BlockMapFilter<math::float4> {
    math::float4 operator()(math::float4 lhs, math::float4 rhs, float td)
    {
        return lhs + (rhs - lhs) * td;
    }
};

/// An arbitrary bitmap of sorts. Only real restriction is that the contained type support addition against itself
/// and multiplication against a float.
template<typename T>
struct BlockMap
{
    std::unique_ptr<T[]> data_;
    size_t width_, height_, depth_;
    BlockMap* mipmap_;
    BlockMap* sibling_;

    BlockMap() : width_(0), height_(0), depth_(0), mipmap_(nullptr), sibling_(nullptr) { }

    BlockMap(size_t width, size_t height, size_t depth = 1) : 
        width_(width), 
        height_(height), 
        depth_(depth), 
        mipmap_(nullptr), 
        sibling_(nullptr) 
    { 
        SetSize(width, height, depth);
    }

    ~BlockMap() {
        data_.reset();
        width_ = height_ = depth_ = 0;
        if (mipmap_)
            delete mipmap_;
        if (sibling_)
            delete sibling_;
    }

    void SetSize(size_t x, size_t y, size_t z = 1)
    {
        width_ = x;
        height_ = y;
        depth_ = z;
        data_.reset(new T[x * y * z]);
        Zero();
    }

    inline void Zero() { memset(data_.get(), 0, GetDataSize()); }

    inline void Fill(T data) { std::fill(data_.get(), data_.get() + GetDataSize(), data); }

    inline size_t GetElemCount() const { return width_ * height_ * depth_; }

    inline size_t GetDataSize() const { return width_ * height_ * depth_ * sizeof(T); }

    inline size_t GetLayerSize() const { return width_ * height_ * sizeof(T); }

    inline T GetPixel(size_t x, size_t y, size_t z = 0) { return data_[ToCoord(x, y, z)]; }

    inline void SetPixel(T value, size_t x, size_t y, size_t z = 0) { data_[ToCoord(x, y, z)] = value; }

    T GetBilinear(float x, float y) 
    {
        x = x - floorf(x);
        y = y - floorf(y);
        x = math::Clamp(x * width_ - 0.5f, 0.0f, (float)(width_ - 1));
        y = math::Clamp(y * height_ - 0.5f, 0.0f, (float)(height_ - 1));

        int xI = (int)x;
        int yI = (int)y;

        float xF = x - floorf(x);
        float yF = y - floorf(y);

        T topValue = (GetPixel(xI, yI) * (1.0f - xF)) + (GetPixel(xI + 1, yI) * xF);
        T bottomValue = (GetPixel(xI, yI + 1) * (1.0f - xF)) + (GetPixel(xI + 1, yI + 1) * xF);
        return (topValue * (1.0f - yF)) + (bottomValue * yF);
    }

    T GetTrilinear(float x, float y, float z)
    {
        if (depth_ < 2)
            return GetBilinear(x, y);

        x = math::Clamp(x * width_ - 0.5f, 0.0f, (float)(width_ - 1));
        y = math::Clamp(y * height_ - 0.5f, 0.0f, (float)(height_ - 1));
        z = math::Clamp(z * depth_ - 0.5f, 0.0f, (float)(depth_ - 1));

        int xI = (int)x;
        int yI = (int)y;
        int zI = (int)z;
        if (zI == depth_ - 1)
            return GetBilinear(x, y);

        // Determine blend weights
        const float dx = x - floorf(x);
        const float dy = y - floorf(y);
        const float dz = z - floorf(z);
        const float invDX = 1.0f - dx;
        const float invDY = 1.0f - dy;
        const float invDZ = 1.0f - dz;

        T topValueNear = GetPixel(xI, yI, zI) * invDX + GetPixel(xI + 1, yI, zI) * dx;
        T bottomValueNear = GetPixel(xI, yI + 1, zI) * invDX + GetPixel(xI + 1, yI + 1, zI) * dx;
        T valueNear = topValueNear * invDY + bottomValueNear * dy;

        T topValueFar = GetPixel(xI, yI, zI + 1) * invDX + GetPixel(xI + 1, yI, zI + 1) * dx;
        T bottomValueFar = GetPixel(xI, yI + 1, zI + 1) * invDX + GetPixel(xI + 1, yI + 1, zI + 1) * dx;
        T valueFar = topValueFar * invDY + bottomValueFar * dy;
        return valueNear * invDZ + valueFar * dz;
    }

    inline size_t ToCoord(size_t x, size_t y, size_t z) const {
        x = math::Clamp<size_t>(x, 0, width_ - 1);
        y = math::Clamp<size_t>(y, 0, height_ - 1);
        z = math::Clamp<size_t>(z, 0, depth_ - 1);
        return z * width_ * height_ + y * width_ + x;
    }

    BlockMap* GenerateMipmap() 
    {
        size_t depth = std::max(depth_ / 2, 1);
        size_t height = std::max(height_ / 2, 1);
        size_t width = std::max(width_ / 2, 1);
        BlockMap* newBlock = new BlockMap(width, height, depth);

        for (unsigned z = 0; z < depth; ++z)
        {
            const float dz = (depth_ > 1 && depth > 1) ? (float)z / (float)(depth - 1) : 0.0f;
            for (unsigned y = 0; y < height; ++y)
            {
                const float dy = (height_ > 1 && height > 1) ? (float)y / (float)(height - 1) : 0.0f;
                for (unsigned x = 0; x < width; ++x)
                {
                    const float dx = (width_ > 1 && width > 1) ? (float)x / (float)(width - 1) : 0.0f;
                    newBlock->SetPixel(GetTrilinear(dx, dy, dz), x, y, z);
                }
            }
        }
        mipmap_ = newBlock;
        return mipmap_;
    }

    typedef void(*BLOCKMAP_FILLER)(float, float, float);
    /// For calculating LUTs and the like.
    void Compute(BLOCKMAP_FILLER function)
    {
        for (unsigned z = 0; z < depth_; ++z)
        {
            const float dz = depth_ > 1 ? (float)z / (float)(depth_ - 1) : 0.0f;
            for (unsigned y = 0; y < height_; ++y)
            {
                const float dy = height_ > 1 ? (float)y / (float)(height_ - 1) : 0.0f;
                for (unsigned x = 0; x < width_; ++x)
                {
                    const float dx = width_ > 1 ? (float)x / (float)(width_ - 1) : 0.0f;
                    SetPixel(function(dx, dy, dz), x, y, z);
                }
            }
        }
    }

    /// Takes a 3D texture, and turns it into a horizontal strip.
    BlockMap* StripFromVolume()
    {
        BlockMap* ret = new BlockMap(width_ * depth_, height_);
        for (unsigned z = 0; z < depth_; ++z)
        {
            for (unsigned y = 0; y < height_; ++y)
            {
                for (unsigned x = 0; x < width_; ++x)
                    ret->SetPixel(GetPixel(x, y, z), x * z, y);
            }
        }
        return ret;
    }

    /// Takes a 2D texture, and turns it into a 3D equal sized volume.
    BlockMap* VolumeFromStrip()
    {
        unsigned layers = width_ / height_;
        BlockMap* ret = new BlockMap(height_, height_, layers);
        for (unsigned y = 0; y < height_; ++y)
        {
            for (unsigned x = 0; x < width_; ++x)
                ret->SetPixel(GetPixel(x, y), x % height_, y, x / height_);
        }
        return ret;
    }
};

}
