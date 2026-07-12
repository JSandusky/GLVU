//****************************************************************************
//
//  File:       Geometry.cpp
//  License:    MIT
//  Project:    GLVU
//
//****************************************************************************

#include "Geometry.h"

#include "GraphicsDevice.h"

using namespace std;
using namespace math;

namespace GLVU
{

//****************************************************************************
//
//  Function:   GeometryLayout::GeometryLayout
//
//  Purpose:    Neutral constructor.
//
//****************************************************************************
GeometryLayout::GeometryLayout(GraphicsDevice* device) :
    GPUObject(device),
#if defined(GLVU_VK)
    vertexObject_(0),
#endif
    vertexDataCount_(0)
{
}

//****************************************************************************
//
//  Function:   GeometryLayout::~GeometryLayout
//
//  Purpose:    Neutral destructor.
//
//****************************************************************************
GeometryLayout::~GeometryLayout()
{
    Release();
}

//****************************************************************************
//
//  Function:   GeometryLayout::AddVertexInfo
//
//  Purpose:    Uses the current tracking counter to push via copy a new vertex-info
//              to the back of the fixed-capacity list.
//
//****************************************************************************
void GeometryLayout::AddVertexInfo(const VertexInfo& info)
{
    assert(vertexDataCount_ < 16);
    vertexData_[vertexDataCount_++] = info;
}

//****************************************************************************
//
//  Function:   GeometryLayout::AttachInstanceInfo
//
//  Purpose:    Pushes standard vertex attributes for reading instance transforms
//              from an additional vertex stream. The most important bit is
//              identifying the index for the vertex-stream. The rest is trite.
//
//****************************************************************************
void GeometryLayout::AttachInstanceInfo(bool forVR)
{
    uint16_t maxBuffer = 0;
    for (uint32_t i = 0; i < vertexDataCount_; ++i)
        maxBuffer = std::max(maxBuffer, vertexData_[i].bufferSlot_);

    instanceBufferIndex_ = maxBuffer + 1;

    uint8_t rate = forVR ? (uint8_t)2 : (uint8_t)1;
    AddVertexInfo({
        VA_INSTANCE,
        VDT_FLOAT,
        0,
        4,
        (uint16_t)instanceBufferIndex_,
        sizeof(float4x4),
        false,
        rate
        });

    AddVertexInfo({
        VA_INSTANCE,
        VDT_FLOAT,
        sizeof(float) * 4,
        4,
        (uint16_t)instanceBufferIndex_,
        sizeof(float4x4),
        false,
        rate
        });

    AddVertexInfo({
        VA_INSTANCE,
        VDT_FLOAT,
        sizeof(float) * 8,
        4,
        (uint16_t)instanceBufferIndex_,
        sizeof(float4x4),
        false,
        rate
        });

    AddVertexInfo({
        VA_INSTANCE,
        VDT_FLOAT,
        sizeof(float) * 12,
        4,
        (uint16_t)instanceBufferIndex_,
        sizeof(float4x4),
        false,
        rate
        });
}

//****************************************************************************
//
//  Function:   GeometryLayout::Clone
//
//  Purpose:    Deep copy of this object.
//
//  Return:     A new GeometryLayout instance whose properties are a match to this one.
//
//****************************************************************************
shared_ptr<GeometryLayout> GeometryLayout::Clone()
{
    shared_ptr<GeometryLayout> ret = device_->CreateGeometryLayout();
    for (int i = 0; i < vertexDataCount_; ++i)
        ret->AddVertexInfo(vertexData_[i]);
    ret->instanceBufferIndex_ = instanceBufferIndex_;
    return ret;
}

//****************************************************************************
//
//  Function:   GeometryLayout::GetInstancedVariant
//
//  Purpose:    Both a deep copy and instance data attachment.
//
//  Return:     A new GeometryLayout instance that matches this, but
//              with the addition of instancing data.
//
//****************************************************************************
shared_ptr<GeometryLayout> GeometryLayout::GetInstancedVariant(bool forVR)
{
    if (forVR && vrInstancedVariant_)
        return vrInstancedVariant_;

    if (instancedVariant_)
        return instancedVariant_;

    instancedVariant_ = Clone();
    instancedVariant_->AttachInstanceInfo(false);

    vrInstancedVariant_ = Clone();
    vrInstancedVariant_->AttachInstanceInfo(true);

    return forVR ? vrInstancedVariant_ : instancedVariant_;
}

//****************************************************************************
//
//  Function:   Geometry::InferValuesFromData
//
//  Purpose:    Calculates vertex, primitive, and index counts based on the
//              other provided data in this geometry object.
//
//****************************************************************************
void Geometry::InferValuesFromData()
{
    assert(layout_);

    indexStart_ = 0;

    auto& rec = layout_->vertexData_[0];
    vertexCount_ = vertexBuffers_[rec.bufferSlot_]->GetSize() / rec.stride_;

    if (indexBuffer_)
    {
        indexStart_ = 0;
        if (indexBuffer_->HasTag(BufferTag_32Bit))
            indexCount_ = indexBuffer_->GetSize() / sizeof(uint32_t);
        else
            indexCount_ = indexBuffer_->GetSize() / sizeof(uint16_t);
    }

    switch (primType_)
    {
    case TRIANGLE_LIST:
        primCount_ = indexBuffer_ ? indexCount_ / 3 : vertexCount_ / 3;
        break;
    case POINT_LIST:
        primCount_ = indexBuffer_ ? indexCount_ : vertexCount_;
        break;
    case LINE_LIST:
        primCount_ = indexBuffer_ ? indexCount_ / 2 : vertexCount_ / 2;
        break;
    }
}

//****************************************************************************
//
//  Function:   Geometry::Clone
//
//  Purpose:    Deep copy of this object.
//
//  Return:     a matched shared_ptr of this.
//
//****************************************************************************
void Geometry::Release()
{
    primCount_ = indexCount_ = vertexCount_ = indexStart_ = 0;
    layout_.reset();
    vertexBuffers_.clear();
    indexBuffer_.reset();
    shadowGeometry_.reset();
}

//****************************************************************************
//
//  Function:   Geometry::Clone
//
//  Purpose:    Deep copy of this object.
//
//  Return:     a matched shared_ptr of this.
//
//****************************************************************************
shared_ptr<Geometry> Geometry::Clone()
{
    shared_ptr<Geometry> geo(new Geometry());
    geo->layout_ = layout_;
    geo->vertexBuffers_ = vertexBuffers_;
    geo->indexBuffer_ = indexBuffer_;
    if (shadowGeometry_)
        geo->shadowGeometry_ = shadowGeometry_->Clone();

    geo->indexStart_ = indexStart_;
    geo->primCount_ = primCount_;
    geo->vertexCount_ = vertexCount_;
    geo->indexCount_ = indexCount_;
    geo->primType_ = primType_;

    return geo;
}

//****************************************************************************
//
//  Function:   Geometry::Create
//
//  Purpose:    Helper for taking the provided vtx/idx buffers, vertex layout, and primitive
//              to produce a fully specified geometry object.
//              Counts, our inferred from the given data.
//
//              Cannot be used when attempting to share vertex buffers.
//
//  Return:     A newly created geometry object.
//
//****************************************************************************
shared_ptr<Geometry> Geometry::Create(PrimitiveType prim, shared_ptr<GeometryLayout> layout, shared_ptr<Buffer> vtx, shared_ptr<Buffer> idx)
{
    shared_ptr<Geometry> geo(new Geometry());
    geo->primType_ = prim;
    geo->layout_ = layout;
    geo->vertexBuffers_.push_back(vtx);
    geo->indexBuffer_ = idx;
    geo->indexStart_ = 0;
    geo->InferValuesFromData();
    return geo;
}

//****************************************************************************
//
//  Function:   Geometry::Create
//
//  Purpose:    Helper for taking the provided vtx/idx buffers, vertex layout, and primitive
//              to produce a fully specified geometry object.
//              Counts, our inferred from the given data.
//
//              Cannot be used when attempting to share vertex buffers.
//
//  Return:     A newly created geometry object.
//
//****************************************************************************
shared_ptr<Geometry> Geometry::Create(PrimitiveType prim, shared_ptr<GeometryLayout> layout, const vector<shared_ptr<Buffer> >& vtx, shared_ptr<Buffer> idx)
{
    shared_ptr<Geometry> geo(new Geometry());
    geo->primType_ = prim;
    geo->layout_ = layout;
    geo->vertexBuffers_ = vtx;
    geo->indexBuffer_ = idx;
    geo->indexStart_ = 0;
    geo->InferValuesFromData();
    return geo;
}

std::shared_ptr<Geometry> Geometry::CreateForMergeInstancing(unsigned vertexCount)
{
    shared_ptr<Geometry> geo(new Geometry());
    geo->primType_ = TRIANGLE_LIST;
    geo->indexStart_ = geo->indexCount_ = 0;
    geo->vertexCount_ = vertexCount;
    return geo;
}

bool CalculateNormals(Geometry& geometry)
{
    assert(!geometry.vertexBuffers_.empty());
    if (geometry.vertexBuffers_.empty())
        return false;

    assert(geometry.vertexBuffers_[0]->IsShadowed());
    if (!geometry.vertexBuffers_[0]->IsShadowed())
        return false;

    auto info = geometry.layout_;
   
    assert(info->HasAttribute(VA_NORMAL));
    if (!info->HasAttribute(VA_NORMAL))
        return false;
    
    size_t positionOffset = info->Info(VA_POSITION)->offset_;
    size_t normalOffset = info->Info(VA_NORMAL)->offset_;
    unsigned char* vertexData = geometry.vertexBuffers_[0]->GetShadowData();
    unsigned char* indexData = geometry.indexBuffer_->GetShadowData();
    
    const bool largeIndices = geometry.indexBuffer_->HasTag(BufferTag_32Bit);
    const size_t idxSize = largeIndices ? sizeof(uint32_t) : sizeof(uint16_t);

    // Accumulate face normals into slots for each vertex

#define CAST(TYPE, OFFSET) ((TYPE*)(vertexData + OFFSET))
    std::vector<float3> norms;
    norms.resize(geometry.vertexCount_);

    unsigned* fatIdxData = (unsigned*)indexData;
    unsigned short* slimIdxData = (unsigned short*)indexData;
    for (uint32_t i = 0; i < geometry.indexCount_; i += 3)
    {
        const uint32_t idx[] = {
            largeIndices ? fatIdxData[i]   : slimIdxData[i],
            largeIndices ? fatIdxData[i+1] : slimIdxData[i+1],
            largeIndices ? fatIdxData[i+2] : slimIdxData[i+2]
        };

        float3 a = *CAST(float3, positionOffset * idx[0]);
        float3 b = *CAST(float3, positionOffset * idx[1]);
        float3 c = *CAST(float3, positionOffset * idx[1]);

        auto ba = (b - a).Normalized();
        auto ca = (c - a).Normalized();
        auto norm = (ba.Cross(ca).Normalized());
        norms[idx[0]] += norm;
        norms[idx[1]] += norm;
        norms[idx[2]] += norm;
    }

    for (auto& n : norms)
        n.Normalize();
    
    /// move it into our data
    for (uint32_t i = 0; i < geometry.indexCount_; ++i)
    {
        const uint32_t idx = largeIndices ? fatIdxData[i] : slimIdxData[i];
        *CAST(float3, normalOffset * idx) = norms[idx];
    }

    geometry.vertexBuffers_[0]->ApplyShadowData(true);

#undef CAST
    return true;
}

bool CaclulateTangents(Geometry& geometry)
{
    assert(!geometry.vertexBuffers_.empty());
    if (geometry.vertexBuffers_.empty())
        return false;

    assert(geometry.vertexBuffers_[0]->IsShadowed());
    if (!geometry.vertexBuffers_[0]->IsShadowed())
        return false;

    auto info = geometry.layout_;

    assert(info->HasAttribute(VA_NORMAL) && info->HasAttribute(VA_TANGENT) && info->HasAttribute(VA_TEXCOORD0));
    if (!info->HasAttribute(VA_NORMAL) || !info->HasAttribute(VA_TANGENT) || !info->HasAttribute(VA_TEXCOORD0))
        return false;

    size_t positionOffset = info->Info(VA_POSITION)->offset_;
    size_t normalOffset = info->Info(VA_NORMAL)->offset_;
    size_t tanOffset = info->Info(VA_TANGENT)->offset_;
    size_t texOffset = info->Info(VA_TEXCOORD0)->offset_;

    unsigned char* vertexData = geometry.vertexBuffers_[0]->GetShadowData();
    unsigned char* indexData = geometry.indexBuffer_->GetShadowData();

    const bool largeIndices = geometry.indexBuffer_->HasTag(BufferTag_32Bit);
    const size_t idxSize = largeIndices ? sizeof(uint32_t) : sizeof(uint16_t);



    return true;
}
bool CalculateGSAdjacency(unsigned char* vertexData, unsigned char* indexData, const GeometryLayout& info)
{
    return true;
}

Quat CalculateBillboard(BillboardType mode, const float3& position, const Quat& rotation, const float4x4& viewMatrix)
{
    switch (mode)
    {

    // These can be calculated once for everything
    case Billboard_Rotate:
        return viewMatrix.RotatePart().ToQuat();
    case Billboard_RotateY: {
        float3 euler = rotation.ToEulerXYZ();
        euler.y_ = viewMatrix.RotatePart().ToEulerXYZ().y;
        Quat q; q.FromEulerXYZ(euler.x, euler.y, euler.z);
        return q;
    }

    // These must be calculated per particle
    case Billboard_LookAtY: {
        float3 euler = rotation.ToEulerXYZ();
        Quat lookAt;
        lookAt.LookAt(float3::unitZ, (position - viewMatrix.TranslatePart()).Normalized(), float3::unitY, float3::unitY);
        euler.y = lookAt.ToEulerXYZ().y;
        lookAt.FromEulerXYZ(euler.x, euler.y, euler.z);
        return lookAt;
    }
    case Billboard_LookAt: {
        Quat lookAt;
        lookAt.LookAt(float3::unitZ, (position - viewMatrix.TranslatePart()).Normalized(), float3::unitY, float3::unitY);
        return lookAt;
    }
    }

    return rotation;
}

void CalculateTrail(const math::float3& viewUp, const math::float3& viewDir, const std::vector<math::float3>& positions)
{
    // failure
    if (positions.size() < 2)
        return;

    std::vector<math::float3> segmentVectors;
    for (size_t i = 0; i < positions.size() - 1; ++i)
        segmentVectors.push_back(positions[i + 1] - positions[i]);

    for (size_t i = 0; i < segmentVectors.size(); ++i)
    {

    }
}

}
