render_script
{

    stage ShadowClear
    {
        enabled = false;
        clear {
            color = "0 0 0 0";
            depth = 1;
        }
    }

    stage ShadowStage
    {    
        enabled = false;
        pass Shadow
        {
            context = "shadow";
            sort = "FrontToBack";
        }
    
    }

}