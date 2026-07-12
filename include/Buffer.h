//****************************************************************************
//
//  File:       Buffer.h
//  License:    MIT
//  Project:    GLVU
//  Contents:   Wrapper object for vertex, index, uniform, and shader-storage
//              buffers. Contains metadata tags about usage intent as well
//              CPU side mirroring/dirty management.
//
//****************************************************************************

#pragma once

#include "glvu.h"
#include "Blob.h"

#include <vector>
#include <memory>

namespace GLVU
{
    class GeometryLayout;
    class GraphicsDevice;

    /// A vertex, index, uniform, or shader-storage buffer object. Texel-buffers are done in
    /// in the GLVU::Texture class, and one big giant hack.
    /// Unlock textures this can maintain a CPU-side *shadow* copy that can be used for whenever
    /// the underlying buffer data needs to be read without doing a GPU->CPU read (ie. geometry)
    class GLVU_API Buffer  : public GPUObject
    {
        friend class GraphicsDevice;
        
    public:
        /// Construct and specify the nature.
        Buffer(GraphicsDevice*, BufferKind);
        /// Destruct.
        ~Buffer();
        
        /// Dispose of GPU objects.
        virtual void Release() override;
        /// Basic tests.
        virtual bool IsValid() const override;

        /// Sets data, and resizes the buffer if necessary.
        void SetData(void* data, uint32_t size);
        /// Sets only a portion of the data, requires an existing buffer.
        void SetSubData(void* data, uint32_t offset, uint32_t size);
        /// Used for glBindBufferRange and kin.
        void SetSharedData(uint32_t index, void* data, uint32_t offset, uint32_t size, uint32_t blockSize);

        /// Preallocate a set of space for the buffer.
        void SetSize(uint32_t size);
        /// Returns the bound nature of the buffer.
        inline BufferKind GetBufferKind() const { return kind_; }
        /// Returns the known size of the buffer.
        inline uint32_t GetSize() const { return size_; }
        /// Copies dirty shadow-data over to the GPU.
        void ApplyShadowData(bool force = false) { if (shadowDirty_ || force) SetData(shadowData_.get(), shadowSize_); shadowDirty_ = false; }

        /// Returns the raw shadow-data.
        unsigned char* GetShadowData() const { return shadowData_.get(); }
        /// USE BEFORE ALLOCATING, indicates a CPU local copy should be kept.
        void SetShadowed(bool state) { shadowed_ = state; }
        /// Check whether shadow-data is valid.
        inline bool IsShadowed() const { return shadowed_; }
        /// Check whether shadow-data is dirty.
        inline bool IsShadowDirty() const { return shadowDirty_; }
        /// Writes data into the shadow-data and marks it as dirty. Use for delaying transfers as long as possible. Not particularly meaningful internally.
        void WriteIntoShadow(void* data, uint32_t offset, uint32_t size) { 
            if (shadowData_ == nullptr || shadowSize_ < (offset + size))
            {
                shadowSize_ = (offset + size);
                shadowData_.reset(new unsigned char[(offset + size)]);
            }
            memcpy(shadowData_.get() + offset, data, size); 
            shadowDirty_ = true; 
        }

        /// Sets extra meta-data for this buffer, such as index-size or intended primitive type.
        void SetTag(BufferTag tag) { tags_ |= tag; }
        /// Checks whether a tag is present.
        bool HasTag(BufferTag tag) const { return tags_ & tag; }

		/// Returns the provided stride data.
		uint32_t GetStride() const { return stride_; }
		/// Sets the specified stride for elements of this buffer, this typically only applies to structured-buffers.
		void SetStride(uint32_t stride) { stride_ = stride; }

        /// Write-only access.
        void* Map();
        /// Should always be shortly after map.
        void Unmap();

		Blob GetGPUData() const;

        template<typename T>
        inline void SetData(const std::vector<T>& data) { SetData((void*)data.data(), data.size() * sizeof(T)); }
        template<typename T>
        inline void SetSubData(const std::vector<T>& data) { SetSubData((void*)data.data(), 0u, data.size() * sizeof(T)); }

        void SetLayout(const std::shared_ptr<GeometryLayout>&);
        std::shared_ptr<GeometryLayout> GetLayout() const { return layout_; }

    #if defined(GLVU_OPENCL_INTEORP)
        cl_mem CLHandle();
    #endif

    #if defined(GLVU_GL)
    public:
        /// GlBuffer object.
        const GLuint GetGPUObject() const { return buffer_; }
    private:
        /// GLbuffer object.
        GLuint buffer_;
    #elif defined(GLVU_VK)
    public:
        /// VKBuffer object.
        inline VkBuffer GetGPUObject() const { return buffer_; }
    private:
        /// VkBuffer object, or 0
        VkBuffer buffer_;
    #elif defined(GLVU_PICA)
    #elif defined(GLVU_DX11)
		ID3D11UnorderedAccessView* GetUAV();

        ID3D11Buffer* buffer_ = nullptr;
		ID3D11ShaderResourceView* srv_ = nullptr;
		ID3D11UnorderedAccessView* uav_ = nullptr;

        ID3D11ShaderResourceView* GetEphemeralView(uint32_t start, uint32_t length);
	private:
		void Create(size_t size, void* data);
    #else
        ID3D12Resource* buffer_;
        D3D12MA::Allocation* bufferMem_;
    #endif

    private:
        /// Only applicable to VertexBuffers.
        std::shared_ptr<GeometryLayout> layout_;
        /// CPU mirror of the data in the buffer.
        std::unique_ptr<unsigned char[]> shadowData_;
        /// Binding nature of this buffer.
        BufferKind kind_;
        /// Tagged metadata.
        uint32_t tags_;
        /// Allocated size of the buffer.
        uint32_t size_;
        /// Allocated size of the shadow buffer.
        uint32_t shadowSize_;
		/// Index/Vertex or struct stride.
		uint32_t stride_;
        /// Marks whether this buffer needs to maintain a CPU mirror.
        bool shadowed_;
        /// Dirty-status flag for the shadow-data of this buffer.
        bool shadowDirty_;
    };
    
}