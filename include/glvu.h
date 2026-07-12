//****************************************************************************
//
//  File:       glvu.h
//  License:    MIT
//  Project:    GLVU
//  Contents:   Core header for GLVU renderer.
//
//****************************************************************************

#pragma once

#include <stdint.h>

#if defined GLVU_EXPORTS
    #if _WIN32
        #define GLVU_API __declspec(dllexport)
    #else
    #endif
#elif defined GLVU_IMPORTS
    #if _WIN32
        #define GLVU_API __declspec(dllimport)
    #else
    #endif
#else
    #define GLVU_API
#endif

#if defined(GLVU_GL) || defined(GLVU_GLES)
    #include <GLEW/GL/glew.h>
#endif

#if defined(GLVU_DX11)
	#define NOMINMAX
	#define WIN32_LEAN_AND_MEAN
    #include <d3d11.h>
	#include <d3d11_1.h>
	#include <dxgi.h>
	#define DX_RELEASE(V) if (V) { V->Release(); V = nullptr; }
#endif

#if defined(GLVU_D3D12)
    #define NOMINMAX
    #include <wrl.h>
    #include <d3d12.h>
    #include "D3D12x.h"
    #include "D3D12MemAlloc.h"
    #undef CM_NONE

#define ThrowIfFailed(STATEMENT) { auto hr = (STATEMENT); if (hr != S_OK) throw hr; }
#endif

#if defined(GLVU_PICA)
    #include <libctru.h>
    #include <citro3d.h>
#endif

#if defined(GLVU_VK)
    #include <vulkan/vulkan.h>
    #include <VEZ/VEZ.h>
    #include <VEZ/VEZ_ext.h>
#endif

#if defined memcpy
	#undef memcpy
#endif

#include <MathGeoLib/MathGeoLibFwd.h>
#include <MathGeoLib/Math/MathAll.h>
#include <MathGeoLib/Geometry/GeometryAll.h>

#define GLVU_BITFIELD(VALUE) (1 << (uint32_t)(VALUE))

#include <cassert>
#if defined(_DEBUG)
    #define VERIFY(EXPR) assert(EXPR)
    //#include <assert.h>
#else
    //#define assert(EXPR) ((void)0)
    #define VERIFY(EXPR) (EXPR)
#endif

#include <algorithm>
#include <memory>
#include <vector>

#define GLVU_INFO 0
#define GLVU_WARNING 1
#define GLVU_ERROR 2

namespace GLVU
{
    class GraphicsDevice;

    enum PrimitiveType
    {
        TRIANGLE_LIST,
        POINT_LIST,
        LINE_LIST,
        TRIANGLE_ADJ,
        LINE_ADJ,
        PRIM_UNKNOWN
    };
    PrimitiveType ParsePrimitiveType(const char*);

    enum FillMode
    {
        FILL_SOLID,
        FILL_WIRE
    };
    FillMode ParseFillMode(const char*);

    enum CullingMode
    {
        CULL_NONE,
        CULL_FRONT,
        CULL_BACK
    };
    CullingMode ParseCullingMode(const char*);

    enum BufferKind
    {
        VertexBufferObject,
        IndexBufferObject,
        UniformBufferObject,
        ShaderDataBufferObject,
        IndirectArgsBufferObject,
        ByteAddressBuffer,
        COUNT_BUFFER_KIND
    };
    BufferKind ParseBufferKind(const char*);
    const char* BufferKindToString(BufferKind);

    enum BufferTag
    {
        BufferTag_None = 0,
        // Index Buffer tags
        BufferTag_32Bit = GLVU_BITFIELD(0),
        BufferTag_Triangles = GLVU_BITFIELD(1),
        BufferTag_Lines = GLVU_BITFIELD(2),
        BufferTag_Points = GLVU_BITFIELD(3),
        BufferTag_TriangleAdjacency = GLVU_BITFIELD(4),
        // Vertex buffer tags
        BufferTag_InstanceTransform = GLVU_BITFIELD(5),
        BufferTag_InstanceExtra = GLVU_BITFIELD(6),
        // Uniform buffer tags
        BufferTag_MaterialUBO = GLVU_BITFIELD(7),
        // General Tags
        BufferTag_Output = GLVU_BITFIELD(8),
        BufferTag_Dynamic = GLVU_BITFIELD(9),
        // Generate a UAV for the given buffer. Some restrictions apply.
        BufferTag_Compute = GLVU_BITFIELD(10),
        BufferTag_AppendConsume = GLVU_BITFIELD(11), // OpenGL backends will create an additional counter buffer.
        BufferTag_Readback = GLVU_BITFIELD(12),
        BufferTag_Staging = GLVU_BITFIELD(13),
    };
    BufferTag ParseBufferTag(const char*);

    enum TextureKind
    {
        Texture2D,
        Texture3D,
        TextureCube,
        Texture2DArray,
        TextureCubeArray,
        Texture1D,
        TextureBuffer,
        COUNT_TEXTURE_KIND
    };
    TextureKind ParseTextureKind(const char*);

    enum TextureFormat
    {
        TEX_RGB8,
        TEX_RGBA8,
        TEX_RGBA16F,
        TEX_RG16F,
        TEX_DXT1,
        TEX_DXT3,
        TEX_DXT5,
        TEX_BC4,
        TEX_BC5,
        TEX_SHADOW16,
        TEX_SHADOW32,
        TEX_DEPTH,
        TEX_BGRA8,
        TEX_R32F,
        TEX_RG16U,
        TEX_RGBA16U,
        TEX_R32U,
        TEX_RGBA8U,
        TEX_R8U,
        COUNT_TEXTURE_FORMAT
    };
    TextureFormat ParseTextureFormat(const char*);
    const char* TextureFormatToString(TextureFormat);
    bool IsShadow(TextureFormat fmt);
    bool IsDepth(TextureFormat fmt);
    bool IsComputeWriteable(TextureFormat fmt);

    enum TextureFilter
    {
        FILTER_POINT,
        FILTER_LINEAR,
        FILTER_TRILINEAR,
        FILTER_ANISOTROPIC,
        FILTER_SHADOW,
        COUNT_TEXTURE_FILTER
    };
    TextureFilter ParseTextureFilter(const char*);
    const char* TextureFilterToString(TextureFilter);

    // 64-bytes per instance. Typically stored in a vertex-buffer.
    struct BatchTransform
    {
        math::float3x4 matrix_;
        // Merge-instancing data.
        uint32_t vertexStart_ = 0;
        uint32_t indexCount_ = 0;
        uint32_t slot_ = 0;
        uint32_t variant_ = 0;

        BatchTransform() { }
        BatchTransform(const math::float3x4& mat) : matrix_(mat) { }
        BatchTransform(const math::float3x4& mat, uint32_t vStart, uint32_t indexCount, uint32_t slot, uint32_t var) :
            matrix_(mat),
            vertexStart_(vStart),
            indexCount_(indexCount),
            slot_(slot),
            variant_(var)
        {

        }
    };

    struct TextureTraits
    {
        TextureKind kind_ = Texture2D;
        TextureFormat format_ = TEX_RGBA8;
        uint32_t width_ = 1;
        uint32_t height_ = 1;
        uint32_t depth_ = 1; // default for 2D
        uint32_t layers_ = 1; // default for non-array
        uint32_t mips_ = 1; // default for no mips
        uint8_t samples_ = 1; // number of MSAA samples
        bool colorAttachment_ = false;
        bool depthAttachment_ = false;
		bool autoMip_ = false;
    };

    struct SamplerTraits
    {
        TextureFilter filter_ = FILTER_POINT;
        bool wrap_ = true;

        inline bool operator==(const SamplerTraits& rhs) const { return wrap_ == rhs.wrap_ && filter_ == rhs.filter_; }
        inline bool operator!=(const SamplerTraits& rhs) const { return wrap_ != rhs.wrap_ || filter_ != rhs.filter_; }
    };

    enum TextureUsage
    {
        TU_DIFFUSE,
        TU_NORMALMAP,
        TU_DISPLACEMENTMAP,
        TU_ROUGHNESS,
        TU_METALNESS,
        TU_SPECULAR_COLOR,
        TU_SPECULAR_POWER
    };

    enum VertexDataType
    {
        VDT_FLOAT,
        VDT_UBYTE,  // only supports 4 components
        VDT_UINT,
        VDT_HALF,   // only supports 1, 2, and 4 components
        VDT_SBYTE   // only supports 4 components
    };

    enum VertexAttribute
    {
        VA_POSITION,  // vector3
        VA_NORMAL,    // vector3
        VA_TANGENT,   // vector4
        
        VA_TEXCOORD0, // vector2
        VA_TEXCOORD1, // vector2
        VA_TEXCOORD3, // vector2

        VA_3DCOORD0, // vector3
        VA_3DCOORD1, // vector3
        VA_3DCOORD3, // vector3

        VA_SINGLE0,  // float
        VA_SINGLE1,  // float
        VA_SINGLE2,  // float

        VA_COLOR0,   // uint packed RGBA
        VA_COLOR1,   // uint packed RGBA
        VA_COLOR2,   // uint packed RGBA

        VA_INSTANCE, // vector4
        VA_BONEINDICES, // ubyte4, probably
        VA_BONEWEIGHTS, // vector4, probably

        VA_UNKNOWN   // we have no idea what this is
    };

    enum ShaderCodeType
    {
        SCT_GLSL,
        SCT_SPIRV,
        SCT_PICA_ASM,
        COUNT_SHADER_CODE_TYPE
    };

    enum ShaderType
    {
        VertexShader,
        PixelShader,
        GeometryShader,
        HullShader,
        DomainShader,
        ComputeShader,
        COUNT_SHADER_TYPE
    };
	ShaderType ParseShaderType(const char*);
	const char* ShaderTypeToString(ShaderType);

    enum SortMode
    {
        ContextSwitch,      // sort by material
        ContextAndDepth,    // sort first by material, then stable sort by depth
        FrontToBack,        // sort by depth near -> far
        BackToFront,        // sort by depth far -> near (ie. alpha blend)
        COUNT_SORT_MODE
    };
    SortMode ParseSortMode(const char*);

    enum Comparison
    {
        COMPARE_EQUAL,
        COMPARE_LEQUAL,
        COMPARE_GEQUAL,
        COMPARE_LESS,
        COMPARE_GREATER,
        COMPARE_NOT_EQUAL,
        COMPARE_ALWAYS,
        COMPARE_NEVER,
		COUNT_COMPARISON
    };
    Comparison ParseComparison(const char*);

    enum BlendMode
    {
        Blend_None,
        Blend_Alpha,
        Blend_Add,
        Blend_Subtract,
        Blend_Mul,
        Blend_Premultiplied,
        Blend_OITMixer, // [0] = GL_ONE, GL_ONE, [1] = GL_ZERO, GL_ONE_MINUS_SRC_ALPHA
        Blend_OITComposite, // [0] = GL_ONE_MINUS_SRC_ALPHA, GL_SRC_ALPHA
        COUNT_BLEND_MODE
    };
    BlendMode ParseBlendMode(const char*);

    enum ViewFlag
    {
        ViewFlag_None = 0,
        ViewFlag_IsRoot = GLVU_BITFIELD(0), // Considered a final possibly-composite view, gets pushed to back of the line
        ViewFlag_PointShadows = GLVU_BITFIELD(1), // Enable point-light shadows (dual paraboloid)
        ViewFlag_DirectionalShadows = GLVU_BITFIELD(2), // 
        ViewFlag_SpotShadows = GLVU_BITFIELD(3),
        ViewFlag_VR = GLVU_BITFIELD(4),
        ViewFlag_ReverseZ = GLVU_BITFIELD(5),
        ViewFlag_Shadows = (ViewFlag_PointShadows | ViewFlag_DirectionalShadows | ViewFlag_SpotShadows),

        ViewFlag_Default = (ViewFlag_Shadows)
    };
    ViewFlag ParseViewFlag(const char*);

    enum ResourceKind
    {
        Resource_Raw,
        Resource_Shader,
        Resource_Texture,
        Resource_Effect,
        Resource_RenderScript,
        Resource_Material
    };

	enum ResourceDetail {
		ResourceDetail_None,
		ResourceDetail_GLSL,
		ResourceDetail_HLSL,
		ResourceDetail_SPV
	};

    enum BillboardType {
        Billboard_None,
        Billboard_Rotate,   // aligned to the camera plane
        Billboard_RotateY,  // aligned to the camera, but the particles only rotate around their Y axis - cylindrical effect
        Billboard_LookAt,   // looks at the camera position, spherical effect
        Billboard_LookAtY,  // looks at the camera position, but the particles only rotate around their Y axis - cylindircal effect
        Billboard_Direction // particle Y axis aligned along a motion-vector, particle rolled to be aligned against the view (ie. laser)
    };

    enum ComputeStage {
        ComputeStage_First,
        ComputeStage_BeforeViews,
        ComputeStage_BeforeRootViews,
        ComputeStage_End
    };

    enum ComputeBlock {
        ComputeBlock_None = 0,
        ComputeBlock_Texture = GLVU_BITFIELD(0),
        ComputeBlock_Surface = GLVU_BITFIELD(1),
    };

    struct IndirectArgs {
        uint32_t IndexCountPerInstance;
        uint32_t InstanceCount;
        uint32_t StartIndexLocation;
        int32_t BaseVertexLocation;
        uint32_t StartInstanceLocation;
    };

    uint32_t Hash(const char*);
    uint32_t Hash(const std::string&);

    /// For small IDs, particularly *local* resources such as render-targets in a render-script.
    struct GLVU_API RS_Identifier
    {
        char name_[128];
        uint32_t nameHash_;

        bool IsValid() const { return strnlen(name_, 128) > 0; }

        void UpdateHash() { nameHash_ = Hash(name_); }

        RS_Identifier& operator=(const std::string& name)
        {
            strcpy_s(name_, name.c_str());
            if (name.empty())
                nameHash_ = 0;
            else
                nameHash_ = Hash(name.c_str());
            return *this;
        }

        void operator=(const RS_Identifier& rhs) {
            memcpy(name_, rhs.name_, sizeof(char) * 128);
            nameHash_ = rhs.nameHash_;
        }

        bool operator==(const char* name) const { return strcmp(name_, name) == 0; }
        bool operator==(uint32_t hash) const { return nameHash_ == hash; }
        bool operator<(const RS_Identifier& rhs) { return nameHash_ < rhs.nameHash_; }

        static const RS_Identifier NULL_ID;
    };
    
    GLVU_API RS_Identifier MakeID(const char*);
    
    GLVU_API RS_Identifier MakeID(const std::string&);

    typedef std::vector<RS_Identifier> RS_IDList;
    typedef std::pair<uint32_t, RS_Identifier> SlottedID;
    typedef std::vector< std::pair<uint32_t, RS_Identifier > > RS_SlottedIdentifier;

    /// Root of most GPU resource types. Ensures a common destruction, validity checking and device access.
    class GLVU_API GPUObject
    {
        friend class GraphicsDevice;
    public:
        GPUObject(GraphicsDevice*);
        virtual ~GPUObject();

        virtual void Release() { }
        virtual bool IsValid() const { return false; }
        GraphicsDevice* GetDevice() const { return device_; }

    protected:
        GraphicsDevice* device_;
        std::string name_;
    };

    template<typename T>
    struct SlotMapChecker { static bool IsAlive(T& v) { return true ; } };

    template<typename T>
    struct SlotMapChecker< std::shared_ptr<T> > { static bool IsAlive(std::shared_ptr<T>& ptr) { return ptr.get() && ptr.use_count() > 1; }     };

    template<typename T>
    struct SlotMapChecker< std::weak_ptr<T> > { static bool IsAlive(std::weak_ptr<T>& ptr) { return ptr.lock() != nullptr; } };

    template<typename T>
    struct SlotMapCountTaker {  };

    template<>
    struct SlotMapCountTaker<int> {
        static void Init(int& v) { v = 0; }
        static void Mark(int& v) { v += 1; }
        static bool Demark(int& v) { v -= 1; return v <= 0; }
    };

    template<>
    struct SlotMapCountTaker<bool> {
        static void Init(bool& v) { v = false; }
        static void Mark(bool& v) { v = true; }
        static bool Demark(bool& v) { v = false; return true; }
    };

    template<class T, class COUNTER>
    struct SlotMap
    {
        struct Datum {
            T data_;
            uint32_t version_ = { };
            COUNTER taken_ = { };
        };
        static inline uint32_t ToIndex(uint64_t handle) { return (handle >> 32) - 1; }
        static inline uint64_t ToHandle(uint32_t handle, uint32_t version) { return (handle << 32) | version; }
        static inline uint32_t ToVersion(uint64_t handle) { return handle & 0xFFFFFFFF; }
    public:
        uint64_t Add(const T& obj)
        {
            if (!available_.empty())
            {
                uint32_t index = available_.back();
                const auto v = objects_[index].version_ + 1;
                objects_[index] = { obj, v, true };
                available_.pop_back();
                return ToHandle(index, v);
            }
            else
            {
                objects_.push_back({ obj, 1, true });
                return ToHandle(objects_.size()-1, 1);
            }
        }

        uint64_t Handle(const T& obj) {
            for (uint32_t i = 0; i < objects_.size(); ++i)
            {
                if (objects_[i].taken_ && objects_[i].data_ == obj)
                {
                    SlotMapCountTaker<COUNTER>::Mark(objects_[i].taken_);
                    return ToHandle(i, objects_[i].version_);
                }
            }
            return Add(obj);
        }

        void GetCompact(std::vector<T>& holder) const
        {
            for (auto& rec : objects_)
            {
                if (rec.taken_)
                    holder.push_back(rec.data_);
            }
        }

        uint64_t AddRef(uint64_t handle)
        {
            const auto h = ToIndex(handle);
            assert(h >= 0 && h < objects_.size());
            if (objects_[h].version_ != ToVersion(handle))
                return 0;

            SlotMapCountTaker<COUNTER>::Mark(objects_[h].taken_);
            return handle;
        }

        void Remove(uint64_t handle)
        {
            const auto h = ToIndex(handle);
            assert(h >= 0 && h < objects_.size());
            assert(std::find(available_.begin(), available_.end(), h) == available_.end());

            if (SlotMapCountTaker<COUNTER>::Demark(objects_[h].taken_))
            {
                objects_[h] = { T(), objects_[h].version_, false };
                available_.push_back(h);
            }
        }

        T& Get(uint64_t handle)
        {
            const auto h = ToIndex(handle);
            assert(h >= 0 && h < objects_.size());

            return objects_[h].version_ == ToVersion(handle) ? objects_[h].data_ : T();
        }

        const T& Get(uint64_t handle) const
        {
            const auto h = ToIndex(handle);
            assert(h >= 0 && h < objects_.size());
            return objects_[h].version_ == ToVersion(handle) ? objects_[h].data_ : T();
        }

        void Allocate(uint32_t count)
        {
            uint32_t curSize = objects_.size();
            objects_.resize(objects_.size() + count);
            for (uint32_t i = curSize; i < objects_.size(); ++i)
                objects_[i].taken_ = false;

            std::vector<uint32_t> newIndices(count);
            std::iota(newIndices.begin(), newIndices.end(), curSize);
            available_.insert(available_.end(), newIndices.rbegin(), newIndices.rend());
        }

        void Clean() {
            for (size_t i = 0; i < objects_.size(); ++i)
            {
                if (objects_[i].taken_ && SlotMapChecker<T>::IsAlive(objects_[i].data_))
                {
                    objects_[i].taken_ = false;
                    available_.push_back(i);
                }
            }
        }

    private:
        std::vector<Datum> objects_;
        std::vector<uint32_t> available_;
    };

    struct GLVU_API FloatDistribution {
        virtual float GetValue(float) const = 0;
    };

    struct GLVU_API VectorDistribution {
        virtual math::float4 GetValue(float) const = 0;
    };

    struct GLVU_API ConstFloatDistribution : FloatDistribution {
        float value_;
        virtual float GetValue(float) const override { return value_; }
    };

    struct GLVU_API LUTFloatDistribution : FloatDistribution {
        float table_[64];
        virtual float GetValue(float td) const override { return table_[(unsigned)std::max<float>(0.0f, std::min<float>(63.0f, td*63.0f))]; }
        void Randomize(bool sNormal = false);
    };

    struct GLVU_API ConstVectorDistribution : VectorDistribution {
        math::float4 value_;
        virtual math::float4 GetValue(float) const override { return value_; }
    };

    struct GLVU_API LUTVectorDistribution : VectorDistribution {
        math::float4 table_[64];
        virtual math::float4 GetValue(float td) const override { return table_[(unsigned)std::max<float>(0.0f, std::min<float>(63.0f, td*63.0f))]; }
        void Randomize(bool sNormal = false);
    };

    /// For Vulkan / DX12 that do not have instance advance.
    struct TransformList : private std::vector<BatchTransform> {
        typedef BatchTransform Elem;
        typedef std::vector<BatchTransform> base;

        /// Set to 2 for VR
        uint32_t pushCount_ = 1;

        void reserve(const size_t ct) {
            base::reserve(ct * pushCount_);
        }

        void push_back(const Elem& trans) {
            for (uint32_t i = 0; i < pushCount_; ++i)
                base::push_back(trans);
        }

        void emplace_back(const Elem&& trans) {
            for (uint32_t i = 0; i < pushCount_; ++i)
                base::push_back(trans);
        }

        using base::resize;
        using base::clear;
        using base::size;
        using base::data;
        using base::reserve;
    };

    /// Coalescence is used for UBOs/Images/Samplers so that something will always come through, if even just a default.
    template<typename T>
    void Coalesce(T a, T b)
    {
        if (a) return a;
        return b;
    }

    template<typename T>
    void Coalesce(T a, T b, T c)
    {
        if (a) return a;
        if (b) return b;
        return c;
    }

    template<typename T>
    void Coalesce(T a, T b, T c, T d)
    {
        if (a) return a;
        if (b) return b;
        if (c) return c;
        return d;
    }

    uint32_t PackColor(math::float4);

    // Utility wrapper.
    template<typename S, typename OP>
    bool select(const std::vector<S>& src, std::vector<S>& dest, OP op)
    {
        dest.clear();
        std::copy_if(src.begin(), src.end(), std::back_inserter(dest), op);
        return !dest.empty();
    }
}
