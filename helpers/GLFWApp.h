#pragma once

#if defined(GLVU_VK)
    #include <vulkan/vulkan.h>
#endif

#include <memory>

struct GLFWwindow;

namespace GLVU
{

class GraphicsDevice;
class Renderer;

class TimingSystem;
class InputSystem;
class DearImgui;

class GLFWApp
{
public:
    GLFWApp(uint32_t width, uint32_t height, bool fullscreen, bool hiSpec);
    virtual ~GLFWApp();

    void Prepare();
    void RunLoop();

    GLFWwindow* GetWindow() { return window_; }
    GraphicsDevice* GetDevice() { return device_.get(); }
    Renderer* GetRenderer() { return renderer_.get(); }

    std::shared_ptr<TimingSystem> GetTiming();
    std::shared_ptr<InputSystem> GetInput();
    std::shared_ptr<DearImgui> GetGUI();

protected:
    void RunFrame();

    std::unique_ptr<GraphicsDevice> device_;
    std::unique_ptr<Renderer> renderer_;
    std::shared_ptr<TimingSystem> timing_;
    std::shared_ptr<InputSystem> input_;
    std::shared_ptr<DearImgui> gui_;
    GLFWwindow* window_;
#if defined(GLVU_VK)
    VkSurfaceKHR surface_;
#endif
};

}