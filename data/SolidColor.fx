effect
{
    instance = true;
    
    sampler colorTexture {
        
    }
    
    pass GBuffer
    {
        cull = front;
        vs = "GBuffer.vert";
        ps = "GBuffer.frag";
    }
    
    pass Diffuse
    {
        cull = front;
        vs = "SolidColor.vert";
        ps = "SolidColor.frag";
    }
    
    pass SHADOW_POINTLIGHT
    {
        cull = front;
        depth_test = true;
        depth_write = true;
        vs = "ShaderLib/Shadow.vert";
        vs_defines = "POINTLIGHT";
        ps = "ShaderLib/Shadow.frag";
    }
    
    pass SHADOW_SPOTLIGHT
    {
        cull = front;
        depth_test = true;
        depth_write = true;
        vs = "ShaderLib/Shadow.vert";
        vs_defines = "SPOTLIGHT";
        ps = "ShaderLib/Shadow.frag";
    }
    
    pass SHADOW_DIRLIGHT
    {
        cull = front;
        depth_test = true;
        depth_write = true;
        vs = "ShaderLib/Shadow.vert";
        vs_defines = "DIRLIGHT";
        ps = "ShaderLib/Shadow.frag";
    }
}