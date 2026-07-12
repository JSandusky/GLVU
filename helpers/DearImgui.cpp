#include "DearImgui.h"

#include <glvu.h>
#include <GraphicsDevice.h>
#include <Renderer.h>
#include <RenderScript.h>

#include "InputSystem.h"

#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>

namespace GLVU
{

static const char* ImGui_ImplGlfw_GetClipboardText(void* user_data)
{
    return glfwGetClipboardString((GLFWwindow*)user_data);
}

static void ImGui_ImplGlfw_SetClipboardText(void* user_data, const char* text)
{
    glfwSetClipboardString((GLFWwindow*)user_data, text);
}

DearImgui::DearImgui(GLFWwindow* window, GraphicsDevice* device, Renderer* renderer) :
    window_(window),
    device_(device),
    renderer_(renderer),
    startVtx_(0), 
    startIdx_(0),
    callback_(nullptr)
{
    context_ = ImGui::CreateContext(nullptr);

    auto& io = ImGui::GetIO();

    io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;

    io.BackendFlags = 0;
    io.BackendFlags |= ImGuiBackendFlags_HasMouseCursors;
    io.BackendFlags |= ImGuiBackendFlags_HasSetMousePos;
    io.BackendPlatformName = "imgui_glvu_glfw";
    io.BackendRendererUserData = this;
    io.RenderDrawListsFn = nullptr;

    io.KeyMap[ImGuiKey_Tab] = GLFW_KEY_TAB;
    io.KeyMap[ImGuiKey_LeftArrow] = GLFW_KEY_LEFT;
    io.KeyMap[ImGuiKey_RightArrow] = GLFW_KEY_RIGHT;
    io.KeyMap[ImGuiKey_UpArrow] = GLFW_KEY_UP;
    io.KeyMap[ImGuiKey_DownArrow] = GLFW_KEY_DOWN;
    io.KeyMap[ImGuiKey_PageUp] = GLFW_KEY_PAGE_UP;
    io.KeyMap[ImGuiKey_PageDown] = GLFW_KEY_PAGE_DOWN;
    io.KeyMap[ImGuiKey_Home] = GLFW_KEY_HOME;
    io.KeyMap[ImGuiKey_End] = GLFW_KEY_END;
    io.KeyMap[ImGuiKey_Insert] = GLFW_KEY_INSERT;
    io.KeyMap[ImGuiKey_Delete] = GLFW_KEY_DELETE;
    io.KeyMap[ImGuiKey_Backspace] = GLFW_KEY_BACKSPACE;
    io.KeyMap[ImGuiKey_Space] = GLFW_KEY_SPACE;
    io.KeyMap[ImGuiKey_Enter] = GLFW_KEY_ENTER;
    io.KeyMap[ImGuiKey_Escape] = GLFW_KEY_ESCAPE;
    io.KeyMap[ImGuiKey_KeyPadEnter] = GLFW_KEY_KP_ENTER;
    io.KeyMap[ImGuiKey_A] = GLFW_KEY_A;
    io.KeyMap[ImGuiKey_C] = GLFW_KEY_C;
    io.KeyMap[ImGuiKey_V] = GLFW_KEY_V;
    io.KeyMap[ImGuiKey_X] = GLFW_KEY_X;
    io.KeyMap[ImGuiKey_Y] = GLFW_KEY_Y;
    io.KeyMap[ImGuiKey_Z] = GLFW_KEY_Z;

    io.SetClipboardTextFn = ImGui_ImplGlfw_SetClipboardText;
    io.GetClipboardTextFn = ImGui_ImplGlfw_GetClipboardText;
    io.ClipboardUserData = nullptr;

    io.Fonts->Clear();
    io.Fonts->AddFontDefault();
    io.Fonts->Build();
    
    unsigned char* pixels;
    int width, height;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);

    TextureTraits fontTexTraits = { };
    fontTexTraits.kind_ = Texture2D;
    fontTexTraits.width_ = width;
    fontTexTraits.height_ = height;
    fontTexTraits.format_ = TEX_RGBA8;
    fontTexture_ = device->CreateTexture(fontTexTraits);
    fontTexture_->SetData(pixels, width, height, 0, 0, 0);

    defaultCursor_ = glfwCreateStandardCursor(GLFW_CURSOR_NORMAL);
    beamCursor_ = glfwCreateStandardCursor(GLFW_IBEAM_CURSOR);
    handCursor_ = glfwCreateStandardCursor(GLFW_HAND_CURSOR);
    resizeHCursor_ = glfwCreateStandardCursor(GLFW_HRESIZE_CURSOR);
    resizeVCursor_ = glfwCreateStandardCursor(GLFW_VRESIZE_CURSOR);
}

DearImgui::~DearImgui()
{
    ImGui::DestroyContext(context_);
    context_ = nullptr;
}

void DearImgui::Update(float td)
{
    ImGui::SetCurrentContext(context_);

    auto input = InputSystem::GetInst();

    auto& io = ImGui::GetIO();
    io.DeltaTime = td;

    io.KeyAlt = input->GetKeyDown(GLFW_KEY_LEFT_ALT) || input->GetKeyDown(GLFW_KEY_RIGHT_ALT);
    io.KeyCtrl = input->GetKeyDown(GLFW_KEY_LEFT_CONTROL) || input->GetKeyDown(GLFW_KEY_RIGHT_CONTROL);
    io.KeyShift = input->GetKeyDown(GLFW_KEY_LEFT_SHIFT) || input->GetKeyDown(GLFW_KEY_RIGHT_SHIFT);

    io.MouseWheel = input->GetWheel() > 0 ? 1 : input->GetWheel() < 0 ? -1 : 0;

    auto mousePos = input->GetMousePosition();
    io.MousePos = { mousePos.x, mousePos.y };

    for (int i = 0; i < 5; ++i)
    {
        io.MouseDown[i] = input->GetMouseDown(i);
    }

#define MAP_KEY(VALUE) io.KeysDown[VALUE] = input->GetKeyDown(VALUE)
    MAP_KEY(GLFW_KEY_TAB);
    MAP_KEY(GLFW_KEY_LEFT);
    MAP_KEY(GLFW_KEY_RIGHT);
    MAP_KEY(GLFW_KEY_UP);
    MAP_KEY(GLFW_KEY_DOWN);
    MAP_KEY(GLFW_KEY_PAGE_UP);
    MAP_KEY(GLFW_KEY_PAGE_DOWN);
    MAP_KEY(GLFW_KEY_HOME);
    MAP_KEY(GLFW_KEY_END);
    MAP_KEY(GLFW_KEY_INSERT);
    MAP_KEY(GLFW_KEY_DELETE);
    MAP_KEY(GLFW_KEY_BACKSPACE);
    MAP_KEY(GLFW_KEY_SPACE);
    MAP_KEY(GLFW_KEY_ENTER);
    MAP_KEY(GLFW_KEY_ESCAPE);
    MAP_KEY(GLFW_KEY_KP_ENTER);
    MAP_KEY(GLFW_KEY_A);
    MAP_KEY(GLFW_KEY_C);
    MAP_KEY(GLFW_KEY_V);
    MAP_KEY(GLFW_KEY_X);
    MAP_KEY(GLFW_KEY_Y);
    MAP_KEY(GLFW_KEY_Z);

    // Update gamepad inputs
#define MAP_BUTTON(NAV_NO, BUTTON_NO)       { if (buttons_count > BUTTON_NO && buttons[BUTTON_NO] == GLFW_PRESS) io.NavInputs[NAV_NO] = 1.0f; }
#define MAP_ANALOG(NAV_NO, AXIS_NO, V0, V1) { float v = (axes_count > AXIS_NO) ? axes[AXIS_NO] : V0; v = (v - V0) / (V1 - V0); if (v > 1.0f) v = 1.0f; if (io.NavInputs[NAV_NO] < v) io.NavInputs[NAV_NO] = v; }
    int axes_count = 0, buttons_count = 0;
    const float* axes = glfwGetJoystickAxes(GLFW_JOYSTICK_1, &axes_count);
    const unsigned char* buttons = glfwGetJoystickButtons(GLFW_JOYSTICK_1, &buttons_count);
    MAP_BUTTON(ImGuiNavInput_Activate, 0);     // Cross / A
    MAP_BUTTON(ImGuiNavInput_Cancel, 1);     // Circle / B
    MAP_BUTTON(ImGuiNavInput_Menu, 2);     // Square / X
    MAP_BUTTON(ImGuiNavInput_Input, 3);     // Triangle / Y
    MAP_BUTTON(ImGuiNavInput_DpadLeft, 13);    // D-Pad Left
    MAP_BUTTON(ImGuiNavInput_DpadRight, 11);    // D-Pad Right
    MAP_BUTTON(ImGuiNavInput_DpadUp, 10);    // D-Pad Up
    MAP_BUTTON(ImGuiNavInput_DpadDown, 12);    // D-Pad Down
    MAP_BUTTON(ImGuiNavInput_FocusPrev, 4);     // L1 / LB
    MAP_BUTTON(ImGuiNavInput_FocusNext, 5);     // R1 / RB
    MAP_BUTTON(ImGuiNavInput_TweakSlow, 4);     // L1 / LB
    MAP_BUTTON(ImGuiNavInput_TweakFast, 5);     // R1 / RB
    MAP_ANALOG(ImGuiNavInput_LStickLeft, 0, -0.3f, -0.9f);
    MAP_ANALOG(ImGuiNavInput_LStickRight, 0, +0.3f, +0.9f);
    MAP_ANALOG(ImGuiNavInput_LStickUp, 1, +0.3f, +0.9f);
    MAP_ANALOG(ImGuiNavInput_LStickDown, 1, -0.3f, -0.9f);
#undef MAP_BUTTON
#undef MAP_ANALOG
    if (axes_count > 0 && buttons_count > 0)
        io.BackendFlags |= ImGuiBackendFlags_HasGamepad;
    else
        io.BackendFlags &= ~ImGuiBackendFlags_HasGamepad;

    if (!input->GetText().empty())
    {
        std::cout << input->GetText();
        io.AddInputCharactersUTF8(input->GetText().c_str());
    }
    
    int winSizeX, winSizeY;
    glfwGetWindowSize(window_, &winSizeX, &winSizeY);
    io.DisplaySize = ImVec2(winSizeX, winSizeY);

    io.ClipboardUserData = window_;
#if defined(_WIN32)
    io.ImeWindowHandle = (void*)glfwGetWin32Window(window_);
#endif

    ImGui::NewFrame();

    if (callback_)
        callback_();
    else
    {
        ImGui::ShowDemoWindow();
    }

    ImGui::Render();

    auto activeCursor = ImGui::GetMouseCursor();
    switch (activeCursor)
    {
    case ImGuiMouseCursor_Arrow:
    case ImGuiMouseCursor_None:
        glfwSetCursor(window_, defaultCursor_);
        break;
    case ImGuiMouseCursor_ResizeAll:
        glfwSetCursor(window_, defaultCursor_);
        break;
    case ImGuiMouseCursor_ResizeNS:
        glfwSetCursor(window_, resizeVCursor_);
        break;
    case ImGuiMouseCursor_ResizeEW:
        glfwSetCursor(window_, resizeHCursor_);
        break;
    // seriously GLFW? WTF?
    case ImGuiMouseCursor_ResizeNESW:
    case ImGuiMouseCursor_ResizeNWSE:
        glfwSetCursor(window_, defaultCursor_);
        break;
    case ImGuiMouseCursor_TextInput:
        glfwSetCursor(window_, beamCursor_);
        break;
    case ImGuiMouseCursor_Hand:
        glfwSetCursor(window_, handCursor_);
        break;
    }
}

std::shared_ptr<Buffer> DearImgui::GetBuffer(bool isIdx)
{
    if (isIdx)
    {
        if (startIdx_ < indexBuffers_.size())
        {
            ++startIdx_;
            return indexBuffers_[startIdx_ - 1];
        }
        
        auto newbuf = device_->CreateIndexBuffer();
        newbuf->SetTag(BufferTag_Dynamic);

        indexBuffers_.push_back(newbuf);
        ++startIdx_;
        return newbuf;
    }
    else
    {
        if (startVtx_ < vertexBuffers_.size())
        {
            ++startVtx_;
            return vertexBuffers_[startIdx_ - 1];
        }

        auto newbuf = device_->CreateVertexBuffer();
        newbuf->SetTag(BufferTag_Dynamic);

        vertexBuffers_.push_back(newbuf);
        ++startVtx_;
        return newbuf;
    }
}

void DearImgui::Render(View* view, RenderScript* script)
{
    startVtx_ = startIdx_ = 0;

    ImGui::SetCurrentContext(context_);

    std::vector<Vertex2D> vertexData;
    
    auto drawLists = ImGui::GetDrawData();

    uint32_t totalVertCt = 0;

    if (drawLists && drawLists->Valid)
    {
        std::vector<Draw2D> drawCalls;

        for (int i = 0; i < drawLists->CmdListsCount; ++i)
        {
            auto cmdList = drawLists->CmdLists[i];
            ImDrawIdx idx_buffer_offset = 0;
            
            for (int cmdIdx = 0; cmdIdx < cmdList->CmdBuffer.Size; ++cmdIdx)
            {
                const ImDrawCmd* drawCmd = &cmdList->CmdBuffer[cmdIdx];
                
                ImVec4 clipRect = drawCmd->ClipRect;
                Texture* texture = nullptr;
                if (drawCmd->TextureId)
                    texture = (Texture*)drawCmd->TextureId;
                else
                    texture = fontTexture_.get();

                Draw2D batch;
                batch.blendMode_ = Blend_Alpha;
                batch.domain_ = float4(drawLists->DisplayPos.x, drawLists->DisplayPos.y, drawLists->DisplaySize.x, drawLists->DisplaySize.y);
                batch.viewport_ = int4(view->viewport_.x, view->viewport_.y, view->viewport_.z, view->viewport_.w);
                batch.clipRect_ = int4(clipRect.x, clipRect.y, clipRect.z, clipRect.w);
                batch.vertices_ = &vertexData;
                batch.texture_ = texture;
                    
                unsigned begin = batch.vertices_->size();
                batch.vertices_->resize(begin + drawCmd->ElemCount);

                Vertex2D* dest = &((*batch.vertices_)[begin]);
                batch.vertexStart_ = begin;
                batch.vertexCount_ = batch.vertices_->size() - begin;

                // Unfortunately, the index buffer is used a lot
                // Consider adding Vtx/Idx buffer support to UI batch?
                for (unsigned i = 0; i < drawCmd->ElemCount; ++i)
                {
                    unsigned index = cmdList->IdxBuffer[idx_buffer_offset + i];

                    auto& vert = cmdList->VtxBuffer[index];
                    // ?? Consider reordering ImDrawVertex data, would be able to memcpy then
                    dest->position_.x = vert.pos.x;
                    dest->position_.y = vert.pos.y;
                    dest->position_.z = 0.0f;

                    dest->colorPacked_ = vert.col;
                    dest->uvwz_.x = vert.uv.x;
                    dest->uvwz_.y = vert.uv.y;
                    dest->uvwz_.z = 0.0f;
                    dest->uvwz_.w = 0.0f;

                    // xyz, rgba8, uv
                    dest += 1;
                }

                drawCalls.push_back(batch);
                idx_buffer_offset += drawCmd->ElemCount;
            }
        }

        if (drawCalls.size() > 0)
            renderer_->Draw2DBatches(drawCalls, *view, script);
    }
}

}