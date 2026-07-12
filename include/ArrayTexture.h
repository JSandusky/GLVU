//****************************************************************************
//
//  File:       ArrayTexture.h
//  License:    MIT
//  Project:    GLVU
//  Contents:   Utility type for managing texture arrays containing variable
//              contents that aren't suitable to source from a DDS/KTX file.
//
//  Usage:      Gobo, ramp/LUT tables
//
//****************************************************************************

#pragma once

#include "glvu.h"
#include "glvu_math.h"
#include "Blob.h"

#include <map>
#include <set>

namespace GLVU
{

class Buffer;
class GeometryLayout;
struct Geometry;
class Texture;

/// Assistance class for packing images into a Texture2D array. Part of core
/// because lighting assumes that arrays are available for Gobos and decals.
class GLVU_API ArrayTexture
{
public:
    ArrayTexture(std::shared_ptr<Texture> texture);
    ~ArrayTexture();

    std::shared_ptr<Texture> GetTexture() const { return texture_; }
    bool IsValid() const;

    /// Returns the label
    std::string GetName(uint32_t slot) const;
    /// Returns the index of the slot with the given label. UINT_MAX if not occupied
    uint32_t IndexOf(const char* name) const;
    
    /// Blob data is expected to be a full mip-chain
    uint32_t LoadSlot(const std::string& slotName, Blob** data, uint32_t dataCt);
    /// Loads a basic image file into the slot we've been given.
    uint32_t LoadSlot(const std::string& slotName, const std::string& fileName);

    /// Marks slot open.
    void FreeSlot(uint32_t slot);
    /// Returns true if slot is occupied.
    bool UsesSlot(uint32_t slot) const;
    /// Returns the total count of slots possible.
    uint32_t Capacity() const { return (uint32_t)(freeSlots_.size() + usedSlots_.size()); /*dirty cheat*/ }
    /// Returns the number of slots available.
    uint32_t Available() const { return (uint32_t)freeSlots_.size(); }
    /// Returns the number of slots used.
    uint32_t Used() const { return (uint32_t)usedSlots_.size(); }

    /// Calls out for mip generation.
    void GenerateMipmaps();

private:
    /// backing texture.
    std::shared_ptr<Texture> texture_;
    /// List of used slots.
    std::set<uint32_t> usedSlots_;
    /// List of free slots.
    std::set<uint32_t> freeSlots_;
    /// Names for each slot.
    std::map<std::string, uint32_t> nameToSlot_;
};

/// Manages a collection of equally limited meshes that share the same buffers.
/// A consequence is that they have considerable waste based on the unused capacities.
class GLVU_API MergeInstanceArray : public GPUObject
{
public:
    MergeInstanceArray(GraphicsDevice* device, uint32_t maxTris, uint32_t totalModelCount);
    virtual ~MergeInstanceArray();

    struct VertexType
    {
        half pos[4];
        half qTan[4];
        half tex[2]; // [2] = UDIM slot

        static VertexType Default;

        // only for identify
        inline bool operator==(const VertexType& rhs) const {
            return  pos[0] == rhs.pos[0] &&
                    pos[1] == rhs.pos[1] &&
                    pos[2] == rhs.pos[2];
        }
    };

    struct MergeItem {
        // goes into the last row of instance transform
        uint32_t vertexStart_ = 0;
        uint32_t vertexCount_ = 0;
        uint32_t slot_ = 0;
        uint32_t indexCount_ = 0;

        // does not go into the instancing data
        AABB bounds_; 
        RS_Identifier name_;

        inline bool NotNull() const { return indexCount_ != 0; }
        inline bool IsNull() const { return indexCount_ == 0; }
    };

    void SetupGeometry(const MergeItem& forItem, std::shared_ptr<Geometry> geom);

    inline uint32_t GetTotalVertices() const { return totalVertices_; }
    inline uint32_t GetPrimitivesPerItem() const { return trianglesPerItem_; }
    inline uint32_t GetIndicesPerItem() const { return trianglesPerItem_ * 3; }
    inline uint32_t GetMaxModelCount() const { return count_; }

    MergeItem Add(const char* name, const std::vector<VertexType>& verts, const std::vector<uint32_t>& indices) { return Add(name, (void*)verts.data(), verts.size(), (void*)indices.data(), indices.size(), true); }
    MergeItem Add(const char* name, const std::vector<VertexType>& verts, const std::vector<uint16_t>& indices) { return Add(name, (void*)verts.data(), verts.size(), (void*)indices.data(), indices.size(), false); }
    MergeItem Add(const char* name, void* vertexData, uint32_t vertexCount, void* indexData, uint32_t indexCount, bool largeIndices);

    void Remove(const MergeItem& item) { Remove(item.slot_); }
    void Remove(uint32_t slot);

    MergeItem GetItem(uint32_t slot) const {
        if (usedSlots_.find(slot) != usedSlots_.end())
            return items_[slot];
        return { };
    }

    std::shared_ptr<Geometry> GetGeometry() const { return geom_; }
    std::shared_ptr<Buffer> GetVertexStructuredBuffer() const { return vertexSSBO_; }
    std::shared_ptr<Buffer> GetIndexBuffer() const { return indexBuffer_; }

    // Convert fat 32-bit float data into VertexType structure for merge instancing.
    static void ConvertFatToHalf(std::vector<VertexType>& output, void* srcData, uint32_t numVerts, uint32_t posOffset, uint32_t texOffset, uint32_t qTanOffset, uint32_t stride);

private:
    uint32_t GetSlot();

    std::shared_ptr<Buffer> vertexSSBO_, indexBuffer_;
    std::shared_ptr<GeometryLayout> layout_;
    std::shared_ptr<Geometry> geom_;

    uint32_t count_;
    uint32_t maxVerticesPerItem_;
    uint32_t trianglesPerItem_;
    uint32_t totalVertices_;
    
    std::vector<MergeItem> items_;

    /// List of used slots.
    std::set<uint32_t> usedSlots_;
    /// List of free slots.
    std::set<uint32_t> freeSlots_;
};

/// More advanced than merge-instancing. Meshes are clumped into 256 triangle patches
/// which can be culled individually.
class GLVU_API MeshClusterArray : public GPUObject
{
    static const uint32_t trianglesPerCluster_ = 256;
    uint64_t curVtxOffset_ = 0;
    uint64_t curIdxOffset_ = 0;
    uint64_t vertCapacity_;
    uint64_t indexCapacity_;
    uint32_t curSlot_ = 0;
    uint32_t curMesh_ = 0;
public:
    MeshClusterArray(GraphicsDevice*, uint64_t vertexCapacity, uint64_t triCapacity);
    virtual ~MeshClusterArray();

    struct Vertex {
        half pos[3];
        half tex[2]; // [2] = UDIM slot
        half qTan[4];
    };

    struct Cluster {
        math::Sphere bounds;
        math::float3 coneCenter;
        int valid; // if invalid then skip cone test and assume passes
        math::float3 coneDir;
        float coneAperture;
        int slot;

        uint32_t vertexStart_;
        uint32_t meshIndex_; // the same on all clusters from the same mesh
        uint32_t indexStart_;
        uint32_t indexCount_;
    };

    struct Mesh {
        math::Sphere bounds; // total bounds of all triangles.
        uint32_t slot;
        std::vector<Cluster> clusters;
    };

    Mesh* Create(Vertex* vertexData, uint32_t vertexct, uint16_t* indexData, uint32_t indexCt);
    Mesh* Create(const Mesh& storedData, Vertex* vertexData, uint32_t vertexct, uint16_t* indexData, uint32_t indexCt);

    inline bool CheckCapacity(uint32_t verts, uint32_t indexCt) const { return curIdxOffset_ + indexCt <= indexCapacity_&& curVtxOffset_ + verts <= vertCapacity_; }

    std::shared_ptr<Buffer> GetVertexBuff() const;
    std::shared_ptr<Buffer> GetIndexBuff() const;
    std::shared_ptr<Buffer> GetMeshDataBuff() const;

private:
    std::vector<Mesh*> meshes_;
    std::shared_ptr<Buffer> vertexBuff_;
    std::shared_ptr<Buffer> indexBuff_;
    mutable std::shared_ptr<Buffer> meshDataBuff_;
};

}