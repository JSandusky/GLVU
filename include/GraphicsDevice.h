//****************************************************************************
//
//  File:       GraphicsDevice.h
//  License:    MIT
//  Project:    GLVU
//
//****************************************************************************

#pragma once

#include "glvu.h"

#include "Blob.h"
#include "Buffer.h"
#include "Geometry.h"
#include "Material.h"
#include "Texture.h"

#include "GPUResourceCache.h"

#if defined(GLVU_GL) || defined(GLVU_GLES3)
    #include "GLHelpers.h"
#endif

#ifdef GLVU_D3D12
    #include "D3D12MemAlloc.h"
#endif

#include <map>
#include <memory>
#include <mutex>
#include <unordered_map>

namespace GLVU
{

	struct DX11StateCache;
    struct GLState;
    class GraphicsDevice;
    class GraphicsDeviceHead;
    struct RenderTargetInfo;
    class Shader;
    class ShaderCache;
    class ScratchBufferCache;
    class CommandBufferPool;
    struct View;

    /// Signature of the function that the external job system must call.
    typedef void (*GFX_THREAD_QUEUE_CALLBACK)(uint32_t taskID, void* userData);
    typedef void (*GFX_THREAD_ENQUEUE_JOB)(GFX_THREAD_QUEUE_CALLBACK, uint32_t taskID, void* userData);
    typedef void (*GFX_THREAD_WAIT)();
    typedef Blob (*GLVU_RESOURCE_LOADER)(ResourceKind resKind, const char* path);
    typedef void (*GLVU_LOGGER)(const char* msg, int level);
    typedef void (*RENDER_CALLBACK)(RenderScript* script, View* view, float* params, uint32_t numParams, void* userData);

	/// Filled by device query, the end-user will only create instances when they personally want to test device capability.
	/// When it is used for a query the features that are "don't care" should be set to UINT_MAX or FALSE as appropriate for the data-type.
    struct GLVU_API GraphicsFeatures
    {
        uint32_t maxUBOSize_ = 256;
        uint32_t minUBOAlignment_ = 256;
        bool compute_ = false;
        bool geometryShader_ = false;
        bool tessellation_ = false;
        bool transformFeedback_ = false;
        bool clipControl_ = false;
        bool shaderStorageBuffer_ = false;
        bool multipleHeads_ = false;
        bool multiview_ = false;
        bool nvapi_ = false;
        bool variableRateShading_ = false;
    };

	/// Held by the device so it can query the appropriate defaults when a value is not specified.
    struct GLVU_API GraphicsDefaults
    {
        /// Which texture filter mode to fallback on by default.
        TextureFilter textureFilter_ = FILTER_LINEAR;
        /// Default to wrapping texture coordinates or not.
        bool textureWrap_ = true;
        /// MSAA level, currently not supported since the library is sort of deferred rendering focused.
        unsigned MSAA_ = 1;
        /// Level of anisotropy to use.
        float anisotropy_ = 4.0f;
        /// Whether to use multithreaded command-buffer construction for RenderScript::DrawBatches (if possible, Vulkan).
        bool threadDrawBatches_ = true;
        /// Whether to use multithreaded command-buffer construction for render-script stage recording (if possible, Vulkan).
        bool threadStages_ = false;
    };

    enum GraphicsStat
    {
        STAT_BATCHES,
        STAT_SHADOWMAPS,
        STAT_PRIMITIVES,
        STAT_INSTANCES,
        STAT_COUNT
    };

	struct GLVU_API ComputeTask
	{
		struct WriteTarget {
			std::shared_ptr<Texture> texture_;
			uint32_t layer_; // use UINT_MAX for all layers
			uint32_t mip_;
			uint32_t bindSlot_;
		};
		std::shared_ptr<Shader> computeProgram_;
		Material::UBOBindingList constBuffers_;
		Material::UBOBindingList readBuffers_;
		Material::UBOBindingList writeBuffers_;
		Material::TextureBindingList readTextures_;
		std::vector<WriteTarget> writeTextures_;
		uint32_t dispatch_[3];
        uint32_t blockMask_ = ComputeBlock_None;
	};

    class GLVU_API GraphicsDevice
    {
    public:
        GraphicsDevice();
        ~GraphicsDevice();

#if defined(GLVU_VK)
        bool OpenDevice(const char** requiredExt, uint32_t);
        bool InitSurface(uint32_t width, uint32_t height, VkSurfaceKHR surface);
#elif defined(GLVU_D3D12)
        bool OpenDevice(const char** requiredExt, uint32_t);
#elif defined(GLVU_GL) || defined(GLVU_GLES3)
        bool OpenDevice(const char** requiredExt, uint32_t);
        void InitSurface(uint32_t width, uint32_t height);
#elif defined(GLVU_DX11)
		bool OpenDevice(const char** requiredExt, uint32_t);
		void InitSurface(uint32_t width, uint32_t height, HWND hwnd);
#endif
        void Shutdown();

        void SetCallbacks(GLVU_RESOURCE_LOADER loader, GLVU_LOGGER log) {
            loader_ = loader;
            logger_ = log;
        }

        void SetThreading(int numThreads, GFX_THREAD_ENQUEUE_JOB queueFunc, GFX_THREAD_WAIT waitFunc) {
            numThreads_ = numThreads;
            jobPusher_ = queueFunc;
            jobWait_ = waitFunc;
        }

        void OnResize(uint32_t width, uint32_t height);

        void BeginFrame();
        void EndFrame();

        std::shared_ptr<Texture> CreateTexture(TextureTraits);
        void UpdateTexture(Texture*, void* data, uint32_t width, uint32_t height, uint32_t depth, uint32_t mip, uint32_t layer);
        void UpdateSubTexture(Texture*, void* data, uint32_t x, uint32_t y, uint32_t z, uint32_t width, uint32_t height, uint32_t depth, uint32_t mip, uint32_t layer);
        std::shared_ptr<FrameBuffer> CreateFrameBuffer(const std::vector< std::shared_ptr<Texture> >& textures);

#if defined(GLVU_GLES3)
        /// GLES 3.0 doesn't have layered render-targets. GLES3.2 does.
        std::shared_ptr<FrameBuffer> CreateFrameBuffer(const std::shared_ptr<Texture>& texture, uint32_t layer, bool wantDepth);
#endif

        std::shared_ptr<Buffer> CreateVertexBuffer();
        std::shared_ptr<Buffer> CreateIndexBuffer();
        std::shared_ptr<Buffer> CreateUniformBuffer();
        std::shared_ptr<Buffer> CreateShaderStorageBuffer();
        std::shared_ptr<Buffer> CreateIndirectArgsBuffer();
        void UpdateBuffer(Buffer*, void* data, uint32_t size);

        std::shared_ptr<GeometryLayout> CreateGeometryLayout();

        std::shared_ptr<FrameBuffer> GetBackbuffer() const { return backbuffer_; }

        Blob GetResourceData(ResourceKind kind, const char* path);

        std::shared_ptr<Shader> GetShader(ShaderType type, const char* fileName, const std::vector<std::string>& defines);
        std::shared_ptr<Effect> GetEffect(const std::string& name);
        std::shared_ptr<Buffer> GetScratchUniformBuffer(size_t desiredSize);
        std::shared_ptr<Buffer> GetScratchVertexBuffer(size_t desiredSize);

        std::shared_ptr<Texture> GetDefaultTexture() const { return defaultTexture_; }

        std::shared_ptr<GeometryLayout> GetLayout_Pos() const;
        std::shared_ptr<GeometryLayout> GetLayout_PosUV() const;
        std::shared_ptr<GeometryLayout> GetLayout_PosNormUV() const;
        std::shared_ptr<GeometryLayout> GetLayout_PosUVColor() const;
        std::shared_ptr<GeometryLayout> GetLayout_PosColor() const;
        std::shared_ptr<GeometryLayout> GetLayout_2D() const;

        std::shared_ptr<Buffer> GetSystemBuffer(const char* name) const;
        std::shared_ptr<Texture> GetSystemTexture(const char* name) const;
        void AddSystemBuffer(const char* name, std::shared_ptr<Buffer>);
        void AddSystemTexture(const char* name, std::shared_ptr<Texture>);
        void RemoveSystem(const char* name);
        void RemoveTexture(const char* name);

        std::shared_ptr<Shader> GetFSTriVertexShader() const { return fsTriVertexShader_; }
        std::shared_ptr<Geometry> GetFSTriGeometry() const { return fsTriGeometry_; }
        std::shared_ptr<Geometry> GetFSQuadGeometry() const { return fsQuadGeometry_; }
        std::shared_ptr<Geometry> GetPointLightGeometry() const { return pointLightGeometry_; }
        std::shared_ptr<Geometry> GetSpotLightGeometry() const { return spotLightGeometry_; }

        std::shared_ptr<Effect> GetDeferredLightEffect() const { return deferredLightEffect_; }
        std::shared_ptr<Effect> GetDeferredDecalEffect() const { return deferredDecalEffect_; }
        std::shared_ptr<Effect> GetGUIEffect() const { return guiEffect_; }
		std::shared_ptr<Effect> GetPassThruEffect() const { return passThruEffect_; }

        void LogMessage(const char*, int level);
        void LogFormat(int level, const char*, ...);

        int GetNumThreads() const { return numThreads_; }
        bool ThreadingIsSupported() const { return numThreads_ > 1; }
        void PushThreadJob(uint32_t taskID, void* userData, GFX_THREAD_QUEUE_CALLBACK worker) { if (jobPusher_) { jobPusher_(worker, taskID, userData); } else { worker(taskID, userData); } }
        void WaitForThreadJobs() { if (jobWait_) jobWait_(); }

        inline uint32_t GPU_MaxUBOSize() const { return graphicsFeatures_.maxUBOSize_; }
        inline uint32_t GPU_MinUBOAlignment() const { return graphicsFeatures_.minUBOAlignment_; }

        const GraphicsFeatures& GetGPUFeatures() const { return graphicsFeatures_; }
        GraphicsDefaults& GetDefaults() { return graphicsDefaults_; }
        const GraphicsDefaults& GetDefaults() const { return graphicsDefaults_; }
		bool SatisfiesFeatures(const GraphicsFeatures&) const;

		ShaderCache* GetShaderCache() const { return shaderCache_.get(); }
        void LogStats();
		void ReloadSystemShaders();

        void RegisterCallback(const char* id, RENDER_CALLBACK, void* userData);
        void RemoveCallback(const char* id);
        void InvokeCallback(const char* id, RenderScript* script, View* view, float* params, uint32_t paramCt);

        uint32_t GetStat(GraphicsStat s) const { return stats_[s]; }
        void AddStat(GraphicsStat s, uint32_t ct) { stats_[s] += ct; }

		void ExecuteCompute(const ComputeTask&, bool block);

#if defined(GLVU_VK)
    public:
        inline VkInstance GetVKInstance() { return instance_; }
        inline VkDevice GetVKDevice() { return device_; }
        inline VezSwapchain GetSwapchain() { return swapchain_; }
        inline VkQueue GetGraphicsQueue() { return graphicsQueue_; }
        inline VkPhysicalDevice GetPhysicalDevice() { return physicalDevice_; }
        VkSampler GetSampler(TextureFilter filterMode, bool wrap) { return wrap ? wrapSamplers_[filterMode] : clampSamplers_[filterMode]; }

        VkCommandBuffer GetGraphicsCmdBuffer();
        VkCommandBuffer GetComputeCmdBuffer();

    private:
        VkInstance instance_;
        VkPhysicalDevice physicalDevice_;
        VkDevice device_;
        VkCommandBuffer mainCmdBuffers_[4];
        VezSwapchain swapchain_;
        VkSurfaceKHR surface_;
        VkQueue graphicsQueue_;
        
        VkSampler clampSamplers_[COUNT_TEXTURE_FILTER];
        VkSampler wrapSamplers_[COUNT_TEXTURE_FILTER];

        std::unique_ptr<CommandBufferPool> graphicsCommandPool_;
        std::unique_ptr<CommandBufferPool> computeCommandPool_;

#elif defined(GLVU_GL) || defined(GLVU_GLES3)
    public:
        GLuint GetSampler(TextureFilter filterMode, bool wrap) { return wrap ? wrapSamplers_[filterMode] : clampSamplers_[filterMode]; }
        GLState& GetGLState() { return glState_; }

    private:
        GLuint vao_;
        GLuint clampSamplers_[COUNT_TEXTURE_FILTER];
        GLuint wrapSamplers_[COUNT_TEXTURE_FILTER];
        GLState glState_;
#elif defined(GLVU_D3D12)
    private:
        ID3D12Device* device_ = nullptr;
        ID3D12CommandQueue* commandQueue_ = nullptr;
        D3D12MA::Allocator* amdAllocator_ = nullptr;
    public:
        ID3D12Device* GetD3D12() { return device_; }
        ID3D12CommandQueue* GetCmdQueue() { return commandQueue_; }
        ID3D12GraphicsCommandList* GetCmdList() { return nullptr; }
        D3D12MA::Allocator* GetAlloc() { return amdAllocator_; }
        ID3D12GraphicsCommandList* GetGraphicsCmdBuffer();
        ID3D12CommandList* GetComputeCmdBuffer();
#elif defined(GLVU_DX11)
	public:
		ID3D11Device* GetD3DDevice() const { return d3dDevice_; }
        ID3D11Device1* GetD3DDevice1() const { return d3dDevice1_; }
		ID3D11DeviceContext* GetD3DContext() const { return d3dContext_; }
		ID3D11DeviceContext1* GetD3DContext1() const { return d3dContext1_; }
		ID3D11SamplerState* GetSampler(TextureFilter filterMode, bool wrap) { return wrap ? wrapSamplers_[filterMode] : clampSamplers_[filterMode]; }
		DX11StateCache* GetDX11State() { return dx11StateCache_; }
	private:

		ID3D11Device* d3dDevice_ = nullptr;
		ID3D11DeviceContext* d3dContext_ = nullptr;
        ID3D11Device1* d3dDevice1_ = nullptr;
		ID3D11DeviceContext1* d3dContext1_ = nullptr;
        ID3DUserDefinedAnnotation* d3dAnnote_ = nullptr;
		ID3D11SamplerState* clampSamplers_[COUNT_TEXTURE_FILTER];
		ID3D11SamplerState* wrapSamplers_[COUNT_TEXTURE_FILTER];

		ID3D11RasterizerState* blendStates_[COUNT_BLEND_MODE];
		ID3D11DepthStencilState* depthStencilStates_[COUNT_COMPARISON];
		DX11StateCache* dx11StateCache_;
#endif

        // Callback functions for interacting with us.
        GFX_THREAD_ENQUEUE_JOB jobPusher_ = nullptr;
        GFX_THREAD_WAIT jobWait_ = nullptr;
        GLVU_RESOURCE_LOADER loader_ = nullptr;
        GLVU_LOGGER logger_ = nullptr;
        int numThreads_ = 0;

    public:
        std::shared_ptr<Buffer> GetFullscreenTriVertices() const { return fullscreenTriVertices_; }
        /// An index buffer that is 0, 1, 2, 3 ... and so on
        std::shared_ptr<Buffer> GetSequentialIndexBuffer() const { return sequentialIdxBuff_; }

        GraphicsDeviceHead* GetActiveDeviceHead() const { return activeHead_; }

    private:
        friend class GraphicsDeviceHead;
        void SetBackbuffer(std::shared_ptr<FrameBuffer> backbuff) { backbuffer_ = backbuff; }

    private:
        void CreateDefaultObjects();
        void PlatformShutdown();

        GraphicsFeatures graphicsFeatures_;
        GraphicsDefaults graphicsDefaults_;

        GraphicsDeviceHead* activeHead_;
        std::shared_ptr<FrameBuffer> backbuffer_;

        std::shared_ptr<Texture> defaultTexture_;
        std::map<std::string, std::shared_ptr<Buffer> > systemBuffers_;
        std::map<std::string, std::shared_ptr<Texture> > systemTextures_;
        std::shared_ptr<Buffer> defaultVertexBuffer_;
        std::shared_ptr<Buffer> defaultIndexBuffer_;

        std::shared_ptr<Buffer> fullscreenTriVertices_;
        std::shared_ptr<Buffer> fullscreenQuadVertices_;
        std::shared_ptr<Buffer> sequentialIdxBuff_;
        std::shared_ptr<Shader> fsTriVertexShader_;
        
        std::shared_ptr<Geometry> fsTriGeometry_;
        std::shared_ptr<Geometry> fsQuadGeometry_;
        std::shared_ptr<Geometry> pointLightGeometry_;
        std::shared_ptr<Geometry> spotLightGeometry_;
        
        std::shared_ptr<Effect> deferredLightEffect_;
        std::shared_ptr<Effect> deferredDecalEffect_;
        std::shared_ptr<Effect> guiEffect_;
		std::shared_ptr<Effect> passThruEffect_;

        // Standard vertex layouts
        std::shared_ptr<GeometryLayout> layoutPos_;
        std::shared_ptr<GeometryLayout> layoutPosUV_;
        std::shared_ptr<GeometryLayout> layoutPosColor_;
        std::shared_ptr<GeometryLayout> layoutPosNormUV_;
        std::shared_ptr<GeometryLayout> layoutPosUVColor_;
        std::shared_ptr<GeometryLayout> layout2D_;

        struct CallbackRecord {
            RS_Identifier id;
            RENDER_CALLBACK call;
            void* userData;
        };

        // Callbacks
        std::vector< CallbackRecord > renderCallbacks_;
        
        // Caches
        EffectCache effectCache_;
        std::unique_ptr<ShaderCache> shaderCache_;
        std::unique_ptr<ScratchBufferCache> uboCache_;

        uint32_t backbufferWidth_;
        uint32_t backbufferHeight_;
        uint32_t stats_[STAT_COUNT];
    };

    class GLVU_API ScratchBufferCache : public GPUObject
    {
    public:
        ScratchBufferCache(GraphicsDevice* device);
        ~ScratchBufferCache();

        virtual bool IsValid() const override { return true; }
        virtual void Release() override;

        /// <Allocated, Remaining> ... Used = Allocated - Remaining
        std::pair<size_t,size_t> GetUniformBufferAllocation() const;
        /// <Allocated, Remaining> ... Used = Allocated - Remaining
        std::pair<size_t,size_t> GetVertexBufferAllocation() const;

        std::shared_ptr<Buffer> GetBuffer(size_t desiredSize);
        std::shared_ptr<Buffer> GetVertexBuffer(size_t desiredSize);
        void FrameFinished();

    private:
        typedef std::pair<size_t, std::shared_ptr<Buffer> > Record;
        struct BufferGroup {
            std::vector<Record> existing_;
            std::vector<Record> remaining_;
        };
        BufferGroup uniformBuffercache_;
        BufferGroup vertexBufferCache_;

        std::shared_ptr<Buffer> GetBuffer(BufferGroup& cache, BufferKind kind, size_t desiredSize);
    };

#if defined(GLVU_VK)
    class CommandBufferPool : public GPUObject
    {
    public:
        CommandBufferPool(GraphicsDevice*, bool forGraphics);
        ~CommandBufferPool();

        virtual bool IsValid() const override { return true; }
        virtual void Release() override;

        VkCommandBuffer Get();
        void Reset();

    private:
        struct PoolData {
            std::vector<VkCommandBuffer> available_;
            std::vector<VkCommandBuffer> total_;
            std::vector<VkCommandBuffer> dished_;
        };

        std::map<std::thread::id, PoolData*> pools_;
        bool forGraphics_;
        std::mutex mutex_;
    };
#endif

}