//****************************************************************************
//
//  File:       Renderer.cpp
//  License:    MIT
//  Project:    GLVU
//  Contents:   API independent rendering loop.
//
//	TODO:		Thread shadow-map batch query.
//
//****************************************************************************

#include "Renderer.h"

#include "Batching.h"
#include "Renderables.h"
#include "GraphicsDeviceHead.h"
#include "RenderGraph.h"

#include <algorithm>
#include <unordered_set>

using namespace std;

namespace GLVU
{

//****************************************************************************
//
//  Function:   Renderer::Renderer
//
//  Purpose:    Construct, setup shadowmap and offscreen low-res lighting atlases
//              then register those textures to the graphics-device so that
//              RenderScript's can refer to them.
//
//****************************************************************************
Renderer::Renderer(GraphicsDevice* device, unsigned shadowDim, unsigned offscreenSize) :
    device_(device),
    shadowsEnabled_(true)
{
    ResizeOffscreenBuffers(shadowDim, offscreenSize);
}

//****************************************************************************
//
//  Function:   Renderer::~Renderer
//
//  Purpose:    Destruct, nothing to do yet.
//
//****************************************************************************
Renderer::~Renderer()
{

}

//****************************************************************************
//
//  Function:   Renderer::ResizeOffscreenBuffers
//
//  Purpose:    Changes (at runtime) the sizes of the shadowmap and low-res lighting
//              buffers / atlases.
//
//****************************************************************************
void Renderer::ResizeOffscreenBuffers(unsigned shadowDim, unsigned offscreenLightingDim)
{
    shadowAtlas_.reset(new RenderTargetAtlas(device_, TEX_SHADOW32, shadowDim, false));
    probeAtlas_.reset(new RenderTargetAtlas(device_, TEX_RGBA16F, shadowDim, false));

    device_->AddSystemTexture(PIPELINE_RESOURCE_SHADOWMAP, shadowAtlas_->shadowAtlas_);
    device_->AddSystemTexture(PIPELINE_RESOURCE_OBJECT_SPACE_LIGHTING, probeAtlas_->shadowAtlas_);
}

//****************************************************************************
//
//  Function:   Renderer::Execute
//
//  Purpose:    Performs the main-loop of rendering. First drawing shadows
//              for all unique shadow-casting lights then drawing each view
//              in sequence.
//
//              This function is responsible shadow-maps and rendering
//              offscreen-low-resolution lighting.
//
//****************************************************************************
void Renderer::Execute(std::shared_ptr<View> view)
{
    PendingView v;
    v.head_ = nullptr;
    v.script_ = view->script_;
    v.view_ = view;
    std::vector<PendingView> tempViews = { v };
    ExecuteInternal(tempViews);
}
void Renderer::Execute()
{
    ExecuteInternal(views_);
}
void Renderer::ExecuteInternal(std::vector<PendingView>& views)
{
    unordered_set<shared_ptr<Light> > shadowedLights;

#define DO_COMPUTE(STAGE) for (auto& compute : computeTasks_) \
    if (compute.second == STAGE) \
        device_->ExecuteCompute(compute.first, true);

    DO_COMPUTE(ComputeStage_First);

    // Collect all shadow-casting lights in all scenes.
    unordered_set<IQueriableScene*> scenes;
    for (const auto& v : views)
        scenes.insert(v.view_->scene_);

    if (shadowsEnabled_)
    {
        bool haveDrawnShadow = false;

        shadowAtlas_->Clear();
#if defined(GLVU_GL) || defined(GLVU_GLES3) || defined(GLVU_DX11)
        // Vulkan does not require a clear because we can specify that when binding.
        shadowAtlas_->shadowFBO_->Clear();
#endif

		struct ShadowMapBatchData
		{
			IQueriableScene* scene_;
			shared_ptr<Light> light_;
			uint32_t index_;
			shared_ptr<ShadowBatchData> batches_;
		};

		vector<ShadowMapBatchData> shadowBatches;
		static auto Task_ShadowMapBatchQuery = [](uint32_t taskID, void* userData)
		{
			ShadowMapBatchData* sb = (ShadowMapBatchData*)userData;
			sb->batches_->light = sb->light_.get();
			sb->batches_->bounds_.SetNegativeInfinity();
			
			auto batches = sb->scene_->GetBatches(sb->light_.get(), sb->index_, sb->batches_->bounds_);
			for (auto& b : batches)
			    sb->batches_->queue_.Add(b);
		};

        for (auto scene : scenes)
        {
            // collect unique visible lights in each view
            unordered_set<shared_ptr<Light> > lights;
            for (const auto& v : views)
            {
                if (v.view_->TestFlag(ViewFlag_Shadows) && v.view_->scene_ == scene)
                {
                    auto l = scene->GetLights(v.view_->cameras_[0]->GetFrustum());
                    if (!l.empty())
                    {
                        for (auto li : l)
                        {
                            if (li->IsShadowCasting())
                                shadowedLights.insert(li);
                        }
                    }
                }
            }

            // now render shadow-maps, since the list is unique no light will render a shadow-map multiple times
            for (auto light : shadowedLights)
            {
                for (uint32_t mapIndex = 0; mapIndex < light->GetShadowMapCount(); ++mapIndex)
                {
					shadowBatches.push_back({
						scene,
						light,
						mapIndex,
						shared_ptr<ShadowBatchData>(new ShadowBatchData())
					});
                }
            }
        }

// PROCESS ALL SHADOWMAP QUERIES IN THREADS
		{
			for (uint32_t i = 0; i < shadowBatches.size(); ++i)
				device_->PushThreadJob(i, &shadowBatches[i], Task_ShadowMapBatchQuery);
			device_->WaitForThreadJobs();
		}

// RENDER ALL SHADOW BATCHES THAT HAVE RESULTS!
		for (auto& sb : shadowBatches)
		{
			if (!sb.batches_->queue_.IsEmpty())
			{
				//if (light->GetKind() != light->DIRECTIONAL)
				{
					unsigned shadowSize = sb.light_->GetShadowDim();
					shadowSize = std::min(shadowSize, maxShadowSize_);

					auto shadowArea = shadowAtlas_->GetShadowRect(shadowSize);
					if (!shadowArea.Equals(AtlasCellTable::InvalidCell))
					{
						auto vpt = shadowAtlas_->ToViewport(shadowArea);

						// any script will do for this, there should be no meaningful state and command-buffer wipe is at the tail-end of execution.
						device_->AddStat(STAT_SHADOWMAPS, 1);
						views[0].script_->RenderShadowMap(this, *sb.batches_, vpt, shadowAtlas_->shadowFBO_, sb.index_, !haveDrawnShadow);
						sb.light_->SetShadowDomain(shadowAtlas_->shadowAtlas_, vpt, shadowArea, sb.index_);
						haveDrawnShadow = true;
					}
				}
			}
		}
    }

    DO_COMPUTE(ComputeStage_BeforeViews);

    unordered_set<void*> lit;
    vector<Batch> offscreenLit;

	vector< vector<Batch> > viewBatches;
	viewBatches.resize(views.size());

	struct BatchQueryTask {
		PendingView* view;
		vector<Batch>* target;
	};

// USE THREADING TO QUERY ALL VIEWS
	vector<BatchQueryTask> viewQueryTasks;
	{
		for (uint32_t i = 0; i < views.size(); ++i)
			viewQueryTasks.push_back({ &views[i], &viewBatches[i] });

		static auto Task_FrustumBatchQuery = [](uint32_t taskID, void* userData) {
			BatchQueryTask* task = (BatchQueryTask*)userData;
			auto v = task->view;
			auto frus = v->view_->cameras_[0]->GetFrustum();
			*(task->target) = v->view_->scene_->GetBatches(frus);
		};

		for (uint32_t i = 0; i < views.size(); ++i)
			device_->PushThreadJob(i, &viewQueryTasks[i], Task_FrustumBatchQuery);
		device_->WaitForThreadJobs();
	}

    // sort views by their relevant graphics device head.
    std::stable_sort(views.begin(), views.end(), [](const PendingView& lhs, const PendingView& rhs) {
        return (uintptr_t)lhs.view_->head_ < (uintptr_t)rhs.view_->head_;
    });
    // now sort by root or not
    std::stable_sort(views.begin(), views.end(), [](const PendingView& lhs, const PendingView& rhs) -> bool {
        return lhs.view_->IsRoot() < rhs.view_->IsRoot();
    });

    GraphicsDeviceHead* activeHead = nullptr;
    bool lastWasRoot = false;
	for (size_t i = 0; i < views.size(); ++i)
	{
		auto& v = views[i];

        if (activeHead != v.view_->head_)
        {
            if (activeHead)
                activeHead->EndHead();
            activeHead = v.view_->head_;
            if (activeHead)
                activeHead->BeginHead();
        }

        if (v.view_->IsRoot() && !lastWasRoot)
            DO_COMPUTE(ComputeStage_BeforeRootViews);

        lastWasRoot = v.view_->IsRoot();

        auto frustum = v.view_->cameras_[0]->GetFrustum();
		auto& batches = viewBatches[i];

        vector<Batch> offscreenLit;
        for (auto& b : batches)
        {
            // cannot offscreen-light instanced batches
            if (b.canInstance_ == false && !b.lightingCell_.Equals(float4::zero))
            {
                auto rect = probeAtlas_->GetShadowRect((unsigned)b.lightingCell_.MaxElement());
                if (!rect.Equals(AtlasCellTable::InvalidCell))
                {
                    b.lightingCell_ = rect;
                    offscreenLit.push_back(b);
                }
            }
        }

        if (offscreenLit.size() > 0)
            v.script_->RenderOffscreen(offscreenLit, uint4(0, 0, probeAtlas_->dim_, probeAtlas_->dim_), probeAtlas_->shadowFBO_);

        auto lights = v.view_->scene_->GetLights(frustum);
        v.script_->Execute(this, *v.view_, batches, lights);
    }

    DO_COMPUTE(ComputeStage_End);

    if (activeHead)
        activeHead->EndHead();

    FinishRendering();
}

//****************************************************************************
//
//  Function:   Renderer::AddView
//
//  Purpose:    Pushes a view/script pair into the list of views to render.
//
//****************************************************************************
void Renderer::AddView(shared_ptr<View> view)
{
    if (view == nullptr)
    {
        device_->LogMessage("Attempted to add null view to Renderer", GLVU_WARNING);
        return;
    }
    if (view->scene_ == nullptr || view->cameras_[0] == nullptr)
    {
        device_->LogMessage("Renderer::AddView, malformed/incomplete view provided", GLVU_ERROR);
        return;
    }
    views_.push_back({ view->head_, view->script_, view });
}

vector<shared_ptr<View> > Renderer::GetViews() const
{
    vector<shared_ptr<View> > ret;
    for (auto& pv : views_)
        ret.push_back(pv.view_);
    return ret;
}

//****************************************************************************
//
//  Function:   Renderer::ClearViews
//
//  Purpose:    Resets the list of views to nothing.
//
//****************************************************************************
void Renderer::ClearViews()
{
    views_.clear();
}

}