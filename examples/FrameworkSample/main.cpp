
#include "FrameworkApp.h"
#include "TimingSystem.h"

#include "GameWorld.h"
#include "GameLogic.h"

#include <Renderer.h>
#include <RenderScript.h>
#include <Texture.h>
#include <Material.h>
#include <Scene.h>

using namespace GLVU;

//int WinMain()
int main()
{

    GLVU::GLFWApp* app = new GLVU::GLFWApp(800, 600, false, true);
    app->Prepare();
    
    GameWorld world;
    app->GetTiming()->SetFixedUpdate(1.0 / 60.0, [](double time, uint64_t frame, void* userData) {
        GameWorld* world = (GameWorld*)userData;
        world->Update(time);
    }, &world);


    auto res = RenderScript::Load(app->GetDevice(), "basic_script.rs");

    auto texture = Texture::LoadFile(app->GetDevice(), "HeightMap.png");

    auto material = Material::Load(app->GetDevice(), "SolidColor.mat");
    auto model = StaticModel::Load(app->GetDevice(), "data/Box.obj");
    auto teaModel = StaticModel::Load(app->GetDevice(), "data/TeaPot.obj");
    auto bunnyModel = StaticModel::Load(app->GetDevice(), "data/Bunny.obj");

    model->GetMaterials().push_back(material);
    teaModel->GetMaterials().push_back(material);
    bunnyModel->GetMaterials().push_back(material);

    Camera* camera = new Camera(app->GetDevice());
    float aspect = (800.0f / 600.0f);
    camera->SetViewport({ 0,0,800,600 });
    camera->SetPosition({ 0, 2, -2 });
    camera->SetPerspective(90, 0.01f, 1000);

    OctreeScene* scene = new OctreeScene(6, AABB({ -500, -500, -500 }, { 500, 500, 500 }));
    scene->Insert(model);

    std::shared_ptr<View> v(new View(camera, res.get(), app->GetDevice()->GetBackbuffer()));
    v->SetFlag(ViewFlag_IsRoot);
    v->viewport_ = uint4(0, 0, 800, 600);
    v->scene_ = scene;
    app->GetRenderer()->AddView(v);

    app->RunLoop();

    return 0;
}