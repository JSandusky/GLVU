#pragma once

#include "../MathBuildConfig.h"
#include "SSEMath.h"
#include "Quat.h"
#include "float3.h"
#include "float3x3.h"
#include "float3x4.h"

#ifdef MATH_ENABLE_STL_SUPPORT
#include <string>
#endif
#include "../MathGeoLibFwd.h"

MATH_BEGIN_NAMESPACE

class ALIGN16 DualQuat
{
public:
    Quat real_;
    Quat dual_;

    DualQuat() : real_(Quat::identity), dual_(0, 0, 0, 0) { }
    DualQuat(const Quat& r, const Quat& d) : real_(r), dual_(d) { }
    DualQuat(const vec& translation, const Quat& rotation) {
        real_ = rotation;
        dual_ = Quat(0.5f * translation.x, 0.5f * translation.y, 0.5f * translation.z, 0.0f) * real_;
    }
    DualQuat(const float3x4& mat) {
        const Quat rotation = Quat(mat.RotatePart());
        const auto translatePart = mat.TranslatePart();
        const Quat translation = Quat(
            translatePart.x * 0.5f,
            translatePart.y * 0.5f,
            translatePart.z * 0.5f,
            0.0f
        );

        real_ = rotation;
        dual_ = translation * rotation;
    }
    float3 TransformPos(const vec& v) const {
        const auto dq = (*this) * DualQuat(Quat::identity, Quat(v.x, v.y, v.z, 0.0f)) * Conjugated();
        float3 retValue(dq.dual_.x, dq.dual_.y, dq.dual_.z);
        retValue += (real_.w * dual_.CastToFloat4() - dual_.w * real_.CastToFloat4().Cross(dual_.CastToFloat4())).xyz() * 2.0f;
        return retValue;
    }
    float3 TransformDir(const vec& v) const {
        const auto dq = (*this) * DualQuat(Quat::identity, Quat(v.x, v.y, v.z, 0.0f)) * Conjugated();
        return float3(dq.dual_.x, dq.dual_.y, dq.dual_.z);
    }

    inline DualQuat QuatConjugated() const {
        return DualQuat(real_.Conjugated(), dual_.Conjugated());
    }

    inline DualQuat Conjugated() const {
        return DualQuat(real_.Conjugated(), Quat(dual_.x, dual_.y, dual_.z, -dual_.w));
    }
    DualQuat& Conjugate() {
        *this = Conjugated();
        return *this;
    }
    DualQuat& QuatConjugate() {
        *this = QuatConjugated();
        return *this;
    }

    inline DualQuat operator*(const DualQuat& rhs) const {
        return DualQuat(
            real_ * rhs.real_,
            Add(real_ * rhs.dual_, dual_ * rhs.real_)
        );
    }
    float3x4 ToMatrix() const {
        float3x4 r(real_);
        auto translation = (dual_ * real_.Conjugated()).CastToFloat4() * 2.0f;
        r.v[0][3] = translation.x;
        r.v[1][3] = translation.y;
        r.v[2][3] = translation.z;
        return r;
    }
    inline float3 Translation() const {
        return ((dual_ * real_.Conjugated()).CastToFloat4() * 2.0f).xyz();
    }
    inline Quat Rotation() const {
        return real_;
    }
private:
    static inline Quat Add(const Quat& lhs, const Quat& rhs) {
        return Quat(lhs.x + rhs.x,
            lhs.y + rhs.y,
            lhs.z + rhs.z,
            lhs.w + rhs.w);
    }
};

MATH_END_NAMESPACE
