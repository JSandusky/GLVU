//****************************************************************************
//
//  File:       ArrayTexture.cpp
//  License:    MIT
//  Project:    GLVU
//  Contents:   Implementation of ArrayTexture utility class for managing
//              texture arrays that are built at runtime.
//
//****************************************************************************

#include "ArrayTexture.h"

#include "Buffer.h"
#include "Geometry.h"
#include "GraphicsDevice.h"
#include "Texture.h"

#include <algorithm>
#include <numeric>

namespace GLVU
{

//****************************************************************************
//
//  Function:   ArrayTexture::ArrayTexture
//
//  Purpose:    Construct, take hold of managed texture and setup as all
//              slots available.
//
//****************************************************************************
ArrayTexture::ArrayTexture(std::shared_ptr<Texture> texture) :
    texture_(texture)
{
    assert(texture_);
    for (uint32_t i = 0; i < texture_->GetLayers(); ++i)
        freeSlots_.insert(i);
}

//****************************************************************************
//
//  Function:   ArrayTexture::~ArrayTexture
//
//  Purpose:    Destruct
//
//****************************************************************************
ArrayTexture::~ArrayTexture()
{

}

//****************************************************************************
//
//  Function:   ArrayTexture::IsValid
//
//  Purpose:    General utility.
//
//  Return:     True if probably legitimate.
//
//****************************************************************************
bool ArrayTexture::IsValid() const
{
    return texture_ && texture_->IsValid();
}

//****************************************************************************
//
//  Function:   ArrayTexture::GetName
//
//  Purpose:    Brute force through table of names->slots to match a slot
//              to a name.
//
//  Return:     Name found or an empty string
//
//****************************************************************************
std::string ArrayTexture::GetName(uint32_t slot) const
{
    for (auto record : nameToSlot_)
        if (record.second == slot)
            return record.first;
    return std::string();
}

//****************************************************************************
//
//  Function:   ArrayTexture::IndexOf
//
//  Purpose:    Lookup name in table to find a registered slot.
//
//  Return:     Slot index, or UINT_MAX if not found
//
//****************************************************************************
uint32_t ArrayTexture::IndexOf(const char* name) const
{
    auto found = nameToSlot_.find(name);
    if (found != nameToSlot_.end())
        return found->second;
    return UINT_MAX;
}

//****************************************************************************
//
//  Function:   ArrayTexture::LoadSlot
//
//  Purpose:    Checks free-slots for availability of a slot to insert the given 
//              data mipmap chain into.
//
//  Return:     Index of assigned slot, UINT_MAX if there's no available slots.
//
//****************************************************************************
uint32_t ArrayTexture::LoadSlot(const std::string& slotName, Blob** data, uint32_t dataCt)
{
    if (freeSlots_.size() == 0 || !IsValid())
        return UINT_MAX;

    auto slot = *freeSlots_.begin();
    freeSlots_.erase(slot);
    usedSlots_.insert(slot);

    uint32_t w = texture_->GetWidth();
    uint32_t h = texture_->GetHeight();
    for (uint32_t i = 0; i < dataCt; ++i)
    {
        texture_->SetSubData(data[i]->data_, 0, 0, 0, w, h, 0, i, slot);
        
        w /= 2;
        h /= 2;
        w = std::max(w, 1u);
        h = std::max(h, 1u);
    }

    nameToSlot_[slotName] = slot;

    return slot;
}

//****************************************************************************
//
//  Function:   ArrayTexture::LoadSlot
//
//  Purpose:    Checks free-slots for availability of a slot to insert a texture
//              loaded from the given filename.
//
//  Return:     Index of assigned slot, UINT_MAX if there's no available slots.
//
//****************************************************************************
uint32_t ArrayTexture::LoadSlot(const std::string& slotName, const std::string& fileName)
{
    if (freeSlots_.size() == 0 || !IsValid())
        return UINT_MAX;

    auto slot = *freeSlots_.begin();
    freeSlots_.erase(slot);
    usedSlots_.insert(slot);

    Texture::LoadFileToLayer(texture_, fileName.c_str(), slot);
    nameToSlot_[slotName] = slot;

    return slot;
}

//****************************************************************************
//
//  Function:   ArrayTexture::FreeSlot
//
//  Purpose:    Releases the handle for a given slot. Doesn't mess with the
//              underlying texture or even bother to update the names table.
//
//****************************************************************************
void ArrayTexture::FreeSlot(uint32_t slot)
{
    usedSlots_.erase(slot);
    freeSlots_.insert(slot);
    for (auto it = nameToSlot_.begin(); it != nameToSlot_.end(); ++it)
    {
        if (it->second == slot)
        {
            nameToSlot_.erase(it);
            break;
        }
    }
}

//****************************************************************************
//
//  Function:   ArrayTexture::UsesSlot
//
//  Purpose:    Utility
//
//  Return:     True if the given slot is used.
//
//****************************************************************************
bool ArrayTexture::UsesSlot(uint32_t slot) const
{
    return usedSlots_.find(slot) != usedSlots_.end();
}

//****************************************************************************
//
//  Function:   ArrayTexture::GenerateMipmaps
//
//  Purpose:    Just wraps the call to the backing texture, minor usability.
//
//****************************************************************************
void ArrayTexture::GenerateMipmaps()
{
    if (texture_ && texture_->IsValid())
        texture_->GenerateMipMaps();
}

MergeInstanceArray::VertexType MergeInstanceArray::VertexType::Default = {
    { 0.0f, 0.0f, 0.0f, 0.0f },
    { 0.0f, 0.0f, 0.0f, 0.0f },
    { 0.0f, 0.0f },
};

//****************************************************************************
//
//  Function:   MergeInstanceArray::MergeInstanceArray
//
//  Purpose:    Constructs and configures for specific capacity. In practice
//              multiple MergeInstanceArrays should be used for narrow bands
//              of vertex-counts instead of stuffing everything into a super-container.
//
//****************************************************************************
MergeInstanceArray::MergeInstanceArray(GraphicsDevice* device, uint32_t maxTriangles, uint32_t totalModelCount) :
    GPUObject(device),
    totalVertices_(maxTriangles * 3 * totalModelCount),
    trianglesPerItem_(maxTriangles),
    count_(totalModelCount),
    maxVerticesPerItem_(maxTriangles * 3)
{
    { // setup items and slots
        items_.resize(totalModelCount);
        std::vector<uint32_t> slots(totalModelCount);
        std::iota(slots.begin(), slots.end(), 0);
        freeSlots_.insert(slots.begin(), slots.end());
    }

    { // setup layout, this is mostly for cases where it needs to be referenced, we never have a real vertexbuffer
        layout_ = std::make_shared<GeometryLayout>(nullptr);

        layout_->AddVertexInfo({
            VA_POSITION,
            VDT_HALF,
            offsetof(VertexType, pos), // offset
            3, // element ct
            0,
            sizeof(VertexType),
            false,
            0 // instance stride
            });
        layout_->AddVertexInfo({
            VA_TEXCOORD0,
            VDT_HALF,
            offsetof(VertexType, tex),
            2,
            0,
            sizeof(VertexType),
            0
            });
        layout_->AddVertexInfo({
            VA_TANGENT,
            VDT_HALF,
            offsetof(VertexType, qTan),
            4,
            0,
            sizeof(VertexType),
            false,
            0
            });
    }
    std::vector<VertexType> defaultData(totalVertices_);
    std::fill(defaultData.begin(), defaultData.end(), VertexType::Default);

    vertexSSBO_ = device->CreateShaderStorageBuffer();
    vertexSSBO_->SetStride(sizeof(VertexType));
    vertexSSBO_->SetSize(sizeof(VertexType) * totalVertices_);
    vertexSSBO_->SetData(defaultData);

    indexBuffer_ = device->CreateIndexBuffer();
    indexBuffer_->SetTag(BufferTag::BufferTag_32Bit|BufferTag::BufferTag_Compute);
    indexBuffer_->SetStride(sizeof(uint32_t));
    indexBuffer_->SetSize(trianglesPerItem_ * count_ * sizeof(uint32_t));
}

//****************************************************************************
//
//  Function:   MergeInstanceArray::~MergeInstanceArray
//
//  Purpose:    Destruct. Natural destruction will take care of everything.
//
//****************************************************************************
MergeInstanceArray::~MergeInstanceArray()
{

}

//****************************************************************************
//
//  Function:   MergeInstanceArray::Add
//
//  Purpose:    Inserts the specified vertex data while resolving an index-buffer.
//              If the number of indices exceeds the capacity then the remainder
//              will be discarded.
//
//  Return:     Valid MergeItem record if success.
//
//****************************************************************************
MergeInstanceArray::MergeItem MergeInstanceArray::Add(const char* name, void* vertexData, uint32_t vertexCount, void* indexData, uint32_t indexCount, bool largeIndices)
{
    if (indexCount / 3 > trianglesPerItem_)
        return MergeItem();

    auto slot = GetSlot();
    if (slot == UINT_MAX)
        return MergeItem();

    std::vector<VertexType> sendData(maxVerticesPerItem_);
    std::fill(sendData.begin(), sendData.end(), VertexType::Default);
    std::vector<uint32_t> sendIndex(trianglesPerItem_ * 3);
    std::fill(sendIndex.begin(), sendIndex.end(), 0ul);

    AABB bnds;
    bnds.SetNegativeInfinity();

    for (uint32_t v = 0; v < vertexCount && v < maxVerticesPerItem_; ++v)
    {
        const VertexType& d = ((VertexType*)vertexData)[v];
        sendData[v] = d;
        bnds.Enclose(float3(d.pos[0].ToFloat(), d.pos[1].ToFloat(), d.pos[2].ToFloat()));
    }

    const auto indicesPerItem = GetIndicesPerItem();
    if (largeIndices)
        memcpy(sendIndex.data(), indexData, Min(indexCount, indicesPerItem) * sizeof(uint32_t));
    else
    {
        for (uint32_t i = 0; i < indexCount && i < indicesPerItem; ++i)
            sendIndex[i] = ((uint16_t*)indexData)[i];
    }
    
    vertexSSBO_->SetSubData(sendData.data(), slot * maxVerticesPerItem_ * sizeof(VertexType), sendData.size() * sizeof(VertexType));
    indexBuffer_->SetSubData(sendIndex.data(), slot * indicesPerItem * sizeof(uint32_t), sendIndex.size() * sizeof(uint32_t));

    MergeItem r = { 
        slot * maxVerticesPerItem_, 
        sendData.size(), 
        slot, 
        Min(indexCount, indicesPerItem), 
        bnds, 
        MakeID(name) 
    };
    items_[slot] = r;
    return r;
}

//****************************************************************************
//
//  Function:   MergeInstanceArray::GetSlot
//
//  Purpose:    Acquires the first free slot, or returns UINT_MAX if out of space.
//
//****************************************************************************
uint32_t MergeInstanceArray::GetSlot()
{
    if (freeSlots_.empty())
        return UINT_MAX;

    auto slot = *freeSlots_.begin();
    freeSlots_.erase(slot);
    usedSlots_.insert(slot);
    return slot;
}

//****************************************************************************
//
//  Function:   MergeInstanceArray::Remove
//
//  Purpose:    Removes the record at the given slot.
//
//****************************************************************************
void MergeInstanceArray::Remove(uint32_t slot)
{
    // not marked as used? do nothing
    if (usedSlots_.find(slot) != usedSlots_.end())
    {
        usedSlots_.erase(slot);
        freeSlots_.insert(slot);
    }
}

//****************************************************************************
//
//  Function:   MergeInstanceArray::ConvertFatToHalf
//
//  Purpose:    Compacts the full size vertex data (using floats) to our
//              compact representation that uses half.
//
//****************************************************************************
void MergeInstanceArray::ConvertFatToHalf(std::vector<VertexType>& output, void* srcData, uint32_t numVerts, uint32_t posOffset, uint32_t texOffset, uint32_t qTanOffset, uint32_t stride)
{
    output.reserve(numVerts);

    struct FatVertex {
        float pos[3];
        float tex[2];
        float qTan[4];
    };

    unsigned char* data = (unsigned char*)srcData;
    for (uint32_t v = 0; v < numVerts; ++v, data += stride)
    {
        float* pos = ((float*)(data + posOffset));
        float* tex = ((float*)(data + texOffset));
        float* qTan = ((float*)(data + qTanOffset));

        output.push_back({
            { pos[0], pos[1], pos[2], 0.0f },
            { qTan[0], qTan[1], qTan[2], qTan[3] },
            { tex[0], tex[1] }
        });
    }
}

//****************************************************************************
//
//  Function:   MergeInstanceArray::MergeItem::SetupGeometry
//
//  Purpose:    Fills out a geometry object to be suitable for this.
//
//****************************************************************************
void MergeInstanceArray::SetupGeometry(const MergeItem& item, std::shared_ptr<Geometry> geom)
{
    if (geom)
    {
        geom->bounds_ = item.bounds_;
        geom->vertexCount_ = maxVerticesPerItem_;
        geom->indexBuffer_ = nullptr;
        geom->vertexBuffers_.clear();
        geom->primType_ = TRIANGLE_LIST;
        geom->layout_ = layout_;
        geom->indexStart_ = 0;
        geom->indexCount_ = 0;
        geom->shadowGeometry_.reset();
        geom->primCount_ = trianglesPerItem_;
    }
}

//****************************************************************************
//
//  Function:   MeshClusterArray::MeshClusterArray
//
//  Purpose:    Constructs and configures values and buffers.
//
//****************************************************************************
MeshClusterArray::MeshClusterArray(GraphicsDevice* device, uint64_t vertexCapacity, uint64_t triCapacity) :
    GPUObject(device),
    curVtxOffset_(0),
    curIdxOffset_(0),
    curSlot_(0),
    curMesh_(0)
{
    vertexBuff_ = device->CreateVertexBuffer();
    vertexBuff_->SetTag(BufferTag::BufferTag_Compute);
    vertexBuff_->SetSize(sizeof(Vertex) * vertexCapacity);

    indexBuff_ = device->CreateIndexBuffer();
    indexBuff_->SetTag(BufferTag::BufferTag_Compute);
    indexBuff_->SetSize(sizeof(uint16_t) * triCapacity * 3);

    vertCapacity_ = vertexCapacity;
    indexCapacity_ = triCapacity * 3;
}

//****************************************************************************
//
//  Function:   MeshClusterArray::~MeshClusterArray
//
//  Purpose:    Destructs, freeing Mesh records and buffers.
//
//****************************************************************************
MeshClusterArray::~MeshClusterArray()
{
    for (auto& m : meshes_)
        delete m;
    meshes_.clear();

    vertexBuff_.reset();
    indexBuff_.reset();
}

//****************************************************************************
//
//  Function:   MeshClusterArray::Create
//
//  Purpose:    Appends vertex and index data to the managed buffers (if space remains).
//              The mesh data be sliced into clusters for rendering and a bounding-sphere
//              computed for the whole thing as well as each cluster.
//
//  Return:     Mesh* if success, null if failure.
//
//****************************************************************************
MeshClusterArray::Mesh* MeshClusterArray::Create(Vertex* vertexData, uint32_t vertexCt, uint16_t* indexData, uint32_t indexCt)
{
    if (!CheckCapacity(vertexCt, indexCt))
        return nullptr;

    Mesh* mesh = new Mesh();
    mesh->slot = curMesh_;
    std::array<math::Triangle, trianglesPerCluster_> triCache;

    const uint32_t triangleCount = indexCt / 3;
    const uint32_t clusterCount = (triangleCount + trianglesPerCluster_ - 1) / trianglesPerCluster_;

    mesh->bounds.SetNegativeInfinity();

    static auto ToFloat3 = [](half* h) { return math::float3(h[0].ToFloat(), h[1].ToFloat(), h[2].ToFloat()); };
    
    for (uint32_t c = 0; c < clusterCount; ++c)
    {
        const uint32_t clusterStart = c * trianglesPerCluster_;
        const uint32_t clusterEnd = Min(clusterStart + trianglesPerCluster_, triangleCount);
        const uint32_t clusterTriCt = clusterEnd - clusterStart;

        math::Sphere clusterBnds;
        clusterBnds.SetNegativeInfinity();

        math::float3 coneAxis = { 0.0f, 0.0f, 0.0f };

        for (uint32_t t = clusterStart; t < clusterEnd; ++t)
        {
            triCache[t - clusterStart].a = ToFloat3(vertexData[indexData[t * 3]].pos);
            triCache[t - clusterStart].b = ToFloat3(vertexData[indexData[t * 3 + 1]].pos);
            triCache[t - clusterStart].c = ToFloat3(vertexData[indexData[t * 3 + 2]].pos);

            clusterBnds.Enclose(triCache[t - clusterStart]);
            mesh->bounds.Enclose(triCache[t - clusterStart]);
            coneAxis += -triCache[t - clusterStart].NormalCCW();
        }

        float coneOpening = 1.0f;
        bool validCluster = true;
        const auto center = clusterBnds.Centroid();
        coneAxis.Normalize();
        float t = -std::numeric_limits<float>::infinity();
        for (uint32_t triIdx = 0; triIdx < clusterTriCt; ++triIdx)
        {
            const auto& tri = triCache[triIdx];
            const auto triNorm = tri.NormalCCW();
            const float dirPart = coneAxis.Dot(-triNorm);
            if (dirPart < 0)
            {
                validCluster = false;
                break;
            }

            const float td = (center - tri.a).Dot(triNorm) / -dirPart;
            t = Max(t, td);
            coneOpening = Min(coneOpening, dirPart);
        }

        Cluster cluster;
        cluster.bounds = clusterBnds;
        cluster.coneDir = coneAxis;
        cluster.coneAperture = validCluster ? sqrtf(1.0f - coneOpening * coneOpening) : 0.0f;
        cluster.coneCenter = center + coneAxis * t;
        cluster.vertexStart_ = curVtxOffset_;
        cluster.meshIndex_ = curMesh_; //??
        cluster.indexStart_ = curIdxOffset_ + clusterStart * 3;
        cluster.indexCount_ = clusterTriCt * 3;
        cluster.valid = validCluster ? 1 : 0; // if invalid then always passes
        cluster.slot = curSlot_++;
        mesh->clusters.push_back(cluster);
    }

    vertexBuff_->SetSubData(vertexData, curVtxOffset_ * sizeof(Vertex), vertexCt * sizeof(Vertex));
    indexBuff_->SetSubData(indexData, curIdxOffset_ * sizeof(uint16_t), indexCt * sizeof(uint16_t));

    curVtxOffset_ += vertexCt;
    curIdxOffset_ += indexCt;
    ++curMesh_;
    meshes_.push_back(mesh);
    return mesh;
}

//****************************************************************************
//
//  Function:   MeshClusterArray::Create
//
//  Purpose:    Appends vertex and index data to the managed buffers (if space remains).
//              Instead of calculating the bounds and apertures the provided information
//              will be used. Presumably the information has come from serializing
//              the results of a previous usage of this class (for the same role).
//
//  Return:     Mesh* if success, null if failure.
//
//****************************************************************************
MeshClusterArray::Mesh* MeshClusterArray::Create(const Mesh& storedData, Vertex* vertexData, uint32_t vertexCt, uint16_t* indexData, uint32_t indexCt)
{
    if (!CheckCapacity(vertexCt, indexCt))
        return nullptr;

    Mesh* mesh = new Mesh();
    mesh->slot = curMesh_;
    mesh->bounds = storedData.bounds;

    uint64_t idxPos = curIdxOffset_;
    uint64_t vtxStart = curVtxOffset_;
    for (uint32_t i = 0; i < storedData.clusters.size(); ++i)
    {
        const auto& srcClust = storedData.clusters[i];
        Cluster clust;
        clust.bounds = srcClust.bounds;
        clust.coneDir = srcClust.coneDir;
        clust.coneCenter = srcClust.coneCenter;
        clust.coneAperture = srcClust.coneAperture;
        clust.vertexStart_ = vtxStart;
        clust.indexStart_ = idxPos;
        clust.indexCount_ = srcClust.indexCount_;
        clust.valid = srcClust.valid;
        clust.meshIndex_ = curMesh_;
        clust.slot = curSlot_++; // this needs to be recomputed as we're retabling into local storage

        idxPos += srcClust.indexCount_;

        mesh->clusters.push_back(clust);
    }

    vertexBuff_->SetSubData(vertexData, curVtxOffset_ * sizeof(Vertex), vertexCt * sizeof(Vertex));
    indexBuff_->SetSubData(indexData, curIdxOffset_ * sizeof(uint16_t), indexCt * sizeof(uint16_t));

    curVtxOffset_ += vertexCt;
    curIdxOffset_ += indexCt;
    ++curMesh_;
    meshes_.push_back(mesh);
    return mesh;
}

//****************************************************************************
//
//  Function:   MeshClusterArray::GetVertexBuff
//
//  Return:     The contained vertex-buffer for all allocations.
//
//****************************************************************************
std::shared_ptr<Buffer> MeshClusterArray::GetVertexBuff() const
{
    return vertexBuff_;
}

//****************************************************************************
//
//  Function:   MeshClusterArray::GetIndexBuff
//
//  Return:     The contained index-buffer for all allocations.
//
//****************************************************************************
std::shared_ptr<Buffer> MeshClusterArray::GetIndexBuff() const
{
    return indexBuff_;
}

std::shared_ptr<Buffer> MeshClusterArray::GetMeshDataBuff() const
{
    struct Record {
        uint32_t vertexStart_;
        uint32_t unused_;
        uint32_t indexStart_;
        uint32_t indexCount_;
    };
    if (meshDataBuff_ == nullptr)
    {
        meshDataBuff_ = GetDevice()->CreateShaderStorageBuffer();
        meshDataBuff_->SetStride(sizeof(Record));
        uint32_t total = 0;
        for (auto& m : meshes_)
            total += m->clusters.size();

        meshDataBuff_->SetSize(sizeof(Record)* total);
        std::vector<Record> data;
        data.reserve(total);

        for (const auto& m : meshes_)
        {
            for (const auto& c : m->clusters)
                data.push_back({ c.vertexStart_, 0, c.indexStart_, c.indexCount_ });
        }
        meshDataBuff_->SetData<Record>(data);
    }
    return meshDataBuff_;
}

}
