//****************************************************************************
//
//  File:       GLBuffer.cpp
//  License:    MIT
//  Project:    GLVU
//  Contents:   OpenGL implementation for Buffer class.
//              Creation code is handlined in GLGraphicsDevice.cpp
//
//****************************************************************************

#include <Buffer.h>
#include <GraphicsDevice.h>

namespace GLVU
{

//****************************************************************************
//
//  Function:   gl_BufferSlot
//
//  Purpose:    Maps a buffer-kind to a GL target.
//
//****************************************************************************
GLuint gl_BufferSlot(BufferKind kind)
{
    switch (kind)
    {
    case VertexBufferObject:
        return GL_ARRAY_BUFFER;
    case IndexBufferObject:
        return GL_ELEMENT_ARRAY_BUFFER;
    case UniformBufferObject:
        return GL_UNIFORM_BUFFER;
    case ShaderDataBufferObject:
        return GL_SHADER_STORAGE_BUFFER;
    }
    return 0;
}

//****************************************************************************
//
//  Function:   Buffer::Buffer
//
//  Purpose:    Construct, sets the gl buffer to 0
//
//****************************************************************************
Buffer::Buffer(GraphicsDevice* device, BufferKind kind) :
    GPUObject(device),
    buffer_(0),
    size_(0),
    shadowed_(false),
    shadowData_(nullptr),
    kind_(kind),
    tags_(0)
{
}

//****************************************************************************
//
//  Function:   Buffer::~Buffer
//
//  Purpose:    Destruct.
//
//****************************************************************************
Buffer::~Buffer()
{
    // GPUObject will release us
}

//****************************************************************************
//
//  Function:   Buffer::Release
//
//  Purpose:    Deletes the buffer object and clears out the other state.
//
//****************************************************************************
void Buffer::Release()
{
    if (buffer_)
        glDeleteBuffers(1, &buffer_);

    buffer_ = 0;
    size_ = 0;
    shadowData_.reset();
}

//****************************************************************************
//
//  Function:   Buffer::GetBuffer
//
//  Purpose:    Utility check for validity.
//
//  Return:     true if we're good
//
//****************************************************************************
bool Buffer::IsValid() const
{
    return buffer_ != 0;
}

//****************************************************************************
//
//  Function:   Buffer::SetData
//
//  Purpose:    Uploads data to the buffer, creates the buffer if necessary.
//
//****************************************************************************
void Buffer::SetData(void* data, uint32_t size)
{
    if (buffer_ == 0)
        glGenBuffers(1, &buffer_);
    if (buffer_)
    {
        const auto bindPoint = gl_BufferSlot(kind_);

        glBindBuffer(bindPoint, buffer_);
        if (size_ >= size)
        {
            if (HasTag(BufferTag_Dynamic))
            {
                if (auto buff = glMapBufferRange(bindPoint, 0, size, GL_MAP_WRITE_BIT | GL_MAP_INVALIDATE_BUFFER_BIT))
                {
                    memcpy(buff, data, size);
                    glUnmapBuffer(bindPoint);
                }
                else
                    GetDevice()->LogMessage("Failed to map buffer-range for Buffer.", GLVU_ERROR);
            }
            else
                glBufferSubData(bindPoint, 0, size, data);

            if (shadowed_ && shadowData_)
                memcpy(shadowData_.get(), data, size);
            return;
        }
        else
            glBufferData(bindPoint, size, data, HasTag(BufferTag_Dynamic) ? GL_DYNAMIC_DRAW : GL_STATIC_DRAW);
        if (shadowed_)
        {
            if (shadowData_)
            {
                if (shadowData_.get() != data)
                {
                    shadowData_.reset(new unsigned char[size]);
                    if (data)
                        memcpy(shadowData_.get(), data, size);
                    else
                        memset(shadowData_.get(), 0, size);
                    shadowSize_ = size;
                }
            }
        }
        else
        {
            shadowData_.reset();
            shadowSize_ = 0;
        }

        size_ = size;
    }
}

//****************************************************************************
//
//  Function:   Buffer::SetSubData
//
//  Purpose:    Transfers data into a region of the buffer, does not try to
//              create a non-existent backing GL-buffer because we can't know
//              what the intended size of the backing buffer would be.
//
//****************************************************************************
void Buffer::SetSubData(void* data, uint32_t offset, uint32_t size)
{
    assert(buffer_ && size_ >= offset + size);
    if (buffer_ == 0)
        return;
    if (buffer_)
    {
        const auto bindPoint = gl_BufferSlot(kind_);
        if (HasTag(BufferTag_Dynamic))
        {
            glBindBuffer(bindPoint, buffer_);
            if (auto buff = glMapBufferRange(bindPoint, offset, size, GL_MAP_WRITE_BIT | GL_MAP_INVALIDATE_RANGE_BIT))
            {
                memcpy(buff, data, size);
                glUnmapBuffer(bindPoint);
            }
            else
                GetDevice()->LogMessage("Failed to map buffer-range for Buffer.", GLVU_ERROR);
        }
        else
        {
            glBindBuffer(bindPoint, buffer_);
            glBufferSubData(bindPoint, offset, size, data);
        }
        if (shadowed_ && shadowData_)
            memcpy(shadowData_.get() + offset, data, size);
    }
}

//****************************************************************************
//
//  Function:   Buffer::SetSize
//
//  Purpose:    Sets the size of the buffer, release and allocating as needed.
//
//****************************************************************************
void Buffer::SetSize(uint32_t size)
{
    if (size_ == 0)
        Release();

    if (buffer_ == 0 || size > size)
    {
        Release();
        glGenBuffers(1, &buffer_);
    }

    if (buffer_ && size_ < size)
    {
        glBindBuffer(gl_BufferSlot(kind_), buffer_);
        glBufferData(gl_BufferSlot(kind_), size, nullptr, HasTag(BufferTag_Dynamic) ? GL_DYNAMIC_DRAW : GL_STATIC_DRAW);
        if (shadowed_)
        {
            shadowData_.reset(new unsigned char[size]);
            memset(shadowData_.get(), 0, size);
            shadowSize_ = size;
        }
        else
        {
            shadowData_.reset();
            shadowSize_ = 0;
        }
    }

    size_ = size;
}

//****************************************************************************
//
//  Function:   Buffer::Map
//
//  Purpose:    Naively attempts to map the buffer for write-only access.
//              Some basic idiot checks are there, but they're not going to save
//              you from not allocating first.
//
//  Return:     data pointer if possible, otherwise it's going to be null.
//
//****************************************************************************
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

    glBindBuffer(gl_BufferSlot(kind_), buffer_);
    return glMapBuffer(gl_BufferSlot(kind_), GL_WRITE_ONLY);
}

//****************************************************************************
//
//  Function:   Buffer::Unmap
//
//  Purpose:    Unmaps the buffer without any safety checks aside from
//              verifying the buffer exists.
//
//              Some checks could be made, but those get a bit nasty.
//
//****************************************************************************
void Buffer::Unmap()
{
    if (!HasTag(BufferTag_Dynamic))
        return;
    if (buffer_ == 0)
        return;

    glBindBuffer(gl_BufferSlot(kind_), buffer_);
    glUnmapBuffer(gl_BufferSlot(kind_));
}

//****************************************************************************
//
//  Function:   Buffer::GetGPUData
//
//  Purpose:    Reads back the buffer-data from the GPU.
//
//****************************************************************************
Blob Buffer::GetGPUData() const
{
	if (buffer_ == 0)
		return Blob(nullptr, 0, false);

	glBindBuffer(gl_BufferSlot(kind_), buffer_);

	void* data;

	Blob blob(size_);
	glGetBufferSubData(gl_BufferSlot(kind_), 0, size_, blob.data_);
	return blob;
}

}