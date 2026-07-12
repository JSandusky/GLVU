#pragma once

#include <Blob.h>
#include <Renderables.h>

namespace GLVU
{

class HeightField : public SceneObject
{
public:
    HeightField();
    virtual ~HeightField();

    /// Compute interpolated height at the given point, bilinear filter
    float GetHeight(float x, float y) const;
    /// Returned height is correct to the underlying trianglulation.
    float GetGeometricHeight(float x, float y) const;

private:
    std::shared_ptr< BlockMap<float> > heightMap_;
    /// sampling domain.
    math::uint4 heightMapCell_;
    std::shared_ptr<Buffer> vertexBuffer_;
    std::shared_ptr<Buffer> indexBuffer_;
    math::AABB bounds_;
};

}