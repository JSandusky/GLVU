#pragma once

#include <Buffer.h>
#include <Texture.h>

#include <GLFW/glfw3.h>
#include <dearimgui/imgui.h>

namespace GLVU
{

class GraphicsDevice;
class Renderer;
class RenderScript;
class View;

typedef void (*IMGUI_RENDER_FUNC)();

class DearImgui
{
public:
    DearImgui(GLFWwindow* window, GLVU::GraphicsDevice* device, Renderer* renderer);
    virtual ~DearImgui();

    void Update(float td);
    void Render(View*, RenderScript*);

    void SetCallback(IMGUI_RENDER_FUNC func) { callback_ = func; }

private:
    std::shared_ptr<Buffer> GetBuffer(bool idx);

    ImGuiContext* context_;
    GLVU::GraphicsDevice* device_;
    Renderer* renderer_;
    IMGUI_RENDER_FUNC callback_;
    GLFWwindow* window_;

    std::vector< std::shared_ptr<Buffer> > vertexBuffers_;
    std::vector< std::shared_ptr<Buffer> > indexBuffers_;

    std::shared_ptr<Texture> fontTexture_;

    GLFWcursor* defaultCursor_;
    GLFWcursor* beamCursor_;
    GLFWcursor* handCursor_;
    GLFWcursor* resizeHCursor_;
    GLFWcursor* resizeVCursor_;

    uint32_t startVtx_, startIdx_;
};

}