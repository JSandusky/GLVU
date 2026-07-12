#pragma once

#include "../MathBuildConfig.h"

#ifdef MATH_ENABLE_STL_SUPPORT
#include <string>
#include <vector>
#endif
#include "../MathGeoLibFwd.h"

MATH_BEGIN_NAMESPACE

struct int3
{
    int x = 0;
    int y = 0;
    int z = 0;

    int3() { }
    int3(int x, int y, int z) : x(x), y(y), z(z) { }

    void Set(int val) { x = y = z = val; }
    void Set(int xx, int yy, int zz) { x = xx; y = yy; z = zz; }

    int3 operator+(int val) const {
        return int3(x + val, y + val, z + val);
    }

    int3 operator-(int val) const {
        return int3(x - val, y - val, z - val);
    }

    int3 operator+(const int3& rhs) const {
        return int3(x + rhs.x, y + rhs.y, z + rhs.z);
    }

    int3 operator-(const int3& rhs) const {
        return int3(x - rhs.x, y - rhs.y, z - rhs.z);
    }

    int& operator[](int idx) {
        if (idx == 0)
            return x;
        else if (idx == 1)
            return y;
        return z;
    }

    const int& operator[](int idx) const {
        if (idx == 0)
            return x;
        else if (idx == 1)
            return y;
        return z;
    }

    int3& operator+=(int rhs) { *this = *this + rhs; return *this; }
    int3& operator-=(int rhs) { *this = *this - rhs; return *this; }
    int3& operator+=(const int3& rhs) { *this = *this + rhs; return *this; }
    int3& operator-=(const int3& rhs) { *this = *this - rhs; return *this; }

    bool operator==(const int3& rhs) const { return x == rhs.x && y == rhs.y && z == rhs.z; }
    bool operator!=(const int3& rhs) const { return !operator==(rhs); }

    bool AllPositive() const { return x > 0 && y > 0 && z > 0; }
    bool AllNegative() const { return x < 0 && y < 0 && z < 0; }
    bool AllZero() const { return x == 0 && y == 0 && z == 0; }
    bool AllNonZero() const { return !AllZero(); }
    
    int MaxIndex() const {
        if (x > y && x > z)
            return 0;
        if (y > x && y > z)
            return 1;
        if (z > y && z > x)
            return 2;
        // TODO: what to do
        return 2;
    }
    
    int MinIndex() const {
        if (x < y && x < z)
            return 0;
        if (y < x && y < z)
            return 1;
        if (z < y && z < x)
            return 2;
        // TODO: what to do
        return 2;
    }

    static const int3 One;
    static const int3 Zero;
    static const int3 NegativeOne;
    static const int3 Min;
    static const int3 Max;
};

struct uint3
{
    uint32_t x = 0;
    uint32_t y = 0;
    uint32_t z = 0;

    uint3() { }
    uint3(uint32_t x, uint32_t y, uint32_t z) : x(x), y(y), z(z) { }

    void Set(uint32_t val) { x = y = z = val; }
    void Set(uint32_t xx, uint32_t yy, uint32_t zz) { x = xx; y = yy; z = zz; }

    uint3 operator+(uint32_t val) const {
        return uint3(x + val, y + val, z + val);
    }

    uint3 operator-(uint32_t val) const {
        return uint3(x - val, y - val, z - val);
    }

    uint3 operator+(const uint3& rhs) const {
        return uint3(x + rhs.x, y + rhs.y, z + rhs.z);
    }

    uint3 operator-(const uint3& rhs) const {
        return uint3(x - rhs.x, y - rhs.y, z - rhs.z);
    }

    uint32_t& operator[](int idx) {
        if (idx == 0)
            return x;
        else if (idx == 1)
            return y;
        return z;
    }

    const uint32_t& operator[](int idx) const {
        if (idx == 0)
            return x;
        else if (idx == 1)
            return y;
        return z;
    }

    uint3& operator+=(int rhs) { *this = *this + rhs; return *this; }
    uint3& operator-=(int rhs) { *this = *this - rhs; return *this; }
    uint3& operator+=(const uint3& rhs) { *this = *this + rhs; return *this; }
    uint3& operator-=(const uint3& rhs) { *this = *this - rhs; return *this; }

    bool operator==(const uint3& rhs) const { return x == rhs.x && y == rhs.y && z == rhs.z; }
    bool operator!=(const uint3& rhs) const { return !operator==(rhs); }

    bool AllPositive() const { return x > 0 && y > 0 && z > 0; }
    bool AllNegative() const { return x < 0 && y < 0 && z < 0; }
    bool AllZero() const { return x == 0 && y == 0 && z == 0; }
    bool AllNonZero() const { return !AllZero(); }

    int MaxIndex() const {
        if (x > y && x > z)
            return 0;
        if (y > x && y > z)
            return 1;
        if (z > y && z > x)
            return 2;
        // TODO: what to do
        return 2;
    }

    int MinIndex() const {
        if (x < y && x < z)
            return 0;
        if (y < x && y < z)
            return 1;
        if (z < y && z < x)
            return 2;
        // TODO: what to do
        return 2;
    }

    static const uint3 One;
    static const uint3 Zero;
    static const uint3 Min;
    static const uint3 Max;
};

MATH_END_NAMESPACE