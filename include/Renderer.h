//****************************************************************************
//
//  File:       Renderer.h
//  License:    MIT
//  Project:    GLVU
//  Contents:   Master management of execution for rendering a collection of
//              views and the resulting shadowmaps, light-buffers, and offscreen
//              model-space renders.
//
//****************************************************************************

#pragma once

#include <glvu.h>

#include <LightShadow.h>
#include <Material.h>
#include <RenderScript.h>

#include <unordered_map>

namespace GLVU
{
    struct Geometry;
    class Light;
    class SceneObject;

    struct GLVU_API Draw2D
    {
        BlendMode blendMode_ = Blend_Alpha;
        math::float4 domain_;
        math::int4 viewport_;
        math::int4 clipRect_;
        Texture* texture_;
        std::vector<Vertex2D>* vertices_;
        uint32_t vertexStart_;
        uint32_t vertexCount_;
    };

    typedef std::pair<RenderScript*, std::shared_ptr<View> > RenderView;

    /// Coalesces the management of multiple views.
    /// Most importantly this ensures that shadowmaps and dynamic cubes are only rendered once per frame.
    class GLVU_API Renderer
    {
    public:
        Renderer(GraphicsDevice*, unsigned shadowDim, unsigned offscreenLightingSize);
        ~Renderer();

        void Execute(std::shared_ptr<View> explicitViews);
        void Execute();

        void AddView(std::shared_ptr<View> view);
        std::vector<std::shared_ptr<View> > GetViews() const;
        void ClearViews();

        inline GraphicsDevice* GetDevice() const { return device_; }

        void ResizeOffscreenBuffers(unsigned shadowDim, unsigned offscreenLightingDim);

        void Draw2DBatches(const std::vector<Draw2D>& calls, View forView, RenderScript*);

        void ScheduleCompute(const ComputeTask& task, ComputeStage when) { computeTasks_.push_back({ task, when }); }

#if defined(GLVU_VK)
    public:
        void ResetCommandBufferChain();
        VkCommandBuffer GetCommandBuffer();
        std::vector<VkCommandBuffer>& GetCommandBufferChain() { return commandBufferChain_; }

    private:
        std::vector<VkCommandBuffer> commandBufferChain_;
#endif

    private:
        void FinishRendering();

        struct PendingView {
            GraphicsDeviceHead* head_;
            RenderScript* script_;
            std::shared_ptr<View> view_;
        };

        void ExecuteInternal(std::vector<PendingView>& views);

        std::unique_ptr<LightTiler> lightTiling_;
        std::unique_ptr<RenderTargetAtlas> shadowAtlas_;
        std::unique_ptr<RenderTargetAtlas> probeAtlas_;
        std::vector<PendingView> views_;
        std::vector<std::pair<ComputeTask, ComputeStage> > computeTasks_;
        GraphicsDevice* device_;

        // Configuration.
        unsigned maxShadowSize_ = 2048;
        unsigned probeSize_ = 64;
        bool shadowsEnabled_ = true;

        // per frame state.
        std::shared_ptr<Geometry> uiGeometry_;
        std::shared_ptr<Buffer> uiUBO_;
    };
}