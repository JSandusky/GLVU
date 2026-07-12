#pragma once

#include "../MathBuildConfig.h"

#ifdef MATH_ENABLE_STL_SUPPORT
#include <string>
#include <vector>
#endif
#include "../MathGeoLibFwd.h"

MATH_BEGIN_NAMESPACE

struct int2
{
    int x = 0;
    int y = 0;

    int2() { }
    int2(int x, int y) : x(x), y(y) { }

    void Set(int val) { x = y = val; }
    void Set(int xx, int yy) { x = xx; y = yy; }

    int2 operator+(int val) const {
        return int2(x + val, y + val);
    }

    int2 operator-(int val) const {
        return int2(x - val, y - val);
    }

    int2 operator+(const int2& rhs) const {
        return int2(x + rhs.x, y + rhs.y);
    }

    int2 operator-(const int2& rhs) const {
        return int2(x - rhs.x, y - rhs.y);
    }

    int& operator[](int idx) {
        if (idx == 0)
            return x;
        return y;
    }

    const int& operator[](int idx) const {
        if (idx == 0)
            return x;
        return y;
    }

    int2& operator+=(int rhs) { *this = *this + rhs; return *this; }
    int2& operator-=(int rhs) { *this = *this - rhs; return *this; }
    int2& operator+=(const int2& rhs) { *this = *this + rhs; return *this; }
    int2& operator-=(const int2& rhs) { *this = *this - rhs; return *this; }

    bool operator==(const int2& rhs) const { return x == rhs.x && y == rhs.y; }
    bool operator!=(const int2& rhs) const { return !operator==(rhs); }

    bool AllPositive() const { return x > 0 && y > 0; }
    bool AllNegative() const { return x < 0 && y < 0; }
    bool AllZero() const { return x == 0 && y == 0; }
    bool AllNonZero() const { return !AllZero(); }

    int MaxIndex() const {
        if (x > y)
            return 0;
        if (y > x)
            return 1;
        // TODO: what to do
        return 1;
    }

    int MinIndex() const {
        if (x < y)
            return 0;
        if (y < x)
            return 1;
        // TODO: what to do
        return 1;
    }

    static const int2 One;
    static const int2 Zero;
    static const int2 NegativeOne;
    static const int2 Min;
    static const int2 Max;
};

struct uint2
{
    uint32_t x = 0;
    uint32_t y = 0;

    uint2() { }
    uint2(uint32_t x, uint32_t y) : x(x), y(y) { }

    void Set(uint32_t val) { x = y = val; }
    void Set(uint32_t xx, uint32_t yy) { x = xx; y = yy; }

    uint2 operator+(uint32_t val) const {
        return uint2(x + val, y + val);
    }

    uint2 operator-(uint32_t val) const {
        return uint2(x - val, y - val);
    }

    uint2 operator+(const uint2& rhs) const {
        return uint2(x + rhs.x, y + rhs.y);
    }

    uint2 operator-(const uint2& rhs) const {
        return uint2(x - rhs.x, y - rhs.y);
    }

    uint32_t& operator[](int idx) {
        if (idx == 0)
            return x;
        return y;
    }

    const uint32_t& operator[](int idx) const {
        if (idx == 0)
            return x;
        return y;
    }

    uint2& operator+=(uint32_t rhs) { *this = *this + rhs; return *this; }
    uint2& operator-=(uint32_t rhs) { *this = *this - rhs; return *this; }
    uint2& operator+=(const uint2& rhs) { *this = *this + rhs; return *this; }
    uint2& operator-=(const uint2& rhs) { *this = *this - rhs; return *this; }

    bool operator==(const uint2& rhs) const { return x == rhs.x && y == rhs.y; }
    bool operator!=(const uint2& rhs) const { return !operator==(rhs); }

    bool AllPositive() const { return x > 0 && y > 0; }
    bool AllNegative() const { return x < 0 && y < 0; }
    bool AllZero() const { return x == 0 && y == 0; }
    bool AllNonZero() const { return !AllZero(); }

    int MaxIndex() const {
        if (x > y)
            return 0;
        if (y > x)
            return 1;
        // TODO: what to do
        return 1;
    }

    int MinIndex() const {
        if (x < y)
            return 0;
        if (y < x)
            return 1;
        // TODO: what to do
        return 1;
    }

    static const uint2 One;
    static const uint2 Zero;
    static const uint2 Min;
    static const uint2 Max;
};

MATH_END_NAMESPACE