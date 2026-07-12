#include "ShaderLib/ShaderCommon.inc"

layout(location = 0) out float fragColor;

layout(location = 0) in float outDepth;

void main()
{
    if (outDepth < 0)
        discard;
        
    fragColor = outDepth;
}