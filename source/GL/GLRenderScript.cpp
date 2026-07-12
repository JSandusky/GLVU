//****************************************************************************
//
//  File:       GLRenderScript.cpp
//  License:    MIT
//  Project:    GLVU
//  Contents:   Implements actual rendering via OpenGL
//
//  WARNING:    Should rarely touch actual OpenGL functions and instead
//              should use the GLState held by the GraphicsDevice.
//
//****************************************************************************

#include "RenderScript.h"

#include "Buffer.h"
#include "LightShadow.h"
#include "Material.h"
#include "GraphicsDevice.h"
#include "Effect.h"
#include "Packing.h"
#include "Renderables.h"
#include "Renderer.h"

#include <algorithm>

using namespace math;
using namespace std;

namespace GLVU
{

extern GLuint gl_BufferSlot(BufferKind kind);

#define BINDING_QUAD_CUSTOM 0
#define BINDING_QUAD_VIEW_MATRIX 3
#define BINDING_QUAD_SIZE_DATA 4

void gl_BindTexture(GraphicsDevice* device, uint32_t bindingPt, Texture* texture, TextureFilter filter, bool wrap)
{
    auto& state = device->GetGLState();
    if (state.SetTexture(bindingPt, texture->GetGPUObject(), texture->GetTarget()))
    {
        glActiveTexture(GL_TEXTURE0 + bindingPt);
        auto tgt = texture->GetTarget();
        glBindTexture(tgt, texture->GetGPUObject());
        auto samp = device->GetSampler(filter, wrap);
        //if (state.SetSampler(bindingPt, samp))
            glBindSampler(bindingPt, samp);
    }
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

    device->GetGLState().SetViewport(view.viewport_.x, view.viewport_.y, view.viewport_.Width(), view.viewport_.Height());

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
                    gl_BindTexture(device_, targetInputBinding.first, rec->texture_.get(), FILTER_POINT, true);
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
    //if (device->GetGLState().SetFBO(0))
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
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
    viewUniformBuffer_->SetData(&viewBufferData_, sizeof(ViewBufferData));

    // Verify that all of our shadow data is up to date
    for (auto& batch : batches)
    {
        if (batch.material_)
            batch.material_->CommitUniforms();
    }

    auto& glState = device_->GetGLState();

    for (uint32_t stageIdx = 0; stageIdx < stages_.size(); ++stageIdx)
    {
        const auto& stage = stages_[stageIdx];
        if (stage->active_)
        {
            bool ignoreThisStage = false;
            for (const auto& req : stage->requireActiveStages_)
            {
                if (!GetStage(req.nameHash_)->active_)
                    ignoreThisStage = true;
            }

            for (const auto& req : stage->ignoreActiveStages_)
            {
                if (GetStage(req.nameHash_)->active_)
                    ignoreThisStage = true;
            }

            if (ignoreThisStage)
                continue;

            BeginStage(device_, view, stage);

            // make sure our viewport is set
            glState.SetViewport(view.viewport_.x, view.viewport_.y, view.viewport_.Width(), view.viewport_.Height());

            for (const auto& cmd : stage->commands_)
            {
                if (cmd.commandType_ == ClearTargets)
                {
                    glClearColor(cmd.cmdData_.clearData_.color_[0], cmd.cmdData_.clearData_.color_[1], cmd.cmdData_.clearData_.color_[2], cmd.cmdData_.clearData_.color_[3]);
                    glClearDepth(cmd.cmdData_.clearData_.depth_);
                    glClearStencil(cmd.cmdData_.clearData_.stencilValue_);
                    GLuint mask = 0;
                    //if (cmd.commandData_.clearData_.discardColor_)
                        mask |= GL_COLOR_BUFFER_BIT;
                    //if (cmd.commandData_.clearData_.discardStencil_)
                    {
                        mask |= GL_STENCIL_BUFFER_BIT;
                        glState.SetStencilMask(0xFF);
                    }
                    //if (cmd.commandData_.clearData_.discardDepth_) 
                    {
                        mask |= GL_DEPTH_BUFFER_BIT;
                        glState.SetDepthMask(true);
                    }
                            
                    glClear(mask);

                    if (cmd.cmdData_.clearData_.discardDepth_)
                        glState.SetDepthMask(false);
                    if (cmd.cmdData_.clearData_.discardStencil_)
                        glState.SetStencilMask(0);
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
                    task.skinnedNameHash_ = Hash(string(cmd.context_.name_) + "_SKINNED");
                    task.instancedNameHash_ = Hash(string(cmd.context_.name_) + "_INST");
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
                    task.skinnedNameHash_ = Hash(string(cmd.context_.name_) + "_SKINNED");
                    task.instancedNameHash_ = Hash(string(cmd.context_.name_) + "_INST");
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

                    auto& glState = device_->GetGLState();
                    glState.SetDepthTest(true);
                    glState.SetDepthMask(false);

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
                            glState.SetCulling(true);
                            //if (light->Contains(view.camera_))
                            {
                                glState.SetCullingFace(GL_BACK);
                                glState.SetDepthFunc(GL_GEQUAL);
                            }
                            //else
                            //{
                            //    glState.SetCullingFace(GL_FRONT);
                            //    glState.SetDepthFunc(GL_LEQUAL);
                            //}
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

                            glState.SetDepthTest(false);
                            glState.SetDepthMask(false);
                            
                        }

                        if (light->GetKind() != Light::DIRECTIONAL)
                            glState.SetDepthTest(true);

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
							glBindBufferBase(GL_UNIFORM_BUFFER, SHADER_BUFFER_VIEW_DATA, buffers.viewBuffer->GetGPUObject());
							lastViewBuffer = buffers.viewBuffer;
						}
						auto lightD = rollLightData.GetAllocation(buffers.dataIndex);
						auto lightT = rollLightData.GetAllocation(buffers.transIndex);
						glBindBufferRange(GL_UNIFORM_BUFFER, 0, lightD.buffer_->GetGPUObject(), lightD.allocationOffset_, lightD.allocationSize_);
						glBindBufferRange(GL_UNIFORM_BUFFER, 2, lightT.buffer_->GetGPUObject(), lightT.allocationOffset_, lightT.allocationSize_);
                            
                        if (geometry->indexBuffer_)
                        {
                            glDrawElements(GL_TRIANGLES, geometry->indexCount_, GL_UNSIGNED_SHORT, (void*)(geometry->indexStart_ * 2));
                            device_->AddStat(STAT_PRIMITIVES, geometry->primCount_);
                        }
                        else
                        {
                            glDrawArrays(GL_TRIANGLES, 0, geometry->vertexCount_);
                            device_->AddStat(STAT_PRIMITIVES, geometry->primCount_);
                        }
                        device_->AddStat(STAT_BATCHES, 1);
                        device_->AddStat(STAT_INSTANCES, 1);
                    }

                    glState.SetDepthTest(true);
                    glState.SetDepthMask(true);

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

                    // bind the geometry and textures
                    auto geo = device_->GetFSTriGeometry();
                    geo->layout_->Bind(geo.get(), { });

                    // beware: this can blow out texture bindings
                    BeginStage(device_, view, stage);

                    for (auto tex : cmd.textures_)
                        gl_BindTexture(device_, tex.slot_, tex.texture_.get(), tex.sampling_.filter_, tex.sampling_.wrap_);

                    // bind UBOs then draw
                    glBindBufferRange(GL_UNIFORM_BUFFER, BINDING_QUAD_VIEW_MATRIX, quadCmdUBO->GetGPUObject(), 0, pack.SizeOf(0));
                    glBindBufferRange(GL_UNIFORM_BUFFER, BINDING_QUAD_SIZE_DATA, quadCmdUBO->GetGPUObject(), pack.OffsetOf(1), pack.SizeOf(1));
                    glBindBufferRange(GL_UNIFORM_BUFFER, BINDING_QUAD_CUSTOM, quadCmdUBO->GetGPUObject(), pack.OffsetOf(2), pack.SizeOf(2));
                    glUseProgram(cmd.effect_->GetPasses()[0]->GetGPUObject());
                    
                    ApplyBlendMode(BlendMode::Blend_None, view, stage);
                    glState.SetCulling(false);
                    glState.SetDepthTest(false);
                    glState.SetDepthMask(false);

                    glDrawArrays(GL_TRIANGLES, 0, 3);
                    device_->AddStat(STAT_BATCHES, 1);
                    device_->AddStat(STAT_PRIMITIVES, 1);
                    device_->AddStat(STAT_INSTANCES, 1);
                    EndStage(device_, stage);
                    glUseProgram(0);
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
									if (found->GetBufferKind() == ShaderDataBufferObject)
										glBindBufferBase(GL_SHADER_STORAGE_BUFFER, uboRecord.first, found->GetGPUObject());
									else if (found->GetBufferKind() == UniformBufferObject)
										glBindBufferBase(GL_UNIFORM_BUFFER, uboRecord.first, found->GetGPUObject());
								}
							}

							if (cmd.numParams_ > 0)
							{
								auto scratch = device_->GetScratchUniformBuffer(sizeof(float) * RENDER_SCRIPT_MAX_PARAMS);
								scratch->SetData((void*)cmd.params_, sizeof(float) * RENDER_SCRIPT_MAX_PARAMS);
								glBindBufferBase(GL_UNIFORM_BUFFER, 0, scratch->GetGPUObject());
							}

							for (auto& texRecord : cmd.textures_)
								cmd.effect_->BindTexture(texRecord.texture_, texRecord.slot_, nullptr);

							glDispatchCompute(cmd.cmdData_.computeData_.groupsX_, cmd.cmdData_.computeData_.groupsY_, cmd.cmdData_.computeData_.groupsZ_);
							glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT | GL_SHADER_STORAGE_BARRIER_BIT);
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

						glBindBuffer(GL_COPY_READ_BUFFER, srcBuffer->GetGPUObject());
						glBindBuffer(GL_COPY_WRITE_BUFFER, destBuffer->GetGPUObject());
						glCopyBufferSubData(GL_COPY_READ_BUFFER, GL_COPY_WRITE_BUFFER, 0, 0, srcBuffer->GetSize());
						glBindBuffer(GL_COPY_READ_BUFFER, 0);
						glBindBuffer(GL_COPY_WRITE_BUFFER, 0);

						//CPU: auto gpuData = srcBuffer->GetGPUData();
						//CPU: //?? is it an error to have no data to copy?
						//CPU: if (gpuData.data_ != nullptr && gpuData.size_ > 0)
						//CPU: 	destBuffer->SetData(gpuData.data_, gpuData.size_);
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
                        glState.SetViewport(0, 0, destTex->GetWidth(), destTex->GetHeight());
                        RenderFullscreen(passThru, view, 
                            { }, 
                            { 
                                { 0, { FILTER_LINEAR, false }, srcTex } 
                            }
                        );
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
    glUseProgram(pass->GetGPUObject());

    auto& glState = device_->GetGLState();
    const auto& drawState = pass->GetDrawState();

    if (drawState.culling_ != CULL_NONE)
    {
        glState.SetCulling(true);
        glState.SetCullingFace(drawState.culling_ == CULL_FRONT ? GL_FRONT : GL_BACK);
    }
    else
        glState.SetCulling(false);

    glState.SetAlphaTest(drawState.alphaTest_);

    //if (drawState.slopeBias_ != 0 || drawState.depthBias_ != 0)
    //{
    //    glEnable(GL_POLYGON_OFFSET_FILL);
    //    glPolygonOffset(pass->GetDrawState().slopeBias_, pass->GetDrawState().depthBias_);
    //}
    //else
    //    glDisable(GL_POLYGON_OFFSET_FILL);

    switch (drawState.depthCompare_)
    {
    case COMPARE_ALWAYS: glState.SetDepthFunc(GL_ALWAYS); break;
    case COMPARE_NEVER: glState.SetDepthFunc(GL_NEVER); break;
    case COMPARE_EQUAL: glState.SetDepthFunc(GL_EQUAL); break;
    case COMPARE_NOT_EQUAL: glState.SetDepthFunc(GL_NOTEQUAL); break;
    case COMPARE_LEQUAL: glState.SetDepthFunc(GL_LEQUAL); break;
    case COMPARE_GEQUAL: glState.SetDepthFunc(GL_GEQUAL); break;
    case COMPARE_GREATER: glState.SetDepthFunc(GL_GREATER); break;
    case COMPARE_LESS: glState.SetDepthFunc(GL_LESS); break;
    }

    glState.SetAlphaToCoverage(drawState.alphaToCoverage_);

    glState.SetDepthTest(drawState.depthWrite_);
    glState.SetDepthMask(drawState.depthWrite_);
    glState.SetBlendMode(drawState.blendMode_);

    if (drawState.stencilWrite_)
        glState.SetStencilFunc(GL_ALWAYS, drawState.stencilMask_, drawState.stencilWrite_);
    else
        glState.SetStencilFunc(GL_NEVER, 0, 0);

    glState.SetStencilTest(drawState.stencilTest_);
    glState.SetStencilMask(drawState.stencilMask_);
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
    layout->Bind(geometry, extraBuffers, laterInstanced); // this will deal with our VBOs and their traits

    if (!laterInstanced)
    {
        if (pass->IsTessellating())
            glPatchParameteri(GL_PATCH_VERTICES, 3);

        if (geometry->indexBuffer_ != nullptr)
            glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, geometry->indexBuffer_->GetGPUObject());
        else
            glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
    }
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
    device_->GetGLState().SetBlendMode(blendMode);


    // DEPRECATED: in-progress, consolidating state changes to minimize them (even if the driver should be something other than a complete moron).

                                 // ALPHA                // ADDITIVE         // SUBTRACT                 // MULTIPLY         // PREMULTIPLIED
    //static const GLuint glBlendSrcTable[] =  {0, GL_SRC_ALPHA,           GL_ONE,             GL_ONE,                     GL_DST_COLOR,       GL_ONE };
    //static const GLuint glBlendDestTable[] = {0, GL_ONE_MINUS_SRC_ALPHA, GL_ONE,             GL_ONE,                     GL_ZERO,            GL_ONE_MINUS_SRC_ALPHA };
    //static const GLuint glBlendFuncTable[] = {0, GL_FUNC_ADD,            GL_FUNC_ADD,        GL_FUNC_REVERSE_SUBTRACT,   GL_FUNC_ADD,        GL_FUNC_ADD };
    //glBlendFuncSeparate(glBlendSrcTable[blendMode], glBlendDestTable[blendMode], glBlendSrcTable[blendMode], glBlendDestTable[blendMode]);
    //glBlendEquation(glBlendFuncTable[blendMode]);
    //if (blendMode != Blend_None)
    //    glEnable(GL_BLEND);
    //else
    //    glDisable(GL_BLEND);
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

    auto& glState = GetDevice()->GetGLState();
    glState.SetViewport(view.viewport_.x, view.viewport_.y, view.viewport_.Width(), view.viewport_.Height());
    glState.SetDepthRange(0.0f, 1.0f);

    auto transformUBO = device_->GetScratchUniformBuffer(64);
    GLBufferStates buffStates;

    static auto applyMaterial = [](GraphicsDevice* device, shared_ptr<ShaderPass> pass, Material* mat, DrawBatchesTaskData& taskData, GLBufferStates& buffStates) {
        for (auto& uboRecord : mat->GetUBOs())
        {
            if (uboRecord.buffer_->GetBufferKind() == UniformBufferObject)
                buffStates.Bind(GL_UNIFORM_BUFFER, uboRecord.buffer_->GetGPUObject(), uboRecord.slot_);
                //glBindBufferBase(GL_UNIFORM_BUFFER, uboRecord.slot_, uboRecord.buffer_->GetGPUObject());
#if !defined(GLVU_GLES3)
            else
                buffStates.Bind(GL_SHADER_STORAGE_BUFFER, uboRecord.buffer_->GetGPUObject(), uboRecord.slot_);
                //glBindBufferBase(GL_SHADER_STORAGE_BUFFER, uboRecord.slot_, uboRecord.buffer_->GetGPUObject());
#endif
        }

        for (auto& uboRecord : taskData.uboBindings_)
        {
            if (uboRecord.buffer_->GetBufferKind() == UniformBufferObject)
                buffStates.Bind(GL_UNIFORM_BUFFER, uboRecord.buffer_->GetGPUObject(), uboRecord.slot_);
                //glBindBufferBase(GL_UNIFORM_BUFFER, uboRecord.slot_, uboRecord.buffer_->GetGPUObject());
#if !defined(GLVU_GLES3)
            else
                buffStates.Bind(GL_SHADER_STORAGE_BUFFER, uboRecord.buffer_->GetGPUObject(), uboRecord.slot_);
                //glBindBufferBase(GL_SHADER_STORAGE_BUFFER, uboRecord.slot_, uboRecord.buffer_->GetGPUObject());
#endif
        }

        for (auto& texRecord : mat->GetTextures())
            mat->GetEffect()->BindTexture(texRecord.texture_, texRecord.slot_, pass.get());

        for (auto& texRecord : taskData.textureBindings_)
            mat->GetEffect()->BindTexture(texRecord.texture_, texRecord.slot_, pass.get());
    };
        
    BeginStage(device_, view, stage);

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
                applyMaterial(device_, pass, activeMaterial, taskData, buffStates);
            }

            if (activeGeometry != grp.geometry_)
            {
                activeGeometry = grp.geometry_;
                ApplyGeometry(pass, activeGeometry, true, { instBatch.second.instanceTransformBuffers_[0].second });
            }

            GLenum mode = GL_TRIANGLES;
            if (activeGeometry->primType_ == POINT_LIST)
                mode = GL_POINTS;
            else if (activeGeometry->primType_ == LINE_LIST)
                mode = GL_LINES;
            if (activePass->IsTessellating())
                mode = GL_PATCHES;

            for (size_t i = 0; i < grp.instanceTransformBuffers_.size(); ++i)
            {
                auto instBuff = grp.instanceTransformBuffers_[i].second;
                uint32_t drawCt = grp.instanceTransformBuffers_[i].first;
                ApplyGeometry(pass, activeGeometry, true, { instBatch.second.instanceTransformBuffers_[i].second }, true);

                //if (!grp.materialTraitUBOs_.empty())
                //    glBindBufferBase(GL_UNIFORM_BUFFER, UBO_SLOT_MATERIAL, grp.materialTraitUBOs_[i]->GetGPUObject());

                if (activeGeometry->indexBuffer_)
                {
                    glDrawElementsInstanced(mode, activeGeometry->indexCount_, GL_UNSIGNED_SHORT, (void*)(activeGeometry->indexStart_ * 2), drawCt);
                    device_->AddStat(STAT_PRIMITIVES, activeGeometry->primCount_ * drawCt);
                }
                else
                {
                    glDrawArraysInstanced(mode, 0, activeGeometry->vertexCount_, drawCt);
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
            if (activePass != pass)
            {
                activePass = pass;
                ApplyPass(pass, view);
            }

            if (activeMaterial != batch.material_)
            {
                activeMaterial = batch.material_;
                applyMaterial(device_, pass, activeMaterial, taskData, buffStates);
            }

            if (activeGeometry != batch.geometry_)
            {
                activeGeometry = batch.geometry_;
                ApplyGeometry(pass, activeGeometry, false, { });
            }

            GLenum mode = GL_TRIANGLES;
            if (activeGeometry->primType_ == POINT_LIST)
                mode = GL_POINTS;
            else if (activeGeometry->primType_ == LINE_LIST)
                mode = GL_LINES;
            if (activePass->IsTessellating())
                mode = GL_PATCHES;

            if (batch.isSkinned_)
            {
                glBindBufferBase(GL_UNIFORM_BUFFER, SHADER_BUFFER_OBJECT_DATA, batch.bonesBuffer_->GetGPUObject());
                if (activeGeometry->indexBuffer_)
                {
                    glDrawElementsBaseVertex(mode, activeGeometry->indexCount_, GL_UNSIGNED_SHORT, 0, 0);
                    device_->AddStat(STAT_PRIMITIVES, activeGeometry->indexCount_ / 3);
                }
                else
                {
                    glDrawArrays(mode, 0, activeGeometry->vertexCount_);
                    device_->AddStat(STAT_PRIMITIVES, activeGeometry->vertexCount_ / 3);
                }
                device_->AddStat(STAT_BATCHES, 1);
                device_->AddStat(STAT_INSTANCES, 1);
            }
            else
            {
                for (uint32_t i = 0; i < batch.numTransforms_; ++i)
                {
                    transformUBO->SetData(batch.transforms_ + sizeof(float4x4) * i, sizeof(float4x4));
                    buffStates.Bind(GL_UNIFORM_BUFFER, transformUBO->GetGPUObject(), SHADER_BUFFER_OBJECT_DATA);
                    //glBindBufferBase(GL_UNIFORM_BUFFER, SHADER_BUFFER_OBJECT_DATA, transformUBO->GetGPUObject());
                    if (activeGeometry->indexBuffer_)
                    {
                        glDrawElements(mode, activeGeometry->indexCount_, GL_UNSIGNED_SHORT, (void*)(activeGeometry->indexStart_ * 2));
                        device_->AddStat(STAT_PRIMITIVES, activeGeometry->primCount_);
                    }
                    else
                    {
                        glDrawArrays(mode, 0, activeGeometry->vertexCount_);
                        device_->AddStat(STAT_PRIMITIVES, activeGeometry->vertexCount_ / 3);
                    }
                    device_->AddStat(STAT_BATCHES, 1);
                    device_->AddStat(STAT_INSTANCES, 1);
                }
            }
        }
    }

    EndStage(device_, stage);
}

//****************************************************************************
//
//  Function:   RenderScript::RenderFullscreen
//
//  Purpose:    Uses the given effect to do a generic full-screen render.
//              This function is makes rocks look smart.
//
//****************************************************************************
void RenderScript::RenderFullscreen(std::shared_ptr<Effect> effect, View view, const Material::UBOBindingList& extraBuffers, const Material::TextureBindingList& extraTextures)
{
    BufferPack pack;
    pack.Pack<float4x4>();

    pack.Get<float4x4>(0) = float4x4::OpenGLOrthoProjLH(0, 1, 2, 2);

    if (auto pass = effect->GetPasses()[0])
    {
        auto& glState = device_->GetGLState();

        auto quadCmdUBO = device_->GetScratchUniformBuffer(pack.allocSize_);
        pack.Transfer(quadCmdUBO, false);

        // bind the geometry and textures
        auto geo = device_->GetFSTriGeometry();
        geo->layout_->Bind(geo.get(), { });

        // bind UBOs then draw
        glBindBufferRange(GL_UNIFORM_BUFFER, BINDING_QUAD_VIEW_MATRIX, quadCmdUBO->GetGPUObject(), 0, pack.SizeOf(0));
        //glBindBufferRange(GL_UNIFORM_BUFFER, BINDING_QUAD_SIZE_DATA, quadCmdUBO->GetGPUObject(), pack.OffsetOf(1), pack.SizeOf(1));
        //glBindBufferRange(GL_UNIFORM_BUFFER, BINDING_QUAD_CUSTOM, quadCmdUBO->GetGPUObject(), pack.OffsetOf(2), pack.SizeOf(2));
        glUseProgram(pass->GetGPUObject());

        for (uint32_t i = 0; i < extraBuffers.size(); ++i)
            glBindBufferBase(GL_UNIFORM_BUFFER, extraBuffers[i].slot_, extraBuffers[i].buffer_->GetGPUObject());
        
        for (uint32_t i = 0; i < extraTextures.size(); ++i)
            gl_BindTexture(device_, extraTextures[i].slot_, extraTextures[i].texture_.get(), extraTextures[i].sampling_.filter_, extraTextures[i].sampling_.wrap_);

        glState.SetDepthTest(false);
        glState.SetDepthMask(false);
        
        glDrawArrays(GL_TRIANGLES, 0, 3);
        device_->AddStat(STAT_BATCHES, 1);
        device_->AddStat(STAT_PRIMITIVES, 1);
        device_->AddStat(STAT_INSTANCES, 1);
        glUseProgram(0);
    }
}

//****************************************************************************
//
//  Function:   Renderer::FinishRendering
//
//  Purpose:    Nothing to do on OpenGL (finish is the head's responsibility)
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
        auto& glState = device_->GetGLState();

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
        glState.SetDepthTest(false);
        glState.SetDepthMask(false);
        

        auto fxUI = device_->GetGUIEffect();
        script->ApplyPass(fxUI->GetPasses()[0], forView);

        glState.SetScissorOn(true);

        int4 clip_rect = clip;
        glScissor((int)clip_rect.x, (int)(vpt.Height() - clip_rect.w), (int)(clip_rect.z - clip_rect.x), (int)(clip_rect.w - clip_rect.y));
        glState.SetViewport(vpt.x, vpt.y, vpt.z, vpt.w);

        for (uint32_t i = 0; i < calls.size(); ++i)
        {
            if (memcmp(&calls[i].viewport_, &vpt, sizeof(int4)) != 0)
            {
                vpt = calls[i].viewport_;
                glState.SetViewport(vpt.x, vpt.y, vpt.z, vpt.w);
            }

            if (memcmp(&calls[i].clipRect_, &clip, sizeof(int4)) != 0)
            {
                clip = calls[i].clipRect_;
                clip_rect = clip;
                glScissor((int)clip_rect.x, (int)(vpt.Height() - clip_rect.w), (int)(clip_rect.z - clip_rect.x), (int)(clip_rect.w - clip_rect.y));
            }
            
            script->ApplyGeometry(fxUI->GetPasses()[0], uiGeometry_.get(), false);

            glBindBufferBase(GL_UNIFORM_BUFFER, 0, uiUBO_->GetGPUObject());

            gl_BindTexture(device_, 0, calls[i].texture_, FILTER_LINEAR, true);
            glDrawArrays(GL_TRIANGLES, calls[i].vertexStart_, calls[i].vertexCount_);
        }
        
        glState.SetScissorOn(false);
    }
}

}