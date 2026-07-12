//****************************************************************************
//
//  File:       RenderScript.h
//  License:    MIT
//  Project:    GLVU
//
//****************************************************************************

#pragma once

#include "glvu.h"

#include "Buffer.h"
#include "GraphicsDevice.h"
#include "RenderScript_Types.h"
#include "Texture.h"
#include "ShaderConstants.h"

#include <vector>
#include <memory>

namespace GLVU
{

// Common
    // slot information
    #define UBO_SLOT_ENVIRONMENT 4
    // timings
    #define UBO_SLOT_FRAME 5

// Fullscreen Quad
    // from the render_script configuration `params`
    #define UBO_SLOT_QUAD_CUSTOM 0
    // viewMat and invViewMat
    #define UBO_SLOT_QUAD_VIEW_MATRIX 3
    // Sizing information for the src and destination targets
    #define UBO_SLOT_QUAD_SIZE_DATA 4
    // Screen-space velocities of the camera, use for faux-blur
    #define UBO_SLOT_QUAD_VELOCITIES 5

    class Camera;
    class BatchQueue;
    class Effect;
    class FrameBuffer;
    struct Geometry;
    class IQueriableScene;
    class Light;
    class Material;
    class RenderScript;
    class ShaderPass;
    class ShadowBatchData;
    class Texture;

    struct RenderTargetAtlas;
    struct RenderScriptStage;

    struct GLVU_API Vertex2D
    {
        math::float3 position_; // xy,z
        math::float4 uvwz_; // W may be used as an array index while Z may be used as a mip
        unsigned colorPacked_;
    };

    struct GLVU_API View
    {
        /// Despite their sequence of provision views will be rendered head-by-head.
        GraphicsDeviceHead* head_ = nullptr;
        /// A view that is not rendered, but used for offscreen-lighting, and frustum culling. Usage case is VR.
        View* sourcingView_ = nullptr;
        /// Scene to be rendered.
        IQueriableScene* scene_ = nullptr;
        /// Camera for this view.
        Camera* cameras_[2] = { nullptr, nullptr };
        /// Script to execute.
        RenderScript* script_ = nullptr;
        /// Viewport region in the rendertarget.
        math::uint4 viewport_;
        /// With VR the viewport_ is the total area, this is the viewport for each eye.
        math::uint4 eyeViewports_[2];
        /// Target to draw to (or it'll be the head's backbuffer [or master backbuffer if there's no head, w/e that means]).
        std::shared_ptr<FrameBuffer> renderTarget_;

        /// Mask to bitwise test against renderables.
        unsigned mask_ = UINT_MAX;
        /// Settings indicating what this view will allow during rendering.
        unsigned flags_ = ViewFlag_Default;

        View(Camera* camera, RenderScript* script, std::shared_ptr<FrameBuffer> renderTarget) :
            renderTarget_(renderTarget), 
            sourcingView_(nullptr),
            script_(script)
        {
            cameras_[0] = camera;
            SetViewport(0, 0, renderTarget_->GetWidth(), renderTarget_->GetHeight());
        }

        View(Camera* camera, RenderScript* script, math::uint4 vpt) :
            viewport_(vpt),
            script_(script)
        {
            cameras_[0] = camera;
        }

        void SetViewport(uint32_t x, uint32_t y, uint32_t width, uint32_t height) {
            viewport_[0] = x;
            viewport_[1] = y;
            viewport_[2] = width;
            viewport_[3] = height;
        }

        inline bool IsRoot() const { return flags_ & ViewFlag_IsRoot; }

        inline void SetFlag(ViewFlag flag) { flags_ |= flag; }
        inline bool TestFlag(ViewFlag flag) const { return flags_ & flag; }
        inline void ClearFlag(ViewFlag flag) { flags_ &= ~((unsigned)flag); }
    };

    /// Coordinates the execution of rendering a view into a scene.
    class GLVU_API RenderScript : public GPUObject
    {
        friend class GraphicsDevice;
        friend class Renderer;
        friend struct RenderTargetAtlas;
    public:
        RenderScript(GraphicsDevice* device);
        virtual ~RenderScript();

        virtual bool IsValid() const override;
        virtual void Release() override;
        std::shared_ptr<RenderScript> Clone() const;

        void OnFrameStart();
        void OnFrameEnd();
        void OnBackbufferResize(GraphicsDevice*, uint32_t newWidth, uint32_t newHeight);
        void Execute(Renderer*, View, const std::vector<Batch>& batches, const std::vector< std::shared_ptr<Light> >& lights);
        void RenderShadowMap(Renderer*, ShadowBatchData& batch, uint4 viewport, std::shared_ptr<FrameBuffer> fbo, int shadowMapIndex, bool isFirst);
        void RenderFullscreen(std::shared_ptr<Effect> effect, View view, const Material::UBOBindingList& extraBuffers, const Material::TextureBindingList& extraTextures);
        void RenderOffscreen(std::vector<Batch>& batches, uint4 viewport, std::shared_ptr<FrameBuffer> fbo);

        RenderScriptStage* GetStage(const char* id) const;
        RenderScriptStage* GetStage(uint32_t nameHash) const;
        void SetStageEnabled(const char* id, bool state);

        static std::shared_ptr<RenderScript> Load(GraphicsDevice* device, const char* fileName);
        static std::shared_ptr<RenderScript> Load(GraphicsDevice* device, const char* buffer, size_t fileSize);

    protected:
        struct DrawBatchesTaskData
        {
            uint32_t contextNameHash_;
            uint32_t instancedNameHash_;
            uint32_t skinnedNameHash_;

            /// Externally bound uniform buffers.
            Material::UBOBindingList uboBindings_;
            /// Externally bound textures.
            Material::TextureBindingList textureBindings_;

            /// In Vulkan when threading need to place the command-buffer in the right position, but the cmd-buffer has to come from the damn thread.
            unsigned placeCmdBufferAt_ = -1;
            /// The targets need to be cleared, used by Vulkan to facilitate FBO clears without repeated wipes. Set the flag on the first set of draws.
            bool clearTargets_ = false;

#if defined(GLVU_VK)
            // optional, used by threading to indicate that draw-batches should NOT pump a command buffer itself.
            VkCommandBuffer cmdBuffer_ = 0;
#elif defined(GLVU_DX11)
            ID3D11DeviceContext* deferredCtx_ = nullptr;
            ID3D11CommandList* cmdList_ = nullptr;
#endif
        };

        void Prepare(GraphicsDevice*);

        bool ShouldStageExecute(RenderScriptStage*) const;
        void BeginStage(GraphicsDevice* device, View view, RenderScriptStage* stage);
        void EndStage(GraphicsDevice* device, RenderScriptStage* stage);
        void PrepareQueue(BatchQueue&);
        void DrawBatches(Renderer*, View, RenderScriptStage* stage, const BatchQueue& selectedBatch, DrawBatchesTaskData& contextNameHash);
        void CoalesceInstanceBatches(GraphicsDevice*, const std::vector<Batch>& batchesIn, std::vector<Batch>& batchesOut);

        void ApplyPass(const std::shared_ptr<ShaderPass> pass, View view);
        void ApplyGeometry(std::shared_ptr<ShaderPass> pass, Geometry* geometry, bool instanced, const std::vector<std::shared_ptr<Buffer>>& extraBuffers = { }, bool laterInstanced = false, bool isVR = false);
        void ApplyBlendMode(BlendMode, View, RenderScriptStage*);

        RenderTargetInfo* GetTargetTexture(uint32_t nameHash);
        std::shared_ptr<Buffer> GetDataBuffer(uint32_t nameHash);
        std::shared_ptr<Buffer> FindBuffer(RS_Identifier id) {
            auto local = GetDataBuffer(id.nameHash_);
            if (local) return local;
            return device_->GetSystemBuffer(id.name_);
        }

		void RenderForwardLights(Renderer*, View view, RenderScriptStage* stage, const RS_DrawCmd& cmd, const std::vector<std::shared_ptr<Light>>& lights);

    private:
        uint32_t width_, height_;

        ViewBufferData viewBufferData_;
        std::shared_ptr<Buffer> viewUniformBuffer_;

        PerFrameData perFrameData_;
        std::shared_ptr<Buffer> frameUniformBuffer_;

        // all lights get uploaded at once
        std::shared_ptr<Buffer> lightUniformBuffer_;

        std::vector<RenderDataBufferInfo*> dataBuffers_;
        std::vector<RenderTargetInfo*> targetTextures_;
        std::vector<RenderScriptStage*> stages_;
        std::vector<RS_DrawCmd*> forwardLitCmds_;
    };
}
