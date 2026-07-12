#pragma once

#include <Renderables.h>

namespace GLVU
{

class ParticleEmitter;

struct ParticleModule {
    virtual void SpawnParticle() = 0;
    virtual void Update(ParticleEmitter*, void* particleData, size_t particleCount);
};

class ParticleEffect
{
public:

private:
    std::array<ParticleModule*, 16> modules_;
    size_t moduleCount_ = 0;
};

class ParticleEmitter : public SceneObject
{
public:
protected:
    std::shared_ptr<ParticleEffect> effect_;
    void* data_;
    unsigned stride_;
};

class ParticleCardEmitter : public ParticleEmitter
{
public:
protected:
};

class ParticleBeamEmitter : public ParticleEmitter
{
public:
protected:
};

class ParticleMeshEmitter : public ParticleEmitter
{
public:
protected:
};

namespace ParticleModules
{
    
struct InitialVelocity : public ParticleModule
{
    std::unique_ptr<VectorDistribution> curve_;

    virtual void SpawnParticle() override;
    virtual void Update(ParticleEmitter*, void* particleData, size_t particleCount) override;
};

struct InitialSize : public ParticleModule
{
    std::unique_ptr<VectorDistribution> curve_;

    virtual void SpawnParticle() override;
    virtual void Update(ParticleEmitter*, void* particleData, size_t particleCount) override;
};

struct Rotation : public ParticleModule
{
    std::unique_ptr<FloatDistribution> curve_;
};

struct Gravity : public ParticleModule
{
    std::unique_ptr<VectorDistribution> curve_;

    virtual void SpawnParticle() override;
    virtual void Update(ParticleEmitter*, void* particleData, size_t particleCount) override;
};

struct Vortex : public ParticleModule
{
    std::unique_ptr<VectorDistribution> positionCurve_;
    std::unique_ptr<VectorDistribution> axisCurve_;
    std::unique_ptr<FloatDistribution> powerCurve_;

    virtual void SpawnParticle() override;
    virtual void Update(ParticleEmitter*, void* particleData, size_t particleCount) override;
};

struct PointAttractor : public ParticleModule
{
    std::unique_ptr<VectorDistribution> positionCurve_;
    std::unique_ptr<FloatDistribution> powerCurve_;
};

struct LineAttractor : public ParticleModule
{
    std::unique_ptr<VectorDistribution> startCurve_;
    std::unique_ptr<VectorDistribution> endCurve_;
    std::unique_ptr<FloatDistribution> powerCurve_;
    bool traceAlong_;
    bool endIsRelative_;
};

struct SizeOverLife : public ParticleModule
{
    std::unique_ptr<VectorDistribution> curve_;
};

struct AccelerationOverLife : public ParticleModule
{
    std::unique_ptr<VectorDistribution> curve_;
};

struct ColorOverLife : public ParticleModule
{
    std::unique_ptr<VectorDistribution> curve_;
};

struct InitialPage : public ParticleModule
{
    std::unique_ptr<VectorDistribution> curve_;
};

struct PageFlip : public ParticleModule
{
    std::unique_ptr<FloatDistribution> keyTimes_;
    std::vector<math::float4> pageRects_;
    bool offsetMode_;
};

}

}