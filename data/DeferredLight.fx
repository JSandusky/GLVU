effect DeferredLight
{
    pass POINT_LIGHT
    {
        blend = add;
        depth_test = true;
        depth_write = false;
    
        vs = "DeferredLight.vs";
        vs_defines = "POINT_LIGHT";
        
        ps = "DeferredLight.ps";
        ps_defines = "POINT_LIGHT";
    }

    pass SPOT_LIGHT
    {
        blend = add;
        depth_test = true;
        depth_write = false;
    
        vs = "DeferredLight.vs";
        vs_defines = "POINT_LIGHT";
        
        ps = "DeferredLight.ps";
        ps_defines = "POINT_LIGHT";
    }
    
    pass DIRECTIONAL_LIGHT
    {
        blend = add;
        depth_test = true;
        depth_write = false;
    
        vs = "DeferredLight.vs";
        vs_defines = "DIRECTIONAL_LIGHT";
        
        ps = "DeferredLight.ps";
        ps_defines = "DIRECTIONAL_LIGHT";
    }
}