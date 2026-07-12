#include "ShaderLib/ShaderCommon.inc"

#include "ShaderLib/Uniforms.inc"

layout(location = 0) in vec3 inPos;
layout(location = 1) in vec3 inNorm;
layout(location = 2) in vec2 inTex;

layout(location = 0) out vec4 posTarget;
layout(location = 1) out vec4 normTarget;

void main()
{
    posTarget = vec4(inPos, 1);
    normTarget = vec4(inNorm * 0.5 + 0.5, 1);
}