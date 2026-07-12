#pragma once

#include <glvu.h>
#include "Geometry.h"

#include <map>

namespace GLVU
{

class Light;
class Material;

typedef std::tuple<Geometry*, Material*, float> BatchGroupKey;

/// Instancing friendly objects get coalesced into batchgroups.
struct GLVU_API BatchGroup
{
    Geometry* geometry_;
    Material* material_;

    /// loose-objects get pushed here, which is not-optimal (slow to resize/push_back)
    std::vector<math::float4x4> transforms_;
    /// Whole clusters get pushed here, fast, memcpy'ed later - this is an easy 40% upgrade
    std::vector< std::pair<math::float4x4*, size_t> > transformSets_;
    uint32_t sortBias_;
    uint32_t instanceTransformStart_;

    std::vector< std::shared_ptr<Buffer> > materialTraitUBOs_; // custom data
    std::vector< std::pair<uint32_t, std::shared_ptr<Buffer> > > instanceTransformBuffers_;
};

struct GLVU_API BatchQueue
{
    std::vector<Batch> batches_;
    std::map<BatchGroupKey, BatchGroup> groups_;
    /// We sort pointers instead of the objects themselves because they're fat.
    std::vector<Batch*> sortedBatches_;

    void SortByStateChange();
    void SortFrontToBack();
    void SortBackToFront();

    void Add(const Batch& batch);

    bool IsEmpty() const;
    bool ConsiderSplitting() const;
    void Split(std::vector<BatchQueue>& target, int maxSplits);
    void Clone(BatchQueue& into) const;

    void CollectInstancingData(void* target, uint32_t stride, uint32_t& currentIndex);

    size_t NumTransforms() const {
        uint32_t r = 0;
        for (auto& b : batches_)
            if (b.canInstance_)
                r += b.numTransforms_;
        for (auto& g : groups_)
            r += g.second.transforms_.size();
    }

private:
    BatchGroup& FindOrCreate(BatchGroupKey key)
    {
        auto found = groups_.find(key);
        if (found != groups_.end())
            return found->second;
        BatchGroup grp;
        grp.geometry_ = std::get<0>(key);
        grp.material_ = std::get<1>(key);
        grp.sortBias_ = std::get<2>(key);
        groups_[key] = grp;
        return groups_[key];
    }
    void AddDirect(BatchGroupKey key, math::float4x4 mat) {
        FindOrCreate(key).transforms_.push_back(mat);
    }
    void AddDirect(BatchGroupKey key, std::pair<math::float4x4*, size_t> set) {
        FindOrCreate(key).transformSets_.push_back(set);
    }
};

// A complete resolved set of batches for a specific pass in a render-script.
struct GLVU_API ResolvedBatchQueue {
    struct ResolvedBatch { 
        const Batch* source_; const ShaderPass*pass_; 
    };
    struct ResolvedGroup { const BatchGroup* group_; const ShaderPass* pass_; };

    std::vector<ResolvedBatch> batches_;
    std::vector<ResolvedGroup> groups_;

    ResolvedBatchQueue() { }

    void Resolve(const BatchQueue&, uint32_t contextNameHash, uint32_t skinnedNamedHash, uint32_t instancedNameHash, SortMode sorting);
    bool HasWork() const { return batches_.size() > 0 || groups_.size() > 0; }

    void Split(std::vector<ResolvedBatchQueue>& target, int maxSplits, bool splitGroupsToo);
};

/// Information required for rendering shadowmaps.
struct GLVU_API ShadowBatchData
{
    Light* light;
    math::float4 atlasCells_[2];
    math::AABB bounds_;
    BatchQueue queue_;
};

}
