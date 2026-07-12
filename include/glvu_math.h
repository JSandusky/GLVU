//****************************************************************************
//
//  File:       glvu_math.h
//  License:    MIT
//  Project:    GLVU
//  Contents:   Helper types and functions for math operations.
//
//****************************************************************************

#pragma once

#include "glvu.h"

#include <array>

namespace GLVU
{

/// CRCs are used to hash state objects. Particularly by DX11.
struct CRC32
{
private:
	static std::array<uint32_t, 256> table_;

	/// Computes table, should only be invoked once.
	static std::array<uint32_t, 256> GenerateTable();

	/// Incremental compute.
	static uint32_t Update(uint32_t* table, uint32_t initial, const void* buf, size_t len);

public:
	/// Returns CRC for given object.
	static uint32_t Calculate(const void* buf, size_t len);

	/// Accumulates an inremental CRC-32.
	inline static uint32_t CalcAdd(uint32_t current, const void* buf, size_t len) { return Update(table_.data(), current, buf, len); }

    inline static void Combine(uint32_t& current, uint32_t next) { current ^= next + 0x9e3779b9 + (current << 6) + (current>> 2); }
};

/// float16 type, for vertex-buffers and textures.
struct GLVU_API half
{
    int16_t raw_;

    half() : raw_(0) { }
    half(float v) : raw_(FromFloat(v)) { }

    inline float ToFloat() const { return ToFloat(raw_); }
    half& operator=(const float& v) { raw_ = FromFloat(v); return *this; }

    static float ToFloat(int16_t);
    static int16_t FromFloat(float);

    inline bool operator==(const half& rhs) const { return raw_ == rhs.raw_; }
};

/// Float16 Vector
struct GLVU_API half4
{
    half x_, y_, z_, w_;

    half4() { }
    half4(float x, float y, float z, float w) : x_(x), y_(y), z_(z), w_(w) { }
    half4(const math::float4& v) : x_(v.x), y_(v.y), z_(v.z), w_(v.w) { }
    half4(const math::Quat& q) : x_(q.x), y_(q.y), z_(q.z), w_(q.w) { }

    half* Data() { return &x_; }
    const half* Data() const { return &x_; }
};

/// Used instead of MathGeoLib's Frustum type sicne the Frustum type is pretty much garbage.
struct GLVU_API ViewFrusta
{
    enum FrusPlane
    {
        P_NEAR = 0,
        P_LEFT,
        P_RIGHT,
        P_UP,
        P_DOWN,
        P_FAR,
    };
    enum FrusCorner
    {
        C_NEAR_TOP_RIGHT,
        C_NEAR_BOTTOM_RIGHT,
        C_NEAR_BOTTOM_LEFT,
        C_NEAR_TOP_LEFT,
        C_FAR_TOP_RIGHT,
        C_FAR_BOTTOM_RIGHT,
        C_FAR_BOTTOM_LEFT,
        C_FAR_TOP_LEFT,
    };

    math::Plane planes_[6];
    math::float3 corners_[8];

    void Set(const math::float3x4& view, const math::float4x4& projection);
   
    /// Returns corner point indices for a line segment of the frustum, use for debug-drawing
    std::pair<int, int> GetLineIndices(int lineIdex) const;
    math::LineSegment GetEdge(int lineIndex) const;
    inline int GetNumLines() const { return 12; }

    math::float3 GetPlaneCentroid(FrusPlane) const;

    ViewFrusta Transform(const math::float3x4&);

    inline bool Contains(const math::float3& p) const
    {
        for (const auto& plane : planes_)
            if (plane.IsOnPositiveSide(p))
                return false;

        return true;
    }

    inline bool Contains(const math::Sphere& s) const 
    {
        for (const auto& plane : planes_)
        {
            float dist = plane.Distance(s.pos);
            if (dist < -s.r)
                return false;
        }

        return true;
    }

    inline bool Contains(const math::AABB& b) const 
    {
        auto center = b.Centroid();
        auto edge = center - b.minPoint;

        for (const auto& plane : planes_)
        {
            float dist = plane.normal.Dot(center) + plane.d;
            float absDist = plane.normal.Abs().Dot(edge);

            if (dist < -absDist)
                return false;
        }

        return true;
    }
};

}
