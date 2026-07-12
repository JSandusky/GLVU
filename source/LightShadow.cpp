//****************************************************************************
//
//  File:       LightShadow.cpp
//  License:    MIT
//  Project:    GLVU
//
//****************************************************************************

#include "LightShadow.h"

#include "Renderables.h"

#include <atomic>

using namespace math;
using namespace std;

namespace GLVU
{

static double atlasSizeTable[] = {
    1.0 / 2,
    1.0 / 4,
    1.0 / 8,
    1.0 / 16,
    1.0 / 32,
    1.0 / 64,
    1.0 / 128
};

const math::float4 AtlasCellTable::InvalidCell(-1, -1, -1, -1);

//****************************************************************************
//
//  Function:   AtlasCellTable::AtlasCellTabe
//
//  Purpose:    Constructs and sets up for up to 64x64 units.
//
//****************************************************************************
AtlasCellTable::AtlasCellTable(int dim) :
    dim_(dim)
{
    levels_ = 0;
    while (dim > 64)
    {
        levels_ += 1;
        dim /= 2;
    }

    cellCount_.resize(levels_ + 1, 0);
    regions_.resize(levels_ + 1);
    Clear();

    dim = dim_;
    levelDims_.resize(levels_ + 1);
    for (int i = 0; i < levels_ + 1; ++i)
    {
        levelDims_[i] = dim;
        dim /= 2;
    }
}

//****************************************************************************
//
//  Function:   AtlasCelLTable::AtlasCellTable
//
//  Purpose:    Constructs, and then configures itself to support up to N-levels.
//              Care should be taken that given params don't result in a level
//              reaching 0x0 units as there are no safety checks.
//
//****************************************************************************
AtlasCellTable::AtlasCellTable(int levels, int dim) :
    dim_(dim),
    levels_(levels)
{
    cellCount_.resize(levels_ + 1, 0);
    regions_.resize(levels_ + 1);
    Clear();

    levelDims_.resize(levels_ + 1);
    for (int i = 0; i < levels_ + 1; ++i)
    {
        levelDims_[i] = dim;
        dim /= 2;
    }
}

//****************************************************************************
//
//  Function:   AtlasCellTable::GetCell
//
//  Purpose:    
//
//  Return:     The coordinates of the cell, or the sentinel InvalidCell value.
//
//****************************************************************************
math::float4 AtlasCellTable::GetCell(int level)
{
    if (level == -1)
        return { -1, -1, -1, -1 };
        
    if (cellCount_[level] > 0 || DivideCells(level - 1))
    {
        auto cell = regions_[level][cellCount_[level] - 1];
        cellCount_[level]--;
        return cell;
    }
    else
        return InvalidCell;
}

//****************************************************************************
//
//  Function:   AtlasCellTable::CalculateLevel
//
//  Purpose:    Calculates subdivision level based on the desired dimensions of the provided tile.
//
//  Return:     The level that is appropriate for a given NxN size.
//
//****************************************************************************
int AtlasCellTable::CalculateLevel(int forSize) const
{
    for (int i = 0; i < levels_ + 1; ++i)
        if (levelDims_[i] == forSize)
            return i;
    return -1;
}

//****************************************************************************
//
//  Function:   AtlasCellTable::Clear
//
//  Purpose:    Wipes all data and resets to a single full cell.
//
//****************************************************************************
void AtlasCellTable::Clear()
{
    cellCount_[0] = 1;
    regions_[0][0] = float4 { 0.0f, 0.0f, 1.0f, 1.0f };
    for (int i = 1; i < levels_ + 1; ++i)
        cellCount_[i] = 0;
}

//****************************************************************************
//
//  Function:   AtlasCellTable::ToViewport
//
//  Purpose:    Converts UV coordinates into meaningful { START, DIM } viewport
//              coordinates that other code uses.
//
//  Return:     UV mapped to viewport.
//
//****************************************************************************
math::uint4 AtlasCellTable::ToViewport(math::float4 value) const
{
    return uint4(value.x * dim_, value.y * dim_, value.Width() * dim_, value.Height() * dim_);
}

//****************************************************************************
//
//  Function:   AtlasCellTable::Divide
//
//  Purpose:    Slices the given rectangle and writes it into the output.
//
//              Why is this unused?
//
//****************************************************************************
void AtlasCellTable::Divide(float4 rect, float4* output)
{
    const float width = (rect.z - rect.x);
    const float height = (rect.w - rect.y);
    const float halfWidth = (rect.z - rect.x) / 2.0f;
    const float halfHeight = (rect.w - rect.y) / 2.0f;

    output[0] = float4 { rect.x, rect.y, rect.x + halfWidth, rect.y + halfHeight }; // 0, 0 -> 0.5, 0.5
    output[1] = float4 { rect.x + halfWidth, rect.y, rect.x + width, rect.y + halfHeight }; // 0.5, 0, -> 1.0, 0.5
    output[2] = float4 { rect.x, rect.y + halfHeight, rect.x + halfWidth, rect.y + height }; // 0, 0.5 -> 0.5, 1.0
    output[3] = float4 { rect.x + halfWidth, rect.y + halfHeight, rect.x + width, rect.y + height }; // 0.5, 0.05 -> 1, 1
}

//****************************************************************************
//
//  Function:   AtlasCellTable::SplitCells
//
//  Purpose:    Chops up the cells at a given level.
//
//****************************************************************************
void AtlasCellTable::SplitCells(int size)
{
    float4 rect = regions_[size][cellCount_[size] - 1];

    const float width = (rect.z - rect.x);
    const float height = (rect.w - rect.y);
    const float halfWidth =  (rect.z - rect.x) / 2.0f;
    const float halfHeight = (rect.w - rect.y) / 2.0f;

    cellCount_[size + 1] = 4;
    cellCount_[size]--;

    ///     |---------| 1,1
    //      |  C | D  |
    ///     |----|----|
    //      |  A | B  |
    /// 0,0 |---------|
    // allocation order is D, C, B, A
    regions_[size + 1][0] = float4 { rect.x, rect.y, rect.x + halfWidth, rect.y + halfHeight }; // 0, 0 -> 0.5, 0.5
    regions_[size + 1][1] = float4 { rect.x + halfWidth, rect.y, rect.x + width, rect.y + halfHeight }; // 0.5, 0, -> 1.0, 0.5
    regions_[size + 1][2] = float4 { rect.x, rect.y + halfHeight, rect.x + halfWidth, rect.y + height }; // 0, 0.5 -> 0.5, 1.0
    regions_[size + 1][3] = float4 { rect.x + halfWidth, rect.y + halfHeight, rect.x + width, rect.y + height }; // 0.5, 0.05 -> 1, 1
}

//****************************************************************************
//
//  Function:   AtlasCellTable::DivideCells
//
//  Purpose:    Tries to divide a given level into 4 cells, will divide
//              up the tree as necessary in an attempt to acquire cells.
//
//  Return:     True if we were able to, false if division failed.
//              If failed then there's no room.
//
//****************************************************************************
bool AtlasCellTable::DivideCells(int level)
{
    if (level < 0 || level > levels_)
        return false;

    if (cellCount_[level] > 0)
    {
        // do we have a cell on our level we can divide?
        SplitCells(level);
        return true;
    }
    else if (DivideCells(level - 1)) 
    {
        // try to go up a level and divide from there
        SplitCells(level);
        return true;
    }
    else
        return false;
}

//****************************************************************************
//
//  Function:   QuadTreeAllocator::QuadTreeAllocator
//
//  Purpose:    Constructs and initializes for a given set of dimensions.
//
//****************************************************************************
QuadTreeAllocator::QuadTreeAllocator(uint32_t width, uint32_t height) : 
    width_(width), 
    height_(height)
{
    root_ = new Cell();
    root_->coords_ = math::uint4::FromPosSize(0, 0, width, height);
    root_->Free();
}

//****************************************************************************
//
//  Function:   QuadTreeAllocator::~QuadTreeAllocator
//
//  Purpose:    Deletes the root cell, and thus the entire tree.
//
//****************************************************************************
QuadTreeAllocator::~QuadTreeAllocator()
{
    delete root_;
    root_ = nullptr;
}

//****************************************************************************
//
//  Function:   QuadTreeAllocator::GetCell
//
//  Purpose:    Grabs a cell from the quad-tree with the appropriate dimensions
//
//  Return:     Viewport coordinates or -1,-1,-1,-1 if unable to acquire a cell.
//
//****************************************************************************
math::uint4 QuadTreeAllocator::GetCell(Cell* cell, uint32_t dim, uintptr_t datum)
{
    // This cell the right size and not taken?
    // cell is bigger or equal, but half of it would be too small
    if (cell->Width() >= dim && (cell->Width()/2) < dim && !cell->taken_ && !cell->AnyTaken())
    {
        cell->taken_ = true;
        if (datum)
        {
            // if we have a datum then mark so we can try to preserve.
            auto& found = datumTable_.find(datum);
            if (found != datumTable_.end())
                found->second.first += 1;
            else
                datumTable_.insert(make_pair(datum, make_pair(1, cell)));
        }
        return cell->ToViewport();
    }

    // if our cell is larger than our size, then continue
    if (cell->Width() > dim)
    {
        // can we and do we need to divide?
        if (cell->children_ == nullptr && cell->Width() > 1)
            cell->Divide();
        if (cell->children_)
        {
            for (int i = 0; i < 4; ++i)
            {
                if (cell->children_[i].taken_)
                    continue;
                auto ret = GetCell(&cell->children_[i], dim, datum);
                if (ret.Width() > 0) // did we get something valid?
                    return ret;
            }
        }
    }

    // Return a sentinel for invaid cell, zero sized.
    return uint4(-1, -1, -1, -1);
}

//****************************************************************************
//
//  Function:   QuadTreeAllocator::GetCell
//
//  Purpose:    Base call for attempting to acquire a cell,
//              first checks the datum value for whether a cell has been
//              requested to be used as a sticky.
//
//****************************************************************************
math::uint4 QuadTreeAllocator::GetCell(uint32_t dim, uintptr_t datum)
{
    if (datum)
    {
        // if we have a datum then check it
        auto& found = datumTable_.find(datum);
        if (found != datumTable_.end())
        {
            // is our current cell still a good choice?
            auto foundWidth = found->second.second->Width();
            if (foundWidth >= dim && (foundWidth / 2) < dim)
            {
                found->second.first += 1;
                found->second.second->taken_ = true;
                return found->second.second->ToViewport();
            }

            // not the same size, free the cell we found.
            found->second.second->taken_ = false;
            datumTable_.erase(found);
        }
    }
    return GetCell(root_, dim, datum);
}

//****************************************************************************
//
//  Function:   QuadTreeAllocator::QuadTreeAllocator
//
//  Purpose:    Constructs and initializes for a given set of dimensions.
//
//****************************************************************************
void QuadTreeAllocator::ProcessUpdate()
{
    // Check and see if anything has expired.
    vector<uint32_t> toRemove;
    for (auto& rec : datumTable_)
    {
        // anyone that's at 0 should be cleaned since it wasn't marked with a +1 again
        if (rec.second.first <= 0)
            toRemove.push_back(rec.first);

        rec.second.first -= 1; // now mark everything down as having been through a cycle.
    }
    // remove everything we need to
    for (auto r : toRemove)
        datumTable_.erase(r);

    // Free everything indiscriminately (mark them taken again soon).
    root_->Free();

    // mark anyone left in the table as taken so that others can't take it.
    for (auto& rec : datumTable_)
        rec.second.second->taken_ = true;
}

//****************************************************************************
//
//  Function:   QuadTreeAllocator::ReturnCell
//
//  Purpose:    Returns the cell associated to a datum back to the allocator.
//
//****************************************************************************
void QuadTreeAllocator::ReturnCell(uintptr_t datum)
{
    auto& found = datumTable_.find(datum);
    if (found != datumTable_.end())
    {
        found->second.second->taken_ = false;
        datumTable_.erase(found);
    }
}

//****************************************************************************
//
//  Function:   QuadTreeAllocator::Cell::AnyTaken
//
//  Purpose:    Checks if this cell or any of its' children are marked as taken.
//
//  Return:     True, if taken.
//
//****************************************************************************
bool QuadTreeAllocator::Cell::AnyTaken() const
{
    if (children_)
    {
        if (children_[0].AnyTaken() ||
            children_[1].AnyTaken() ||
            children_[2].AnyTaken() || 
            children_[3].AnyTaken())
            return true;
    }
    return taken_;
}

//****************************************************************************
//
//  Function:   QuadTreeAllocator::CollectRects
//
//  Purpose:    Recurses through the tree collecting the rectangles of everything
//              that isn't claimed, this can be used to gather the minimum number
//              of clear rects for something like vkCmdClearAttachments.
//
//****************************************************************************
void QuadTreeAllocator::CollectRects(Cell* cell, vector<uint4>& holder)
{
    if (cell->AnyTaken())
    {
        if (cell->children_)
        {
            if (!cell->children_[0].taken_)
                CollectRects(&cell->children_[0], holder);
            if (!cell->children_[1].taken_)
                CollectRects(&cell->children_[1], holder);
            if (!cell->children_[2].taken_)
                CollectRects(&cell->children_[2], holder);
            if (!cell->children_[3].taken_)
                CollectRects(&cell->children_[3], holder);
        }
    }
    else
        holder.push_back(cell->ToViewport());
}

//****************************************************************************
//
//  Function:   QuadTreeAllocator::CollectRects
//
//  Purpose:    Public wrapper around the recursive rect collector.
//
//****************************************************************************
void QuadTreeAllocator::CollectRects(vector<uint4>& holder)
{
    CollectRects(root_, holder);
}

//****************************************************************************
//
//  Function:   QuadTreeAllocator::Clear
//
//  Purpose:    Resets state of the quad-tree allocator so that everything is clear.
//
//****************************************************************************
void QuadTreeAllocator::Clear()
{
    root_->Clear();
    datumTable_.clear();
}

//****************************************************************************
//
//  Function:   RenderTargetAtlas::RenderTargetAtlas
//
//  Purpose:    Construct and setup the textures + FBOs.
//
//****************************************************************************
RenderTargetAtlas::RenderTargetAtlas(GraphicsDevice* device, TextureFormat format, uint32_t dim, bool withDepth) :
    dim_(dim),
    atlasTable_(dim, dim)
{
    TextureTraits t = { };
    t.width_ = dim;
    t.height_ = dim;
    t.kind_ = Texture2D;
    t.format_ = format;
    t.colorAttachment_ = !IsDepth(format);
    t.depthAttachment_ = IsDepth(format);
    t.mips_ = 1;
    shadowAtlas_ = device->CreateTexture(t);

    if (withDepth)
    {
        t.format_ = TEX_DEPTH;
        t.colorAttachment_ = false;
        t.depthAttachment_ = true;
        auto depthTex = device->CreateTexture(t);
        shadowFBO_ = device->CreateFrameBuffer({ shadowAtlas_, depthTex });
    }
    else
        shadowFBO_ = device->CreateFrameBuffer({ shadowAtlas_ });
}

//****************************************************************************
//
//  Function:   RenderTargetAtlas::GetShadowRect
//
//  Purpose:    Acquires a UV cell that's dim-by-dim units.
//
//  Return:     The UV-coords of the cell, or InvalidCell sentinel value
//
//****************************************************************************
float4 RenderTargetAtlas::GetShadowRect(uint32_t dim)
{
    auto c = atlasTable_.GetCell(dim);
    return float4(c.x / (float)atlasTable_.width_, c.y / (float)atlasTable_.height_, c.z / (float)atlasTable_.width_, c.w / (float)atlasTable_.height_);
    //return atlasTable_.GetArea(dim);
}

//****************************************************************************
//
//  Function:   LightTiler::LightTiler
//
//  Purpose:    Constructs buffers for Tiled/Clustered lighting methods.
//              Actual scheme is arbitrary, and could be:
//              
//              XY: Forward-tiled
//              XZ: Just Cause 2
//              XYZ: clustered
//
//              Because the light recording is done on the CPU here the tests
//              are crude AABB tests that will cause a lot of false positives.
//
//****************************************************************************
LightTiler::LightTiler(GraphicsDevice* device, uint3 cells, uint32_t lightsPerCell) :
    lightsPerCell_(lightsPerCell),
    tileDim_(cells),
    device_(device)
{
    maxLights_ = (uint32_t)floorf(device->GetGPUFeatures().maxUBOSize_ / sizeof(LightData));

    cellsUBO_ = device->CreateShaderStorageBuffer();
    cellsUBO_->SetTag(BufferTag_Dynamic);
    cellsUBO_->SetStride(sizeof(uint4));
    cellsUBO_->SetSize(sizeof(uint4) * CellCount());
    
    lightsUBO_ = device->CreateShaderStorageBuffer();
    lightsUBO_->SetTag(BufferTag_Dynamic);
    lightsUBO_->SetStride(sizeof(LightData));
    lightsUBO_->SetSize(sizeof(LightData) * maxLights_);

    lightIndexesUBO_ = device->CreateShaderStorageBuffer();
    lightIndexesUBO_->SetTag(BufferTag_Dynamic);
    lightIndexesUBO_->SetStride(sizeof(unsigned));
    lightIndexesUBO_->SetSize(sizeof(unsigned) * lightsPerCell_ * CellCount());
}

//****************************************************************************
//
//  Function:   LightTiler::BuildLightTables
//
//  Purpose:    Constructs buffers for Tiled/Clustered lighting methods.
//              Actual scheme is arbitrary, and could be:
//              
//              XY: Forward-tiled
//              XZ: Just Cause 2
//              XYZ: clustered
//
//              Because the light recording is done on the CPU here the tests
//              are crude AABB tests that will cause a lot of false positives.
//
//****************************************************************************
uint32_t LightTiler::BuildLightTables(Camera* camera, const vector< shared_ptr<Light> >& lights)
{
    auto proj = camera->GetViewProjection();

    const float zStep = 1.0f / tileDim_.z;
    const float yStep = 1.0f / tileDim_.y;
    const float xStep = 1.0f / tileDim_.x;

    vector<uint4> lightCounts(CellCount(), { 0, 0, 0, 0 });
    vector<uint32_t> lightIndices(CellCount() * lightsPerCell_);
    vector<LightData> lightRawData;

    uint32_t hitCt = 0;

    for (auto& l : lights)
    {
        LightData d;
        d.lightMat = l->GetShadowMatrix(0);
        d.lightPos = float4(l->GetPosition(), l->GetKind());
        d.lightDir = float4(l->GetDirection(), l->GetRadius());
        d.color = l->GetColor();
        d.shadowMapCoords[0] = l->GetShadowDomain(0);
        d.shadowMapCoords[1] = l->GetShadowDomain(1);
        d.extraParams = float4(l->GetFOV(), l->IsShadowCasting() ? 1.0f : 0.0f, 0.0f, 0.0f);
        lightRawData.push_back(d);
    }

    auto frus = camera->GetFrustum();
    auto trimDim = uint3(tileDim_.x - 1, tileDim_.y - 1, tileDim_.z - 1);

    auto viewMat = frus.ViewProjMatrix();
    float viewRange = frus.FarPlaneDistance() - frus.NearPlaneDistance();

    for (unsigned lightIndex = 0; lightIndex < lights.size() && lightIndex < maxLights_; ++lightIndex)
    {
        auto& l = lights[lightIndex];
        auto aabb = l->GetBounds();
        if (!frus.IsInsideFast(aabb))
            continue;

        float4 pts[8];
        for (int i = 0; i < 8; ++i)
            pts[i] = viewMat.Mul(POINT_TO_FLOAT4(aabb.CornerPoint(i)));

        //int allBad = 0;
        for (int i = 0; i < 8; ++i)
        {
            //allBad += pts[i].w < 0 ? 1 : 0;
            auto invW = 1.0f / pts[i].w;
            pts[i].x *= invW;
            pts[i].y *= invW;
            //pts[i].z = (pts[i].z - frus.NearPlaneDistance()); / viewRange;
        }
        //if (allBad == 8)
        //    continue;

        //bool anyGood = false;
        //for (int i = 0; i < 8; ++i)
        //    if (pts[i].z > 0.0f && pts[i].z < 1.0f)
        //        anyGood = true;
        //
        //if (!anyGood)
        //    continue;

        auto minPt = pts[0];
        auto maxPt = pts[0];
        for (int i = 1; i < 8; ++i)
        {
            minPt = minPt.Min(pts[i]);
            maxPt = maxPt.Max(pts[i]);
        }

        int zStartIndex = toSliceZ(minPt.z, frus.NearPlaneDistance(), frus.FarPlaneDistance());
        int zEndIndex = toSliceZ(maxPt.z, frus.NearPlaneDistance(), frus.FarPlaneDistance());
        //int zStartIndex = (int)floorf((minPt.z*tileDim_.z));
        //int zEndIndex = (int)ceilf(  (maxPt.z*tileDim_.z));

        int yStartIndex = (int)floorf((minPt.y*0.5f+0.5f) * (float)tileDim_.y);
        int yEndIndex = (int)ceilf(   (maxPt.y*0.5f+0.5f) * (float)tileDim_.y);
            
        int xStartIndex = (int)floorf((minPt.x*0.5f+0.5f) * (float)tileDim_.x);
        int xEndIndex = (int)ceilf(   (maxPt.x*0.5f+0.5f) * (float)tileDim_.x);

        // Now cull the light, casts aren't optional
        if ((zStartIndex < 0 && zEndIndex < 0) || (zStartIndex >= (int)tileDim_.z && zEndIndex >= (int)tileDim_.z))
            continue;

        if ((yStartIndex < 0 && yEndIndex < 0) || (yStartIndex >= (int)tileDim_.y && yEndIndex >= (int)tileDim_.y))
            continue; 

        if ((xStartIndex < 0 && xEndIndex < 0) || (xStartIndex >= (int)tileDim_.x && xEndIndex >= (int)tileDim_.x))
            continue;

        zStartIndex = math::Clamp<int>(zStartIndex, 0, tileDim_.z - 1);
        zEndIndex = Clamp<int>(zEndIndex, 0, tileDim_.z - 1);

        yStartIndex = Clamp<int>(yStartIndex, 0, tileDim_.y - 1);
        yEndIndex = Clamp<int>(yEndIndex, 0, tileDim_.y - 1);

        xStartIndex = Clamp<int>(xStartIndex, 0, tileDim_.x - 1);
        xEndIndex = Clamp<int>(xEndIndex, 0, tileDim_.x - 1);

        for (auto z = zStartIndex; z <= zEndIndex; ++z)
        {
            for (auto y = yStartIndex; y <= yEndIndex; ++y)
            {
                for (auto x = xStartIndex; x <= xEndIndex; ++x)
                {
                    auto clusterID = x + y * tileDim_.x + z * tileDim_.x * tileDim_.y;
                    lightIndices[clusterID * lightsPerCell_ + lightCounts[clusterID].x % lightsPerCell_] = lightIndex;
                    lightCounts[clusterID].x += 1;
                    ++hitCt;
                }
            }
        }
    }

    cellsUBO_->SetData(lightCounts.data(), sizeof(uint4) * lightCounts.size());
    lightsUBO_->SetData(lightRawData.data(), sizeof(LightData) * lightRawData.size());
    lightIndexesUBO_->SetData(lightIndices.data(), sizeof(uint32_t) * lightIndices.size());

    return hitCt;
}

AABB LightTiler::ComputeFroxelBounds(Camera* camera, float x, float y, float z) const
{
    auto frus = camera->GetFrustum();
    frus.SetKind(FrustumSpaceD3D, FrustumLeftHanded);

    const float froxelWidthInClipSpace = 2.0f / tileDim_.x;//(2.0f * tileDim_.x) / camera->GetViewportWidth();
    const float froxelHeightInClipSpace = 2.0f / tileDim_.y;//(2.0f * tileDim_.y) / camera->GetViewportHeight();
    const float froxelDepth = 1.0f / tileDim_.z;

    float3 steps = float3(1,1,1) / float3(tileDim_.x, tileDim_.y, tileDim_.z);

    float xFracStart = x*froxelWidthInClipSpace - 1;
    float xFracEnd = (x+1)*froxelWidthInClipSpace - 1;

    float yFracStart = y*froxelHeightInClipSpace - 1;
    float yFracEnd = (y+1) * froxelHeightInClipSpace - 1;

    float zFracStart = z * froxelDepth;
    float zFracEnd = (z + 1) * froxelDepth;

    AABB bnds;
    bnds.SetNegativeInfinity();

    bnds.Enclose(frus.PointInside(xFracStart, yFracStart, zFracStart));
    bnds.Enclose(frus.PointInside(xFracStart, yFracStart, zFracEnd));
    bnds.Enclose(frus.PointInside(xFracStart, yFracEnd, zFracStart));
    bnds.Enclose(frus.PointInside(xFracStart, yFracEnd, zFracEnd));

    bnds.Enclose(frus.PointInside(xFracEnd, yFracStart, zFracStart));
    bnds.Enclose(frus.PointInside(xFracEnd, yFracStart, zFracEnd));
    bnds.Enclose(frus.PointInside(xFracEnd, yFracEnd, zFracStart));
    bnds.Enclose(frus.PointInside(xFracEnd, yFracEnd, zFracEnd));

    return bnds;
}

uint32_t LightTiler::BuildLightTablesBounds(Camera* camera, const std::vector< std::shared_ptr<Light> >& lights)
{
    AABB* froxels = new AABB[CellCount()];
    for (int z = 0; z < tileDim_.z; ++z)
        for (int y = 0; y < tileDim_.y; ++y)
            for (int x = 0; x < tileDim_.x; ++x)
                froxels[toIndex(x, y, z)] = ComputeFroxelBounds(camera, x, y, z);

    uint32_t hitCt = 0;
    for (uint32_t lightIndex = 0; lightIndex < lights.size(); ++lightIndex)
    {
        auto light = lights[lightIndex];
        auto lightBnds = Sphere(light->GetPosition(), light->GetRadius());// light->GetBounds();
        for (int z = 0; z < tileDim_.z; ++z)
            for (int y = 0; y < tileDim_.y; ++y)
                for (int x = 0; x < tileDim_.x; ++x)
                {
                    auto& froxel = froxels[toIndex(x, y, z)];
                    if (froxel.Intersects(lightBnds))
                        ++hitCt;
                }
    }

    delete[] froxels;
    return hitCt;
}

}