#include "ShaderLib/ShaderCommon.inc"

#include "ShaderLib/Uniforms.inc"

layout(location = 0) in vec3 inPos;
layout(location = 1) in vec3 inNorm;
layout(location = 2) in vec2 inTex;

#if defined(INSTANCED)
    layout(location = 3) in vec4 instTrans0;
    layout(location = 4) in vec4 instTrans1;
    layout(location = 5) in vec4 instTrans2;
    layout(location = 6) in vec4 instTrans3;
#elif defined(SKINNED)
    layout(location = 3) in vec4 boneWeights;
    layout(location = 4) in uint4 boneIndices;
#endif

layout(location = 0) out vec3 outPos;
layout(location = 1) out vec3 outNorm;
layout(location = 2) out vec2 outTex;

void main()
{
    #if defined(INSTANCED)
        mat4 mvp = transpose(viewProjMat) * transpose(mat4(instTrans0, instTrans1, instTrans2, instTrans3));
        mat4 localMat = transpose(mat4(instTrans0, instTrans1, instTrans2, instTrans3));
    #else
        mat4 mvp = transpose(viewProjMat) * transpose(objectTrans);
        mat4 localMat = transpose(objectTrans);
    #endif
    
    gl_Position = mvp * vec4(inPos, 1.0);
    
    outPos = (localMat * vec4(inPos, 1.0)).xyz;
    outNorm = mat3(localMat) * inNorm;
    outTex = inTex;
}