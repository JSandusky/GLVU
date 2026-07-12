effect DeferredLight
{
    pass POINT_LIGHT
    {
        blend = add;
        depth_test = true;
        depth_write = false;
    
        vs = "DeferredLight.vs";
        vs_defines = "POINT_LIGHT PREPASS";
        
        ps = "DeferredLight.ps";
        ps_defines = "POINT_LIGHT PREPASS";
    }

    pass SPOT_LIGHT
    {
        blend = add;
        depth_test = true;
        depth_write = false;
    
        vs = "DeferredLight.vs";
        vs_defines = "POINT_LIGHT PREPASS";
        
        ps = "DeferredLight.ps";
        ps_defines = "POINT_LIGHT PREPASS";
    }
    
    pass DIRECTIONAL_LIGHT
    {
        blend = add;
        depth_test = true;
        depth_write = false;
    
        vs = "DeferredLight.vs";
        vs_defines = "DIRECTIONAL_LIGHT PREPASS";
        
        ps = "DeferredLight.ps";
        ps_defines = "DIRECTIONAL_LIGHT PREPASS";
    }
}