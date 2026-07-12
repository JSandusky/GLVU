//****************************************************************************
//
//  File:       glvu_math.cpp
//  License:    MIT
//  Project:    GLVU
//  Contents:   Helper types and functions for math.
//
//****************************************************************************

#include "glvu_math.h"
#include <xxhash.hpp>

namespace GLVU
{

std::array<uint32_t, 256> CRC32::table_ = CRC32::GenerateTable();

//****************************************************************************
//
//  Function:   CRC32::GenerateTable
//
//  Purpose:    Calculates the LUT for CRC-32 algorithm.
//
//	Return:		CRC32 LUT
//
//****************************************************************************
std::array<uint32_t, 256> CRC32::GenerateTable()
{
	std::array<uint32_t, 256> table;
	uint32_t polynomial = 0xEDB88320;
	for (uint32_t i = 0; i < 256; i++)
	{
		uint32_t c = i;
		for (size_t j = 0; j < 8; j++)
		{
			if (c & 1) {
				c = polynomial ^ (c >> 1);
			}
			else {
				c >>= 1;
			}
		}
		table[i] = c;
	}
	return table;
}

//****************************************************************************
//
//  Function:   CRC32::Update
//
//  Purpose:    Calculates incremental CRC-32 value.
//
//	Return:		CRC-32 accumulation
//
//****************************************************************************
uint32_t CRC32::Update(uint32_t* table, uint32_t initial, const void* buf, size_t len)
{
    return xxh::xxhash<32>(buf, len, initial);
	//uint32_t c = initial ^ 0xFFFFFFFF;
	//const uint8_t* u = static_cast<const uint8_t*>(buf);
	//for (size_t i = 0; i < len; ++i)
	//{
	//	c = table[(c ^ u[i]) & 0xFF] ^ (c >> 8);
	//}
	//return c ^ 0xFFFFFFFF;
}

//****************************************************************************
//
//  Function:   CRC32::Calculate
//
//  Purpose:    Computes a fresh CRC-32 value.
//
//	Return:		Calculate CRC-32 value.
//
//****************************************************************************
uint32_t CRC32::Calculate(const void* buf, size_t len)
{
    return xxh::xxhash<32>(buf, len);
	//return Update(table_.data(), 0, buf, len);
}

//****************************************************************************
//
//  Function:   half::ToFloat
//
//  Purpose:    Converts a half to a 32-bit float.
//
//	Source:		Implementation directly from DirectXMath/Inc/DirectXPackedVector.inl
//  License:    MIT License, Microsoft Corporation
//  URL:        https://github.com/microsoft/DirectXMath
//
//****************************************************************************
float half::ToFloat(int16_t value)
{
    auto Mantissa = static_cast<uint32_t>(value & 0x03FF);

    uint32_t Exponent = (value & 0x7C00);
    if (Exponent == 0x7C00) // INF/NAN
    {
        Exponent = 0x8f;
    }
    else if (Exponent != 0)  // The value is normalized
    {
        Exponent = static_cast<uint32_t>((static_cast<int>(value) >> 10) & 0x1F);
    }
    else if (Mantissa != 0)     // The value is denormalized
    {
        // Normalize the value in the resulting float
        Exponent = 1;

        do
        {
            Exponent--;
            Mantissa <<= 1;
        } while ((Mantissa & 0x0400) == 0);

        Mantissa &= 0x03FF;
    }
    else                        // The value is zero
    {
        Exponent = static_cast<uint32_t>(-112);
    }

    uint32_t Result =
        ((static_cast<uint32_t>(value) & 0x8000) << 16) // Sign
        | ((Exponent + 112) << 23)                      // Exponent
        | (Mantissa << 13);                             // Mantissa

    return reinterpret_cast<float*>(&Result)[0];
}

//****************************************************************************
//
//  Function:   half::FromFloat
//
//  Purpose:    Converts a 32-bit float into a half-precision float.
//
//	Source:		Implementation directly from DirectXMath/Inc/DirectXPackedVector.inl
//  License:    MIT License, Microsoft Corporation
//  URL:        https://github.com/microsoft/DirectXMath
//
//****************************************************************************
int16_t half::FromFloat(float value)
{
    uint32_t Result;

    auto IValue = reinterpret_cast<uint32_t*>(&value)[0];
    uint32_t Sign = (IValue & 0x80000000U) >> 16U;
    IValue = IValue & 0x7FFFFFFFU;      // Hack off the sign
    if (IValue >= 0x47800000 /*e+16*/)
    {
        // The number is too large to be represented as a half. Return infinity or NaN
        Result = 0x7C00U | ((IValue > 0x7F800000) ? (0x200 | ((IValue >> 13U) & 0x3FFU)) : 0U);
    }
    else if (IValue <= 0x33000000U /*e-25*/)
    {
        Result = 0;
    }
    else if (IValue < 0x38800000U /*e-14*/)
    {
        // The number is too small to be represented as a normalized half.
        // Convert it to a denormalized value.
        uint32_t Shift = 125U - (IValue >> 23U);
        IValue = 0x800000U | (IValue & 0x7FFFFFU);
        Result = IValue >> (Shift + 1);
        uint32_t s = (IValue & ((1U << Shift) - 1)) != 0;
        Result += (Result | s) & ((IValue >> Shift) & 1U);
    }
    else
    {
        // Rebias the exponent to represent the value as a normalized half.
        IValue += 0xC8000000U;
        Result = ((IValue + 0x0FFFU + ((IValue >> 13U) & 1U)) >> 13U) & 0x7FFFU;
    }
    return static_cast<int16_t>(Result | Sign);
}

//****************************************************************************
//
//  Function:   ViewFrusta::Set
//
//  Purpose:    Calculates geometric properties of frustum from provided 
//              projection and view matrices.
//
//****************************************************************************
void ViewFrusta::Set(const math::float3x4& view, const math::float4x4& projection)
{
    const auto projInverse = projection.Inverted();

    corners_[0] = view.TransformPos(projInverse.TransformPos(float3(1.0f, 1.0f, 0.0f)));
    corners_[1] = view.TransformPos(projInverse.TransformPos(float3(1.0f, -1.0f, 0.0f)));
    corners_[2] = view.TransformPos(projInverse.TransformPos(float3(-1.0f, -1.0f, 0.0f)));
    corners_[3] = view.TransformPos(projInverse.TransformPos(float3(-1.0f, 1.0f, 0.0f)));
    corners_[4] = view.TransformPos(projInverse.TransformPos(float3(1.0f, 1.0f, 1.0f)));
    corners_[5] = view.TransformPos(projInverse.TransformPos(float3(1.0f, -1.0f, 1.0f)));
    corners_[6] = view.TransformPos(projInverse.TransformPos(float3(-1.0f, -1.0f, 1.0f)));
    corners_[7] = view.TransformPos(projInverse.TransformPos(float3(-1.0f, 1.0f, 1.0f)));
    
    planes_[P_NEAR] = Plane(corners_[2], corners_[1], corners_[0]);
    planes_[P_LEFT] = Plane(corners_[3], corners_[7], corners_[6]);
    planes_[P_RIGHT] = Plane(corners_[1], corners_[5], corners_[4]);
    planes_[P_UP] = Plane(corners_[0], corners_[4], corners_[7]);
    planes_[P_DOWN] = Plane(corners_[6], corners_[5], corners_[1]);
    planes_[P_FAR] = Plane(corners_[5], corners_[6], corners_[7]);
}

//****************************************************************************
//
//  Function:   ViewFrusta::Transform
//
//  Purpose:    Creates a copy of this frusta and transforms it by the given
//              transform matrix.
//
//	Return:     Transformed ViewFrusta.
//
//****************************************************************************
ViewFrusta ViewFrusta::Transform(const math::float3x4& matrix)
{
    ViewFrusta ret = *this;

    for (int i = 0; i < 8; ++i)
        ret.corners_[i] = matrix.TransformPos(corners_[i]);

    ret.planes_[P_NEAR] = Plane(ret.corners_[2], ret.corners_[1], ret.corners_[0]);
    ret.planes_[P_LEFT] = Plane(ret.corners_[3], ret.corners_[7], ret.corners_[6]);
    ret.planes_[P_RIGHT] = Plane(ret.corners_[1], ret.corners_[5], ret.corners_[4]);
    ret.planes_[P_UP] = Plane(ret.corners_[0], ret.corners_[4], ret.corners_[7]);
    ret.planes_[P_DOWN] = Plane(ret.corners_[6], ret.corners_[5], ret.corners_[1]);
    ret.planes_[P_FAR] = Plane(ret.corners_[5], ret.corners_[6], ret.corners_[7]);

    return ret;
}

//****************************************************************************
//
//  Function:   ViewFrusta::GetLineIndices
//
//  Purpose:    Used for debug-drawing needs, or other circumstances where
//              frustum-edges are important.
//
//	Return:		Index pair into ViewFrusta::corners_[8] that defines 
//              a line segment.
//
//****************************************************************************
std::pair<int, int> ViewFrusta::GetLineIndices(int lineIndex) const
{
    static const std::pair<int, int> indexTable[] = {
        // near
        { 0, 1 },
        { 1, 2 },
        { 2, 3 },
        { 3, 0 },
        // far
        { 4, 5 },
        { 5, 6 },
        { 6, 7 },
        { 7, 4 },
        // cross
        { 0, 4 },
        { 1, 5 },
        { 2, 6 },
        { 3, 7 }
    };

    return indexTable[lineIndex];
}

//****************************************************************************
//
//  Function:   ViewFrusta::GetEdge
//
//  Purpose:    Wraps conversion of line-indices into an actual LineSegement
//              object for convenience.
//
//	Return:		MathGeoLib LineSegment from the corner-points.
//
//****************************************************************************
math::LineSegment ViewFrusta::GetEdge(int lineIndex) const
{
    const auto indices = GetLineIndices(lineIndex);
    return LineSegment(corners_[indices.first], corners_[indices.second]);
}

//****************************************************************************
//
//  Function:   ViewFrusta::GetPlaneCentroid
//
//  Purpose:    Calculates centroid of a frustum plane via average of
//              each contributing corner-point. Close-enough.
//
//	Return:		Centroid of the given plane.
//
//****************************************************************************
math::float3 ViewFrusta::GetPlaneCentroid(FrusPlane plane) const
{
    static int indices[][4] = {
        { 0, 1, 2, 3 }, // near
        { 2, 3, 7, 6 }, // left
        { 0, 1, 4, 5 }, // right
        { 0, 3, 4, 7 }, // top
        { 1, 2, 5, 6 }, // bottom
        { 4, 5, 6, 7 }, // far
    };

    auto accum = corners_[indices[plane][0]] +
        corners_[indices[plane][1]] +
        corners_[indices[plane][2]] +
        corners_[indices[plane][3]];

    accum *= 0.25f;

    return accum;
}

}