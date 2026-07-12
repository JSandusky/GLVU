//****************************************************************************
//
//  File:       RenderScript.cpp
//  License:    MIT
//  Project:    GLVU
//  Contents:   API independent portions of the execution of a render-script
//              on a view.
//
//  Notes:      BatchGroups/Batches and the like might perform faster with an allocator
//              and storing pointers in the BatchQueue entirely instead of hard-objects.
//              Something like `device->GetBatches(&myBatchList, 3);`
//
//****************************************************************************

#include "RenderScript.h"

#include "Batching.h"
#include "Buffer.h"
#include "Material.h"
#include "GraphicsDevice.h"
#include "Effect.h"
#include "Renderables.h"
#include "Packing.h"

#include <algorithm>
#include <numeric>

#define MAX_INSTANCES 65000

using namespace std;
using namespace math;

namespace GLVU
{

//****************************************************************************
//
//  Function:   RenderScriptStage::IsEffectApplicable
//
//  Purpose:    Was used at one point for attempting to see if an effect even had a meaning.  
//
//  DEPRECATED
//
//****************************************************************************
bool RenderScriptStage::IsEffectApplicable(const shared_ptr<Effect>& effect) const
{
    bool anyApplied = false;
    for (const auto& cmd : commands_)
    {
            
    }

    return anyApplied;
}

//****************************************************************************
//
//  Function:   RenderScript::RenderScript
//
//  Purpose:    Construct, sets up common UBOs and sets width/height to the
//              *sane* value of zero.
//
//****************************************************************************
RenderScript::RenderScript(GraphicsDevice* device) : 
    GPUObject(device),
    width_(0),
    height_(0)
{
    frameUniformBuffer_ = device->CreateUniformBuffer();
    frameUniformBuffer_->SetTag(BufferTag_Dynamic);
    frameUniformBuffer_->SetSize(sizeof(PerFrameData));

    lightUniformBuffer_ = device->CreateUniformBuffer();
    lightUniformBuffer_->SetTag(BufferTag_Dynamic);
    lightUniformBuffer_->SetSize(sizeof(LightData) * 200);

    viewUniformBuffer_ = device->CreateUniformBuffer();
    viewUniformBuffer_->SetTag(BufferTag_Dynamic);
    viewUniformBuffer_->SetSize(sizeof(ViewBufferData));
}

//****************************************************************************
//
//  Function:   RenderScript::~RenderScript
//
//  Purpose:    Destruct, releases resources.
//
//****************************************************************************
RenderScript::~RenderScript()
{
    Release();
}

//****************************************************************************
//
//  Function:   RenderScript::IsValid
//
//  Purpose:    Low-tech safety check.
//
//  Return:     True if this render-script could potentially do work.
//
//****************************************************************************
bool RenderScript::IsValid() const
{
    return stages_.size() > 0;
}

//****************************************************************************
//
//  Function:   RenderScript::Release
//
//  Purpose:    Frees raw-ptrs (there's alot here), and releases other resources.
//
//****************************************************************************
void RenderScript::Release()
{
    for (auto stage : stages_)
        delete stage;
    stages_.clear();

    for (auto tex : targetTextures_)
        delete tex;
    targetTextures_.clear();

    for (auto buf : dataBuffers_)
        delete buf;
    dataBuffers_.clear();

    forwardLitCmds_.clear();
}

//****************************************************************************
//
//  Function:   RenderScript::Clone
//
//  Purpose:    Deep-copy of this script.
//
//****************************************************************************
std::shared_ptr<RenderScript> RenderScript::Clone() const
{
    shared_ptr<RenderScript> ret(new RenderScript(device_));

    ret->width_ = width_;
    ret->height_ = height_;
    
    for (const auto& target : targetTextures_)
    {
        RenderTargetInfo* cpy = new RenderTargetInfo();
        cpy->name_ = target->name_;
        cpy->backbufferWidthFraction_ = target->backbufferWidthFraction_;
        cpy->backbufferHeightFraction_ = target->backbufferHeightFraction_;
        cpy->fixedWidth_ = target->fixedWidth_;
        cpy->fixedHeight_ = target->fixedHeight_;
        cpy->targetFormat_ = target->targetFormat_;
        cpy->pingPong_ = target->pingPong_;

        ret->targetTextures_.push_back(cpy);
    }

    for (const auto& buff : dataBuffers_)
    {
        RenderDataBufferInfo* cpy = new RenderDataBufferInfo();
        cpy->name_ = buff->name_;
        cpy->size_ = buff->size_;

        ret->dataBuffers_.push_back(cpy);
    }

    for (const auto& stage : stages_)
    {
        RenderScriptStage* cpy = new RenderScriptStage();
        cpy->self_ = stage->self_;
        cpy->active_ = stage->active_;
        cpy->requireActiveStages_ = stage->requireActiveStages_;
        cpy->ignoreActiveStages_ = stage->ignoreActiveStages_;
        cpy->targets_ = stage->targets_;
        cpy->targetBindings_ = stage->targetBindings_;
        
        for (const auto& cmd : stage->commands_)
        {
            RS_DrawCmd cmdCpy = cmd;
#if defined(GLVU_VK)
            cmdCpy.bakedBuffer_ = 0;
#endif
            cpy->commands_.push_back(cmdCpy);
        }

        ret->stages_.push_back(cpy);
    }

    return ret;
}

//****************************************************************************
//
//  Function:   RenderScript::OnFrameStart
//
//  Purpose:    ??, most of what this did was moved
//
//****************************************************************************
void RenderScript::OnFrameStart()
{

}

//****************************************************************************
//
//  Function:   RenderScript::OnFrameEnd
//
//  Purpose:    ??, most of what this used to do was moved
//
//****************************************************************************
void RenderScript::OnFrameEnd()
{

}

//****************************************************************************
//
//  Function:   RenderScript::GetStage
//
//  Purpose:    Attempt to find a stage in this script by its ID.
//
//  Return:     Pointer to a RS-stage.
//
//****************************************************************************
RenderScriptStage* RenderScript::GetStage(const char* id) const
{
    for (auto& stage : stages_)
        if (strcmp(stage->self_.name_, id) == 0)
            return stage;
    return nullptr;
}

//****************************************************************************
//
//  Function:   RenderScript::GetStage
//
//  Purpose:    Attempt to find a stage by the hash of its name, no use to outside.
//
//  Return:     Pointer to a RS-stage
//
//****************************************************************************
RenderScriptStage* RenderScript::GetStage(uint32_t nameHash) const
{
    for (auto& stage : stages_)
        if (stage->self_.nameHash_ == nameHash)
            return stage;
    return nullptr;
}

//****************************************************************************
//
//  Function:   RenderScript::SetStageEnabled
//
//  Purpose:    Utility, tries to find a stage by ID and then sets it's enabled
//              state to the given value.
//
//  WARNING:    require/ban traits of stages are implicit, any change to a stage's
//              enabled status will internally take action.
//
//****************************************************************************
void RenderScript::SetStageEnabled(const char* id, bool state)
{
    if (auto stage = GetStage(id))
        stage->active_ = state;
}

//****************************************************************************
//
//  Function:   RenderScript::Prepare
//
//  Purpose:    Realizes the required resources for this stage.
//
//****************************************************************************
void RenderScript::Prepare(GraphicsDevice* device)
{
    for (auto& stage : stages_)
    {
        for (auto& cmd : stage->commands_)
        {
            if (cmd.commandType_ == ForwardLights)
                forwardLitCmds_.push_back(&cmd);
        }
    }

    for (auto& buf : dataBuffers_)
    {
        if (buf->buffer_ == nullptr)
        {
            buf->buffer_ = device->CreateShaderStorageBuffer();
            buf->buffer_->SetSize(buf->size_);
        }
    }
}

//****************************************************************************
//
//  Function:   RenderScript::PrepareQueue
//
//  Purpose:    Converts instance transforms into a vertex-stream
//              and transfers bones into uniform buffers.
//
//  WARNING:    This is a performance and task critical function.
//
//****************************************************************************
void RenderScript::PrepareQueue(BatchQueue& batchQueue)
{
    static const size_t InstanceBufferSize = sizeof(float4x4) * MAX_INSTANCES;

    for (auto& grp : batchQueue.groups_)
    {
        size_t offset = 0;

        // do the loose instances
        int ct = grp.second.transforms_.size();
        size_t consumed = 0;
        while (ct > 0)
        {
            auto drawCt = std::min<int>(ct, MAX_INSTANCES);

            auto instVertBuffer = device_->GetScratchVertexBuffer(InstanceBufferSize);
            instVertBuffer->SetSubData(&grp.second.transforms_[consumed], 0, sizeof(float4x4) * drawCt);
            grp.second.instanceTransformBuffers_.push_back({ drawCt, instVertBuffer });

            ct -= MAX_INSTANCES;
            consumed += drawCt;
        }

        // now, deal with the sets of instance transforms, coalesce them into a single block
        // TODO: make this happen in the above.
        // BEWARE OF CHANGING THE FOLLOWING BLOCK:
        //      mistakes can be misinterpreted as failures in Frustum culling
        //      wasting lots of time "digging in the wrong place"
        std::vector<float4x4> instTransforms;
		size_t transCt = 0;
		for (auto& v : grp.second.transformSets_)
			transCt += v.second;
		instTransforms.resize(transCt);

        uint32_t writeCt = 0;
        for (size_t i = 0; i < grp.second.transformSets_.size(); ++i)
        {
            const auto& set = grp.second.transformSets_[i];
            if (set.second > 0)
            {
                if (writeCt + set.second > MAX_INSTANCES)
                {
                    auto working = device_->GetScratchVertexBuffer(InstanceBufferSize);
                    working->SetSubData(instTransforms.data(), 0, writeCt * sizeof(float4x4));
                    grp.second.instanceTransformBuffers_.push_back({ writeCt, working });
                    writeCt = 0;
                }
		
                memcpy(instTransforms.data() + writeCt, set.first, set.second * sizeof(float4x4));
                writeCt += set.second;
            }
        }
        // anything left that wasn't resolved in the loop?
        if (writeCt > 0)
        {
            auto working = device_->GetScratchVertexBuffer(InstanceBufferSize);
            working->SetData(instTransforms.data(), writeCt * sizeof(float4x4));
            grp.second.instanceTransformBuffers_.push_back({ writeCt, working });
            writeCt = 0;
        }
    }

    for (auto& batch : batchQueue.batches_)
    {
        if (batch.isSkinned_)
        {
            auto ubo = device_->GetScratchUniformBuffer(64 * 256); // support 256 bones
            ubo->SetData(batch.transforms_, sizeof(float4x4) * batch.numTransforms_);
            batch.bonesBuffer_ = ubo;
        }
    }

    for (auto& stage : stages_)
    {
        for (auto& cmd : stage->commands_)
        {
            if (cmd.commandType_ == GeometryPass)
            {
                auto skinnedHash = Hash(string(cmd.context_.name_) + SHADER_CONTEXT_SUFFIX_SKINNED);
                auto instHash = Hash(string(cmd.context_.name_) + SHADER_CONTEXT_SUFFIX_INST);
                cmd.resolvedQueue_.Resolve(batchQueue, cmd.context_.nameHash_, skinnedHash, instHash, cmd.cmdData_.drawData_.sortMode_);
            }
        }
    }
}

//****************************************************************************
//
//  Function:   RenderScript::ShouldStageExecute
//
//  Purpose:    Checks the require/ban properties of a stage to see
//              if the given stage should be executed.
//              Any failure, is complete failure.
//
//  Return:     True if this stage should execute.
//
//****************************************************************************
bool RenderScript::ShouldStageExecute(RenderScriptStage* stage) const
{
    // if any of these are inactive, then we don't run
    for (const auto& req : stage->requireActiveStages_)
    {
        if (!GetStage(req.nameHash_)->active_)
            return false;
    }

    // if any of these are active, then we don't run
    for (const auto& req : stage->ignoreActiveStages_)
    {
        if (GetStage(req.nameHash_)->active_)
            return false;
    }

    return stage->active_;
}

//****************************************************************************
//
//  Function:   RenderScript::OnBackBufferResize
//
//  Purpose:    Resizes the internally managed render-targets according to size
//              changes in the provided *back-buffer* (might not be a real back-buffer)
//              Notably this function deals with proportional sizes.
//
//****************************************************************************
void RenderScript::OnBackbufferResize(GraphicsDevice* device, uint32_t newWidth, uint32_t newHeight)
{
    width_ = newWidth;
    height_ = newHeight;

    for (auto& target : targetTextures_)
    {
        bool dirty = false;
        if (target->texture_ == nullptr)
            dirty = true;

        if (target->backbufferWidthFraction_ != 1.0f)
        {
            dirty = target->width_ != target->backbufferWidthFraction_ * newWidth;
            target->width_ = (uint32_t)(target->backbufferWidthFraction_ * newWidth);
        }
        else if (target->width_ != newWidth)
        {
            dirty = true;
            target->width_ = newWidth;
        }

        if (target->backbufferHeightFraction_ != 1.0f)
        {
            dirty = target->height_ != target->backbufferHeightFraction_ * newHeight;
            target->height_ = (uint32_t)(target->backbufferHeightFraction_ * newHeight);
        }
        else if (target->height_ != newHeight)
        {
            dirty = true;
            target->height_ = newHeight;
        }

        if (dirty)
        {
            if (target->texture_)
                target->texture_.reset();

            TextureTraits traits;
            traits.depth_ = 1;
            traits.height_ = target->height_;
            traits.width_ = target->width_;
            traits.kind_ = Texture2D;
            traits.format_ = target->targetFormat_;
            traits.layers_ = 1;
            traits.colorAttachment_ = target->targetFormat_ != TEX_DEPTH ? true : false;
            traits.depthAttachment_ = target->targetFormat_ == TEX_DEPTH ? true : false;
            target->texture_ = device->CreateTexture(traits);
#if defined(GLVU_GL)
            target->texture_->SetData(nullptr, target->width_, target->height_, 0, 0, 0);
#endif
        }
    }

    for (auto& stage : stages_)
    {
        vector< shared_ptr<Texture> > textures;
        stage->targetConfig_.targets_.clear();
        for (auto tgt : stage->targets_)
        {
            if (auto found = GetTargetTexture(tgt.nameHash_))
            {
                stage->targetConfig_.targets_.push_back(found);
                textures.push_back(found->texture_);
            }
        }

        if (textures.size() > 0)
            stage->targetConfig_.fbo_ = device->CreateFrameBuffer(textures);
    }
}

//****************************************************************************
//
//  Function:   RenderScript::GetTargetTexture
//
//  Purpose:    Finding a texture for a render-script to use as a target
//              isn't straight-forward. The target may be owned by this script
//              Or it may be a special target such as the backbuffer, shadowmap FBO,
//              or offscreen-LRL FBO. This function deals with that and the
//              translation into a `RenderTargetInfo` struct.
//
//  Threading:  not-safe
//
//****************************************************************************
RenderTargetInfo* RenderScript::GetTargetTexture(uint32_t nameHash)
{
    // these are safe as statics, they only need to be here so that they are 'pinned' for safe pointer return.
    // NOT SAFE TO USE FROM A THREAD THOUGH
    static RenderTargetInfo bbInfo;
    static RenderTargetInfo depthInfo;
    static auto backbufferHash = Hash(PIPELINE_RESOURCE_BACKBUFFER);
    static auto depthbufferHash = Hash(PIPELINE_RESOURCE_DEPTHBUFFER);
    static auto shadowmapHash = Hash(PIPELINE_RESOURCE_SHADOWMAP);

    if (nameHash == backbufferHash)
    {
        auto bb = device_->GetBackbuffer();
        bbInfo.width_ = bbInfo.fixedWidth_ = bb->GetWidth();
        bbInfo.height_ = bbInfo.fixedHeight_ = bb->GetHeight();
        bbInfo.texture_ = bb->GetTextureCount() > 0 ? bb->GetTexture(0) : nullptr;
        bbInfo.backbufferHeightFraction_ = bbInfo.backbufferWidthFraction_ = 1.0f;
        bbInfo.pingPong_ = false;
        bbInfo.targetFormat_ = TEX_BGRA8;
        return &bbInfo;
    }
    else if (depthbufferHash == nameHash)
    {
        auto db = device_->GetBackbuffer();
        depthInfo.width_ = depthInfo.fixedWidth_ = db->GetWidth();
        depthInfo.height_ = depthInfo.fixedHeight_ = db->GetHeight();
        depthInfo.texture_ = db->GetTextureCount() > 1 ? db->GetTexture(1) : nullptr;
        depthInfo.backbufferHeightFraction_ = depthInfo.backbufferWidthFraction_ = 1.0f;
        depthInfo.pingPong_ = false;
        depthInfo.targetFormat_ = TEX_DEPTH;
        return &depthInfo;
    }
    for (auto f : targetTextures_)
        if (f->name_.nameHash_ == nameHash)
            return f;

    return nullptr;
}

//****************************************************************************
//
//  Function:   RenderScript::GetDataBuffer
//
//  Purpose:    Finds an internal UBO by hash of the name.
//
//  Return:     Found UBO, or null.
//
//****************************************************************************
shared_ptr<Buffer> RenderScript::GetDataBuffer(uint32_t nameHash)
{
    for (auto buf : dataBuffers_)
    {
        if (buf->name_.nameHash_ == nameHash)
            return buf->buffer_;
    }
    return nullptr;
}

//****************************************************************************
//
//  Function:   RenderScript::CoalesceInstanceBatches
//
//  Purpose:    ??, used to do stuff, BatchQueue does it now
//
//  DEPRECATED
//
//****************************************************************************
void RenderScript::CoalesceInstanceBatches(GraphicsDevice*, const vector<Batch>& batchesIn, vector<Batch>& batchesOut)
{
    batchesOut.reserve(batchesIn.size());
    auto workingBatches = batchesIn;
    sort(workingBatches.begin(), workingBatches.end(), [](const Batch& lhs, const Batch& rhs) {
        return make_pair(lhs.material_, lhs.geometry_) < make_pair(rhs.material_, rhs.geometry_);
    });

    for (uint32_t idx = 0; idx < workingBatches.size(); ++idx)
    {
        auto& batch = workingBatches[idx];
        if (batch.isSkinned_)
            batchesOut.push_back(batch);
        else if (batch.numTransforms_ > 1)
            batchesOut.push_back(batch);
        else if (batch.canInstance_)
        {
            uint32_t workingIdx = idx + 1;
            while (workingIdx < workingBatches.size() && workingIdx - idx < 1000)
            {
                if (workingBatches[workingIdx].material_ == batch.material_ && workingBatches[workingIdx].geometry_ == batch.geometry_)
                    ++workingIdx;
                else
                    break;
            }
            if (workingIdx - idx > 1)
            {
                Batch newBatch;
                newBatch.geometry_ = batch.geometry_;
                newBatch.material_ = batch.material_;
                newBatch.isSkinned_ = false;
                newBatch.canInstance_ = true;
                newBatch.numTransforms_ = workingIdx - idx;
                idx = workingIdx;
            }
        }
    }
}

//****************************************************************************
//
//  Function:   RenderScript::RenderShadowMap
//
//  Purpose:    Performs the setup to flush batches of geometry to the given
//              FBO and viewport configuration. Because this function deals
//              shadow-maps it makes the appropriate selections of shader-pass,
//              and prepares the view-data appropriately for a given *face*
//              of a shadowmap.
//
//****************************************************************************
void RenderScript::RenderShadowMap(Renderer* renderer, ShadowBatchData& shadowBatch, uint4 vpt, shared_ptr<FrameBuffer> fbo, int shadowIndex, bool isFirst)
{
    PrepareQueue(shadowBatch.queue_);

    DrawBatchesTaskData task = { };

    if (shadowBatch.light->GetKind() == Light::POINT)
    {
        task.contextNameHash_ = Hash(SHADER_CONTEXT_RENDER_SHADOWMAP_POINT);
        task.instancedNameHash_ = Hash(SHADER_CONTEXT_RENDER_SHADOWMAP_POINT SHADER_CONTEXT_SUFFIX_INST);
        task.skinnedNameHash_ = Hash(SHADER_CONTEXT_RENDER_SHADOWMAP_POINT SHADER_CONTEXT_SUFFIX_SKINNED);
    }
    else if (shadowBatch.light->GetKind() == Light::SPOT)
    {
        task.contextNameHash_ = Hash(SHADER_CONTEXT_RENDER_SHADOWMAP_SPOTLIGHT);
        task.instancedNameHash_ = Hash(SHADER_CONTEXT_RENDER_SHADOWMAP_SPOTLIGHT SHADER_CONTEXT_SUFFIX_INST);
        task.skinnedNameHash_ = Hash(SHADER_CONTEXT_RENDER_SHADOWMAP_SPOTLIGHT SHADER_CONTEXT_SUFFIX_SKINNED);
    }
    else if (shadowBatch.light->GetKind() == Light::DIRECTIONAL)
    {
        task.contextNameHash_ = Hash(SHADER_CONTEXT_RENDER_SHADOWMAP_DIRLIGHT);
        task.instancedNameHash_ = Hash(SHADER_CONTEXT_RENDER_SHADOWMAP_DIRLIGHT SHADER_CONTEXT_SUFFIX_INST);
        task.skinnedNameHash_ = Hash(SHADER_CONTEXT_RENDER_SHADOWMAP_DIRLIGHT SHADER_CONTEXT_SUFFIX_SKINNED);
    }

    View shadowView = View(nullptr, nullptr, fbo);
    shadowView.viewport_[0] = vpt.x;
    shadowView.viewport_[1] = vpt.y;
    shadowView.viewport_[2] = vpt.x + vpt.z;
    shadowView.viewport_[3] = vpt.y + vpt.w;
    shadowView.scene_ = nullptr;

    Camera dummyCamera(device_);
    dummyCamera.SetViewport(vpt);
    shadowBatch.light->SetupShadowCamera(&dummyCamera, shadowIndex, shadowBatch.bounds_);
    shadowView.cameras_[0] = &dummyCamera;

    ViewBufferData viewDataBuffer = { };
    viewDataBuffer.viewProj[0] = viewDataBuffer.viewProj[1] = dummyCamera.GetInvView();
    viewDataBuffer.invViewProj[0] = viewDataBuffer.invViewProj[1] = dummyCamera.GetInvViewProjection();
    //shadowBatch.light->shadowMatrix_[0] = dummyCamera.GetInvViewProjection(); // WTF was this for?
    viewDataBuffer.viewPos[0] = dummyCamera.GetPosition().ToPos4();
    viewDataBuffer.viewDir[0] = dummyCamera.GetDirection().ToPos4();
    viewDataBuffer.viewUp[0] = dummyCamera.GetUp().ToPos4();
    viewDataBuffer.viewport = vpt;
    viewDataBuffer.nearFar = float4 { dummyCamera.GetNear(), dummyCamera.GetFar(), 0, 0 };

    shadowBatch.light->SetShadowDomain(fbo->GetTexture(0), vpt, shadowBatch.atlasCells_[0], shadowIndex);

    auto ubo = device_->GetScratchUniformBuffer(sizeof(ViewBufferData));
    ubo->SetData(&viewDataBuffer, sizeof(ViewBufferData));

    task.uboBindings_.push_back({ SHADER_BUFFER_VIEW_DATA, 0, UINT_MAX, ubo });
    task.clearTargets_ = isFirst;
    DrawBatches(renderer, shadowView, nullptr, shadowBatch.queue_, task);
}

//****************************************************************************
//
//  Function:   RenderScript::RenderOffscreen
//
//  Purpose:    Flushes the given batches to an FBO with the target viewport.
//
//****************************************************************************
void RenderScript::RenderOffscreen(vector<Batch>& batches, uint4 vpt, shared_ptr<FrameBuffer> fbo)
{
    BatchQueue queue;
    for (auto& b : batches)
        queue.Add(b);
    PrepareQueue(queue);
    queue.SortByStateChange();

    DrawBatchesTaskData task = { };
    task.contextNameHash_ = Hash(SHADER_CONTEXT_OFFSCREEN_LIGHT);
    task.instancedNameHash_ = Hash(SHADER_CONTEXT_OFFSCREEN_LIGHT SHADER_CONTEXT_SUFFIX_INST);
    task.skinnedNameHash_ = Hash(SHADER_CONTEXT_OFFSCREEN_LIGHT SHADER_CONTEXT_SUFFIX_SKINNED);

    View view = View(nullptr, nullptr, fbo);
    view.viewport_[0] = vpt.x;
    view.viewport_[1] = vpt.y;
    view.viewport_[2] = vpt.z;
    view.viewport_[3] = vpt.w;
    view.scene_ = nullptr;

    Camera dummyCamera(device_);
    dummyCamera.SetViewport(vpt);
    dummyCamera.SetOrtho(0, 0, 1, 1, 0, 1000);
    view.cameras_[0] = &dummyCamera;

    ViewBufferData viewDataBuffer = { };
    SetupViewbufferData(&dummyCamera, viewDataBuffer, 0);

    auto ubo = device_->GetScratchUniformBuffer(sizeof(ViewBufferData));
    ubo->SetData(&viewDataBuffer, sizeof(ViewBufferData));

    task.uboBindings_.push_back({ SHADER_BUFFER_VIEW_DATA, 0, UINT_MAX, ubo });
    task.clearTargets_ = true;
    DrawBatches(nullptr, view, nullptr, queue, task);
}

void RenderScript::RenderForwardLights(Renderer* renderer, View view, RenderScriptStage* stage, const RS_DrawCmd& cmd, const vector<shared_ptr<Light>>& lights)
{
	auto lightDataBuffer = device_->GetScratchUniformBuffer(sizeof(LightData));

	// There's 2 ways to do this:
	// foreach (light) foreach (drawable)
	// foreach (drawable) foreach (lightAffecting)
	//         There does not appear to be an ideal one
	//         except when summing lights (ie. 4 lights in 1 pass)

	// Regardless of whether threading is available this will use jobs
	// to perform the scene queries and construct the queues from those results.
	// It's a minor indirection if threading isn't available.

	std::vector<BatchQueue> lightQueues;
	lightQueues.resize(lights.size());

	struct TaskData {
		IQueriableScene* scene;
		const std::vector<std::shared_ptr<Light> >& lights;
		std::vector<BatchQueue>* batchGroups;
	} taskData = { view.scene_, lights, &lightQueues };

	static auto job_LightBatchQuery = [](uint32_t taskID, void* taskData) {
		TaskData* data = (TaskData*)taskData;
		AABB dumbBnds;
		dumbBnds.SetNegativeInfinity();

		auto batches = data->scene->GetBatches(data->lights[taskID].get(), UINT_MAX, dumbBnds);
		BatchQueue& targetQueue = (*data->batchGroups)[taskID];
		for (size_t i = 0; i < batches.size(); ++i)
			targetQueue.Add(batches[i]);
	};

	for (uint32_t i = 0; i < lights.size(); ++i)
		device_->PushThreadJob(i, &taskData, job_LightBatchQuery);

	device_->WaitForThreadJobs();

	for (uint32_t i = 0; i < lights.size(); ++i)
	{
		auto light = lights[i];
		if (lightQueues[i].IsEmpty())
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
		lightDataBuffer->SetData(&lightData, sizeof(lightData));

		DrawBatchesTaskData task;
		task.uboBindings_.push_back({ SHADER_BUFFER_LIGHT_DATA, 0, UINT_MAX, lightDataBuffer });
		task.clearTargets_ = false;
        task.textureBindings_.push_back({ SHADER_TEX_SHADOWMAP, SamplerTraits { FILTER_SHADOW, false }, light->GetShadowMapTexture() });

		// can nicely pack this all into a LUT
		static const char* Tables[] = {
			"_LITPOINT",
			"_LITPOINT_SKINNED",
			"_LITPOINT_INST",
			"_LITPOINT_SHDW",
			"_LITPOINT_SHDW_SKINNED",
			"_LITPOINT_SHDW_INST",
			
            "_LITSPOT",
			"_LITSPOT_SKINNED",
			"_LITSPOT_INST",
			"_LITSPOT_SHDW",
			"_LITSPOT_SHDW_SKINNED",
			"_LITSPOT_SHDW_INST",
			
            "_LITDIR",
			"_LITDIR_SKINNED",
			"_LITDIR_INST",
			"_LITDIR_SHDW",
			"_LITDIR_SHDW_SKINNED",
			"_LITDIR_SHDW_INST",
		};

		// entries per Light::Type
		const unsigned permutationsPerTable = 6;
		// Offset to the SHDW variations from the above entry base
		const unsigned shadowPermsOffset = 3;

		unsigned tableBase = light->GetKind() * permutationsPerTable;
		if (light->IsShadowCasting())
			tableBase += shadowPermsOffset;

		task.contextNameHash_ = Hash(string(cmd.context_.name_) + Tables[tableBase]);
		task.skinnedNameHash_ = Hash(string(cmd.context_.name_) + Tables[tableBase + 1]);
		task.instancedNameHash_ = Hash(string(cmd.context_.name_) + Tables[tableBase + 2]);
		DrawBatches(renderer, view, stage, lightQueues[i], task);
	}
}

}