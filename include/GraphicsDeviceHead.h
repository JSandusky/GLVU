#pragma once

#include "glvu.h"
#include "Texture.h"

namespace GLVU
{

class FrameBuffer;
class GraphicsDeviceHead;

/// For GLFW/SDL is likely the end-user will need to manually activate GL-contexts for multiple window rendering.
/// Vulkan uses swap-chains so this isn't really necessary (so far).
typedef void(*GRAPHICS_HEAD_BEGIN)(GraphicsDeviceHead*, intptr_t);
typedef void(*GRAPHICS_HEAD_END)(GraphicsDeviceHead*, intptr_t);

/// A GraphicsHead 
class GLVU_API GraphicsDeviceHead : public GPUObject
{
public:
    /// Construct with callbacks, the callbacks are optional on Vulkan.
    GraphicsDeviceHead(GraphicsDevice*, uint2 size, intptr_t userData = 0);
#if defined(GLVU_VK)
    GraphicsDeviceHead(GraphicsDevice*, uint2 size, VkSurfaceKHR surface, intptr_t userData = 0);
#elif defined(GLVU_DX11)
	GraphicsDeviceHead(GraphicsDevice*, uint2 size, HWND window, intptr_t userData = 0);
#endif
    virtual ~GraphicsDeviceHead();

    // These callbacks are needed for GL contexts.
    void SetCallbacks(GRAPHICS_HEAD_BEGIN beg, GRAPHICS_HEAD_END end) { beginCallback_ = beg; endCallback_ = end; }

    inline GraphicsDevice* GetDevice() const { return device_; }

    std::shared_ptr<FrameBuffer> GetBackbuffer() const { return backbuffer_; }
    uint2 GetBackbufferSize() const { return { backbufferWidth_, backbufferHeight_ }; }

    void BeginHead();
    void EndHead();

    intptr_t GetUserData() const { return userData_; }
    void SetUserData(intptr_t ptr) { userData_ = ptr; }

    template<typename T>
    T* CastUserData() { return (T*)userData_; }

    void Resize(uint32_t width, uint32_t height);

    virtual void Release() override;

private:
    friend class Renderer;

    void CreateBackbuffers();
    void Flush();
    void FinishRendering();

    std::shared_ptr<FrameBuffer> backbuffer_;
    std::shared_ptr<Texture> backbufferDepth_;

    GRAPHICS_HEAD_BEGIN beginCallback_;
    GRAPHICS_HEAD_END endCallback_;

    uint32_t backbufferWidth_;
    uint32_t backbufferHeight_;
    intptr_t userData_ = 0;
    bool vsync_ = false;

#if defined(GLVU_VK)
public:
    VezSwapchain GetSwapchain() { return swapchain_; }
    VkCommandBuffer GetCommandBuffer();
    std::vector<VkCommandBuffer>& GetBufferQueue() { return bufferQueue_; }

private:
    VkSurfaceKHR surface_;
    VezSwapchain swapchain_;
    std::vector<VkCommandBuffer> bufferQueue_;
#elif defined(GLVU_GL)
    void* glContext_ = nullptr;
    GLuint vao_ = 0;
#elif defined(GLVU_DX11)
	IDXGISwapChain* swapChain_ = nullptr;
	ID3D11Texture2D* swapChainBuffer_ = nullptr;
    HWND wnd_ = 0;
#endif
};

}