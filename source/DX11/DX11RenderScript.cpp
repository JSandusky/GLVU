//****************************************************************************
//
//  File:       DX11RenderScript.cpp
//  License:    MIT
//  Project:    GLVU
//  Contents:   Implements actual rendering via DirectX 11
//
//****************************************************************************

#include "RenderScript.h"

#include "Batching.h"
#include "Buffer.h"
#include "LightShadow.h"
#include "Material.h"
#include "GraphicsDevice.h"
#include "Effect.h"
#include "Packing.h"
#include "Renderables.h"
#include "Renderer.h"

#include "glvu.h"
#include <d3d11_1.h>
#include <d3d11_2.h>
#include "DX11StateCache.h"

#ifdef GLVU_NVAPI
    #include <nvapi/nvapi.h>
#endif

#include <algorithm>

#pragma optimize("", off)

using namespace math;
using namespace std;

namespace GLVU
{

typedef D3D11_DEPTH_STENCIL_DESC DepthStencilState;
typedef D3D11_RASTERIZER_DESC RasterState;

#define BINDING_QUAD_CUSTOM 0
#define BINDING_QUAD_VIEW_MATRIX 3
#define BINDING_QUAD_SIZE_DATA 4

template<typename T>
T CreateDXType()
{
	T ret;
	ZeroMemory(&ret, sizeof(ret));
	return ret;
}

void MultidrawIndirectIndexed(GraphicsDevice* device, uint32_t drawCt, ID3D11Buffer* argsBuff)
{
    auto ctx = device->GetD3DContext();
#ifdef GLVU_NVAPI
    if (device->GetGPUFeatures().nvapi_)
    {
        NvAPI_D3D11_MultiDrawIndexedInstancedIndirect(ctx, drawCt, argsBuff, 0, sizeof(IndirectArgs));
        return;
    }
#endif
    for (uint32_t d = 0; d < drawCt; ++d)
        ctx->DrawIndexedInstancedIndirect(argsBuff, d * sizeof(IndirectArgs));
}

void dx_UseProgram(DX11StateCache* ctx, shared_ptr<ShaderPass> shader)
{
	ctx->SetShaders(shader->GetVS(), shader->GetPS(), shader->GetHS(), shader->GetDS(), shader->GetGS());
}

void dx_BindTexture(GraphicsDevice* device, ID3D11DeviceContext* ctx, Texture* texture, int binding, TextureFilter filter, bool wrap, bool fullPipe)
{
    auto tex = texture->GetReadable();
	ID3D11SamplerState* state = device->GetSampler(filter, wrap);
	if (fullPipe)
	{
		ctx->VSSetSamplers(binding, 1, &state);
		ctx->HSSetSamplers(binding, 1, &state);
		ctx->DSSetSamplers(binding, 1, &state);
		ctx->GSSetSamplers(binding, 1, &state);

		ctx->VSSetShaderResources(binding, 1, &tex->srv_);
		ctx->HSSetShaderResources(binding, 1, &tex->srv_);
		ctx->DSSetShaderResources(binding, 1, &tex->srv_);
		ctx->GSSetShaderResources(binding, 1, &tex->srv_);
	}

	ctx->PSSetSamplers(binding, 1, &state);
	ctx->PSSetShaderResources(binding, 1, &tex->srv_);
}

void dx_BindCBuffer(ID3D11DeviceContext* ctx, shared_ptr<Buffer> buffer, int binding, bool fullPipe)
{
	ctx->VSSetConstantBuffers(binding, 1, &buffer->buffer_);
	if (fullPipe)
	{
		ctx->HSSetConstantBuffers(binding, 1, &buffer->buffer_);
		ctx->DSSetConstantBuffers(binding, 1, &buffer->buffer_);
		ctx->GSSetConstantBuffers(binding, 1, &buffer->buffer_);
	}
	ctx->PSSetConstantBuffers(binding, 1, &buffer->buffer_);
}

void dx_BindCBufferRange(ID3D11DeviceContext1* ctx, shared_ptr<Buffer> buffer, int binding, size_t start, size_t len, bool fullPipe)
{
	// ID3D11DeviceContext1::VSSetConstantBuffers1 is weird, it's measured in 16-byte units for 1 param and bytes for the other ... it's weird.
	const unsigned startOffset = start / 16;
	const unsigned length = len;
	ctx->VSSetConstantBuffers1(binding, 1, &buffer->buffer_, &startOffset, &length);
	if (fullPipe)
	{
		ctx->HSSetConstantBuffers1(binding, 1, &buffer->buffer_, &startOffset, &length);
		ctx->DSSetConstantBuffers1(binding, 1, &buffer->buffer_, &startOffset, &length);
		ctx->GSSetConstantBuffers1(binding, 1, &buffer->buffer_, &startOffset, &length);
	}
	ctx->PSSetConstantBuffers1(binding, 1, &buffer->buffer_, &startOffset, &length);
}

bool dx_IsFullPipe(shared_ptr<ShaderPass> pass)
{
	return pass->GetHS() || pass->GetGS() || pass->GetDS();
}

//****************************************************************************
//
//  Function:   RenderScript::BeginStage
//
//  Purpose:    Binds the relevant FBO and binds (as 2D) any render-target inputs that need to be bound.
//
//****************************************************************************
void RenderScript::BeginStage(GraphicsDevice* device, View view, RenderScriptStage* stage)
{
    if (stage && stage->targetConfig_.fbo_)
        stage->targetConfig_.fbo_->Bind();
    else
        view.renderTarget_->Bind();

	D3D11_VIEWPORT vpt;
	vpt.TopLeftX = view.viewport_.x;
	vpt.TopLeftY = view.viewport_.y;
	vpt.Width = view.viewport_.Width();
	vpt.Height = view.viewport_.Height();
	vpt.MinDepth = 0.0f;
	vpt.MaxDepth = 1.0f;
	device->GetD3DContext()->RSSetViewports(1, &vpt);

    if (stage)
    {
		const auto& deviceDefaults = device_->GetDefaults();
        for (auto& targetInputBinding : stage->targetBindings_)
        {
            for (auto rec : targetTextures_)
            {
                if (rec->name_.nameHash_ == targetInputBinding.second.nameHash_)
                {
					// should this be using the device defaults?
					//TODO
                    dx_BindTexture(device_, device_->GetD3DContext(), rec->texture_.get(), targetInputBinding.first, FILTER_POINT, true, false);
                    break;
                }
            }
        }
    }
}

//****************************************************************************
//
//  Function:   RenderScript::EndStage
//
//  Purpose:    Binds the backbuffer, effectively that's a reset.
//
//****************************************************************************
void RenderScript::EndStage(GraphicsDevice* device, RenderScriptStage* stage)
{
    // this is necessary for swaps to happen correctly if the last pass is an off-screen pass.
    // TODO: reset target
}

//****************************************************************************
//
//  Function:   RenderScript::Execute
//
//  Purpose:    Runs this script for the given view. An extensive function
//              responsible for executing the stages and their commands.
//
//****************************************************************************
void RenderScript::Execute(Renderer* renderer, View view, const vector<Batch>& batches, const vector< shared_ptr<Light> >& lights)
{
	auto ctx = device_->GetD3DContext();
	auto ctx1 = device_->GetD3DContext1();
	auto state = device_->GetDX11State();

    if (view.renderTarget_ == nullptr)
        view.renderTarget_ = device_->GetBackbuffer();

    view.cameras_[0]->SetViewport(view.viewport_);

    if (width_ != view.renderTarget_->GetWidth() || height_ != view.renderTarget_->GetHeight())
        OnBackbufferResize(device_, view.renderTarget_->GetWidth(), view.renderTarget_->GetHeight());

    BatchQueue queue;
    for (unsigned i = 0; i < batches.size(); ++i)
        queue.Add(batches[i]);

    queue.SortFrontToBack();
    PrepareQueue(queue);

    if (!viewUniformBuffer_)
        viewUniformBuffer_ = device_->CreateUniformBuffer();

    SetupViewbufferData(view, viewBufferData_);
    viewUniformBuffer_->SetData(&viewBufferData_, sizeof(viewBufferData_));

    // Verify that all of our shadow data is up to date
    for (auto& batch : batches)
    {
        if (batch.material_)
            batch.material_->CommitUniforms();
    }

	auto d3dContext = device_->GetD3DContext();

    for (uint32_t stageIdx = 0; stageIdx < stages_.size(); ++stageIdx)
    {
        const auto& stage = stages_[stageIdx];
        if (stage->active_)
        {
            if (!ShouldStageExecute(stage))
                continue;

            BeginStage(device_, view, stage);

            // make sure our viewport is set
			D3D11_VIEWPORT vpt;
			vpt.TopLeftX = view.viewport_[0];
			vpt.TopLeftY = view.viewport_[1];
			vpt.Width = view.viewport_.Width();
			vpt.Height = view.viewport_.Height();
			vpt.MinDepth = 0.0f;
			vpt.MaxDepth = 1.0f;
			d3dContext->RSSetViewports(1, &vpt);

            for (const auto& cmd : stage->commands_)
            {
                if (cmd.commandType_ == ClearTargets)
                {
					if (stage->targetConfig_.fbo_)
						stage->targetConfig_.fbo_->Clear(cmd.cmdData_.clearData_.discardColor_ ? cmd.cmdData_.clearData_.color_ : nullptr,
							cmd.cmdData_.clearData_.discardDepth_,
							cmd.cmdData_.clearData_.discardStencil_);
					else
						view.renderTarget_->Clear(cmd.cmdData_.clearData_.discardColor_ ? cmd.cmdData_.clearData_.color_ : nullptr, 
							cmd.cmdData_.clearData_.discardDepth_, 
							cmd.cmdData_.clearData_.discardStencil_);
                }
            }

            for (const auto& cmd : stage->commands_)
            {
                if (!cmd.enabled_)
                    continue;

                if (cmd.commandType_ == GeometryPass)
                {
                    DrawBatchesTaskData task;
                    task.uboBindings_.push_back({ SHADER_BUFFER_VIEW_DATA, 0, UINT_MAX, viewUniformBuffer_ });
                    task.contextNameHash_ = cmd.context_.nameHash_;
                    task.skinnedNameHash_ = Hash(string(cmd.context_.name_) + SHADER_CONTEXT_SUFFIX_SKINNED);
                    task.instancedNameHash_ = Hash(string(cmd.context_.name_) + SHADER_CONTEXT_SUFFIX_INST);
                    DrawBatches(renderer, view, stage, queue, task);
                }
                else if (cmd.commandType_ == ForwardLights)
                {
					RenderForwardLights(renderer, view, stage, cmd, lights);
                }
                else if (cmd.commandType_ == ForwardTiledLights)
                {
                    LightTiler tiler = LightTiler(device_, { cmd.cmdData_.tiledLightData_.tilesX_, cmd.cmdData_.tiledLightData_.tilesY_, cmd.cmdData_.tiledLightData_.tilesZ_ }, 4);
                    tiler.BuildLightTables(view.cameras_[0], lights);
                    
                    DrawBatchesTaskData task;
                    task.uboBindings_.push_back({ SHADER_BUFFER_VIEW_DATA, 0, UINT_MAX, viewUniformBuffer_ });
                    task.uboBindings_.push_back({ SHADER_BUFFER_CLUSTER_COUNTS, 0, UINT_MAX, nullptr });
                    task.uboBindings_.push_back({ SHADER_BUFFER_CLUSTER_LIGHT_INDEXES, 0, UINT_MAX, nullptr });
                    task.contextNameHash_ = cmd.context_.nameHash_;
                    task.skinnedNameHash_ = Hash(string(cmd.context_.name_) + SHADER_CONTEXT_SUFFIX_SKINNED);
                    task.instancedNameHash_ = Hash(string(cmd.context_.name_) + SHADER_CONTEXT_SUFFIX_INST);
                    DrawBatches(renderer, view, stage, queue, task);
                }
                else if (cmd.commandType_ == DeferredTiledLights)
                {
                    LightTiler tiler = LightTiler(device_, { cmd.cmdData_.tiledLightData_.tilesX_, cmd.cmdData_.tiledLightData_.tilesY_, cmd.cmdData_.tiledLightData_.tilesZ_ }, 4);
                    tiler.BuildLightTables(view.cameras_[0], lights);

                    // do a fullscreen pass with the added buffers
                    if (cmd.effect_ == nullptr)
                    {
                        device_->LogFormat(GLVU_WARNING, "No effect specified for DeferredTiledLights pass: %s", cmd.passIdentifier_.name_);
                        continue;
                    }

                    RenderFullscreen(cmd.effect_, view, {}, {});
                }
                else if (cmd.commandType_ == LightVolumes)
                {
                    auto lightFX = device_->GetDeferredLightEffect();

                    BatchQueue lightQueue;
                    vector<float4x4> transforms;
                    transforms.resize(lights.size());

                    shared_ptr<Geometry> geometry, lastGeometry;

                    auto viewDataBuffer = device_->GetScratchUniformBuffer(sizeof(ViewBufferData));

                    LightData lightData;

                    shared_ptr<ShaderPass> pass, lastPass;

                    BeginStage(device_, view, stage);

					RollingBufferAllocator rollLightData(device_);

                    struct LightBufferSet {
                        shared_ptr<Buffer> viewBuffer;
						unsigned dataIndex;
						unsigned transIndex;
                    };
                    std::vector<LightBufferSet> lightBuffers;

                    for (uint32_t i = 0; i < lights.size(); ++i)
                    {
                        auto light = lights[i];
                    
                        lightData.lightMat = light->GetShadowMatrix(0);
                        lightData.lightPos = float4(lights[i]->GetPosition(), (int)light->GetKind());
                        lightData.lightDir = float4(lights[i]->GetDirection(), light->GetRadius());
                        lightData.color = light->GetColor();
                        lightData.extraParams.x = light->GetFOV();
                        lightData.extraParams.y = light->IsShadowCasting() ? 1.0f : 0.0f;
                        lightData.shadowMapCoords[0] = light->IsShadowCasting() ? light->GetShadowDomain(0) : float4::zero;
                        lightData.shadowMapCoords[1] = light->IsShadowCasting() ? light->GetShadowDomain(1) : float4::zero;

						const unsigned dataIdx = rollLightData.GetAllocCount();
						auto alloc = rollLightData.Allocate(sizeof(lightData));
						memcpy(alloc.first, &lightData, sizeof(lightData));

                        if (light->GetKind() == Light::POINT)
                        {
                            auto transform = float4x4::FromTRS(light->GetPosition(), Quat::identity, float3(light->GetRadius()));

							const unsigned transIdx = rollLightData.GetAllocCount();
							auto alloc = rollLightData.Allocate(sizeof(transform));
							memcpy(alloc.first, &transform, sizeof(transform));

							lightBuffers.push_back({ viewUniformBuffer_, dataIdx, transIdx });
                        }
                        else if (light->GetKind() == Light::SPOT)
                        {
                            float yScale = tanf(math::DegToRad(light->GetFOV()) * 0.5f) * light->GetRadius();
                            float xScale = yScale;
                            auto transform = float4x4::FromTRS(light->GetPosition(), light->GetRotation(), float3(xScale, yScale, light->GetRadius()));

							const unsigned transIdx = rollLightData.GetAllocCount();
							auto alloc = rollLightData.Allocate(sizeof(transform));
							memcpy(alloc.first, &transform, sizeof(transform));

                            lightBuffers.push_back({ viewUniformBuffer_, dataIdx, transIdx});
                        }
                        else if (light->GetKind() == Light::DIRECTIONAL)
                        {
                            auto viewMatrix = float4x4::OpenGLOrthoProjLH(0, 1, 2, 2);

                            ViewBufferData dirLightOverride = viewBufferData_;
                            dirLightOverride.invViewProj[0] = viewMatrix;
                            dirLightOverride.viewProj[0] = viewMatrix;

                            auto viewDataBuffer = device_->GetScratchUniformBuffer(sizeof(ViewBufferData));
                            viewDataBuffer->SetData(&dirLightOverride, sizeof(ViewBufferData));

                            auto transform = float4x4::identity;
                            auto transformBuffer = device_->GetScratchUniformBuffer(sizeof(float4x4));
							const unsigned transIdx = rollLightData.GetAllocCount();
							auto alloc = rollLightData.Allocate(sizeof(transform));
							memcpy(alloc.first, &transform, sizeof(transform));

                            lightBuffers.push_back({ viewDataBuffer, dataIdx, transIdx});
                        }
                    }

					rollLightData.Finish();

					shared_ptr<Buffer> lastViewBuffer;
					D3D11_RASTERIZER_DESC rasterState = state->Default<D3D11_RASTERIZER_DESC>();
					D3D11_DEPTH_STENCIL_DESC depthState = state->Default<D3D11_DEPTH_STENCIL_DESC>();
					depthState.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;

                    for (uint32_t i = 0; i < lights.size(); ++i)
                    {
                        auto light = lights[i];

                        if (light->GetKind() == Light::POINT || light->GetKind() == Light::SPOT)
                        {
							if (light->GetKind() == Light::POINT)
							{
								pass = light->IsShadowCasting() ? lightFX->GetPass("POINT_LIGHT_SHDW", PRIM_UNKNOWN) : lightFX->GetPass("POINT_LIGHT", PRIM_UNKNOWN);
								geometry = device_->GetPointLightGeometry();
							}
							else
							{
								pass = light->IsShadowCasting() ? lightFX->GetPass("SPOT_LIGHT_SHDW", PRIM_UNKNOWN) : lightFX->GetPass("SPOT_LIGHT", PRIM_UNKNOWN);
								geometry = device_->GetSpotLightGeometry();
							}

                            if (pass != lastPass)
                            {
                                ApplyPass(pass, view);
                                lastPass = pass;
                            }
                            if (lastGeometry != geometry)
                            {
                                ApplyGeometry(pass, geometry.get(), false);
                                lastGeometry = geometry;
                            }

                            if (light->Contains(view.cameras_[0]))
                            {
								rasterState.CullMode = D3D11_CULL_BACK;
								depthState.DepthFunc = D3D11_COMPARISON_GREATER_EQUAL;
                            }
                            else
                            {
								rasterState.CullMode = D3D11_CULL_FRONT;
								depthState.DepthFunc = D3D11_COMPARISON_LESS_EQUAL;
                            }
                        }
                        else if (light->GetKind() == Light::DIRECTIONAL)
                        {
                            pass = light->IsShadowCasting() ? lightFX->GetPass("DIRECTIONAL_LIGHT_SHDW", PRIM_UNKNOWN) : lightFX->GetPass("DIRECTIONAL_LIGHT", PRIM_UNKNOWN);

                            if (pass != lastPass)
                            {
                                ApplyPass(pass, view);
                                lastPass = pass;
                            }

                            geometry = device_->GetFSTriGeometry();
                            if (geometry != lastGeometry)
                            {
                                ApplyGeometry(pass, geometry.get(), false);
                                lastGeometry = geometry;
                            }

							depthState.DepthEnable = false;
                        }

                        if (light->GetKind() != Light::DIRECTIONAL)
                            depthState.DepthEnable = true;

                        if (light->GetShadowMapTexture())
                            lightFX->BindTexture(light->GetShadowMapTexture(), SHADER_TEX_SHADOWMAP, nullptr);
                        else
                            lightFX->BindTexture(device_->GetSystemTexture(PIPELINE_RESOURCE_SHADOWMAP), SHADER_TEX_SHADOWMAP, nullptr);

                        if (light->GetColor().w < 0)
                            ApplyBlendMode(Blend_Subtract, view, stage);
                        else if (light->GetColor().w > 0)
                            ApplyBlendMode(Blend_Add, view, stage);
                        else
                            ApplyBlendMode(Blend_Alpha, view, stage);

                        auto buffers = lightBuffers[i];

						if (buffers.viewBuffer != lastViewBuffer)
						{
							dx_BindCBuffer(ctx, buffers.viewBuffer, SHADER_BUFFER_VIEW_DATA, false);
							lastViewBuffer = buffers.viewBuffer;
						}
						auto lightD = rollLightData.GetAllocation(buffers.dataIndex);
						auto lightT = rollLightData.GetAllocation(buffers.transIndex);
						dx_BindCBuffer(ctx, lightD.buffer_, 0, false);

						dx_BindCBufferRange(ctx1, lightD.buffer_, 0, lightD.allocationOffset_, lightD.allocationSize_, false);
						dx_BindCBufferRange(ctx1, lightT.buffer_, 2, lightT.allocationOffset_, lightT.allocationSize_, false);                            
                        if (geometry->indexBuffer_)
                        {
							state->SetRasterState(rasterState);
							state->SetDepthStencilState(depthState);
							state->SetPrimitive(TRIANGLE_LIST, false);
							ctx->DrawIndexed(geometry->indexCount_, geometry->indexStart_, 0);
                            device_->AddStat(STAT_PRIMITIVES, geometry->primCount_);
                        }
                        else
                        {
							state->SetRasterState(rasterState);
							state->SetDepthStencilState(depthState);
							state->SetPrimitive(TRIANGLE_LIST, false);
							ctx->Draw(geometry->vertexCount_, 0);
                            device_->AddStat(STAT_PRIMITIVES, geometry->primCount_);
                        }
                        device_->AddStat(STAT_BATCHES, 1);
                        device_->AddStat(STAT_INSTANCES, 1);
                    }

					depthState.DepthEnable = true;
					depthState.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;

                    EndStage(device_, stage);
                }
                else if (cmd.commandType_ == FullscreenQuad)
                {   
                    BufferPack pack(256);

                    struct ParamData {
                        float params[32];
                    };

                    // view matrix
                    pack.Pack<float4x4>();
                    pack.Pack<ViewData>();
                    pack.Pack<ParamData>();

                    auto quadCmdUBO = device_->GetScratchUniformBuffer(pack.allocSize_);
                    pack.AllocateUBO(quadCmdUBO);

                    pack.Get<float4x4>(0) = float4x4::OpenGLOrthoProjLH(0, 1, 2, 2);

                    // INPUT and OUTPUT size information sources
                    SetupViewData(pack.Get<ViewData>(1), view.viewport_, GetTargetTexture(cmd.cmdData_.quadData_.inputSize_.nameHash_), GetTargetTexture(cmd.cmdData_.quadData_.outputSize_.nameHash_));

                    // 128 bytes just isn't something to worry about, so copy the whole thing regardless how many are really used
                    memcpy(pack.Get<ParamData>(2).params, cmd.params_, sizeof(float) * RENDER_SCRIPT_MAX_PARAMS);

                    pack.Transfer(quadCmdUBO, false);

                    // beware: this can blow out texture bindings
                    BeginStage(device_, view, stage);

                    for (auto tex : cmd.textures_)
                        dx_BindTexture(device_, ctx, tex.texture_.get(), tex.slot_, tex.sampling_.filter_, tex.sampling_.wrap_, false);

                    // bind UBOs then draw
					dx_BindCBufferRange(ctx1, quadCmdUBO, BINDING_QUAD_VIEW_MATRIX, 0, pack.SizeOf(0), false);
					dx_BindCBufferRange(ctx1, quadCmdUBO, BINDING_QUAD_SIZE_DATA, pack.OffsetOf(1), pack.SizeOf(1), false);
					dx_BindCBufferRange(ctx1, quadCmdUBO, BINDING_QUAD_CUSTOM, pack.OffsetOf(2), pack.SizeOf(2), true);
                    

					dx_UseProgram(state, cmd.effect_->GetPasses()[0]);
                    ApplyBlendMode(BlendMode::Blend_None, view, stage);
					state->SetNoDepth();
					state->SetNoCull();

					// bind the geometry and textures
					auto geo = device_->GetFSTriGeometry();
					geo->layout_->Bind(geo.get(), { });

					ctx->Draw(3, 0);
                    device_->AddStat(STAT_BATCHES, 1);
                    device_->AddStat(STAT_PRIMITIVES, 1);
                    device_->AddStat(STAT_INSTANCES, 1);
                    EndStage(device_, stage);
                }
                else if (cmd.commandType_ == ComputePass)
                {
                    if (auto pass = cmd.effect_->GetPass(cmd.context_.nameHash_, PRIM_UNKNOWN))
                    {
						if (pass->IsCompute())
						{
							ApplyPass(pass, view);

							for (auto& uboRecord : cmd.buffers_)
							{
								if (auto found = GetDataBuffer(uboRecord.second.nameHash_))
								{
									if (found->GetBufferKind() == UniformBufferObject)
										dx_BindCBuffer(ctx, found, uboRecord.first, false);
								}
							}

							if (cmd.numParams_ > 0)
							{
								auto scratch = device_->GetScratchUniformBuffer(sizeof(float) * RENDER_SCRIPT_MAX_PARAMS);
								scratch->SetData((void*)cmd.params_, sizeof(float) * RENDER_SCRIPT_MAX_PARAMS);
								dx_BindCBuffer(ctx, scratch, 0, false);
							}

							for (auto& texRecord : cmd.textures_)
								cmd.effect_->BindTexture(texRecord.texture_, texRecord.slot_, pass.get());


							ctx->Dispatch(cmd.cmdData_.computeData_.groupsX_, cmd.cmdData_.computeData_.groupsY_, cmd.cmdData_.computeData_.groupsZ_);
						}
						else
						{
							device_->LogFormat(GLVU_ERROR, "ComputePass: attempted to use non-compute shader: %s", pass->GetName());
						}
                    }
                }
                else if (cmd.commandType_ == GenMips)
                {
                    if (auto tgt = GetTargetTexture(cmd.cmdData_.genMipsData_.texture_.nameHash_))
                        tgt->texture_->GenerateMipMaps();
                }
                else if (cmd.commandType_ == RenderCallback)
                {
                    device_->InvokeCallback(cmd.cmdData_.callData_.callID_.name_, this, &view, (float*)cmd.params_, cmd.numParams_);
                }
                else if (cmd.commandType_ == BufferCopy)
                {
                    auto srcBuffer = FindBuffer(cmd.cmdData_.buffCopyData_.source_);
                    auto destBuffer = FindBuffer(cmd.cmdData_.buffCopyData_.dest_);

					if (srcBuffer && destBuffer && srcBuffer->IsValid() && destBuffer->IsValid())
					{
						if (destBuffer->GetSize() < srcBuffer->GetSize())
							destBuffer->SetSize(srcBuffer->GetSize());

						ctx->CopyResource(destBuffer->buffer_, srcBuffer->buffer_);
					}
                }
				else if (cmd.commandType_ == Blit)
				{
					shared_ptr<Texture> srcTex;
					if (auto srcFbo = GetTargetTexture(cmd.cmdData_.blitData_.source_.nameHash_))
						srcTex = srcFbo->texture_;
					else
						srcTex = device_->GetSystemTexture(cmd.cmdData_.blitData_.source_.name_);

					shared_ptr<Texture> destTex;
					if (auto destFBO = GetTargetTexture(cmd.cmdData_.blitData_.dest_.nameHash_))
						destTex = destFBO->texture_;
					else
						srcTex = device_->GetSystemTexture(cmd.cmdData_.blitData_.dest_.name_);

					if (srcTex && destTex)
					{
						auto fbo = device_->CreateFrameBuffer({ destTex });
						auto passThru = device_->GetPassThruEffect();

                        fbo->Bind();
                        
                        D3D11_VIEWPORT vpt = CreateDXType<D3D11_VIEWPORT>();
                        vpt.Width = destTex->GetWidth();
                        vpt.Height = destTex->GetHeight();
                        vpt.MinDepth = 0.0f; vpt.MaxDepth = 1.0f;

                        ctx->RSSetViewports(1, &vpt);
                        RenderFullscreen(passThru, view, { }, { { 0, SamplerTraits { FILTER_LINEAR, false }, srcTex } });
                        
                        // begin stage again
                        BeginStage(device_, view, stage);                        
					}
				}
            }

            EndStage(device_, stage);
        }
    }
}

//****************************************************************************
//
//  Function:   RenderScript::ApplyPass
//
//  Purpose:    Handles common shader-pass state binding. Presently this
//              is highly redundant frequently enabling/disabling states - 
//              most of which is meaningless though.
//
//****************************************************************************
void RenderScript::ApplyPass(const shared_ptr<ShaderPass> pass, View view)
{
	ID3D11DeviceContext* ctx = device_->GetD3DContext();
	DX11StateCache* state = device_->GetDX11State();

	dx_UseProgram(state, pass);
//#define SET_SHADER(STAGE, NAME) ctx-> STAGE ## SetShader(pass->Get ## STAGE () ? pass->Get ## STAGE ()->GetShader<ID3D11 ## NAME>() : nullptr, nullptr, 0)
//	SET_SHADER(VS, VertexShader);
//	SET_SHADER(HS, HullShader);
//	SET_SHADER(DS, DomainShader);
//	SET_SHADER(GS, GeometryShader);
//	SET_SHADER(PS, PixelShader);
//#undef SET_SHADER

	D3D11_DEPTH_STENCIL_DESC depthState = state->Default<D3D11_DEPTH_STENCIL_DESC>();
	D3D11_RASTERIZER_DESC rasterState = state->Default< D3D11_RASTERIZER_DESC>();

	const auto& drawState = pass->GetDrawState();

    static D3D11_CULL_MODE cullModes[] = { D3D11_CULL_NONE, D3D11_CULL_FRONT, D3D11_CULL_BACK };
	rasterState.CullMode = cullModes[drawState.culling_];
	rasterState.DepthBias = drawState.depthBias_;
	rasterState.SlopeScaledDepthBias = drawState.slopeBias_;

	depthState.DepthEnable = drawState.depthTest_ ? TRUE : FALSE;
	depthState.DepthWriteMask = drawState.depthWrite_ ? D3D11_DEPTH_WRITE_MASK_ALL : D3D11_DEPTH_WRITE_MASK_ZERO;
	depthState.StencilEnable = drawState.stencilTest_ ? TRUE : FALSE;

	static const D3D11_COMPARISON_FUNC compareFuncs[] = {
		D3D11_COMPARISON_EQUAL,
		D3D11_COMPARISON_LESS_EQUAL,
		D3D11_COMPARISON_GREATER_EQUAL,
		D3D11_COMPARISON_LESS,
		D3D11_COMPARISON_GREATER,
		D3D11_COMPARISON_NOT_EQUAL,
		D3D11_COMPARISON_ALWAYS,
		D3D11_COMPARISON_NEVER,
	};
	depthState.DepthFunc = compareFuncs[drawState.depthCompare_];
    if (drawState.stencilTest_)
    {
        depthState.StencilReadMask = 0xFF;
        depthState.StencilWriteMask = 0xFF;
        depthState.StencilEnable = TRUE;
    }
    else
        depthState.StencilEnable = FALSE;

	state->SetRasterState(rasterState);
	state->SetDepthStencilState(depthState);
	ApplyBlendMode(pass->GetDrawState().blendMode_, view, nullptr);
}

//****************************************************************************
//
//  Function:   RenderScript::ApplyGeometry
//
//  Purpose:    Does the repeated task of setting of the VAO and bindings buffers
//
//****************************************************************************
void RenderScript::ApplyGeometry(shared_ptr<ShaderPass> pass, Geometry* geometry, bool isInstanced, const vector<shared_ptr<Buffer> >& extraBuffers, bool laterInstanced, bool isVR)
{
    auto  layout = isInstanced ? geometry->layout_->GetInstancedVariant(isVR) : geometry->layout_;
    layout->Bind(geometry, extraBuffers, laterInstanced);

    // D3D11 has less work to do here
}

//****************************************************************************
//
//  Function:   RenderScript::ApplyBlendMode
//
//  Purpose:    Utility for reliably setting blend-modes, previously this was done poorly.
//              Unused parameters are Vulkan specific.
//
//****************************************************************************
void RenderScript::ApplyBlendMode(BlendMode blendMode, View view, RenderScriptStage* stage)
{
    typedef std::tuple<D3D11_BLEND, D3D11_BLEND, D3D11_BLEND_OP> BlendSetup;
    static const BlendSetup BlendTable[] = {
        { D3D11_BLEND_ONE, D3D11_BLEND_ONE, D3D11_BLEND_OP_ADD }, // none
        { D3D11_BLEND_SRC_ALPHA, D3D11_BLEND_INV_SRC_ALPHA, D3D11_BLEND_OP_ADD }, // alpha
        { D3D11_BLEND_ONE, D3D11_BLEND_ONE, D3D11_BLEND_OP_ADD }, // add
        { D3D11_BLEND_ONE, D3D11_BLEND_ONE, D3D11_BLEND_OP_REV_SUBTRACT }, // subtract
        { D3D11_BLEND_DEST_COLOR, D3D11_BLEND_ZERO, D3D11_BLEND_OP_ADD }, // multiply
        { D3D11_BLEND_ONE, D3D11_BLEND_INV_SRC_ALPHA, D3D11_BLEND_OP_ADD }, // premul
        { D3D11_BLEND_ONE, D3D11_BLEND_ONE, D3D11_BLEND_OP_ADD }, // OIT mixer
        { D3D11_BLEND_INV_SRC_ALPHA, D3D11_BLEND_SRC_ALPHA, D3D11_BLEND_OP_ADD } // OIT combine
    };

    if (blendMode == Blend_OITMixer)
    {
        D3D11_BLEND_DESC desc = CreateDXType<D3D11_BLEND_DESC>();
        ZeroMemory(&desc, sizeof(desc));
        desc.IndependentBlendEnable = TRUE;
        desc.AlphaToCoverageEnable = FALSE;

        desc.RenderTarget[0].BlendEnable = TRUE;
        desc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
        desc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
        desc.RenderTarget[0].SrcBlend = D3D11_BLEND_ONE;
        desc.RenderTarget[0].DestBlend = D3D11_BLEND_ONE;
        desc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
        desc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ONE;
        desc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;

        desc.RenderTarget[1].BlendEnable = TRUE;
        desc.RenderTarget[1].BlendOp = D3D11_BLEND_OP_ADD;
        desc.RenderTarget[1].BlendOpAlpha = D3D11_BLEND_OP_ADD;
        desc.RenderTarget[1].SrcBlend = D3D11_BLEND_ZERO;
        desc.RenderTarget[1].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
        desc.RenderTarget[1].SrcBlendAlpha = D3D11_BLEND_ZERO;
        desc.RenderTarget[1].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
        desc.RenderTarget[1].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;

        device_->GetDX11State()->SetBlendState(desc);
    }
    else if (blendMode == Blend_OITComposite)
    {
        D3D11_BLEND_DESC desc = CreateDXType<D3D11_BLEND_DESC>();
        ZeroMemory(&desc, sizeof(desc));
        desc.IndependentBlendEnable = FALSE;
        desc.AlphaToCoverageEnable = FALSE;
        desc.RenderTarget[0].BlendEnable = blendMode == Blend_None ? FALSE : TRUE;
        desc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
        desc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
        desc.RenderTarget[0].SrcBlend = D3D11_BLEND_INV_SRC_ALPHA;
        desc.RenderTarget[0].DestBlend = D3D11_BLEND_SRC_ALPHA;
        desc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
        desc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_SRC_ALPHA;
        desc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
        device_->GetDX11State()->SetBlendState(desc);
    }
    else
    {
        D3D11_BLEND_DESC desc = CreateDXType<D3D11_BLEND_DESC>();
        ZeroMemory(&desc, sizeof(desc));
        desc.IndependentBlendEnable = FALSE;
        desc.AlphaToCoverageEnable = FALSE;
        desc.RenderTarget[0].BlendEnable = blendMode == Blend_None ? FALSE : TRUE;
        desc.RenderTarget[0].BlendOp = std::get<2>(BlendTable[blendMode]);
        desc.RenderTarget[0].BlendOpAlpha = std::get<2>(BlendTable[blendMode]);
        desc.RenderTarget[0].SrcBlend =  std::get<0>(BlendTable[blendMode]);
        desc.RenderTarget[0].DestBlend = std::get<1>(BlendTable[blendMode]);
        desc.RenderTarget[0].SrcBlendAlpha = std::get<0>(BlendTable[blendMode]);
        desc.RenderTarget[0].DestBlendAlpha = std::get<1>(BlendTable[blendMode]);
        desc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
        device_->GetDX11State()->SetBlendState(desc);
    }
}

//****************************************************************************
//
//  Function:   RenderScript::DrawBatches
//
//  Purpose:    Executes the bind + draw loop for a queue of batches that are
//              to be drawn to a specific view. Task data is used to encapsulate
//              a lot of the detail configuration such as which passes to look
//              for and UBOs/textures that should be inherited.
//
//****************************************************************************
void RenderScript::DrawBatches(Renderer* renderer, View view, RenderScriptStage* stage, const BatchQueue& queue, DrawBatchesTaskData& taskData)
{
    shared_ptr<ShaderPass> activePass = nullptr;
    Geometry* activeGeometry = nullptr;
    Material* activeMaterial = nullptr;

    view.renderTarget_->MarkDrawn();

	auto ctx = taskData.deferredCtx_ ? taskData.deferredCtx_ : device_->GetD3DContext();
	auto state = device_->GetDX11State();

	D3D11_VIEWPORT viewport;
	viewport.MinDepth = 0.0f;
	viewport.MaxDepth = 1.0f;
	viewport.TopLeftX = view.viewport_[0];
	viewport.TopLeftY = view.viewport_[1];
	viewport.Width = view.viewport_.Width();
	viewport.Height = view.viewport_.Height();

	ctx->RSSetViewports(1, &viewport);

    auto transformUBO = device_->GetScratchUniformBuffer(64);

    static auto applyMaterial = [](GraphicsDevice* device, shared_ptr<ShaderPass> pass, Material* mat, DrawBatchesTaskData& taskData) {
        for (auto& uboRecord : mat->GetUBOs())
        {
			if (uboRecord.buffer_->GetBufferKind() == UniformBufferObject)
				dx_BindCBuffer(device->GetD3DContext(), uboRecord.buffer_, uboRecord.slot_, dx_IsFullPipe(pass));
        }

        // task specific buffers override material ones
        for (auto& uboRecord : taskData.uboBindings_)
        {
            if (uboRecord.buffer_->GetBufferKind() == UniformBufferObject)
				dx_BindCBuffer(device->GetD3DContext(), uboRecord.buffer_, uboRecord.slot_, dx_IsFullPipe(pass));
        }

        for (auto& texRecord : mat->GetTextures())
            mat->GetEffect()->BindTexture(texRecord.texture_, texRecord.slot_, pass.get());

        for (auto& texRecord : taskData.textureBindings_)
            mat->GetEffect()->BindTexture(texRecord.texture_, texRecord.slot_, pass.get());
    };
        
    BeginStage(device_, view, stage);

    const bool isVR = view.TestFlag(ViewFlag_VR);
    const int instanceMultiplier = isVR ? 2 : 1;

    // draw the instance batches
    for (const auto& instBatch : queue.groups_)
    {
        if (auto pass = instBatch.second.material_->GetEffect()->GetPass(taskData.instancedNameHash_, instBatch.second.geometry_->primType_))
        {
            auto& grp = instBatch.second;
            if (activePass != pass)
            {
                activePass = pass;
                ApplyPass(pass, view);
            }

            if (activeMaterial != grp.material_)
            {
                activeMaterial = grp.material_;
                applyMaterial(device_, pass, activeMaterial, taskData);
            }

            if (activeGeometry != grp.geometry_)
            {
                activeGeometry = grp.geometry_;
                ApplyGeometry(pass, activeGeometry, true, { instBatch.second.instanceTransformBuffers_[0].second }, false, view.TestFlag(ViewFlag_VR));
            }

            D3D11_PRIMITIVE_TOPOLOGY mode = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
            if (activeGeometry->primType_ == POINT_LIST)
                mode = D3D11_PRIMITIVE_TOPOLOGY_POINTLIST;
            else if (activeGeometry->primType_ == LINE_LIST)
                mode = D3D11_PRIMITIVE_TOPOLOGY_LINELIST;
            else if (activeGeometry->primType_ == TRIANGLE_ADJ)
                mode = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST_ADJ;
            else if (activeGeometry->primType_ == LINE_ADJ)
                mode = D3D11_PRIMITIVE_TOPOLOGY_LINELIST_ADJ;

            if (activePass->IsTessellating())
                mode = D3D11_PRIMITIVE_TOPOLOGY_3_CONTROL_POINT_PATCHLIST;

			ctx->IASetPrimitiveTopology(mode);

            for (size_t i = 0; i < grp.instanceTransformBuffers_.size(); ++i)
            {
                auto instBuff = grp.instanceTransformBuffers_[i].second;
                uint32_t drawCt = grp.instanceTransformBuffers_[i].first * instanceMultiplier;
                ApplyGeometry(pass, activeGeometry, true, { instBatch.second.instanceTransformBuffers_[i].second }, true, view.TestFlag(ViewFlag_VR));

                // ubos that contain extra instance data?
                if (!grp.materialTraitUBOs_.empty())
                    dx_BindCBuffer(ctx, grp.materialTraitUBOs_[i], SHADER_BUFFER_USER, true);

                if (activeGeometry->indexBuffer_)
                {
					ctx->DrawIndexedInstanced(activeGeometry->indexCount_, drawCt, activeGeometry->indexStart_, 0, 0);
                    device_->AddStat(STAT_PRIMITIVES, activeGeometry->primCount_ * drawCt);
                }
                else
                {
					ctx->DrawInstanced(activeGeometry->vertexCount_, drawCt, 0, 0);
                    device_->AddStat(STAT_PRIMITIVES, (activeGeometry->vertexCount_ / 3) * drawCt);
                }
                device_->AddStat(STAT_BATCHES, 1);
                device_->AddStat(STAT_INSTANCES, drawCt);
            }
        }
    }

    // Draw the sorted batches
    for (const auto& batch : queue.batches_)
    {
        shared_ptr<ShaderPass> pass = batch.isSkinned_ ?
            batch.material_->GetEffect()->GetPass(taskData.skinnedNameHash_, batch.geometry_->primType_) :
            batch.material_->GetEffect()->GetPass(taskData.contextNameHash_, batch.geometry_->primType_);

        if (pass)
        {
            const bool isFullPipe = dx_IsFullPipe(pass);
            if (activePass != pass)
            {
                activePass = pass;
                ApplyPass(pass, view);
            }

            if (activeMaterial != batch.material_)
            {
                activeMaterial = batch.material_;
                applyMaterial(device_, pass, activeMaterial, taskData);
            }

            if (activeGeometry != batch.geometry_)
            {
                activeGeometry = batch.geometry_;
                ApplyGeometry(pass, activeGeometry, false, { });
            }

            D3D11_PRIMITIVE_TOPOLOGY mode = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
            if (activeGeometry->primType_ == POINT_LIST)
                mode = D3D11_PRIMITIVE_TOPOLOGY_POINTLIST;
            else if (activeGeometry->primType_ == LINE_LIST)
                mode = D3D11_PRIMITIVE_TOPOLOGY_LINELIST;
            if (activePass->IsTessellating())
                mode = D3D11_PRIMITIVE_TOPOLOGY_3_CONTROL_POINT_PATCHLIST;

            ctx->IASetPrimitiveTopology(mode);

            if (batch.isSkinned_)
            {
                dx_BindCBuffer(ctx, batch.bonesBuffer_, SHADER_BUFFER_OBJECT_DATA, isFullPipe);
                if (activeGeometry->indexBuffer_)
                {
                    if (view.TestFlag(ViewFlag_VR))
                        ctx->DrawIndexedInstanced(activeGeometry->indexCount_, 2, activeGeometry->indexStart_, 0, 0);
                    else
                        ctx->DrawIndexed(activeGeometry->indexCount_, activeGeometry->indexStart_, 0);
                    device_->AddStat(STAT_PRIMITIVES, activeGeometry->indexCount_ / 3);
                }
                else
                {
                    if (view.TestFlag(ViewFlag_VR))
                        ctx->DrawInstanced(activeGeometry->vertexCount_, 2, 0, 0);
                    else
                        ctx->Draw(activeGeometry->vertexCount_, 0);
                    device_->AddStat(STAT_PRIMITIVES, activeGeometry->vertexCount_ / 3);
                }
                device_->AddStat(STAT_BATCHES, 1);
                device_->AddStat(STAT_INSTANCES, 1 * instanceMultiplier);
            }
            else
            {
                for (uint32_t i = 0; i < batch.numTransforms_; ++i)
                {
                    transformUBO->SetData(batch.transforms_ + sizeof(float4x4) * i, sizeof(float4x4));
                    dx_BindCBuffer(ctx, transformUBO, SHADER_BUFFER_OBJECT_DATA, isFullPipe);
                    if (activeGeometry->indexBuffer_)
                    {
                        if (isVR)
                            ctx->DrawIndexedInstanced(activeGeometry->indexCount_, 2, activeGeometry->indexStart_, 0, 0);
                        else
                            ctx->DrawIndexed(activeGeometry->indexCount_, activeGeometry->indexStart_, 0);
                        device_->AddStat(STAT_PRIMITIVES, activeGeometry->primCount_);
                    }
                    else
                    {
                        if (isVR)
                            ctx->DrawInstanced(activeGeometry->vertexCount_, 2, 0, 0);
                        else
                            ctx->Draw(activeGeometry->vertexCount_, 0);
                        device_->AddStat(STAT_PRIMITIVES, activeGeometry->vertexCount_ / 3);
                    }
                    device_->AddStat(STAT_BATCHES, 1);
                    device_->AddStat(STAT_INSTANCES, 1 * instanceMultiplier);
                }
            }
        }
    }

    EndStage(device_, stage);

    if (taskData.deferredCtx_)
        taskData.deferredCtx_->FinishCommandList(TRUE, &taskData.cmdList_);
}

//****************************************************************************
//
//  Function:   RenderScript::RenderFullscreen
//
//  Purpose:    Uses the given effect to do a generic full-screen render.
//              This function makes rocks look smart.
//
//****************************************************************************
void RenderScript::RenderFullscreen(std::shared_ptr<Effect> effect, View view, const Material::UBOBindingList& extraBuffers, const Material::TextureBindingList& extraTextures)
{
    BufferPack pack;
    pack.Pack<float4x4>();

    pack.Get<float4x4>(0) = float4x4::OpenGLOrthoProjLH(0, 1, 2, 2);

	auto ctx = device_->GetD3DContext();
	auto cache = device_->GetDX11State();

    if (auto pass = effect->GetPasses()[0])
    {
        auto quadCmdUBO = device_->GetScratchUniformBuffer(pack.allocSize_);
        pack.Transfer(quadCmdUBO, false);

		ctx->VSSetConstantBuffers(BINDING_QUAD_VIEW_MATRIX, 1, &quadCmdUBO->buffer_);
		ctx->PSSetConstantBuffers(BINDING_QUAD_VIEW_MATRIX, 1, &quadCmdUBO->buffer_);

		cache->SetShaders(pass->GetVS(), pass->GetPS(), nullptr, nullptr, nullptr);

		// bind the geometry and textures
		auto geo = device_->GetFSTriGeometry();
		geo->layout_->Bind(geo.get(), { });

		for (uint32_t i = 0; i < extraBuffers.size(); ++i)
		{
			ctx->VSSetConstantBuffers(extraBuffers[i].slot_, 1, &extraBuffers[i].buffer_->buffer_);
			ctx->PSSetConstantBuffers(extraBuffers[i].slot_, 1, &extraBuffers[i].buffer_->buffer_);
		}
        
		for (auto& extraTex : extraTextures)
			dx_BindTexture(device_, ctx, extraTex.texture_.get(), extraTex.slot_, extraTex.sampling_.filter_, extraTex.sampling_.wrap_, false);

		D3D11_DEPTH_STENCIL_DESC depthState = device_->GetDX11State()->Default<D3D11_DEPTH_STENCIL_DESC>();
		depthState.DepthEnable = FALSE;
		depthState.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;

		D3D11_RASTERIZER_DESC rasterState = device_->GetDX11State()->Default<D3D11_RASTERIZER_DESC>();
		rasterState.CullMode = D3D11_CULL_NONE;
		rasterState.FillMode = D3D11_FILL_SOLID;
		
		device_->GetDX11State()->SetRasterState(rasterState);
		device_->GetDX11State()->SetDepthStencilState(depthState);
		ctx->IASetPrimitiveTopology(pass->IsTessellating() ? D3D11_PRIMITIVE_TOPOLOGY_3_CONTROL_POINT_PATCHLIST : D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		ctx->Draw(3, 0);

        device_->AddStat(STAT_BATCHES, 1);
        device_->AddStat(STAT_PRIMITIVES, 1);
        device_->AddStat(STAT_INSTANCES, 1);
    }
}

//****************************************************************************
//
//  Function:   Renderer::FinishRendering
//
//  Purpose:    Nothing to do on DX11 (finish is the head's responsibility)
//
//****************************************************************************
void Renderer::FinishRendering()
{
}

//****************************************************************************
//
//  Function:   Renderer::Draw2DBatches
//
//  Purpose:    Draws a collection of 2D batches, in reality this thing
//              is basically written exactly for dear-imgui,
//              but it is just a vertex-pump with clipping and single-texture
//              so - meh.
//
//****************************************************************************
void Renderer::Draw2DBatches(const std::vector<Draw2D>& calls, View forView, RenderScript* script)
{
    if (forView.renderTarget_ == nullptr)
        forView.renderTarget_ = device_->GetBackbuffer();

    if (calls.size() > 0)
    {    
		auto state = device_->GetDX11State();
        auto ctx = device_->GetD3DContext();
		D3D11_DEPTH_STENCIL_DESC depthState = state->Default<D3D11_DEPTH_STENCIL_DESC>();
		depthState.DepthEnable = FALSE;
		depthState.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
		
		D3D11_RASTERIZER_DESC rasterState = state->Default<D3D11_RASTERIZER_DESC>();
		rasterState.CullMode = D3D11_CULL_NONE;
		rasterState.ScissorEnable = TRUE;
		
		state->SetDepthStencilState(depthState);

        if (uiGeometry_ == nullptr)
        {
            uiGeometry_.reset(new Geometry());
            uiGeometry_->vertexBuffers_.push_back(device_->CreateVertexBuffer());
            uiGeometry_->vertexBuffers_[0]->SetTag(BufferTag_Dynamic);
            uiGeometry_->layout_ = device_->GetLayout_2D();

            uiUBO_ = device_->CreateUniformBuffer();
            uiUBO_->SetTag(BufferTag_Dynamic);
        }

        float4 sz = calls[0].domain_;
        float L = sz.x;
        float R = sz.x + sz.z;
        float T = sz.y;
        float B = sz.y + sz.w;
        
        const float ortho_projection[4][4] =
        {
            { 2.0f / (R - L),   0.0f,                   0.0f,   0.0f },
            { 0.0f,             2.0f / (T - B),        0.0f,   0.0f },
            { 0.0f,             0.0f,                  -1.0f,   0.0f },
            { ((R + L) / (L - R)),  ((T + B) / (B - T)),    0.0f,   1.0f },
        };
        
        auto mat = float4x4((const float*)ortho_projection);
        //mat.Transpose();
        uiUBO_->SetData(&mat, sizeof(float4x4));

        uiGeometry_->vertexBuffers_[0]->SetSize(calls[0].vertices_->size() * sizeof(Vertex2D));
        Vertex2D* dataStart = &((*calls[0].vertices_)[0]);
        uiGeometry_->vertexBuffers_[0]->SetData(dataStart, calls[0].vertices_->size() * sizeof(Vertex2D));
        uiGeometry_->InferValuesFromData();

        forView.renderTarget_->Bind();
        int4 vpt = calls[0].viewport_;
        int4 clip = calls[0].clipRect_;

        auto fxUI = device_->GetGUIEffect();
        script->ApplyPass(fxUI->GetPasses()[0], forView);
		state->SetRasterState(rasterState);

        int4 clip_rect = clip;

		D3D11_RECT r;
		r.left = clip_rect.x;
		r.right = clip_rect.z;
		r.top = clip_rect.y;
		r.bottom = clip_rect.w;
		ctx->RSSetScissorRects(1, &r);

		D3D11_VIEWPORT viewport = CreateDXType<D3D11_VIEWPORT>();
		viewport.MinDepth = 0.0f; viewport.MaxDepth = 1.0f;
		viewport.TopLeftX = vpt.x;
		viewport.TopLeftY = vpt.y;
		viewport.Width = vpt.z;
		viewport.Height = vpt.w;
		ctx->RSSetViewports(1, &viewport);

        for (uint32_t i = 0; i < calls.size(); ++i)
        {
            if (memcmp(&calls[i].viewport_, &vpt, sizeof(int4)) != 0)
            {
                vpt = calls[i].viewport_;
				D3D11_VIEWPORT viewport;
				viewport.MinDepth = 0.0f; viewport.MaxDepth = 1.0f;
				viewport.TopLeftX = vpt.x;
				viewport.TopLeftY = vpt.y;
				viewport.Width = vpt.Width();
				viewport.Height = vpt.Height();
				ctx->RSSetViewports(1, &viewport);
            }

            if (memcmp(&calls[i].clipRect_, &clip, sizeof(int4)) != 0)
            {
                clip = calls[i].clipRect_;
                clip_rect = clip;

				D3D11_RECT r;
				r.left = clip_rect.x;
				r.right = clip_rect.z;
				r.top = clip_rect.y;
				r.bottom = clip_rect.w;
				ctx->RSSetScissorRects(1, &r);
            }
            
            script->ApplyGeometry(fxUI->GetPasses()[0], uiGeometry_.get(), false);

			ctx->VSSetConstantBuffers(0, 1, &uiUBO_->buffer_);
			ctx->PSSetConstantBuffers(0, 1, &uiUBO_->buffer_);

			auto sampler = device_->GetSampler(FILTER_LINEAR, true);
			ctx->PSSetSamplers(0, 1, &sampler);
			ctx->PSSetShaderResources(0, 1, &calls[i].texture_->srv_);
			
			ctx->Draw(calls[i].vertexCount_, calls[i].vertexStart_);
        }
    }
}

}