/* Copyright Jukka Jylänki

   Licensed under the Apache License, Version 2.0 (the "License");
   you may not use this file except in compliance with the License.
   You may obtain a copy of the License at

       http://www.apache.org/licenses/LICENSE-2.0

   Unless required by applicable law or agreed to in writing, software
   distributed under the License is distributed on an "AS IS" BASIS,
   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
   See the License for the specific language governing permissions and
   limitations under the License. */

/** @file MathGeoLib.h
	@author Jukka Jylänki
	@brief A conveniency file to include all header files in MathGeoLib. */

#pragma once

#include "Geometry/GeometryAll.h"
#include "Math/MathAll.h"
#include "Algorithm/Random/LCG.h"
#include "Time/Clock.h"

typedef math::float2 Vec2;
typedef math::float3 Vec3;
typedef math::float4 Vec4;
typedef math::float3x3 Mat3x3;
typedef math::float3x4 Mat3x4;
typedef math::float4x4 Mat4x4;
typedef math::AABB BoundingBox;
typedef math::Circle Disc;
typedef math::OBB OrientedBoundingBox;
typedef math::int2 IntVec2;
typedef math::int3 IntVec3;
typedef math::int4 IntVec4;