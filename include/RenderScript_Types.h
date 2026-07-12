#pragma once

#include "glvu.h"

#include "Batching.h"
#include "Buffer.h"
#include "Effect.h"
#include "Material.h"
#include "Texture.h"

namespace GLVU
{

class Camera;
class View;
class ViewBufferData;

#define RENDER_SCRIPT_MAX_PARAMS 32

    struct GLVU_API MergeInstanceTraits
    {
        uint32_t vertexStart_;
        uint32_t vertexCount_;
    };

	struct GLVU_API RenderTargetInfo
	{
    // Data configuration
		RS_Identifier name_;
		TextureFormat targetFormat_;
		uint32_t width_;
		uint32_t height_;
		float backbufferWidthFraction_;
		float backbufferHeightFraction_;
		uint32_t fixedWidth_;
		uint32_t fixedHeight_;
		bool pingPong_;

    // Runtime configuration
        std::shared_ptr<Texture> texture_;
	};

	struct GLVU_API RenderDataBufferInfo
	{
		RS_Identifier name_;
		uint32_t size_;
		std::shared_ptr<Buffer> buffer_;
	};

	struct GLVU_API RenderTargetConfiguration
	{
		std::vector<RenderTargetInfo*> targets_;
		std::shared_ptr<FrameBuffer> fbo_;
	};

	enum RenderScriptCmd
	{
		GeometryPass,
		FullscreenQuad,
		LightVolumes,
		DeferredTiledLights,
		ForwardTiledLights,
		ForwardLights,
		ClearTargets,
		RenderCallback, // what to call this? It was originally pre-render only
		GenMips,
		ComputePass,
		BufferCopy,
        Blit, // equivalent to a TargetCopy
	};

    /// Rendering command.
	struct RS_DrawCmd
	{
		RenderScriptCmd commandType_;
		/// Identifier for the upper pass structure
		RS_Identifier passIdentifier_;
		/// Used by lighting and geometry pass commands
		RS_Identifier context_;
		/// Used by FullscreenQuad (actually it's handled as a single triangle)
		std::shared_ptr<Effect> effect_;
        /// Optional override.
        std::shared_ptr<Material> materialOverride_;
		/// List of textures attached to this draw command.
		Material::TextureBindingList textures_;
		/// List of buffers attached to this draw command.
		RS_SlottedIdentifier buffers_;
        /// List of extra parameters
        float params_[RENDER_SCRIPT_MAX_PARAMS];
        /// Count of the params.
        uint32_t numParams_ = 0;
		/// Any draw command can potentially be disabled.
		bool enabled_ = true;
		/// Only applicable to Vulkan, means the command-buffer can be baked
		bool canBake_ = false;
#if defined(GLVU_VK)
		VkCommandBuffer bakedBuffer_ = 0;
#endif
        ResolvedBatchQueue resolvedQueue_;

        // Command data
		union {

			struct clear {
				float color_[4];
				float depth_;
				uint32_t stencilValue_;
				bool discardColor_;
				bool discardDepth_;
				bool discardStencil_;
				bool willFill_;
			} clearData_;

			struct quad {
				RS_Identifier inputSize_;
				RS_Identifier outputSize_;
				RS_Identifier shader_;
			} quadData_;

			struct draw {
                RS_Identifier materialOverride_;
				SortMode sortMode_;
				bool alphaToCoverage_;
			} drawData_;

			struct blit {
				RS_Identifier source_;
				RS_Identifier dest_;
				bool runMipsAfterwards_;
			} blitData_;

			struct genMips {
				RS_Identifier texture_;
				uint32_t layer_;
			} genMipsData_;

			struct compute {
				uint32_t groupsX_;
				uint32_t groupsY_;
				uint32_t groupsZ_;
			} computeData_;

			struct callback {
				RS_Identifier callID_;
			} callData_;

			struct buffer_copy {
				RS_Identifier source_;
				RS_Identifier dest_;
			} buffCopyData_;

			struct {
				uint32_t lightMask_;    // only select passing lights
                RS_Identifier context_; // use this context, such as for decals instead of lights
			} volumeLightData_;

            struct {
                uint32_t lightMask_;    // only select passing lights
                RS_Identifier context_; // use this context, such as for decals
            } forwardLightData_;

			struct {
				uint32_t tilesX_;
				uint32_t tilesY_;
				uint32_t tilesZ_;
				uint32_t lightMask_;
			} tiledLightData_;

            struct {
                RS_Identifier target_;
                RS_Identifier source_;
            } targetCopyData_;

            // ?? draw a render-target to the screen ... odd. Why did I feel this was needed?
			struct {
				RS_Identifier source_;
				float pos_[2];
				float size_[2];
			} debugTargetData_;

		} cmdData_;
	};

    /// Encapsulation of the core view-data.
    struct ViewData {
        math::float2 inputTexSize;
        math::float2 invInputTexSize;
        math::float2 outputTexSize;
        math::float2 invOutputTexSize;
    };

    void SetupViewData(ViewData& target, math::uint4 viewport, RenderTargetInfo* srcTex, RenderTargetInfo* destTex);
    void SetupViewbufferData(const View&, ViewBufferData&);
    void SetupViewbufferData(const Camera*, ViewBufferData&, int targetEye);

    /// A stage is a greater collection of commands, where all commands are written to the same set of targets.
    /// Vulkan and DX12 benefit from this outer grouping instead of just a raw list of commands.
    struct GLVU_API RenderScriptStage
    {
        // constant data
        RS_Identifier self_;
        std::vector<RS_Identifier> targets_;
        RS_SlottedIdentifier targetBindings_;
        std::vector<RS_Identifier> requireActiveStages_;
        std::vector<RS_Identifier> ignoreActiveStages_;
        std::vector<RS_DrawCmd> commands_;
        RenderTargetConfiguration targetConfig_;
        bool active_;

        bool IsEffectApplicable(const std::shared_ptr<Effect>&) const;
    };
}
