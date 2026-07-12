#pragma once

#include <glvu.h>
#include <Renderables.h>

#include <vector>

struct GameWorld
{
    std::vector< std::shared_ptr<GLVU::StaticModel> > incoming_;
    std::shared_ptr<GLVU::StaticModel> playerShip_;

    void Load();
    void Update(double time);
};