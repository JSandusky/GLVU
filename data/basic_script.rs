render_script
{
    target GBufferCombine {
        format = rgba8;
    }
    
    target SpareDepth {
        format = DEPTH;
    }

    target GBufferColor {
        format = rgba8;
    }

    target GBufferNormals {
        format = rgba8;
    }
        
    target GBufferWorldPos {
        format = RGBA16F;
    }
    
    target GBufferDepth {
        format = DEPTH;
    }

    stage RenderGBuffer
    {
        targets {
            GBufferWorldPos,
            GBufferNormals,
            GBufferDepth
        }
        
        clear { 
            color = "0 1 0 1";
            stencil = 0;
            depth = 1;
        }
        
        pass GBuffer {
            context = GBuffer;
        }
    }
    
    stage LightingStage
    {
        targets {
            GBufferCombine,
            SpareDepth
        }
        
        clear {
            color = "1 0 0 1";
            stencil = 0;
            depth = 1;
        }
        
        bind_targets {
            GBufferWorldPos = 0;
            GBufferNormals = 1;
            GBufferColor = 2;
            GBufferDepth = 3;
            SHADOWMAP = 15;
        }
        
        quad DrawTex {
            shader = "FSShader.ps";
            texture 0 = "HeightMap.png";
        }
        
        pass SolidColor {
            context = Diffuse;
        }
        
        lighting_pass DeferredLights
        {
            method = volumes;
        }
    }
    
    //stage Transparents
    //{
    //    targets {
    //        GBufferCombine,
    //        GBufferDepth
    //    }
    //    
    //    bind_targets {
    //        SpareDepth = 13;
    //    }
    //    
    //    pass Alpha {
    //        context = ALPHA;
    //        sort = FrontToBack;
    //    }
    //}
    
    stage FXAA 
    {
        bind_targets 
        {
            GBufferCombine = 0;
        }
        
        quad FXAA {
            shader = "ShaderLib/FXAA.glsl";
        }
    }
    
    stage Debug
    {
        pass Debug
        {
            context = Debug;
        }
    }
    
}

