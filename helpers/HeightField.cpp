#include "HeightField.h"

namespace GLVU
{

//****************************************************************************
//
//  Function:   HeightFiled::GetHeight
//
//  Purpose:    Sample the bilinearly interpolated height coordinate.
//
//  Return:     height at the given point, or whatever we get from a clipped range
//
//****************************************************************************
float HeightField::GetHeight(float x, float y) const
{
    return 0.0f;
}

//****************************************************************************
//
//  Function:   HeightFiled::GetGeometricHeight
//
//  Purpose:    Sample the height as appropriate by the triangulation.
//              Height value a point on the plane of one of the 2 triangles
//              of the terrain's cell.
//               ____
//              | * /|
//              |  / |
//              | /  |
//              |/___|
//
//  Return:     Y value of XY in the plane of a triangle
//
//****************************************************************************
float HeightField::GetGeometricHeight(float x, float y) const
{
    return 0.0f;
}

}