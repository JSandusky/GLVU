#include "ShaderLib/ShaderCommon.inc"

layout(location = 0) in vec3 position;

layout_buffer(1) uniform ViewData {
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

void main()
{
#if defined(DIRECTIONAL_LIGHT)
    gl_Position = vec4(position.x, position.y, 0, 1) * viewProjMat;
#else
    mat4 mvp = transpose(viewProjMat) * transpose(objectTrans);
    gl_Position = mvp * vec4(position, 1);
#endif
}