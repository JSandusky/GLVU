#include "Scene.h"

#pragma optimize("", off)

namespace GLVU
{

Octant::Octant(unsigned level, unsigned maxLevel, math::AABB bounds) :
    maxLevel_(maxLevel)
{
    bounds_ = bounds;
    cullingBounds_ = bounds;
    cullingBounds_.minPoint -= bounds_.HalfSize();
    cullingBounds_.maxPoint += bounds_.HalfSize();
    level_ = level;
    for (int i = 0; i < 8; ++i)
        children_[i] = nullptr;
}

bool Octant::Insert(std::shared_ptr<SceneObject> object)
{
    auto bb = object->GetBounds();
    Insert(object, bb);
    //if (!Insert(object, bb))
    //    objects_.push_back(object);
    return true;
}

static int totalPushes = 0;
bool Octant::Insert(std::shared_ptr<SceneObject> object, const math::AABB& box)
{
    bool insertHere = false;
    if (level_ == maxLevel_)
    {
        insertHere = !bounds_.Contains(box);// || CheckFit(box);
    }
    else
        insertHere = CheckFit(box);

    if (insertHere)
    {
        //Octant* oldOctant = drawable->octant_;
        //if (oldOctant != this)
        //{
        //    // Add first, then remove, because drawable count going to zero deletes the octree branch in question
        //    AddDrawable(drawable);
        //    if (oldOctant)
        //        oldOctant->RemoveDrawable(drawable, false);
        //}
        objects_.push_back(object);
        return true;
    }
    else
    {
        auto boxCenter = box.Centroid();
        auto selfCenter = bounds_.minPoint + bounds_.HalfSize();
        unsigned x = boxCenter.x < selfCenter.x ? 0 : 1;
        unsigned y = boxCenter.y < selfCenter.y ? 0 : 2;
        unsigned z = boxCenter.z < selfCenter.z ? 0 : 4;

        GetOrCreateChild(x + y + z)->Insert(object);
        return true;
    }

    //auto center = bounds_.Centroid();
    //
    //if (level_ == 0 || CheckFit(box))
    //{
    //    ++totalPushes;
    //    objects_.push_back(object);
    //    return true;
    //}
    //if (level_ > 0)
    //{
    //    auto boxCenter = box.Centroid();
    //    unsigned x = boxCenter.x_ < center.x ? 0 : 1;
    //    unsigned y = boxCenter.y_ < center.y ? 0 : 2;
    //    unsigned z = boxCenter.z_ < center.z ? 0 : 4;
    //
    //    if (GetOrCreateChild(x + y + z)->Insert(object, box))
    //        return true;
    //}
    //return false;
}

void Octant::Remove(std::shared_ptr<SceneObject> o)
{
    auto found = std::find(objects_.begin(), objects_.end(), o);
    if (found != objects_.end())
        objects_.erase(found);

    auto foundL = std::find(lights_.begin(), lights_.end(), o);
    if (foundL != lights_.end())
        lights_.erase(foundL);
}

void Octant::RemoveDeep(std::shared_ptr<SceneObject> o)
{
    Remove(o);
    for (int i = 0; i < 8; ++i)
        if (children_[i])
            children_[i]->RemoveDeep(o);
}

bool Octant::Insert(std::shared_ptr<Light> object)
{
    // directional light always goes into the root.
    if (object->GetKind() == Light::DIRECTIONAL)
    {
        lights_.push_back(object);
        return true;
    }
    auto bb = object->GetBounds();
    if (!Insert(object, bb))
        lights_.push_back(object);
    return true;
}

bool Octant::Insert(std::shared_ptr<Light> object, const math::AABB& bounds)
{
    //if (bounds_.Contains(bounds))
    {
        if (level_ == 0 || CheckFit(bounds))
        {
            lights_.push_back(object);
            return true;
        }
        if (level_ > 0)
        {
            auto center = bounds_.minPoint + bounds_.HalfSize();

            auto boxCenter = bounds.Centroid();
            unsigned x = boxCenter.x_ < center.x ? 0 : 1;
            unsigned y = boxCenter.y_ < center.y ? 0 : 2;
            unsigned z = boxCenter.z_ < center.z ? 0 : 4;
            
            if (GetOrCreateChild(x + y + z)->Insert(object, bounds))
                return true;
        }
    }
    return false;
}

void Octant::Remove(std::shared_ptr<Light> l)
{
    auto found = std::find(lights_.begin(), lights_.end(), l);
    if (found != lights_.end())
        lights_.erase(found);
}

void Octant::DrawDebug(DebugGeometry* geo)
{
    for (int i = 0; i < bounds_.NumEdges(); ++i)
    {
        auto e = bounds_.Edge(i);
        geo->AddLine(e.a, e.b, float4(1, 1, 1, 1), false);
    }
    for (int i = 0; i < 8; ++i)
        if (children_[i])
            children_[i]->DrawDebug(geo);
}


bool Octant::CheckFit(const AABB& box) const
{
    auto boxSize = box.Size();
    auto halfSize = bounds_.HalfSize();

    if (level_ == 0 || 
        boxSize.x_ >= halfSize.x_ || 
        boxSize.y_ >= halfSize.y_ || 
        boxSize.z_ >= halfSize.z_)
        return true;
    else
    {
        if (box.minPoint.x <= bounds_.minPoint.x - 0.5f * halfSize.x ||
            box.maxPoint.x >= bounds_.maxPoint.x + 0.5f * halfSize.x ||
            box.minPoint.y <= bounds_.minPoint.y - 0.5f * halfSize.y ||
            box.maxPoint.y >= bounds_.maxPoint.y + 0.5f * halfSize.y ||
            box.minPoint.z <= bounds_.minPoint.z - 0.5f * halfSize.z ||
            box.maxPoint.z >= bounds_.maxPoint.z + 0.5f * halfSize.z)
            return true;
    }

    return false;
}

Octant* Octant::GetOrCreateChild(unsigned index)
{
    if (children_[index])
        return children_[index];
    
    float3 newMin = bounds_.minPoint;
    float3 newMax = bounds_.maxPoint;
    float3 oldCenter = bounds_.Centroid();

    if (index & 1)
        newMin.x_ = oldCenter.x_;
    else
        newMax.x_ = oldCenter.x_;

    if (index & 2)
        newMin.y_ = oldCenter.y_;
    else
        newMax.y_ = oldCenter.y_;

    if (index & 4)
        newMin.z_ = oldCenter.z_;
    else
        newMax.z_ = oldCenter.z_;

    children_[index] = new Octant(level_ - 1, maxLevel_, AABB(newMin, newMax));
    return children_[index];
}

OctreeScene::OctreeScene(unsigned maxSubdivision, math::AABB bounds) : Octant(maxSubdivision, maxSubdivision, bounds)
{
    
}

std::vector<Batch> OctreeScene::GetBatches(const math::Frustum& frustum)
{
    std::vector<Batch> batches;
    batches.reserve(10000);
    GetBatches(batches, this, frustum);
    return batches;
}

std::vector<std::shared_ptr<Light>> OctreeScene::GetLights(const math::Frustum& frustum)
{
    std::vector< std::shared_ptr<Light> > lights;
    GetLights(lights, this, frustum);
    return lights;
}

std::vector<Batch> OctreeScene::GetBatches(const Light* forLight, unsigned shadowMapIndex, AABB& totalBnds)
{
    std::vector<Batch> batches;
    batches.reserve(10000);
    GetBatches(batches, this, forLight, shadowMapIndex, totalBnds);
    return batches;
}

void OctreeScene::GetBatches(std::vector<Batch>& batches, Octant* oct, const math::Frustum& frustum)
{
    //if (frustum.Intersects(oct->bounds_))
    //if (frustum.IsInsideFast(oct->bounds_))
    if (oct->bounds_.Contains(frustum.Pos()) || frustum.Intersects(oct->cullingBounds_))
    {
        for (size_t i = 0; i < oct->objects_.size(); ++i)
        {
            //if (frustum.Intersects(oct->objects_[i]->GetBounds()))
            if (frustum.IsInsideFast(oct->objects_[i]->GetBounds()))
                oct->objects_[i]->GetBatches(batches);
        }
        for (int i = 0; i < 8; ++i)
        {
            if (oct->children_[i])
                GetBatches(batches, oct->children_[i], frustum);
        }
    }
}

void OctreeScene::GetBatches(std::vector<Batch>& batches, Octant* oct, const Light* forLight, unsigned index, AABB& totalBnds)
{
    if (forLight->Contains(oct->cullingBounds_))
    {
        for (size_t i = 0; i < oct->objects_.size(); ++i)
        {
            if (forLight->Contains(oct->objects_[i]->GetBounds()))
            {
                //if (forLight->GetKind() == Light::POINT)
                //{
                //    auto lp = math::Plane(forLight->GetPosition(), float3::unitZ);
                //    float sdf = lp.SignedDistance(oct->objects_[i]->GetPosition());
                //    if ((sdf < 0 && index == 0) || (sdf > 0 && index == 1))
                //        continue;
                //}
                oct->objects_[i]->GetBatches(batches);
                totalBnds.Enclose(oct->bounds_);
            }
        }
        for (int i = 0; i < 8; ++i)
        {
            if (oct->children_[i])
                GetBatches(batches, oct->children_[i], forLight, index, totalBnds);
        }
    }
}

void OctreeScene::GetLights(std::vector< std::shared_ptr<Light> >& lights, Octant* oct, const math::Frustum& frustum)
{
    if (frustum.IsInsideFast(oct->bounds_))
    {
        for (size_t i = 0; i < oct->lights_.size(); ++i)
        {
            if (frustum.IsInsideFast(oct->lights_[i]->GetBounds()))
                lights.push_back(oct->lights_[i]);
        }

        if (oct->level_ > 0)
        {
            for (int i = 0; i < 8; ++i)
            {
                if (oct->children_[i])
                    GetLights(lights, oct->children_[i], frustum);
            }
        }
    }
}

}