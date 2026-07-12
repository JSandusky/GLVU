#pragma once

#include <Renderables.h>

namespace GLVU
{

class AnimatedModel : public SceneObject
{
public:
private:
};

/// Convert a 3x3 binorm, tangent, normal matrix (by row) into a Q-Tangent
Quat FrameToQTangent(float3x3 frame);

/// QTangents greatly compress the buffer data, even better if everything becomes half-precision.
void CalculateQTangents(std::vector<math::Quat>&, const std::vector<math::float3>& normals, const std::vector<math::float3>& tangents, const std::vector<math::float3>& bitangents);

/// QTangents from handed tangent vector/
void CalculateQTangents(std::vector<math::Quat>&, const std::vector<math::float3>& normals, const std::vector<math::float4>& handedTangents);

/// Blob indexed version of QTangent calculation.
void CalculateQTangents(std::vector<math::Quat>&, void* data, uint32_t vertexCt, uint32_t offsetNor, uint32_t offsetTan, uint32_t offsetBitan, uint32_t stride);

template<typename T>
void ExtractBufferElements(std::vector<T>& output, void* data, uint32_t vertexCt, uint32_t elemOffset, uint32_t stride)
{
    if (data == nullptr) return;

    unsigned char* d = (unsigned char*)data;
    for (uint32_t i = 0; i < vertexCount; ++i)
    {
        output.push_back(*((T*)(d + elemOffset)));
        d += stride;
    }
}

template<typename T>
void WeaveBufferElements(const std::vector<T>& weave, void* data, uint32_t vertexCt, uint32_t targetOffset, uint32_t stride)
{
    if (data == nullptr) return;

    unsigned char* d = (unsigned char*)data;
    for (uint32_t i = 0; i < vertexCount && i < weave.size(); ++i)
    {
        *((T*)(d + elemOffset)) = weave[i];
        d += stride;
    }
}


}