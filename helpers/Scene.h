#pragma once

#include <Renderables.h>

namespace GLVU
{

struct Octant
{
    Octant(unsigned level, unsigned maxLevel, math::AABB bounds);

    bool Insert(std::shared_ptr<SceneObject> object);
    void Remove(std::shared_ptr<SceneObject>);
    void RemoveDeep(std::shared_ptr<SceneObject>);

    bool Insert(std::shared_ptr<Light> object);
    void Remove(std::shared_ptr<Light>);
    void DrawDebug(DebugGeometry* geo);

protected:
    friend class OctreeScene;

    bool Insert(std::shared_ptr<SceneObject> object, const math::AABB& bounds);
    bool Insert(std::shared_ptr<Light> object, const math::AABB& bounds);
    Octant* GetOrCreateChild(unsigned index);
    bool CheckFit(const math::AABB& bounds) const;

    Octant* children_[8];
    std::vector< std::shared_ptr<SceneObject> > objects_;
    std::vector< std::shared_ptr<Light> > lights_;
    math::AABB bounds_, cullingBounds_;
    unsigned level_;
    unsigned maxLevel_;
};

class OctreeScene : public IQueriableScene, public Octant
{
public:
    OctreeScene(unsigned maxSubdivision, math::AABB bounds);

    virtual std::vector<Batch> GetBatches(const math::Frustum& frustum) override;
    virtual std::vector<std::shared_ptr<Light>> GetLights(const math::Frustum& frustum) override;
    virtual std::vector<Batch> GetBatches(const Light* forLight, unsigned shadowMapIndex, AABB& totalBnds) override;

protected:
    void GetBatches(std::vector<Batch>&, Octant* oct, const math::Frustum& frustum);
    void GetBatches(std::vector<Batch>&, Octant* oct, const Light* forLight, unsigned index, AABB& totalBnds);
    void GetLights(std::vector< std::shared_ptr<Light> >&, Octant* oct, const math::Frustum& frustum);
};

}