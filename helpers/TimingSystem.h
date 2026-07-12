#pragma once

#include <vector>

namespace GLVU
{

typedef void(*TIMING_CALL)(double timing, uint64_t frameNum, void* userData);

//****************************************************************************
//
//  Class:      TimingSystem
//
//  Purpose:    Manages the update and dispatch of per-frame and fixed-interval
//              w/ accumulator callbacks.
//
//****************************************************************************
class TimingSystem
{
public:
    TimingSystem();
    virtual ~TimingSystem();

    void Update();

    void SetFixedUpdate(double fixedRate, TIMING_CALL call, void* userData = nullptr);
    void SetFrameUpdate(TIMING_CALL call, void* userData = nullptr) { frameUpdate_ = call; userData_ = userData; }

    uint32_t GetFrameNumber() const { return frameNum_; }

    void ResetFrameNumbers()
    {
        frameNum_ = 0;
        for (auto& c : fixedUpdates_)
            c.frameNum_ = 0;
    }

private:
    struct FixedUpdate {
        TIMING_CALL call_;
        uint64_t frameNum_;
        double timing_;
        double elapsed_;
        void* userData_;
    };

    TIMING_CALL frameUpdate_;
    std::vector<FixedUpdate> fixedUpdates_;
    uint64_t frameNum_;
    void* userData_;
    double lastTime_;
};

}