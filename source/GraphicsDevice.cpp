//****************************************************************************
//
//  File:       GraphicsDevice.cpp
//  License:    MIT
//  Project:    GLVU
//
//****************************************************************************

#include "GraphicsDevice.h"

#include "Effect.h"
#include "ShaderCache.h"

#include <algorithm>
#include <numeric>
#include <stdarg.h>

#include <algorithm>
#include <unordered_set>

using namespace std;
using namespace math;

namespace GLVU
{

//****************************************************************************
//
//  Function:   GraphicsDevice::Shutdown
//
//  Purpose:    Releases the default/internal/stock resources.
//
//****************************************************************************
void GraphicsDevice::Shutdown()
{
	shaderCache_->LogCacheInfo();

    defaultTexture_.reset();
    defaultIndexBuffer_.reset();
    defaultVertexBuffer_.reset();
    sequentialIdxBuff_.reset();
    backbuffer_.reset();

    layoutPos_.reset();
    layoutPosUV_.reset();
    layoutPosNormUV_.reset();
    layoutPosUVColor_.reset();
    layoutPosColor_.reset();
    layout2D_.reset();

    fullscreenTriVertices_.reset();
    fullscreenQuadVertices_.reset();
    fsTriVertexShader_.reset();
    fsTriGeometry_.reset();
    fsQuadGeometry_.reset();

    shaderCache_.reset();
    uboCache_.reset();
    effectCache_.Release();
    systemTextures_.clear();

    PlatformShutdown();
}

//****************************************************************************
//
//  Function:   GraphicsDevice::CreateGeometryLayout
//
//  Purpose:    ?? vestigial
//
//  Return:     a new geometry layout instance.
//
//****************************************************************************
shared_ptr<GeometryLayout> GraphicsDevice::CreateGeometryLayout()
{
    return make_shared<GeometryLayout>(this);
}

//****************************************************************************
//
//  Function:   GraphicsDevice::GetResourceData
//
//  Purpose:    Calls the loader interface to try to get a Blob
//
//  Return:     A blob provided by the loader, or a dead-blob if there isn't a laoder.
//
//****************************************************************************
Blob GraphicsDevice::GetResourceData(ResourceKind kind, const char* path)
{
    if (loader_)
        return loader_(kind, path);
    LogFormat(GLVU_ERROR, "Unable to load file: %s", path);
    return  { nullptr, 0, false };
}

//****************************************************************************
//
//  Function:   GraphicsDevice::LogMessage
//
//  Purpose:    Logs a raw message.
//
//****************************************************************************
void GraphicsDevice::LogMessage(const char* msg, int level)
{
    if (logger_)
        logger_(msg, level);
}

//****************************************************************************
//
//  Function:   GraphicsDevice::LogFormat
//
//  Purpose:    Logs a message with string formatting (sprintf)
//
//****************************************************************************
void GraphicsDevice::LogFormat(int level, const char* msg, ...)
{
    va_list asp;
    va_start(asp, msg);
    char buffer[4096 * 2];
    vsprintf_s(buffer, msg, asp);
    LogMessage(buffer, level);
}

//****************************************************************************
//
//  Function:   GraphicsDevice::CreateDefaultObjects
//
//  Purpose:    Constructs the GPU objects that are required. Default textures,
//              standard sequential VBOs, fullscreen buffers/shaders, and
//              stock vertex-layouts, etc.
//
//              For some reason a bunch of 1-pixel color textures are generated,
//              not presently used.
//
//****************************************************************************
void GraphicsDevice::CreateDefaultObjects()
{
	// Create Fullscreen Triangle geom
	{
		struct V {
			float3 position_;
			float2 uv_;
		};

		V triVertices[] = {
			{ { -1, -1, 0 },{ 0, 0 } },
			{ { -1,  3, 0 },{ 0, 2 } },
			{ { 3, -1, 0 },{ 2, 0 } }
		};

		V quadVertices[] = {
			{ { -1, -1, 0 }, { 0, 0 } },
			{ { -1,  1, 0 }, { 0, 1 } },
			{ {  1, -1, 0 }, { 1, 0 } },

			{ { -1,  1, 0 }, { 0, 1 } },
			{ {  1,  1, 0 }, { 1, 1 } },
			{ {  1, -1, 0 }, { 1, 0 } },
		};

		fullscreenTriVertices_ = CreateVertexBuffer();
		fullscreenTriVertices_->SetShadowed(true);
		fullscreenTriVertices_->SetData(triVertices, sizeof(triVertices));

		fullscreenQuadVertices_ = CreateVertexBuffer();
		fullscreenQuadVertices_->SetShadowed(true);
		fullscreenQuadVertices_->SetData(quadVertices, sizeof(quadVertices));
	}

    // Create internal mono-color textures: 1x1 2D textures
    {
        TextureTraits texTraits = {};
        texTraits.format_ = TEX_RGBA8;
        texTraits.width_ = texTraits.height_ = 1;

        defaultTexture_ = CreateTexture(texTraits);
        uint32_t texData = 0xFFFFFFFF;
        defaultTexture_->SetData(&texData, 1, 1, 0, 0, 0);

        uint32_t colors[] = {
            0xFF0000FF, // RED
            0x00FF00FF, // GREEN
            0x0000FFFF, // BLUE
            0xFFFFFF00, // Trans-white
            0x00000000, // Trans-black
            0x000000FF, // Black
            0xFFFFFFFF, // White
            0xFFFF00FF, // Yellow
            0xFF00FFFF, // Magenta
            0x00FFFFFF	// Cyan
        };
        const char* colorNames[] = {
            "RED",
            "GREEN",
            "BLUE",
            "TRANSPARENT_WHITE",
            "TRANSPARENT_BLACK",
            "BLACK",
            "WHITE",
            "YELLOW",
            "MAGENTA",
            "CYAN"
        };

        for (uint32_t i = 0; i < 10; ++i)
        {
            auto tex = CreateTexture(texTraits);
            texData = 0x000000FF;
            tex->SetData(&texData, 1, 1, 0, 0, 0);
            systemTextures_.insert({ colorNames[i], tex });
        }
    }

    // Create default VBO
    {
        defaultVertexBuffer_ = CreateVertexBuffer();
    }

    // Create sequential/default IBO
    {
        vector<uint16_t> sequence(64000);
        iota(sequence.begin(), sequence.end(), 0);

        sequentialIdxBuff_ = defaultIndexBuffer_ = CreateIndexBuffer();
        sequentialIdxBuff_->SetData(sequence.data(), (uint32_t)(sequence.size() * sizeof(uint16_t)));
    }

    // standard vertex layouts
    {
        // float[3] position
        layoutPos_ = CreateGeometryLayout();
        layoutPos_->AddVertexInfo({ VA_POSITION, VDT_FLOAT, 0, 3, 0, sizeof(float3), false, false });

        // float[3] position, float[2] UV
        layoutPosUV_ = CreateGeometryLayout();
        static const auto layoutPosUV_Size = sizeof(float3) + sizeof(float2);
        layoutPosUV_->AddVertexInfo({ VA_POSITION, VDT_FLOAT, 0, 3, 0, layoutPosUV_Size, false, false });
        layoutPosUV_->AddVertexInfo({ VA_TEXCOORD0, VDT_FLOAT, sizeof(float3), 2, 0, layoutPosUV_Size, false, false });

        // float[3] position, float[3] normal, float[2] uv
        layoutPosNormUV_ = CreateGeometryLayout();
        static const auto layoutPosNormUV_Size = sizeof(float3) + sizeof(float3) + sizeof(float2);
        layoutPosNormUV_->AddVertexInfo({ VA_POSITION, VDT_FLOAT, 0, 3, 0, layoutPosNormUV_Size, false, false });
        layoutPosNormUV_->AddVertexInfo({ VA_NORMAL, VDT_FLOAT, sizeof(float3), 3, 0, layoutPosNormUV_Size, false, false });
        layoutPosNormUV_->AddVertexInfo({ VA_TEXCOORD0, VDT_FLOAT, sizeof(float3) + sizeof(float3), 2, 0, layoutPosNormUV_Size, false, false });        

        // float[3] position, float[2] UV, ubyte[4] color
        layoutPosUVColor_ = CreateGeometryLayout();
        static const auto layoutPosUVColor_Size = sizeof(float3) + sizeof(float2) + sizeof(uint32_t);
        layoutPosUVColor_->AddVertexInfo({ VA_POSITION, VDT_FLOAT, 0, 3, 0, layoutPosUVColor_Size, false, false });
        layoutPosUVColor_->AddVertexInfo({ VA_TEXCOORD0, VDT_FLOAT, sizeof(float3), 2, 0, layoutPosUVColor_Size, false, false });
        layoutPosUVColor_->AddVertexInfo({ VA_COLOR0, VDT_UBYTE, sizeof(float3) + sizeof(float2), 4, 0, layoutPosUVColor_Size, true, false });

        // float[3] position, ubyte[4] color
        layoutPosColor_ = CreateGeometryLayout();
        static const auto layoutPosColor_Size = sizeof(float3) + sizeof(uint32_t);
        layoutPosColor_->AddVertexInfo({ VA_POSITION, VDT_FLOAT, 0, 3, 0, layoutPosColor_Size, false, false });
        layoutPosColor_->AddVertexInfo({ VA_COLOR0, VDT_UBYTE, sizeof(float3), 4, 0, layoutPosColor_Size, true, false });

        // float[3] position, float[4] uvwz, ubyte[4] color
        layout2D_ = CreateGeometryLayout();
        static const auto layout2D_Size = sizeof(float3) + sizeof(float4) + sizeof(uint32_t);
        layout2D_->AddVertexInfo({ VA_POSITION, VDT_FLOAT, 0, 3, 0, layout2D_Size, false, false });
        layout2D_->AddVertexInfo({ VA_TEXCOORD0, VDT_FLOAT, sizeof(float3), 4, 0, layout2D_Size, false, false });
        layout2D_->AddVertexInfo({ VA_COLOR0, VDT_UBYTE, sizeof(float3) + sizeof(float4), 4, 0, layout2D_Size, true, false });
    }

    fsTriGeometry_ = Geometry::Create(TRIANGLE_LIST, layoutPosUV_, fullscreenTriVertices_, nullptr);
    fsQuadGeometry_ = Geometry::Create(TRIANGLE_LIST, layoutPosUV_, fullscreenQuadVertices_, nullptr);
        
    // Point Light
    {
        static float pointLightVertexData[] = {
            -0.423169f, -1.000000f, 0.423169f,          -0.423169f, -1.000000f, -0.423169f,         0.423169f, -1.000000f, -0.423169f,          0.423169f, -1.000000f, 0.423169f,
            0.423169f, 1.000000f, -0.423169f,           -0.423169f, 1.000000f, -0.423169f,          -0.423169f, 1.000000f, 0.423169f,           0.423169f, 1.000000f, 0.423169f,
            -1.000000f, 0.423169f, -0.423169f,          -1.000000f, -0.423169f, -0.423169f,         -1.000000f, -0.423169f, 0.423169f,          -1.000000f, 0.423169f, 0.423169f,
            0.423169f, 0.423169f, -1.000000f,           0.423169f, -0.423169f, -1.000000f,          -0.423169f, -0.423169f, -1.000000f,         -0.423169f, 0.423169f, -1.000000f,
            1.000000f, 0.423169f, 0.423169f,            1.000000f, -0.423169f, 0.423169f,           1.000000f, -0.423169f, -0.423169f,          1.000000f, 0.423169f, -0.423169f,
            0.423169f, -0.423169f, 1.000000f,           0.423169f, 0.423169f, 1.000000f,            -0.423169f, 0.423169f, 1.000000f,           -0.423169f, -0.423169f, 1.000000f
        };
            
        static unsigned short pointLightIndexData[] = {
            0, 1, 2,        0, 2, 3,        4, 5, 6,        4, 6, 7,        8, 9, 10,       8, 10, 11,      12, 13, 14,     12, 14, 15,     16, 17, 18,
            16, 18, 19,     20, 21, 22,     20, 22, 23,     0, 10, 9,       0, 9, 1,        13, 2, 1,       13, 1, 14,      23, 0, 3,       23, 3, 20,
            17, 3, 2,       17, 2, 18,      21, 7, 6,       21, 6, 22,      7, 16, 19,      7, 19, 4,       5, 8, 11,       5, 11, 6,       4, 12, 15,
            4, 15, 5,       22, 11, 10,     22, 10, 23,     8, 15, 14,      8, 14, 9,       12, 19, 18,     12, 18, 13,     16, 21, 20,     16, 20, 17,
            0, 23, 10,      1, 9, 14,       2, 13, 18,      3, 17, 20,      6, 11, 22,      5, 15, 8,       4, 19, 12,      7, 21, 16
        };
            
        auto vb = CreateVertexBuffer();
        vb->SetShadowed(true);
        vb->SetData(pointLightVertexData, sizeof(pointLightVertexData));
            
        auto ib = CreateIndexBuffer();
        ib->SetShadowed(true);
        ib->SetData(pointLightIndexData, sizeof(pointLightIndexData));
            
        pointLightGeometry_ = Geometry::Create(TRIANGLE_LIST, layoutPos_, vb, ib);
    }
        
    // Spot Light
    {
        static float spotLightVertexData[] = {
            0.00001f, 0.00001f, 0.00001f,           0.00001f, -0.00001f, 0.00001f,          -0.00001f, -0.00001f, 0.00001f,
            -0.00001f, 0.00001f, 0.00001f,          1.00000f, 1.00000f, 0.99999f,           1.00000f, -1.00000f, 0.99999f,
            -1.00000f, -1.00000f, 0.99999f,         -1.00000f, 1.00000f, 0.99999f,
        };

        static unsigned short spotLightIndexData[] = {
            3, 0, 1,        3, 1, 2,        0, 4, 5,        0, 5, 1,
            3, 7, 4,        3, 4, 0,        7, 3, 2,        7, 2, 6,
            6, 2, 1,        6, 1, 5,        7, 5, 4,        7, 6, 5
        };
            
        auto vb = CreateVertexBuffer();
        vb->SetShadowed(true);
        vb->SetData(spotLightVertexData, sizeof(spotLightVertexData));
            
        auto ib = CreateIndexBuffer();
        ib->SetShadowed(true);
        ib->SetData(spotLightIndexData, sizeof(spotLightIndexData));
            
        spotLightGeometry_ = Geometry::Create(TRIANGLE_LIST, layoutPos_, vb, ib);

    }

	ReloadSystemShaders();
}

//****************************************************************************
//
//  Function:   GraphicsDevice::ReloadSystemShaders
//
//  Purpose:    Constructs the GPU shader effects that are required. This
//				is kept independent though used by CreateDefaultObjects() so
//				that this function can be called at runtime to reload
//				the shaders.
//
//	WARNING:	Do not call mid-flight of a frame.
//
//****************************************************************************
void GraphicsDevice::ReloadSystemShaders()
{
	// Create the fullscreen triangle
	{
#if defined(GLVU_GL)
		static string vsQuad =
			R"(#version 420
			layout(location = 0) in vec3 pos;
			layout(location = 1) in vec2 inTex;
			layout(location = 0) out vec2 screenCoord;
			layout(binding = 3) uniform ViewData { mat4 viewMat; };
			void main()
			{
			   screenCoord = vec2(inTex.x, 1.0 - inTex.y);
			   gl_Position = vec4(pos.x, pos.y, 0, 1);
			}
			)";
#elif defined(GLVU_VK)
		static string vsQuad =
			R"(#version 420
			layout(location = 0) in vec3 pos;
			layout(location = 1) in vec2 inTex;
			layout(location = 0) out vec2 screenCoord;

			layout(set = 0, binding = 3) uniform ViewData {
			mat4 viewMat;
			};

			void main()
			{
			screenCoord = vec2(inTex.x, inTex.y);
			gl_Position = viewMat * vec4(pos.x, pos.y, 0, 1);
			}
			)";
#elif defined(GLVU_DX11)
		static string vsQuad = R"(
			struct VertData {
				float3 pos : POSITION0;
				float2 tex : TEXCOORD0;
			};

			struct VertOut {
				float2 screenCoord : TEXCOORD0;
				float4 pos : SV_Position;
			};

			cbuffer ViewData : register(b3)
			{
				float4x4 viewMat;
			};

			VertOut VS(in VertData vData) {
				VertOut ret = (VertOut)0;
				ret.screenCoord = float2(vData.tex.x, vData.tex.y);
				ret.pos = mul(viewMat, float4(vData.pos.x, vData.pos.y, 0.0, 1.0));
				return ret;
			}
			)";
#else
	#error
#endif
		//fsTriVertexShader_ = GetShader(VertexShader, "FSVert.vs", {});
		fsTriVertexShader_.reset(new Shader(this, "FullScreen.vs", VertexShader, SCT_GLSL, vsQuad, { }));
		bool success = fsTriVertexShader_->Compile();
		assert(success);
	}

	// light materials
	guiEffect_ = Effect::LoadEffect(this, "GUI.fx");
	passThruEffect_ = Effect::LoadEffect(this, "PassThru.fx");
	deferredLightEffect_ = Effect::LoadEffect(this, "DeferredLight.fx");
}

//****************************************************************************
//
//  Function:   GraphicsDevice::GetShader
//
//  Purpose:    Tries to acquire a shader by filename and definitions.
//              Multiple kinds of caching are used so that the file-system won't be
//              repeatedly hit, and matching shaders are used instead of many clones.
//
//  Return:     An shader (whether newly created or existing), or null if there were errors.
//
//****************************************************************************
shared_ptr<Shader> GraphicsDevice::GetShader(ShaderType type, const char* fileName, const vector<string>& defines)
{
    return shaderCache_->GetShader(type, fileName, defines);
}

//****************************************************************************
//
//  Function:   GraphicsDevice::GetEffect
//
//  Purpose:    Queries a cache for an existing named effect (filename),
//              if it wasn't found then it will try to create that effect.
//
//  Return:     Existing or new effect, null if there was an error.
//
//****************************************************************************
shared_ptr<Effect> GraphicsDevice::GetEffect(const string& name)
{
    if (auto found = effectCache_.Get(name))
        return found;

    auto fx = Effect::LoadEffect(this, name.c_str());
    if (fx)
        return effectCache_.Add(name, fx);
    return nullptr;
}

//****************************************************************************
//
//  Function:   GraphicsDevice::GetScratchUniformBuffer
//
//  Purpose:    Tries to fetch a UBO with a given capacity from the scratch collection.
//
//  Return:     A temporary-use UBO that cannot be safely used outside of the
//              current frame.
//
//****************************************************************************
shared_ptr<Buffer> GraphicsDevice::GetScratchUniformBuffer(size_t desiredSize)
{
    return uboCache_->GetBuffer(desiredSize);
}

//****************************************************************************
//
//  Function:   GraphicsDevice::GetScratchVertexBuffer
//
//  Purpose:    Tries to fetch a VBO with a given capacity from the scratch collection.
//
//  Return:     a temporary VBO, it's not safe to store or use for more than 1-frame
//
//****************************************************************************
shared_ptr<Buffer> GraphicsDevice::GetScratchVertexBuffer(size_t desiredSize)
{
    return uboCache_->GetVertexBuffer(desiredSize);
}

//****************************************************************************
//
//  Function:   GraphicsDevice::GetLayout_Pos
//
//  Purpose:    Utility.
//
//  Return:     The stock vertex layout for { vec3 pos; }
//
//****************************************************************************
shared_ptr<GeometryLayout> GraphicsDevice::GetLayout_Pos() const
{
    return layoutPos_;
}

//****************************************************************************
//
//  Function:   GraphicsDevice::GetLayout_PosUV
//
//  Purpose:    Utility.
//
//  Return:     The stock vertex layout for { vec3 pos; vec2 uv; }
//
//****************************************************************************
shared_ptr<GeometryLayout> GraphicsDevice::GetLayout_PosUV() const
{
    return layoutPosUV_;
}

//****************************************************************************
//
//  Function:   GraphicsDevice::GetLayout_PosNormUV
//
//  Purpose:    Utility.
//
//  Return:     The stock vertex layout for { vec3 pos; vec3 norm; vec2 uv; }
//
//****************************************************************************
shared_ptr<GeometryLayout> GraphicsDevice::GetLayout_PosNormUV() const
{
    return layoutPosNormUV_;
}

//****************************************************************************
//
//  Function:   GraphicsDevice::GetLayout_PosUVColor
//
//  Purpose:    Utility.
//
//  Return:     The stock vertex layout for { vec3 pos; vec2 uv, unsigned color; }
//
//****************************************************************************
shared_ptr<GeometryLayout> GraphicsDevice::GetLayout_PosUVColor() const
{
    return layoutPosUVColor_;
}

//****************************************************************************
//
//  Function:   GraphicsDevice::GetLayout_PosColor
//
//  Purpose:    Utility.
//
//  Return:     The stock vertex layout for { vec3 pos; unsigned color; }
//
//****************************************************************************
shared_ptr<GeometryLayout> GraphicsDevice::GetLayout_PosColor() const
{
    return layoutPosColor_;
}

//
//  Function:   GraphicsDevice::GetLayout_PosColor
//
//  Purpose:    Utility.
//
//  Return:     The stock vertex layout for { vec3 pos; vec4 uvwz; unsigned color; }
//
//****************************************************************************
shared_ptr<GeometryLayout> GraphicsDevice::GetLayout_2D() const
{
    return layout2D_;
}

//****************************************************************************
//
//  Function:   GraphicsDevice::GetSystemBuffer
//
//  Purpose:    Tries to find a specially recorded Buffer by an identifier.
//
//  Return:     The buffer found, or null
//
//****************************************************************************
shared_ptr<Buffer> GraphicsDevice::GetSystemBuffer(const char* name) const
{
    auto found = systemBuffers_.find(name);
    if (found != systemBuffers_.end())
        return found->second;
    return nullptr;
}

//****************************************************************************
//
//  Function:   GraphicsDevice::GetSystemTexture
//
//  Purpose:    Tries to find a specially recorded texture by an identifier.
//
//  Return:     The texture found, or null
//
//****************************************************************************
shared_ptr<Texture> GraphicsDevice::GetSystemTexture(const char* name) const
{
    auto found = systemTextures_.find(name);
    if (found != systemTextures_.end())
        return found->second;
    return nullptr;
}

//****************************************************************************
//
//  Function:   GraphicsDevice::AddSytemBuffer
//
//  Purpose:    Adds a named buffer that render-scripts can map to.
//
//****************************************************************************
void GraphicsDevice::AddSystemBuffer(const char* name, shared_ptr<Buffer> buffer)
{
    systemBuffers_.insert({ name, buffer });
}

//****************************************************************************
//
//  Function:   GraphicsDevice::AddSytemTexture
//
//  Purpose:    Adds a named texture that render-scripts can reference.
//              New insertions will overwrite.
//
//****************************************************************************
void GraphicsDevice::AddSystemTexture(const char* name, shared_ptr<Texture> texture)
{
    systemTextures_.insert({ name, texture });
}

//****************************************************************************
//
//  Function:   GraphicsDevice::RemoveSystem
//
//  Purpose:    Removes (if possible) a named system-buffer
//
//****************************************************************************
void GraphicsDevice::RemoveSystem(const char* name)
{
    systemBuffers_.erase(name);
}

//****************************************************************************
//
//  Function:   GraphicsDevice::RemoveTexture
//
//  Purpose:    Removes (if possible) a named system-texture.
//
//****************************************************************************
void GraphicsDevice::RemoveTexture(const char* name)
{
    systemTextures_.erase(name);
}

//****************************************************************************
//
//  Function:   GraphicsDevice::SatisfiesFeatures
//
//  Purpose:    Performs a check as to whether the device has the required
//				feature support that the end-user may need.
//
//	Return:		True if the device is able to satisfy the given features.
//
//****************************************************************************
bool GraphicsDevice::SatisfiesFeatures(const GraphicsFeatures& features) const
{
	if (features.maxUBOSize_ != UINT_MAX && features.maxUBOSize_ > graphicsFeatures_.maxUBOSize_)
		return false;

	if (features.minUBOAlignment_ != UINT_MAX && features.minUBOAlignment_ < graphicsFeatures_.minUBOAlignment_)
		return false;

#define CHECK_MISSING_FEATURE_NEED(FIELD) if (!graphicsFeatures_.FIELD && features.FIELD) return false

	CHECK_MISSING_FEATURE_NEED(compute_);
	CHECK_MISSING_FEATURE_NEED(geometryShader_);
	CHECK_MISSING_FEATURE_NEED(tessellation_);
	CHECK_MISSING_FEATURE_NEED(transformFeedback_);
	CHECK_MISSING_FEATURE_NEED(clipControl_);
	CHECK_MISSING_FEATURE_NEED(shaderStorageBuffer_);

#undef CHECK_MISSING_FEATURE_NEED

	return true;
}

//****************************************************************************
//
//  Function:   GraphicsDevice::LogStats
//
//  Purpose:    Print general device data via logging messages.
//
//****************************************************************************
void GraphicsDevice::LogStats()
{
    auto uboMem = uboCache_->GetUniformBufferAllocation();
    float uboAlloc = uboMem.first / 1024.0f;
    float uboAvail = uboMem.second / 1024.0f;
    LogFormat(GLVU_INFO, "Memory [UBO]: %.2fkb, %.2fkb %.2fkb", uboAlloc - uboAvail, uboAvail, uboAlloc);

    auto vboMem = uboCache_->GetVertexBufferAllocation();
    float vboAlloc = vboMem.first / 1024.0f;
    float vboAvail = vboMem.second / 1024.0f;
    LogFormat(GLVU_INFO, "Memory [VTX]: %.2fkb, %.2fkb %.2fkb", vboAlloc - vboAvail, vboAvail, vboAlloc);
}

//****************************************************************************
//
//  Function:   GraphicsDevice::RegisterCallback
//
//  Purpose:    Pushes (or replaces) a callback for invocation from render-scripts.
//
//****************************************************************************
void GraphicsDevice::RegisterCallback(const char* id, RENDER_CALLBACK call, void* userData)
{
    for (auto& i : renderCallbacks_)
    {
        if (i.id == id)
        {
            i.call = call;
            i.userData = userData;
            return;
        }
    }
    renderCallbacks_.push_back({ MakeID(id), call, userData });
}

//****************************************************************************
//
//  Function:   GraphicsDevice::RemoveCallback
//
//  Purpose:    Removes a given callback from the list of those available.
//
//****************************************************************************
void GraphicsDevice::RemoveCallback(const char* id)
{
    for (auto it = renderCallbacks_.begin(); it != renderCallbacks_.end(); ++it)
    {
        if (it->id == id)
        {
            renderCallbacks_.erase(it);
            return;
        }
    }
}

//****************************************************************************
//
//  Function:   GraphicsDevice::InvokeCallback
//
//  Purpose:    Triggers the execution of a callback, can only activate once
//              so duplicate IDs (or equally hashing Ids) aren't allowed.
//              RegisterCallback already enforces this constraint however.
//
//****************************************************************************
void GraphicsDevice::InvokeCallback(const char* id, RenderScript* caller, View* view, float* params, uint32_t paramCt)
{
    for (auto& rec : renderCallbacks_)
    {
        if (rec.id == id)
        {
            rec.call(caller, view, params, paramCt, rec.userData);
            break;
        }
    }
}

//****************************************************************************
//
//  Function:   ScratchBufferCache::ScratchBufferCache
//
//  Purpose:    Construct.
//
//****************************************************************************
ScratchBufferCache::ScratchBufferCache(GraphicsDevice* device) : GPUObject(device)
{

}

//****************************************************************************
//
//  Function:   ScratchBufferCache::~ScratchBufferCache
//
//  Purpose:    Destruct, release owned buffers.
//
//****************************************************************************
ScratchBufferCache::~ScratchBufferCache()
{
    Release();
}

//****************************************************************************
//
//  Function:   ScratchBufferCache::Release
//
//  Purpose:    Clears the vectors of buffers.
//
//****************************************************************************
void ScratchBufferCache::Release()
{
    uniformBuffercache_.existing_.clear();
    uniformBuffercache_.remaining_.clear();

    vertexBufferCache_.existing_.clear();
    vertexBufferCache_.remaining_.clear();
}

//****************************************************************************
//
//  Function:   ScratchBufferCache::GetUniformBufferAllocation
//
//  Purpose:    Utility calculation
//
//  Return:     size in bytes of allocated and remaining UBO scratch buffers
//              To get amount used subtract first from second
//
//****************************************************************************
std::pair<size_t, size_t> ScratchBufferCache::GetUniformBufferAllocation() const
{
    std::pair<size_t, size_t> size = { 0, 0 };
    for (auto& v : uniformBuffercache_.existing_)
        size.first += v.first;
    for (auto& v : uniformBuffercache_.remaining_)
        size.second += v.first;
    return size;
}

//****************************************************************************
//
//  Function:   ScratchBufferCache::GetVertexBufferAllocation
//
//  Purpose:    Utility calculation
//
//  Return:     size in bytes of allocated and remaining vertex scratch buffers
//              To get amount used subtract first from second
//
//****************************************************************************
std::pair<size_t,size_t> ScratchBufferCache::GetVertexBufferAllocation() const
{
    std::pair<size_t, size_t> size = { 0, 0 };
    for (auto& v : vertexBufferCache_.existing_)
        size.first += v.first;
    for (auto& v : vertexBufferCache_.remaining_)
        size.second += v.first;
    return size;
}

//****************************************************************************
//
//  Function:   ScratchBufferCache::GetBuffer
//
//  Purpose:    Acquires a UBO of at least the desired size.
//
//****************************************************************************
shared_ptr<Buffer> ScratchBufferCache::GetBuffer(size_t desiredSize)
{
    auto& cache = uniformBuffercache_;
    return GetBuffer(cache, UniformBufferObject, std::max<size_t>(256, desiredSize));
}

//****************************************************************************
//
//  Function:   ScratchBufferCache::GetVertexBuffer
//
//  Purpose:    Acquires a VBO of at least the desired size.
//
//****************************************************************************
shared_ptr<Buffer> ScratchBufferCache::GetVertexBuffer(size_t desiredSize)
{
    auto& cache = vertexBufferCache_;
    return GetBuffer(cache, VertexBufferObject, std::max<size_t>(256u, desiredSize));
}

//****************************************************************************
//
//  Function:   ScratchBufferCache::GetBuffer
//
//  Purpose:    Deals with the trying to find a fitting buffer or acquiring a new one.
//              To lighten that pain buffers must be multiples of 256 bytes which
//              should minimize (in practical use) the diversity of the buffer sizes so
//              that reuse can be as extensive as possible.
//
//  Return:     An buffer with at least the given capacity.
//
//****************************************************************************
shared_ptr<Buffer> ScratchBufferCache::GetBuffer(BufferGroup& cache, BufferKind kind, size_t desiredSize)
{
    uint32_t dif = (desiredSize % 256);
    desiredSize += dif;
    auto found = find_if(cache.remaining_.begin(), cache.remaining_.end(), [=](const Record& r) { return r.first == desiredSize; });
    if (found != cache.remaining_.end())
    {
        auto ret = found->second;
        cache.remaining_.erase(found);
        return ret;
    }

    auto effectiveSize = kind == UniformBufferObject ? std::min<size_t>(desiredSize, device_->GPU_MaxUBOSize()) : desiredSize;
    shared_ptr<Buffer> buffer = kind == UniformBufferObject ? device_->CreateUniformBuffer() : device_->CreateVertexBuffer();
    buffer->SetTag(BufferTag_Dynamic);
    buffer->SetSize(effectiveSize);

    cache.existing_.push_back({ effectiveSize, buffer });
    return buffer;
}

//****************************************************************************
//
//  Function:   ScratchBufferCache::FrameFinished
//
//  Purpose:    Returns the buffers that were allocated into the list of given buffers.
//
//****************************************************************************
void ScratchBufferCache::FrameFinished()
{
    uniformBuffercache_.remaining_ = uniformBuffercache_.existing_;
    vertexBufferCache_.remaining_ = vertexBufferCache_.existing_;
}

}