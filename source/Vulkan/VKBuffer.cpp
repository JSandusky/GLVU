#include "Buffer.h"

#include <GraphicsDevice.h>

namespace GLVU
{

const char* VK_ErrorName(uint32_t code)
{
#define HANDLE_ERROR(VALUE) if (code == VALUE) return #VALUE;
    HANDLE_ERROR(VK_SUCCESS);
    HANDLE_ERROR(VK_NOT_READY);
    HANDLE_ERROR(VK_TIMEOUT);
    HANDLE_ERROR(VK_EVENT_SET);
    HANDLE_ERROR(VK_EVENT_RESET);
    HANDLE_ERROR(VK_INCOMPLETE);
    HANDLE_ERROR(VK_ERROR_OUT_OF_HOST_MEMORY);
    HANDLE_ERROR(VK_ERROR_OUT_OF_DEVICE_MEMORY);
    HANDLE_ERROR(VK_ERROR_INITIALIZATION_FAILED);
    HANDLE_ERROR(VK_ERROR_DEVICE_LOST);
    HANDLE_ERROR(VK_ERROR_MEMORY_MAP_FAILED);
    HANDLE_ERROR(VK_ERROR_LAYER_NOT_PRESENT);
    HANDLE_ERROR(VK_ERROR_EXTENSION_NOT_PRESENT);
    HANDLE_ERROR(VK_ERROR_FEATURE_NOT_PRESENT);
    HANDLE_ERROR(VK_ERROR_INCOMPATIBLE_DRIVER);
    HANDLE_ERROR(VK_ERROR_TOO_MANY_OBJECTS);
    HANDLE_ERROR(VK_ERROR_FORMAT_NOT_SUPPORTED);
    HANDLE_ERROR(VK_ERROR_FRAGMENTED_POOL);
    HANDLE_ERROR(VK_ERROR_OUT_OF_POOL_MEMORY);
    HANDLE_ERROR(VK_ERROR_INVALID_EXTERNAL_HANDLE);
    HANDLE_ERROR(VK_ERROR_SURFACE_LOST_KHR);
    HANDLE_ERROR(VK_ERROR_NATIVE_WINDOW_IN_USE_KHR);
    HANDLE_ERROR(VK_SUBOPTIMAL_KHR);
    HANDLE_ERROR(VK_ERROR_OUT_OF_DATE_KHR);
    HANDLE_ERROR(VK_ERROR_INCOMPATIBLE_DISPLAY_KHR);
    HANDLE_ERROR(VK_ERROR_VALIDATION_FAILED_EXT);
    HANDLE_ERROR(VK_ERROR_INVALID_SHADER_NV);
    HANDLE_ERROR(VK_ERROR_INVALID_DRM_FORMAT_MODIFIER_PLANE_LAYOUT_EXT);
    HANDLE_ERROR(VK_ERROR_FRAGMENTATION_EXT);
    HANDLE_ERROR(VK_ERROR_NOT_PERMITTED_EXT);
    HANDLE_ERROR(VK_ERROR_INVALID_DEVICE_ADDRESS_EXT);
    HANDLE_ERROR(VK_ERROR_FULL_SCREEN_EXCLUSIVE_MODE_LOST_EXT);
    HANDLE_ERROR(VK_ERROR_OUT_OF_POOL_MEMORY_KHR);
    HANDLE_ERROR(VK_ERROR_INVALID_EXTERNAL_HANDLE_KHR);
    return "Unknown error";
}

unsigned vk_BufferUsage(BufferKind kind)
{
    switch (kind)
    {
    case VertexBufferObject:
        return VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    case IndexBufferObject:
        return VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
    case UniformBufferObject:
        return VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
    case ShaderDataBufferObject:
        return VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    case IndirectArgsBufferObject:
        return VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT;
    }
    return VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
}

//****************************************************************************
//
//  Function:   Buffer::Buffer
//
//  Purpose:    Construct, zero init
//
//****************************************************************************
Buffer::Buffer(GraphicsDevice* device, BufferKind kind) : 
    GPUObject(device),
    size_(0),
    shadowed_(false),
    shadowData_(nullptr),
    buffer_(0),
    kind_(kind),
    tags_(0)
{
}

//****************************************************************************
//
//  Function:   Buffer::~Buffer
//
//  Purpose:    Destruct
//
//****************************************************************************
Buffer::~Buffer()
{

}

//****************************************************************************
//
//  Function:   Buffer::Release
//
//  Purpose:    Destroy the buffer object if needed
//
//****************************************************************************
void Buffer::Release()
{
    if (buffer_)
        vezDestroyBuffer(device_->GetVKDevice(), buffer_);
    
    buffer_ = 0;
    shadowData_.reset();
    size_ = 0;
}

//****************************************************************************
//
//  Function:   Buffer::IsValid
//
//  Purpose:    Utility
//
//  Return:     True if probably valid
//
//****************************************************************************
bool Buffer::IsValid() const
{
    return buffer_ != 0;
}

//****************************************************************************
//
//  Function:   Buffer::SetSize
//
//  Purpose:    Allocates(reallocates) the buffer as necessary
//
//****************************************************************************
void Buffer::SetSize(uint32_t size)
{
    if (buffer_)
    {
        vezDestroyBuffer(device_->GetVKDevice(), buffer_);
        buffer_ = 0;
    }

    VezBufferCreateInfo info = {};
    info.size = size;
    info.pQueueFamilyIndices = 0;
    switch (kind_)
    {
    case VertexBufferObject:
        info.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        break;
    case IndexBufferObject:
        info.usage = VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        break;
    case UniformBufferObject:
        info.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        SetTag(BufferTag_Dynamic);
        break;
    case ShaderDataBufferObject:
        info.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        break;
    }

    auto result = vezCreateBuffer(device_->GetVKDevice(), HasTag(BufferTag_Dynamic) ? VEZ_MEMORY_CPU_TO_GPU : VEZ_MEMORY_GPU_ONLY, &info, &buffer_);
    if (result == VK_SUCCESS)
    {
        size_ = size;

        if (shadowed_)
        {
            shadowData_.reset(new unsigned char[size]);
            memset(shadowData_.get(), 0, size);
        }
        else if (shadowData_)
            shadowData_.reset();
    }
    else
    {
        device_->LogFormat(GLVU_ERROR, "Failed Buffer::SetSize, %s", VK_ErrorName(result));
        assert(0);
        buffer_ = 0;
        size_ = 0;
    }
}

//****************************************************************************
//
//  Function:   Buffer::SetData
//
//  Purpose:    Copies data into the buffer. Buffer will be resized larger if
//              necessaary to fit the data (that means realloc)
//
//****************************************************************************
void Buffer::SetData(void* data, uint32_t size)
{
    if (size_ < size)
    {
        if (buffer_)
        {
            vezDestroyBuffer(device_->GetVKDevice(), buffer_);
            buffer_ = 0;
        }

        VezBufferCreateInfo info = { };
        info.size = size;
        info.pQueueFamilyIndices = 0;
        switch (kind_)
        {
        case VertexBufferObject:
            info.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
            break;
        case IndexBufferObject:
            info.usage = VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
            break;
        case UniformBufferObject:
            info.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
            break;
        case ShaderDataBufferObject:
            info.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
            break;
        }

        auto result = vezCreateBuffer(device_->GetVKDevice(), HasTag(BufferTag_Dynamic) ? VEZ_MEMORY_CPU_TO_GPU : VEZ_MEMORY_GPU_ONLY, &info, &buffer_);
        if (result == VK_SUCCESS)
            size_ = size;
        else
        {
            device_->LogFormat(GLVU_ERROR, "Failed Buffer::SetData, %s", VK_ErrorName(result));
            buffer_ = 0;
        }
    }

    if (data != nullptr && buffer_)
    {
        assert(size <= size_);
        VkResult result = VK_SUCCESS;
        
        if (!HasTag(BufferTag_Dynamic))
            result = vezBufferSubData(device_->GetVKDevice(), buffer_, 0, size, data);
        else
        {
            // CPU -> GPU is way faster if we map
            void* dataPtr = nullptr;
            result = vezMapBuffer(device_->GetVKDevice(), buffer_, 0, size, &dataPtr);
            memcpy(dataPtr, data, size);
            vezUnmapBuffer(device_->GetVKDevice(), buffer_);
        }

        if (result == VK_SUCCESS)
        {
            if (shadowed_)
            {
                if (shadowData_.get() != data)
                {
                    shadowData_.reset(new unsigned char[size]);
                    memcpy(shadowData_.get(), data, size);
                }
            }
            else if (shadowData_)
                shadowData_.reset();

            size_ = size;
        }
    }
}

//****************************************************************************
//
//  Function:   Buffer::SetSubData
//
//  Purpose:    Transfers partial data into a buffer, does not attempt
//              any allocation or reallocation because the total
//              buffer size cannot be known from the given data
//
//****************************************************************************
void Buffer::SetSubData(void* data, uint32_t offset, uint32_t size)
{
    assert(buffer_ && size_ >= offset + size);
    if (buffer_ == 0)
        return;

    if (!HasTag(BufferTag_Dynamic))
    {
        auto result = vezBufferSubData(device_->GetVKDevice(), buffer_, offset, size, data);
        if (result != VK_SUCCESS)
            device_->LogFormat(GLVU_ERROR, "Buffer::SetSubData failure in vezBufferSubData, %s", VK_ErrorName(result));
    }
    else
    {
        void* dataPtr = nullptr;
        auto result = vezMapBuffer(device_->GetVKDevice(), buffer_, offset, size, &dataPtr);
        if (result == VK_SUCCESS)
        {
            memcpy(dataPtr, data, size);
            vezUnmapBuffer(device_->GetVKDevice(), buffer_);
        }
        else
            device_->LogFormat(GLVU_ERROR, "Buffer::SetSubData failure in vezMapBuffer, %s", VK_ErrorName(result));
    }

    if (shadowed_ && shadowData_.get())
        memcpy(shadowData_.get() + offset, data, size);
}

void* Buffer::Map()
{
    assert(HasTag(BufferTag_Dynamic));
    if (!HasTag(BufferTag_Dynamic))
    {
        GetDevice()->LogMessage("UNSUPPORTED: Attempted to map a Buffer that is not marked as dynamic.", GLVU_WARNING);
        return nullptr;
    }
    if (buffer_ == 0)
        return nullptr;

    void* buffer = nullptr;
    vezMapBuffer(device_->GetVKDevice(), buffer_, 0, size_, &buffer);
    return buffer;
}

void Buffer::Unmap()
{
    if (!HasTag(BufferTag_Dynamic))
        return;
    if (buffer_ == 0)
        return;

    vezUnmapBuffer(device_->GetVKDevice(), buffer_);
}

}