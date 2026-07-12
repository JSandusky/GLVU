#include "Batching.h"

#include "Effect.h"
#include "Material.h"
#include "Packing.h"

#include <algorithm>
#include <numeric>

using namespace std;

#define AUTO_INSTANCE_RESERVE 1000
#define MAX_INSTANCES 65000 //(4096 * 2)
#define GET_SORT_VALUE(VAL, FLIP_VAL) ((VAL) == FLT_MAX ? FLIP_VAL : (VAL))

namespace GLVU
{

//****************************************************************************
//
//  Function:   BatchQueue::SortByStateChange
//
//  Purpose:    A duplicate queue is created and then its contents are sorted.
//              The sort is done in two parts (first is complete, second is stable).
//              
//  Notes:      Tessellation ON/OFF is a heavy state change, might want to represent that?
//
//****************************************************************************
void BatchQueue::SortByStateChange()
{
    sortedBatches_.resize(batches_.size());
    memcpy(sortedBatches_.data(), batches_.data(), sizeof(Batch) * batches_.size());
    // Intention of the above is this:
    //for (size_t i = 0; i < batches_.size(); ++i)
    //    sortedBatches_[i] = &batches_[i];

    // sort by distance, front -> back for depth, translucents should always sort back->front
    sort(sortedBatches_.begin(), sortedBatches_.end(), [](const Batch* lhs, const Batch* rhs) -> bool {
        return
            make_pair(
                lhs->renderOrder_,
                lhs->computedSortDistance_)
            <
            make_pair(
                rhs->renderOrder_,
                rhs->computedSortDistance_);
    });

    // now stable sort by renderorder, then material, then by geometry.
    // we'll be in state sequence, but ordered by depth
    sort(sortedBatches_.begin(), sortedBatches_.end(), [](const Batch* lhs, const Batch* rhs) -> bool {
        return
            make_tuple(
                lhs->renderOrder_,
                lhs->material_,
                lhs->geometry_)
            <
            make_tuple(
                rhs->renderOrder_,
                rhs->material_,
                rhs->geometry_);
    });
}

//****************************************************************************
//
//  Function:   BatchQueue::SortFrontToBack
//
//  Purpose:    A duplicate queue is created and then its contents are sorted.
//              The sort is done first by render-order and then by distance.
//
//****************************************************************************
void BatchQueue::SortFrontToBack()
{
    sortedBatches_.resize(batches_.size());
    for (size_t i = 0; i < batches_.size(); ++i)
        sortedBatches_[i] = &batches_[i];

    sort(sortedBatches_.begin(), sortedBatches_.end(), [](const Batch* l, const Batch* r) -> bool {
        return
            make_pair(
                l->renderOrder_,
                l->computedSortDistance_)
            <
            make_pair(
                r->renderOrder_,
                r->computedSortDistance_);
    });
}

//****************************************************************************
//
//  Function:   BatchQueue::SortBackToFront
//
//  Purpose:    A duplicate queue is created and then its contents are sorted.
//              This sort is intended for alpha blending where the actual sequence
//              of rendered objects determines the final result and state change
//              sorting will just mess that up.
//
//****************************************************************************
void BatchQueue::SortBackToFront()
{
    sortedBatches_.resize(batches_.size());
    for (size_t i = 0; i < batches_.size(); ++i)
        sortedBatches_[i] = &batches_[i];

    sort(sortedBatches_.begin(), sortedBatches_.end(), [](const Batch* l, const Batch* r) -> bool {
        return
            make_pair(
                l->renderOrder_,
                GET_SORT_VALUE(l->computedSortDistance_, FLT_MIN))
    >
            make_pair(
                r->renderOrder_,
                GET_SORT_VALUE(r->computedSortDistance_, FLT_MIN));
    });
}

//****************************************************************************
//
//  Function:   BatchQueue::Add
//
//  Purpose:    Pushes an object into this queue. First it's checked for whether instancing
//              is an option, if so then it'll be pushed into an instancing group.
//              Otherwise it will be pushed as a *loose* batch.
//
//              This function is heavily influenced by Urho3D.
//
//****************************************************************************
void BatchQueue::Add(const Batch& batch)
{
    // for safety we'll protect ourselves from this.
    if (batch.geometry_ == nullptr || batch.material_ == nullptr)
        return;

    if (batch.canInstance_)
    {
        BatchGroupKey key = { batch.geometry_, batch.material_, batch.computedSortDistance_ };
        auto found = groups_.find(key);
        if (found != groups_.end())
        {
            if (batch.numTransforms_ == 1)
            {
                found->second.transforms_.emplace_back(*batch.transforms_);
            }
            else
            {
                // split it up if we need to, far easier to do the offsets here
                // then when memcpying into mapped buffers.
                float4x4* basePtr = batch.transforms_;
                size_t totalCt = batch.numTransforms_;
                while (totalCt > MAX_INSTANCES)
                {
                    found->second.transformSets_.push_back({ basePtr, MAX_INSTANCES });
                    totalCt -= MAX_INSTANCES;
                    basePtr += MAX_INSTANCES;
                }
                if (totalCt > 0)
                    found->second.transformSets_.push_back({ basePtr, totalCt });
            }
        }
        else
        {
            // create a new batchgroup
            BatchGroup grp = { };
            grp.material_ = batch.material_;
            grp.geometry_ = batch.geometry_;
            grp.sortBias_ = static_cast<uint32_t>(batch.computedSortDistance_);
            groups_.insert({ key, grp });
            found = groups_.find(key);

            if (batch.numTransforms_ == 1)
            {
                found->second.transforms_.reserve(AUTO_INSTANCE_RESERVE);
                found->second.transforms_.emplace_back(*batch.transforms_);
            }
            else
            {
                // split it up if we need to, far easier to do the offsets here
                // then when memcpying into mapped buffers.
                float4x4* basePtr = batch.transforms_;
                size_t totalCt = batch.numTransforms_;
                while (totalCt > MAX_INSTANCES)
                {
                    found->second.transformSets_.push_back({ basePtr, MAX_INSTANCES });
                    totalCt -= MAX_INSTANCES;
                    basePtr += MAX_INSTANCES;
                }
                if (totalCt > 0)
                    found->second.transformSets_.push_back({ basePtr, totalCt });
            }
        }
    }
    else
    {
        batches_.push_back(batch);
    }
}

//****************************************************************************
//
//  Function:   BatchQueue::IsEmpty
//
//  Purpose:    Utility.
//
//  Return:     True if there's nothing here.
//
//****************************************************************************
bool BatchQueue::IsEmpty() const
{
    return groups_.empty() && batches_.empty();
}

//****************************************************************************
//
//  Function:   BatchQueue::ConsiderSplitting
//
//  Purpose:    It's not always a good idea to split command-list construction
//              onto different threads, this function decides when that's worth
//              doing.
//
//  Return:     True if splitting for threading is a reasonable decision.
//
//****************************************************************************
#define BATCH_QUEUE_SPLIT_SIZE 1
bool BatchQueue::ConsiderSplitting() const
{
#if !defined(GLVU_VK)
    return false;
#else
    // we'll split at more than 64 draw calls
    return batches_.size() > BATCH_QUEUE_SPLIT_SIZE;
#endif
}

//****************************************************************************
//
//  Function:   BatchQueue::Split
//
//  Purpose:    Splits this queue into N new queues. The first queue gets all
//              of the instanced draws.
//
//              Only relevant to Vulkan where command-lists construction can
//              be recorded.
//
//****************************************************************************
void BatchQueue::Split(vector<BatchQueue>& target, int maxSplits)
{
    static auto VectorSplit = [](const std::vector<Batch>& batches, size_t parts) -> std::vector< std::vector<Batch> > {

        vector< vector<Batch> > subVecs;

        auto itr = batches.begin();
        // Variable to control size of non divided elements
        auto fullSize = batches.size();

        for (unsigned i = 0; i < parts; ++i)
        {
            // Variable controls the size of a part
            auto partSize = fullSize / (parts - i);
            fullSize -= partSize;
            subVecs.emplace_back(std::vector<Batch>{itr, itr + partSize});
            itr += partSize;
        }
        return subVecs;
    };

    if (batches_.size() > 0)
    {
        auto ret = VectorSplit(batches_, std::min<size_t>(batches_.size(), maxSplits));
        for (size_t i = 0; i < ret.size(); ++i)
        {
            target.push_back(BatchQueue());
            target.back().batches_ = ret[i];
        }
    }

    while (target.size() < maxSplits && groups_.size() > 0)
        target.push_back(BatchQueue());

    for (auto& grp : this->groups_)
    {
        for (size_t j = 0; j < grp.second.transforms_.size(); ++j)
            target[j % maxSplits].AddDirect(grp.first, grp.second.transforms_[j]);
        for (size_t j = 0; j < grp.second.transformSets_.size(); ++j)
            target[j % maxSplits].AddDirect(grp.first, grp.second.transformSets_[j]);
    }
}

//****************************************************************************
//
//  Function:   BatchQueue::Clone
//
//  Purpose:    Makes a duplicate of this queue, to be used before performing
//              sorting requirements so there are specific sorted queues.
//
//****************************************************************************
void BatchQueue::Clone(BatchQueue& into) const
{
    into.groups_ = groups_;
    into.batches_ = batches_;
    // enforce this for safety.
    into.sortedBatches_.clear();
}

//****************************************************************************
//
//  Function:   BatchQueue::CollectInstancingData
//
//  Purpose:    Copies instance transform data into mapped buffer data.
//              Shouldn't care whether the buffer is an SSBO or VBO
//
//****************************************************************************
void BatchQueue::CollectInstancingData(void* target, uint32_t stride, uint32_t& currentIndex)
{
    for (auto b : sortedBatches_)
    {
        if (b->canInstance_)
        {
            unsigned char* writeDest = ((unsigned char*)target) + stride * currentIndex;
            const auto bytesToWrite = sizeof(float4x4) * b->numTransforms_;
            memcpy(writeDest, b->transforms_, bytesToWrite);
            currentIndex += b->numTransforms_;
        }
    }
    for (auto g : groups_)
    {
        unsigned char* writeDest = ((unsigned char*)target) + stride * currentIndex;
        const auto bytesToWrite = sizeof(float4x4) * g.second.transforms_.size();
        memcpy(writeDest, g.second.transforms_.data(), bytesToWrite);
        g.second.instanceTransformStart_ = currentIndex;
        currentIndex += (uint32_t)g.second.transforms_.size();
    }
}

//****************************************************************************
//
//  Function:   ResolvedBatchQueue::Resolve
//
//  Purpose:    Grabs appropriate batches and groups from a complete queue
//              to narrow them down for use from a specific render-cmd.
//              This front-loads the work so it can be chewed through while
//              rendering and allows different sorting-modes per command instead
//              sorting BatchQueue itself over and over again or storing many
//              batchqueues.
//
//****************************************************************************
void ResolvedBatchQueue::Resolve(const BatchQueue& queue, uint32_t contextNameHash, uint32_t skinnedNameHash, uint32_t instancedNameHash, SortMode sorting)
{
    batches_.clear();
    groups_.clear();

    batches_.reserve(queue.batches_.size());
    groups_.reserve(queue.groups_.size());
    for (auto& g : queue.groups_)
    {
        if (auto fnd = g.second.material_->GetEffect()->GetPass(instancedNameHash, g.second.geometry_->primType_))
            groups_.push_back({ &g.second, fnd.get() });
    }

    for (auto& b : queue.batches_)
    {
        std::shared_ptr<ShaderPass> pass;
        if (b.isSkinned_)
            pass = b.material_->GetEffect()->GetPass(skinnedNameHash, b.geometry_->primType_);
        else if (b.numTransforms_ > 1)
            pass = b.material_->GetEffect()->GetPass(instancedNameHash, b.geometry_->primType_);
        else
            pass = b.material_->GetEffect()->GetPass(contextNameHash, b.geometry_->primType_);

        if (pass)
            batches_.push_back({ &b, pass.get() });
    }

    if (sorting == FrontToBack)
    {
        static auto sort_f2b = [](const ResolvedBatch& lhs, const ResolvedBatch& rhs) {
            return make_pair(lhs.source_->renderOrder_, lhs.source_->computedSortDistance_) 
                <
            make_pair(rhs.source_->renderOrder_, rhs.source_->computedSortDistance_);
        };
        std::sort(batches_.begin(), batches_.end(), sort_f2b);
    }
    else if (sorting == BackToFront)
    {
        static auto sort_b2f = [](const ResolvedBatch& lhs, const ResolvedBatch& rhs) {
            return make_pair(lhs.source_->renderOrder_, -lhs.source_->computedSortDistance_)
                <
            make_pair(rhs.source_->renderOrder_, -rhs.source_->computedSortDistance_);
        };
        std::sort(batches_.begin(), batches_.end(), sort_b2f);
    }
    else if (sorting == ContextSwitch)
    {
        static auto sort_state = [](const ResolvedBatch& lhs, const ResolvedBatch& rhs) {
            return make_tuple(lhs.source_->renderOrder_, lhs.source_->material_, lhs.source_->geometry_)
                <
            make_tuple(rhs.source_->renderOrder_, rhs.source_->material_, rhs.source_->geometry_);
        };
        std::sort(batches_.begin(), batches_.end(), sort_state);
    }
    else // ContextAndDepth
    {
        static auto optimal = [](const ResolvedBatch& lhs, const ResolvedBatch& rhs) {
            return 
            make_tuple(lhs.source_->renderOrder_, lhs.source_->computedSortDistance_, lhs.source_->material_, lhs.source_->geometry_)
                <
            make_tuple(rhs.source_->renderOrder_, rhs.source_->computedSortDistance_, rhs.source_->material_, rhs.source_->geometry_);
        };  
        std::sort(batches_.begin(), batches_.end(), optimal);
    }
}

//****************************************************************************
//
//  Function:   ResolvedBatchQueue::Split
//
//  Purpose:    For Vulkan, D3D11, and D3D12 we can multiple command-lists
//              can be concurrently recorded. They'll be executed in sequence.
//              Whether to split groups between command-lists or not is a tuning
//              variable, some scenarios will have different gains/losses.
//
//****************************************************************************
void ResolvedBatchQueue::Split(vector<ResolvedBatchQueue>& target, int maxSplits, bool splitGroupsToo)
{
    vector< vector<ResolvedBatch> > splitBatches;
    Partition<ResolvedBatch>(splitBatches, batches_, maxSplits);

    vector< vector<ResolvedGroup> > splitGrps;
    if (splitGroupsToo)
        Partition<ResolvedGroup>(splitGrps, groups_, maxSplits);

    for (size_t i = 0; i < std::max(splitBatches.size(), splitGrps.size()); ++i)
    {
        ResolvedBatchQueue newQueue;
        
        if (splitGroupsToo && i < splitGrps.size())
            newQueue.groups_ = splitGrps[i];
        else if (splitGroupsToo == false && i == 0)
            newQueue.groups_ = groups_;

        if (i < splitBatches.size())
            newQueue.batches_ = splitBatches[i];
    }
}

}
