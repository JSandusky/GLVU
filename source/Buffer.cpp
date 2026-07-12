//****************************************************************************
//
//  File:       Buffer.cpp
//  License:    MIT
//  Project:    GLVU
//
//****************************************************************************

#include "Buffer.h"

#include "GraphicsDevice.h"

namespace GLVU
{

//****************************************************************************
//
//  Function:   Buffer::SetSharedData
//
//  Purpose:    Takes into account the GPUs alignment rules for setting data
//              in a buffer such that in will be placed into a relevant location
//
//              Unsafe.
//
//****************************************************************************
void Buffer::SetSharedData(uint32_t index, void* data, uint32_t offset, uint32_t size, uint32_t blockSize)
{
    const uint32_t uboAlign = device_->GetGPUFeatures().minUBOAlignment_;
    if (blockSize % uboAlign != 0)
        blockSize += uboAlign - (blockSize % uboAlign);

    SetSubData(data, index * blockSize + offset, size);
}

//****************************************************************************
//
//  Function:   Buffer::SetLayout
//
//  Purpose:    Sets the geometry layout information for this buffer.
//              This only ever used if the buffer is a VertexBuffer, otherwise
//              it'll never be touched.
//
//  TODO:       Consider using a single record layout for index buffers?
//
//****************************************************************************
void Buffer::SetLayout(const std::shared_ptr<GeometryLayout>& layout)
{
    layout_ = layout;
}
    
}
