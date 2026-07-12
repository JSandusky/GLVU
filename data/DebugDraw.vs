#define MAX_INSTANCES 1000
#if defined(VULKAN)
    #define layout_tex(SLOT) layout(set = 1, location = SLOT)
    #define layout_buffer(SLOT) layout(set = 0, location = SLOT)
    #define GetTexCoord(C) (C)
    #if defined(INSTANCED)
        #define OBJECT_DATA layout(binding = 2)
        #define per_inst(NAME) NAME[MAX_INSTANCES]
    #else
        #define OBJECT_DATA layout(push_constant)
        #define per_inst(NAME) NAME
    #endif
#else
    #define layout_tex(SLOT) layout(location = SLOT)
    #define layout_buffer(SLOT) layout(location = SLOT)
    #define GetTexCoord(C) vec2((C).x, 1.0 - (C).y)
    #define OBJECT_DATA layout(binding = 2)
    #if defined(INSTANCED)
        #define per_inst(NAME) NAME[MAX_INSTANCES]
    #else
        #define per_inst(NAME) NAME
    #endif
#endif

layout(location = 0) in vec3 pos;
layout(location = 1) in vec4 inColor;

layout(binding = 1) uniform ViewData {
    mat4 viewMat;
    mat4 invViewMat;
};

OBJECT_DATA uniform ObjectData {
    mat4 per_inst(objectTrans);
};

layout(location = 0) out vec4 oColor;

void main()
{
    #if defined(INSTANCED)
        mat4 mvp = transpose(invViewMat) * transpose(objectTrans[gl_InstanceID]);
    #else
        mat4 mvp = transpose(invViewMat) * transpose(objectTrans);
    #endif
    gl_Position = mvp * vec4(pos, 1);
    oColor = inColor;
}