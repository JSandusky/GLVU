//****************************************************************************
//
//  File:       ShaderConstraints.h
//  License:    MIT
//  Project:    GLVU
//  Contents:   Defs and structs that specify the contents of shaders, specifically
//              the UBOs of them.
//
//****************************************************************************

#pragma once

#include <glvu.h>

namespace GLVU
{
    // UBO #0 is allowed 256-bytes per object
    // Additional instance data is allowed 64-bytes per object

#define SHADER_BUFFER_USER 0
#define SHADER_BUFFER_USER_EXTRA 1

#define SHADER_BUFFER_OBJECT_DATA 2
#define SHADER_BUFFER_VIEW_DATA 3
#define SHADER_BUFFER_TARGET_DATA 4
#define SHADER_BUFFER_FULLSCREEN_PARAMS 5
#define SHADER_BUFFER_COMMAND_PARAMS 5

#define SHADER_BUFFER_LIGHT_DATA 6
#define SHADER_BUFFER_DECAL_DATA 7
#define SHADER_BUFFER_PROBE_DATA 8
#define SHADER_BUFFER_CLUSTER_COUNTS 9
#define SHADER_BUFFER_CLUSTER_LIGHT_INDEXES 10
#define SHADER_BUFFER_CLUSTER_DECAL_INDEXES 11

#define SHADER_TEX_ENVMAP 14
#define SHADER_TEX_SHADOWMAP 15

#define PIPELINE_RESOURCE_SHADOWMAP "SHADOWMAP"
#define PIPELINE_RESOURCE_BACKBUFFER "BACKBUFFER"
#define PIPELINE_RESOURCE_DEPTHBUFFER "DEPTHBUFFER"
#define PIPELINE_RESOURCE_GOBO_TABLE "GOBO_TABLE"
#define PIPELINE_RESOURCE_OBJECT_SPACE_LIGHTING "OBJECT_SPACE_LIGHTING"

#define PIEPLINE_RESOURCE_CLUSTERS "CLUSTERS"
#define PIPELINE_RESOURCE_LIGHT_TILES "LIGHT_TILES"
#define PIPELINE_RESOURCE_PROBE_TILES "PROBE_TILES"
#define PIPELINE_RESOURCE_DECAL_TILES "DECAL_TILES"

// CONTEXT AND DEFINES

#define SHADER_CONTEXT_POINT_LIGHT "POINT_LIGHT"
#define SHADER_CONTEXT_SPOT_LIGHT "SPOT_LIGHT"
#define SHADER_CONTEXT_DIR_LIGHT "DIRECTIONAL_LIGHT"
#define SHADER_CONTEXT_POINT_LIGHT_SHADOW "POINT_LIGHT_SHDW"
#define SHADER_CONTEXT_SPOT_LIGHT_SHADOW "SPOT_LIGHT_SHDW"
#define SHADER_CONTEXT_DIR_LIGHT_SHADOW "DIRECTIONAL_LIGHT_SHDW"
#define SHADER_CONTEXT_OFFSCREEN_LIGHT "OFFSCREEN_LIGHT"

#define SHADER_CONTEXT_SUFFIX_INST "_INST"
#define SHADER_CONTEXT_SUFFIX_SKINNED "_SKINNED"

#define SHADER_CONTEXT_RENDER_SHADOWMAP_POINT "SHADOW_POINTLIGHT"
#define SHADER_CONTEXT_RENDER_SHADOWMAP_SPOTLIGHT "SHADOW_SPOTLIGHT"
#define SHADER_CONTEXT_RENDER_SHADOWMAP_DIRLIGHT "SHADOW_DIRLIGHT"

#define SHADER_DEFINE_INST "INSTANCED"
#define SHADER_DEFINE_SKINNED "SKINNED"
#define SHADER_DEFINE_POINTLIGHT "POINTLIGHT"
#define SHADER_DEFINE_SPOTLIGHT "SPOTLIGHT"
#define SHADER_DEFINE_DIRLIGHT "DIRLIGHT"

#if 0
// FOR REFERENCE: these are the forward lighting premutations
static const char* Tables[] = {
	"_LITPOINT",
	"_LITPOINT_SKINNED",
	"_LITPOINT_INST",
	"_LITPOINT_SHDW",
	"_LITPOINT_SHDW_SKINNED",
	"_LITPOINT_SHDW_INST",
	"_LITSPOT",
	"_LITSPOT_SKINNED",
	"_LITSPOT_INST",
	"_LITSPOT_SHDW",
	"_LITSPOT_SHDW_SKINNED",
	"_LITSPOT_SHDW_INST",
	"_LITDIR",
	"_LITDIR_SKINNED",
	"_LITDIR_INST",
	"_LITDIR_SHDW",
	"_LITDIR_SHDW_SKINNED",
	"_LITDIR_SHDW_INST",
};
#endif

    struct PerFrameData {
        float frameDelta;
        unsigned frameCount;
    };

    struct CameraData
    {
        math::float4x4 viewProj;
        math::float4x4 invViewProj;
        math::float4 viewPos;
        math::float4 viewDir;
        math::float4 viewUp;
        math::float4 viewRight;
        math::uint4 viewport; // x,y width/height of the viewport
        math::float4 nearFar; // x = near, y = far, ZW = unused
    };

    __declspec(align(16)) struct ViewBufferData {
        math::float4x4 viewProj[2];
        math::float4x4 invViewProj[2];
        math::float4 viewPos[2];
        math::float4 viewDir[2];
        math::float4 viewUp[2];
        math::uint4 viewport; // x,y width/height of the viewport
        math::float4 nearFar; // x = near, y = far, ZW = unused
        math::float4 sourceInfo; // Width, Height, InvWidth, InvHeight - not reliably present
        math::float4 destInfo; // Width, Height, InvWidth, InvHeight - not reliably present
    };

    // 0 = left eye, 1 = right eye, 2 = combined view
    _declspec(align(16)) struct VRViewBufferData {
        math::float4x4 viewProj[3];
        math::float4x4 invViewProj[3];
        math::float4 viewPos[3];
        math::float4 viewDir[3];
        math::float4 viewUp[3];
        math::uint4 viewport; // x,y width/height of the viewport
        math::float4 nearFar; // x = near, y = far, ZW = unused
        math::float4 sourceInfo; // Width, Height, InvWidth, InvHeight - not reliably present
        math::float4 destInfo; // Width, Height, InvWidth, InvHeight - not reliably present
    };

    __declspec(align(16)) struct ZoneData
    {
        math::float4 ambient;
        math::float3 envMapPos;
        int envMapIndex;
        math::float3 envMapBoxSize;
        int pad0;
    };
    
    __declspec(align(16)) struct DirectionalLightData
    {
        math::float4x4 cascadeViews[2]; //view-projection matrix
        math::float4 lightPos;
        math::float4 lightDir;
        math::float4 color;
        math::float4 shadowMapCoords[2];
    };

    __declspec(align(16)) struct LightData
    {
        math::float4x4 lightMat; //view-projection matrix
        math::float4 lightPos; // XYZ, W = type
        math::float4 lightDir; // W SPOT/POINT: radius, spot-centroid = lightPos.xyz + lightDir.xyz * radius, infalte radius by 1.2
        math::float4 color;    // RGB = color, A = blending sign (positive is additive, negative is subtract)
        math::float4 extraParams; // X = spotlight FOV, Y = casts shadows, Z = gobo index, W = ramp index
        math::float4 shadowMapCoords[2];// XY = min, ZW = max, SIZE = ZW - XY
    };

    __declspec(align(16)) struct TinyLightData
    {
        math::float4 lightPosType; // XYZ = pos, W = type ID
        math::float4 lightDirRadius; // XYZ = dir, W = radius
        math::float4 color;         
        math::float4 fovGobo; // X = fov, Y = gobo-index, Z = transform index
    };

    struct LightCellData
    {
        uint32_t lightCount;
        uint32_t lightIndices[7];
    };
}