#include "GLFWApp.h"

#include <glvu.h>
#include <GraphicsDevice.h>
#include <Renderer.h>

#include "TimingSystem.h"
#include "DearImgui.h"
#include "InputSystem.h"

#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>

#include <fstream>

#undef max
#undef min

namespace GLVU
{

std::shared_ptr<TimingSystem> GLFWApp::GetTiming() { return timing_; }
std::shared_ptr<InputSystem> GLFWApp::GetInput() { return input_; }
std::shared_ptr<DearImgui> GLFWApp::GetGUI() { return gui_; }

GLFWApp::GLFWApp(uint32_t width, uint32_t height, bool fullscreen, bool hiSpec)
{
    const uint32_t atlasSize = hiSpec ? 4096 : 2048;

    glfwInit();

    device_.reset(new GraphicsDevice());
    device_->SetCallbacks(
        // File input callback
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
            std::cout << "info: " << msg << std::endl;
            break;
        case 1:
            std::cout << "warning: " << std::endl << "    " << msg << std::endl;
            break;
        case 2:
            std::cout << "error: " << std::endl << "    " << msg << std::endl;
            break;
        }
    });

#if defined(GLVU_VK)
    // Opening the device will create a context/instance
    uint32_t instanceExtensionCount = 0U;
    auto instanceExtensions = glfwGetRequiredInstanceExtensions(&instanceExtensionCount);
    device_->OpenDevice(instanceExtensions, instanceExtensionCount);

    VkDebugUtilsMessengerCreateInfoEXT msgInfo = {};
    msgInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
    msgInfo.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    msgInfo.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
    msgInfo.pfnUserCallback = [](VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity, VkDebugUtilsMessageTypeFlagsEXT messageType, const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData, void* pUserData) -> VkBool32 {

        std::cerr << "validation layer: " << pCallbackData->pMessage << std::endl;
        return VK_TRUE;
    };
    msgInfo.pUserData = nullptr; // Optional

    auto func = (PFN_vkCreateDebugUtilsMessengerEXT)glfwGetInstanceProcAddress(device_->GetVKInstance(), "vkCreateDebugUtilsMessengerEXT");
    VkDebugUtilsMessengerEXT msg;
    if (func != nullptr)
    {
        VkResult r = func(device_->GetVKInstance(), &msgInfo, nullptr, &msg);
        if (r != VK_SUCCESS)
            device_->LogMessage("Failed to setup logging", GLVU_WARNING);
    }

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);

    auto primaryMonitor = glfwGetPrimaryMonitor();
    int monWidth, monHeight;
    glfwGetMonitorPhysicalSize(primaryMonitor, &monWidth, &monHeight);
    
    const uint32_t screenWidth = fullscreen ? monWidth : width;
    const uint32_t screenHeight = fullscreen ? monHeight : height;
    
    window_ = glfwCreateWindow(screenWidth, screenHeight, "GLVU Example", fullscreen ? primaryMonitor : 0, nullptr);

    // Now we can initialize our surface
    glfwWindowHint(GLFW_DOUBLEBUFFER, false);
    glfwCreateWindowSurface(device_->GetVKInstance(), window_, nullptr, &surface_);
    device_->InitSurface(800, 600, surface_);
#elif defined(GLVU_DX11)
	glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);

	auto primaryMonitor = glfwGetPrimaryMonitor();
	int monWidth, monHeight;
	glfwGetMonitorPhysicalSize(primaryMonitor, &monWidth, &monHeight);

	const uint32_t screenWidth = fullscreen ? monWidth : width;
	const uint32_t screenHeight = fullscreen ? monHeight : height;

	window_ = glfwCreateWindow(screenWidth, screenHeight, "GLVU Example", fullscreen ? primaryMonitor : nullptr, nullptr);
	glfwMakeContextCurrent(window_);
	device_->OpenDevice(0, 0);
	device_->InitSurface(screenWidth, screenHeight, glfwGetWin32Window(window_));
#else
    //glfwSwapInterval(1);
    glfwWindowHint(GLFW_DOUBLEBUFFER, GL_FALSE);

    auto primaryMonitor = glfwGetPrimaryMonitor();
    int monWidth, monHeight;
    glfwGetMonitorPhysicalSize(primaryMonitor, &monWidth, &monHeight);

    const uint32_t screenWidth = fullscreen ? monWidth : width;
    const uint32_t screenHeight = fullscreen ? monHeight : height;

    window_ = glfwCreateWindow(screenWidth, screenHeight, "GLVU Example", fullscreen ? primaryMonitor : nullptr, nullptr);
    glfwMakeContextCurrent(window_);
    device_->OpenDevice(0, 0);
    device_->InitSurface(screenWidth, screenHeight);
#endif

    renderer_.reset(new Renderer(device_.get(), atlasSize, atlasSize));

    timing_.reset(new TimingSystem());
    input_.reset(new InputSystem());
    input_->RegisterWindow(window_);
    gui_.reset(new DearImgui(window_, device_.get(), renderer_.get()));

    device_->RegisterCallback("DrawGUI", [](RenderScript* caller, View* view, float* params, uint32_t numParams, void* userData) {
        GLFWApp* app = (GLFWApp*)userData;
        app->GetGUI()->Render(view, caller);
    }, this);
}

GLFWApp::~GLFWApp()
{

}

void GLFWApp::Prepare()
{
    timing_->SetFrameUpdate([](double time, uint64_t frame, void* userData) {
        GLFWApp* app = (GLFWApp*)userData;
        
        app->GetInput()->Update((float)time);
        app->GetGUI()->Update((float)time);

        int w, h;
        glfwGetWindowSize(app->GetWindow(), &w, &h);

        app->GetDevice()->OnResize(std::max(1, w), std::max(1, h));
        app->GetRenderer()->Execute();

        app->GetInput()->PostUpdate();

    }, this);
}

void GLFWApp::RunLoop()
{
    while (!glfwWindowShouldClose(window_))
    {
        RunFrame();
    }
}

void GLFWApp::RunFrame()
{
    glfwPollEvents();

    device_->BeginFrame();

    int w, h;
    glfwGetFramebufferSize(window_, &w, &h);
    device_->OnResize(w, h);

    auto views = renderer_->GetViews();
    for (auto& v : views)
    {
        if (v->IsRoot())
        {
            v->renderTarget_ = device_->GetBackbuffer();
            v->viewport_ = uint4(0, 0, w, h);
        }
    }

    timing_->Update();

    device_->EndFrame();

#if defined(GLVU_GL)
    glfwSwapBuffers(window_);
#endif
}

}