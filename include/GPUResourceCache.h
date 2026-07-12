#pragma once

#include "glvu.h"

#include "Effect.h"

#include <unordered_map>

namespace GLVU
{

    template<typename T>
    class GLVU_API GPUResourceCache : public GPUObject
    {
    public:
        GPUResourceCache(GraphicsDevice* device) : GPUObject(device) { }
        ~GPUResourceCache() { Release(); }

        virtual bool IsValid() const override { return true; }
        virtual void Release() override {
            records_.clear();
        }

        std::shared_ptr<T> Get(const std::string& name) const {
            auto found = records_.find(name);
            if (found != records_.end())
                return found->second;
            return nullptr;
        }

        std::shared_ptr<T> Add(const std::string& name, std::shared_ptr<T>& item)
        {
            records_.insert({ name, item });
            return item;
        }

        bool Remove(const std::string& name)
        {
            auto found = records_.find(name);
            if (found != records_.end())
            {
                records_.erase(found);
                return true;
            }
            return false;
        }

    private:
        std::unordered_map<std::string, std::shared_ptr<T> > records_;
    };

    typedef GPUResourceCache<Effect> EffectCache;

}