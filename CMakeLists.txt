
cmake_minimum_required(VERSION 3.8)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_DEBUG_POSTFIX "_d")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(VS_DEBUGGER_WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

set(CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/CMake/Modules/" ${CMAKE_MODULE_PATH})
set(GLVU_ROOT_DIR "${CMAKE_CURRENT_SOURCE_DIR}")

option(GLVU_VK "Use Vulkan for rendering" OFF)
option(GLVU_PICA "Use Citro3D for rendering / 3DS" OFF)
option(GLVU_GLES3 "Use GLES3 mobile rendering" OFF)
option(GLVU_GL "Use OpenGL3.3+ for rendering" OFF)
option(GLVU_DX11 "Use DirectX11 for rendering" ON)
option(GLVU_DX12 "Use Direct3D 12 for rendering" OFF)
option(GLVU_NVAPI "Add NVAPI support" OFF)

if (GLVU_VK OR GLVU_PICA OR GLVU_GLES3 OR GLVU_DX11 OR GLVU_DX12)
    set(GLVU_GL OFF)
endif()

# our primary set of source files that are the interface
set(GLVU_SOURCES
    include/ArrayTexture.h
    include/Batching.h
    include/Blob.h
    include/Buffer.h
    include/Effect.h
    include/Geometry.h
    include/glvu.h
    include/glvu_math.h
    include/GPUResourceCache.h
    include/GraphicsDevice.h
    include/GraphicsDeviceHead.h
    include/LightShadow.h
    include/Material.h
    include/Packing.h
    include/Renderables.h
    include/Renderer.h
    include/RenderGraph.h
    include/RenderScript.h
    include/RenderScript_Types.h
    include/ShaderConstants.h
    include/ShaderCache.h
    include/stb_c_lexer.h
    include/Texture.h
    source/ArrayTexture.cpp
    source/Batching.cpp
    source/Buffer.cpp
    source/Effect.cpp
    source/FormatLoaders.cpp
    source/Geometry.cpp
    source/glvu.cpp
	source/glvu_math.cpp
    source/GraphicsDevice.cpp
    source/GraphicsDeviceHead.cpp
    source/LightShadow.cpp
    source/Material.cpp
    source/Packing.cpp
    source/Renderer.cpp
    source/Renderables.cpp
    source/RenderScript.cpp
    source/RenderScript_Types.cpp
    source/ShaderCache.cpp
    source/Texture.cpp
    include/ECS.h
    source/ECS.cpp
)
# cluster the main sources into a top-level group
source_group("" FILES ${GLVU_SOURCES})

# select implementation specific source files

############# VULKAN #############
if (GLVU_VK)
    add_definitions(-DGLVU_VK)
    
    set(GLVU_VK_SOURCES
        source/Vulkan/VKBuffer.cpp
        source/Vulkan/VKEffect.cpp
        source/Vulkan/VKFrameBuffer.cpp
        source/Vulkan/VKGeometry.cpp
        source/Vulkan/VKGraphicsDevice.cpp
        source/Vulkan/VKGraphicsDeviceHead.cpp
        source/Vulkan/VKMaterial.cpp
        source/Vulkan/VKRenderScript.cpp
        source/Vulkan/VKShaderCache.cpp
        source/Vulkan/VKTexture.cpp
    )
    
    source_group ("Vulkan" FILES ${GLVU_VK_SOURCES})
    list (APPEND GLVU_SOURCES ${GLVU_VK_SOURCES})
    
elseif (GLVU_DX12)

    add_definitions(-DGLVU_D3D12)
    
    set(GLVU_D3D12_SOURCES
        source/DX12/DX12AssistanceTypes.cpp
        source/DX12/DX12AssistanceTypes.h
        source/DX12/DX12DescriptorManagement.cpp
        source/DX12/DX12DescriptorManagement.h
        source/DX12/DX12Buffer.cpp
        source/DX12/DX12FrameBuffer.cpp
        source/DX12/DX12Geometry.cpp
        source/DX12/DX12GraphicsDevice.cpp
        source/DX12/DX12Texture.cpp
        source/DX12/D3D12MemAlloc.cpp
        source/DX12/D3D12MemAlloc.h
        source/DX12/D3D12x.h
        source/DX12/D3D12_L.cpp
        source/DX12/D3D12_L.h
    )
    
    source_group ("D3D12" FILES ${GLVU_D3D12_SOURCES})
    list (APPEND GLVU_SOURCES ${GLVU_D3D12_SOURCES})
    include_directories(source/DX12)
    
elseif (GLVU_PICA)
    add_definitions(-DGLVU_PICA)
    
    set(GLVU_PICA_SOURCES
        source/PICA/PICABuffer.cpp
        source/PICA/PICAFrameBuffer.cpp
        source/PICA/PICAShaderEmulator.cpp
        source/PICA/PICAGraphicsDevice.cpp
        source/PICA/PICAGraphicsDeviceHead.cpp
        source/PICA/PICAMaterial.cpp
        source/PICA/PICARenderScript.cpp
        source/PICA/PICATexture.cpp
        source/PICA/PICACache.h
        source/PICA/PICACache.cpp
    )
    
    source_group ("PICA" FILES ${GLVU_PICA_SOURCES})
    list (APPEND GLVU_SOURCES ${GLVU_PICA_SOURCES})    
    
############# DIRECTX 11 #############
elseif (GLVU_DX11)
    add_definitions(-DGLVU_DX11)
    
    set (GLVU_DX11_SOURCES
        source/DX11/DX11Buffer.cpp
        source/DX11/DX11Effect.cpp
        source/DX11/DX11FrameBuffer.cpp
        source/DX11/DX11Geometry.cpp
        source/DX11/DX11GraphicsDevice.cpp
        source/DX11/DX11GraphicsDeviceHead.cpp
        source/DX11/DX11Material.cpp
        source/DX11/DX11RenderScript.cpp
        source/DX11/DX11StateCache.h
        source/DX11/DX11StateCache.cpp
        source/DX11/DX11Texture.cpp
    )
    source_group("DX11" FILES ${GLVU_DX11_SOURCES})
    list (APPEND GLVU_SOURCES ${GLVU_DX11_SOURCES})
elseif (GLVU_GLES3)
    add_definitions(-DGLVU_GLES3)
    # files handled below (common with GL3.3+)
else()
    add_definitions(-DGLVU_GL)
    # files handled below (common with GLES3)
endif()

############# OPENGL #############
if (GLVU_GLES3 OR GLVU_GL)
    set(GLVU_GL_SOURCES
        include/GLHelpers.h
        source/GL/GLBuffer.cpp
        source/GL/GLEffect.cpp
        source/GL/GLFrameBuffer.cpp
        source/GL/GLGeometry.cpp
        source/GL/GLGraphicsDevice.cpp
        source/GL/GLGraphicsDeviceHead.cpp
        source/GL/GLMaterial.cpp
        source/GL/GLRenderScript.cpp
        source/GL/GLTexture.cpp
    )
    
    source_group ("GL" FILES ${GLVU_GL_SOURCES})
    list (APPEND GLVU_SOURCES ${GLVU_GL_SOURCES})
endif()

if (GLVU_GL OR GLVU_GLES3)
    find_package(OpenGL REQUIRED)
    
    set(GLEW_SOURCES 
        deps/GLEW/GL/glew.c
        deps/GLEW/GL/glewinfo.c
        deps/GLEW/GL/visualinfo.c
        )
    project(GLEW VERSION 1.0 LANGUAGES C)
    add_definitions(-DGLEW_STATIC)
    include_directories(deps deps/GLEW/GL deps/GLEW)
    add_library(GLEW STATIC ${GLEW_SOURCES})
endif()
    
if (GLVU_NVAPI)
	add_definitions(-DGLVU_NVAPI)
endif ()
	
############# TAILING BITS #############

    include_directories(deps deps/Eigen)
    include_directories(include)

    project(GLVU VERSION 1.0 LANGUAGES CXX)
    #add_definitions(-DMATH_SSE2)
    add_library(GLVU STATIC ${GLVU_SOURCES})
    
    add_subdirectory(deps/hjson)

    if (GLVU_GL OR GLVU_GLES3)
        target_link_libraries(GLVU ${OPENGL_LIBRARIES})
        add_dependencies(GLVU GLEW)
    elseif (GLVU_VK)
        add_subdirectory(deps/glslang)
        add_subdirectory(deps/OGLCompilersDLL)
        add_subdirectory(deps/SPIRV)
        add_subdirectory(deps/SPIRV-Cross)
        add_subdirectory(deps/VEZ)
        
        target_link_libraries(GLVU VEZ)
        add_dependencies(GLVU VEZ)
    elseif (GLVU_DX11)
		if (GLVU_NVAPI)
			target_link_libraries(GLVU d3d11 dxgi d3dcompiler dxguid ${CMAKE_CURRENT_SOURCE_DIR}/deps/nvapi/amd64/nvapi64.lib)
		else()
			target_link_libraries(GLVU d3d11 dxgi d3dcompiler dxguid)
		endif()
    endif()

    add_subdirectory(deps/MathGeoLib)
    target_link_libraries(GLVU MathGeoLib hjson)
    add_dependencies(GLVU MathGeoLib)

    if (WIN32)
        add_definitions(-DGLFW_EXPOSE_NATIVE_WIN32)
    elseif (ANDROID)
        add_definitions(-DGLFW_EXPOSE_NATIVE_EGL)
    endif()
    find_package(GLFW)

    add_subdirectory(deps/dearimgui)
    add_subdirectory(helpers)

    add_subdirectory(examples/SampleBase)
    add_subdirectory(examples/FrameworkSample)
    