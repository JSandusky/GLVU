
#include "AnimatedModel.h"

#include <MathGeoLib/Math/float3x3.h>

namespace GLVU
{

//****************************************************************************
//
//  Function:   QuaternionToTangentBitangent
//
//  Purpose:    Not for serious use, only intended so a reference is always on
//              hand (and near the QTangent code).
//
//  WARNING:    IF YOU CHANGE THE Q-TANGENT CODE YOU NEED TO VERIFY THIS
//              REFERENCE STILL HOLDS! IF IT DOESN'T THEN FIX IT!
//
//****************************************************************************
float3x3 QuaternionToTangentBitangent(Quat q)
{
    return float3x3(
        1 - 2 * (q.y*q.y + q.z*q.z), 2 * (q.x*q.y + q.w*q.z), 2 * (q.x*q.z - q.w*q.y),
        2 * (q.x*q.y - q.w*q.z), 1 - 2 * (q.x*q.x + q.z*q.z), 2 * (q.y*q.z + q.w*q.x),
        0.0f, 0.0f, 0.0f // cross(tBt[0], tBt[1]) * (q.w < 0 ? -1 : 1) // if we wanted it
    );
}

//****************************************************************************
//
//  Function:   FrameToQTangent
//
//  Purpose:    Converts a 3x3 rotation matrix (typically constructed via
//              basis vectors) into a Q-Tangent. The Q-Tangent is a quaternion
//              that has some extra considerations made to prevent -0,+0
//              singularities due to IEEE non-compliance in GPUs.
//
//****************************************************************************
Quat FrameToQTangent(float3x3 frame)
{
    const float scale = frame.Determinant() < 0 ? 1.0f : -1.0f;

    frame[2][0] *= scale;
    frame[2][1] *= scale;
    frame[2][2] *= scale;

    auto qTangent = frame.ToQuat();

    // singularity evasion
    const float epsilon = 0.000001f;
    if (math::Equal(qTangent.w, 0.0f, epsilon))
    {
        qTangent.w = qTangent.w > 0 ? epsilon : -epsilon;
        const auto remap = sqrt(1.0f - epsilon * epsilon);
        qTangent.x *= remap;
        qTangent.y *= remap;
        qTangent.z *= remap;
    }

    // mirroring
    const float finalScale = (scale < 0 && qTangent.w > 0.0f) || (scale > 0 && qTangent.w < 0.0f) ? -1.0f : 1.0f;
    qTangent.x *= finalScale;
    qTangent.y *= finalScale;
    qTangent.z *= finalScale;
    qTangent.w *= finalScale;

    return qTangent;
}

//****************************************************************************
//
//  Function:   CalculateQTangents
//
//  Purpose:    Produces the quaternions representing the orientation of the
//              nor,tan,bitan frame. This particular version works on a blob.
//
//  Notes:      Does not orthonormalize, you should have done that already.
//
//****************************************************************************
void CalculateQTangents(std::vector<math::Quat>& output, void* data, uint32_t vertexCt, uint32_t offsetNor, uint32_t offsetTan, uint32_t offsetBitan, uint32_t stride)
{
    output.reserve(vertexCt);

    unsigned char* d = (unsigned char*)data;
    for (uint32_t idx = 0; idx < vertexCt; ++idx)
    {
        const float3 nor = *((float3*)(d + offsetNor));
        const float3 tan = *((float3*)(d + offsetTan));
        const float3 bitan = *((float3*)(d + offsetBitan));

        auto frame = float3x3(
            bitan.x, bitan.y, bitan.z,
            tan.x, tan.y, tan.z,
            nor.x, nor.y, nor.z
        );

        output.push_back(FrameToQTangent(frame));

        d += stride;
    }
}

//****************************************************************************
//
//  Function:   CalculateQTangents
//
//  Purpose:    Produces the quaternions representing the orientation of the
//              nor,tan,bitan frame. This particular version works with vectors.
//
//  Notes:      Does not orthonormalize, you should have done that already.
//
//****************************************************************************
void CalculateQTangents(std::vector<math::Quat>& output, const std::vector<math::float3>& normals, const std::vector<math::float3>& tangents, const std::vector<math::float3>& bitangents)
{
    output.reserve(normals.size());

    for (size_t i = 0; i < normals.size(); ++i)
    {
        const auto& bitan = bitangents[i];
        const auto& tan = tangents[i];
        const auto& nor = normals[i];
        
        auto frame = float3x3(
            bitan.x, bitan.y, bitan.z,
            tan.x, tan.y, tan.z,
            nor.x, nor.y, nor.z
        );

        output.push_back(FrameToQTangent(frame));
    }
}

//****************************************************************************
//
//  Function:   CalculateQTangents
//
//  Purpose:    Produces the quaternions representing the orientation of the
//              nor,tan,bitan frame. This particular version works on vectors
//              where the tangent's W component stores a multiplier (-1 or 1)
//              that represents the orientation of the bitangent found via the
//              cross-product of the normal and tangent.
//
//  Notes:      Does not orthonormalize, you should have done that already.
//              Handed-tangents are a fatter alternative to Q-Tangents.
//
//****************************************************************************
void CalculateQTangents(std::vector<math::Quat>& output, const std::vector<math::float3>& normals, const std::vector<math::float4>& handedTangents)
{
    output.reserve(normals.size());

    for (size_t i = 0; i < normals.size(); ++i)
    {
        //const auto& bitan = bitangents[i];
        const auto& tan = handedTangents[i].xyz();
        const auto& nor = normals[i];
        const auto bitan = tan.Cross(nor) * handedTangents[i].w;

        auto frame = float3x3(
            bitan.x, bitan.y, bitan.z,
            tan.x, tan.y, tan.z,
            nor.x, nor.y, nor.z
        );

        output.push_back(FrameToQTangent(frame));
    }
}

}

