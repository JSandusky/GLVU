//****************************************************************************
//
//  File:       Effect.h
//  License:    MIT
//  Project:    GLVU
//  Contents:   Types for the shader-pipeline from individual shaders and their
//              metadata up thru a complete pass and amalgamations of passes
//              and states.
//
//****************************************************************************

#pragma once

#include <glvu.h>

#include <array>
#include <map>

namespace GLVU
{

    class Texture;

    /// A field within a UBO, meta is limited - you still need to know roughly what it is.
    struct GLVU_API UBORecord
    {
        /// buffer binding-index, binding-sets are inferred from standardization. Always 0 for buffers.
        uint16_t blockIndex_;
        /// Offset into the buffer.
        uint16_t offset_;
        /// Name of the field.
        char name_[128];
    };

    /// Metadata about a specific uniform buffer binding found in the shader.
    struct GLVU_API UBOInfo
    {
        /// Name of the UBO.
        char name_[128];
        /// Total size as reported by the reflection APIs.
        uint32_t totalSize_;
        /// binding-index of the buffer, sets are inferred by standardization. Always 0 for buffers.
        uint32_t bindingIndex_;
        /// List of fields in the UBO.
        std::vector<UBORecord> records_;
    };

    /// Texture information.
    struct GLVU_API TexInfo
    {
        /// Binding index, set is inferred to always be 1
        uint16_t blockIndex_;
        /// Name of the texture object.
        char name_[128];
    };

    /// A distance shader-module for one of the shader stages of the pipeline. VS, FS, HS, DS, GS, CS, etc.
	class GLVU_API Shader : public GPUObject
	{
		friend class ShaderPass;
	public:
		/// Construct, at this point the shader's type, code, and defines are set in stone. It could be compiled here but then failure status would be an additional query anyways.
		Shader(GraphicsDevice*, const std::string& name, ShaderType, ShaderCodeType, const std::string& code, const std::vector<std::string>& shaderDefs);
		/// Destruct, release GPU objects.
		~Shader();

		/// Release GPU objects.
		virtual void Release() override;
		/// Low-accuracy utility.
		virtual bool IsValid() const override;

		/// Returns the ShaderType for the pipeline stage this shader is intended for.
		ShaderType GetStage() const { return kind_; }
		/// Attempt to compile the shader, returns false if failed.
		bool Compile();

		/// Returns the name this shader was initialized with, high risk to assume its nature.
		const std::string& GetName() const { return name_; }
		/// Returns the list of defines this shader was created with.
		const std::vector<std::string>& GetDefines() const { return defines_; }
		/// Try to get complete GLSL code out of the shader, this only applies to shaders that came from actual GLSL.
		std::string GetCode() const { return code_; }

#if defined(GLVU_GL) || defined(GLVU_GLES3)
	public:
		/// Returns the GL shader handle.
		inline GLuint GetGPUObject() { return shader_; }
	private:
		/// GL shader handle, 0 if not compiled.
		GLuint shader_;
#elif defined(GLVU_DX11)
	public:
		template<typename T>
		T* GetShader() const { return (T*)shader_; }
		ID3DBlob* GetShaderByteCode() const { return shaderByteCode_; }
        uint32_t GetSignatureHash() const { return sigHash_; }
	private:
		ID3D11Resource* shader_ = nullptr;
		std::vector<UBOInfo> cbufferData_;
		std::vector<TexInfo> texData_;
		ID3DBlob* shaderByteCode_ = nullptr;
        uint32_t sigHash_ = 0;
#elif defined(GLVU_VK)
    public:
        /// Get the shader-module for use.
        inline VkShaderModule GetGPUObject() { return shader_; }
    private:
        /// The VkShaderModule (or 0) that was compiled.
        VkShaderModule shader_;
#elif defined(GLVU_D3D12)
    public:
    private:
#else
	#error
#endif
        /// List of preprocessor definitions for shader compilation.
        std::vector<std::string> defines_;
        /// Stage this shader belongs to.
        ShaderType kind_;
        /// Code-type, always GLSL in public version.
        ShaderCodeType codeType_;
        /// Identifying name (likely a filename, but not necessarilly)
        std::string name_;
        /// Source-code used at construction, already has #include's resolved.
        std::string code_;
        /// Compute-shader group-size.
        math::uint3 dispatchGroupSize_ = { };
    };

    /// Encapsulates the common state into one utility object, the meanings
    /// of the members should be apparent by naming. If not then the
    //  reader probably shouldn't be using this library.
    struct GLVU_API DrawState
    {
        CullingMode culling_ = CULL_FRONT;
        Comparison depthCompare_ = COMPARE_LEQUAL;
        BlendMode blendMode_ = Blend_None;
        uint32_t stencilWrite_ = 0xFFFFFFFF;
        uint32_t stencilMask_ = 0;
        bool stencilTest_ = false;
        bool alphaToCoverage_ = false;
        bool depthTest_ = true;
        bool depthWrite_ = true;
        bool alphaTest_ = false;
        float depthBias_ = 0.0f;
        float slopeBias_ = 0.0f;
    };

    /// A complete combination of shader-stages (or just a compute-shader, because Krhonos has *reasons*)
    /// used to form a full GPU piepline.
    class GLVU_API ShaderPass : public GPUObject
    {
    public:
        /// Construct and setup name identifier.
        ShaderPass(GraphicsDevice* device, const char* name, PrimitiveType forPrim);
        /// Destruct, dispose.
        ~ShaderPass();

        /// Only a basic test.
        virtual bool IsValid() const override;
        /// Depose the GPU objects from their thrones.
        virtual void Release() override;

        /// Returns the identifying name for this pass, typically meaningful/annotated (ie. Diffuse, Diffuse_INST, Diffuse_SKINNED)
        const char* GetName() const { return identifier_.name_; }
        /// Returns the hash of the identifying name for this pass, used in performance critical areas.
        inline uint32_t GetNameHash() const { return identifier_.nameHash_; }

        /// *Links* a compute-shader, this is Khronos nonsense mostly.
        bool Link(std::shared_ptr<Shader> computeShader);
        /// Links the given shaders into a program. ALWAYS check for success.
        bool Link(std::shared_ptr<Shader> vs, std::shared_ptr<Shader> ps, std::shared_ptr<Shader> gs = nullptr, std::shared_ptr<Shader> hs = nullptr, std::shared_ptr<Shader> ds = nullptr);

        /// Get UBO meta.
        UBOInfo* GetUBO(uint32_t blockIndex);
        /// Get UBO meta.
        UBOInfo* GetUBO(const char* name);

        /// Get texture meta (or null) by binding index.
        TexInfo* GetTexInfo(uint32_t bindingIndex);
        /// Get texture meta (or null) by name.
        TexInfo* GetTexInfo(const char* name);

        /// Returns true if there are any tessellation stage shaders present, needed to switch primitive-types when rendering triangles as *patches.*
        inline bool IsTessellating() const { return hs_ != nullptr && ds_ != nullptr; }

		/// Returns true if there's a geometry shader in the pipe.
		inline bool HasGeometryShader() const { return gs_ != nullptr; }

		/// Returns true if this is a compute shader, we should not have anthing else - that error checking should be done elsewhere.
        inline bool IsCompute() const { return cs_ != nullptr; }

        /// Returns the draw-state encapsulation.
        inline DrawState& GetDrawState() { return drawState_; }
        /// Returns the draw-state encapsulation.
        inline const DrawState& GetDrawState() const { return drawState_; }

        /// Attempts to get a version of this shader using additional #defines (ie. for skinning/instancing)
        std::shared_ptr<ShaderPass> GetVariation(const std::string& name, const std::vector<std::string>& defines) const;

        PrimitiveType GetPrimitive() const { return forPrim_; }

        std::shared_ptr<Shader> GetVS() { return vs_; }
        std::shared_ptr<Shader> GetPS() { return ps_; }
        std::shared_ptr<Shader> GetGS() { return gs_; }
        std::shared_ptr<Shader> GetHS() { return hs_; }
	    std::shared_ptr<Shader> GetDS() { return ds_; }
	    std::shared_ptr<Shader> GetCS() { return cs_; }

        const std::array<uint32_t, 16>& GetTextureAccesses() const { return stageTextureAccesses_; };

        inline math::uint3 GroupSize() const { return cs_->dispatchGroupSize_; }
        inline math::uint3 ToGroupSize(math::uint3 threadCt) const { 
            return ToGroupSize(threadCt, cs_->dispatchGroupSize_);
        }

        inline static math::uint3 ToGroupSize(math::uint3 threadCt, math::uint3 localGroupSize) {
            return math::uint3(
                math::CeilInt(threadCt.x / (float)localGroupSize.x),
                math::CeilInt(threadCt.y / (float)localGroupSize.y),
                math::CeilInt(threadCt.z / (float)localGroupSize.z)
            );
        }
    
    protected:
        /// Record the meta-data needed.
        void BuildReflection();

#if defined(GLVU_GL) || defined(GLVU_GLES3)
    public:
        /// Get the glProgram object
        inline GLuint GetGPUObject() { return shaderProgram_; }
    private:
        /// glProgram
        GLuint shaderProgram_;
#elif defined(GLVU_VK)
    public:
        /// Get the pipeline object for usage.
        inline VezPipeline GetPipeline() { return pipeline_; }
    private:
        /// Vez pipeline handle, VEZ deals with the permutations for us.
        VezPipeline pipeline_;
#elif defined(GLVU_DX11)
	public:
	private:
#endif
        /// Compiled shaders used by this pass.
        std::shared_ptr<Shader> vs_, ps_, gs_, hs_, ds_, cs_;
        /// Uniform-buffer binding meta.
        std::vector<UBOInfo> uniformBuffers_;
        /// Texture binding meta.
        std::vector<TexInfo> textures_;
        /// Name/hash identifier for this pass.
        RS_Identifier identifier_;
        /// Local draw-attributes for this pass.
        DrawState drawState_;
        /// A shader-pass can be linked to a primitive-type (or UNKNOWN for all). This makes effect authoring easier using a `pass MyPass:triangles {}` syntax to overload pass contexts.
        PrimitiveType forPrim_;
        /// Bitwise mask indicating which shader-stages a texture is used by.
        std::array<uint32_t, 16> stageTextureAccesses_;
        /// Bitwise mask indicating which shader-stages a CBuffer is used by.
        std::array<uint32_t, 16> stageCBufferAccesses_;
    };

    /// Encapsulates a collection of passes responsible for the completion of some
    /// specific classification of geometries.
    class GLVU_API Effect : public GPUObject
    {
    public:
        /// Construct.
        Effect(GraphicsDevice* device) : GPUObject(device) { }
        /// Destruct and free GPU objects.
        ~Effect() { Release(); }

        /// Verifies that there are passes to potentially use.
        virtual bool IsValid() const { return !passes_.empty(); }
        /// Free the GPU objects.
        virtual void Release();

        /// Get the list of passes.
        const std::vector< std::shared_ptr<ShaderPass> >& GetPasses() const { return passes_; }
        /// Get the list of passes.
        std::vector< std::shared_ptr<ShaderPass> >& GetPasses() { return passes_; }
        /// Find a pass by hash.
        std::shared_ptr<ShaderPass> GetPass(uint32_t contextHash, PrimitiveType) const;
        /// Find a pass by string identifier.
        std::shared_ptr<ShaderPass> GetPass(const char* name, PrimitiveType) const;
        /// Adds a pass, there are no safety checks performed.
        void AddPass(std::shared_ptr<ShaderPass>);

        /// Handles the texture-bind and sampler configuration.
        void BindTexture(std::shared_ptr<Texture>, uint32_t slot, ShaderPass* shaderPass);

        /// Utility for creating a complete effect from file data.
        static std::shared_ptr<Effect> LoadEffect(GraphicsDevice* device, const char* fileName);
        /// Utility for creating a complete effect from blob text data.
        static std::shared_ptr<Effect> LoadEffect(GraphicsDevice* device, const char* buffer, size_t fileSize);

        /// Find uniform buffer meta by name.
        UBORecord* GetUBORecord(const char* name);
        /// Find a texture binding index by name.
        uint32_t GetTextureSlot(const char* name);
        /// Get the Sampler properties of a texture-binding slot (or defaults).
        SamplerTraits GetSamplerTraits(uint32_t slot);
        /// Gets the default-texture (if any) for a binding-index.
        std::shared_ptr<Texture> GetDefaultTexture(uint32_t slot) const;

    private:
        typedef std::pair<uint32_t, std::shared_ptr<Texture> > DefaultTexture;

        /// List of default-textures for this effect.
        std::vector<DefaultTexture> defaultTextures_;
        /// List of defined samplers.
        std::vector<std::pair<uint32_t, SamplerTraits> > samplers_;
        /// All passes contained by this effect.
        std::vector< std::shared_ptr<ShaderPass> > passes_;
        /// Flat list of all known UBO fields.
        std::vector<UBORecord> uboFields_;
        /// Table tracking all texture-bindings that are in use by any pass.
        std::vector<std::pair<uint32_t, std::string> > usedTextureSlots_;
        /// Maps an alias -> concrete name.
        std::map<std::string, std::string> aliasNames_;
    };

}