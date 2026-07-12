#if defined(VULKAN)
    #define layout_tex(SLOT) layout(set = 1, location = SLOT)
    #define layout_buffer(SLOT) layout(set = 0, location = SLOT)
    #define GetTexCoord(C) (C)
#else
    #define layout_tex(SLOT) layout(location = SLOT)
    #define layout_buffer(SLOT) layout(location = SLOT)
    #define GetTexCoord(C) vec2((C).x, 1.0 - (C).y)
#endif

layout(location = 0) in vec3 pos;
layout(location = 1) in vec2 inTex;
layout(location = 0) out vec2 screenCoord;

layout(binding = 3) uniform ViewData {
    mat4 viewMat;
};

void main()
{
    screenCoord = vec2(inTex.x, 1.0 - inTex.y);
    gl_Position = viewMat * vec4(pos.x, pos.y, 0, 1);
}