#include "InputSystem.h"

namespace GLVU
{

InputSystem* InputSystem::inst_ = nullptr;

InputSystem::InputSystem()
{
    inst_ = this;

    glfwSetJoystickCallback([](int joystick, int event) {
    });
}

std::unordered_map<GLFWwindow*, InputSystem::InputData> g_inputWindowData;

void SetKey(InputSystem::InputData& data, int key, int scanCode, bool down)
{
    if (down)
    {
        data.keyDown_.insert(key);
        data.scanCodeDown_.insert(scanCode);
    }
    else
    {
        auto found = data.keyDown_.find(key) != data.keyDown_.end();
        if (found)
        {
            data.keyPress_.insert(key);
            data.scanCodePress_.insert(scanCode);
        }
        data.keyDown_.erase(key);
        data.scanCodeDown_.erase(scanCode);
    }
}

void SetMouse(InputSystem::InputData& data, int button, bool down)
{
    if (down)
    {
        data.mousebuttonDown_[button] = true;
    }
    else
    {
        if (data.mousebuttonDown_[button])
            data.mousebuttonPressed_[button] = true;
        data.mousebuttonDown_[button] = false;
    }
}

void InputSystem::PostUpdate()
{
    globalData_.text_.clear();
    globalData_.mouseWheel_ = 0;
}

void InputSystem::Update(float td)
{
    //globalData_.text_.clear();
    globalData_.keyPress_.clear();
    globalData_.scanCodePress_.clear();
    for (int i = 0; i < 32; ++i)
        globalData_.mousebuttonPressed_[i] = false;

    for (int i = 0; i < 16; ++i)
    {
        for (int b = 0; b < 32; ++b)
            gamePads_[i].buttonsPressed_[b] = false;
    }

    for (auto& set : g_inputWindowData)
    {
        auto& data = set.second;
        data.keyPress_.clear();
        data.scanCodePress_.clear();
        data.text_.clear();

        for (int i = 0; i < 32; ++i)
            data.mousebuttonPressed_[i] = false;
    }

    for (int i = 0; i < 16; ++i)
    {
        if (glfwJoystickPresent(i))
        {
            int axesCount;
            const float* axes = glfwGetJoystickAxes(i, &axesCount);

            for (int a = 0; a < axesCount; ++a)
                gamePads_[i].axisValues_[a] = axes[a];

            int buttonCt;
            const unsigned char* buttons = glfwGetJoystickButtons(i, &buttonCt);
            for (int b = 0; b < buttonCt; ++b)
            {
                if (buttons[b] == GLFW_RELEASE)
                {
                    gamePads_[i].buttonsPressed_[b] = true;
                    gamePads_[i].buttonsDown_[b] = false;
                }
                else
                    gamePads_[i].buttonsDown_[b] = buttons[b] != GLFW_RELEASE;
            }
        }
    }
}

void InputSystem::RegisterWindow(GLFWwindow* window)
{
    g_inputWindowData[window] = InputData();

    glfwSetCharCallback(window, [](GLFWwindow* w, unsigned int codePoint) {
        auto& winData = g_inputWindowData[w];
        auto input = InputSystem::GetInst();
        winData.text_ = (char)codePoint;
        input->globalData_.text_ = (char)codePoint;
    });

    glfwSetMouseButtonCallback(window, [](GLFWwindow* w, int button, int action, int modifiers) {
        auto& winData = g_inputWindowData[w];

        auto input = InputSystem::GetInst();

        SetMouse(winData, button, action != GLFW_RELEASE);
        SetMouse(input->globalData_, button, action != GLFW_RELEASE);

        SetKey(winData, GLFW_KEY_LEFT_SHIFT, GLFW_KEY_LEFT_SHIFT, modifiers & GLFW_MOD_SHIFT);
        SetKey(winData, GLFW_KEY_LEFT_ALT, GLFW_KEY_LEFT_ALT, modifiers & GLFW_MOD_ALT);
        SetKey(winData, GLFW_KEY_LEFT_CONTROL, GLFW_KEY_LEFT_CONTROL, modifiers & GLFW_MOD_CONTROL);

        SetKey(input->globalData_, GLFW_KEY_LEFT_SHIFT, GLFW_KEY_LEFT_SHIFT, modifiers & GLFW_MOD_SHIFT);
        SetKey(input->globalData_, GLFW_KEY_LEFT_ALT, GLFW_KEY_LEFT_ALT, modifiers & GLFW_MOD_ALT);
        SetKey(input->globalData_, GLFW_KEY_LEFT_CONTROL, GLFW_KEY_LEFT_CONTROL, modifiers & GLFW_MOD_CONTROL);
    });

    glfwSetKeyCallback(window, [](GLFWwindow* w, int key, int scanCode, int action, int modifiers) {
        auto& winData = g_inputWindowData[w];

        auto input = InputSystem::GetInst();

        SetKey(winData, key, scanCode, action != GLFW_RELEASE);
        SetKey(input->globalData_, key, scanCode, action != GLFW_RELEASE);
        
        SetKey(winData, GLFW_KEY_LEFT_SHIFT, GLFW_KEY_LEFT_SHIFT, modifiers & GLFW_MOD_SHIFT);
        SetKey(winData, GLFW_KEY_LEFT_ALT, GLFW_KEY_LEFT_ALT, modifiers & GLFW_MOD_ALT);
        SetKey(winData, GLFW_KEY_LEFT_CONTROL, GLFW_KEY_LEFT_CONTROL, modifiers & GLFW_MOD_CONTROL);

        SetKey(input->globalData_, GLFW_KEY_LEFT_SHIFT, GLFW_KEY_LEFT_SHIFT, modifiers & GLFW_MOD_SHIFT);
        SetKey(input->globalData_, GLFW_KEY_LEFT_ALT, GLFW_KEY_LEFT_ALT, modifiers & GLFW_MOD_ALT);
        SetKey(input->globalData_, GLFW_KEY_LEFT_CONTROL, GLFW_KEY_LEFT_CONTROL, modifiers & GLFW_MOD_CONTROL);
    });

    glfwSetScrollCallback(window, [](GLFWwindow* w, double, double y) {
        auto& winData = g_inputWindowData[w];

        auto input = InputSystem::GetInst();
        input->globalData_.mouseWheel_ = y;
    });

    glfwSetCursorPosCallback(window, [](GLFWwindow* w, double x, double y) {
        auto& winData = g_inputWindowData[w];
        auto oldPos = winData.mousePosition_;

        winData.lastMousePosition_ = winData.mousePosition_;
        winData.mousePosition_.x = x;
        winData.mousePosition_.y = y;
        winData.mouseMove_ = winData.mousePosition_ - oldPos;

        auto input = InputSystem::GetInst();

        input->globalData_.lastMousePosition_ = winData.mousePosition_;
        input->globalData_.mousePosition_.x = x;
        input->globalData_.mousePosition_.y = y;
        input->globalData_.mouseMove_ = winData.mousePosition_ - oldPos;
    });

    glfwSetCharModsCallback(window, [](GLFWwindow* w, unsigned int codePoint, int modifiers) {
        auto& winData = g_inputWindowData[w];
    });
}

}