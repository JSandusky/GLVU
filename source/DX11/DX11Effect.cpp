//****************************************************************************
//
//  File:       DX11Effect.cpp
//  License:    MIT
//  Project:    GLVU
//  Contents:   Implementation of DX11 specific shader compilation / reflection.
//
//****************************************************************************

#include "Effect.h"

#include "GraphicsDevice.h"
#include "glvu_math.h"

#include <array>

#include <d3d11.h>
#include <d3dcompiler.h>

#define DXERROR(MESSAGE) GetDevice()->LogFormat(GLVU_ERROR, MESSAGE ": HRESULT %X", hr)

#define STAGES_LIST \
	X(VertexShader, "vertex shader") \
	X(PixelShader, "pixel shader") \
	X(GeometryShader, "geometry shader") \
	X(HullShader, "hull shader") \
	X(DomainShader, "domain shader") \
	X(ComputeShader, "compute shader")

using namespace std;

namespace GLVU
{

//****************************************************************************
//
//  Function:   Shader::Shader
//
//  Purpose:    Construct, zero-init and setup code/defs
//
//****************************************************************************
Shader::Shader(GraphicsDevice* device, const string& name, ShaderType kind, ShaderCodeType codeType, const string& code, const vector<string>& defines) :
    GPUObject(device),
    kind_(kind),
    codeType_(codeType),
    code_(code),
    shader_(nullptr),
    name_(name),
    defines_(defines)
{
}

//****************************************************************************
//
//  Function:   Shader::~Shader
//
//  Purpose:    Destruct
//
//****************************************************************************
Shader::~Shader()
{
    Release();
}

//****************************************************************************
//
//  Function:   Shader::Release
//
//  Purpose:    Destroy shader and wipe memory used by code/defines.
//
//****************************************************************************
void Shader::Release()
{
    if (shader_)
        ((ID3D11Resource*)shader_)->Release();
    shader_ = nullptr;
    code_.clear();
    defines_.clear();
}

//****************************************************************************
//
//  Function:   Shader::IsValid
//
//  Purpose:    Utility
//
//  Return:     True if this shader is probably good.
//
//****************************************************************************
bool Shader::IsValid() const
{
    return shader_ != nullptr;
}

//****************************************************************************
//
//  Function:   Shader::Compile
//
//  Purpose:    Attempts to compile the shader, logging DX11 compiler output.
//
//  Return:     True if success, read log for errors.
//
//****************************************************************************
bool Shader::Compile()
{
	if (code_.empty())
	{
		GetDevice()->LogMessage("Attempted to compile shader without code", GLVU_WARNING);
		return false;
	}

	std::vector<std::string> defines = defines_;
	static const char* table_entryPoint[] = {
		"VS",
		"PS",
		"GS",
		"HS",
		"DS",
		"main", // CS
	};
	static const char* table_stageDef[] = {
		"COMPILEVS",
		"COMPILEPS",
		"COMPILEGS",
		"COMPILEHS",
		"COMPILEDS",
		"COMPILECS",
	};
	static const char* table_stageProfile[] = {
		"vs_5_0",
		"ps_5_0",
		"gs_5_0",
		"hs_5_0",
		"ds_5_0",
		"cs_5_0",
	};
	
	const char* entryPoint = table_entryPoint[kind_];
	defines.push_back(table_stageDef[kind_]);
	
	std::vector<D3D_SHADER_MACRO> macros;
	for (auto& def : defines)
	{
		D3D_SHADER_MACRO macro;
		macro.Name = def.c_str();
		macro.Definition = "1";
		macros.push_back(macro);
	}
	D3D_SHADER_MACRO endMacro;
	endMacro.Name = nullptr;
	endMacro.Definition = nullptr;
	macros.push_back(endMacro);

	ID3DBlob* errorMsgs = nullptr;
	unsigned flags = 0;
	
	auto hr = D3DCompile(code_.c_str(), code_.length(), name_.c_str(), macros.data(), nullptr,
		entryPoint, table_stageProfile[kind_], flags, 0, &shaderByteCode_, &errorMsgs);

	static const char* table_stageName[] = {
		"vertex shader",
		"pixel shader",
		"geometry shader",
		"hull shader",
		"domain shader",
		"compute shader"
	};

	if (FAILED(hr))
	{
#define X(A,B) if (kind_ == A) { DXERROR("Failed to compile " B); }
		STAGES_LIST
#undef X		
		auto errMsg = std::string((const char*)errorMsgs->GetBufferPointer(), (unsigned)errorMsgs->GetBufferSize() - 1);
		GetDevice()->LogMessage(errMsg.c_str(), GLVU_WARNING);
		Release();
		return false;
	}

	std::string msg = "Compiled ";
	msg += table_stageName[kind_];
	msg += ": ";
	msg += name_;
	GetDevice()->LogMessage(msg.c_str(), GLVU_INFO);

#define DO_SHADER(STAGE) if (kind_ == STAGE) { \
	hr = GetDevice()->GetD3DDevice()->Create ## STAGE (shaderByteCode_->GetBufferPointer(), shaderByteCode_->GetBufferSize(), nullptr, (ID3D11 ## STAGE **)&shader_); \
	if (FAILED(hr)) { \
		GetDevice()->LogFormat(GLVU_ERROR, "Failed to construct shader from byte-code: %x", hr); \
		if (shader_) shader_->Release(); \
		return false; \
	} }

	DO_SHADER(VertexShader);
	DO_SHADER(PixelShader);
	DO_SHADER(GeometryShader);
	DO_SHADER(DomainShader);
	DO_SHADER(HullShader);
	DO_SHADER(ComputeShader);

#undef DO_SHADER

	ID3D11ShaderReflection* reflection = nullptr;
	hr = D3DReflect(shaderByteCode_->GetBufferPointer(), shaderByteCode_->GetBufferSize(), IID_ID3D11ShaderReflection, (void**)&reflection);
	if (FAILED(hr))
	{
		DXERROR("Failed to reflect shader");
		if (reflection)
			reflection->Release();
		Release();
		return false;
	}

    if (kind_ == ComputeShader)
    {
        UINT x, y, z;
        reflection->GetThreadGroupSize(&x, &y, &z);
        dispatchGroupSize_ = { x, y, z };
    }

	D3D11_SHADER_DESC desc;
	reflection->GetDesc(&desc);

    // If we have input parameters than calculate our hash.
    // This is needed to minimize how fat the cache of Input-layouts is,
    // not because of concerns over the space usage of tons of input-layouts
    // but the possibility of hash-table collisions, which would be *very* bad.
    sigHash_ = 0;
    for (unsigned i = 0; i < desc.InputParameters; ++i)
    {
        D3D11_SIGNATURE_PARAMETER_DESC pDesc;
        reflection->GetInputParameterDesc(i, &pDesc);
        if (sigHash_ == 0)
            sigHash_ = CRC32::Calculate(&pDesc, sizeof pDesc);
        else
            CRC32::Combine(sigHash_, CRC32::Calculate(&pDesc, sizeof pDesc));
    }

	std::map<std::string, int> indexTable;
	for (unsigned i = 0; i < desc.BoundResources; ++i)
	{
		D3D11_SHADER_INPUT_BIND_DESC resourceDesc;
		reflection->GetResourceBindingDesc(i, &resourceDesc);

		if (resourceDesc.Type == D3D_SIT_CBUFFER)
		{
			indexTable[resourceDesc.Name] = resourceDesc.BindPoint;
		}
		else if (resourceDesc.Type == D3D_SIT_SAMPLER)
		{
			indexTable[resourceDesc.Name] = resourceDesc.BindPoint;
			TexInfo texInfo;
			texInfo.blockIndex_ = resourceDesc.BindPoint;
			strcpy(texInfo.name_, resourceDesc.Name);

			texData_.push_back(texInfo);
		}
	}

	for (unsigned i = 0; i < desc.ConstantBuffers; ++i)
	{
		ID3D11ShaderReflectionConstantBuffer* cb = reflection->GetConstantBufferByIndex(i);
		D3D11_SHADER_BUFFER_DESC cbDesc;
		cb->GetDesc(&cbDesc);

		UBOInfo uboInfo;
		uboInfo.bindingIndex_ = indexTable[cbDesc.Name];
		uboInfo.totalSize_ = cbDesc.Size;

		for (unsigned v = 0; v < cbDesc.Variables; ++v)
		{
			ID3D11ShaderReflectionVariable* var = cb->GetVariableByIndex(v);
			D3D11_SHADER_VARIABLE_DESC varDesc;
			var->GetDesc(&varDesc);

			UBORecord rec;
			rec.blockIndex_ = uboInfo.bindingIndex_;
			strcpy(rec.name_, varDesc.Name);
			rec.offset_ = varDesc.StartOffset;
			
			uboInfo.records_.push_back(rec);
		}

		cbufferData_.push_back(uboInfo);
	}
	
	reflection->Release();

    return true;
}

//****************************************************************************
//
//  Function:   ShaderPass::IsValid
//
//  Purpose:    Utility
//
//  Return:     True if the shader-program for the pass is probably healthy.
//
//****************************************************************************
bool ShaderPass::IsValid() const
{
	if (cs_)
		return cs_->IsValid();

	bool valid = ps_ && ps_->IsValid();
	valid &= vs_ && vs_->IsValid();
	valid &= hs_ ? gs_->IsValid() : true;
	valid &= ds_ ? gs_->IsValid() : true;
	valid &= gs_ ? gs_->IsValid() : true;
	return valid;
}

//****************************************************************************
//
//  Function:   ShaderPass::Release
//
//  Purpose:    Delete the program and zero-reinit
//
//****************************************************************************
void ShaderPass::Release()
{
	vs_.reset();
	ps_.reset();

	cs_.reset();
	hs_.reset();
	ds_.reset();
	gs_.reset();
}

//****************************************************************************
//
//  Function:   ShaderPass::Link
//
//  Purpose:    Overload for compute shader ... it's really a stupid GL
//              fossil thing. Errors/warnings are logged.
//
//  Return:     True if successfully linked.
//
//****************************************************************************
bool ShaderPass::Link(shared_ptr<Shader> computeShader)
{
    //??
    
    cs_ = computeShader;
    BuildReflection();

    return true;
}

//****************************************************************************
//
//  Function:   ShaderPass::Link
//
//  Purpose:    Links together the given shader-programs into a complete program,
//              logging any warnings/errors.
//
//  Return:     True if link was a success.
//
//****************************************************************************
bool ShaderPass::Link(shared_ptr<Shader> vs, shared_ptr<Shader> ps, shared_ptr<Shader> gs, shared_ptr<Shader> hs, shared_ptr<Shader> ds)
{
    // for the moment it's always required that there be a VS and PS (stream-out support will change this)
    // but will likely use a different path
    assert(vs && ps && vs->IsValid() && ps->IsValid());
    if (!vs || !ps || !vs->IsValid() || !ps->IsValid())
    {
        device_->LogMessage("Provided invalid shaders to ShaderPass::Link", GLVU_ERROR);
        return false;
    }

	vs_ = vs;
	ps_ = ps;
	gs_ = gs;
	hs_ = hs;
	ds_ = ds;

    BuildReflection();

    return true;
}

//****************************************************************************
//
//  Function:   ShaderPass::BuildReflection
//
//  Purpose:    Populates meta-data using GL's reflection API.
//				GLVU requires that constant buffers for a register be
//				the same between all stages, it's this function's responsibility
//				to scan through the reflection stored in each shader and
//				verify that the CBs are *probably* compatible.
//
//				This function does not attempt to TRULY verify compatibility
//				via a member-wise comparison as that gets hairy (member reinterpretation).
//
//				Basically, this function accumulates the first information it finds 
//				then raises a warning if you've *probably* messed up.				
//
//****************************************************************************
void ShaderPass::BuildReflection()
{
    uniformBuffers_.clear();

	vector< shared_ptr<Shader> > shaders = { vs_, hs_, ds_, gs_, ps_, cs_ };

	std::array<unsigned, 16> cbufferSizes = { 
		0, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0 
	};
	std::array<shared_ptr<Shader>, 16> writtenBy;

    std::array<uint32_t, 16> texTouchedBy = { 
        0, 0, 0, 0, 
        0, 0, 0, 0,
        0, 0, 0, 0,
        0, 0, 0, 0
    };
    std::array<std::string, 16> texNames;

	for (const auto& shader : shaders)
	{
		if (shader && shader->IsValid())
		{
			for (auto& buff : shader->cbufferData_)
			{
				if (cbufferSizes[buff.bindingIndex_] != 0 && cbufferSizes[buff.bindingIndex_] != buff.totalSize_)
				{
					// "CBuffer size mismatch between VertexShader and PixelShader @ 0, first seen with 128 bytes -> 64 bytes"
					GetDevice()->LogFormat(GLVU_WARNING, "CBuffer size mismatch between %s and %s @ %u, first seen %u bytes -> %u bytes",
						ShaderTypeToString(writtenBy[buff.bindingIndex_]->GetStage()),
						ShaderTypeToString(shader->GetStage()),
						buff.bindingIndex_,
						cbufferSizes[buff.bindingIndex_],
						buff.totalSize_);
				}
				else
				{
					cbufferSizes[buff.bindingIndex_] = buff.totalSize_;
					writtenBy[buff.bindingIndex_] = shader;

					uniformBuffers_.push_back(buff);
				}
			}

			for (auto& tex : shader->texData_)
			{
				if (texNames[tex.blockIndex_].empty())
				{
					texNames[tex.blockIndex_] = tex.name_;
                    texTouchedBy[tex.blockIndex_] |= GLVU_BITFIELD(shader->GetStage());
					textures_.push_back(tex);
				}
				else if (texNames[tex.blockIndex_] != std::string(tex.name_))
				{
					// "Texture mismatch between VertexShader and PixelShader @ 12, first saw myTextureName -> aDifferentTextureName"
					GetDevice()->LogFormat(GLVU_WARNING, "Texture mismatch between %s and %s @ %u, first saw %s -> %s",
						ShaderTypeToString(writtenBy[tex.blockIndex_]->GetStage()),
						ShaderTypeToString(shader->GetStage()),
						tex.blockIndex_,
						tex.name_,
						texNames[tex.blockIndex_].c_str());
				}
			}
		}
	}

    stageTextureAccesses_ = texTouchedBy;
}

//****************************************************************************
//
//  Function:   Effect::BindTexture
//
//  Purpose:    Hides the boiler plate of binding a texture and setting up the
//              sampler-object - including the validity checks.
//              If a pass is provided then the texture will be bound to the relevant
//              shader stages, otherwise just to the pixel-shader stage.
//
//****************************************************************************
void Effect::BindTexture(shared_ptr<Texture> tex, uint32_t slot, ShaderPass* pass)
{
    assert(tex && tex->IsValid());
    if (tex && tex->IsValid())
    {
		auto ctx = GetDevice()->GetD3DContext();
        
		for (auto& s : samplers_)
		{
			if (s.first == slot)
			{
				auto state = device_->GetSampler(s.second.filter_, s.second.wrap_);

                if (pass)
                {
                    auto mask = pass->GetTextureAccesses()[slot];

#define DO_STAGE(MASK, PREFIX) if (mask & GLVU_BITFIELD(MASK)) { ctx-> PREFIX ## SetSamplers(slot, 1, &state); ctx->PREFIX ## SetShaderResources(slot, 1, &tex->srv_); }

                    DO_STAGE(VertexShader, VS);
                    DO_STAGE(HullShader, HS);
                    DO_STAGE(DomainShader, DS);
                    DO_STAGE(GeometryShader, GS);
                    DO_STAGE(PixelShader, PS);
                    DO_STAGE(ComputeShader, CS);
                }
                else
                {
                    ctx->PSSetSamplers(slot, 1, &state);
                    ctx->PSSetShaderResources(slot, 1, &tex->srv_);
                }
				return;
			}
		}

		ID3D11SamplerState* state = nullptr;
		if (IsShadow(tex->GetFormat()))
			state = device_->GetSampler(FILTER_SHADOW, false);
		else
			state = device_->GetSampler(FILTER_POINT, true);

        if (pass)
        {
            auto mask = pass->GetTextureAccesses()[slot];
            DO_STAGE(VertexShader, VS);
            DO_STAGE(HullShader, HS);
            DO_STAGE(DomainShader, DS);
            DO_STAGE(GeometryShader, GS);
            DO_STAGE(ComputeShader, CS);
            DO_STAGE(PixelShader, PS);
        }
        else
        {
            ctx->PSSetSamplers(slot, 1, &state);
            ctx->PSSetShaderResources(slot, 1, &tex->srv_);
        }
#undef DO_STAGE
    }
}

}