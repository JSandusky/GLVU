#include "RenderScript.h"

#include "Buffer.h"
#include "Material.h"
#include "GraphicsDevice.h"
#include "GraphicsDeviceHead.h"
#include "Effect.h"
#include "LightShadow.h"
#include "Packing.h"
#include "Renderables.h"
#include "Renderer.h"
#include "ShaderConstants.h"

#include <algorithm>
#include <functional>

#pragma optimize("", off)

using namespace math;
using namespace std;

#define LOG_VULKAN(MSG, RESULT) device_->LogFormat(GLVU_ERROR, MSG ": %u", RESULT)

namespace GLVU
{

#define BINDING_QUAD_CUSTOM 0
#define BINDING_QUAD_VIEW_MATRIX 3
#define BINDING_QUAD_SIZE_DATA 4

VkCommandBuffer RS_StartCommandBuffer(GraphicsDevice* device)
{
    auto buffer = device->GetGraphicsCmdBuffer();
    vezBeginCommandBuffer(buffer, VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
    return buffer;
}

void RS_SetupViewport(GraphicsDevice* device, const View& view)
{
    VkViewport viewport = { static_cast<float>(view.viewport_[0]), static_cast<float>(view.viewport_[1]), static_cast<float>(view.viewport_[2]), static_cast<float>(view.viewport_[3]), 0.0f, 1.0f };
    VkRect2D scissor = { { (int)view.viewport_[0], (int)view.viewport_[1] }, { view.viewport_.Width(), view.viewport_.Height() } };
    vezCmdSetViewport(0, 1, &viewport);
    vezCmdSetScissor(0, 1, &scissor);
    vezCmdSetViewportState(1);
}

//****************************************************************************
//
//  Function:   RenderScript::BeginStage
//
//  Purpose:    For Vulkan there's a lot of boiler-plate even with VEZ and this
//              function deals with that for binding a render-target setup to draw
//              to and the render-targets that will be read.
//
//****************************************************************************
void RenderScript::BeginStage(GraphicsDevice* device, View view, RenderScriptStage* stage)
{
    // setup our output target configuration
    VezRenderPassBeginInfo passInfo = {};
    vector<VezAttachmentInfo> attachmentInfo;
    RenderTargetConfiguration* targetConfig = nullptr;
    for (auto& targetID : stage->targets_)
    {
        VezAttachmentInfo attach = {};
        attach.loadOp = VkAttachmentLoadOp::VK_ATTACHMENT_LOAD_OP_LOAD;
        attach.storeOp = VkAttachmentStoreOp::VK_ATTACHMENT_STORE_OP_STORE;
        attachmentInfo.push_back(attach);
    }

    if (stage && stage->targetConfig_.fbo_ != 0)
        passInfo.framebuffer = stage->targetConfig_.fbo_->GetGPUObject();
    else
    {
        passInfo.framebuffer = device->GetBackbuffer()->GetGPUObject();

        VezAttachmentInfo attach = { };
        attach.loadOp = VkAttachmentLoadOp::VK_ATTACHMENT_LOAD_OP_LOAD;
        attach.storeOp = VkAttachmentStoreOp::VK_ATTACHMENT_STORE_OP_STORE;
        attachmentInfo.push_back(attach);
        attachmentInfo.push_back(attach);
    }

    passInfo.pAttachments = attachmentInfo.data();
    passInfo.attachmentCount = attachmentInfo.size();
    vezCmdBeginRenderPass(&passInfo);

    // bind render target textures from inputs
    for (auto& targetInputBinding : stage->targetBindings_)
    {
        bool texFound = false;
        for (auto rec : targetTextures_)
        {
            if (rec->name_.nameHash_ == targetInputBinding.second.nameHash_)
            {
                if (auto found = GetTargetTexture(targetInputBinding.second.nameHash_))
                {
                    if (found->texture_->IsTextureBuffer())
                        vezCmdBindBufferView(found->texture_->GetTextureBufferView(), 1, targetInputBinding.first, 0);
                    else
                        vezCmdBindImageView(rec->texture_->GetView(), device_->GetSampler(FILTER_LINEAR, false), 1, targetInputBinding.first, 0);
                    texFound = true;
                }
                else
                {
                    if (auto system = device->GetSystemTexture(rec->name_.name_))
                    {
                        if (system->IsTextureBuffer())
                            vezCmdBindBufferView(system->GetTextureBufferView(), 1, targetInputBinding.first, 0);
                        else
                            vezCmdBindImageView(system->GetView(), device_->GetSampler(FILTER_LINEAR, false), 1, targetInputBinding.first, 0);
                    }
                    else // fallback on a default
                        vezCmdBindImageView(device->GetDefaultTexture()->GetView(), device_->GetSampler(FILTER_LINEAR, false), 1, targetInputBinding.first, 0);
                }

                break;
            }
        }
        if (!texFound)
        {
            if (auto system = device->GetSystemTexture(targetInputBinding.second.name_))
            {
                if (system->IsTextureBuffer())
                    vezCmdBindBufferView(system->GetTextureBufferView(), 1, targetInputBinding.first, 0);
                else
                    vezCmdBindImageView(system->GetView(), device_->GetSampler(FILTER_LINEAR, false), 1, targetInputBinding.first, 0);
            }
        }
    }
}

//****************************************************************************
//
//  Function:   RenderScript::EndStage
//
//  Purpose:    Just closes a render-pass. Each stage consists of 1 VEZ renderpass.
//
//****************************************************************************
void RenderScript::EndStage(GraphicsDevice* device, RenderScriptStage* stage)
{
    vezCmdEndRenderPass();
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

    if (!viewUniformBuffer_)
    {
        viewUniformBuffer_ = device_->CreateUniformBuffer();
        viewUniformBuffer_->SetTag(BufferTag_Dynamic);
        viewUniformBuffer_->SetShadowed(true);
        viewUniformBuffer_->SetSize(sizeof(viewBufferData_));
    }

    SetupViewbufferData(view, viewBufferData_);
    
    viewUniformBuffer_->SetData(&viewBufferData_, sizeof(viewBufferData_));

    // Verify that all of our shadow data is up to date
    for (uint32_t b = 0; b < batches.size(); ++ b)
    {
        auto& batch = batches[b];
        if (batch.material_)
            batch.material_->CommitUniforms();
    }

    BatchQueue queue;
    for (uint32_t b = 0; b < batches.size(); ++b)
    {
        auto& batch = batches[b];
        queue.Add(batch);
    }

    queue.SortFrontToBack();
    PrepareQueue(queue);

    for (uint32_t stageIdx = 0; stageIdx < stages_.size(); ++stageIdx)
    {
        const auto& stage = stages_[stageIdx];
        if (stage->active_)
        {
            if (!ShouldStageExecute(stage))
                continue;

            for (const auto& cmd : stage->commands_)
            {
                if (!cmd.enabled_)
                    continue;

                if (cmd.commandType_ == ClearTargets)
                {
                    auto cmdBuffer = RS_StartCommandBuffer(device_);
                    renderer->GetCommandBufferChain().push_back(cmdBuffer);

                    for (size_t i = 0; i < stage->targetConfig_.targets_.size(); ++i)
                    {
                        auto tgt = stage->targetConfig_.targets_[i];

                        bool clearDepth = cmd.cmdData_.clearData_.discardDepth_;
                        bool clearStencil = cmd.cmdData_.clearData_.discardStencil_;
                        bool clearColor = cmd.cmdData_.clearData_.discardColor_;

                        RS_SetupViewport(device_, view);

                        VezClearAttachment value;
                        value.colorAttachment = (uint32_t)i;
                        if (tgt->targetFormat_ == TEX_DEPTH && (clearDepth || clearStencil))
                        {
                            VkClearDepthStencilValue value = {};
                            value.depth = cmd.cmdData_.clearData_.depth_;
                            value.stencil = cmd.cmdData_.clearData_.stencilValue_;
                            VezImageSubresourceRange range = {};
                            range.layerCount = 1;
                            range.levelCount = 1;
                            vezCmdClearDepthStencilImage(tgt->texture_->GetImage(), &value, 1, &range);
                        }
                        else if (tgt->targetFormat_ != TEX_DEPTH && clearColor)
                        {
                            VezImageSubresourceRange range = {};
                            range.layerCount = 1;
                            range.levelCount = 1;
                            VkClearColorValue value = { cmd.cmdData_.clearData_.color_[0], cmd.cmdData_.clearData_.color_[1], cmd.cmdData_.clearData_.color_[2], cmd.cmdData_.clearData_.color_[3] };
                            vezCmdClearColorImage(tgt->texture_->GetImage(), &value, 1, &range);
                        }
                    }
                    if (stage->targetConfig_.targets_.empty())
                    {
                        // backbuffers
                        for (uint32_t i = 0; i < device_->GetBackbuffer()->GetTextureCount(); ++i)
                        {
                            auto tex = device_->GetBackbuffer()->GetTexture(i);

                            bool clearDepth = cmd.cmdData_.clearData_.discardDepth_;
                            bool clearStencil = cmd.cmdData_.clearData_.discardStencil_;
                            bool clearColor = cmd.cmdData_.clearData_.discardColor_;

                            VezClearAttachment value;
                            value.colorAttachment = (uint32_t)i;
                            if (tex->GetFormat() == TEX_DEPTH && (clearDepth || clearStencil))
                            {
                                VkClearDepthStencilValue value = {};
                                value.depth = cmd.cmdData_.clearData_.depth_;
                                value.stencil = cmd.cmdData_.clearData_.stencilValue_;
                                VezImageSubresourceRange range = {};
                                range.layerCount = 1;
                                range.levelCount = 1;
                                vezCmdClearDepthStencilImage(tex->GetImage(), &value, 1, &range);
                            }
                            else if (tex->GetFormat() != TEX_DEPTH && clearColor)
                            {
                                VezImageSubresourceRange range = {};
                                range.layerCount = 1;
                                range.levelCount = 1;
                                VkClearColorValue value = { cmd.cmdData_.clearData_.color_[0], cmd.cmdData_.clearData_.color_[1], cmd.cmdData_.clearData_.color_[2], cmd.cmdData_.clearData_.color_[3] };
                                vezCmdClearColorImage(tex->GetImage(), &value, 1, &range);
                            }
                        }
                    }

                    vezEndCommandBuffer();
                }
            }

            for (const auto& cmd : stage->commands_)
            {
                if (!cmd.enabled_)
                    continue;

                if (cmd.commandType_ == ClearTargets)
                    continue;
                else if (cmd.commandType_ == GeometryPass)
                {
                    if (device_->ThreadingIsSupported() && queue.ConsiderSplitting())
                    {
                        int threadCt = device_->GetNumThreads();
                        vector<BatchQueue> queues;
                        queue.Split(queues, threadCt);

                        vector<DrawBatchesTaskData> tasks(queues.size());

                        struct GeomPassThreadTaskData {
                            Renderer* renderer;
                            RenderScript* script;
                            RenderScriptStage* stage;
                            DrawBatchesTaskData* taskData;
                            BatchQueue* queue;
                            View view;
                        };
                        vector<GeomPassThreadTaskData> taskData;

                        size_t baseLoc = renderer->GetCommandBufferChain().size();
                        renderer->GetCommandBufferChain().resize(renderer->GetCommandBufferChain().size() + tasks.size());
                        for (uint32_t i = 0; i < tasks.size(); ++i)
                        {
                            tasks[i].uboBindings_.push_back({ 1, 0, UINT_MAX, viewUniformBuffer_ });
                            tasks[i].contextNameHash_ = cmd.context_.nameHash_;
                            tasks[i].skinnedNameHash_ = Hash(string(cmd.context_.name_) + "_SKINNED");
                            tasks[i].instancedNameHash_ = Hash(string(cmd.context_.name_) + "_INST");
                            tasks[i].placeCmdBufferAt_ = baseLoc + i;
                            PrepareQueue(queues[i]);
                            taskData.push_back({ renderer, this, stage, &tasks[i], &queues[i], view });
                        }

                        //TODO
                        for (int i = 0; i < queues.size(); ++i)
                        {
                            device_->PushThreadJob(i, &taskData[i], [](uint32_t taskID, void* data) {
                                GeomPassThreadTaskData* taskData = (GeomPassThreadTaskData*)data;
                                taskData->script->DrawBatches(taskData->renderer, taskData->view, taskData->stage, *(taskData->queue), *(taskData->taskData));
                            });
                        }
                        device_->WaitForThreadJobs();
                    }
                    else
                    {
                        DrawBatchesTaskData task;
                        task.uboBindings_.push_back({ SHADER_BUFFER_VIEW_DATA, 0, UINT_MAX, viewUniformBuffer_ });
                        task.contextNameHash_ = cmd.context_.nameHash_;
                        task.skinnedNameHash_ = Hash(string(cmd.context_.name_) + "_SKINNED");
                        task.instancedNameHash_ = Hash(string(cmd.context_.name_) + "_INST");
                        DrawBatches(renderer, view, stage, queue, task);
                    }
                }
                else if (cmd.commandType_ == FullscreenQuad)
                {
                    if (!cmd.effect_)
                        continue;

                    BufferPack pack(256);

                    struct ViewData {
                        float2 inputTexSize;
                        float2 invInputTexSize;
                        float2 outputTexSize;
                        float2 invOutputTexSize;
                    };

                    struct ParamData {
                        float params[32];
                    };

                    pack.Pack<float4x4>();
                    pack.Pack<ViewData>();
                    pack.Pack<ParamData>();

                    auto quadCmdUBO = device_->GetScratchUniformBuffer(pack.CalculateSize());

                    pack.AllocateUBO(quadCmdUBO);

                    // view matrix
                    pack.Get<float4x4>(0) = float4x4::OpenGLOrthoProjLH(0, 1, 2, 2);

                    // INPUT and OUTPUT size information sources
                    if (cmd.cmdData_.quadData_.inputSize_.IsValid())
                    {
                        if (auto info = GetTargetTexture(cmd.cmdData_.quadData_.inputSize_.nameHash_))
                            pack.Get<ViewData>(1).inputTexSize = { (float)info->width_, (float)info->height_ };
                        else
                            pack.Get<ViewData>(1).inputTexSize = { (float)view.viewport_[2], (float)view.viewport_[3] };
                    }
                    else
                        pack.Get<ViewData>(1).inputTexSize = { (float)view.viewport_[2], (float)view.viewport_[3] };

                    if (cmd.cmdData_.quadData_.outputSize_.IsValid())
                    {
                        if (auto info = GetTargetTexture(cmd.cmdData_.quadData_.outputSize_.nameHash_))
                            pack.Get<ViewData>(1).outputTexSize = { (float)info->width_, (float)info->height_ };
                        else
                            pack.Get<ViewData>(1).outputTexSize = { (float)view.viewport_[2], (float)view.viewport_[3] };
                    }
                    else
                        pack.Get<ViewData>(1).outputTexSize = { (float)view.viewport_[2], (float)view.viewport_[3] };

                    pack.Get<ViewData>(1).invInputTexSize = float2(1, 1) / pack.Get<ViewData>(1).inputTexSize;
                    pack.Get<ViewData>(1).invOutputTexSize = float2(1, 1) / pack.Get<ViewData>(1).outputTexSize;

                    // 128 bytes just isn't something to worry about, so copy the whole thing regardless how many are really used
                    memcpy(pack.Get<ParamData>(2).params, cmd.params_, sizeof(float) * RENDER_SCRIPT_MAX_PARAMS);

                    pack.Transfer(quadCmdUBO, false);

                    auto cmdBuffer = RS_StartCommandBuffer(device_);
                    renderer->GetCommandBufferChain().push_back(cmdBuffer);

                    BeginStage(device_, view, stage);
                    {
                        auto pass = cmd.effect_->GetPasses()[0];

                        auto geo = device_->GetFSTriGeometry();
                        geo->layout_->Bind(geo.get(), { });

                        RS_SetupViewport(device_, view);

                        // Set rasterization state.
                        VezRasterizationState rasterizationState = { };
                        rasterizationState.polygonMode = VK_POLYGON_MODE_FILL;
                        rasterizationState.cullMode = VK_CULL_MODE_NONE;
                        rasterizationState.frontFace = VK_FRONT_FACE_CLOCKWISE;
                        vezCmdSetRasterizationState(&rasterizationState);

                        // Set depth stencil state.
                        VezDepthStencilState depthStencilState = {};
                        depthStencilState.depthTestEnable = VK_FALSE;
                        depthStencilState.depthWriteEnable = VK_FALSE;
                        depthStencilState.depthCompareOp = VK_COMPARE_OP_ALWAYS;
                        depthStencilState.stencilTestEnable = VK_FALSE;
                        depthStencilState.depthBoundsTestEnable = VK_FALSE;
                        vezCmdSetDepthStencilState(&depthStencilState);

                        for (auto tex : cmd.textures_)
                        {
                            if (tex.texture_->IsTextureBuffer())
                                vezCmdBindBufferView(tex.texture_->GetTextureBufferView(), 1, tex.slot_, 0);
                            else
                                vezCmdBindImageView(tex.texture_->GetView(), device_->GetSampler(tex.sampling_.filter_, tex.sampling_.wrap_), 1, tex.slot_, 0);
                        }

                        VezInputAssemblyState state = { };
                        state.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
                        vezCmdSetInputAssemblyState(&state);

                        vezCmdBindPipeline(pass->GetPipeline());

                        auto vbo = geo->vertexBuffers_[0]->GetGPUObject();
                        VkDeviceSize junk = 0;
                        vezCmdBindVertexBuffers(0, 1, &vbo, &junk);
                        vezCmdBindIndexBuffer(device_->GetSequentialIndexBuffer()->GetGPUObject(), 0, VK_INDEX_TYPE_UINT16);

                        vezCmdBindBuffer(quadCmdUBO->GetGPUObject(), pack.OffsetOf(0), pack.SizeOf(0), 0, BINDING_QUAD_VIEW_MATRIX, 0);
                        vezCmdBindBuffer(quadCmdUBO->GetGPUObject(), pack.OffsetOf(1), pack.SizeOf(1), 0, BINDING_QUAD_SIZE_DATA, 0);
                        vezCmdBindBuffer(quadCmdUBO->GetGPUObject(), pack.OffsetOf(2), pack.SizeOf(2), 0, 5, 0);

                        vezCmdDrawIndexed(3, 1, 0, 0, 0);
                        device_->AddStat(STAT_BATCHES, 1);
                        device_->AddStat(STAT_PRIMITIVES, 1);
                    }
                    EndStage(device_, stage);

                    vezEndCommandBuffer();
                }
                else if (cmd.commandType_ == LightVolumes && lights.size())
                {
                    auto lightFX = device_->GetDeferredLightEffect();

                    BatchQueue lightQueue;
                    vector<float4x4> transforms;
                    transforms.resize(lights.size());

                    shared_ptr<Geometry> geometry;

                    auto transformBuffer = device_->GetScratchUniformBuffer(sizeof(float4x4));

                    auto cmdBuffer = RS_StartCommandBuffer(device_);
                    renderer->GetCommandBufferChain().push_back(cmdBuffer);

                    VezRasterizationState frontFaces = {};
                    frontFaces.polygonMode = VK_POLYGON_MODE_FILL;
                    frontFaces.cullMode = VK_CULL_MODE_BACK_BIT;
                    frontFaces.frontFace = VK_FRONT_FACE_CLOCKWISE;
                    frontFaces.depthBiasEnable = VK_FALSE;

                    VezRasterizationState backFaces = {};
                    backFaces.polygonMode = VK_POLYGON_MODE_FILL;
                    backFaces.cullMode = VK_CULL_MODE_FRONT_BIT;
                    backFaces.frontFace = VK_FRONT_FACE_CLOCKWISE;
                    backFaces.depthBiasEnable = VK_FALSE;

                    VezRasterizationState noCullFaces = {};
                    noCullFaces.polygonMode = VK_POLYGON_MODE_FILL;
                    noCullFaces.cullMode = VK_CULL_MODE_NONE;
                    noCullFaces.frontFace = VK_FRONT_FACE_CLOCKWISE;
                    noCullFaces.depthBiasEnable = VK_FALSE;
                    noCullFaces.depthClampEnable = VK_TRUE;

                    VezDepthStencilState depthStencil = {};
                    depthStencil.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
                    depthStencil.depthTestEnable = VK_TRUE;
                    depthStencil.depthWriteEnable = VK_FALSE;
                    depthStencil.stencilTestEnable = VK_FALSE;

                    shared_ptr<ShaderPass> lastPass = nullptr;
                    BeginStage(device_, view, stage);

                    RS_SetupViewport(device_, view);

					BufferPool lightBuffers;

                    vector<pair<void*, size_t> > lightAllocs;
					for (uint32_t i = 0; i < lights.size(); ++i)
					{
						auto light = lights[i];

                        LightData lightData = { };
						lightData.lightMat = light->GetShadowMatrix(0);
						lightData.lightPos = float4(lights[i]->GetPosition(), (int)light->GetKind());
						lightData.lightDir = float4(lights[i]->GetDirection(), light->GetRadius());
						lightData.color = light->GetColor();
						lightData.extraParams.x = light->GetFOV();
						lightData.extraParams.y = light->IsShadowCasting() ? 1.0f : 0.0f;
						lightData.shadowMapCoords[0] = light->IsShadowCasting() ? light->GetShadowDomain(0) : float4::zero;
						lightData.shadowMapCoords[1] = light->IsShadowCasting() ? light->GetShadowDomain(1) : float4::zero;

						auto alloc = lightBuffers.Allocate(sizeof(lightData));
						memcpy(alloc.first, &lightData, sizeof(lightData));
                        lightAllocs.push_back(alloc);
					}

                    auto lightDataBuffer = device_->GetScratchUniformBuffer(sizeof(LightData));
					lightBuffers.Transfer(lightDataBuffer);

					for (uint32_t i = 0; i < lights.size(); ++i)
					{
						auto light = lights[i];
                        if (light->GetKind() == Light::POINT)
                        {
                            auto pass = light->IsShadowCasting() ? lightFX->GetPass("POINT_LIGHT_SHDW", PRIM_UNKNOWN) : lightFX->GetPass("POINT_LIGHT", PRIM_UNKNOWN);
                            geometry = device_->GetPointLightGeometry();
                            
                            auto transform = float4x4::FromTRS(light->GetPosition(), Quat::identity, float3(light->GetRadius()));
                            
							auto dataT = lightAllocs[i];
							vezCmdBindBuffer(lightDataBuffer->GetGPUObject(), dataT.second, sizeof(LightData), 0, SHADER_BUFFER_LIGHT_DATA, 0);
                            vezCmdBindBuffer(viewUniformBuffer_->GetGPUObject(), 0, VK_WHOLE_SIZE, 0, SHADER_BUFFER_VIEW_DATA, 0);
                            if (light->GetShadowMapTexture())
                                vezCmdBindImageView(light->GetShadowMapTexture()->GetView(), device_->GetSampler(FILTER_SHADOW, false), 1, SHADER_TEX_SHADOWMAP, 0);
                            else
                                vezCmdBindImageView(device_->GetSystemTexture(PIPELINE_RESOURCE_SHADOWMAP)->GetView(), device_->GetSampler(FILTER_SHADOW, false), 1, SHADER_TEX_SHADOWMAP, 0);

                            ApplyPass(pass, view);
                            ApplyGeometry(pass, geometry.get(), false);
                            ApplyBlendMode(light->GetColor().w < 0 ? Blend_Subtract : Blend_Add, view, stage);
                            vezCmdPushConstants(0, sizeof(float4x4), &transform);
                            if (light->Contains(view.cameras_[0]))
                            {
                                vezCmdSetRasterizationState(&backFaces);
                                depthStencil.depthCompareOp = VK_COMPARE_OP_GREATER;
                            }
                            else
                            {
                                vezCmdSetRasterizationState(&frontFaces);
                                depthStencil.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
                            }

                            vezCmdSetDepthStencilState(&depthStencil);
                            vezCmdDrawIndexed(geometry->indexCount_, 1, 0, 0, 0);
                            device_->AddStat(STAT_BATCHES, 1);
                            device_->AddStat(STAT_PRIMITIVES, geometry->primCount_);
                            //EndStage(device, stage);
                        }
                        else if (light->GetKind() == Light::SPOT)
                        {
                            auto pass = light->IsShadowCasting() ? lightFX->GetPass("SPOT_LIGHT_SHDW", PRIM_UNKNOWN) : lightFX->GetPass("SPOT_LIGHT", PRIM_UNKNOWN);
                            
                            geometry = device_->GetSpotLightGeometry();

							auto dataT = lightAllocs[i];
                            vezCmdBindBuffer(lightDataBuffer->GetGPUObject(), dataT.second, sizeof(LightData), 0, SHADER_BUFFER_LIGHT_DATA, 0);
                            vezCmdBindBuffer(viewUniformBuffer_->GetGPUObject(), 0, VK_WHOLE_SIZE, 0, SHADER_BUFFER_VIEW_DATA, 0);

                            if (light->GetShadowMapTexture())
                                vezCmdBindImageView(light->GetShadowMapTexture()->GetView(), device_->GetSampler(FILTER_SHADOW, false), 1, SHADER_TEX_SHADOWMAP, 0);
                            else
                                vezCmdBindImageView(device_->GetSystemTexture(PIPELINE_RESOURCE_SHADOWMAP)->GetView(), device_->GetSampler(FILTER_SHADOW, false), 1, SHADER_TEX_SHADOWMAP, 0);

                            ApplyPass(pass, view);
                            ApplyGeometry(pass, geometry.get(), false);
                            ApplyBlendMode(light->GetColor().w < 0 ? Blend_Subtract : Blend_Add, view, stage);

                            float yScale = tanf(math::DegToRad(light->GetFOV()) * 0.5f) * light->GetRadius() * 0.53f;
                            float xScale = yScale;
                            auto transform = float4x4::FromTRS(light->GetPosition(), light->GetRotation(), float3(xScale, yScale, light->GetRadius()));
                            vezCmdPushConstants(0, sizeof(float4x4), &transform);

                            if (light->Contains(view.cameras_[0]))
                            {
                                vezCmdSetRasterizationState(&backFaces);
                                depthStencil.depthCompareOp = VK_COMPARE_OP_GREATER;
                            }
                            else
                            {
                                vezCmdSetRasterizationState(&frontFaces);
                                depthStencil.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
                            }

                            vezCmdSetDepthStencilState(&depthStencil);
                            vezCmdDrawIndexed(geometry->indexCount_, 1, 0, 0, 0);
                            device_->AddStat(STAT_BATCHES, 1);
                            device_->AddStat(STAT_PRIMITIVES, geometry->primCount_);
                        }
                        else if (light->GetKind() == Light::DIRECTIONAL)
                        {
                            auto pass = light->IsShadowCasting() ? lightFX->GetPass("DIRECTIONAL_LIGHT_SHDW", PRIM_UNKNOWN) : lightFX->GetPass("DIRECTIONAL_LIGHT", PRIM_UNKNOWN);
                            if (pass != lastPass)
                            {
                                ApplyPass(pass, view);
                                lastPass = pass;
                            }

                            geometry = device_->GetFSTriGeometry();
                            ApplyGeometry(pass, geometry.get(), false);

                            auto viewMatrix = float4x4::OpenGLOrthoProjLH(0, 1, 2, 2);

                            ViewBufferData dirLightOverride = viewBufferData_;
                            dirLightOverride.invViewProj[0] = viewMatrix;
                            dirLightOverride.viewProj[0] = viewMatrix;

                            auto viewDataBuffer = device_->GetScratchUniformBuffer(sizeof(ViewBufferData));
                            viewDataBuffer->SetData(&dirLightOverride, sizeof(ViewBufferData));

                            ApplyBlendMode(Blend_Add, view, stage);
							auto dataT = lightAllocs[i];
							vezCmdBindBuffer(lightDataBuffer->GetGPUObject(), dataT.second, sizeof(LightData), 0, SHADER_BUFFER_LIGHT_DATA, 0);
                            vezCmdBindBuffer(viewDataBuffer->GetGPUObject(), 0, VK_WHOLE_SIZE, 0, SHADER_BUFFER_VIEW_DATA, 0);

                            vezCmdSetRasterizationState(&noCullFaces);

                            if (light->GetShadowMapTexture())
                                vezCmdBindImageView(light->GetShadowMapTexture()->GetView(), device_->GetSampler(FILTER_SHADOW, false), 1, SHADER_TEX_SHADOWMAP, 0);
                            else
                                vezCmdBindImageView(device_->GetSystemTexture(PIPELINE_RESOURCE_SHADOWMAP)->GetView(), device_->GetSampler(FILTER_SHADOW, false), 1, SHADER_TEX_SHADOWMAP, 0);

                            auto transform = float4x4::identity;
                            vezCmdPushConstants(0, sizeof(float4x4), &transform);

                            VezDepthStencilState depthStencilState = {};
                            depthStencilState.depthTestEnable = VK_FALSE;
                            depthStencilState.depthWriteEnable = VK_FALSE;
                            depthStencilState.depthCompareOp = VK_COMPARE_OP_ALWAYS;
                            depthStencilState.stencilTestEnable = VK_FALSE;
                            depthStencilState.depthBoundsTestEnable = VK_FALSE;
                            vezCmdSetDepthStencilState(&depthStencilState);

                            vezCmdDraw(3, 1, 0, 0);
                            device_->AddStat(STAT_BATCHES, 1);
                            device_->AddStat(STAT_PRIMITIVES, 1);
                        }
                    }
                    EndStage(device_, stage);
                    vezEndCommandBuffer();
                }
                else if (cmd.commandType_ == DeferredTiledLights)
                {
                    // get deferred tiled pass
                    VkBuffer fsQuad = device_->GetFullscreenTriVertices()->GetGPUObject();
                    device_->GetLayout_PosUV()->Bind(nullptr, { });
                    vezCmdBindVertexBuffers(0, 1, &fsQuad, 0);
                    vezCmdDraw(3, 1, 0, 0);
                    device_->AddStat(STAT_BATCHES, 1);
                    device_->AddStat(STAT_PRIMITIVES, 1);
                }
                else if (cmd.commandType_ == ForwardTiledLights)
                {
                    ShaderPass* activePass = nullptr;
                    shared_ptr<Geometry> activeGeometry;
                    shared_ptr<Material> activeMaterial;

                    static uint32_t tiledLightingPassID = 0;
                    // render the geometries now that we've bound the tiled lights

                    shared_ptr<ShaderPass> lastPass = nullptr;
                    Geometry* lastGeometry = nullptr;
                    Material* lastMaterial = nullptr;
                    for (const auto& batch : batches)
                    {
                        if (auto pass = batch.material_->GetEffect()->GetPass(tiledLightingPassID, batch.geometry_->primType_))
                        {
                            if (pass != lastPass)
                            {
                                lastPass = pass;
                                vezCmdBindPipeline(lastPass->GetPipeline());
                            }

                            if (batch.material_ != lastMaterial)
                            {
                                lastMaterial = batch.material_;
                            }

                            if (batch.geometry_ != lastGeometry)
                            {
                                lastGeometry = batch.geometry_;

                                vector<VkBuffer> vbos;
                                for (auto b : lastGeometry->vertexBuffers_)
                                    vbos.push_back(b->GetGPUObject());

                                VkDeviceSize junk = 0;
                                vezCmdBindVertexBuffers(0, (uint32_t)vbos.size(), vbos.data(), &junk);
                                if (lastGeometry->indexBuffer_)
                                    vezCmdBindIndexBuffer(lastGeometry->indexBuffer_->GetGPUObject(), 0, lastGeometry->indexBuffer_->HasTag(BufferTag_32Bit) ? VK_INDEX_TYPE_UINT32 : VK_INDEX_TYPE_UINT16);
                            }

                            if (lastGeometry->indexBuffer_)
                            {
                                vezCmdDrawIndexed(lastGeometry->GetIndexCount(), batch.numTransforms_, lastGeometry->indexStart_, 0, 0);
                                device_->AddStat(STAT_BATCHES, 1);
                                device_->AddStat(STAT_PRIMITIVES, lastGeometry->primCount_ * batch.numTransforms_);
                            }
                            else
                            {
                                vezCmdDraw(lastGeometry->vertexCount_, batch.numTransforms_, 0, 0);
                                device_->AddStat(STAT_BATCHES, 1);
                                device_->AddStat(STAT_PRIMITIVES, lastGeometry->primCount_ * batch.numTransforms_);
                            }
                        }
                    }
                }
                else if (cmd.commandType_ == ForwardLights)
                {
                    for (uint32_t i = 0; i < lights.size(); ++i)
                    {
                        auto light = lights[i];
                        AABB dumbBnds;
                        dumbBnds.SetNegativeInfinity();
                        auto batches = view.scene_->GetBatches(light.get(), UINT_MAX, dumbBnds);
                        if (batches.empty())
                            continue;

                        LightData lightData;
                        lightData.lightMat = light->GetShadowMatrix(0);
                        lightData.lightPos = float4(lights[i]->GetPosition(), (int)light->GetKind());
                        lightData.lightDir = float4(lights[i]->GetDirection(), light->GetRadius());
                        lightData.color = light->GetColor();
                        lightData.extraParams.x = light->GetFOV();
                        lightData.extraParams.y = light->IsShadowCasting() ? 1.0f : 0.0f;
                        lightData.shadowMapCoords[0] = light->IsShadowCasting() ? light->GetShadowDomain(0) : float4::zero;
                        lightData.shadowMapCoords[1] = light->IsShadowCasting() ? light->GetShadowDomain(1) : float4::zero;

                        auto lightDataBuffer = device_->GetScratchUniformBuffer(sizeof(LightData));
                        lightDataBuffer->SetData(&lightData, sizeof(lightData));

                        BatchQueue lightQueue;
                        for (uint32_t i = 0; i < batches.size(); ++i)
                            lightQueue.Add(batches[i]);

                        DrawBatchesTaskData task;
                        task.uboBindings_.push_back({ SHADER_BUFFER_LIGHT_DATA, 0, UINT_MAX, lightDataBuffer });
                        task.clearTargets_ = false;
                        task.textureBindings_.push_back({ SHADER_TEX_SHADOWMAP, { FILTER_LINEAR, false }, light->GetShadowMapTexture() });
                        if (light->GetKind() == Light::POINT)
                        {
                            task.contextNameHash_ = Hash(string(cmd.context_.name_) + "_LITPOINT");
                            task.skinnedNameHash_ = Hash(string(cmd.context_.name_) + "_LITPOINT_SKINNED");
                            task.instancedNameHash_ = Hash(string(cmd.context_.name_) + "_LITPOINT_INST");
                        }
                        else if (light->GetKind() == Light::SPOT)
                        {
                            task.contextNameHash_ = Hash(string(cmd.context_.name_) + "_LITSPOT");
                            task.skinnedNameHash_ = Hash(string(cmd.context_.name_) + "_LITSPOT_SKINNED");
                            task.instancedNameHash_ = Hash(string(cmd.context_.name_) + "_LITSPOT_INST");
                        }
                        else if (light->GetKind() == Light::DIRECTIONAL)
                        {
                            task.contextNameHash_ = Hash(string(cmd.context_.name_) + "_LITDIR");
                            task.skinnedNameHash_ = Hash(string(cmd.context_.name_) + "_LITDIR_SKINNED");
                            task.instancedNameHash_ = Hash(string(cmd.context_.name_) + "_LITDIR_INST");
                        }
                        DrawBatches(renderer, view, stage, lightQueue, task);
                    }
                }
                else if (cmd.commandType_ == Blit)
                {
                    auto src = GetTargetTexture(cmd.cmdData_.blitData_.source_.nameHash_);
                    auto dest = GetTargetTexture(cmd.cmdData_.blitData_.dest_.nameHash_);
                    if (src && dest)
                    {
                        VezImageBlit blitInfo = { };
                        blitInfo.srcSubresource.mipLevel = 0;
                        blitInfo.srcSubresource.baseArrayLayer = 0;
                        blitInfo.srcSubresource.layerCount = 1;
                        blitInfo.srcOffsets[1].x = src->width_;
                        blitInfo.srcOffsets[1].y = src->height_;
                        blitInfo.srcOffsets[1].z = 1;

                        blitInfo.dstSubresource.mipLevel = 0;
                        blitInfo.dstSubresource.baseArrayLayer = 0;
                        blitInfo.dstSubresource.layerCount = 1;
                        blitInfo.dstOffsets[1].x = dest->width_;
                        blitInfo.dstOffsets[1].y = dest->height_;
                        blitInfo.dstOffsets[1].z = 1;

                        vezCmdBlitImage(src->texture_->GetImage(), dest->texture_->GetImage(), 1, &blitInfo, VK_FILTER_LINEAR);
                    }
                }
                else if (cmd.commandType_ == GenMips)
                {
                    if (auto tgt = GetTargetTexture(cmd.cmdData_.genMipsData_.texture_.nameHash_))
                    {
                        auto width = tgt->width_;
                        auto height = tgt->height_;

                        const uint32_t layerCt = cmd.cmdData_.genMipsData_.layer_ == -1 ? tgt->texture_->GetLayers() : 1;
                        const uint32_t layerBase = cmd.cmdData_.genMipsData_.layer_ == -1 ? 0 : cmd.cmdData_.genMipsData_.layer_;

                        for (auto level = 1; level < tgt->texture_->GetMips(); ++level)
                        {
                            VezImageBlit blitInfo = {};
                            blitInfo.srcSubresource.mipLevel = level - 1;
                            blitInfo.srcSubresource.baseArrayLayer = layerBase;
                            blitInfo.srcSubresource.layerCount = layerCt;
                            blitInfo.srcOffsets[1].x = tgt->width_ >> (level - 1);
                            blitInfo.srcOffsets[1].y = tgt->height_ >> (level - 1);
                            blitInfo.srcOffsets[1].z = 1;

                            blitInfo.dstSubresource.mipLevel = level;
                            blitInfo.dstSubresource.baseArrayLayer = layerBase;
                            blitInfo.dstSubresource.layerCount = layerCt;
                            blitInfo.dstOffsets[1].x = tgt->width_ >> level;
                            blitInfo.dstOffsets[1].y = tgt->height_ >> level;
                            blitInfo.dstOffsets[1].z = 1;

                            vezCmdBlitImage(tgt->texture_->GetImage(), tgt->texture_->GetImage(), 1, &blitInfo, VK_FILTER_LINEAR);
                        }
                    }
                }
                else if (cmd.commandType_ == BufferCopy)
                {
                    auto srcBuffer = FindBuffer(cmd.cmdData_.buffCopyData_.source_);
                    auto destBuffer = FindBuffer(cmd.cmdData_.buffCopyData_.dest_);

                    if (srcBuffer && destBuffer)
                    {
                        VezBufferCopy reg = { };
                        reg.size = srcBuffer->GetSize();
                        reg.dstOffset = 0;
                        reg.srcOffset = 0;
                        vezCmdCopyBuffer(srcBuffer->GetGPUObject(), destBuffer->GetGPUObject(), 0, &reg);
                    }
                }
                else if (cmd.commandType_ == ComputePass)
                {
                    if (auto pass = cmd.effect_->GetPass(cmd.context_.nameHash_, PRIM_UNKNOWN))
                    {
                        ApplyPass(pass, view);

                        for (auto& uboRecord : cmd.buffers_)
                        {
                            if (auto found = GetDataBuffer(uboRecord.second.nameHash_))
                                vezCmdBindBuffer(found->GetGPUObject(), 0, VK_WHOLE_SIZE, 0, uboRecord.first, 0);
                        }

                        for (auto& texRecord : cmd.textures_)
                            cmd.effect_->BindTexture(texRecord.texture_, texRecord.slot_, pass.get());

                        if (cmd.numParams_ > 0)
                        {
                            auto scratch = device_->GetScratchUniformBuffer(sizeof(float) * RENDER_SCRIPT_MAX_PARAMS);
                            scratch->SetData((void*)cmd.params_, sizeof(float) * RENDER_SCRIPT_MAX_PARAMS);
                            vezCmdBindBuffer(scratch->GetGPUObject(), 0, VK_WHOLE_SIZE, 0, 0, 0);
                        }

                        vezCmdDispatch(cmd.cmdData_.computeData_.groupsX_, cmd.cmdData_.computeData_.groupsY_, cmd.cmdData_.computeData_.groupsZ_);
                    }
                }
                else if (cmd.commandType_ == RenderCallback)
                {
                    device_->InvokeCallback(cmd.cmdData_.callData_.callID_.name_, this, &view, (float*)cmd.params_, cmd.numParams_);
                }
            }

        }
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
void RenderScript::DrawBatches(Renderer* renderer, View view, RenderScriptStage* stage, const BatchQueue& batchQueue, DrawBatchesTaskData& taskData)
{
    shared_ptr<ShaderPass> activePass = nullptr;
    Geometry* activeGeometry = nullptr;
    Material* activeMaterial = nullptr;

    static auto applyPass = [](const shared_ptr<ShaderPass> pass, View view) {

        vezCmdBindPipeline(pass->GetPipeline());

        VkViewport v = {};
        v.x = (float)view.viewport_[0];
        v.y = (float)view.viewport_[1];
        v.width =  view.viewport_.Width();
        v.height = view.viewport_.Height();
        v.minDepth = 0.0f;
        v.maxDepth = 1.0f;
        vezCmdSetViewport(0, 1, &v);

        if (pass->GetDrawState().stencilTest_)
        {
            //??
        }
    };

    static auto applyMaterial = [](GraphicsDevice* device, View view, RenderScriptStage* stage, shared_ptr<ShaderPass> pass, Material* mat, Geometry* geom, DrawBatchesTaskData& taskData) {
        for (auto& uboRecord : mat->GetUBOs())
        {
            bool ignore = false;
            for (auto& rec : taskData.uboBindings_)
            {
                if (rec.slot_ == uboRecord.slot_)
                {
                    ignore = true;
                    break;
                }
            }
            if (!ignore)
                vezCmdBindBuffer(uboRecord.buffer_->GetGPUObject(), uboRecord.startBytes_, uboRecord.size_ == UINT_MAX ? VK_WHOLE_SIZE : uboRecord.size_, 0, uboRecord.slot_, 0);
        }

        for (auto& rec : taskData.uboBindings_)
            vezCmdBindBuffer(rec.buffer_->GetGPUObject(), rec.startBytes_, rec.size_ == UINT_MAX ? VK_WHOLE_SIZE : rec.size_, 0, rec.slot_, 0);

        for (auto& tex : taskData.textureBindings_)
        {
            auto traits = mat->GetEffect()->GetSamplerTraits(tex.slot_);
            if (tex.texture_->IsTextureBuffer())
                vezCmdBindBufferView(tex.texture_->GetTextureBufferView(), 1, tex.slot_, 0);
            else
                vezCmdBindImageView(tex.texture_->GetView(), device->GetSampler(traits.filter_, traits.wrap_), 1, tex.slot_, 0);
        }

        for (auto& texRecord : mat->GetTextures())
        {
            if (texRecord.texture_)
            {
                auto traits = mat->GetEffect()->GetSamplerTraits(texRecord.slot_);
                if (texRecord.texture_->IsTextureBuffer())
                    vezCmdBindBufferView(texRecord.texture_->GetTextureBufferView(), 1, texRecord.slot_, 0);
                else
                    vezCmdBindImageView(texRecord.texture_->GetView(), device->GetSampler(traits.filter_, traits.wrap_), 1, texRecord.slot_, 0);
            }
        }

        static VkPolygonMode polyModes[] = {
            VK_POLYGON_MODE_FILL,
            VK_POLYGON_MODE_POINT,
            VK_POLYGON_MODE_LINE
        };

        static VkCullModeFlags cullModes[] = { 
            VK_CULL_MODE_NONE,
            VK_CULL_MODE_FRONT_BIT,
            VK_CULL_MODE_BACK_BIT
        };

        VezRasterizationState raster = {};
        raster.polygonMode = polyModes[0];
        raster.cullMode = cullModes[pass->GetDrawState().culling_];
        raster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
        raster.depthBiasEnable = pass->GetDrawState().depthBias_ != 0.0f || pass->GetDrawState().slopeBias_ != 0.0f;
        vezCmdSetRasterizationState(&raster);

        if (raster.depthBiasEnable)
            vezCmdSetDepthBias(pass->GetDrawState().depthBias_, 0.0f, pass->GetDrawState().slopeBias_);

        static const VkCompareOp compareTable[] = {
            VK_COMPARE_OP_EQUAL,
            VK_COMPARE_OP_LESS_OR_EQUAL,
            VK_COMPARE_OP_GREATER_OR_EQUAL,
            VK_COMPARE_OP_LESS,
            VK_COMPARE_OP_GREATER,
            VK_COMPARE_OP_NOT_EQUAL,
            VK_COMPARE_OP_ALWAYS,
            VK_COMPARE_OP_NEVER
        };
        VezDepthStencilState depthStencil = {};
        depthStencil.depthCompareOp = compareTable[pass->GetDrawState().depthCompare_];
        depthStencil.depthTestEnable = pass->GetDrawState().depthTest_ ? VK_TRUE : VK_FALSE;
        depthStencil.depthWriteEnable = pass->GetDrawState().depthWrite_ ? VK_TRUE : VK_FALSE;
        depthStencil.stencilTestEnable = pass->GetDrawState().stencilTest_ ? VK_TRUE : VK_FALSE;
        if (pass->GetDrawState().stencilMask_)
        {
            depthStencil.front.passOp = VK_STENCIL_OP_REPLACE;
            depthStencil.back.passOp = VK_STENCIL_OP_REPLACE;
            vezCmdSetStencilReference(VkStencilFaceFlagBits::VK_STENCIL_FRONT_AND_BACK, pass->GetDrawState().stencilMask_);
        }
        vezCmdSetDepthStencilState(&depthStencil);

        VkColorComponentFlags vkColorCompAll = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        static VezColorBlendAttachmentState blendStates[] = {
            // Blend none
            { false, VK_BLEND_FACTOR_SRC_ALPHA, VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA, VK_BLEND_OP_ADD, VK_BLEND_FACTOR_SRC_ALPHA, VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA, VK_BLEND_OP_ADD, vkColorCompAll },
            // Alpha
            { true, VK_BLEND_FACTOR_SRC_ALPHA, VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA, VK_BLEND_OP_ADD, VK_BLEND_FACTOR_SRC_ALPHA, VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA, VK_BLEND_OP_ADD, vkColorCompAll },
            // Add
            { true, VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ONE, VK_BLEND_OP_ADD, VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ONE, VK_BLEND_OP_ADD, vkColorCompAll },
            // Subtract
            { true, VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ONE, VK_BLEND_OP_REVERSE_SUBTRACT, VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ONE, VK_BLEND_OP_REVERSE_SUBTRACT, vkColorCompAll },
            // Multiply
            { true, VK_BLEND_FACTOR_DST_COLOR, VK_BLEND_FACTOR_ZERO, VK_BLEND_OP_ADD, VK_BLEND_FACTOR_DST_COLOR, VK_BLEND_FACTOR_ZERO, VK_BLEND_OP_ADD, vkColorCompAll },
            // Premultiplied
            { true, VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA, VK_BLEND_OP_ADD, VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA, VK_BLEND_OP_ADD, vkColorCompAll },
        };

        VezColorBlendState blendState = { };
        vector<VezColorBlendAttachmentState> attachStates;
        auto fbo = stage && stage->targetConfig_.fbo_ ? stage->targetConfig_.fbo_ : view.renderTarget_;
        for (uint32_t i = 0; i < fbo->GetTextureCount(); ++i)
        {
            auto tgt = fbo->GetTexture(i);
            if (tgt->GetFormat() != TEX_DEPTH )
                attachStates.push_back(blendStates[pass->GetDrawState().blendMode_]);
        }
        
        blendState.attachmentCount = (uint32_t)attachStates.size();
        blendState.pAttachments = attachStates.data();
        blendState.logicOpEnable = false;
        vezCmdSetColorBlendState(&blendState);
    };

    if (taskData.cmdBuffer_ == 0)
    {
        auto cmdBuffer = RS_StartCommandBuffer(device_);
        if (taskData.placeCmdBufferAt_ != -1)
            renderer->GetCommandBufferChain()[taskData.placeCmdBufferAt_] = cmdBuffer;
        else
            renderer->GetCommandBufferChain().push_back(cmdBuffer);
        taskData.cmdBuffer_ = cmdBuffer;
    }
    else
        vezBeginCommandBuffer(taskData.cmdBuffer_, VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);

    RS_SetupViewport(device_, view);

    if (stage != nullptr)
        BeginStage(device_, view, stage);
    else
    {
        VezRenderPassBeginInfo info = { };
        info.attachmentCount = view.renderTarget_->GetTextureCount();
        info.framebuffer = view.renderTarget_->GetGPUObject();
        vector<VezAttachmentInfo> attach;
        for (unsigned i = 0; i < view.renderTarget_->GetTextureCount(); ++i)
        {
            VezAttachmentInfo at = { };
            at.loadOp = taskData.clearTargets_ ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD;
            at.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
            at.clearValue.color = { 0, 0, 0, 1.0f };
            at.clearValue.depthStencil.depth = 1.0f;
            at.clearValue.depthStencil.stencil = 0;
            attach.push_back(at);
        }
        info.pAttachments = attach.data();
        vezCmdBeginRenderPass(&info);
    }

    for (const auto& grp : batchQueue.groups_)
    {
        assert(grp.second.geometry_ && grp.second.material_);
        if (grp.second.material_ == nullptr || grp.second.geometry_ == nullptr)
            continue;

        if (auto pass = grp.second.material_->GetEffect()->GetPass(taskData.instancedNameHash_, grp.second.geometry_->primType_))
        {
            if (pass != activePass)
            {
                applyPass(pass, view);
                activePass = pass;
            }
            if (activeMaterial != grp.second.material_ || activeGeometry != grp.second.geometry_)
            {
                applyMaterial(device_, view, stage, pass, grp.second.material_, grp.second.geometry_, taskData);
                activeMaterial = grp.second.material_;
            }
            if (activeGeometry != grp.second.geometry_)
            {
                ApplyGeometry(pass, grp.second.geometry_, true);
                activeGeometry = grp.second.geometry_;
            }

            uint32_t drawCt = (uint32_t)grp.second.transforms_.size();

            for (size_t i = 0; i < grp.second.instanceTransformBuffers_.size(); ++i)
            {
                //uint32_t drawCt = grp.transforms_.size();
                auto instBuff = grp.second.instanceTransformBuffers_[i].second;
                uint32_t drawCt = grp.second.instanceTransformBuffers_[i].first;
                //ApplyGeometry(pass, activeGeometry, true, { grp.second.instanceTransformBuffers_[i].second });
                VkDeviceSize inOfs = 0;
                VkBuffer buff = grp.second.instanceTransformBuffers_[i].second->GetGPUObject();
                vezCmdBindVertexBuffers(activeGeometry->vertexBuffers_.size(), 1, &buff, &inOfs);

                if (activeGeometry->indexBuffer_)
                {
                    vezCmdDrawIndexed(activeGeometry->indexCount_, drawCt, activeGeometry->indexStart_, 0, 0);
                    device_->AddStat(STAT_BATCHES, 1);
                    device_->AddStat(STAT_PRIMITIVES, activeGeometry->primCount_ * drawCt);
                    device_->AddStat(STAT_INSTANCES, drawCt);
                }
                else
                {
                    vezCmdDraw(activeGeometry->vertexCount_, drawCt, 0, 0);
                    device_->AddStat(STAT_BATCHES, 1);
                    device_->AddStat(STAT_PRIMITIVES, activeGeometry->primCount_ * drawCt);
                    device_->AddStat(STAT_INSTANCES, drawCt);
                }
            }
        }
    }

    // batches that aren't instanced
    for (const auto& batch : batchQueue.batches_)
    {
        assert(batch.geometry_ && batch.material_);
        if (batch.geometry_ == nullptr || batch.material_ == nullptr)
            continue;

        if (auto pass = batch.material_->GetEffect()->GetPass(taskData.contextNameHash_, batch.geometry_->primType_))
        {
            if (pass != activePass)
            {
                applyPass(pass, view);
                activePass = pass;
            }

            if (activeMaterial != batch.material_ || activeGeometry != batch.geometry_)
            {
                activeMaterial = batch.material_;
                applyMaterial(device_, view, stage, activePass, activeMaterial, batch.geometry_, taskData);
            }

            if (batch.geometry_ != activeGeometry)
            {
                activeGeometry = batch.geometry_;
                ApplyGeometry(activePass, activeGeometry, false);
            }

            if (batch.isSkinned_)
            {
                vezCmdBindBuffer(batch.bonesBuffer_->GetGPUObject(), 0, VK_WHOLE_SIZE, 0, 1, 0);
                vezCmdPushConstants(0, sizeof(float4x4), batch.transforms_); // we still have this one guy
                if (activeGeometry->indexBuffer_ != nullptr)
                {
                    vezCmdDrawIndexed(activeGeometry->indexCount_, 1, activeGeometry->indexStart_, 0, 0);
                    device_->AddStat(STAT_BATCHES, 1);
                    device_->AddStat(STAT_PRIMITIVES, activeGeometry->primCount_);
                }
                else
                {
                    vezCmdDraw(activeGeometry->vertexCount_, 1, 0, 0);
                    device_->AddStat(STAT_BATCHES, 1);
                    device_->AddStat(STAT_PRIMITIVES, activeGeometry->primCount_);
                }
            }
            else
            {
                // we'll iterate, fake instancing - though this is *effectively* what instancing does
                for (uint32_t i = 0; i < batch.numTransforms_; ++i)
                {
                    vezCmdPushConstants(0, sizeof(float4x4), batch.transforms_ + sizeof(float4x4) * i);
                    if (activeGeometry->indexBuffer_ != nullptr)
                    {
                        vezCmdDrawIndexed(activeGeometry->indexCount_, 1, activeGeometry->indexStart_, 0, 0);
                        device_->AddStat(STAT_BATCHES, 1);
                        device_->AddStat(STAT_PRIMITIVES, activeGeometry->primCount_);
                    }
                    else
                    {
                        vezCmdDraw(activeGeometry->vertexCount_, 1, 0, 0);
                        device_->AddStat(STAT_BATCHES, 1);
                        device_->AddStat(STAT_PRIMITIVES, activeGeometry->primCount_);
                    }
                }
            }
        }
    }

    if (stage != nullptr)
        EndStage(device_, stage);
    else
        vezCmdEndRenderPass();

    vezEndCommandBuffer();
}

//****************************************************************************
//
//  Function:   RenderScript::ApplyPass
//
//  Purpose:    Not much for vulkan to do.
//
//****************************************************************************
void RenderScript::ApplyPass(const shared_ptr<ShaderPass> pass, View view)
{
    vezCmdBindPipeline(pass->GetPipeline());

    VkViewport v = {};
    v.x = (float)view.viewport_[0];
    v.y = (float)view.viewport_[1];
    v.width =  view.viewport_.Width();
    v.height = view.viewport_.Height();
    v.minDepth = 0.0f;
    v.maxDepth = 1.0f;
    vezCmdSetViewport(0, 1, &v);

    VkRect2D r;
    r.offset.x = (int)v.x;
    r.offset.y = (int)v.y;
    r.extent.width =  (uint32_t)v.width;
    r.extent.height = (uint32_t)v.height;
    vezCmdSetScissor(0, 1, &r);
}

//****************************************************************************
//
//  Function:   RenderScript::ApplyBlendMode
//
//  Purpose:    Utility for reliably setting blend-modes, previously this was done poorly.
//              Unused parameters are Vulkan specific.
//
//****************************************************************************
void RenderScript::ApplyBlendMode(BlendMode mode, View view, RenderScriptStage* stage)
{
    VkColorComponentFlags vkColorCompAll = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    static VezColorBlendAttachmentState blendStates[] = {
        // Blend none
        { VK_FALSE, VK_BLEND_FACTOR_SRC_ALPHA, VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA, VK_BLEND_OP_ADD, VK_BLEND_FACTOR_SRC_ALPHA, VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA, VK_BLEND_OP_ADD, vkColorCompAll },
        // Alpha
        { VK_TRUE, VK_BLEND_FACTOR_SRC_ALPHA, VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA, VK_BLEND_OP_ADD, VK_BLEND_FACTOR_SRC_ALPHA, VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA, VK_BLEND_OP_ADD, vkColorCompAll },
        // Add
        { VK_TRUE, VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ONE, VK_BLEND_OP_ADD, VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ONE, VK_BLEND_OP_ADD, vkColorCompAll },
        // Subtract
        { VK_TRUE, VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ONE, VK_BLEND_OP_REVERSE_SUBTRACT, VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ONE, VK_BLEND_OP_REVERSE_SUBTRACT, vkColorCompAll },
        // Multiply
        { VK_TRUE, VK_BLEND_FACTOR_DST_COLOR, VK_BLEND_FACTOR_ZERO, VK_BLEND_OP_ADD, VK_BLEND_FACTOR_DST_COLOR, VK_BLEND_FACTOR_ZERO, VK_BLEND_OP_ADD, vkColorCompAll },
        // Premultiplied
        { VK_TRUE, VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA, VK_BLEND_OP_ADD, VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA, VK_BLEND_OP_ADD, vkColorCompAll },
    };

    VezColorBlendState blendState = { };
    vector<VezColorBlendAttachmentState> attachStates;
    auto fbo = stage && stage->targetConfig_.fbo_ ? stage->targetConfig_.fbo_ : view.renderTarget_;
    for (uint32_t i = 0; i < fbo->GetTextureCount(); ++i)
    {
        auto tgt = fbo->GetTexture(i);
        if (tgt->GetFormat() != TEX_DEPTH)
            attachStates.push_back(blendStates[mode]);
    }

    blendState.attachmentCount = (uint32_t)attachStates.size();
    blendState.pAttachments = attachStates.data();
    blendState.logicOpEnable = false;
    vezCmdSetColorBlendState(&blendState);
}

//****************************************************************************
//
//  Function:   RenderScript::ApplyGeometry
//
//  Purpose:    Does the repeated task of setting of the primtive assembly and bindings buffers
//              Watch out for the bit with instancing, that's about the automatic instancing.
//
//****************************************************************************
void RenderScript::ApplyGeometry(shared_ptr<ShaderPass> pass, Geometry* geometry, bool isInstanced, const std::vector<std::shared_ptr<Buffer>>& extraBuffers, bool laterInstanced, bool isVR)
{
    static VkPrimitiveTopology topoModes[] = {
            VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
            VK_PRIMITIVE_TOPOLOGY_POINT_LIST,
            VK_PRIMITIVE_TOPOLOGY_LINE_LIST
    };
    VezInputAssemblyState inputAssemblyState = {};
    inputAssemblyState.topology = pass->IsTessellating() ? VK_PRIMITIVE_TOPOLOGY_PATCH_LIST : topoModes[geometry->primType_];
    inputAssemblyState.primitiveRestartEnable = false;
    vezCmdSetInputAssemblyState(&inputAssemblyState);

    auto layout = !isInstanced ? geometry->layout_ : geometry->layout_->GetInstancedVariant(isVR);

    layout->Bind(geometry, { });

    size_t numBuffers = geometry->vertexBuffers_.size();
    vector<VkDeviceSize> vtxOffsets;
    vector<VkBuffer> buffers;
    buffers.reserve(numBuffers);
    vtxOffsets.reserve(numBuffers);
    for (const auto& buf : geometry->vertexBuffers_)
    {
        buffers.push_back(buf->GetGPUObject());
        vtxOffsets.push_back(0);
    }

    for (const auto& extraBuf : extraBuffers)
    {
        buffers.push_back(extraBuf->GetGPUObject());
        vtxOffsets.push_back(0);
    }

    vezCmdBindVertexBuffers(0, (uint32_t)buffers.size(), buffers.data(), vtxOffsets.data());
    if (geometry->indexBuffer_ != nullptr)
        vezCmdBindIndexBuffer(geometry->indexBuffer_->GetGPUObject(), geometry->indexStart_, geometry->indexBuffer_->HasTag(BufferTag_32Bit) ? VK_INDEX_TYPE_UINT32 : VK_INDEX_TYPE_UINT16);
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
    auto pass = effect->GetPasses()[0];
    ApplyPass(pass, view);

    auto geo = device_->GetFSTriGeometry();
    ApplyGeometry(pass, geo.get(), false, { });

    // Set rasterization state.
    VezRasterizationState rasterizationState = { };
    rasterizationState.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizationState.cullMode = VK_CULL_MODE_NONE;
    rasterizationState.frontFace = VK_FRONT_FACE_CLOCKWISE;
    vezCmdSetRasterizationState(&rasterizationState);

    // Set depth stencil state which is special for a fullscreen pass
    VezDepthStencilState depthStencilState = {};
    depthStencilState.depthTestEnable = VK_FALSE;
    depthStencilState.depthWriteEnable = VK_FALSE;
    depthStencilState.depthCompareOp = VK_COMPARE_OP_ALWAYS;
    depthStencilState.stencilTestEnable = VK_FALSE;
    depthStencilState.depthBoundsTestEnable = VK_FALSE;
    vezCmdSetDepthStencilState(&depthStencilState);

    auto vbo = geo->vertexBuffers_[0]->GetGPUObject();
    VkDeviceSize junk = 0;
    vezCmdBindVertexBuffers(0, 1, &vbo, &junk);
    vezCmdBindIndexBuffer(device_->GetSequentialIndexBuffer()->GetGPUObject(), 0, VK_INDEX_TYPE_UINT16);
    vezCmdDraw(3, 1, 0, 0);
    device_->AddStat(STAT_BATCHES, 1);
    device_->AddStat(STAT_PRIMITIVES, 1);
}

//****************************************************************************
//
//  Function:   Renderer::Draw2DBatches
//
//  Purpose:    Performs generic 2D rendering, intended for use with naive
//              GUIs like dear-imgui.
//
//****************************************************************************
void Renderer::Draw2DBatches(const std::vector<Draw2D>& calls, View forView, RenderScript* script)
{
    if (forView.renderTarget_ == nullptr)
        forView.renderTarget_ = device_->GetBackbuffer();

    if (calls.size() > 0)
    {
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

        float scale[2];
        scale[0] = 2.0f / (R - L);
        scale[1] = 2.0f / (B - T);
        float translate[2];
        translate[0] = -1.0f - sz.x * scale[0];
        translate[1] = -1.0f - sz.y * scale[1];

        auto mat = float4x4(
            translate[0], translate[1], 0,0,
            scale[0], scale[1], 0,0,
            0,0,0,0,
            0,0,0,0
        );
        //mat.Transpose();
        uiUBO_->SetData(&mat, sizeof(float4x4));

        Vertex2D* dataStart = &((*calls[0].vertices_)[0]);
        uiGeometry_->vertexBuffers_[0]->SetData(dataStart, calls[0].vertices_->size() * sizeof(Vertex2D));
        uiGeometry_->InferValuesFromData();

        auto cmdBuffer = device_->GetGraphicsCmdBuffer();
        GetCommandBufferChain().push_back(cmdBuffer);

        vezBeginCommandBuffer(cmdBuffer, VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
        RS_SetupViewport(device_, forView);

        VezRenderPassBeginInfo pass = { };
        pass.framebuffer = forView.renderTarget_->GetGPUObject();
        std::vector<VezAttachmentInfo> attachments;
        for (uint32_t i = 0; i < forView.renderTarget_->GetTextureCount(); ++i)
        {
            VezAttachmentInfo attach = {};
            attach.loadOp = VkAttachmentLoadOp::VK_ATTACHMENT_LOAD_OP_LOAD;
            attach.storeOp = VkAttachmentStoreOp::VK_ATTACHMENT_STORE_OP_STORE;
            attachments.push_back(attach);
        }
        pass.pAttachments = attachments.data();
        pass.attachmentCount = attachments.size();

        vezCmdBeginRenderPass(&pass);

        vezCmdBindBuffer(uiUBO_->GetGPUObject(), 0, VK_WHOLE_SIZE, 0, 0, 0);

        auto fxUI = device_->GetGUIEffect();
        script->ApplyPass(fxUI->GetPasses()[0], forView);

        int4 vpt = calls[0].viewport_;
        VkViewport v;
        v.x = vpt.x;
        v.y = vpt.y;
        v.width = vpt.Width();
        v.height = vpt.Height();
        v.minDepth = 0.0f;
        v.maxDepth = 1.0f;
        vezCmdSetViewport(0, 1, &v);

        int4 clip = calls[0].clipRect_;
        VkRect2D scissor = { calls[0].clipRect_.x, calls[0].clipRect_.y, calls[0].clipRect_.Width(), calls[0].clipRect_.Height() };
        vezCmdSetScissor(0, 1, &scissor);

        for (uint32_t i = 0; i < calls.size(); ++i)
        {
            const auto& call = calls[i];

            if (memcmp(&calls[i].viewport_, &vpt, sizeof(int4)) != 0)
            {
                vpt = calls[i].viewport_;
                VkViewport v;
                v.x = vpt.x;
                v.y = vpt.y;
                v.width = vpt.Width();
                v.height = vpt.Height();
                v.minDepth = 0.0f;
                v.maxDepth = 1.0f;
                vezCmdSetViewport(0, 1, &v);
            }

            if (memcmp(&calls[i].clipRect_, &clip, sizeof(int4)) != 0)
            {
                clip = calls[i].clipRect_;
                VkRect2D scissor = { call.clipRect_.x, call.clipRect_.y, call.clipRect_.Width(), call.clipRect_.Height() };
                vezCmdSetScissor(0, 1, &scissor);
            }

            script->ApplyBlendMode(call.blendMode_, forView, nullptr);
            script->ApplyGeometry(fxUI->GetPasses()[0], uiGeometry_.get(), false);

            if (call.texture_)
                vezCmdBindImageView(call.texture_->GetView(), device_->GetSampler(FILTER_POINT, true), 1, 0, 0);

            vezCmdDraw(call.vertexCount_, 1, call.vertexStart_, 0);
            device_->AddStat(STAT_BATCHES, 1);
            device_->AddStat(STAT_PRIMITIVES, call.vertexCount_ / 3);
        }

        vezCmdEndRenderPass();
        vezEndCommandBuffer();

        VkQueue graphicsQueue = 0;
        vezGetDeviceGraphicsQueue(device_->GetVKDevice(), 0, &graphicsQueue);

        array<VkSemaphore, 3> semaphores;
        VezSubmitInfo submission = {};
        submission.commandBufferCount = 1;
        submission.pCommandBuffers = &cmdBuffer;
        submission.signalSemaphoreCount = 1;
        submission.pSignalSemaphores = &semaphores[0];

        VkResult result = vezQueueSubmit(graphicsQueue, 1, &submission, nullptr);
        if (result != VK_SUCCESS)
            LOG_VULKAN("Failed to submit to queue: %u", result);
    }
}

//****************************************************************************
//
//  Function:   Renderer::ResetCommandBufferChain
//
//  Purpose:    Wipes our local list of buffers to execute, GraphicsDevice
//              is responsible for the actual pool management.
//
//****************************************************************************
void Renderer::ResetCommandBufferChain()
{
    commandBufferChain_.clear();
}

//****************************************************************************
//
//  Function:   Renderer::GetCommandBuffer
//
//  Purpose:    Just wraps the call the GraphicsDevice. 
//              
//  TODO:       Consider removal? When we have renderer we have the device too?
//              Will there be no special management concerns at this point?
//
//****************************************************************************
VkCommandBuffer Renderer::GetCommandBuffer()
{
    auto buffer = device_->GetGraphicsCmdBuffer();
    return buffer;
}

//****************************************************************************
//
//  Function:   Renderer::FinishRendering
//
//  Purpose:    Submits our constructed VkCommandBuffer lists.
//
//  TODO:       Incremental submissions? Submit-after-N? Avoid waiting.
//              Or do resource uploads/set-data have it covered there?
//
//  This function needs changes when GraphicsDeviceHead is implemented.
//  Right now we're assuming a singular swap-chain, in the future
//  there will be a swapchain per-head, this will make semaphore management
//  *interesting*.
//
//****************************************************************************
void Renderer::FinishRendering()
{
    VkQueue graphicsQueue = 0;
    vezGetDeviceGraphicsQueue(device_->GetVKDevice(), 0, &graphicsQueue);

    array<VkSemaphore, 3> semaphores;
    VezSubmitInfo submission = {};
    submission.commandBufferCount = (uint32_t)commandBufferChain_.size();
    submission.pCommandBuffers = commandBufferChain_.data();
    submission.signalSemaphoreCount = 1;
    submission.pSignalSemaphores = &semaphores[0];

    VkResult result = vezQueueSubmit(graphicsQueue, 1, &submission, nullptr);
    if (result != VK_SUCCESS)
        LOG_VULKAN("Failed to submit command-buffers", result);
    
    //VkPipelineStageFlags waitDstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    //auto swapchain = device_->GetSwapchain();
    //auto srcImage = device_->GetBackbuffer()->GetTexture(0)->GetImage();
    //
    //VezPresentInfo presentInfo = {};
    //presentInfo.waitSemaphoreCount = 1;
    //presentInfo.pWaitSemaphores = &semaphores[0];
    //presentInfo.pWaitDstStageMask = &waitDstStageMask;
    //presentInfo.swapchainCount = 1;
    //presentInfo.pSwapchains = &swapchain;
    //presentInfo.pImages = &srcImage;
    //result = vezQueuePresent(graphicsQueue, &presentInfo);
    //if (result != VK_SUCCESS)
    //    LOG_VULKAN("Failed to present swap-chain", result);
        

    for (auto& v : views_)
        v.head_->FinishRendering();

    ResetCommandBufferChain();
}

}