#include "ShaderCache.h"

#include "glvu_math.h"

#include "Effect.h"
#include "GraphicsDevice.h"

#include <algorithm>
#include <numeric>
#include <unordered_set>

using namespace std;

#if defined(GLVU_DX11)
	#define SHADER_LANG "HLSL"
	#define SHADER_COOK_DIR ""
#elif defined(GLVU_GLES3)
	#define SHADER_LANG "GLSL"
	#define SHADER_COOK_DIR "essl/"
#elif defined(GLVU_GL)
	#define SHADER_LANG "GLSL"
	#define SHADER_COOK_DIR "glsl/"
#elif defined(GLVU_VK)
	#define SHADER_LANG "GLSL"
	#define SHADER_COOK_DIR "glsl_vk/"
#endif

namespace GLVU
{

//****************************************************************************
//
//  Function:   ShaderCache::ShaderCache
//
//  Purpose:    Construct.
//
//****************************************************************************
ShaderCache::ShaderCache(GraphicsDevice* device) :
	GPUObject(device)
{

}

//****************************************************************************
//
//  Function:   ShaderCache::~ShaderCache
//
//  Purpose:    Destruct, free caches.
//
//****************************************************************************
ShaderCache::~ShaderCache()
{
	Release();
}

//****************************************************************************
//
//  Function:   ShaderCache::Release
//
//  Purpose:    Clears the cache tables, freeing the stored shared_ptrs
//
//****************************************************************************
void ShaderCache::Release()
{
	for (auto& table : caches_)
		table.cache_.clear();

	codeCache_.clear();
}

//****************************************************************************
//
//  Function:   ShaderCache::GetShader
//
//  Purpose:    Tries to acquire a shader of the given stage/type from a filename
//              and list of #preprocessor defines.
//
//              Responsible for dealing with caching the base source-code (raw file-data),
//              embedding #includes and prefixing #version as well as preprocessor defs.
//
//  Return:     If successfully than a new or existing shader, null otherwise
//
//****************************************************************************
shared_ptr<Shader> ShaderCache::GetShader(ShaderType type, const char* name, const vector<string>& shaderDefines)
{
	auto& table = caches_[type];

	auto sortedDefines = shaderDefines;
	std::sort(sortedDefines.begin(), sortedDefines.end());

	const uint32_t nameCRC = CRC32::Calculate(name, strlen(name));
	uint32_t defineCRC = 0;
	for (const auto& def : sortedDefines)
		defineCRC = CRC32::CalcAdd(defineCRC, def.c_str(), def.length());

// CHECK FOR EXISTING RECORD
	{
		auto iter = table.cache_.find(name);
		if (iter != table.cache_.end())
		{
			auto ct = table.cache_.count(name);
			for (int i = 0; i < ct; ++i, ++iter)
			{
				if (iter->second.definesHash_ == defineCRC && iter->second.shader_->GetStage() == type)
					return iter->second.shader_;
			}
		}
	}

// QUERY FOR IT IN THE COOK DIRECTORY
#if 0
	{		
		auto resData = device_->GetResourceData(Resource_Shader, name);
		if (resData.data_)
		{
			string code = string(resData.data_, resData.size_);
			Record rec = {
				sortedDefines, 
				shared_ptr<Shader>(new Shader(device_, name, type, SCT_GLSL, code, { })),
				defineCRC,
				nameCRC
			};
			table.cache_.insert({ name, rec });
			return rec.shader_;
		}
	}
#endif

// DIDN"T FIND IT
	string code;
	auto foundCode = codeCache_.find(name);
	if (foundCode == codeCache_.end())
	{
		auto blob = device_->GetResourceData(Resource_Shader, name);
		code = string(blob.data_, blob.size_);

		// unused currently ... need to work out how best to get this information
		static auto getLineNumber = [](const string& src, size_t pos) -> uint32_t {
			uint32_t line;
			for (size_t i = 0; i < pos; ++i)
			{
				if (src[i] == '\n')
					++line;
				else if (src[i] == '#')
					--line;
			}
		};

		code = ProcessIncludes(code);

		codeCache_.insert({ name, code });
	}
	else
		code = foundCode->second;

	if (!code.empty())
	{
		auto definesStr = accumulate(std::begin(sortedDefines), std::end(sortedDefines), string(), [](const string& l, const string& r) {
			return l.empty() ? "#define " + r : l + "\n#define " + r;
		});
#if defined(GLVU_GL) || defined(GLVU_GLES3)
		definesStr += "\n#define OPENGL";
#elif defined(GLVU_GLES3)
		definesStr += "\n#define GLES";
#else
		// V-EZ/GLSLANG includes this thing, which is objectively fucking terrible of them to do.
		//definesStr += "\n#define VULKAN";
#endif

#if defined(GLVU_GL) || defined(GLVU_VK)
		auto joinedCode = "#version 420\n" + definesStr + "\n" + code;
#elif defined(GLVU_GLES3)
		auto joinedCode = "#version 300 es\n" + definesStr + "\n" + code;
#elif defined(GLVU_DX11)
		auto joinedCode = definesStr + "\n" + code;
#endif

		Record rec = {
			sortedDefines, 
			shared_ptr<Shader>(new Shader(device_, name, type, SCT_GLSL, joinedCode, sortedDefines)),
			CRC32::Calculate(definesStr.c_str(), definesStr.length()),
			CRC32::Calculate(name, strlen(name))
		};
		table.cache_.insert({ name, rec });
		return rec.shader_;
	}

	return nullptr;
}

//****************************************************************************
//
//  Function:   ShaderCache::ProcessIncludes
//
//  Purpose:    Runs through the continuous handling of replacing #include statements
//
//	Return:		The resolved code with #include directives replaced.
//
//****************************************************************************
string ShaderCache::ProcessIncludes(const std::string& inputCode)
{
	// only include each header once, if encountered again then we'll just erase the whole #include statement
	unordered_set<string> includedHeaders;

	string code = inputCode;
	auto incIndex = code.find("#include");
	while (incIndex != string::npos)
	{
		// #include is 8 characters in length, don't want sizeof("#include") because that includes the \0 terminal
		auto p = incIndex + 8;
		while (code[p++] != '"')
			continue;

		string header;
		while (code[p] != '"')
			header += code[p++];
		p++; // bypass the closing '"'

		if (includedHeaders.find(header) == includedHeaders.end())
		{
			auto foundHeader = device_->GetResourceData(Resource_Shader, header.c_str());
			if (foundHeader.size_ > 0)
			{
				code = code.replace(incIndex, p - incIndex, string(foundHeader.data_, foundHeader.size_));
				includedHeaders.insert(header);
			}
			else
			{
				device_->LogFormat(GLVU_WARNING, "Unable to find " SHADER_LANG " header: %s", header.c_str());
				code = code.replace(incIndex, p - incIndex, "");
			}
		}
		else
			code = code.replace(incIndex, p - incIndex, "");

		incIndex = code.find("#include");
	}

	return code;
}

//****************************************************************************
//
//  Function:   ShaderCache::PrintCacheInfo
//
//  Purpose:    Prints a list of shaders and their defines into a string.
//
//	Return:		Summary of all shaders in the cache records.
//
//****************************************************************************
std::string ShaderCache::PrintCacheInfo() const
{
	std::stringstream ss;

	for (uint32_t i = 0; i < COUNT_SHADER_TYPE; ++i)
	{
		// Don't print an empty cache
		if (caches_[i].cache_.empty())
			continue;

		ss << ShaderTypeToString((ShaderType)i) << "\r\n";
		for (auto& rec : caches_[i].cache_)
		{
			ss << "  - " << rec.second.shader_->GetName() << "\r\n";

			// Only print defines if they're present
			if (!rec.second.defines_.empty())
			{
				ss << "        " << accumulate(std::begin(rec.second.defines_), std::end(rec.second.defines_), string(), [](const string& l, const string& r) {
					return l.empty() ? r : l + " " + r;
				});
				ss << "\r\n";
			}
		}
	}
	return ss.str();
}

//****************************************************************************
//
//  Function:   ShaderCache::LogCacheInfo
//
//  Purpose:    Writes the results of PrintCacheInfo to the log as an info message.
//
//****************************************************************************
void ShaderCache::LogCacheInfo() const
{
	auto cacheInfo = PrintCacheInfo();
	device_->LogMessage(cacheInfo.c_str(), GLVU_INFO);
}

}
