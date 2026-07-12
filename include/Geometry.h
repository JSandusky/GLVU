//****************************************************************************
//
//  File:       Geometry.h
//  License:    MIT
//  Project:    GLVU
//  Contents:   Geometric information required for setting up vertex streams
//              and combinations of vertex+index buffers in order to draw.
//
//****************************************************************************

#pragma once

#include "glvu.h"

#include "Buffer.h"

namespace GLVU
{

    struct Geometry;
    class Material;
    class ShaderPass;

    /// A singular piece of vertex data from a blob (ie. POSITION, TEXCOORD0, COLOR0, etc).
    struct GLVU_API VertexInfo
    {
        /// Nature of the data here.
        VertexAttribute attribute_;
        /// Element type of each member of the vector (float, int, byte).
        VertexDataType type_;
        /// Offset into the local structure.
        uint16_t offset_;
        /// Size of a vector, vec3, uvec3, etc.
        uint16_t elementCount;
        /// Vertex-buffer binding this data is read from.
        uint16_t bufferSlot_;
        /// Stride FOR THE BUFFER SLOT, this info belongs to.
        uint16_t stride_;
        /// Element data is normalized, ie rgba8.
        bool normalized_;
        /// Indicates the advancement of the VBO data is per instance.
        uint8_t instanceStride_;
    };

    /// A complete vertex-input configuration mapping 1 or many vertex-buffers into discrete data-elements for shader use.
    /// Only 16 attributes are supported.
    class GLVU_API GeometryLayout : public GPUObject
    {
    public:
        /// Construct.
        GeometryLayout(GraphicsDevice* device);
        /// Destruct.
        ~GeometryLayout();

        /// Basic test, low reliability.
        virtual bool IsValid() const override;
        ///Dump any GPU objects.
        virtual void Release() override;

        /// Pushes new data (max 16).
        void AddVertexInfo(const VertexInfo& info);
        /// Returns true if there's system instancing attributes.
        bool HasInstancing() const { return instanceBufferIndex_ != -1; }
        /// Deals with boiler-plate for activating this layout.
        void Bind(Geometry* forGeo, const std::vector<std::shared_ptr<Buffer>>& extraBuffers, bool instanceDataOnly = false);

        /// Creates a deep copy.
        std::shared_ptr<GeometryLayout> Clone();
        /// Creates a deep copy with extra attributes for automatic instancing.
        std::shared_ptr<GeometryLayout> GetInstancedVariant(bool forVR);

        /// Flat vector of data.
        VertexInfo vertexData_[16];
        /// Number of elements.
        uint32_t vertexDataCount_ = 0;
        /// Vertex-binding slot for the system instancing attributes buffer.
        uint32_t instanceBufferIndex_ = -1;
#if defined(GLVU_VK)
        /// Vulkan has a concrete object.
        VezVertexInputFormat vertexObject_ = { 0 };
#elif defined(GLVU_D3D12)
        std::vector<D3D12_INPUT_ELEMENT_DESC> elementDesc_;
#endif

        /// Tests the availability of the attribute
        bool HasAttribute(VertexAttribute attr) const 
        {
            for (uint32_t i = 0; i < vertexDataCount_; ++i)
                if (vertexData_[i].attribute_ == attr)
                    return true;
            return false;
        }

        /// Returns the acquired vertex info or null for the given attribute.
        const VertexInfo* Info(VertexAttribute attr) const 
        {
            for (uint32_t i = 0; i < vertexDataCount_; ++i)
                if (vertexData_[i].attribute_ == attr)
                    return &vertexData_[i];
            return nullptr;
        }

        /// Gets the size of each vertex-element of a buffer.
        size_t ElementSize(size_t bufferIndex = 0) const { 
            for (uint32_t i = 0; i < vertexDataCount_; ++i)
                if (vertexData_[i].bufferSlot_ == bufferIndex)
                    return vertexData_[i].stride_;
            return 0;
        }

        /// Returns number of vertices in a buffer based on its size.
        size_t CountOf(size_t dataSize, size_t bufferIndex = 0) const
        {
            size_t elem = ElementSize(bufferIndex);
            if (elem == 0)
                return 0;
            return dataSize / elem;
        }

    private:
        /// Utility for reliability.
        void AttachInstanceInfo(bool forVR);

        /// In a non-instanced geometry this holds a duplicate with instancing data, so end-user doesn't deal with it.
        std::shared_ptr<GeometryLayout> instancedVariant_;
        std::shared_ptr<GeometryLayout> vrInstancedVariant_;
    };

    /// A collection of buffers and meta-data used to create a draw-call. Most members should has have obvious purposes.
    struct GLVU_API Geometry
    {
        ~Geometry() { Release(); }

        /// Optional alternative geometry that can be used for shadow drawing, ie. positions only.
        std::shared_ptr<Geometry> shadowGeometry_;
        std::vector< std::shared_ptr<Buffer> > vertexBuffers_;
        std::shared_ptr<Buffer> indexBuffer_;
        std::shared_ptr<GeometryLayout> layout_;

        math::AABB bounds_;
        uint32_t indexStart_;
        uint32_t primCount_;
        uint32_t vertexCount_;
        uint32_t indexCount_;
        PrimitiveType primType_;

        inline uint32_t GetIndexCount() const {
            if (primType_ == TRIANGLE_LIST) return 3 * primCount_;
            if (primType_ == POINT_LIST) return primCount_;
            if (primType_ == LINE_LIST) return primCount_ * 2;
            return 3 * primCount_;
        }

        /// Helper for constructing a new geometry. Most values are inferred.
        static std::shared_ptr<Geometry> Create(PrimitiveType prim, std::shared_ptr<GeometryLayout> layout, std::shared_ptr<Buffer> vtx, std::shared_ptr<Buffer> idx);
        static std::shared_ptr<Geometry> Create(PrimitiveType prim, std::shared_ptr<GeometryLayout> layout, const std::vector<std::shared_ptr<Buffer> >& vtx, std::shared_ptr<Buffer> idx);
        static std::shared_ptr<Geometry> CreateForMergeInstancing(unsigned vertexCount);

        void InferValuesFromData();

        void Release();

        std::shared_ptr<Geometry> Clone();
    };

    /// Encapsulation of the information that is needed to draw some arbitrary geometries.
    /// Beware that Batch uses raw pointers for performance, copying std::shared_ptr has significant overhead
    /// when done by the thousands. During the flight of a frame the geometry/material must not be destroyed.
    /// Although the overhead may sound worth accepting it's important to remember that shadows result in
    /// more batch construction than you'd suspect from just looking at a rendered frame.
    struct GLVU_API Batch
    {
        /// Object that produced this batch.
        void* source_;
        /// Geometry.
        Geometry* geometry_;
        /// Material used for drawing this geometry.
        Material* material_;
        /// List of matrices involved, may be just 1, may be the bones, or may be instance transforms.
        math::float4x4* transforms_;
        /// At construction set this to the desired pixel size, note: the region provided will always be square. At draw it will be valid.
        math::float4 lightingCell_;
        /// Number of transforms_.
        uint32_t numTransforms_;
        /// Typically the distance to camera, but it's up to the end-user.
        float computedSortDistance_;
        /// Sequence of renderering.
        uint8_t renderOrder_;
        /// if skinned then transforms are treated as bone-matrices.
        bool isSkinned_;
        /// if instancing is allowed then a pass will be made to coalesce based on geometry and material.
        bool canInstance_;

        Batch() : source_(nullptr), transforms_(nullptr), numTransforms_(0) { }

        /// Construction and vector<T>::push_back are a real bottleneck.
        Batch(Geometry* g, Material* m, math::float4x4* transforms, const uint32_t& transformCt, const bool& skinned, const bool& canInstance, const float& computedSort = 1.0f) :
            geometry_(g),
            material_(m),
            transforms_(transforms),
            numTransforms_(transformCt),
            computedSortDistance_(computedSort),
            canInstance_(canInstance),
            isSkinned_(skinned),
            lightingCell_(float4::zero),
            source_(nullptr),
            renderOrder_(128)
        {
        }

        inline bool operator<(const Batch& rhs)
        {
#define BATCH_COMP(VALUE) if (VALUE < rhs.VALUE) { return true; } else if (VALUE > rhs.VALUE) { return false; }

            BATCH_COMP(computedSortDistance_);
            BATCH_COMP(material_);
            BATCH_COMP(geometry_);

            return true;
        }

    private:
        friend class RenderScript;
        std::shared_ptr<Buffer> bonesBuffer_;
    };

    GLVU_API bool CalculateNormals(Geometry& geometry);
    GLVU_API bool CaclulateTangents(Geometry& geometry);

    GLVU_API math::Quat CalculateBillboard(BillboardType type, const math::float3& position, const math::Quat& rotation, const math::float4x4& viewMatrix);
    GLVU_API void CalculateTrail(const math::float3& viewUp, const math::float3& viewDir, const std::vector<math::float3>& positions);

}
