#pragma once

#include <MathGeoLib/MathGeoLib.h>

#include <GLFW/glfw3.h>

#include <list>
#include <unordered_map>
#include <unordered_set>

namespace GLVU
{

//****************************************************************************
//
//  Class:      InputSystem
//
//  Purpose:    Records the input state into a structure that we can
//              query as needed while also performing some subtleties
//              such as global vs per-window input handling as
//              well as key-bindings and press mechanisms.
//
//****************************************************************************
class InputSystem
{
public:
    InputSystem();
    void RegisterWindow(GLFWwindow* window);

    void Update(float td);
    void PostUpdate();

    bool GetKeyDown(int code) const { return globalData_.keyDown_.find(code) != globalData_.keyDown_.end(); }
    bool GetKeyPressed(int code) const { return globalData_.keyPress_.find(code) != globalData_.keyPress_.end(); }
    std::string GetText() const { return globalData_.text_; }
    int GetWheel() const { return globalData_.mouseWheel_; }
    float2 GetMousePosition() const { return globalData_.mousePosition_; }
    bool GetMouseDown(int code) const { return globalData_.mousebuttonDown_[code]; }
    bool GetMousePressed(int code) const { return globalData_.mousebuttonPressed_[code]; }

    struct GamePadData
    {
        bool buttonsDown_[32] = { };
        bool buttonsPressed_[32] = { };
        float axisValues_[32] = { };
    };

    struct InputData
    {
        bool mousebuttonDown_[32] = { };
        bool mousebuttonPressed_[32] = { };

        std::unordered_set<int> keyDown_;
        std::unordered_set<int> keyPress_;
        std::unordered_set<int> scanCodeDown_;
        std::unordered_set<int> scanCodePress_;

        float2 mouseMove_ = float2::zero;
        float2 mousePosition_ = float2::zero;
        float2 lastMousePosition_ = float2::zero;

        std::list<int> availableTouchIDs_;
        std::unordered_map<int, int> touchIDMap_;
        int mouseWheel_ = 0;
        bool hadTouch_ = false;
        bool everHadTouch_ = false;
        std::string text_;
    };

    static InputSystem* GetInst() { return inst_; }

private:
    static InputSystem* inst_;

    InputData globalData_;
    GamePadData gamePads_[16];
    std::unordered_map<GLFWwindow*, InputData> windowInputData_;
};

}