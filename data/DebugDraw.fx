effect {
    
    pass Debug {
        blend = add;
        vs = "DebugDraw.vs";
        ps = "DebugDraw.fs";
        depth_test = false;
        depth_write = false;
    }

}