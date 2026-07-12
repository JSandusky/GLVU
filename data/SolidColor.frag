#include "ShaderLib/ShaderCommon.inc"

layout(location = 0) out vec4 fragColor;

layout(location = 0) in vec2 uvCoord;
layout(location = 1) in vec3 normal;

layout_tex(0) uniform sampler2D colorTexture;

void main()
{
    fragColor = vec4(normal * 0.5 + 0.5, 1);
}