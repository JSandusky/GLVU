//****************************************************************************
//
//  File:       glvu.cpp
//  License:    MIT
//  Project:    GLVU
//  Contents:   Implementations of core functions from glvu.h, largely conversions
//              and common types.
//
//****************************************************************************

#include "glvu.h"

#include <cmath>
#include <string>

#include <MathGeoLib/Algorithm/Random/LCG.h>

using namespace std;

#define STRCASE(V) case V: return #V

namespace GLVU
{

//****************************************************************************
//
//  Function:   strcicmp
//
//  Purpose:    Case-insensitive (MSVC has one, but it's not cross-plat)
//
//  Return:     0 if equal, -1 if lhs is lesser, 1 otherwise
//
//****************************************************************************
int strcicmp(char const *a, char const *b)
{
    for (;; a++, b++) {
        int d = tolower((unsigned char)*a) - tolower((unsigned char)*b);
        if (d != 0 || !*a)
            return d;
    }
}

//****************************************************************************
//
//  Function:   ParsePrimitiveType
//
//  Purpose:    Turn a string into the PrimitiveType enum value.
//
//  Return:     Parsed value, or LINE_LIST - because anything bad is insane.
//
//****************************************************************************
PrimitiveType ParsePrimitiveType(const char* str)
{
    if (strcicmp(str, "triangles") == 0)
        return TRIANGLE_LIST;
    else if (strcicmp(str, "points") == 0)
        return POINT_LIST;
    else if (strcicmp(str, "triangle_adj"))
        return TRIANGLE_ADJ;
    else if (strcicmp(str, "line_adj"))
        return LINE_ADJ;
    else if (strcicmp(str, "unknown"))
        return PRIM_UNKNOWN;
    return LINE_LIST;
}

//****************************************************************************
//
//  Function:   ParseFillMode
//
//  Purpose:    Turn a string into the FillMode enum value.
//
//  Return:     Parsed value, or FILL_SOLID
//
//****************************************************************************
FillMode ParseFillMode(const char* str)
{
    if (strcicmp(str, "wire") == 0)
        return FILL_WIRE;
    return FILL_SOLID;
}

//****************************************************************************
//
//  Function:   ParseCullingMode
//
//  Purpose:    Turn a string into the CullingMode enum value.
//
//  Return:     Parsed value, or CULL_NONE
//
//****************************************************************************
CullingMode ParseCullingMode(const char* str)
{
    if (strcicmp(str, "front") == 0)
        return CULL_FRONT;
    else if (strcicmp(str, "back") == 0)
        return CULL_BACK;
    return CULL_NONE;
}

//****************************************************************************
//
//  Function:   ParseBufferKind
//
//  Purpose:    Turn a string into the BufferKind enum value.
//
//  Return:     Parsed value, or ShaderDataBufferObject
//
//****************************************************************************
BufferKind ParseBufferKind(const char* str)
{
    if (strcicmp(str, "vbo") == 0 || strcicmp(str, "VertexBufferObject") == 0)
        return VertexBufferObject;
    else if (strcicmp(str, "ibo") == 0 || strcicmp(str, "IndexBufferObject") == 0)
        return IndexBufferObject;
    else if (strcicmp(str, "ubo") == 0 || strcicmp(str, "UniformBufferObject") == 0)
        return UniformBufferObject;
    else if (strcicmp(str, "args") == 0 || strcicmp(str, "IndirectArgsBufferObject") == 0)
        return IndirectArgsBufferObject;
    else if (strcicmp(str, "raw") == 0 || strcicmp(str, "ByteAddressBuffer") == 0)
        return ByteAddressBuffer;
    return ShaderDataBufferObject;
}

//****************************************************************************
//
//  Function:   BufferKindToString
//
//  Purpose:    Turn a BufferKind enum value into a string.
//              For display in logs, file-io, UI, etc
//
//****************************************************************************
const char* BufferKindToString(BufferKind kind)
{
    switch (kind)
    {
    STRCASE(VertexBufferObject);
    STRCASE(IndexBufferObject);
    STRCASE(ShaderDataBufferObject);
    STRCASE(UniformBufferObject);
    STRCASE(IndirectArgsBufferObject);
    STRCASE(ByteAddressBuffer);
    }
    return "Invalid";
}

//****************************************************************************
//
//  Function:   ParseBufferTag
//
//  Purpose:    Turn a string into the BufferTag enum value.
//
//  Return:     Parsed value, or 0
//
//****************************************************************************
BufferTag ParseBufferTag(const char* str)
{
    if (strcicmp(str, "triangles") == 0)
        return BufferTag_Triangles;
    if (strcicmp(str, "TriangleAdjacency") == 0)
        return BufferTag_TriangleAdjacency;
    if (strcicmp(str, "lines") == 0)
        return BufferTag_Lines;
    if (strcicmp(str, "points") == 0)
        return BufferTag_Points;
    if (strcicmp(str, "is32bit") == 0)
        return BufferTag_32Bit;

    if (strcicmp(str, "instancetransform") == 0)
        return BufferTag_InstanceTransform;
    if (strcicmp(str, "instanceextra") == 0)
        return BufferTag_InstanceExtra;
    if (strcicmp(str, "ubo") == 0 || strcicmp(str, "materialubo") == 0)
        return BufferTag_MaterialUBO;

    if (strcicmp(str, "output") == 0)
        return BufferTag_Output;
    if (strcicmp(str, "dynamic") == 0)
        return BufferTag_Dynamic;

    if (strcicmp(str, "compute") == 0)
        return BufferTag_Compute;
    if (strcicmp(str, "appendconsume") == 0)
        return BufferTag_AppendConsume;

    return BufferTag_None;
}

//****************************************************************************
//
//  Function:   ParseTextureKind
//
//  Purpose:    Turn a string into the TextureKind enum value.
//
//  Return:     Parsed value, or TextureCube
//
//****************************************************************************
TextureKind ParseTextureKind(const char* str)
{
    if (strcicmp(str, "Texture2D") == 0)
        return Texture2D;
    if (strcicmp(str, "Texture2DArray") == 0)
        return Texture2DArray;
    if (strcicmp(str, "Texture3D") == 0)
        return Texture3D;
    if (strcicmp(str, "TextureCubeArray") == 0)
        return TextureCubeArray;
    if (strcicmp(str, "Texture1D") == 0)
        return Texture1D;
    if (strcicmp(str, "TextureBuffer") == 0)
        return TextureBuffer;
    return TextureCube;
}

//****************************************************************************
//
//  Function:   ParseTextureFormat
//
//  Purpose:    ??
//
//****************************************************************************
TextureFormat ParseTextureFormat(const char* str)
{
    if (strcicmp(str, "RGB8") == 0)
        return TEX_RGB8;
    if (strcicmp(str, "RGBA8") == 0)
        return TEX_RGBA8;
    if (strcicmp(str, "RGBA16F") == 0)
        return TEX_RGBA16F;
    if (strcicmp(str, "RG16F") == 0)
        return TEX_RG16F;
    if (strcicmp(str, "DXT1") == 0)
        return TEX_DXT1;
    if (strcicmp(str, "DXT3") == 0)
        return TEX_DXT3;
    if (strcicmp(str, "DXT5") == 0)
        return TEX_DXT5;
    if (strcicmp(str, "BC4") == 0)
        return TEX_BC4;
    if (strcicmp(str, "BC5") == 0)
        return TEX_BC5;
    if (strcicmp(str, "SHADOW") == 0 || strcicmp(str, "SHADOW16") == 0)
        return TEX_SHADOW16;
    if (strcicmp(str, "SHADOW32") == 0)
        return TEX_SHADOW32;
    if (strcicmp(str, "DEPTH") == 0)
        return TEX_DEPTH;

    if (strcicmp(str, "BGRA8") == 0)
        return TEX_BGRA8;
    if (strcicmp(str, "R32F") == 0)
        return TEX_R32F;
    if (strcicmp(str, "RG16U") == 0)
        return TEX_RG16U;
    if (strcicmp(str, "RGBA16U") == 0)
        return TEX_RGBA16U;
    if (strcicmp(str, "R32U") == 0)
        return TEX_R32U;
    if (strcicmp(str, "RGBA8U") == 0)
        return TEX_RGBA8U;
    if (strcicmp(str, "R8") == 0)
        return TEX_R8U;
    return TEX_RGBA8;
}

const char* TextureFormatToString(TextureFormat fmt)
{
#define FMTSTR(V) if (fmt == V) return #V;
    FMTSTR(TEX_RGB8)
    FMTSTR(TEX_RGBA8)
    FMTSTR(TEX_RGBA16F)
    FMTSTR(TEX_RG16F)
    FMTSTR(TEX_DXT1)
    FMTSTR(TEX_DXT3)
    FMTSTR(TEX_DXT5)
    FMTSTR(TEX_BC4)
    FMTSTR(TEX_BC5)
    FMTSTR(TEX_SHADOW16)
    FMTSTR(TEX_SHADOW32)
    FMTSTR(TEX_DEPTH)
    FMTSTR(TEX_BGRA8)
    FMTSTR(TEX_R32F)
    FMTSTR(TEX_RG16U)
    FMTSTR(TEX_RGBA16U)
    FMTSTR(TEX_R32U)
    FMTSTR(TEX_RGBA8U)
    FMTSTR(TEX_R8U)
#undef FMTSTR
    return "INVALID_FORMAT";
}

bool IsShadow(TextureFormat fmt)
{
    return fmt == TEX_SHADOW16 || fmt == TEX_SHADOW32;
}

bool IsDepth(TextureFormat fmt)
{
    return fmt == TEX_SHADOW16 || fmt == TEX_SHADOW32 || fmt == TEX_DEPTH;
}

bool IsComputeWriteable(TextureFormat fmt)
{
    switch (fmt)
    {
    case TEX_RGBA8:
    case TEX_RGBA16F:
    case TEX_R32F:
        return true;
    }
    return false;
}

//****************************************************************************
//
//  Function:   ParseTextureFilter
//
//  Purpose:    ??
//
//****************************************************************************
TextureFilter ParseTextureFilter(const char* str)
{
    if (strcicmp(str, "point") == 0 || strcicmp(str, "nearest") == 0)
        return FILTER_POINT;
    if (strcicmp(str, "linear") == 0 || strcicmp(str, "bilinear") == 0)
        return FILTER_LINEAR;
    if (strcicmp(str, "trilinear") == 0)
        return FILTER_TRILINEAR;
    if (strcicmp(str, "aniso") == 0 || strcicmp(str, "anisotropic") == 0)
        return FILTER_ANISOTROPIC;
    if (strcicmp(str, "shadow") == 0)
        return FILTER_SHADOW;
    return FILTER_POINT;
}

//****************************************************************************
//
//  Function:   TextureFilterToString
//
//  Purpose:    ??
//
//****************************************************************************
const char* TextureFilterToString(TextureFilter f)
{
    switch (f)
    {
    case FILTER_POINT: return "point";
    case FILTER_LINEAR: return "linear";
    case FILTER_TRILINEAR: return "trilinear";
    case FILTER_ANISOTROPIC: return "anisotropic";
    case FILTER_SHADOW: return "shadow";
    }
    return "point";
}

ShaderType ParseShaderType(const char* str)
{
	if (strcicmp(str, "vertexshader") == 0) return VertexShader;
	if (strcicmp(str, "pixelshader") == 0 || strcicmp(str, "fragmentshader") == 0) return PixelShader;
	if (strcicmp(str, "geometryshader") == 0) return GeometryShader;
	if (strcicmp(str, "hullshader") == 0) return HullShader;
	if (strcicmp(str, "domainshader") == 0) return DomainShader;
	if (strcicmp(str, "computeshader") == 0) return ComputeShader;
	return VertexShader;
}
const char* ShaderTypeToString(ShaderType type)
{
	switch (type)
	{
	case VertexShader: return "VertexShader";
	case HullShader: return "HullShader";
	case DomainShader: return "DomainShader";
	case GeometryShader: return "GeometryShader";
	case PixelShader: return "PixelShader";
	case ComputeShader: return "ComputeShader";
	}
	return "ComputeShader";
}

//****************************************************************************
//
//  Function:   ParseSortMode
//
//  Purpose:    ??
//
//****************************************************************************
SortMode ParseSortMode(const char* str)
{
    if (strcicmp(str, "context_switch") == 0)
        return ContextSwitch;
    else if (strcicmp(str, "context_and_depth") == 0)
        return ContextAndDepth;
    else if (strcicmp(str, "front_to_back") == 0)
        return FrontToBack;
    else if (strcicmp(str, "optimal") == 0) // well, that's what it is
        return ContextAndDepth;
    return BackToFront;
}

//****************************************************************************
//
//  Function:   ParseComparison
//
//  Purpose:    Note that lequal and gequal are short-forms.
//
//****************************************************************************
Comparison ParseComparison(const char* str)
{
    if (strcicmp(str, "equal") == 0)
        return COMPARE_EQUAL;
    if (strcicmp(str, "lequal") == 0 || strcicmp(str, "less_equal") == 0)
        return COMPARE_LEQUAL;
    if (strcicmp(str, "gequal") == 0 || strcicmp(str, "greater_equal") == 0)
        return COMPARE_GEQUAL;
    if (strcicmp(str, "not_equal") == 0)
        return COMPARE_NOT_EQUAL;
    if (strcicmp(str, "less") == 0)
        return COMPARE_LESS;
    if (strcicmp(str, "greater") == 0)
        return COMPARE_GREATER;
    if (strcicmp(str, "always") == 0)
        return COMPARE_ALWAYS;
    if (strcicmp(str, "never") == 0)
        return COMPARE_NEVER;
        
    return COMPARE_LEQUAL;
}

//****************************************************************************
//
//  Function:   ParseBlendMode
//
//  Purpose:    Only limited blend-modes are supported by GLVU instead of a
//              complete multipart blend (only a few are usually meaningful).
//              In the case of this parse more values are accepted.
//
//****************************************************************************
BlendMode ParseBlendMode(const char* str)
{
    if (strcicmp(str, "premultiplied") == 0 || strcicmp(str, "premul") == 0)
        return Blend_Premultiplied;
    else if (strcicmp(str, "alpha") == 0)
        return Blend_Alpha;
    else if (strcicmp(str, "additive") == 0 || strcicmp(str, "add") == 0)
        return Blend_Add;
    else if (strcicmp(str, "subtract") == 0 || strcicmp(str, "sub") == 0)
        return Blend_Subtract;
    else if (strcicmp(str, "multiply") == 0 || strcicmp(str, "mul") == 0)
        return Blend_Mul;
    else if (strcicmp(str, "pre") == 0)
        return Blend_Premultiplied;
    else if (strcicmp(str, "oit") == 0)
        return Blend_OITMixer;
    else if (strcicmp(str, "oitcomp") == 0)
        return Blend_OITComposite;
    return Blend_None;
}

//****************************************************************************
//
//  Function:   ParseViewFlag
//
//  Purpose:    
//
//****************************************************************************
ViewFlag ParseViewFlag(const char* str)
{
    if (strcicmp(str, "isroot") == 0)
        return ViewFlag_IsRoot;
    else if (strcicmp(str, "shadows") == 0)
        return ViewFlag_Shadows;
    else if (strcicmp(str, "pointshadow") == 0)
        return ViewFlag_PointShadows;
    else if (strcicmp(str, "spotshadow") == 0)
        return ViewFlag_SpotShadows;
    else if (strcicmp(str, "dirshadow") == 0)
        return ViewFlag_DirectionalShadows;
    else if (strcicmp(str, "default") == 0)
        return ViewFlag_Default;
    return ViewFlag_None;
}

const RS_Identifier RS_Identifier::NULL_ID = { };

//****************************************************************************
//
//  Function:   MakeID
//
//  Purpose:    Construct an RS_Identifier from a string and hash it.
//              Keeps RS_Identifier a POD so it can go in unions
//
//****************************************************************************
RS_Identifier MakeID(const char* str)
{
    RS_Identifier ret;
    strcpy_s(ret.name_, str);
    ret.nameHash_ = Hash(ret.name_);
    return ret;
}

//****************************************************************************
//
//  Function:   MakeID
//
//  Purpose:    String -> RS_Identifier factory, keeps RS_Identifier a POD
//              so that it can be stuck in unions for RS_DrawCmd
//
//****************************************************************************
RS_Identifier MakeID(const string& str)
{
    RS_Identifier ret;
    ret = str;
    return ret;
}

//****************************************************************************
//
//  Function:   Hash
//
//  Purpose:    SDBM hash for use in RS_Identifier
//
//****************************************************************************
uint32_t Hash(const char* str)
{
    const auto length = strlen(str);
        
    uint32_t hash = 0;
    uint32_t i = 0;
    for (i = 0; i < length; ++str, ++i)
        hash = (*str) + (hash << 6) + (hash << 16) - hash;

    return hash;
}

//****************************************************************************
//
//  Function:   Hash
//
//  Purpose:    Call the SDBM hash function for an STL string.
//
//****************************************************************************
uint32_t Hash(const string& str)
{
    return Hash(str.c_str());
}

//****************************************************************************
//
//  Function:   GPUObject::GPUObject
//
//  Purpose:    Construct, the base of most everything.
//
//****************************************************************************
GPUObject::GPUObject(GraphicsDevice* device) : 
    device_(device)
{

}

//****************************************************************************
//
//  Function:   GPUObject::~GPUObject
//
//  Purpose:    Destruct, call Release
//
//****************************************************************************
GPUObject::~GPUObject()
{
    Release();
}

//****************************************************************************
//
//  Function:   LUTFloatDistribution::Randomize
//
//  Purpose:    Fill the table with noise, either as UNORM or SNORM.
//
//****************************************************************************
void LUTFloatDistribution::Randomize(bool sNormal)
{
    math::LCG lcg((unsigned)this);
    if (sNormal)
    {
        for (int i = 0; i < 64; ++i)
            table_[i] = lcg.FloatNeg1_1();
    }
    else
    {
        for (int i = 0; i < 64; ++i)
            table_[i] = lcg.Float01Incl();
    }
}

//****************************************************************************
//
//  Function:   LUTVectorDistribution::Randomize
//
//  Purpose:    Fill the table with noise, either as UNORM or SNORM.
//
//****************************************************************************
void LUTVectorDistribution::Randomize(bool sNormal)
{
    math::LCG lcg((unsigned)this);
    if (sNormal)
    {
        for (int i = 0; i < 64; ++i)
            table_[i] = float4 { lcg.FloatNeg1_1(), lcg.FloatNeg1_1(), lcg.FloatNeg1_1(), lcg.FloatNeg1_1() };
    }
    else
    {
        for (int i = 0; i < 64; ++i)
            table_[i] = float4 { lcg.Float01Incl(), lcg.Float01Incl(), lcg.Float01Incl(), lcg.Float01Incl() };
    }
}

//****************************************************************************
//
//  Function:   LUTVectorDistribution::Randomize
//
//  Purpose:    converts
//
//****************************************************************************
uint32_t PackColor(const float4& p)
{
#define TO_INT(V) ((uint32_t)(V * 255))

    return TO_INT(p.x) << 24 | TO_INT(p.y) << 16 | TO_INT(p.z) << 8 | TO_INT(p.w);
}

void SlotMapTest()
{
    SlotMap<std::string, int> mapp;
    mapp.Get(0);
    mapp.Add("something");
    mapp.Remove(0);
    mapp.AddRef(0);
    mapp.Handle("something");
    mapp.Clean();

    SlotMap<std::weak_ptr<float>, int> map;
    map.Get(0).lock();
    map.Clean();
}

template<typename T>
constexpr T max_p(T a) { return a; }

template<typename T, typename...ARGS>
constexpr T max_p(T a, ARGS...args)
{
    return std::max(a, max_p<T>(args...));
}

template<typename R, typename...T>
constexpr R max_n(T...args)
{
    return max_p(args...);
}

static constexpr int ConstExprTest = max_n<int>(0, 1, 7, 4);

}