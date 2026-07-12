#include "ShaderLib/ShaderCommon.inc"

in vec3 pos;
in vec3 norm;
in vec2 inTex;
#if defined(INSTANCED)
    layout(location = 3) in vec4 instTrans0;
    layout(location = 4) in vec4 instTrans1;
    layout(location = 5) in vec4 instTrans2;
    layout(location = 6) in vec4 instTrans3;
#elif defined(SKINNED)
    layout(location = 3) in vec4 boneWeights;
    layout(location = 4) in uint4 boneIndices;
#endif

layout(binding = 1) uniform ViewData {
    mat4 viewMat;
    mat4 viewProjMat;
    vec4 viewPos;
    vec4 viewDir;
    vec4 viewUp;
    uvec4 viewport;
    vec4 nearFar;
};

OBJECT_DATA uniform ObjectData {
    mat4 per_inst(objectTrans);
};

layout(binding = 0) uniform MaterialData {
    vec4 color;
};

layout(location = 0) out float outDepth;

void main()
{
    #if defined(POINTLIGHT)
        #if defined(INSTANCED)
            mat4 mvp = transpose(viewMat) * transpose(mat4(instTrans0, instTrans1, instTrans2, instTrans3));
        #else                        
            mat4 mvp = transpose(viewMat) * transpose(objectTrans);
        #endif
    #else
        #if defined(INSTANCED)
            mat4 mvp = transpose(viewProjMat) * transpose(mat4(instTrans0, instTrans1, instTrans2, instTrans3));
        #else
            mat4 mvp = transpose(viewProjMat) * transpose(objectTrans);
        #endif
    #endif
    
    #if defined(POINTLIGHT)    
        gl_Position = mvp * vec4(pos, 1);
        highp vec3 vtx = gl_Position.xyz;
        highp float distance = length(vtx);
        
        vtx = normalize(vtx);
        vtx.xy /= 1.0 - vtx.z;
        vtx.z = (distance / nearFar.y);
        
        #if defined(VULKAN)
            vtx.y *= -1;
        #endif
        
        gl_Position.xyz = vtx;
        outDepth = vtx.z / gl_Position.w;
        #if defined(VULKAN)
            outDepth = vtx.z;
        #endif
        
        #if defined(OPENGL)
            gl_Position.z = gl_Position.z * 2.0 - gl_Position.w;
        #endif
    #else
        gl_Position = mvp * vec4(pos, 1);
        gl_Position.z += 0.0001;
        outDepth = gl_Position.z / nearFar.y;
        #if defined(OPENGL)
            gl_Position.z = gl_Position.z * 2.0 - gl_Position.w;
        #endif
    #endif
}