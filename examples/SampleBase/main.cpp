
#if defined(GLVU_VK)
    #define GLFW_INCLUDE_VULKAN
#endif

#include <glvu.h>

#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>

#include <Effect.h>
#include <GraphicsDevice.h>
#include <GraphicsDeviceHead.h>
#include <RenderScript.h>
#include <Renderer.h>
#include <Renderables.h>
#include <LightShadow.h>
#include <Scene.h>
#include <ECS.h>

#include <MathGeoLib/MathGeoLib.h>

#include <fstream>
#include <iomanip>
#include <iostream>
#include <thread>
#include <mutex>

#include <windows.h>

void showFPS(GLFWwindow *pWindow);

inline std::ostream& blue(std::ostream &s)
{
    HANDLE hStdout = GetStdHandle(STD_OUTPUT_HANDLE);
    SetConsoleTextAttribute(hStdout, FOREGROUND_BLUE
        | FOREGROUND_GREEN | FOREGROUND_INTENSITY);
    return s;
}

inline std::ostream& red(std::ostream &s)
{
    HANDLE hStdout = GetStdHandle(STD_OUTPUT_HANDLE);
    SetConsoleTextAttribute(hStdout,
        FOREGROUND_RED | FOREGROUND_INTENSITY);
    return s;
}

inline std::ostream& green(std::ostream &s)
{
    HANDLE hStdout = GetStdHandle(STD_OUTPUT_HANDLE);
    SetConsoleTextAttribute(hStdout,
        FOREGROUND_GREEN | FOREGROUND_INTENSITY);
    return s;
}

inline std::ostream& yellow(std::ostream &s)
{
    HANDLE hStdout = GetStdHandle(STD_OUTPUT_HANDLE);
    SetConsoleTextAttribute(hStdout,
        FOREGROUND_GREEN | FOREGROUND_RED | FOREGROUND_INTENSITY);
    return s;
}

inline std::ostream& white(std::ostream &s)
{
    HANDLE hStdout = GetStdHandle(STD_OUTPUT_HANDLE);
    SetConsoleTextAttribute(hStdout,
        FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
    return s;
}

using namespace GLVU;

GraphicsDevice* device;

struct Job {
    GFX_THREAD_QUEUE_CALLBACK call = nullptr;
    uint32_t taskID = 0;
    void* userData = nullptr;
    bool taken = false;

    bool operator==(const Job& rhs) const {
        return call == rhs.call && taskID == rhs.taskID && userData == rhs.userData;
    }
};

std::vector<Job> jobs_;
std::mutex mutex_;

void thread_func() 
{
    while (true)
    {
        Job job;
        job.taken = false;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (jobs_.size() > 0)
            {
                for (auto& j : jobs_)
                    if (!j.taken)
                    {
                        j.taken = true;
                        job = j;
                        break;
                    }
            }
        }

        if (!job.taken)
            continue;

        if (job.call)
            job.call(job.taskID, job.userData);

        {
            std::lock_guard<std::mutex> lock(mutex_);
            auto found = std::find(jobs_.begin(), jobs_.end(), job);
            assert(found != jobs_.end());
            jobs_.erase(found);
        }
    }
}

int main()
{
    //std::thread thread1(thread_func);
    //thread1.detach();
    //std::thread thread2(thread_func);
    //thread2.detach();
    //std::thread thread3(thread_func);
    //thread3.detach();
    //std::thread thread4(thread_func);
    //thread4.detach();

    //Antlia::ECS_Test();

    // Create a device and then set our 2 required callbacks.
    //      threading callbacks are not required (internals will fallback to unthreaded rendering)
    device = new GraphicsDevice();
    device->SetCallbacks(
    // File input callback, with this we can reroute file lookups
    [](ResourceKind kind, const char* fileName) -> Blob {

        std::fstream str;
		std::string openPath = "data/";
		if (kind == Resource_Effect) // Pull all effects relative to "data/Effects/", shader system automatically injects "HLSL/" and "GLSL/" prefixes.
			openPath += "Effects/";

        str.open(openPath + std::string(fileName), std::ios::binary | std::ios::in);
        if (str.is_open())
        {
            str.seekg(0, str.end);
            auto len = str.tellg();
            str.seekg(0, str.beg);
            std::unique_ptr<char[]> data(new char[len]);
            str.read(data.get(), len);
            return Blob(data.get(), len, true);
        }

        return Blob(nullptr, 0, false);
    },
    // Logging callback
    [](const char* msg, int level) {
        switch (level)
        {
        case 0:
            std::cout << white << "info: " << msg << std::endl << white;
            break;
        case 1:
            std::cout << yellow << "warning: " << std::endl << "    " << msg << std::endl << white;
            break;
        case 2:
            std::cout << red << "error: " << std::endl << "    " << msg << std::endl << white;
            break;
        }
    });
    //device->SetThreading(3, [](GFX_THREAD_QUEUE_CALLBACK call, uint32_t taskID, void* userData) {
    //    mutex_.lock();
    //    jobs_.push_back({ call, taskID, userData, false });
    //    mutex_.unlock();
    //}, []() {
    //    bool empty = false;
    //    do {
    //        {
    //            std::lock_guard<std::mutex> lock(mutex_);
    //            empty = jobs_.empty();
    //        }
    //    } while (!empty);
    //});

    int windowWidth = 800;
    int windowHeight = 600;

    static auto mod_floor = [](int x, int k) { return ((x % k) + k) % k; };

    glfwInit();
#if defined(GLVU_VK)
    // Opening the device will create a context/instance
    uint32_t instanceExtensionCount = 0U;
    auto instanceExtensions = glfwGetRequiredInstanceExtensions(&instanceExtensionCount);
    device->OpenDevice(instanceExtensions, instanceExtensionCount);

    VkDebugUtilsMessengerCreateInfoEXT msgInfo = {};
    msgInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
    msgInfo.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    msgInfo.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
    msgInfo.pfnUserCallback = [](VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity, VkDebugUtilsMessageTypeFlagsEXT messageType, const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData, void* pUserData) -> VkBool32 {

        std::cerr << "validation layer: " << pCallbackData->pMessage << std::endl;
        return VK_TRUE;
    };
    msgInfo.pUserData = nullptr; // Optional

    auto func = (PFN_vkCreateDebugUtilsMessengerEXT)glfwGetInstanceProcAddress(device->GetVKInstance(), "vkCreateDebugUtilsMessengerEXT");
    VkDebugUtilsMessengerEXT msg;
    if (func != nullptr)
    {
        VkResult r = func(device->GetVKInstance(), &msgInfo, nullptr, &msg);
        if (r != VK_SUCCESS)
            device->LogMessage("Failed to setup logging", GLVU_WARNING);
    }

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    auto window = glfwCreateWindow(windowWidth, windowHeight, "GLVU Example", nullptr, nullptr);

    // Now we can initialize our surface
    VkSurfaceKHR surface;

    glfwWindowHint(GLFW_DOUBLEBUFFER, false);
    glfwCreateWindowSurface(device->GetVKInstance(), window, nullptr, &surface);
    device->InitSurface(800, 600, surface);
    GraphicsDeviceHead* head = new GraphicsDeviceHead(device, { (unsigned)windowWidth, (unsigned)windowHeight }, surface, 0ull);
#elif defined(GLVU_DX11)
	glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
	auto window = glfwCreateWindow(windowWidth, windowHeight, "GLVU Example", nullptr, nullptr);
	device->OpenDevice(0, 0);
    GraphicsDeviceHead* head = new GraphicsDeviceHead(device, { (unsigned)windowWidth, (unsigned)windowHeight }, glfwGetWin32Window(window), 0ull);
#else
    //glfwSwapInterval(1);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 5);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_DOUBLEBUFFER, false);
    auto window = glfwCreateWindow(windowWidth, windowHeight, "GLVU Example", nullptr, nullptr);
    glfwMakeContextCurrent(window);
    device->OpenDevice(0, 0);
    device->InitSurface(windowWidth, windowHeight);
    GraphicsDeviceHead* head = nullptr;
#endif

    auto res = RenderScript::Load(device, "basic_script.rs");
    auto secondRes = res->Clone();

    auto texture = Texture::LoadFile(device, "HeightMap.png");
    texture->GenerateMipMaps();

    auto material = Material::Load(device, "SolidColor.mat");
    auto model = StaticModel::Load(device, "data/Box.obj");
    auto teaModel = StaticModel::Load(device, "data/TeaPot.obj");
    auto bunnyModel = StaticModel::Load(device, "data/Bunny.obj");

    auto ssbo = device->CreateShaderStorageBuffer();
    ssbo->SetStride(16);
    ssbo->SetSize(64);

    model->GetMaterials().push_back(material);
    teaModel->GetMaterials().push_back(material);
    bunnyModel->GetMaterials().push_back(material);

    model->SetPosition({ 0,0, 2 });

    math::float4x4 m = math::float4x4::FromTRS(math::float3(0,0,1), math::Quat::identity, math::float3(1,1,1));

    float angle = 90.0f;
    auto rotAxis = math::float3(0, 1, 0);
    model->SetRotation(math::Quat(rotAxis, math::DegToRad(90)));
    auto rotatedPt = model->ToLocalSpace({ 0,0,1 });
    auto worldPt = model->ToWorldSpace({ 0,0,1 });
    auto rotatedMat = model->ToLocalSpace(m);
    auto rotatedTrans = rotatedMat.TranslatePart();
    auto worldMat = model->ToWorldSpace(m);
    auto worldTrans = worldMat.TranslatePart();
    model->SetScale({ 1, 1, 1 });

    auto secondModel = model->Clone();
    secondModel->SetPosition({ 0.2f,0, 1.5 });
    secondModel->SetScale({ 6, 10, 6 });

    Scene* scene = new Scene();
    //OctreeScene* scene = new OctreeScene(8, AABB({ -200, -200, -200 }, { 200, 200, 200 }));
    //scene->Insert(model);
    //scene->Insert(secondModel);
    
    std::shared_ptr<InstanceCluster> cluster;
    //std::shared_ptr<InstanceCluster> cluster(new InstanceCluster(device));
    //cluster->geometry_ = model->GetGeometries()[0];
    //cluster->material_ = model->GetMaterials()[0];

    //cluster->SetInstanceCount(250 * 250);

    math::LCG lcg;
    std::vector< std::shared_ptr<InstanceCluster> > clusters;
    for (int y = -20; y < 20; ++y)
    for (int x = -20; x < 20; ++x)
    {
        for (int z = 0; z < 1; ++z)
        {
            if (cluster == nullptr || cluster->GetInstanceCount() >= 25*25)
            {
                if (cluster != nullptr)
                    scene->Insert(cluster);
                cluster.reset(new InstanceCluster(device));
                cluster->geometry_ = model->GetGeometries()[0];
                cluster->material_ = model->GetMaterials()[0];
                clusters.push_back(cluster);
            }
            
            cluster->PushInstancePosition(float3(x * 1.2f, z * -5.0f + lcg.Float(0.0f, 0.5f), y * 1.2f), Quat(float3::unitY, lcg.Float(0.0f, math::pi * 2)));
	
            //auto clone = model->Clone();
            //clone->SetPosition(float3(x * 1.2f, -1 + z * -2, y * 1.2f));
            //scene->Insert(clone);
        }
    }
    if (cluster)
        scene->Insert(cluster);

    Camera* camera = new Camera(device);
    float aspect = (800.0f / 600.0f);
    camera->SetViewport({ 0,0,800,600 });
    camera->SetOrtho(-2, 2, 2, -2, 0, 100.0f);
    camera->SetPosition({ 0, 2, -2 });
    camera->SetPerspective(90, 0.01f, 1000);
    //camera->LookAt({ 0,0,0 });

    std::shared_ptr<DebugGeometry> debugGeo(new DebugGeometry(device));
    camera->DrawDebug(debugGeo.get());
    //scene->DrawDebug(debugGeo.get());
    //scene->Insert(debugGeo);


    float time = (float)glfwGetTime();
    glfwSetTime(time);

    std::shared_ptr<Light> light(new Light(device));
    light->SetKind(Light::POINT);
    light->SetPosition({ -5, 2, 1 });
    light->SetColor({ 1, 0, 0, 1 });
    light->SetRadius(8);
    light->SetShadowMask(0xFF);
    light->SetShadowDim(128);
    scene->Insert(light);

    std::shared_ptr<Light> spotLight(new Light(device));
    spotLight->SetKind(Light::SPOT);
    spotLight->SetPosition({ 2.2f,3,-4 });
    spotLight->SetRadius(7);
    spotLight->Rotate(math::Quat(float4::unitY, DegToRad(90)));
    spotLight->Rotate(math::Quat(float4::unitX, DegToRad(60)), true);
    spotLight->SetColor({ 1.0f, 1.0f, 0, 1 });
    spotLight->SetFOV(45);
    spotLight->SetShadowDim(512);
	spotLight->SetShadowMask(0xFF);
    scene->Insert(spotLight);
    
    LightTiler tiler(device, uint3(16, 1, 32), 4);
    tiler.tileDim_.x = 16;
    tiler.tileDim_.y = 8;
    tiler.tileDim_.z = 1;

    //scene->Insert(light);

    //for (int i = 0; i < 10; ++i)
    //{
    //    std::shared_ptr<Light> light(new Light(device));
    //    light->SetKind(Light::POINT);
    //    light->SetPosition({ -15 + 3.25f * i, 3, -4 });
    //    light->SetColor({ 1, 0, 0, 1 });
    //    light->SetRadius(18);
    //    light->SetShadowMask(0x0);
    //    scene->Insert(light);
    //}

    model->GetTransform();
    secondModel->GetTransform();
    light->GetTransform();
    spotLight->GetTransform();

    Renderer* renderer = new Renderer(device, 1024, 1024);
    debugGeo->UpdateBuffers();

    glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
    double lastMouseX = DBL_MAX, lastMouseY = DBL_MAX;

    std::shared_ptr<Camera> secondCam(new Camera(device));
    secondCam->SetViewport({ 0,0,800,600 });
    secondCam->SetPosition({ 0, 2, -2 });
    secondCam->LookAtDir(float3::unitX);
    secondCam->SetPerspective(90, 0.01f, 1000);

    float lastTime = glfwGetTime();
    time = lastTime;
    while (!glfwWindowShouldClose(window))
    {
        glfwPollEvents();

        //scene->RemoveDeep(light);
        ///light->SetPosition( camera->GetPosition() );
        //scene->Insert(light);

        float t = glfwGetTime();
        time = t - lastTime;
        lastTime = t;        

        debugGeo->Clear();
        
        Camera testCam(device);
        AABB casterBnds; casterBnds.SetNegativeInfinity();
        spotLight->SetupShadowCamera(&testCam, 0, casterBnds);

        debugGeo->AddLine(testCam.GetPosition(), testCam.GetPosition() + testCam.GetDirection() * 3, float4(0, 0, 1, 1), false);
        debugGeo->AddLine(testCam.GetPosition(), testCam.GetPosition() + testCam.GetUp() * 3, float4(0, 1, 0, 1), false);
        debugGeo->AddLine(testCam.GetPosition(), testCam.GetPosition() + testCam.GetRight() * 3, float4(1, 0, 0, 1), false);
        testCam.DrawDebug(debugGeo.get());
        debugGeo->AddTriangle(float3::zero, float3::unitZ * 10, float3::unitX * 10, { 1.0f, 0.0f, 0.0f, 0.5f }, false);
        debugGeo->AddAxis(float4x4::FromTRS(float3(0.0f, 10.0f, 0.0f), Quat(), float3(1, 1, 1)), 5.0f, false);

        //scene->DrawDebug(debugGeo.get());
        //for (auto& cluster : clusters)
        //    cluster->DrawDebug(debugGeo.get());

        showFPS(window);

        double newMouseX = DBL_MAX, newMouseY = DBL_MAX;
        glfwGetCursorPos(window, &newMouseX, &newMouseY);

        float2 mouseDelta = { 0, 0 };
        if (lastMouseX != DBL_MAX && lastMouseY != DBL_MAX)
        {
            mouseDelta = float2(newMouseX - lastMouseX, newMouseY - lastMouseY);
        }
        lastMouseX = newMouseX;
        lastMouseY = newMouseY;

        if (glfwGetKey(window, GLFW_KEY_LEFT_SHIFT))
            time *= 20;
        if (glfwGetKey(window, GLFW_KEY_LEFT_CONTROL))
            time *= 20;

        if (glfwGetKey(window, GLFW_KEY_W))
            camera->SetPosition(camera->GetPosition() + camera->GetDirection() * time);
        else if (glfwGetKey(window, GLFW_KEY_S))
            camera->SetPosition(camera->GetPosition() + camera->GetDirection() * -time);
        if (glfwGetKey(window, GLFW_KEY_D))
            camera->SetPosition(camera->GetPosition() + camera->GetRight() * time);
        else if (glfwGetKey(window, GLFW_KEY_A))
            camera->SetPosition(camera->GetPosition() + camera->GetRight() * -time);
        if (glfwGetKey(window, GLFW_KEY_Q))
            camera->SetPosition(camera->GetPosition() + camera->GetUp() * time);
        else if (glfwGetKey(window, GLFW_KEY_E))
            camera->SetPosition(camera->GetPosition() + camera->GetUp() * -time);

        if (mouseDelta.x != 0.0f)
            camera->Rotate(Quat(float3::unitY, DegToRad(mouseDelta.x * 0.01f)));
        if (mouseDelta.y != 0.0f)
            camera->Rotate(Quat(float3::unitX, DegToRad(mouseDelta.y * 0.01f)), true);

        if (glfwGetKey(window, GLFW_KEY_HOME))
            camera->LookAt(float3::zero);

        if (glfwGetKey(window, GLFW_KEY_Z))
        {
            angle += 0.1f;
            if (angle >= 360.0f)
                angle = 0.0f;
            //model->SetRotation(math::Quat(rotAxis, math::DegToRad(angle)));
            model->RotateAround(math::float3::unitZ, math::Quat(rotAxis, math::DegToRad(0.1f)));
        }
        else if (glfwGetKey(window, GLFW_KEY_X))
        {
            angle -= 0.1f;
            if (angle < 0.0f)
                angle = 359.99f;
            model->SetRotation(math::Quat(rotAxis, math::DegToRad(angle)));
        }

        device->BeginFrame();

        int w, h;
        glfwGetFramebufferSize(window, &w, &h);

        //tiler.BuildLightTables(camera, { light, spotLight });
        device->OnResize(w, h);
        if (head)
            head->Resize(w, h);
        camera->SetViewport({ 0,0,(unsigned)w,(unsigned)h});

        std::shared_ptr<View> auxV(new View(secondCam.get(), secondRes.get(), { 0u, 0u, 800, 600 }));
        auxV->scene_ = scene;
        auxV->head_ = head;

        std::shared_ptr<View> v(new View(camera, res.get(), { 0u, 0u, (unsigned)w, (unsigned)h }));
        v->scene_ = scene;
        v->head_ = head;
        v->SetFlag(ViewFlag_IsRoot);
        //v->ClearFlag(ViewFlag_Shadows);

        static float lightZ = 2;
        lightZ += time * 3.1415962 * 0.1f;
        light->SetPosition({ -5, 2, math::PingPongMod(lightZ, 8.0f) });
        debugGeo->AddAxis(light->GetTransform(), 2.0f, false);
        static float spotLightX = 0.0f;
        spotLightX += time * 3;
        spotLight->LookAt(spotLight->GetPosition() + float3(10, -math::PingPongMod(spotLightX, 8.5f), 0.0f));
        debugGeo->AddAxis(spotLight->GetTransform(), 2.0f, false);
        //scene->Remove(light);
        //scene->Remove(spotLight);
        //scene->Insert(light);
        //scene->Insert(spotLight);

        //renderer->Execute(auxV);
        renderer->ClearViews();
        renderer->AddView(v);
        renderer->Execute();

        //device->LogStats();

        device->EndFrame();

#if defined(GLVU_GL)
        glfwSwapBuffers(window);
#endif
        
    }
    
    //device->Shutdown();
    delete device;

    return 0;
}

int nbFrames = 0;
double lastTime = 0.0;
double lastActual = 0.0;
void showFPS(GLFWwindow *pWindow)
{
    // Measure speed
     double currentTime = glfwGetTime();
     double delta = currentTime - lastTime;
     double actualDelta = currentTime - lastActual;
     lastActual = currentTime;
     nbFrames++;
     if ( delta >= 1.0 ){ // If last cout was more than 1 sec ago
         double fps = double(nbFrames) / delta;

         std::stringstream ss;
         ss.imbue(std::locale(""));
         ss << "Example" << " [" << std::fixed << std::setprecision(2) << actualDelta * 1000 << " ms, " << fps
             << " FPS] Batches: " << std::fixed << device->GetStat(GLVU::STAT_BATCHES)
             << " Prim: " << device->GetStat(GLVU::STAT_PRIMITIVES)
             << " Inst: " << device->GetStat(GLVU::STAT_INSTANCES)
             << " Shadow-maps: " << device->GetStat(GLVU::STAT_SHADOWMAPS);

         glfwSetWindowTitle(pWindow, ss.str().c_str());

         nbFrames = 0;
         lastTime = currentTime;
     }
}

void CommonTests(GLVU::GraphicsDevice* device)
{
    // create a white texture
    TextureTraits traits = { };
    traits.width_ = 1;
    traits.height_ = 1;

    auto tex = device->CreateTexture(traits);
    uint32_t texData = 0xFFFFFFFF;
    tex->SetData(&texData, 1, 1, 1, 0, 0);

    const auto elemSize = sizeof(float4);
    auto uavBuffer = device->CreateShaderStorageBuffer();
    uavBuffer->SetStride(elemSize);
    uavBuffer->SetSize(elemSize * 32);

    auto loaded = Texture::LoadFile(device, "HeightMap.png");
    auto loaded2 = Texture::LoadFile(device, "UrhoDecalAlpha.dds");
    loaded->GenerateMipMaps();

    auto lut = Texture::Load3DLUT(device, "LUTIdentity.png");
    auto lutArray = Texture::LoadArrayStrip(device, "LUTIdentity.png");
    auto loadedArray = Texture::LoadFile(device, "data4.dds");

    auto vertBuffer = device->CreateVertexBuffer();
    float vertices[] = {
        0.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f,
        1.0f, 1.0f, 0.0f
    };
    vertBuffer->SetData(vertices, sizeof(float) * 9);

    auto idxBuffer = device->CreateIndexBuffer();
    uint16_t indices[] = { 0, 1, 2 };
    idxBuffer->SetData(indices, sizeof(uint16_t) * 3);

    std::shared_ptr<Geometry> geo(new Geometry());
    geo->vertexBuffers_.push_back(vertBuffer);
    geo->indexBuffer_ = idxBuffer;
    geo->vertexCount_ = 3;
    geo->indexCount_ = 3;
    geo->indexStart_ = 0;
    geo->primCount_ = 1;
    geo->primType_ = TRIANGLE_LIST;
    geo->InferValuesFromData();
}
