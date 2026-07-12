//****************************************************************************
//
//  File:       GLGeometry.cpp
//  License:    MIT
//  Project:    GLVU
//  Contents:   Manages the GLState for VAOs which is a little `special`
//              since GLVU uses a single continuously remapped VAO.
//
//****************************************************************************

#include "Geometry.h"

#include "GraphicsDevice.h"

#include <set>

namespace GLVU
{

//****************************************************************************
//
//  Function:   GeometryLayout::IsValid
//
//  Purpose:    Nothing to do on GL
//
//****************************************************************************
bool GeometryLayout::IsValid() const
{
    // because a single master VAO is used this doesn't matter
    return true;
}

//****************************************************************************
//
//  Function:   GeometryLayout::Release
//
//  Purpose:    Nothing to do on GL.
//
//****************************************************************************
void GeometryLayout::Release()
{
    // do nothing, we keep a master VAO that is constantly updated
}

//****************************************************************************
//
//  Function:   GeometryLayout::Bind
//
//  Purpose:    Performs some gymnastics to make the minimum number of calls to
//              to finangle the global VAO state such that the given geometry
//              will be properly configured.
//
//              This function is an asshole.
//
//****************************************************************************
void GeometryLayout::Bind(Geometry* forGeo, const std::vector<std::shared_ptr<Buffer>>& extraBuffers, bool instanceDataOnly)
{
    auto& glState = device_->GetGLState();

    std::set<GLint> buffersBound;

    if (instanceDataOnly)
    {
        bool didBind = false;
        for (uint32_t i = 0; i < vertexDataCount_; ++i)
        {
            if (vertexData_[i].instanceStride_)
            {
                const auto& data = vertexData_[i];

                glEnableVertexAttribArray(i);
                if (!didBind)
                    glBindBuffer(GL_ARRAY_BUFFER, extraBuffers[data.bufferSlot_ - forGeo->vertexBuffers_.size()]->GetGPUObject());
                didBind = true;

                switch (data.type_)
                {
                case VDT_FLOAT:
                    glVertexAttribPointer(i, data.elementCount, GL_FLOAT, data.normalized_ ? GL_TRUE : GL_FALSE, data.stride_, (void*)data.offset_);
                    break;
                case VDT_UBYTE:
                    glVertexAttribPointer(i, data.elementCount, GL_UNSIGNED_BYTE, data.normalized_ ? GL_TRUE : GL_FALSE, data.stride_, (void*)data.offset_);
                    break;
                case VDT_UINT:
                    glVertexAttribPointer(i, data.elementCount, GL_UNSIGNED_INT, data.normalized_ ? GL_TRUE : GL_FALSE, data.stride_, (void*)data.offset_);
                    break;
                }

                glVertexAttribDivisor(i, data.instanceStride_);

                glState.vao_ActiveStates[i] = true;
                glState.vao_Instanced[i] = true;
            }
        }
        return;
    }

    bool oldState[16];
    memcpy(oldState, glState.vao_ActiveStates, sizeof(glState.vao_ActiveStates));

    bool touched[16];
    memset(touched, 0, sizeof(touched));

    GLuint lastBuffer = 0;
    for (uint32_t i = 0; i < vertexDataCount_; ++i)
    {
        const auto& data = vertexData_[i];

        if (!glState.vao_ActiveStates[i])
            glEnableVertexAttribArray(i);

        if (data.bufferSlot_ > forGeo->vertexBuffers_.size() - 1)
        {
            if (lastBuffer != extraBuffers[data.bufferSlot_ - forGeo->vertexBuffers_.size()]->GetGPUObject())
                glBindBuffer(GL_ARRAY_BUFFER, extraBuffers[data.bufferSlot_ - forGeo->vertexBuffers_.size()]->GetGPUObject());
            lastBuffer = extraBuffers[data.bufferSlot_ - forGeo->vertexBuffers_.size()]->GetGPUObject();
        }
        else if (lastBuffer != forGeo->vertexBuffers_[data.bufferSlot_]->GetGPUObject())
        {
            glBindBuffer(GL_ARRAY_BUFFER, forGeo->vertexBuffers_[data.bufferSlot_]->GetGPUObject());
            lastBuffer = forGeo->vertexBuffers_[data.bufferSlot_]->GetGPUObject();
        }

        switch (data.type_)
        {
        case VDT_FLOAT:
            glVertexAttribPointer(i, data.elementCount, GL_FLOAT, data.normalized_ ? GL_TRUE : GL_FALSE, data.stride_, (void*)data.offset_);
            break;
        case VDT_UBYTE:
            glVertexAttribPointer(i, data.elementCount, GL_UNSIGNED_BYTE, data.normalized_ ? GL_TRUE : GL_FALSE, data.stride_, (void*)data.offset_);
            break;
        case VDT_UINT:
            glVertexAttribPointer(i, data.elementCount, GL_UNSIGNED_INT, data.normalized_ ? GL_TRUE : GL_FALSE, data.stride_, (void*)data.offset_);
            break;
        }

        if (glState.vao_Instanced[i] != data.instanceStride_)
            glVertexAttribDivisor(i, data.instanceStride_);
        touched[i] = true;
        glState.vao_Instanced[i] = data.instanceStride_;
        glState.vao_ActiveStates[i] = true;
    }

    // disable all other vertex attributes
    for (uint32_t i = 0; i < 16; ++i)
    {
        if (!touched[i] && oldState[i])
        {
            glDisableVertexAttribArray(i);
            glState.vao_ActiveStates[i] = false;
        }
    }
}

}