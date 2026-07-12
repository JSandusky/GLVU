#include "ShaderLib/ShaderCommon.inc"

layout(location = 0) in vec3 pos;
layout(location = 1) in vec3 norm;
layout(location = 2) in vec2 inTex;
#if defined(INSTANCED)
    layout(location = 3) in vec4 instTrans0;
    layout(location = 4) in vec4 instTrans1;
    layout(location = 5) in vec4 instTrans2;
    layout(location = 6) in vec4 instTrans3;
#elif defined(SKINNED)
    in vec4 boneWeights;
    in uint4 boneIndices;
#endif

layout_buffer(1) uniform ViewData {
    mat4 viewMat;
    mat4 viewProjMat;
};

OBJECT_DATA uniform ObjectData {
    mat4 per_inst(objectTrans);
};

layout_buffer(0) uniform MaterialData {
    vec4 color;
};

layout(location = 0) out vec2 uvCoord;
layout(location = 1) out vec3 normal;

void main()
{
    //#if defined(INSTANCED)
    //    mat4 objectMat = transpose(mat4(instTrans0, instTrans1, instTrans2, instTrans3));
    //    mat4 mvp = transpose(viewProjMat) * objectMat;
    //#else
    //    mat4 mvp = transpose(viewProjMat) * transpose(objectTrans);
    //    mat4 objectMat = transpose(objectTrans);
    //#endif
    //gl_Position = mvp * vec4(pos, 1);
    
    #if defined(INSTANCED)
        mat4 objectMat = mat4(instTrans0, instTrans1, instTrans2, instTrans3);
        mat4 mvp = objectMat * viewProjMat;
    #else
        mat4 mvp = objectTrans * viewProjMat;
        mat4 objectMat = objectTrans;
    #endif
    gl_Position = vec4(pos, 1) * mvp;
    
    normal = norm * mat3(objectMat);
    uvCoord = inTex;
}