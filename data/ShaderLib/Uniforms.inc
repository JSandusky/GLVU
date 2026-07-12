

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