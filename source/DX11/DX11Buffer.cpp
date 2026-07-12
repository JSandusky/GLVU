//****************************************************************************
//
//  File:       DX11Buffer.cpp
//  License:    MIT
//  Project:    GLVU
//  Contents:   DX11 implementation for Buffer class.
//              Creation code is handlined in DXGraphicsDevice.cpp
//
//****************************************************************************

#include <Buffer.h>
#include <GraphicsDevice.h>

namespace GLVU
{
    
int dx_BufferSlot(BufferKind kind)
{
    switch (kind)
    {
    case VertexBufferObject:
        return D3D11_BIND_VERTEX_BUFFER | D3D11_BIND_SHADER_RESOURCE;
    case IndexBufferObject:
        return D3D11_BIND_INDEX_BUFFER | D3D11_BIND_SHADER_RESOURCE;
    case UniformBufferObject:
        return D3D11_BIND_CONSTANT_BUFFER;
    case ShaderDataBufferObject:
        return D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_SHADER_RESOURCE;
    case IndirectArgsBufferObject:
        return D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_SHADER_RESOURCE;
    case ByteAddressBuffer:
        return D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_VERTEX_BUFFER | D3D11_BIND_INDEX_BUFFER;
    }
    return 0;
}

// https://stackoverflow.com/questions/3407012/c-rounding-up-to-the-nearest-multiple-of-a-number
int dx_RoundUp(int numToRound, int multiple)
{
	if (multiple == 0)
		return numToRound;

	int remainder = numToRound % multiple;
	if (remainder == 0)
		return numToRound;

	return numToRound + multiple - remainder;
}

//****************************************************************************
//
//  Function:   Buffer::Buffer
//
//  Purpose:    Construct, null initialize
//
//****************************************************************************
Buffer::Buffer(GraphicsDevice* device, BufferKind kind) :
    GPUObject(device),
    buffer_(0),
    size_(0),
    shadowSize_(0),
    shadowed_(false),
    shadowData_(nullptr),
    kind_(kind),
    tags_(0),
	stride_(0),
    shadowDirty_(false)
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
	Release();
    shadowSize_ = 0;
    shadowData_.reset();
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
	if (srv_)
		srv_->Release();
	if (uav_)
		uav_->Release();
    if (buffer_)
        buffer_->Release();

    buffer_ = nullptr;
    srv_ = nullptr;
	uav_ = nullptr;
    size_ = 0;
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

void Buffer::Create(size_t size, void* data)
{
	if (buffer_)
		buffer_->Release();

	D3D11_BUFFER_DESC bufferDesc;
	ZeroMemory(&bufferDesc, sizeof(bufferDesc));

	bufferDesc.ByteWidth = size;
	bufferDesc.BindFlags = dx_BufferSlot(kind_);
	bufferDesc.CPUAccessFlags = HasTag(BufferTag_Dynamic) ? D3D11_CPU_ACCESS_WRITE : 0;
	bufferDesc.Usage = HasTag(BufferTag_Dynamic) ? D3D11_USAGE_DYNAMIC : D3D11_USAGE_DEFAULT;

    if (GetBufferKind() == VertexBufferObject)
        bufferDesc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS;
	if (GetBufferKind() == UniformBufferObject)
	{
		bufferDesc.ByteWidth = dx_RoundUp(bufferDesc.ByteWidth, 16);
		bufferDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
		bufferDesc.Usage = D3D11_USAGE_DYNAMIC;//D3D11_USAGE_DEFAULT;
		SetTag(BufferTag_Dynamic);
	}
	else if (GetBufferKind() == ShaderDataBufferObject)
	{
		bufferDesc.StructureByteStride = stride_;
        if (stride_ == 0)
            device_->LogMessage("Attempting to create SSBO with 0 stride", GLVU_ERROR);
		bufferDesc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
        SetTag(BufferTag_Compute);
        bufferDesc.Usage = D3D11_USAGE_DEFAULT;
	}
    else if (GetBufferKind() == ByteAddressBuffer)
    {
        bufferDesc.StructureByteStride = sizeof(uint32_t);
        stride_ = sizeof(uint32_t);
        bufferDesc.BindFlags |= D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_INDEX_BUFFER | D3D11_BIND_VERTEX_BUFFER;
        bufferDesc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS;
        SetTag(BufferTag_Compute);
    }
    else if (GetBufferKind() == IndirectArgsBufferObject)
    {
        bufferDesc.BindFlags |= D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
        bufferDesc.MiscFlags = D3D11_RESOURCE_MISC_DRAWINDIRECT_ARGS;
    }
    else if (GetBufferKind() == IndexBufferObject)
    {
        bufferDesc.BindFlags |= D3D11_BIND_SHADER_RESOURCE;
        if (HasTag(BufferTag_32Bit))
            bufferDesc.StructureByteStride = sizeof(uint32_t); // no half buffer
    }

    if (HasTag(BufferTag_Compute))
        bufferDesc.BindFlags |= D3D11_BIND_UNORDERED_ACCESS;

	D3D11_SUBRESOURCE_DATA datum;
	ZeroMemory(&datum, sizeof(datum));
	datum.pSysMem = data;
	datum.SysMemPitch = size;
	auto hr = GetDevice()->GetD3DDevice()->CreateBuffer(&bufferDesc, data ? &datum : nullptr, (ID3D11Buffer**)&buffer_);
	if (FAILED(hr))
	{
		if (buffer_)
			buffer_->Release();
		buffer_ = nullptr;
		GetDevice()->LogFormat(GLVU_ERROR, "Failed to create buffer: %X", hr);
		return;
	}

	if (GetBufferKind() == ShaderDataBufferObject || GetBufferKind() == IndirectArgsBufferObject)
	{
		D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc;
		ZeroMemory(&uavDesc, sizeof(uavDesc));

        const bool isIndirect = GetBufferKind() == IndirectArgsBufferObject;
        if (isIndirect)
            stride_ = sizeof(IndirectArgs);

		uavDesc.Format = isIndirect ? DXGI_FORMAT_R32_UINT : DXGI_FORMAT_UNKNOWN;
		uavDesc.Buffer.FirstElement = 0;
		uavDesc.Buffer.Flags = HasTag(BufferTag_AppendConsume) ? D3D11_BUFFER_UAV_FLAG_APPEND : 0;
		uavDesc.Buffer.NumElements = size / stride_;
		uavDesc.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;

		hr = GetDevice()->GetD3DDevice()->CreateUnorderedAccessView(buffer_, &uavDesc, &uav_);
		if (FAILED(hr))
		{
			GetDevice()->LogFormat(GLVU_ERROR, "Failed to create UAV for buffer: %X", hr);
			if (uav_)
				uav_->Release();
			uav_ = nullptr;
		}

        if (GetBufferKind() != IndirectArgsBufferObject)
        {
            D3D11_SHADER_RESOURCE_VIEW_DESC viewDesc;
            ZeroMemory(&viewDesc, sizeof(viewDesc));
            viewDesc.BufferEx.FirstElement = 0;
            viewDesc.BufferEx.NumElements = size / stride_;
            viewDesc.Format = DXGI_FORMAT_UNKNOWN;
            viewDesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFEREX;
            hr = GetDevice()->GetD3DDevice()->CreateShaderResourceView(buffer_, &viewDesc, &srv_);
            if (FAILED(hr))
            {
                GetDevice()->LogFormat(GLVU_ERROR, "Failed to create SRV for buffer: %X", hr);
            }
        }
	}
    else if (HasTag(BufferTag_Compute))
    {
        D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc;
        ZeroMemory(&uavDesc, sizeof uavDesc);
        uavDesc.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;

        D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc;
        ZeroMemory(&srvDesc, sizeof(srvDesc));
        srvDesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;

        if (GetBufferKind() == IndexBufferObject)
        {
            stride_ = HasTag(BufferTag_32Bit) ? sizeof(uint32_t) : sizeof(uint16_t);;
            srvDesc.Format = HasTag(BufferTag_32Bit) ? DXGI_FORMAT_R32_UINT : DXGI_FORMAT_R16_UINT;
            uavDesc.Format = HasTag(BufferTag_32Bit) ? DXGI_FORMAT_R32_UINT : DXGI_FORMAT_R16_UINT;
        }
        else if (GetBufferKind() == VertexBufferObject)
        {
            stride_ = sizeof(uint32_t);
            srvDesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFEREX;
            srvDesc.BufferEx.FirstElement = 0;
            srvDesc.BufferEx.Flags = D3D11_BUFFEREX_SRV_FLAG_RAW;
            srvDesc.BufferEx.NumElements = bufferDesc.ByteWidth / 4;
            srvDesc.Format = DXGI_FORMAT_R32_TYPELESS;
            uavDesc.Format = DXGI_FORMAT_R32_TYPELESS;
            uavDesc.Buffer.Flags = D3D11_BUFFER_UAV_FLAG_RAW;
        }
        else if (GetBufferKind() == ByteAddressBuffer)
        {
            stride_ = sizeof(uint32_t);
            srvDesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFEREX;
            srvDesc.Format = DXGI_FORMAT_R32_TYPELESS;
            srvDesc.BufferEx.Flags = D3D11_BUFFEREX_SRV_FLAG_RAW;
            uavDesc.Format = DXGI_FORMAT_R32_TYPELESS;
            uavDesc.Buffer.Flags = D3D11_BUFFER_UAV_FLAG_RAW;
            srvDesc.BufferEx.FirstElement = 0;
            srvDesc.BufferEx.NumElements = size / stride_;
        }

        uavDesc.Buffer.FirstElement = 0;
        uavDesc.Buffer.NumElements = size / stride_;
        if (srvDesc.ViewDimension == D3D11_SRV_DIMENSION_BUFFER)
        {
            srvDesc.Buffer.FirstElement = 0;
            srvDesc.Buffer.NumElements = size / stride_;
        }

        auto hr = GetDevice()->GetD3DDevice()->CreateUnorderedAccessView(buffer_, &uavDesc, &uav_);
        if (FAILED(hr))
        {
            if (uav_) uav_->Release();
            uav_ = nullptr;
            GetDevice()->LogFormat(GLVU_ERROR, "Failed to create UAV for %s: %X", BufferKindToString(kind_), hr);
        }

        hr = GetDevice()->GetD3DDevice()->CreateShaderResourceView(buffer_, &srvDesc, &srv_);
        if (FAILED(hr))
        {
            if (srv_) srv_->Release();
            srv_ = nullptr;
            GetDevice()->LogFormat(GLVU_ERROR, "Failed to create SRV for %s : %X", BufferKindToString(kind_), hr);
        }
    }

	size_ = size;
	if (GetBufferKind() == UniformBufferObject)
		SetShadowed(true);

	if (buffer_ && shadowed_)
	{
        if (shadowSize_ != size || shadowData_ == nullptr)
		    shadowData_.reset(new unsigned char[size]);
        
        shadowSize_ = size;
		if (data && data != shadowData_.get())
			memcpy(shadowData_.get(), data, size);
        else if (data != shadowData_.get())
            memset(shadowData_.get(), 0, size);
	}
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
	if (size_ < size)
		Release();

	if (buffer_ == nullptr)
	{
		Create(size, data);
		return;
	}
    
    if (buffer_)
    {
        D3D11_BOX destBox;
        destBox.left = 0;
        destBox.right = size;
        destBox.top = 0;
        destBox.bottom = 1;
        destBox.front = 0;
        destBox.back = 1;

		if (GetBufferKind() == UniformBufferObject)
		{
			if (!shadowData_)
				shadowData_.reset(new unsigned char[size_]);
			shadowed_ = true;

			memcpy(shadowData_.get(), data, size);
			auto ptr = Map();
			memcpy(ptr, data, size);
			Unmap();
            return;
		}
		else
		{
			if (HasTag(BufferTag_Dynamic))
			{
				void* target = Map();
				memcpy(target, data, size);
				Unmap();
			}
			else
				GetDevice()->GetD3DContext()->UpdateSubresource((ID3D11Buffer*)buffer_, 0, &destBox, data, 0, 0);
		}
        
        if (shadowed_)
        {
            if (shadowData_)
            {
                if (shadowData_.get() != data)
                {
                    shadowSize_ = size;
                    shadowData_.reset(new unsigned char[size]);
                    if (data)
                        memcpy(shadowData_.get(), data, size);
                    else
                        memset(shadowData_.get(), 0, size);
                }
            }
        }
        else
            shadowData_.reset();

        size_ = size;
    }
}

//****************************************************************************
//
//  Function:   Buffer::SetSubData
//
//  Purpose:    Transfers data into a region of the buffer, does not try to
//              create a non-existent backing DX11-buffer because we can't know
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
        if (HasTag(BufferTag_Dynamic))
        {
            auto m = Map();
            memcpy((char*)m + offset, data, size);
            Unmap();
        }
        else
        {
            D3D11_BOX destBox;
            destBox.left = offset;
            destBox.right = size;
            destBox.top = 0;
            destBox.bottom = 1;
            destBox.front = 0;
            destBox.back = 1;

            GetDevice()->GetD3DContext()->UpdateSubresource((ID3D11Buffer*)buffer_, 0, &destBox, data, 0, 0);
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

    if (buffer_ == 0 || size > size_)
		Create(size, nullptr);

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

    D3D11_MAPPED_SUBRESOURCE mappedData;
    mappedData.pData = nullptr;

    auto hr = GetDevice()->GetD3DContext()->Map((ID3D11Buffer*)buffer_, 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedData);
    if (FAILED(hr))
    {
		GetDevice()->LogFormat(GLVU_WARNING, "Failed to map Buffer object: %X", hr);
        return nullptr;
    }
    
    return mappedData.pData;
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

    GetDevice()->GetD3DContext()->Unmap((ID3D11Buffer*)buffer_, 0);
}

//****************************************************************************
//
//  Function:   Buffer::GetGPUData
//
//  Purpose:    Attempts to read back the buffer data from the GPU.
//
//	Return:		Blob container of the data, if possible.
//
//****************************************************************************
Blob Buffer::GetGPUData() const
{
	if (buffer_ == 0)
		return Blob(nullptr, 0, false);


    D3D11_MAPPED_SUBRESOURCE mappedData;
    mappedData.pData = nullptr;
    
    HRESULT hr = GetDevice()->GetD3DContext()->Map((ID3D11Buffer*)buffer_, 0, D3D11_MAP_READ, 0, &mappedData);
    if (FAILED(hr))
    {
        //TODO
        return Blob(nullptr, 0, false);
    }
    
    Blob blob(size_);    
    memcpy(blob.data_, mappedData.pData, size_);
	
	return blob;
}

//****************************************************************************
//
//  Function:   Buffer::GetUAV
//
//  Purpose:    Constructs a UAV if possible and then provides that back.
//				Tags and/or stride needs to be configured so it can determine
//				how to construct.
//
//	Return:		The UAV pointer.
//
//****************************************************************************
ID3D11UnorderedAccessView* Buffer::GetUAV()
{
	if (uav_)
		return uav_;

	D3D11_UNORDERED_ACCESS_VIEW_DESC desc;
	ZeroMemory(&desc, sizeof(desc));
    
    desc.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
    desc.Buffer.FirstElement = 0;
    desc.Buffer.Flags = 0;
    desc.Buffer.NumElements = size_ / stride_;

    switch (kind_)
    {
    case VertexBufferObject:
        desc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
        desc.Buffer.NumElements = size_ / (sizeof(float) * 4);
        break;
    case IndexBufferObject:
        desc.Format = HasTag(BufferTag_32Bit) ? DXGI_FORMAT_R32_UINT : DXGI_FORMAT_R16_UINT;
        desc.Buffer.NumElements = HasTag(BufferTag_32Bit) ? size_ / sizeof(unsigned) : size_ / sizeof(unsigned short);
        break;
    case ShaderDataBufferObject:
        desc.Format = DXGI_FORMAT_UNKNOWN;
        break;
    case UniformBufferObject:
        GetDevice()->LogFormat(GLVU_ERROR, "Cannot create UAV for constant-buffer");
        return nullptr;
    }

	auto hr = GetDevice()->GetD3DDevice()->CreateUnorderedAccessView(buffer_, &desc, &uav_);
	if (FAILED(hr))
	{
		GetDevice()->LogFormat(GLVU_ERROR, "Failed to create UAV: %X", hr);
		return nullptr;
	}
	return uav_;
}

extern void dx_PushLateRelease(ID3D11DeviceChild*);
ID3D11ShaderResourceView* Buffer::GetEphemeralView(uint32_t start, uint32_t length)
{
    if (buffer_ == nullptr || srv_ == nullptr)
        return nullptr;

    ID3D11ShaderResourceView* srv = nullptr;

    D3D11_SHADER_RESOURCE_VIEW_DESC desc;
    srv_->GetDesc(&desc);
    desc.BufferEx.FirstElement = start;
    desc.BufferEx.NumElements = length / stride_;

    GetDevice()->GetD3DDevice()->CreateShaderResourceView(buffer_, &desc, &srv);
    dx_PushLateRelease(srv);
    return srv;
}

}