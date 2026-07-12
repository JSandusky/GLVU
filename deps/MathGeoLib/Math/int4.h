#pragma once

#include "../MathBuildConfig.h"

#ifdef MATH_ENABLE_STL_SUPPORT
#include <string>
#include <vector>
#endif
#include "../MathGeoLibFwd.h"

MATH_BEGIN_NAMESPACE

struct ALIGN16 int4
{
    int x = 0;
    int y = 0;
    int z = 0;
    int w = 0;

    int4() { }
    int4(int x, int y, int z, int w) : x(x), y(y), z(z), w(w) { }

    int Width() const { return z - x; }
    int Height() const { return w - y; }

    void Set(int val) { x = y = z = w = val; }
    void Set(int xx, int yy, int zz, int ww) { x = xx; y = yy; z = zz; w = ww; }

    int4 operator+(int val) const {
        return int4(x + val, y + val, z + val, w + val);
    }

    int4 operator-(int val) const {
        return int4(x - val, y - val, z - val, w - val);
    }

    int4 operator+(const int4& rhs) const {
        return int4(x + rhs.x, y + rhs.y, z + rhs.z, w + rhs.w);
    }

    int4 operator-(const int4& rhs) const {
        return int4(x - rhs.x, y - rhs.y, z - rhs.z, z - rhs.z);
    }

    int& operator[](int idx) {
        if (idx == 0)
            return x;
        else if (idx == 1)
            return y;
        else if (idx == 2)
            return z;
        return w;
    }

    const int& operator[](int idx) const {
        if (idx == 0)
            return x;
        else if (idx == 1)
            return y;
        else if (idx == 2)
            return z;
        return w;
    }

    int4& operator+=(int rhs) { *this = *this + rhs; return *this; }
    int4& operator-=(int rhs) { *this = *this - rhs; return *this; }
    int4& operator+=(const int4& rhs) { *this = *this + rhs; return *this; }
    int4& operator-=(const int4& rhs) { *this = *this - rhs; return *this; }

    bool operator==(const int4& rhs) const { return x == rhs.x && y == rhs.y && z == rhs.z; }
    bool operator!=(const int4& rhs) const { return !operator==(rhs); }

    bool AllPositive() const { return x > 0 && y > 0 && z > 0 && w > 0; }
    bool AllNegative() const { return x < 0 && y < 0 && z < 0 && w < 0; }
    bool AllZero() const { return x == 0 && y == 0 && z == 0 && w == 0; }
    bool AllNonZero() const { return !AllZero(); }

    int MaxIndex() const {
        if (x > y && x > z && x > w)
            return 0;
        if (y > x && y > z && y > w)
            return 1;
        if (z > y && z > x && z > w)
            return 2;

        // TODO: what to do
        return 3;
    }

    int MinIndex() const {
        if (x < y && x < z && x < w)
            return 0;
        if (y < x && y < z && y < w)
            return 1;
        if (z < y && z < x && z < w)
            return 2;
        // TODO: what to do
        return 3;
    }

    static const int4 One;
    static const int4 Zero;
    static const int4 NegativeOne;
    static const int4 Min;
    static const int4 Max;
};

struct ALIGN16 uint4
{
    uint32_t x = 0;
    uint32_t y = 0;
    uint32_t z = 0;
    uint32_t w = 0;

    uint4(uint32_t v = 0) : x(v), y(v), z(v), w(v) { }
    uint4(uint32_t x, uint32_t y, uint32_t z, uint32_t w) : x(x), y(y), z(z), w(w) { }

    static uint4 FromPosSize(uint32_t x, uint32_t y, uint32_t w, uint32_t h)
    {
        return uint4(x, y, x + w, y + h);
    }

    void Set(uint32_t val) { x = y = z = w = val; }
    void Set(uint32_t xx, uint32_t yy, uint32_t zz, uint32_t ww) { x = xx; y = yy; z = zz; w = ww; }

    uint4 operator+(int val) const {
        return uint4(x + val, y + val, z + val, w + val);
    }

    uint4 operator-(int val) const {
        return uint4(x - val, y - val, z - val, w - val);
    }

    uint4 operator+(const uint4& rhs) const {
        return uint4(x + rhs.x, y + rhs.y, z + rhs.z, w + rhs.w);
    }

    uint4 operator-(const uint4& rhs) const {
        return uint4(x - rhs.x, y - rhs.y, z - rhs.z, z - rhs.z);
    }

    uint32_t& operator[](int idx) {
        if (idx == 0)
            return x;
        else if (idx == 1)
            return y;
        else if (idx == 2)
            return z;
        return w;
    }

    const uint32_t& operator[](int idx) const {
        if (idx == 0)
            return x;
        else if (idx == 1)
            return y;
        else if (idx == 2)
            return z;
        return w;
    }

    uint4& operator+=(int rhs) { *this = *this + rhs; return *this; }
    uint4& operator-=(int rhs) { *this = *this - rhs; return *this; }
    uint4& operator+=(const uint4& rhs) { *this = *this + rhs; return *this; }
    uint4& operator-=(const uint4& rhs) { *this = *this - rhs; return *this; }

    bool operator==(const uint4& rhs) const { return x == rhs.x && y == rhs.y && z == rhs.z; }
    bool operator!=(const uint4& rhs) const { return !operator==(rhs); }

    bool AllPositive() const { return x > 0 && y > 0 && z > 0 && w > 0; }
    bool AllNegative() const { return x < 0 && y < 0 && z < 0 && w < 0; }
    bool AllZero() const { return x == 0 && y == 0 && z == 0 && w == 0; }
    bool AllNonZero() const { return !AllZero(); }

    int MaxIndex() const {
        if (x > y && x > z && x > w)
            return 0;
        if (y > x && y > z && y > w)
            return 1;
        if (z > y && z > x && z > w)
            return 2;

        // TODO: what to do
        return 3;
    }

    int MinIndex() const {
        if (x < y && x < z && x < w)
            return 0;
        if (y < x && y < z && y < w)
            return 1;
        if (z < y && z < x && z < w)
            return 2;
        // TODO: what to do
        return 3;
    }

    inline uint32_t Width() const { return z - x; }
    inline uint32_t Height() const { return w - y; }
    inline uint32_t Right() const { return x + z; }
    inline uint32_t Top() const { return y + w; }
    inline uint32_t Left() const { return x; }
    inline uint32_t Bottom() const { return y; }
    inline uint2 Center() const { return { x + Width() / 2, y + Height() / 2 }; }
    inline uint2 TopLeft() const { return { x, y + w }; }
    inline uint2 TopRight() const { return { x + z, y + w }; }
    inline uint2 BottomLeft() const { return { x, y }; }
    inline uint2 BottomRight() const { return { x + z, y }; }

    static const uint4 One;
    static const uint4 Zero;
    static const uint4 Min;
    static const uint4 Max;
};

MATH_END_NAMESPACE