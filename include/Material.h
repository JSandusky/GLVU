//****************************************************************************
//
//  File:       Material.h
//  License:    MIT
//  Project:    GLVU
//
//****************************************************************************

#pragma once

#include <glvu.h>

namespace GLVU
{

    class Buffer;
    class Effect;
    class Pass;
    class Texture;

    class GLVU_API Material
    {
    public:
        Material(std::shared_ptr<Effect>);
        Material(const Material& src);
        ~Material();

        void SetShaderParameter(const char*, math::uint2);
        void SetShaderParameter(const char*, math::uint3);
        void SetShaderParameter(const char*, math::uint4);
        void SetShaderParameter(const char*, math::float2);
        void SetShaderParameter(const char*, math::float3);
        void SetShaderParameter(const char*, math::float4);
        void SetShaderParameter(const char*, math::Quat);
        void SetShaderParameter(const char*, math::float4x4);

        void CommitUniforms();

        inline bool CastShadows() const { return castShadows_; }
        inline bool ReceiveShadows() const { return receiveShadows_; }
        inline bool IsLit() const { return lit_; }

        void ApplyEffect();
        std::shared_ptr<Effect> GetEffect();

        struct TextureBinding {
            uint32_t slot_;
            SamplerTraits sampling_;
            std::shared_ptr<Texture> texture_;
        };
        typedef std::vector<TextureBinding> TextureBindingList;

        struct UBOBinding {
            uint32_t slot_;
            uint32_t startBytes_ = 0;
            uint32_t size_ = UINT_MAX;
            std::shared_ptr<Buffer> buffer_;
        };
        typedef std::vector<UBOBinding> UBOBindingList;

        inline UBOBindingList& GetUBOs() { return uniformBuffers_; }
        inline const UBOBindingList& GetUBOs() const { return uniformBuffers_; }

        inline TextureBindingList& GetTextures() { return textures_; }
        inline const TextureBindingList& GetTextures() const { return textures_; }

        bool SetUBO(uint32_t slot, std::shared_ptr<Buffer>);
        std::shared_ptr<Buffer> GetUBO(uint32_t slot);

        bool SetTexture(const char* name, std::shared_ptr<Texture> tex);
        bool SetTexture(uint32_t slot, std::shared_ptr<Texture>);
        std::shared_ptr<Texture> GetTexture(uint32_t slot);

        static std::shared_ptr<Material> Load(GraphicsDevice*, const char* fileName);
        static std::shared_ptr<Material> Load(GraphicsDevice*, const char* data, size_t dataSize);

        inline uint32_t GetViewMask() const { return viewMask_; }
        inline uint32_t GetLightMask() const { return lightMask_; }
        inline uint32_t GetShadowMask() const { return shadowMask_; }

        std::shared_ptr<Material> Clone() const;

    private:
        std::shared_ptr<Effect> effect_;
        UBOBindingList uniformBuffers_;
        TextureBindingList textures_;
        uint32_t viewMask_;
        uint32_t lightMask_;
        uint32_t shadowMask_;
        bool castShadows_;
        bool receiveShadows_;
        bool lit_;
    };

}