#include "Geometry.h"

#include "GraphicsDevice.h"

using namespace std;

namespace GLVU
{

//****************************************************************************
//
//  Function:   GeometryLayout::IsValid
//
//  Purpose:    Utility.
//
//  Return:     True if probably valid.
//
//****************************************************************************
bool GeometryLayout::IsValid() const { return vertexObject_ != 0; }

//****************************************************************************
//
//  Function:   GeometryLayout::Release
//
//  Purpose:    Destroy the vertex-input format.
//
//****************************************************************************
void GeometryLayout::Release()
{
    if (vertexObject_)
        vezDestroyVertexInputFormat(device_->GetVKDevice(), vertexObject_);
    vertexObject_ = 0;
}

//****************************************************************************
//
//  Function:   GeometryLayout::Bind
//
//  Purpose:    Sets the vertex-layout in a command-buffer.
//              If this layout's vertex-layout-object doesn't exist then
//              it will be created at this time.
//
//****************************************************************************
void GeometryLayout::Bind(Geometry* forGeo, const vector<shared_ptr<Buffer>>& extraBuffers, bool instanceDataOnly)
{
    if (vertexObject_ == 0)
    {
        vector<VkVertexInputAttributeDescription> attr;
        vector<VkVertexInputBindingDescription> bindings;

        for (uint32_t i = 0; i < vertexDataCount_; ++i)
        {
            auto& data = vertexData_[i];
            VkVertexInputAttributeDescription desc = {};
            desc.offset = data.offset_;
            desc.location = i;
            desc.binding = data.bufferSlot_;
            switch (data.type_)
            {
            case VDT_FLOAT:
                if (data.elementCount == 4)
                    desc.format = VK_FORMAT_R32G32B32A32_SFLOAT;
                else if (data.elementCount == 3)
                    desc.format = VK_FORMAT_R32G32B32_SFLOAT;
                else if (data.elementCount == 2)
                    desc.format = VK_FORMAT_R32G32_SFLOAT;
                else
                    desc.format = VK_FORMAT_R32_SFLOAT;
                break;
            case VDT_UBYTE:
                if (data.elementCount == 4)
                    desc.format = data.normalized_ ? VK_FORMAT_R8G8B8A8_UNORM : VK_FORMAT_R8G8B8A8_UINT;
                else if (data.elementCount == 3)
                    desc.format = data.normalized_ ? VK_FORMAT_R8G8B8_UNORM : VK_FORMAT_R8G8B8_UINT;
                else if (data.elementCount == 2)
                    desc.format = data.normalized_ ? VK_FORMAT_R8G8_UNORM : VK_FORMAT_R8G8_UINT;
                else
                    desc.format = data.normalized_ ? VK_FORMAT_R8_UNORM : VK_FORMAT_R8_UINT;
                break;
            case VDT_UINT:
                if (data.elementCount == 4)
                    desc.format = VK_FORMAT_R32G32B32A32_UINT;
                else if (data.elementCount == 3)
                    desc.format = VK_FORMAT_R32G32B32_UINT;
                else if (data.elementCount == 2)
                    desc.format = VK_FORMAT_R32G32_UINT;
                else
                    desc.format = VK_FORMAT_R32_UINT;
                break;
            }

            attr.push_back(desc);
        }

        bool touched[16];
        memset(touched, 0, sizeof(touched));

        for (uint32_t i = 0; i < vertexDataCount_; ++i)
        {
            auto& data = vertexData_[i];
            if (touched[data.bufferSlot_])
                continue;
            VkVertexInputBindingDescription desc = {};
            desc.binding = data.bufferSlot_;
            desc.inputRate = data.instanceStride_ ? VK_VERTEX_INPUT_RATE_INSTANCE : VK_VERTEX_INPUT_RATE_VERTEX;
            desc.stride = data.stride_;

            bindings.push_back(desc);

            touched[data.bufferSlot_] = true;
        }

        VezVertexInputFormatCreateInfo vertexInfo = {};
        vertexInfo.vertexAttributeDescriptionCount = (uint32_t)attr.size();
        vertexInfo.pVertexAttributeDescriptions = attr.data();
        vertexInfo.vertexBindingDescriptionCount = (uint32_t)bindings.size();
        vertexInfo.pVertexBindingDescriptions = bindings.data();
        auto result = vezCreateVertexInputFormat(device_->GetVKDevice(), &vertexInfo, &vertexObject_);
        if (result != VK_SUCCESS)
        {
            // is this even possible?
            device_->LogMessage("Failed to create Vertex Layout", GLVU_ERROR);
        }
    }

    assert(vertexObject_);
    vezCmdSetVertexInputFormat(vertexObject_);
}

}