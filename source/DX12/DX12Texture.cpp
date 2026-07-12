#include "Texture.h"

#include "GraphicsDevice.h"

#include <d3d12.h>

namespace GLVU
{

unsigned dx_RowDataSize(TextureFormat format, int width)
{
    switch (format)
    {
    case TEX_RGB8:
        return (unsigned)(width * 3);

    case TEX_RGBA8:
    case TEX_RGBA8U:
    case TEX_BGRA8:
    case TEX_DEPTH:
    case TEX_RG16U:
    case TEX_RG16F:
    case TEX_R32F:
    case TEX_R32U:
    case TEX_SHADOW32:
        return (unsigned)(width * 4);

    case TEX_SHADOW16:
        return (unsigned)(width * 2);

    case TEX_RGBA16F:
    case TEX_RGBA16U:
        return (unsigned)(width * 8);

    case TEX_DXT1:
        return ((unsigned)(width + 3) >> 2u) * 8;
    case TEX_DXT3:
    case TEX_DXT5:
        return ((unsigned)(width + 3) >> 2u) * 16;

    default:
        return 0;
    }
}

size_t dx_GetBlockSize(TextureFormat format, unsigned width, unsigned height)
{
    if (format == TEX_DXT1 || format == TEX_DXT3 || format == TEX_DXT5)
        return dx_RowDataSize(format, width) * ((height + 3) >> 2u);
    else
        return dx_RowDataSize(format, width) * height;
}

static DXGI_FORMAT FormatFor(TextureFormat fmt)
{
    switch (fmt)
    {
    case TEX_RGBA8: return DXGI_FORMAT_R8G8B8A8_UNORM;
    case TEX_RGBA16F: return DXGI_FORMAT_R16G16B16A16_FLOAT;
    case TEX_RGBA16U: return DXGI_FORMAT_R16G16B16A16_UINT;
    case TEX_BGRA8: return DXGI_FORMAT_B8G8R8A8_UNORM;
    case TEX_R32F: return DXGI_FORMAT_R32_FLOAT;
    case TEX_RG16F: return DXGI_FORMAT_R16G16_FLOAT;
    case TEX_RG16U: return DXGI_FORMAT_R16G16_UINT;
    case TEX_SHADOW16: return DXGI_FORMAT_D16_UNORM;
    case TEX_SHADOW32: return DXGI_FORMAT_D32_FLOAT;
    case TEX_DEPTH: return DXGI_FORMAT_D24_UNORM_S8_UINT;
    }

    return DXGI_FORMAT_R8G8B8A8_UNORM;
}

static bool SupportsUAV(TextureFormat format)
{
    switch (format)
    {
    case TEX_R32F: return true;
    case TEX_RGBA8: return true;
    }
    return false;
}

Texture::~Texture()
{
    Release();
}

void Texture::Release()
{
    if (texture_)
        texture_->Release();
    texture_ = nullptr;

    if (textureMem_)
        textureMem_->Release();
    textureMem_ = nullptr;
}

bool Texture::IsValid() const
{
    return texture_ != nullptr && textureMem_ != nullptr;
}

bool Texture::Create(const TextureTraits& traits)
{
    traits_ = traits;

    auto alloc = GetDevice()->GetAlloc();
    
    D3D12MA::ALLOCATION_DESC allocDesc = { };
    allocDesc.HeapType = D3D12_HEAP_TYPE_DEFAULT;

    D3D12_RESOURCE_DESC resDesc = { };
    resDesc.Alignment = 0;
    resDesc.DepthOrArraySize = 1;

    switch (traits_.kind_)
    {
    case Texture1D:
        resDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE1D;
        break;
    case Texture2D:
        resDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        break;
    case Texture3D:
        resDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE3D;
        resDesc.DepthOrArraySize = traits_.depth_;
        break;
    case Texture2DArray:
        resDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        resDesc.DepthOrArraySize = traits_.layers_;
        break;
    case TextureCube:
        resDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        resDesc.DepthOrArraySize = 6;
        break;
    }

    resDesc.Width = traits_.width_;
    resDesc.Height = traits_.height_;
    resDesc.Format = FormatFor(traits_.format_);
    resDesc.SampleDesc.Count = 1;
    resDesc.MipLevels = traits_.mips_;
    resDesc.SampleDesc.Quality = 0;

    if (traits_.colorAttachment_)
        resDesc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    if (traits_.depthAttachment_)
        resDesc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
    if (SupportsUAV(traits_.format_))
        resDesc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    resDesc.Flags |= D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE;

    D3D12_CLEAR_VALUE clr;
    D3D12_CLEAR_VALUE* clrValue = nullptr;
    if (traits.colorAttachment_)
    {
        clr.Format = resDesc.Format;
        clr.Color[0] = 0.0f;
        clr.Color[1] = 0.0f;
        clr.Color[2] = 0.0f;
        clr.Color[3] = 0.0f;
        clrValue = &clr;
    }
    else if (traits.depthAttachment_)
    {
        clr.Format = resDesc.Format;
        clr.DepthStencil.Depth = 1.0f;
        clr.DepthStencil.Stencil = 0;
        clrValue = &clr;
    }

    auto hr = alloc->CreateResource(&allocDesc, &resDesc, D3D12_RESOURCE_STATE_COPY_DEST, clrValue, &textureMem_, IID_PPV_ARGS(&texture_));
    if (hr != S_OK)
    {
        Release();
        return false;
    }

    creationDesc_ = resDesc;

    return true;
}

void Texture::SetData(void* data, uint32_t width, uint32_t height, uint32_t depth, uint32_t mip, uint32_t layer)
{
    SetSubData(data, 0, 0, 0, width, height, depth, mip, layer);
}

void Texture::SetSubData(void* data, uint32_t x, uint32_t y, uint32_t z, uint32_t width, uint32_t height, uint32_t depth, uint32_t mip, uint32_t layer)
{
    auto alloc = GetDevice()->GetAlloc();
    auto device = GetDevice()->GetD3D12();

    auto resDesc = creationDesc_;
    resDesc.Width = width;
    resDesc.Height = height;

    uint32_t numRows;
    uint64_t totalBytes;
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT layout;
    GetDevice()->GetD3D12()->GetCopyableFootprints(&resDesc
        , 0
        , 1
        , 0
        , &layout
        , &numRows
        , NULL
        , &totalBytes
    );

    const uint32_t rowPitch = layout.Footprint.RowPitch;

    // Do the work
    D3D12MA::Allocation* scratchAlloc = nullptr;
    ID3D12Resource* scratchResource = nullptr;

    D3D12MA::ALLOCATION_DESC allocDesc = { };
    allocDesc.HeapType = D3D12_HEAP_TYPE_UPLOAD;
    allocDesc.Flags = D3D12MA::ALLOCATION_FLAG_COMMITTED;

    D3D12_RESOURCE_DESC scratchDesc = { };
    scratchDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    scratchDesc.Width = totalBytes;
    scratchDesc.Height = 1;
    scratchDesc.Format = DXGI_FORMAT_UNKNOWN;
    scratchDesc.DepthOrArraySize = 1;
    scratchDesc.MipLevels = 1;
    scratchDesc.SampleDesc.Count = 1;
    scratchDesc.SampleDesc.Quality = 0;
    scratchDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

    auto subRes = D3D12CalcSubresource(mip, layer, 0, traits_.mips_, traits_.layers_);

    auto hr = alloc->CreateResource(&allocDesc, &scratchDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, &scratchAlloc, IID_PPV_ARGS(&scratchResource));
    if (hr != S_OK)
    {
        GetDevice()->LogFormat(GLVU_ERROR, "Texture::SetData failed to create staging resource %u", hr);
        return;
    }
    D3D12_RANGE readRange = { 0, 0 };
    void* writeTarget = nullptr;
    hr = scratchResource->Map(0, &readRange, &writeTarget);
    if (hr != S_OK)
    {
        GetDevice()->LogFormat(GLVU_ERROR, "Texture::SetData failed to map resource %u", hr);
        return;
    }

    // TODO: verify this is correct
    auto srcPitch = dx_RowDataSize(traits_.format_, width);
    for (uint32_t row = 0, height = numRows; row < height; ++row)
        memcpy(&((uint8_t*)writeTarget)[row * rowPitch], &((uint8_t*)data)[row * srcPitch], srcPitch);

    D3D12_RANGE writeRange = { 0, numRows * rowPitch };
    scratchResource->Unmap(0, &writeRange);

    ID3D12GraphicsCommandList* cmdList;
    D3D12_TEXTURE_COPY_LOCATION src = { scratchResource, D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT, { layout } };
    D3D12_TEXTURE_COPY_LOCATION dest = { texture_, D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX, { } };
    dest.SubresourceIndex = subRes;

    D3D12_BOX box;
    box.left = 0;
    box.top = 0;
    box.right = box.left + width;
    box.bottom = box.top + height;
    box.front = z;
    box.back = z + depth;

    cmdList->CopyTextureRegion(&src, x, y, 0, &dest, &box);
}

bool Texture::CopyFromBuffer(const std::shared_ptr<Buffer>& buff)
{
    return false;
}

}