//****************************************************************************
//
//  File:       D3D12Geometry.cpp
//  License:    MIT
//  Project:    GLVU
//
//****************************************************************************

#include "Geometry.h"

namespace GLVU
{

//****************************************************************************
//
//  Function:   GeometryLayout::IsValid
//
//  Purpose:    Makes a reasonable determination if this layout is valid.
//
//****************************************************************************
bool GeometryLayout::IsValid() const
{
    return !elementDesc_.empty();
}

//****************************************************************************
//
//  Function:   GeometryLayout::Release
//
//  Purpose:    Disposes of GPU objects held, does nothing for D3D12 where
//              the input layout is just a configuration structure to guide
//              PSO construction.
//
//****************************************************************************
void GeometryLayout::Release()
{
    // nothing to do on D3D12
}

//****************************************************************************
//
//  Function:   GeometryLayout::Bind
//
//  Purpose:    Turns the meta-description into a concrete D3D12 input layout
//              then passes that off to the device so that it knows the layout
//              for PSO selection/creation.
//
//  TODO:       What to do about an idiot passign R8_G8_B8 or R16_G16_B16
//
//****************************************************************************
void GeometryLayout::Bind(Geometry* forGeo, const std::vector<std::shared_ptr<Buffer>>& extraBuffers, bool instanceDataOnly)
{
    if (elementDesc_.empty())
    {
        uint32_t instDataCt = 0;
        for (uint32_t i = 0; i < vertexDataCount_; ++i)
        {
            const auto& d = vertexData_[i];

            D3D12_INPUT_ELEMENT_DESC desc = { };
            desc.InputSlot = d.bufferSlot_;
            desc.AlignedByteOffset = d.offset_;
            desc.InputSlotClass = d.perInstance_ ? D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA : D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA;
            desc.InstanceDataStepRate = d.perInstance_ ? 1 : 0;
            
            switch (d.type_)
            {
            case VDT_FLOAT: {
                if (d.elementCount == 4)
                    desc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
                else if (d.elementCount == 3)
                    desc.Format = DXGI_FORMAT_R32G32B32_FLOAT;
                else if (d.elementCount == 2)
                    desc.Format = DXGI_FORMAT_R32G32_FLOAT;
                else
                    desc.Format = DXGI_FORMAT_R32_FLOAT;
            } break;
            case VDT_HALF: {
                if (d.elementCount == 4)
                    desc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
                else if (d.elementCount == 2)
                    desc.Format = DXGI_FORMAT_R16G16_FLOAT;
                else
                    desc.Format = DXGI_FORMAT_R16_FLOAT;
            } break;
            case VDT_UINT: {
                if (d.elementCount == 4)
                    desc.Format = DXGI_FORMAT_R32G32B32A32_UINT;
                else if (d.elementCount == 3)
                    desc.Format = DXGI_FORMAT_R32G32B32_UINT;
                else if (d.elementCount == 2)
                    desc.Format = DXGI_FORMAT_R32G32_UINT;
                else
                    desc.Format = DXGI_FORMAT_R32_UINT;
            } break;
            case VDT_UBYTE: {
                if (d.elementCount == 4)
                    desc.Format = d.normalized_ ? DXGI_FORMAT_R8G8B8A8_UNORM : DXGI_FORMAT_R8G8B8A8_UINT;
                else if (d.elementCount == 2)
                    desc.Format = d.normalized_ ? DXGI_FORMAT_R8G8_UNORM : DXGI_FORMAT_R8G8_UINT;
                else
                    desc.Format = d.normalized_ ? DXGI_FORMAT_R8_UNORM : DXGI_FORMAT_R8_UINT;
            } break;
            }

            // see glvu.h for the table of enum VertexAttribute
            static uint32_t semanticIndices[] = {
                0, 0, 0,
                0, 1, 2,
                0, 1, 2,
                0, 1, 2,
                0, 1, 2,
                0, 0, 0,
                0 //unkown
            };
            static const char* semanticNames[] = {
                "POSITION", "NORMAL", "TANGENT",
                "TEXCOORD", "TEXCOORD", "TEXCOORD",
                "COORD", "COORD", "COORD",
                "SINGLE","SINGLE","SINGLE",
                "COLOR","COLOR","COLOR",
                "INSTANCE","BLENDINDICES","BLENDWEIGHTS",
                "UNKNOWN"
            };

            desc.SemanticIndex = semanticIndices[d.attribute_];
            desc.SemanticName = semanticNames[d.attribute_];

            // HACK
            if (d.attribute_ == VA_INSTANCE)
                desc.SemanticIndex = instDataCt++;

            elementDesc_.push_back(desc);
        }
    }
}

}