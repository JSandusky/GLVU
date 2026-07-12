#include "TimingSystem.h"

#include <GLFW/glfw3.h>

namespace GLVU
{

TimingSystem::TimingSystem() :
    frameUpdate_(nullptr),
    lastTime_(0.0),
    userData_(nullptr)
{

}

TimingSystem::~TimingSystem()
{

}

void TimingSystem::Update()
{
    double newTime = glfwGetTime();
    double delta = newTime - lastTime_;
    lastTime_ = newTime;

    ++frameNum_;
    if (frameUpdate_)
        frameUpdate_(delta, frameNum_, userData_);

    for (auto& fixedUpdate : fixedUpdates_)
    {
        fixedUpdate.elapsed_ += newTime;
        while (fixedUpdate.elapsed_ > fixedUpdate.timing_)
        {
            fixedUpdate.frameNum_ += 1;
            fixedUpdate.call_(fixedUpdate.timing_, fixedUpdate.frameNum_, fixedUpdate.userData_);
            fixedUpdate.elapsed_ -= fixedUpdate.timing_;
        }
    }
}

void TimingSystem::SetFixedUpdate(double fixedRate, TIMING_CALL call, void* userData)
{
    fixedUpdates_.push_back({ call, 0ull, fixedRate, 0.0, userData });
}

}