#define MAX_INSTANCES 1000
#if defined(VULKAN)
    #define layout_tex(SLOT) layout(set = 1, binding = SLOT)
    #define layout_buffer(SLOT) layout(set = 0, binding = SLOT)
    #define GetTexCoord(C) (C)
    #define ConvertClipspace(V) (V)
    #if defined(INSTANCED)
        #define OBJECT_DATA layout(binding = 2)
        #define per_inst(NAME) NAME[MAX_INSTANCES]
    #else
        #define OBJECT_DATA layout(push_constant)
        #define per_inst(NAME) NAME
    #endif
    
    #define INST_ID gl_InstanceIndex
#else
    #define layout_tex(SLOT) layout(binding = SLOT)
    #define layout_buffer(SLOT) layout(binding = SLOT)
    #define GetTexCoord(C) vec2((C).x, 1.0 - (C).y)
    #define OBJECT_DATA layout(binding = 2)
    #define ConvertClipspace(V) vec4(V.x, V.y, V.z * 2.0 - V.w, V.w)
    #if defined(INSTANCED)
        #define per_inst(NAME) NAME[MAX_INSTANCES]
    #else
        #define per_inst(NAME) NAME
    #endif
    
    #define INST_ID gl_InstanceID
#endif

#define BINDING_FULLSCREEN_VIEW_MATRIX 3
#define BINDING_FULLSCREEN_VIEW_BUFFER_INFO 4
#define BINDING_FULLSCREEN_USER_DATA 5