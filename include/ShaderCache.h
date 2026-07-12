#pragma once

#include "glvu.h"

#include <map>
#include <unordered_map>
#include <vector>

namespace GLVU
{

	class GraphicsDevice;
	class Shader;

	class GLVU_API ShaderCache : public GPUObject
	{
	public:
		ShaderCache(GraphicsDevice* device);
		~ShaderCache();

		virtual bool IsValid() const override { return true; }
		virtual void Release() override;

		std::shared_ptr<Shader> GetShader(ShaderType type, const char* name, const std::vector<std::string>& defines);

		std::string PrintCacheInfo() const;
		void LogCacheInfo() const;

	private:
		std::string ProcessIncludes(const std::string&);

		struct Record
		{
			std::vector<std::string> defines_;
			std::shared_ptr<Shader> shader_;
			uint32_t definesHash_;
			uint32_t nameHash_;
		};

		struct CacheTable
		{
			std::multimap<std::string, Record> cache_;
		};
		/// Indexed by shader-type.
		CacheTable caches_[6];
		std::unordered_map<std::string, std::string> codeCache_;
	};

}