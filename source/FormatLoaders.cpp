//****************************************************************************
//
//  File:       FormatLoaders.cpp
//  License:    MIT
//  Project:    GLVU
//
//  Concerns:   Should there be more granularity? Local load function just for shader-pass? etc?
//              Switch to YAML or HJson (no quote requirement and comments are very desirable for editing pipelines and materials)
//
//              The switch keeps getting hard and harder:
//                  - new features `pass MyContext:triangles {`
//
//  Todo:       Support inline shader code in materials
//
//****************************************************************************

// WARNING: This file is under considerable revision

#include "Buffer.h"
#include "GraphicsDevice.h"
#include "Effect.h"
#include "Material.h"
#include "RenderScript.h"
#include "ShaderConstants.h"

#define STB_C_LEXER_IMPLEMENTATION
#include "stb_c_lexer.h"

#include <algorithm>
#include <array>
#include <functional>
#include <regex>
#include <string>
#include <sstream>
#include <vector>

#include <hjson/hjson.h>

using namespace std;

#if defined(GLVU_DX11)
	#define SHADER_PATH_PREFIX "HLSL/"
#else
	#define SHADER_PATH_PREFIX "GLSL/"
#endif

namespace GLVU
{

#define LEXER_WARNING(msg, ...) { \
    stb_lex_location loc; \
    stb_c_lexer_get_location(&lexer, lexer.parse_point, &loc); \
    device->LogFormat(2, msg ", line: %u, col: %u", __VA_ARGS__, loc.line_number, loc.line_offset); }

#define LEXER_ERROR(msg, ...) { \
    stb_lex_location loc; \
    stb_c_lexer_get_location(&lexer, lexer.parse_point, &loc); \
    device->LogFormat(2, msg ", line: %u, col: %u", __VA_ARGS__, loc.line_number, loc.line_offset); }

#define LEXER_INTERNAL_WARNING(msg, ...) { \
    stb_lex_location loc; \
    stb_c_lexer_get_location(&lexer, lexer.parse_point, &loc); \
    ((GraphicsDevice*)lexer.user_data)->LogFormat(2, msg ", line: %u, col: %u", __VA_ARGS__, loc.line_number, loc.line_offset); }

#define LEXER_INTERNAL_ERROR(msg, ...) { \
    stb_lex_location loc; \
    stb_c_lexer_get_location(&lexer, lexer.parse_point, &loc); \
    ((GraphicsDevice*)lexer.user_data)->LogFormat(2, msg ", line: %u, col: %u", __VA_ARGS__, loc.line_number, loc.line_offset); }

#define ADVANCE_LEXER_WARN(LEXER, TOKEN) if (AdvanceLexer(LEXER, TOKEN) == 0) LEXER_WARNING("Unexpected token: %u", LEXER.token)

//****************************************************************************
//
//  Function:   split
//
//  Purpose:    Used to split "MY_SHADER_DEFINE, SECOND_DEFINE, BONE_COUNT = 2" 
//              into discrete definitions.
//
//  Return:     List of the split CSV
//
//****************************************************************************
vector<string> split(const string& str)
{
    std::regex regex{ R"([,]+)" }; // split on comma
    std::sregex_token_iterator it{ str.begin(), str.end(), regex, -1 };
    return { it, { } };
};


/// Internally we have no concern over integer or floating-point numbers, everything is stored as a floating point for now.
//****************************************************************************
//
//  Function:   CLEX_IsNumeric
//
//  Purpose:    Internally we have no concern over integer or floating-point 
//              numbers, everything is stored as a floating point for now.
//
//  Return:     True if the value in the lexer's state is an int/float
//
//****************************************************************************
bool CLEX_IsNumeric(long token)
{
    return token == CLEX_intlit || token == CLEX_floatlit;
}

//****************************************************************************
//
//  Function:   CLEX_GetNumber
//
//  Purpose:    Reliably fetch a number from the lexer, is potentially an ancient
//              value if used when an int/float is not ready.
//
//  Return:     Value as floating point
//
//****************************************************************************
float CLEX_GetNumber(stb_lexer& lexer)
{
    if (lexer.token == CLEX_intlit)
        return lexer.int_number;

    return (float)lexer.real_number;
}

//****************************************************************************
//
//  Function:   CLEX_String
//
//  Purpose:    Grab the string contained in the lexer's current state.
//              Does not advance or manipulate the lexer.
//
//  Return:     String value, as stored by lexer
//
//****************************************************************************
string CLEX_String(stb_lexer& lexer)
{
    return string(lexer.string);
}

//****************************************************************************
//
//  Function:   CLEX_BoolString
//
//  Purpose:    For uniform handling of what qualifies as a bool.
//
//  Return:     True if the comparison passes
//
//****************************************************************************
inline bool CLEX_BoolString(const std::string& strValue)
{
    return !(strValue == "false");
}


//****************************************************************************
//
//  Function:   CLEX_GetRSIdentifier
//
//  Purpose:    RS_Identifier's are PODs for C-union support so they require
//              initialization, this function reliably fetches an RS_ID from
//              the lexer.
//
//  Return:     Parsed RS_Identifier
//
//****************************************************************************
RS_Identifier CLEX_GetRSIdentifier(stb_lexer& lexer)
{
    //return MakeID(lexer.string);
    RS_Identifier r;
    strcpy(r.name_, lexer.string);
    r.UpdateHash();
    return r;
}

//****************************************************************************
//
//  Function:   AdvanceLexer
//
//  Purpose:    Use this method for all lexer advances, takes care of recording 
//              the last N tokens/values and making lexer callbacks for depth tracking.
//
//  Return:     Token-value or zero
//
//****************************************************************************
/// 
int AdvanceLexer(stb_lexer& lexer)
{
    if (lexer.token == CLEX_eof)
        return 0;
    if (lexer.parse_point >= lexer.eof)
        return 0;
    int code = stb_c_lexer_get_token(&lexer);
    if (code)
        return lexer.token;
    else
        return 0;
}

//****************************************************************************
//
//  Function:   PeekToken
//
//  Purpose:    For whatever reason stb_c_lexer doesn't provide a Peek function.
//              This stores, queries token, then restores state to what we had.
//              Function goes a little above and beyond, realistically
//              only `parse_point` and `token` should matter but I'm not risking
//              something somewhere/anywhere taking a silent-dive from a bad
//              int_number or string.
//
//  Return:     Token-value
//
//****************************************************************************
int PeekToken(stb_lexer& lexer)
{
    auto parsePoint = lexer.parse_point;
    auto oldToken = lexer.token;
    auto oldStr = lexer.string;
    auto oldStrLen = lexer.string_len;
    auto oldInt = lexer.int_number;
    auto oldDouble = lexer.real_number;
    auto oldWhereChar = lexer.where_firstchar;
    auto oldWhereLastChar = lexer.where_lastchar;

    stb_c_lexer_get_token(&lexer);
        
    auto ret = lexer.token;
    lexer.parse_point = parsePoint;
    lexer.token = oldToken;
    lexer.string = oldStr;
    lexer.string_len = oldStrLen;
    lexer.int_number = oldInt;
    lexer.real_number = oldDouble;
    lexer.where_firstchar = oldWhereChar;
    lexer.where_lastchar = oldWhereLastChar;

    return ret;
}

//****************************************************************************
//
//  Function:   EatTokenIf
//
//  Purpose:    Looks at the next token, and if it's the given value then
//              advances the lexer, effectively eating the token.
//
//  Usage:      Trailing ; hell
//
//****************************************************************************
void EatTokenIf(stb_lexer& lexer, long token)
{
    if (PeekToken(lexer) == token)
        AdvanceLexer(lexer);
}

//****************************************************************************
//
//  Function:   AdvanceLexer
//
//  Purpose:    Advances for the next token, returning non-zero only if the
//              token acquired is the expected token.
//              Optionally, if the token is CLEX_id can check if it is also
//              an expected value.
//
//  WARNING:    while the expected string-check could be done on CLEX_dqstring/CLEX_sqstring
//              the usage of such a thing would questionable in the context of parsing.
//
//  Return:     Token-value, or zero if not legitimate
//
//****************************************************************************
int AdvanceLexer(stb_lexer& lexer, int expected, const char* expectedID = 0)
{
    if (lexer.token == CLEX_eof)
        return 0;
    if (lexer.parse_point >= lexer.eof)
        return 0;
    int code = stb_c_lexer_get_token(&lexer);
    if (code && lexer.token == expected)
    {
        if (lexer.token == CLEX_id && expectedID)
        {
            if (strcmp(lexer.string, expectedID) != 0)
                return 0;
        }
        return lexer.token;
    }
    else
        return 0;
}

//****************************************************************************
//
//  Function:   ExtractKVP
//
//  Purpose:    Grabs a `KEY = VALUE;`
//              Copes with strings, floats, and 4-component vectors.
//              Percentages (50%) are automatically converted to fractions (0.5)
//              via division by 100.
//
//              Presently this function does not tell you what it found
//              so it's the caller's responsibility to check the key's value
//              and determine what should be done with the values extracted.
//
//  WARNING:    Integers and hexadecimal numbers are converted to float.
//
//  Return:     The last token value touched, or zero if we outright fail.
//
//****************************************************************************
int ExtractKVP(stb_lexer& lexer, string& key, string& value, float& floatVal, math::float4& vectorValue)
{
    if (AdvanceLexer(lexer) == CLEX_id)
        key = CLEX_String(lexer);
    else
        return 0;

    if (AdvanceLexer(lexer, '=', 0) == 0)
        return 0;

    int valueCode = AdvanceLexer(lexer);
    if (valueCode == CLEX_id || valueCode == CLEX_dqstring) // text/ID value
    {
        value = CLEX_String(lexer);
    }
    else if (CLEX_IsNumeric(valueCode)) // numeric value
    {
        floatVal = CLEX_GetNumber(lexer);
        if (PeekToken(lexer) == '%')
        {
            AdvanceLexer(lexer);
            floatVal /= 100.0f;
        }
    }
    else if (valueCode == '<') // vector value
    {
        // reset to null so we don't have quasi-random consequences.
        vectorValue = float4(0,0,0,0);

        int idx = 0;
        for (;;)
        {
            AdvanceLexer(lexer);
            vectorValue[idx] = CLEX_GetNumber(lexer);

            // if we're a percentage then convert
            if (AdvanceLexer(lexer) == '%')
                vectorValue[idx] = vectorValue[idx] / 100.0f;

            AdvanceLexer(lexer);

            ++idx;
            if (lexer.token == ',') // only continue if we have closes
                continue;

            break;
        }

        if (lexer.token != '>')
        {
            // TODO: log this error

            // well, at least we can recover
            if (lexer.token == ';')
                return lexer.token;
        }
    }
    else
        return 0;

    //TODO: raise an error if not what we want
    AdvanceLexer(lexer, ';');

    return lexer.token;
}

//****************************************************************************
//
//  Function:   CLEX_ReadParams
//
//  Purpose:    Grabs a string of numbers as a vector, what's special is that
//              this function will eat text and special characters so
//              that the values can be *commented* without actual comments.
//              Used to read preconfigured UBO data for shaders.
//
//  Example:    {
//                  sun dir: 0.1 0.3 0.66 1.0
//                  moon dir= -0.5 0.5 0.5 1.0 
//              }
//
//  WARNING:    Integers and hexadecimal numbers are converted to float.
//
//  Return:     List of floating point values found
//
//****************************************************************************
vector<float> CLEX_ReadParams(stb_lexer& lexer)
{
    vector<float> params;
    for (;;)
    {
        AdvanceLexer(lexer);
        if (CLEX_IsNumeric(lexer.token))
            params.push_back(CLEX_GetNumber(lexer));
        else if (lexer.token == CLEX_id) // we eat ID's so names can be inlined, so "colors: 0.5 0.1 0.6 0.2" is legitimate for legibility
            continue;
        else if (lexer.token == ':' || lexer.token == '=') // also eat these
            continue;
        else if (lexer.token != '}' || lexer.token == CLEX_eof) // should be }
            break;
    }
    return params;
}

void TransferParamsData(const vector<float>& data, float* target, uint32_t* targetSize, uint32_t maxSize)
{
    *targetSize = std::min<size_t>(data.size(), maxSize);
    memcpy(target, data.data(), sizeof(float) * std::min<size_t>(data.size(), maxSize));
}

//****************************************************************************
//
//  Function:   CLEX_ToString
//
//  Purpose:    Converts the current token into a string.
//
//  WARNING:    This is not suitable for code emission.
//              Strings are not contained in quotes ("my string") is
//              returned as `my string` and not `"my string"`
//
//  Return:     Meaningful string value.
//
//****************************************************************************
string CLEX_ToString(const stb_lexer& lexer)
{
    if (lexer.token == CLEX_andand) return "&&";
    else if (lexer.token == CLEX_andeq) return "&=";
    else if (lexer.token == CLEX_arrow) return "->";
    else if (lexer.token == CLEX_charlit) return "" + ((char)lexer.token);
    else if (lexer.token == CLEX_diveq) return "/=";
    else if (lexer.token == CLEX_dqstring) return string(lexer.string, lexer.string_len);
    else if (lexer.token == CLEX_eq) return "==";
    else if (lexer.token == CLEX_floatlit) return std::to_string(lexer.real_number); // TODO
    else if (lexer.token == CLEX_greatereq) return ">=";
    else if (lexer.token == CLEX_id) return lexer.string;
    else if (lexer.token == CLEX_intlit) return std::to_string(lexer.int_number); // TODO
    else if (lexer.token == CLEX_lesseq) return "<=";
    else if (lexer.token == CLEX_minuseq) return "-=";
    else if (lexer.token == CLEX_minusminus) return "--";
    else if (lexer.token == CLEX_modeq) return "%=";
    else if (lexer.token == CLEX_muleq) return "*=";
    else if (lexer.token == CLEX_noteq) return "!=";
    else if (lexer.token == CLEX_oreq) return "|=";
    else if (lexer.token == CLEX_oror) return "||";
    else if (lexer.token == CLEX_pluseq) return "+=";
    else if (lexer.token == CLEX_plusplus) return "++";
    else if (lexer.token == CLEX_shl) return "<<";
    else if (lexer.token == CLEX_shleq) return "<<=";
    else if (lexer.token == CLEX_shr) return ">>";
    else if (lexer.token == CLEX_shreq) return ">>=";
    else if (lexer.token == CLEX_xoreq) return "^=";
    else if (lexer.token == CLEX_eof) return "EOF";
    return "" + ((char)lexer.token);
}

/// Grab as string everything up until we hit an expected token code
/// Used for grabbing initializers for members and the values of enums (to avoid needing to run a full parser)
/// For enums:
///     CLEX_ToStringUntil(lexer, ',') ... but we have to initialize these things somehow
/// For member defaults:
///     CLEX_ToStringUntil(lexer, ';')
string CLEX_ToStringUntil(stb_lexer& lexer, long tokenCode)
{
    stringstream ss;
    while (AdvanceLexer(lexer) != tokenCode && lexer.token != CLEX_eof)
        ss << CLEX_ToString(lexer);
    return ss.str();
}

enum DataObjectFlags {
    DOF_Global = GLVU_BITFIELD(0),
    DOF_PingPong = GLVU_BITFIELD(1)
};

/// Either an index or a name.
struct ObjectIndicator {
    union {
        RS_Identifier id_;
        int number_;
    };
    bool isID_;

    ObjectIndicator() { isID_ = false; number_ = INT_MAX; }
    ObjectIndicator(int num) { isID_ = false; number_ = num; }
    ObjectIndicator(RS_Identifier id) { isID_ = true; id_ = id; }

    bool IsNumberValid() const { return number_ != INT_MAX && !isID_; }

    RS_Identifier GetID() const { return isID_ ? id_ : RS_Identifier {  }; }
    const char* GetString() const { return isID_ ? id_.name_ : nullptr; }
};

//****************************************************************************
//
//  Function:   RenderScript::Load
//
//  Purpose:    Loads a render-script from file.
//              This will load shaders, effects, and textures as needed.
//
//  Return:     Loaded render-script if there weren't any breaking errors.
//
//****************************************************************************
shared_ptr<RenderScript> RenderScript::Load(GraphicsDevice* device, const char* fileName)
{
    auto blob = device->GetResourceData(Resource_RenderScript, fileName);
    if (blob.size_ == 0 || blob.data_ == nullptr)
    {
        device->LogFormat(GLVU_ERROR, "Failed to load render_script: %s", fileName);
        return nullptr;
    }

    char lexerStorage[1024];
    memset(lexerStorage, 0, sizeof(lexerStorage));

    auto start = chrono::steady_clock::now();
    stb_lexer lexer;
    stb_c_lexer_init(&lexer, blob.data_, blob.data_ + blob.size_, lexerStorage, 1024);
    lexer.user_data = device;

    //auto list = DataBlock::ParseList(lexer);
    //auto end = chrono::steady_clock::now();
    //auto len = end - start;
    //cout << "Load DOM: " << chrono::duration<double, milli>(len).count() << endl;
        

    //start = chrono::steady_clock::now();
    auto ret = Load(device, blob.data_, blob.size_);
    //end = chrono::steady_clock::now();
    //len = end - start;
    //cout << "Complete deserialization: " << chrono::duration<double, milli>(len).count() << endl;
    return ret;
}

//****************************************************************************
//
//  Function:   RenderScript::Load
//
//  Purpose:    Loads a render-script from file.
//              This will load shaders, effects, and textures as needed.
//
//  Return:     Loaded render-script if there weren't any breaking errors.
//
//****************************************************************************
shared_ptr<RenderScript> RenderScript::Load(GraphicsDevice* device, const char* buffer, size_t fileSize)
{
    char lexerStorage[1024];
    memset(lexerStorage, 0, sizeof(lexerStorage));

    stb_lexer lexer;
    stb_c_lexer_init(&lexer, buffer, buffer + fileSize, lexerStorage, 1024);
    lexer.user_data = device;

#if 1
    if (AdvanceLexer(lexer, CLEX_id))
    {
        if (strcmp(lexer.string, "render_script") == 0)
        {
            shared_ptr<RenderScript> script(new RenderScript(device));

            if (AdvanceLexer(lexer, '{'))
            {
                // inside a script
                while (AdvanceLexer(lexer))
                {
                    if (lexer.token == CLEX_id)
                    {
                        auto tokenKey = CLEX_String(lexer);
                        if (tokenKey == "target")
                        {
                            bool makeGlobal = false;
                            RenderTargetInfo* targetInfo = new RenderTargetInfo();
                            targetInfo->backbufferWidthFraction_ = targetInfo->backbufferHeightFraction_ = 1.0f;
                            targetInfo->width_ = -1;
                            targetInfo->height_ = -1;
                            targetInfo->targetFormat_ = TEX_RGB8;
                            targetInfo->pingPong_ = false;
                            targetInfo->fixedWidth_ = 0;
                            targetInfo->fixedHeight_ = 0;

                            script->targetTextures_.push_back(targetInfo);

                            if (AdvanceLexer(lexer, CLEX_id, 0))
                            {
                                targetInfo->name_ = CLEX_GetRSIdentifier(lexer);

                                ADVANCE_LEXER_WARN(lexer, '{');

                                string k, v; float f; float4 vectorValue; 
                                while (ExtractKVP(lexer, k, v, f, vectorValue))
                                {
                                    if (k == "format")
                                        targetInfo->targetFormat_ = ParseTextureFormat(v.c_str());
                                    else if (k == "width_fraction")
                                        targetInfo->backbufferWidthFraction_ = f;
                                    else if (k == "height_fraction")
                                        targetInfo->backbufferHeightFraction_ = f;
                                    else if (k == "width")
                                        targetInfo->fixedWidth_ = (uint32_t)f;
                                    else if (k == "height")
                                        targetInfo->fixedHeight_ = (uint32_t)f;
                                    else if (k == "global" && v == "true")
                                        makeGlobal = true;
                                    else if (k == "ping_pong" && v == "true")
                                        targetInfo->pingPong_ = true;
                                    else
                                    {
                                        LEXER_WARNING("Target '%s', unknown variable: %s", targetInfo->name_.name_, k.c_str());
                                    }
                                }
                            }
                        }
                        else if (tokenKey == "buffer")
                        {
                            RenderDataBufferInfo* dataBuffInfo = new RenderDataBufferInfo();
                            script->dataBuffers_.push_back(dataBuffInfo);

                            dataBuffInfo->size_ = 32;
                                
                            if (AdvanceLexer(lexer, CLEX_id))
                            {
                                dataBuffInfo->name_ = CLEX_GetRSIdentifier(lexer);
                                if (!AdvanceLexer(lexer, '{'))
                                    return nullptr;

                                string k, v; float f; float4 vectorValue;
                                while (ExtractKVP(lexer, k, v, f, vectorValue) != '}')
                                {
                                    if (k == "size")
                                        dataBuffInfo->size_ = (uint32_t)f;
                                    else
                                    {
                                        LEXER_WARNING("Buffer '%s', unknown variable: %s", dataBuffInfo->name_.name_, k.c_str());
                                    }
                                }
                            }
                            else
                            {
                                auto str = CLEX_ToString(lexer);
                                LEXER_WARNING("buffer: expected identifier, got %s", str.c_str());
                            }
                        }
                        else if (tokenKey == "stage")
                        {
                            RenderScriptStage* stage = new RenderScriptStage();
                            stage->active_ = true;

                            ADVANCE_LEXER_WARN(lexer, CLEX_id);
                            stage->self_ = CLEX_GetRSIdentifier(lexer);

                            while (AdvanceLexer(lexer))
                            {
                                if (lexer.token == '}')
                                    break;

                                if (lexer.token == CLEX_id)
                                {
                                    auto part = CLEX_String(lexer);
                                    if (part == "ban")
                                    {
                                        ADVANCE_LEXER_WARN(lexer, '=');
                                        ADVANCE_LEXER_WARN(lexer, CLEX_id);
                                        RS_Identifier req = CLEX_GetRSIdentifier(lexer);
                                        stage->ignoreActiveStages_.push_back(req);
                                        ADVANCE_LEXER_WARN(lexer, ';');
                                    }
                                    else if (part == "enabled")
                                    {
                                        ADVANCE_LEXER_WARN(lexer, '=');
                                        ADVANCE_LEXER_WARN(lexer, CLEX_id);
                                        stage->active_ = CLEX_BoolString(CLEX_ToString(lexer));
                                        ADVANCE_LEXER_WARN(lexer, ';');
                                    }
                                    else if (part == "require")
                                    {
                                        ADVANCE_LEXER_WARN(lexer, '=');
                                        ADVANCE_LEXER_WARN(lexer, CLEX_id);
                                        RS_Identifier req = CLEX_GetRSIdentifier(lexer);
                                        stage->requireActiveStages_.push_back(req);
                                        ADVANCE_LEXER_WARN(lexer, ';');
                                    }
                                    else if (part == "targets")
                                    {
                                        ADVANCE_LEXER_WARN(lexer, '{');
                                        while (AdvanceLexer(lexer))
                                        {
                                            if (lexer.token == CLEX_id)
                                            {
                                                RS_Identifier id = CLEX_GetRSIdentifier(lexer);
                                                stage->targets_.push_back(id);
                                            }
                                            else if (lexer.token == ',')
                                                continue;
                                            else
                                                break;
                                        }
                                        assert(lexer.token == '}');
                                    }
                                    else if (part == "bind_targets")
                                    {
                                        AdvanceLexer(lexer, '{', 0);
                                        string k, v; float f; float4 vectorValue;
                                        while (ExtractKVP(lexer, k, v, f, vectorValue))
                                        {
                                            SlottedID slot;
                                            slot.first = (uint32_t)f;
                                            slot.second = k;
                                            stage->targetBindings_.push_back(slot);
                                        }
                                    }
                                    else if (part == "lighting_pass" || part == "pass")
                                    {
                                        RS_DrawCmd cmd;
                                        cmd.enabled_ = true;
                                        cmd.cmdData_.drawData_.materialOverride_ = RS_Identifier::NULL_ID;
                                        cmd.cmdData_.drawData_.sortMode_ = ContextSwitch;
                                        cmd.cmdData_.drawData_.alphaToCoverage_ = false;
                                        cmd.commandType_ = GeometryPass;

                                        if (AdvanceLexer(lexer, CLEX_id, 0) == 0) { /*error*/ }
                                        cmd.passIdentifier_ = CLEX_GetRSIdentifier(lexer);

                                        AdvanceLexer(lexer, '{', 0);
                                        string k, v; float f; float4 vectorValue;
                                        while (ExtractKVP(lexer, k, v, f, vectorValue))
                                        {
                                            if (k == "method")
                                            {
                                                if (v == "forward")
                                                    cmd.commandType_ = ForwardLights;
                                                else if (v == "deferred_tiled")
                                                    cmd.commandType_ = DeferredTiledLights;
                                                else if (v == "forward_tiled")
                                                    cmd.commandType_ = ForwardTiledLights;
                                                else if (v == "light_volumes")
                                                    cmd.commandType_ = LightVolumes;
                                                else if (v == "volumes")
                                                    cmd.commandType_ = LightVolumes;
                                            }
                                            else if (k == "context")
                                                cmd.context_ = v;
                                            else if (k == "sort")
                                                cmd.cmdData_.drawData_.sortMode_ = ParseSortMode(v.c_str());
                                            else if (k == "alpha_to_coverage")
                                                cmd.cmdData_.drawData_.alphaToCoverage_ = v == "on";
                                            else if (k == "enabled")
                                                cmd.enabled_ = v == "true";
                                            else if (k == "material")
                                                cmd.cmdData_.drawData_.materialOverride_ = v;
                                        }

                                        stage->commands_.push_back(cmd);

                                        if (lexer.token != '}')
                                            AdvanceLexer(lexer, '}', 0);
                                    }
                                    else if (part == "fs_quad" || part == "quad")
                                    {
                                        RS_DrawCmd cmd = { };
                                        cmd.enabled_ = true;
                                        cmd.commandType_ = FullscreenQuad;
                                        cmd.cmdData_.quadData_.shader_ = RS_Identifier::NULL_ID;
                                        cmd.cmdData_.quadData_.inputSize_ = PIPELINE_RESOURCE_BACKBUFFER;
                                        cmd.cmdData_.quadData_.outputSize_ = PIPELINE_RESOURCE_BACKBUFFER;
                                        cmd.numParams_ = 0;

                                        if (AdvanceLexer(lexer, CLEX_id, 0) == 0) { /*error*/ }
                                        cmd.passIdentifier_ = CLEX_GetRSIdentifier(lexer);

                                        AdvanceLexer(lexer, '{', 0);

                                        map<string, shared_ptr<Texture>> namedTex;

                                        while (AdvanceLexer(lexer, CLEX_id))
                                        {
                                            auto name = CLEX_ToString(lexer);
                                            if (name == "enabled")
                                            {
                                                ADVANCE_LEXER_WARN(lexer, '=');
                                                ADVANCE_LEXER_WARN(lexer, CLEX_id);
                                                cmd.enabled_ = CLEX_BoolString(CLEX_ToString(lexer));
                                            }
                                            else if (name == "shader")
                                            {
                                                ADVANCE_LEXER_WARN(lexer, '=');
                                                AdvanceLexer(lexer, CLEX_dqstring);
                                                cmd.cmdData_.quadData_.shader_ = SHADER_PATH_PREFIX + CLEX_ToString(lexer);
                                                ADVANCE_LEXER_WARN(lexer, ';');
                                            }
                                            else if (name == "input_size_info")
                                            {
                                                ADVANCE_LEXER_WARN(lexer, '=');
                                                AdvanceLexer(lexer);
                                                cmd.cmdData_.quadData_.inputSize_ = CLEX_ToString(lexer);
                                                ADVANCE_LEXER_WARN(lexer, ';');
                                            }
                                            else if (name == "output_size_info")
                                            {
                                                ADVANCE_LEXER_WARN(lexer, '=');
                                                AdvanceLexer(lexer);
                                                cmd.cmdData_.quadData_.outputSize_ = CLEX_ToString(lexer);
                                                ADVANCE_LEXER_WARN(lexer, ';');
                                            }
                                            else if (name == "params")
                                            {
                                                auto params = CLEX_ReadParams(lexer);
                                                TransferParamsData(params, cmd.params_, &cmd.numParams_, 32);
                                            }
                                            else if (name == "texture")
                                            {
                                                AdvanceLexer(lexer);
                                                if (lexer.token == CLEX_id)
                                                {
                                                    string name = lexer.string;
                                                    ADVANCE_LEXER_WARN(lexer, '=');
                                                    AdvanceLexer(lexer);
                                                    if (auto found = Texture::LoadFile(device, lexer.string))
                                                    {
                                                        namedTex.insert({ name, found });
                                                        found->GenerateMipMaps();
                                                    }
                                                }
                                                else if (lexer.token == CLEX_intlit)
                                                {
                                                    uint32_t idx = lexer.int_number;
                                                    ADVANCE_LEXER_WARN(lexer, '=');
                                                    AdvanceLexer(lexer);
                                                    if (auto found = Texture::LoadFile(device, lexer.string))
                                                    {
                                                        // the SamplerTraits don't matter because we'll respect the Effect
                                                        cmd.textures_.push_back({ idx, SamplerTraits { FILTER_LINEAR, true }, found });
                                                    }
                                                }
                                                ADVANCE_LEXER_WARN(lexer, ';');
                                            }
                                        }

                                        shared_ptr<Effect> fx(new Effect(device));
                                        shared_ptr<ShaderPass> pass(new ShaderPass(device, cmd.passIdentifier_.name_, PRIM_UNKNOWN));
                                        fx->AddPass(pass);

										// NOTE: path prefix handled above
                                        auto shader = device->GetShader(PixelShader, cmd.cmdData_.quadData_.shader_.name_, {});
                                        if (!shader->Compile())
                                            return nullptr;
                                        if (!pass->Link(device->GetFSTriVertexShader(), shader))
                                        {
                                            LEXER_ERROR("Failed to construct quad pass for shader: %s", cmd.cmdData_.quadData_.shader_.name_);
                                            return nullptr;
                                        }

                                        for (auto named : namedTex)
                                        {
                                            // TODO
                                            cmd.textures_.push_back({ fx->GetTextureSlot(named.first.c_str()), SamplerTraits { FILTER_LINEAR, true }, named.second });
                                        }

                                        cmd.effect_ = fx;
                                        stage->commands_.push_back(cmd);
                                    }
                                    else if (part == "compute")
                                    {
                                        RS_DrawCmd cmd;
                                        cmd.enabled_ = true;
                                        cmd.commandType_ = ComputePass;
                                        cmd.cmdData_.computeData_.groupsX_ = 1;
                                        cmd.cmdData_.computeData_.groupsX_ = 1;
                                        cmd.cmdData_.computeData_.groupsZ_ = 1;
                                        cmd.numParams_ = 0;

                                        if (AdvanceLexer(lexer, CLEX_id, 0) == 0) { /*error*/ }
                                        cmd.passIdentifier_ = CLEX_GetRSIdentifier(lexer);

                                        ADVANCE_LEXER_WARN(lexer, '{');

                                        map<string, shared_ptr<Texture>> namedTex;
                                        map<string, string> namedBuffer;
                                        string csName, csDefines;

                                        while (AdvanceLexer(lexer, CLEX_id))
                                        {
                                            auto token = CLEX_ToString(lexer);
                                            if (token == "cs")
                                            {
                                                ADVANCE_LEXER_WARN(lexer, '=');
                                                ADVANCE_LEXER_WARN(lexer, CLEX_id);
                                                csName = CLEX_ToString(lexer);
                                            }
                                            else if (token == "cs_defines")
                                            {
                                                ADVANCE_LEXER_WARN(lexer, '=');
                                                ADVANCE_LEXER_WARN(lexer, CLEX_id);
                                                csDefines = CLEX_ToString(lexer);
                                            }
                                            else if (token == "enabled")
                                            {
                                                ADVANCE_LEXER_WARN(lexer, '=');
                                                ADVANCE_LEXER_WARN(lexer, CLEX_id);
                                                cmd.enabled_ = CLEX_BoolString(CLEX_ToString(lexer));
                                            }
                                            else if (token == "texture")
                                            {
                                                AdvanceLexer(lexer);
                                                if (lexer.token == CLEX_id)
                                                {
                                                    string name = lexer.string;
                                                    ADVANCE_LEXER_WARN(lexer, '=');
                                                    AdvanceLexer(lexer);
                                                    if (auto found = Texture::LoadFile(device, lexer.string))
                                                        namedTex.insert({ name, found });
                                                }
                                                else if (lexer.token == CLEX_intlit)
                                                {
                                                    uint32_t idx = lexer.int_number;
                                                    ADVANCE_LEXER_WARN(lexer, '=');
                                                    AdvanceLexer(lexer);
                                                    if (auto found = Texture::LoadFile(device, lexer.string))
                                                    {
                                                        // TODO
                                                        cmd.textures_.push_back({ idx, SamplerTraits { FILTER_LINEAR, true }, found });
                                                    }
                                                }
                                                ADVANCE_LEXER_WARN(lexer, ';');
                                            }
                                            else if (token == "buffer")
                                            {
                                                AdvanceLexer(lexer);
                                                if (lexer.token == CLEX_id)
                                                {
                                                    auto key = CLEX_ToString(lexer);
                                                    ADVANCE_LEXER_WARN(lexer, '=');
                                                    ADVANCE_LEXER_WARN(lexer, CLEX_id);
                                                    auto value = CLEX_ToString(lexer);
                                                    namedBuffer.insert({ key, value });
                                                }
                                                else if (lexer.token == CLEX_intlit)
                                                {
                                                    uint32_t idx = lexer.int_number;
                                                    ADVANCE_LEXER_WARN(lexer, '=');
                                                    ADVANCE_LEXER_WARN(lexer, CLEX_id);
                                                    auto value = CLEX_ToString(lexer);
                                                    cmd.buffers_.push_back({ idx, MakeID(value) });
                                                }
                                                ADVANCE_LEXER_WARN(lexer, ';');
                                            }
                                            else if (token == "params")
                                            {
                                                auto params = CLEX_ReadParams(lexer);
                                                TransferParamsData(params, cmd.params_, &cmd.numParams_, 32);
                                            }
                                        }

                                        if (auto shader = device->GetShader(ComputeShader, (SHADER_PATH_PREFIX + csName).c_str(), csName.empty() ? vector<string>() : split(csName)))
                                        {
                                            shared_ptr<Effect> fx(new Effect(device));
                                            shared_ptr<ShaderPass> pass(new ShaderPass(device, cmd.passIdentifier_.name_, PRIM_UNKNOWN));
                                            fx->AddPass(pass);

                                            // should fail for these?
                                            if (!shader->Compile())
                                                return nullptr;
                                            if (!pass->Link(shader))
                                                return nullptr;

                                            cmd.effect_ = fx;
                                            for (auto named : namedTex)
                                                cmd.textures_.push_back({ fx->GetTextureSlot(named.first.c_str()), SamplerTraits { FILTER_LINEAR, true }, named.second });
                                            for (auto named : namedBuffer)
                                                cmd.buffers_.push_back({ fx->GetUBORecord(named.first.c_str())->blockIndex_, MakeID(named.second) });

                                            stage->commands_.push_back(cmd);
                                        }
                                        else
                                        {
                                            // this is a serious issue, we should fail
                                            device->LogFormat(GLVU_ERROR, "Unable to acquire shader for compute pass: %s", cmd.passIdentifier_.name_);
                                            return nullptr;
                                        }
                                    }
                                    else if (part == "clear_targets" || part == "clear")
                                    {
                                        RS_DrawCmd cmd;
                                        cmd.enabled_ = true;
                                        cmd.commandType_ = ClearTargets;
                                        cmd.cmdData_.clearData_.color_[0] = 0.0f;
                                        cmd.cmdData_.clearData_.color_[1] = 0.0f;
                                        cmd.cmdData_.clearData_.color_[2] = 0.0f;
                                        cmd.cmdData_.clearData_.color_[3] = 1.0f;
                                        cmd.cmdData_.clearData_.stencilValue_ = 0;
                                        cmd.cmdData_.clearData_.depth_ = 0;
                                        cmd.cmdData_.clearData_.discardColor_ = true;
                                        cmd.cmdData_.clearData_.discardDepth_ = true;
                                        cmd.cmdData_.clearData_.discardStencil_ = true;

                                        ADVANCE_LEXER_WARN(lexer, '{');

                                        string k, v; float f; float4 vectorValue;
                                        while (ExtractKVP(lexer, k, v, f, vectorValue))
                                        {
                                            if (k == "color")
                                                sscanf(v.c_str(), "%f %f %f %f", &cmd.cmdData_.clearData_.color_[0], &cmd.cmdData_.clearData_.color_[1], &cmd.cmdData_.clearData_.color_[2], &cmd.cmdData_.clearData_.color_[3]);
                                            else if (k == "stencil")
                                                cmd.cmdData_.clearData_.stencilValue_ = f;
                                            else if (k == "depth")
                                                cmd.cmdData_.clearData_.depth_ = f;
                                            else if (k == "discard_color")
                                                cmd.cmdData_.clearData_.discardColor_ = v == "true";
                                            else if (k == "discard_depth")
                                                cmd.cmdData_.clearData_.discardDepth_ = v == "true";
                                            else if (k == "discard_stencil")
                                                cmd.cmdData_.clearData_.discardStencil_ = v == "true";
                                        }
                                        assert(lexer.token ==  '}');

                                        stage->commands_.push_back(cmd);
                                    }
                                    else if (part == "gen_mips")
                                    {
                                        RS_DrawCmd cmd;
                                        cmd.enabled_ = true;
                                        cmd.cmdData_.genMipsData_.layer_ = -1;
                                        cmd.commandType_ = GenMips;

                                        if (AdvanceLexer(lexer, CLEX_id) == 0)
                                            LEXER_ERROR("gen_mips: expected identifier")

                                            RS_Identifier targetID = CLEX_GetRSIdentifier(lexer);
                                        if (AdvanceLexer(lexer) == 0)
                                            LEXER_ERROR("gen_mips: unexpected end of file");

                                        if (lexer.token == CLEX_intlit)
                                        {
                                            cmd.cmdData_.genMipsData_.layer_ = lexer.int_number;
                                            AdvanceLexer(lexer, ';');
                                        }
                                        else if (lexer.token != ';')
                                        {
                                            auto str = CLEX_ToString(lexer);
                                            LEXER_WARNING("Unexpected token: %s", str.c_str());
                                        }

                                        stage->commands_.push_back(cmd);
                                    }
                                    else if (part == "buffer_copy")
                                    {
                                        RS_DrawCmd cmd;
                                        cmd.enabled_ = true;
                                        cmd.commandType_ = BufferCopy;

                                        ADVANCE_LEXER_WARN(lexer, CLEX_id);
                                        cmd.cmdData_.buffCopyData_.source_ = CLEX_GetRSIdentifier(lexer);

                                        ADVANCE_LEXER_WARN(lexer, CLEX_id);
                                        cmd.cmdData_.buffCopyData_.dest_ = CLEX_GetRSIdentifier(lexer);

                                        stage->commands_.push_back(cmd);
                                    }
                                    else if (part == "callback")
                                    {
                                        RS_DrawCmd cmd;
                                        cmd.enabled_ = true;
                                        cmd.commandType_ = RenderCallback;

                                        ADVANCE_LEXER_WARN(lexer, CLEX_id);
                                        cmd.cmdData_.callData_.callID_ = CLEX_GetRSIdentifier(lexer);

                                        AdvanceLexer(lexer);
                                        if (lexer.token == ';')
                                        {
                                            stage->commands_.push_back(cmd);
                                            continue;
                                        }
                                        else if (lexer.token == '{')
                                        {
                                            auto params = CLEX_ReadParams(lexer);
                                            TransferParamsData(params, cmd.params_, &cmd.numParams_, 32);
                                        }
                                        EatTokenIf(lexer, ';');
                                        stage->commands_.push_back(cmd);
                                    }
                                    else if (tokenKey == "blit")
                                    {
                                        RS_DrawCmd cmd;
                                        cmd.enabled_ = true;
                                        cmd.commandType_ = Blit;

                                        ADVANCE_LEXER_WARN(lexer, CLEX_id);
                                        cmd.cmdData_.blitData_.source_ = CLEX_GetRSIdentifier(lexer);
                                        ADVANCE_LEXER_WARN(lexer, CLEX_id);
                                        cmd.cmdData_.blitData_.dest_ = CLEX_GetRSIdentifier(lexer);

                                        if (PeekToken(lexer) == CLEX_id)
                                        {
                                            AdvanceLexer(lexer);
                                            auto str = CLEX_ToString(lexer);
                                            if (str == "mip" || str == "mipmap")
                                                cmd.cmdData_.blitData_.runMipsAfterwards_ = true;
                                        }

                                        EatTokenIf(lexer, ';');

                                        stage->commands_.push_back(cmd);
                                    }
                                    else {
                                        LEXER_WARNING("Unrecognized render_script command '%s'", lexer.string);
                                    }
                                }
                            }

                            if (stage->commands_.size() > 0)
                                script->stages_.push_back(stage);
                            else
                            {
                                LEXER_WARNING("Empty stage: '%s'", stage->self_.name_);
                                delete stage;
                            }
                        }
                        else {
                            LEXER_WARNING("render_script, unrecognized symbol: %s", lexer.string);
                        }
                    }
                    else if (lexer.token == '}')
                        break;
                }
            }

            script->Prepare(device);
            return script;
        }
        else
        {
            // error
            return nullptr;
        }
    }
    else
    {
        // error
        return nullptr;
    }
#else
    DataBlock block;
    block.Parse(lexer);

    if (block.self_.name_ == "render_script")
    {
        shared_ptr<RenderScript> script(new RenderScript(device));

        for (auto& child : block.children_)
        {
            if (child->self_.name_ == "target")
            {
                bool makeGlobal = false;
                RenderTargetInfo* targetInfo = new RenderTargetInfo();
                targetInfo->backbufferWidthFraction_ = targetInfo->backbufferHeightFraction_ = 1.0f;
                targetInfo->width_ = UINT_MAX;
                targetInfo->height_ = UINT_MAX;
                targetInfo->targetFormat_ = TEX_RGB8;
                targetInfo->pingPong_ = false;
                targetInfo->fixedWidth_ = 0;
                targetInfo->fixedHeight_ = 0;

                script->targetTextures_.push_back(targetInfo);

                for (auto& var : child->values_)
                {
                    if (var.first == "format")
                        targetInfo->targetFormat_ = ParseTextureFormat(var.second.sValue_.c_str());
                    else if (var.first == "width")
                        targetInfo->fixedWidth_ = (unsigned)var.second.fValue_;
                    else if (var.first == "height")
                        targetInfo->fixedHeight_ = (unsigned)var.second.fValue_;
                    else if (var.first == "width_fraction")
                        targetInfo->backbufferWidthFraction_ = var.second.fValue_;
                    else if (var.first == "height_fraction")
                        targetInfo->backbufferHeightFraction_ = var.second.fValue_;
                    else if (var.first == "ping_pong")
                        targetInfo->pingPong_ = var.second.bValue_;
                    else if (var.first == "global")
                        makeGlobal = var.second.bValue_;
                }
            }
            else if (child->self_.name_ == "buffer")
            {

            }
            else if (child->self_.name_ == "shadow_technique")
            {

            }
            else if (child->self_.name_ == "stage")
            {
                RenderScriptStage* stage = new RenderScriptStage();
                script->stages_.push_back(stage);
                stage->active_ = true;
                    
                for (auto& var : child->values_)
                {
                    if (var.first.name_ == "enabled")
                        stage->active_ = var.second.bValue_;
                    else if (var.first.name_ == "ban")
                    {
                        if (var.second.identifierList_.size() > 0)
                            stage->ignoreActiveStages_ = var.second.identifierList_;
                        else
                            stage->ignoreActiveStages_.push_back(MakeID(var.second.sValue_));
                    }
                    else if (var.first.name_ == "require")
                    {
                        if (var.second.identifierList_.size() > 0)
                            stage->requireActiveStages_ = var.second.identifierList_;
                        else
                            stage->requireActiveStages_.push_back(MakeID(var.second.sValue_));
                    }
                    else if (var.first == "targets")
                        stage->targets_ = var.second.identifierList_;
                }
                for (auto& command : child->children_)
                {
                    if (command->self_.name_ == "bind_targets")
                    {
                        for (auto& var : command->values_)
                            stage->targetBindings_.push_back({ (unsigned)var.second.fValue_, var.first.name_ });
                    }
                    else if (command->self_.name_ == "pass" || command->self_.name_ == "lighting_pass")
                    {
                        RS_DrawCmd cmd;
                        cmd.enabled_ = true;
                        cmd.cmdData_.drawData_.sortMode_ = ContextSwitch;
                        cmd.cmdData_.drawData_.alphaToCoverage_ = false;
                        cmd.commandType_ = GeometryPass;
                        cmd.passIdentifier_ = command->self_.specifier_.GetID();

                        for (auto& var : command->values_)
                        {
                            if (var.first == "method")
                            {
                                if (var.second.sValue_ == "forward")
                                    cmd.commandType_ = ForwardLights;
                                else if (var.second.sValue_ == "deferred_tiled")
                                    cmd.commandType_ = DeferredTiledLights;
                                else if (var.second.sValue_ == "forward_tiled")
                                    cmd.commandType_ = ForwardTiledLights;
                                else if (var.second.sValue_ == "light_volumes")
                                    cmd.commandType_ = LightVolumes;
                                else if (var.second.sValue_ == "volumes")
                                    cmd.commandType_ = LightVolumes;
                            }
                            else if (var.first == "context")
                                cmd.context_ = var.second.sValue_;
                            else if (var.first == "sort")
                                cmd.cmdData_.drawData_.sortMode_ = ParseSortMode(var.second.sValue_.c_str());
                            else if (var.first == "enabled")
                                cmd.enabled_ = var.second.bValue_;
                        }

                        stage->commands_.push_back(cmd);                            
                    }
                    else if (command->self_ == "compute")
                    {

                    }
                    else if (command->self_ == "quad")
                    {
                        RS_DrawCmd cmd;
                        cmd.commandType_ = FullscreenQuad;
                        cmd.enabled_ = true;
                        cmd.cmdData_.quadData_.inputSize_ = PIPELINE_RESOURCE_BACKBUFFER;
                        cmd.cmdData_.quadData_.outputSize_ = PIPELINE_RESOURCE_BACKBUFFER;
                        cmd.cmdData_.quadData_.numParams_ = 0;

                        cmd.passIdentifier_ = command->self_.specifier_.id_;

                        map<string, shared_ptr<Texture> > namedTextures;
                        shared_ptr<Shader> ps, vs, gs, hs, ds;
                        string psName, vsName, gsName, hsName, dsName;
                        vector<string> psDefines, vsDefines, gsDefines, hsDefines, dsDefines;

                        for (auto& var : command->values_)
                        {
                            if (var.first == "shader")
                            {
                                auto shader = device->GetShader(PixelShader, var.second.sValue_.c_str(), { });
                                auto effect = make_shared<Effect>(device);
                                auto pass = make_shared<ShaderPass>(device, var.second.sValue_.c_str());
                                    
                                if (shader && shader->Compile() && pass->Link(device->GetFSTriVertexShader(), shader))
                                {
                                    effect->AddPass(pass);
                                    cmd.effect_ = effect;
                                }
                                else
                                    return nullptr;
                            }
#define DO_SHADER(NAME)         if (var.first == #NAME "_defines") { NAME ## Defines = split(var.second.sValue_); } \
                            if (var.first == #NAME) { NAME ## Name = var.second.sValue_; }
                            DO_SHADER(vs)
                            DO_SHADER(ps)
                            DO_SHADER(gs)
                            DO_SHADER(hs)
                            DO_SHADER(ds)
#undef DO_SHADER

                            else if (var.first == "input_size_src")
                                cmd.cmdData_.quadData_.inputSize_ = var.second.sValue_;
                            else if (var.first == "output_size_src")
                                cmd.cmdData_.quadData_.outputSize_ = var.second.sValue_;
                            else if (var.first == "params")
                            {
                                cmd.cmdData_.quadData_.numParams_ = var.second.dataValue_.size();
                                memcpy(cmd.cmdData_.quadData_.params_, var.second.dataValue_.data(), sizeof(float) * var.second.dataValue_.size());
                            }
                            else if (var.first == "texture")
                            {
                                if (auto tex = Texture::LoadFile(device, var.second.sValue_.c_str()))
                                {
                                    if (var.first.specifier_.IsNumberValid())
                                        cmd.textures_.push_back({ var.first.specifier_.number_, tex });
                                    else
                                        namedTextures.insert({ var.first.specifier_.GetString(), tex });
                                }
                            }
                            else if (var.first == "buffer")
                            {
                                    
                            }
                        }

                        // if we never hit a shader than we at least need to have hit vs/ps and vs_defines/ps_defines (potentially any of the stages)
                        if (cmd.effect_ == nullptr)
                        {
#define GET_SHADER(NAME, STAGE) if (NAME ## Name.empty() == false) { NAME = device->GetShader(STAGE, NAME ## Name.c_str(), NAME ## Defines); NAME->Compile(); }
                            GET_SHADER(vs, VertexShader);
                            GET_SHADER(ps, VertexShader);
                            GET_SHADER(gs, VertexShader);
                            GET_SHADER(hs, VertexShader);
                            GET_SHADER(ds, VertexShader);
#undef GET_SHADER
                            auto pass = make_shared<ShaderPass>(device, cmd.passIdentifier_.name_);
                            if (pass->Link(vs, ps, gs, hs, ds))
                            {
                                cmd.effect_.reset(new Effect(device));
                                cmd.effect_->AddPass(pass);
                            }
                        }

                        for (auto namedTex : namedTextures)
                            cmd.textures_.push_back({ cmd.effect_->GetTextureSlot(namedTex.first.c_str()), namedTex.second });

                        stage->commands_.push_back(cmd);
                    }
                    else if (command->self_.name_ == "clear")
                    {
                        RS_DrawCmd cmd;
                        cmd.passIdentifier_ = command->self_.specifier_.GetID();
                        cmd.enabled_ = true;
                        cmd.commandType_ = ClearTargets;
                        cmd.cmdData_.clearData_.color_[0] = 0.0f;
                        cmd.cmdData_.clearData_.color_[1] = 0.0f;
                        cmd.cmdData_.clearData_.color_[2] = 0.0f;
                        cmd.cmdData_.clearData_.color_[3] = 1.0f;
                        cmd.cmdData_.clearData_.stencilValue_ = 0;
                        cmd.cmdData_.clearData_.depth_ = 0;
                        cmd.cmdData_.clearData_.discardColor_ = true;
                        cmd.cmdData_.clearData_.discardDepth_ = true;
                        cmd.cmdData_.clearData_.discardStencil_ = true;

                        for (auto& var : command->values_)
                        {
                            if (var.first == "color")
                                sscanf(var.second.sValue_.c_str(), "%f %f %f %f", &cmd.cmdData_.clearData_.color_[0], &cmd.cmdData_.clearData_.color_[1], &cmd.cmdData_.clearData_.color_[2], &cmd.cmdData_.clearData_.color_[3]);
                            else if (var.first == "depth")
                                cmd.cmdData_.clearData_.depth_ = var.second.fValue_;
                            else if (var.first == "stencil")
                                cmd.cmdData_.clearData_.stencilValue_ = (unsigned)var.second.fValue_;
                            else if (var.first == "discard_color")
                                cmd.cmdData_.clearData_.discardColor_ = var.second.bValue_;
                            else if (var.first == "discard_depth")
                                cmd.cmdData_.clearData_.discardDepth_ = var.second.bValue_;
                            else if (var.first == "discard_stencil")
                                cmd.cmdData_.clearData_.discardStencil_ = var.second.bValue_;
                            else if (var.first == "enabled")
                                cmd.enabled_ = var.second.bValue_;
                        }

                        stage->commands_.push_back(cmd);
                    }
                    else if (command->self_.name_ == "blit")
                    {
                        RS_DrawCmd cmd;
                        cmd.enabled_ = true;
                        cmd.passIdentifier_ = command->self_.specifier_.GetID();
                        cmd.cmdData_.blitData_.runMipsAfterwards_ = false;
                        cmd.commandType_ = Blit;

                        for (auto& var : command->values_)
                        {
                            if (var.first == "src")
                                cmd.cmdData_.blitData_.source_ = var.second.sValue_;
                            else if (var.first == "dest")
                                cmd.cmdData_.blitData_.dest_ = var.second.sValue_;
                            else if (var.first == "gen_mips")
                                cmd.cmdData_.blitData_.runMipsAfterwards_ = var.second.bValue_;
                            else if (var.first == "enabled")
                                cmd.enabled_ = var.second.bValue_;
                        }
                    }
                    else if (command->self_.name_ == "gen_mips")
                    {
                        RS_DrawCmd cmd;
                        cmd.enabled_ = true;
                        cmd.passIdentifier_ = command->self_.specifier_.GetID();
                        cmd.commandType_ = GenMips;

                        for (auto& var : command->values_)
                        {
                            if (var.first == "enabled")
                                cmd.enabled_ = var.second.bValue_;
                            else if (var.first == "target")
                                cmd.cmdData_.genMipsData_.texture_ = var.second.sValue_;
                        }
                    }
                    else if (command->self_.name_ == "swap")
                    {
                            
                    }
                }
            }
        }

        return script;
    }
#endif
    return nullptr;
}

//****************************************************************************
//
//  Function:   Effect::LoadEffect
//
//  Purpose:    Wrapper, just calls the overload with the blob data.
//
//  Return:     Loaded effect if success.
//
//****************************************************************************
shared_ptr<Effect> Effect::LoadEffect(GraphicsDevice* device, const char* fileName)
{
    auto blob = device->GetResourceData(Resource_Effect, fileName);
    if (blob.data_ == nullptr || blob.size_ == 0)
    {
        device->LogFormat(GLVU_ERROR, "Failed to load effect: %s", fileName);
        return nullptr;
    }
    return LoadEffect(device, blob.data_, blob.size_);
}

//****************************************************************************
//
//  Function:   Effect::LoadEffect
//
//  Purpose:    Loads an effect file, loading shaders, textures, and creates UBOs/SSBOs
//              as required.
//
//  Return:     Loaded effect if success, null if there were errors.
//
//****************************************************************************
shared_ptr<Effect> Effect::LoadEffect(GraphicsDevice* device, const char* buffer, size_t fileSize)
{
    if (buffer == nullptr || fileSize == 0)
    {
        device->LogMessage("Failed to load effect", GLVU_ERROR);
        return nullptr;
    }

    char lexerStorage[1024];
    memset(lexerStorage, 0, sizeof(lexerStorage));

    stb_lexer lexer;
    stb_c_lexer_init(&lexer, buffer, buffer + fileSize, lexerStorage, 1024);
    lexer.user_data = device;

    if (AdvanceLexer(lexer, CLEX_id, "effect") != 0)
    {
        shared_ptr<Effect> effect(new Effect(device));
        EatTokenIf(lexer, CLEX_id);
        EatTokenIf(lexer, '{');

        bool isSkinned = false;
        bool canInstance = false;
        while (AdvanceLexer(lexer))
        {
            if (lexer.token == CLEX_id)
            {
                auto name = CLEX_String(lexer);
                if (name == "skin")
                {
                    ADVANCE_LEXER_WARN(lexer, '=');
                    ADVANCE_LEXER_WARN(lexer, CLEX_id);
                    isSkinned = CLEX_BoolString(CLEX_ToString(lexer));
                }
                else if (name == "instance")
                {
                    ADVANCE_LEXER_WARN(lexer, '=');
                    ADVANCE_LEXER_WARN(lexer, CLEX_id);
                    canInstance = CLEX_BoolString(CLEX_ToString(lexer));
                }
                else if (name == "alias")
                {
                    ADVANCE_LEXER_WARN(lexer, CLEX_id);
                    auto realName = CLEX_ToString(lexer);
                    ADVANCE_LEXER_WARN(lexer, CLEX_id);
                    auto aliasName = CLEX_ToString(lexer);
                    if (aliasName == "as")
                    {
                        AdvanceLexer(lexer, CLEX_id);
                        aliasName = CLEX_ToString(lexer);
                    }

                    effect->aliasNames_.insert({ aliasName, realName });
                }
                else if (name == "pass" || name == "context")
                {
                    ADVANCE_LEXER_WARN(lexer, CLEX_id);
                    auto passName = CLEX_String(lexer);
                    PrimitiveType forPrim = PRIM_UNKNOWN;

                    AdvanceLexer(lexer);
                    if (lexer.token != '{' && lexer.token != ':')
                        LEXER_WARNING("Unexpected token %u, expected: { or :", lexer.token);

                    if (lexer.token == ':')
                    {
                        ADVANCE_LEXER_WARN(lexer, CLEX_id);
                        forPrim = ParsePrimitiveType(lexer.string);
                        ADVANCE_LEXER_WARN(lexer, '{');
                    }

                    shared_ptr<ShaderPass> pass(new ShaderPass(device, passName.c_str(), forPrim));

                    string k, v; float f; float4 vectorValue;
                    shared_ptr<Shader> vs, ps, gs, hs, ds, cs;
                    string vsName, psName, gsName, hsName, dsName, csName;
					vector<string> allDefines;
                    vector<string> vsDefines, psDefines, gsDefines, hsDefines, dsDefines, csDefines;

                    while (ExtractKVP(lexer, k, v, f, vectorValue))
                    {
						if (k == "defines") allDefines = split(v);

#define HANDLE_SHADER_TYPE(NAME) if (k == #NAME) NAME ## Name = v; \
                                if (k == #NAME "_defines") { NAME ## Defines = split(v); }
                        HANDLE_SHADER_TYPE(vs);
                        HANDLE_SHADER_TYPE(ps);
                        HANDLE_SHADER_TYPE(gs);
                        HANDLE_SHADER_TYPE(hs);
                        HANDLE_SHADER_TYPE(ds);
                        HANDLE_SHADER_TYPE(cs);
#undef HANDLE_SHADER_TYPE

#define HANDLE_DRAW_STATE_BOOL(NAME, VAR) if (k == #NAME) { pass->GetDrawState().VAR = CLEX_BoolString(v); }
#define HANDLE_DRAW_STATE_PARSE(NAME, VAR, PARSEFUNC) if (k == #NAME) { pass->GetDrawState().VAR = PARSEFUNC(v.c_str()); }

                        HANDLE_DRAW_STATE_BOOL(depth_test, depthTest_);
                        HANDLE_DRAW_STATE_BOOL(depth_write, depthWrite_);
                        HANDLE_DRAW_STATE_BOOL(alpha_test, alphaTest_);
                        HANDLE_DRAW_STATE_BOOL(alpha_to_coverage, alphaToCoverage_);
                        HANDLE_DRAW_STATE_PARSE(blend, blendMode_, ParseBlendMode);
                        HANDLE_DRAW_STATE_PARSE(depth_compare, depthCompare_, ParseComparison);
                        HANDLE_DRAW_STATE_PARSE(cull, culling_, ParseCullingMode);
                        HANDLE_DRAW_STATE_PARSE(culling, culling_, ParseCullingMode);
                        if (k == "stencil_write")
                            pass->GetDrawState().stencilWrite_ = (uint32_t)f;
                        else if (k == "depth_bias")
                            pass->GetDrawState().depthBias_ = f;
                        else if (k == "stencil_mask")
                        {
                            pass->GetDrawState().stencilTest_ = true;
                            pass->GetDrawState().stencilMask_ = (uint32_t)f;
                        }
                        else if (k == "slope_bias")
                            pass->GetDrawState().slopeBias_ = f;

#undef HANDLE_DRAW_STATE_PARSE
#undef HANDLE_DRAW_STATE_BOOL

                    }

					// if we have all purpose defines then pump them in
					if (!allDefines.empty())
					{
						vsDefines.insert(vsDefines.end(), allDefines.begin(), allDefines.end());
						hsDefines.insert(hsDefines.end(), allDefines.begin(), allDefines.end());
						dsDefines.insert(dsDefines.end(), allDefines.begin(), allDefines.end());
						gsDefines.insert(gsDefines.end(), allDefines.begin(), allDefines.end());
						psDefines.insert(psDefines.end(), allDefines.begin(), allDefines.end());
						csDefines.insert(csDefines.end(), allDefines.begin(), allDefines.end());
					}

#define GET_SHADER(NAME, STAGE) if (! NAME ## Name.empty()) { NAME = device->GetShader(STAGE ## Shader, (SHADER_PATH_PREFIX + NAME ## Name).c_str(), NAME ## Defines); if (NAME) NAME->Compile(); }
                    GET_SHADER(vs, Vertex);
                    GET_SHADER(ps, Pixel);
                    GET_SHADER(gs, Geometry);
                    GET_SHADER(hs, Hull);
                    GET_SHADER(ds, Domain);
                    GET_SHADER(cs, Compute);
#undef GET_SHADER

                    if (cs == nullptr)
                    {
                        if (pass->Link(vs, ps, gs, hs, ds))
                        {
                            effect->AddPass(pass);

                            //// these are built ins
                            if (canInstance)
                            {
                                string varName = pass->GetName();
                                varName += SHADER_CONTEXT_SUFFIX_INST;
                                if (auto instancedVersion = pass->GetVariation(varName.c_str(), { SHADER_DEFINE_INST }))
                                    effect->AddPass(instancedVersion);
                            }
                            if (isSkinned)
                            {
                                string varName = pass->GetName();
                                varName += SHADER_CONTEXT_SUFFIX_SKINNED;
                                if (auto skinnedVersion = pass->GetVariation(varName.c_str(), { SHADER_DEFINE_SKINNED }))
                                    effect->AddPass(skinnedVersion);
                            }

                            //???if (strcmp(pass->GetName(), "SHADOW") == 0)
                            //???{
                            //???    string varName = pass->GetName();
                            //???
                            //???    if (auto inst = pass->GetVariation(varName + "_POINTLIGHT", { "POINTLIGHT" }))
                            //???        effect->AddPass(inst);
                            //???    if (auto inst = pass->GetVariation(varName + "_SPOTLIGHT", { "SPOTLIGHT" }))
                            //???        effect->AddPass(inst);
                            //???    if (auto inst = pass->GetVariation(varName + "_DIRLIGHT", { "DIRLIGHT" }))
                            //???        effect->AddPass(inst);
                            //???
                            //???    if (canInstance)
                            //???    {
                            //???        if (auto inst = pass->GetVariation(varName + "_POINTLIGHT_INST", { "POINTLIGHT", SHADER_DEFINE_INST }))
                            //???            effect->AddPass(inst);
                            //???        if (auto inst = pass->GetVariation(varName + "_SPOTLIGHT_INST", { "SPOTLIGHT", SHADER_DEFINE_INST }))
                            //???            effect->AddPass(inst);
                            //???        if (auto inst = pass->GetVariation(varName + "_DIRLIGHT_INST", { "DIRLIGHT", SHADER_DEFINE_INST }))
                            //???            effect->AddPass(inst);
                            //???    }
                            //???
                            //???    if (isSkinned)
                            //???    {
                            //???        if (auto inst = pass->GetVariation(varName + "_POINTLIGHT_SKINNED", { "POINTLIGHT", SHADER_DEFINE_SKINNED }))
                            //???            effect->AddPass(inst);
                            //???        if (auto inst = pass->GetVariation(varName + "_SPOTLIGHT_SKINNED", { "SPOTLIGHT", SHADER_DEFINE_SKINNED }))
                            //???            effect->AddPass(inst);
                            //???        if (auto inst = pass->GetVariation(varName + "_DIRLIGHT_SKINNED", { "DIRLIGHT", SHADER_DEFINE_SKINNED }))
                            //???            effect->AddPass(inst);
                            //???    }
                            //???}
                        }
                        else
                        {
                            LEXER_ERROR("Effect::Load: Could not link pass: %s", passName.c_str());
                            return nullptr;
                        }
                    }
                    else
                    {
                        // what should be done for compute?
                    }
                }
                else if (name == "sampler")
                {
                    ADVANCE_LEXER_WARN(lexer, CLEX_id);
                    RS_Identifier samplerName = CLEX_GetRSIdentifier(lexer);
                    ADVANCE_LEXER_WARN(lexer, '{');
                        
					// this is about IO, device defaults mean nothing
                    SamplerTraits sampler = { FILTER_LINEAR, true };
                    uint32_t slot = 0;
                    shared_ptr<Texture> defTex;

                    string k, v; float f; float4 vectorValue;
                    while (ExtractKVP(lexer, k, v, f, vectorValue))
                    {
                        if (k == "filter")
                            sampler.filter_ = ParseTextureFilter(v.c_str());
                        else if (k == "wrap")
                            sampler.wrap_ = v == "on";
                        else if (k == "slot")
                            slot = (int)f;
                        else if (k == "default")
                            defTex = Texture::LoadFile(device, v.c_str());
                    }

                    effect->samplers_.push_back({ slot, sampler });
                    if (defTex)
                        effect->defaultTextures_.push_back({ slot, defTex });

                    if (lexer.token != '}')
                        AdvanceLexer(lexer);
                }
            }
        }

        AdvanceLexer(lexer, '}');
        return effect;
    }
    else
    {
        //error
    }
    return nullptr;
}

//****************************************************************************
//
//  Function:   Material::Load
//
//  Purpose:    Wrapper
//
//  Return:     New material if good, null if there were errors.
//
//****************************************************************************
shared_ptr<Material> Material::Load(GraphicsDevice* device, const char* fileName)
{
    auto blob = device->GetResourceData(Resource_Material, fileName);
    if (blob.size_ > 0)
        return Load(device, blob.data_, blob.size_);
    else
        device->LogFormat(GLVU_ERROR, "Failed to load material: %s", fileName);
    return nullptr;
}

//****************************************************************************
//
//  Function:   Material::Load
//
//  Purpose:    Loads a material, as well as any effects, shaders, textures, UBOs/SSBOs
//
//  Return:     Doesn't load any geometries.
//
//****************************************************************************
shared_ptr<Material> Material::Load(GraphicsDevice* device, const char* buffer, size_t fileSize)
{
    if (buffer == nullptr || fileSize == 0)
    {
        device->LogMessage("Failed to load material", GLVU_ERROR);
        return nullptr;
    }

    char lexerStorage[1024];
    memset(lexerStorage, 0, sizeof(lexerStorage));

    stb_lexer lexer;
    stb_c_lexer_init(&lexer, buffer, buffer + fileSize, lexerStorage, 1024);
    lexer.user_data = device;

#if 0
    DataBlock block;
    block.Parse(lexer);

    if (block.self_ == "material")
    {
        auto materialName = block.self_.name_;

        shared_ptr<Material> material;
        shared_ptr<Effect> effect;
        bool isLit = true;
        bool isShadowed = true;
        bool castsShadows = false;
        uint32_t viewMask = 0xFFFFFFFF, shadowMask = 0xFFFFFFFF, lightMask = 0xFFFFFFFF;

        typedef pair<string, vector<float> > UBODataFill;
        vector< UBODataFill > uboData;
        Material::TextureBindingList textures;
        map<string, shared_ptr<Texture> > namedTextures;

        for (auto& var : block.values_)
        {
            if (var.first == "lit")
                isLit = var.second.bValue_;
            else if (var.first == "cast_shadow")
                castsShadows = var.second.bValue_;
            else if (var.first == "is_shadowed")
                isShadowed = var.second.bValue_;
            else if (var.first == "shadowed")
                isShadowed = castsShadows = var.second.bValue_;
            else if (var.first == "shadow_mask")
                shadowMask = (unsigned)var.second.fValue_;
            else if (var.first == "light_mask")
                lightMask = (unsigned)var.second.fValue_;
            else if (var.first == "view_mask")
                viewMask = (unsigned)var.second.fValue_;
            else if (var.first == "effect")
                effect = device->GetEffect(var.second.sValue_);
            else if (var.first == "texture")
            {
                if (auto tex = Texture::LoadFile(device, var.second.sValue_.c_str()))
                {
                    if (var.first.specifier_.IsNumberValid())
                        textures.push_back({ var.first.specifier_.number_, tex });
                    else
                        namedTextures.insert({ var.first.specifier_.GetString(), tex });
                }
            }
            else if (var.first == "ubo")
            {
                uboData.push_back({ var.first.specifier_.GetString(), var.second.dataValue_ });
            }
        }

        material.reset(new Material(effect));
        material->castShadows_ = castsShadows;
        material->receiveShadows_ = isShadowed;
        material->viewMask_ = viewMask;
        material->lightMask_ = lightMask;
        material->shadowMask_ = shadowMask;
        material->lit_ = isLit;
        material->textures_ = textures;
        for (auto& namedTex : namedTextures)
        {
            auto slot = effect->GetTextureSlot(namedTex.first.c_str());
            if (slot != UINT_MAX)
                material->textures_.push_back({ slot, namedTex.second });
            else
            {
                //TODO
            }
        }
        for (auto& ubo : uboData)
        {
            shared_ptr<Buffer> buffer = device->CreateUniformBuffer();
            buffer->SetData((void*)ubo.second.data(), ubo.second.size() * sizeof(float));
            material->SetUBO(effect->GetUBORecord(ubo.first.c_str())->blockIndex_, buffer);
        }

        return material;
    }
#endif

#if 1
    map<string, shared_ptr<Material> > materialsInGroup;

    if (AdvanceLexer(lexer, CLEX_id, "material") != 0)
    {
        ADVANCE_LEXER_WARN(lexer, CLEX_id);
        shared_ptr<Material> material;
        shared_ptr<Effect> effect;
        bool isLit = true;
        bool isShadowed = true;
        bool castsShadows = false;
        uint32_t viewMask = 0xFFFFFFFF, shadowMask = 0xFFFFFFFF, lightMask = 0xFFFFFFFF;
        RS_Identifier matName = CLEX_GetRSIdentifier(lexer);

        typedef pair<string, vector<float> > UBODataFill;
        vector< UBODataFill > uboData;
        Material::TextureBindingList textures;
        map<string, shared_ptr<Texture> > namedTextures;
        map<string, shared_ptr<Buffer> > namedBuffers;

        ADVANCE_LEXER_WARN(lexer, '{');
        if (lexer.token == ':')
        {
            ADVANCE_LEXER_WARN(lexer, CLEX_id);
            auto baseClassName = CLEX_ToString(lexer);

            auto foundBaseClass = materialsInGroup.find(baseClassName);
            if (foundBaseClass != materialsInGroup.end())
            {
                auto srcMat = foundBaseClass->second;;
                castsShadows = srcMat->castShadows_;
                isShadowed = srcMat->receiveShadows_;
                isLit = srcMat->lit_;
                effect = srcMat->effect_;
                textures = srcMat->textures_;
            }
            else
                device->LogFormat(GLVU_WARNING, "Unknown material baseclass: %s", baseClassName.c_str());

            ADVANCE_LEXER_WARN(lexer, '{');
        }

        while (AdvanceLexer(lexer, CLEX_id))
        {
            auto name = CLEX_ToString(lexer);

            if (name == "effect")
            {
                ADVANCE_LEXER_WARN(lexer, '=');
                AdvanceLexer(lexer);
                effect = device->GetEffect(CLEX_ToString(lexer));
                AdvanceLexer(lexer, ';');
            }
#define DO_MASK(NAME, STRNAME) else if (name == STRNAME) { ADVANCE_LEXER_WARN(lexer, '='); AdvanceLexer(lexer); if (lexer.token == CLEX_intlit) { NAME = (uint32_t)lexer.int_number; } ADVANCE_LEXER_WARN(lexer, ';'); }
            DO_MASK(viewMask, "view_mask")
            DO_MASK(lightMask, "light_mask")
            DO_MASK(shadowMask, "shadow_mask")
#undef DO_MASK
            else if (name == "texture" || name == "tex")
            {
                AdvanceLexer(lexer);
                uint32_t slot = -1;
                if (lexer.token == CLEX_id || lexer.token == CLEX_dqstring)
                {
                    auto slotName = CLEX_ToString(lexer);
                    ADVANCE_LEXER_WARN(lexer, '=');
                    AdvanceLexer(lexer);
                    auto texName = CLEX_ToString(lexer);

                    if (lexer.token == CLEX_dqstring)
                        namedTextures.insert({ slotName, Texture::LoadFile(device, texName.c_str()) });
                    else if (lexer.token == CLEX_id) // system texture
                    {
                        if (auto found = device->GetSystemTexture(texName.c_str()))
                            namedTextures.insert({ slotName, device->GetSystemTexture(texName.c_str()) });
                        else
                        {
                            device->LogFormat(GLVU_ERROR, "Unknown system buffer: %s", texName.c_str());
                        }
                    }
                    else
                    {
                        LEXER_ERROR("Material: unexpected value in 'texture', expected string or ID");
                    }

                    ADVANCE_LEXER_WARN(lexer, ';');
                }
                else if (lexer.token == CLEX_intlit)
                {
                    uint32_t idx = (uint32_t)lexer.int_number;
                    ADVANCE_LEXER_WARN(lexer, '=');
                    ADVANCE_LEXER_WARN(lexer, CLEX_dqstring);
                    auto texName = CLEX_ToString(lexer);

                    // when inheriting it's necessary to check for an existing record
                    bool texFound = false;
                    for (auto& t : textures)
                    {
                        if (t.slot_ == idx)
                        {
                            t.texture_ = Texture::LoadFile(device, texName.c_str());
                            texFound = true;
                        }
                    }
                    if (!texFound)
                        textures.push_back({ idx, { FILTER_LINEAR, true }, Texture::LoadFile(device, texName.c_str()) });

                    ADVANCE_LEXER_WARN(lexer, ';');
                }
            }
            else if (name == "ubo")
            {
                ADVANCE_LEXER_WARN(lexer, CLEX_id);
                auto uboName = CLEX_ToString(lexer);
                vector<float> data;
                AdvanceLexer(lexer);
                if (lexer.token == '{')
                {
                    do {
                        AdvanceLexer(lexer);
                        if (lexer.token == CLEX_id && CLEX_ToString(lexer) == "internal")
                        {
                            continue;
                            ADVANCE_LEXER_WARN(lexer, ';');
                        }

                        if (CLEX_IsNumeric(lexer.token))
                            data.push_back(CLEX_GetNumber(lexer));
                    } while (lexer.token != '}');

                    if (!data.empty())
                        uboData.push_back({ uboName, data });
                }
                else if (lexer.token == CLEX_id)
                {
                    auto src = CLEX_ToString(lexer);
                    if (auto found = device->GetSystemBuffer(src.c_str()))
                        namedBuffers.insert({ uboName, found });
                    else
                    {
                        device->LogFormat(GLVU_ERROR, "Unknown system buffer: %s", src.c_str());
                    }
                    ADVANCE_LEXER_WARN(lexer, ';');
                }
            }
            else if (name == "lit")
            {
                ADVANCE_LEXER_WARN(lexer, '=');
                AdvanceLexer(lexer);
                isLit = CLEX_BoolString(CLEX_ToString(lexer));
                ADVANCE_LEXER_WARN(lexer, ';');
            }
            else if (name == "cast_shadow" || name == "cast_shadows")
            {
                ADVANCE_LEXER_WARN(lexer, '=');
                AdvanceLexer(lexer);
                castsShadows = CLEX_BoolString(CLEX_ToString(lexer));
                ADVANCE_LEXER_WARN(lexer, ';');
            }
            else if (name == "recieve_shadows")
            {
                ADVANCE_LEXER_WARN(lexer, '=');
                AdvanceLexer(lexer);
                isShadowed = CLEX_BoolString(CLEX_ToString(lexer));
                ADVANCE_LEXER_WARN(lexer, ';');
            }
            else if (name == "shadowed" || name == "shadow")
            {
                ADVANCE_LEXER_WARN(lexer, '=');
                AdvanceLexer(lexer);
                castsShadows = isShadowed = CLEX_BoolString(CLEX_ToString(lexer));
                ADVANCE_LEXER_WARN(lexer, ';');
            }
        }

        if (effect)
        {
            material.reset(new Material(effect));
            material->lit_ = isLit;
            material->castShadows_ = castsShadows;
            material->receiveShadows_ = isShadowed;
            material->lightMask_ = lightMask;
            material->shadowMask_ = shadowMask;
            material->viewMask_ = viewMask;
            material->ApplyEffect();
                
            for (const auto& data : uboData)
            {
                shared_ptr<Buffer> ubo = device->CreateUniformBuffer();
                ubo->SetData((void*)data.second.data(), data.second.size() * sizeof(float));
                material->SetUBO(0, ubo);
            }

            material->textures_ = textures;
            for (auto& named : namedTextures)
                material->SetTexture(named.first.c_str(), named.second);

            materialsInGroup.insert({ matName.name_, material });
        }
        else
            device->LogFormat(2, "No effect found for material: %s", matName.name_);

        return material;
    }
#endif
    return nullptr;
}

bool HJSON_To_RenderSscript(GraphicsDevice* device, const Hjson::Value& root, std::shared_ptr<RenderScript> target)
{
    auto targets = root["targets"];
    for (auto target : targets)
    {
        auto targetName = target.first;
        auto& tgtNode = target.second;

        string format = tgtNode["format"];
        int w = tgtNode["width"].to_int64();
        int h = tgtNode["height"].to_int64();
        float wFrac = tgtNode["width_fraction"].to_float();
        float hFrac = tgtNode["height_fraction"].to_float();
        bool pingPong = tgtNode["ping_pong"];
        bool mips = tgtNode["mips"];
        bool globa = tgtNode["global"];
    }

    auto stages = root["stages"];
    for (auto s = 0ull; s < stages.size(); ++s)
    {
        const auto& stage = stages[s];
        auto stageName = stage.key(s);

        bool enabled = stage["enabled"];
        string banRule = stage["ban"];
        string requireRule = stage["require"];

        auto targets = stage["targets"];
        for (size_t i = 0; i < targets.size(); ++i)
        {
            string targetName = targets[i];
        }

        auto bindings = stage["bind"];
        for (auto& bind : bindings)
        {
            string bindName = bind.first;
            int bindSlot = 0;
            TextureFilter filter = FILTER_LINEAR;
            bool wrap = false;
            if (bind.second.type() == Hjson::Value::Type::MAP)
            {
                string filtString = bind.second["filter"];
                bindSlot = bind.second["slot"];
                filter = ParseTextureFilter(filtString.c_str());
                wrap = bind.second["wrap"];
            }
            else
                bindSlot = (double)bind.second;
        }

        auto cmds = stage["commands"];
        for (size_t cmdIdx = 0; cmdIdx < cmds.size(); ++cmdIdx)
        {
            const auto& cmdNode = cmds[cmdIdx];
            string cmdName = cmdNode["name"];
            string cmdType = cmdNode["type"];
        }
    }

    return false;
}

bool HJSON_To_Effect(GraphicsDevice* device, const Hjson::Value& root, std::shared_ptr<Effect> target)
{
    auto aliasTable = root["alias"];
    for (auto alias : aliasTable)
    {

    }

    bool isSkinned = root["skinned"].to_bool();
    bool isInstanced = root["instance"].to_bool();

    auto samplers = root["samplers"];
    for (auto& sampler : samplers)
    {
        auto samplerName = sampler.first;
        std::string filtText = sampler.second["filter"];
        bool filtWrap = sampler.second["wrap"];
        std::string defTex = sampler.second["default"];
        
        auto filterMode = ParseTextureFilter(filtText.c_str());

    }

    auto contexts = root["contexts"];
    for (auto i = 0ul; i < contexts.size(); ++i)
    {
        auto ctxNode = contexts[i];

        auto ctxName = contexts.key(i);
        auto geomType = ctxNode["geometry"].to_string("unknown");
        std::string defines = ctxNode["defines"];
        std::string vsDefines = ctxNode["vs_defines"];
        std::string hsDefines = ctxNode["hs_defines"];
        std::string dsDefines = ctxNode["ds_defines"];
        std::string gsDefines = ctxNode["gs_defines"];
        std::string psDefines = ctxNode["ps_defines"];

        std::string vs = ctxNode["vs"];
        std::string ds = ctxNode["vs"];
        std::string hs = ctxNode["vs"];
        std::string gs = ctxNode["vs"];
        std::string ps = ctxNode["vs"];

        std::string blendMode = ctxNode["blend"].to_string("NONE");
        bool depthTest = ctxNode["depth_test"].to_bool(true);
        bool depthWRite = ctxNode["depth_write"].to_bool(true);
        float depthBias = ctxNode["depth_bias"].to_float();
        float slopeBias = ctxNode["slope_bias"].to_float();
        bool alphaToCoverage = ctxNode["alpha_to_coverage"].to_bool();
        bool usesDiscard = ctxNode["uses_discard"].to_bool();
    }

    return false;
}

bool HJSON_To_Material(GraphicsDevice* device, const Hjson::Value& root, std::shared_ptr<Material> target)
{
    auto fx = root["effect"];
    if (!fx.defined())
        return false;

    bool isLit = root["lit"].to_bool(true);
    bool shadowed = root["shadowed"].to_bool(false);
    bool castShadows = root["cast_shadow"].to_bool(false) | shadowed;
    bool receiveShadows = root["get_shadow"].to_bool(false) | shadowed;

    std::shared_ptr<Effect> effect;
    for (int i = 0; i < root.size(); ++i)
    {
        auto key = root.key(i);
        auto slot = effect->GetTextureSlot(key.c_str());
        if (slot != UINT_MAX)
        {
            auto texName = root[i].to_string();
        }
    }

    return false;
}

bool HJSON_To_Material(GraphicsDevice* device, const char* data, size_t dataLen, std::shared_ptr<Material> target)
{
    auto root = Hjson::Unmarshal(data, dataLen);
    if (root.type() != Hjson::Value::Type::UNDEFINED)
        return HJSON_To_Material(device, root, target);
    // todo
    return false;
}

}
