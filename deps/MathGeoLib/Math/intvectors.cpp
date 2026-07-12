#include "int2.h"
#include "int3.h"
#include "int4.h"
#include "Rect.h"

MATH_BEGIN_NAMESPACE

const Rect Rect::ZERO = Rect(0, 0, 0, 0);

const int2 int2::One = int2(1, 1);
const int2 int2::Zero = int2();
const int2 int2::NegativeOne = int2(-1, -1);
const int2 int2::Min = int2(INT_MIN, INT_MIN);
const int2 int2::Max = int2(INT_MAX, INT_MAX);

const int3 int3::One = int3(1, 1, 1);
const int3 int3::Zero = int3();
const int3 int3::NegativeOne = int3(-1, -1, -1);
const int3 int3::Min = int3(INT_MIN, INT_MIN, INT_MIN);
const int3 int3::Max = int3(INT_MAX, INT_MAX, INT_MAX);

const int4 int4::One = int4(1, 1, 1, 1);
const int4 int4::Zero = int4();
const int4 int4::NegativeOne = int4(-1, -1, -1, -1);
const int4 int4::Min = int4(INT_MIN, INT_MIN, INT_MIN, INT_MIN);
const int4 int4::Max = int4(INT_MAX, INT_MAX, INT_MAX, INT_MAX);

const uint2 uint2::One = uint2(1, 1);
const uint2 uint2::Zero = uint2();
const uint2 uint2::Min = uint2();
const uint2 uint2::Max = uint2(UINT_MAX, UINT_MAX);

const uint3 uint3::One = uint3(1, 1, 1);
const uint3 uint3::Zero = uint3();
const uint3 uint3::Min = uint3();
const uint3 uint3::Max = uint3(UINT_MAX, UINT_MAX, UINT_MAX);

const uint4 uint4::One = uint4(1, 1, 1, 1);
const uint4 uint4::Zero = uint4();
const uint4 uint4::Min = uint4();
const uint4 uint4::Max = uint4(UINT_MAX, UINT_MAX, UINT_MAX, UINT_MAX);

MATH_END_NAMESPACE