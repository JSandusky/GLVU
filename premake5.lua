
newoption 
{
    trigger = "basedir",
    value = "",
    description = "Sets the base-dir to run the premake code-against"
}

newoption
{
    trigger = "gfxapi",
    value = "vk",
    description = "Selects 3D API to use",
    allowed = {
        { "gl", "OpenGL3.3+" },
        { "vk", "Vulkan" },
        { "gles3", "OpenGL ES 3.0" },
    }
}

if not _OPTIONS["gfxapi"] then
   _OPTIONS["gfxapi"] = "gl"
end

local baseDir = _OPTIONS["basedir"] or "";

function setupGLFW()
    includedirs { baseDir .. "deps/GLFWBin/include" }
    libdirs { baseDir .. "deps/GLFWBin/lib-vc2017" }
    links { "glfw3" }
end

function setupGLEW()
    defines { "GLEW_STATIC" }
    includedirs { baseDir .. "deps/GLEW" }
    links { "GLEW" }
end

function setupOpenGL()
    defines { "GLVU_GL" }
    links  { "OpenGL32" }
    filter { "system:windows" }
        links  { 
            "OpenGL32",
            "glu32"
    }
        
    filter { "system:not windows" }
        links { "GL" }
        
    filter { }
end

function setupVulkan()
    defines { "GLVU_VK" }
    includedirs { "$(VULKAN_SDK)/include" }
    links { "$(VULKAN_SDK)/lib/vulkan-1.lib" }
end

workspace "GLVU"
    location "Generated"
    language "C++"
    cppdialect "C++17"
    architecture "x64"
    
    configurations 
    {
        "Debug",
        "Release"
    }
    
    filter "configurations:Debug"
        defines { "DEBUG", "WIN32", "WIN64" }
        symbols "On"
        
    filter "configurations:Release"
        defines { "NDEBUG", "WIN32", "WIN64" }
        symbols "On"
        optimize "On"
        
    filter { }
    
    project "GLVU"
        kind "StaticLib"
        
        files {
            baseDir .. "source/*.c",
            baseDir .. "source/*.cpp",
            baseDir .. "include/*.h"
        }
        
        includedirs { 
            baseDir .. "deps",
            baseDir .. "include" 
        }
        
        if _OPTIONS["gfxapi"] == "gl" then
            defines { "GLVU_GL" }
            setupGLEW()
            files { 
                baseDir .. "source/GL/*.cpp",
                baseDir .. "source/GL/*.h" 
            }
        elseif _OPTIONS["gfxapi"] == "gles3" then
            setupGLEW()
            defines { "GLVU_GLES3" }
            files { 
                baseDir .. "source/GL/*.cpp",
                baseDir .. "source/GL/*.h" 
            }
        elseif _OPTIONS["gfxapi"] == "vk" then
            setupVulkan()
            defines { "GLVU_VK" }
            files { baseDir .. "source/Vulkan/*.cpp" }
        end
       
    if _OPTIONS["gfxapi"] ~= "vk" then
        project "GLEW"
            language "C++"
            kind "StaticLib"
            location "Generated"
            targetdir "bin/Build/%{cfg.buildcfg}"
            characterset "MBCS"
            
            defines { "GLEW_STATIC" }
            includedirs { baseDir .. "Deps/GLEW" }
            
            files {
                baseDir .. "Deps/GLEW/**.h",
                baseDir .. "Deps/GLEW/**.c"
            }
    end
        
