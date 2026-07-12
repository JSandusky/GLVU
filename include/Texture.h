//****************************************************************************
//
//  File:       Texture.h
//  License:    MIT
//  Project:    GLVU
//  Contents:   Wrapper object for 2d, 3d, Cube, and array textures.
//
//****************************************************************************

#pragma once

#include "glvu.h"

namespace GLVU
{

    class Buffer;
    class GraphicsDevice;
    class FrameBuffer;

    class GLVU_API Texture : public GPUObject
    {
        friend class GraphicsDevice;

        Texture(GraphicsDevice*);
    public:
        virtual ~Texture();

        virtual void Release() override;
        virtual bool IsValid() const override;

        void SetData(void* data, uint32_t width, uint32_t height, uint32_t depth, uint32_t mip, uint32_t layer);
        void SetSubData(void* data, uint32_t x, uint32_t y, uint32_t z, uint32_t width, uint32_t height, uint32_t depth, uint32_t mip, uint32_t layer);
        inline TextureKind GetTextureKind() const { return traits_.kind_; }
        inline TextureFormat GetFormat() const { return traits_.format_; }
        inline uint32_t GetWidth() const { return traits_.width_; }
        inline uint32_t GetHeight() const { return traits_.height_; }
        inline uint32_t GetDepth() const { return traits_.depth_; }
        inline uint32_t GetLayers() const { return traits_.layers_; }
        inline uint32_t GetMips() const { return traits_.mips_; }
        inline bool IsMSAA() const { return traits_.samples_ > 1; }
        inline uint32_t GetSamples() const { return traits_.samples_; }

		static int NumLevels(int x, int y, int z);
#ifdef GLVU_D3D12
        bool Create(const TextureTraits& traits);
#endif

        /// Loads a 2D texture of a format supported by stb_image or DDS_KTX
        static std::shared_ptr<Texture> LoadFile(GraphicsDevice* device, const char* fileName, bool wantMips = true);
        /// For non-DDS/KTX files, 1D array of luts. Each row is a LUT.
        static std::shared_ptr<Texture> LoadArrayLUT(GraphicsDevice* device, const char* fileName);
        /// For non-DDS/KTX files, loads a 2d texture array where each image is Height x Height.
        static std::shared_ptr<Texture> LoadArrayStrip(GraphicsDevice* device, const char* fileName, bool wantMips = true);
        /// For non-DDS/KTX files, 3d texture arranged in left->right array, such as seen for color-adjustment 3D LUTs
        static std::shared_ptr<Texture> Load3DLUT(GraphicsDevice* device, const char* fileName);
        /// For non-DDS/KTX files, loads an stb_image supported format into a layer of an array texture.
        static bool LoadFileToLayer(std::shared_ptr<Texture> texture, const char* fileName, uint32_t layer);

        void GenerateMipMaps();

#ifdef GLVU_GL
        inline bool IsTextureBuffer() const { return buffer_ != 0; }
#else
        inline bool IsTextureBuffer() const { return false; }
#endif
        inline bool IsCompressed() const { 
            return traits_.format_ == TEX_DXT1 || traits_.format_ == TEX_DXT3 || traits_.format_ == TEX_DXT5 || 
                traits_.format_ == TEX_BC4 || traits_.format_ == TEX_BC5; 
        }

        const Texture* GetReadable() const {
            if (resolveTexture_)
            {
                if (resolveDirty_)
                    Resolve();
                return resolveTexture_.get();
            }
            return this;
        }

        std::shared_ptr<Texture> GetResolveTexture() const { return resolveTexture_; }

    #if defined(GLVU_GL)
    public:
        const GLenum GetTarget() const;
        const GLuint GetGPUObject() const { return texture_; }
        const GLuint GetTextureBuffer() const { return buffer_; }
    private:
        GLuint texture_;
        GLuint buffer_;
    #elif defined(GLVU_VK)
    public:
        VkImage GetImage() const { return image_; }
        VkImageView GetView() const { return view_; }
        VkBuffer GetTextureBuffer() const { return buffer_; }
        VkBufferView GetTextureBufferView() const { return bufferView_; }
    private:
        VkImage image_;
        VkImageView view_;
        VkBuffer buffer_;
        VkBufferView bufferView_;
    #elif defined(GLVU_PICA)
    #elif defined(GLVU_DX11)
        ID3D11Resource* texture_ = nullptr;
		ID3D11ShaderResourceView* srv_ = nullptr;
		ID3D11UnorderedAccessView* GetUAV(uint32_t layer, uint32_t mip);
		struct UAVHandle {
			ID3D11UnorderedAccessView* uav_;
			uint32_t layer_;
			uint32_t mip_;
		};
		std::vector<UAVHandle> uavs_;
    #else
        ID3D12Resource* texture_;
        ID3D12Resource* buffer_;
        D3D12MA::Allocation* textureMem_;
        D3D12_RESOURCE_DESC creationDesc_;

        bool CopyFromBuffer(const std::shared_ptr<Buffer>&);
    #endif

    private:
        void Resolve() const;

        std::shared_ptr<Texture> resolveTexture_;
        mutable bool resolveDirty_ = false;
        TextureTraits traits_;
    };

    class GLVU_API FrameBuffer : public GPUObject
    {
        friend class GraphicsDevice;
        friend class GraphicsDeviceHead;
    public:
        /// Construct from a list of 2d textures.
        FrameBuffer(GraphicsDevice* device, const std::vector<std::shared_ptr<Texture> >& textures);
        /// Construct from layers of a cube/texture-array.
        FrameBuffer(GraphicsDevice* device, const std::shared_ptr<Texture>& textures, int layer = -1);
        virtual ~FrameBuffer();

        /// For GL. Vulkan has context specific needs.
        virtual void Bind();
        virtual bool IsValid() const override;
        virtual void Release() override;

        uint32_t GetWidth() const;
        uint32_t GetHeight() const;

        inline std::shared_ptr<Texture> GetTexture(uint32_t idx) const { return textures_[idx]; }
        inline uint32_t GetTextureCount() const { return (uint32_t)textures_.size(); }

        void Clear(const float* color = nullptr, bool depth = true, bool stencil = true);
        inline void MarkDrawn() { dirty_ = true; }
        inline bool IsDirty() const { return dirty_; }

        bool UsesTexture(const std::shared_ptr<Texture>& t) const {
            for (auto& tex : textures_)
            {
                if (tex == t)
                    return true;
            }
            return false;
        }

#if defined(GLVU_GL) || defined(GLVU_GLES3)
    public:
        inline GLuint GetGPUObject() { return fbo_; }
    private:
        GLuint fbo_;
#elif defined(GLVU_VK)
    public:
        inline VezFramebuffer GetGPUObject() { return fbo_; }
    private:
        VezFramebuffer fbo_;
        VkImageView ownedView_;
        std::shared_ptr<Texture> ownedDepth_;
#elif defined(GLVU_DX11)
	public:
		const std::vector<ID3D11RenderTargetView*>& GetViews() const { return views_; }
		ID3D11DepthStencilView* GetDepthStencilView() const { return depthView_; }
		void FromBackBuffer(ID3D11RenderTargetView* v, ID3D11DepthStencilView* ds) { views_.push_back(v); depthView_ = ds; }
	private:
		/// Extra resources that we don't care about access to aside from release (such as from a swapchain).
		std::vector<ID3D11Resource*> extraResources_;
		std::vector<ID3D11RenderTargetView*> views_;
		ID3D11DepthStencilView* depthView_ = nullptr;

		void Create();
#endif
        std::vector<std::shared_ptr<Texture> > textures_;
        uint32_t reportWidth_;
        uint32_t reportHeight_;
		int mipLevel_ = 0;
        int layer_ = -1;
        bool dirty_ = true;
    };

}