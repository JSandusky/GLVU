//****************************************************************************
//
//  File:       Texture.cpp
//  License:    MIT
//  Project:    GLVU
//
//****************************************************************************

#include "Texture.h"

#include "GraphicsDevice.h"

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#define DDSKTX_IMPLEMENT
#include "ddsktx.h"

using namespace std;

namespace GLVU
{

//****************************************************************************
//
//  Function:   FrameBuffer::GetWidth
//
//  Purpose:    Utility
//
//  Return:     The width of the this backbuffers, has to deal with the
//              *report width* nonsense that cheats OpenGL's backbuffer into this guy.
//
//****************************************************************************
uint32_t FrameBuffer::GetWidth() const
{
    if (textures_.empty())
        return reportWidth_;
    //assert(!textures_.empty());
    return textures_[0]->GetWidth();
}

//****************************************************************************
//
//  Function:   FrameBuffer::GetHeight
//
//  Purpose:    Utility
//
//  Return:     The width of the this backbuffers, has to deal with the
//              *report height* nonsense that cheats OpenGL's backbuffer into this guy.
//
//****************************************************************************
uint32_t FrameBuffer::GetHeight() const
{
    if (textures_.empty())
        return reportHeight_;
    //assert(!textures_.empty());
    return textures_[0]->GetHeight();
}

//****************************************************************************
//
//  Function:   Texture::LoadFile
//
//  Purpose:    Loads a raster images from STB or DDSKTX support formats.
//              DDS/KTX format support is limited to those GLVU allows.
//
//              In a *thou shall not touch* scenario, it could deal with others
//              with minor modification, ie TextureFormat::TEX_MAGIC
//
//              TODO: DDS/KTX cubemap arrays?
//
//  Return:     The loaded texture if successful, otherwise null.
//
//****************************************************************************
shared_ptr<Texture> Texture::LoadFile(GraphicsDevice* device, const char* file, bool wantMips)
{
    string fileName = file;
        
    auto blob = device->GetResourceData(Resource_Texture, file);
    if (blob.size_ == 0)
        return nullptr;

    auto dotIndex = fileName.find('.');
    auto suffix = fileName.substr(dotIndex + 1);

    if (suffix == "png" || suffix == "jpg" || suffix == "jpeg" || suffix == "gif" || suffix == "psd" || suffix == "bmp")
    {
        int x, y, comps;
        auto data = stbi_load_from_memory((unsigned char*)blob.data_, (int)blob.size_, &x, &y, &comps, 4);
        if (data == nullptr)
        {
            device->LogMessage(stbi_failure_reason(), GLVU_ERROR);
            return nullptr;
        }

        // must have at least RGB

        TextureTraits traits = {};
        traits.width_ = x;
        traits.height_ = y;
        traits.format_ = TEX_RGBA8;
        traits.autoMip_ = wantMips;
        auto ret = device->CreateTexture(traits);
        ret->SetData(data, x, y, 1, 0, 0);
        if (wantMips)
            ret->GenerateMipMaps();

        stbi_image_free(data);

        return ret;
    }
    else if (suffix == "ktx" || suffix == "dds")
    {
        ddsktx_texture_info info;
        ddsktx_error err;
        if (ddsktx_parse(&info, blob.data_, (int)blob.size_, &err))
        {
            TextureTraits traits = { };
            traits.width_ = info.width;
            traits.height_ = info.height;
            traits.depth_ = info.depth;
            traits.layers_ = info.num_layers;
            traits.mips_ = info.num_mips;

            switch (info.format)
            {
            case DDSKTX_FORMAT_RGB8:
                traits.format_ = TEX_RGB8;
                break;
            case DDSKTX_FORMAT_RGBA16F:
                traits.format_ = TEX_RGBA16F;
                break;
            case DDSKTX_FORMAT_RGBA16:
                traits.format_ = TEX_RGBA16U;
                break;
            case DDSKTX_FORMAT_RG16F:
                traits.format_ = TEX_RG16F;
                break;
            case DDSKTX_FORMAT_RG16:
                traits.format_ = TEX_RG16U;
                break;
            case DDSKTX_FORMAT_R32F:
                traits.format_ = TEX_R32F;
                break;
            case DDSKTX_FORMAT_RGBA8:
                traits.format_ = TEX_RGBA8;
                break;
            case DDSKTX_FORMAT_BGRA8:
                traits.format_ = TEX_BGRA8;
                break;
            case DDSKTX_FORMAT_BC1:
                traits.format_ = TEX_DXT1;
                break;
            case DDSKTX_FORMAT_BC2:
                traits.format_ = TEX_DXT3;
                break;
            case DDSKTX_FORMAT_BC3:
                traits.format_ = TEX_DXT5;
                break;
            case DDSKTX_FORMAT_BC4:
                traits.format_ = TEX_BC4;
                break;
            case DDSKTX_FORMAT_BC5:
                traits.format_ = TEX_BC5;
                break;
            default:
                device->LogFormat(GLVU_ERROR, "Unknown texture format: %u", info.format);
            }

            if (info.flags & DDSKTX_TEXTURE_FLAG_CUBEMAP)
                traits.kind_ = TextureCube;
            else if (traits.layers_ > 1)
                traits.kind_ = Texture2DArray;
            else if (traits.depth_ > 1)
                traits.kind_ = Texture3D;

            auto ret = device->CreateTexture(traits);
                
            for (int layer = 0; layer < info.num_layers; ++layer)
            {
                for (int mip = 0; mip < info.num_mips; ++mip)
                {
                    for (int depth = 0; depth < info.depth; ++depth)
                    {
                        if (traits.kind_ == TextureCube)
                        {
                            for (int i = 0; i < 6; ++i)
                            {
                                ddsktx_sub_data subData;
                                ddsktx_get_sub(&info, &subData, blob.data_, (int)blob.size_, layer, i, mip);
                                ret->SetData((void*)subData.buff, subData.width, subData.height, 0, mip, i * layer);
                            }
                        }
                        else
                        {
                            ddsktx_sub_data subData;
                            ddsktx_get_sub(&info, &subData, blob.data_, (int)blob.size_, layer, depth, mip);
                            ret->SetData((void*)subData.buff, subData.width, subData.height, depth, mip, layer);
                        }
                    }
                }
            }

            return ret;
        }
        else
            device->LogMessage(err.msg, 2);
    }

    return nullptr;
}

//****************************************************************************
//
//  Function:   Texture::LoadArrayLUT
//
//  Purpose:    Loads a basic image that's laid out as a vertical list of 1D
//              texture ramps/LUTs which will be interpreted as a Texture2DArray
//              of Y-layers of X-by-1 textures.
//              Due to how arrays are uploaded and the vertical arrangement
//              this is basically just a convenience wrapper.
//
//  Return:     Loaded texture.
//
//****************************************************************************
shared_ptr<Texture> Texture::LoadArrayLUT(GraphicsDevice* device, const char* file)
{
    string fileName = file;

    auto blob = device->GetResourceData(Resource_Texture, file);
    if (blob.size_ == 0)
        return nullptr;

    auto dotIndex = fileName.find('.');
    auto suffix = fileName.substr(dotIndex + 1);

    if (suffix == "png" || suffix == "jpg" || suffix == "jpeg" || suffix == "gif" || suffix == "psd" || suffix == "bmp")
    {
        int x, y, comps;
        auto data = stbi_load_from_memory((unsigned char*)blob.data_, (int)blob.size_, &x, &y, &comps, 4);
        if (data == nullptr)
        {
            device->LogMessage(stbi_failure_reason(), GLVU_ERROR);
            return nullptr;
        }

        // must have at least RGB

        TextureTraits traits = {};
        traits.width_ = x;
        traits.height_ = 1;
        traits.layers_ = y;
        traits.format_ = TEX_RGBA8;
        traits.mips_ = false;
        traits.kind_ = Texture2DArray;

        auto ret = device->CreateTexture(traits);
        ret->SetData(data, x, 1, 0, 0, y);

        stbi_image_free(data);

        return ret;
    }
    return nullptr;
}

//****************************************************************************
//
//  Function:   Texture::LoadArrayStrip
//
//  Purpose:    Loads a basic image that's laid out as a horizontal list of 2D
//              textures which will be interpreted as a Texture2DArray
//              of X-layers of Y-by-Y textures. Implementation via blockmap isn't ideal
//              but simple has value.              
//
//  Return:     Loaded texture.
//
//****************************************************************************
std::shared_ptr<Texture> Texture::LoadArrayStrip(GraphicsDevice* device, const char* file, bool wantMips)
{
    string fileName = file;

    auto blob = device->GetResourceData(Resource_Texture, file);
    if (blob.size_ == 0)
        return nullptr;

    auto dotIndex = fileName.find('.');
    auto suffix = fileName.substr(dotIndex + 1);

    if (suffix == "png" || suffix == "jpg" || suffix == "jpeg" || suffix == "gif" || suffix == "psd" || suffix == "bmp")
    {
        int x, y, comps;
        auto data = stbi_load_from_memory((unsigned char*)blob.data_, (int)blob.size_, &x, &y, &comps, 4);
        if (data == nullptr)
        {
            device->LogMessage(stbi_failure_reason(), GLVU_ERROR);
            return nullptr;
        }

        // must have at least RGB
        int layers = x / y;

        TextureTraits traits = {};
        traits.width_ = y;
        traits.height_ = y;
        traits.layers_ = layers;
        traits.format_ = TEX_RGBA8;
		traits.autoMip_ = wantMips;
        traits.kind_ = Texture2DArray;

        auto ret = device->CreateTexture(traits);

        BlockMap<uint32_t> blockMap(x, y);
        memcpy(blockMap.data_.get(), data, blockMap.GetDataSize());

        BlockMap<uint32_t>* newBlock = blockMap.VolumeFromStrip();
        ret->SetData(newBlock->data_.get(), y, y, 0, 0, layers);
        delete newBlock;

        stbi_image_free(data);

        if (wantMips)
            ret->GenerateMipMaps();

        return ret;
    }
    return nullptr;
}

//****************************************************************************
//
//  Function:   Texture::Load3DLut
//
//  Purpose:    Loads a basic image that's laid out as 3D texture which has it's Z
//              components stored along the X axis. Frequently seen in color-correction.
//
//  Return:     Loaded texture.
//
//****************************************************************************
shared_ptr<Texture> Texture::Load3DLUT(GraphicsDevice* device, const char* file)
{
    string fileName = file;

    auto blob = device->GetResourceData(Resource_Texture, file);
    if (blob.size_ == 0)
        return nullptr;

    auto dotIndex = fileName.find('.');
    auto suffix = fileName.substr(dotIndex + 1);

    if (suffix == "png" || suffix == "jpg" || suffix == "jpeg" || suffix == "gif" || suffix == "psd" || suffix == "bmp")
    {
        int x, y, comps;
        auto data = stbi_load_from_memory((unsigned char*)blob.data_, (int)blob.size_, &x, &y, &comps, 4);
        if (data == nullptr)
        {
            device->LogMessage(stbi_failure_reason(), GLVU_ERROR);
            return nullptr;
        }

        // must have at least RGB

        int depthSlices = x / y;

        TextureTraits traits = {};
        traits.width_ = y;
        traits.height_ = y;
        traits.depth_ = depthSlices;
        traits.format_ = TEX_RGBA8;
        traits.kind_ = Texture3D;

        auto ret = device->CreateTexture(traits);

        BlockMap<uint32_t> blockMap(x, y);
        memcpy(blockMap.data_.get(), data, blockMap.GetDataSize());
        
        BlockMap<uint32_t>* newBlock = blockMap.VolumeFromStrip();
        ret->SetData(newBlock->data_.get(), y, y, depthSlices, 0, 0);
        delete newBlock;

        stbi_image_free(data);

        return ret;
    }
    return nullptr;
}

//****************************************************************************
//
//  Function:   FrameBuffer::GetWidth
//
//  Purpose:    Loads a basic image that's laid out as 3D texture which has it's Z
//              components stored along the X axis. Frequently seen in color-correction.
//
//  Return:     True if successful.
//
//****************************************************************************
bool Texture::LoadFileToLayer(std::shared_ptr<Texture> texture, const char* file, uint32_t layer)
{
    if (texture == nullptr || file == nullptr)
        return false;

    string fileName = file;

    auto blob = texture->GetDevice()->GetResourceData(Resource_Texture, file);
    if (blob.size_ == 0)
        return nullptr;

    auto dotIndex = fileName.find('.');
    auto suffix = fileName.substr(dotIndex + 1);

    if (suffix == "png" || suffix == "jpg" || suffix == "jpeg" || suffix == "gif" || suffix == "psd" || suffix == "bmp")
    {
        int x, y, comps;
        auto data = stbi_load_from_memory((unsigned char*)blob.data_, (int)blob.size_, &x, &y, &comps, 4);
        if (data == nullptr)
        {
            texture->GetDevice()->LogMessage(stbi_failure_reason(), GLVU_ERROR);
            return nullptr;
        }

        if (x != texture->GetWidth() || y != texture->GetHeight())
        {
            texture->GetDevice()->LogFormat(GLVU_ERROR, "Attempted to load Texture array layer with mismatched sizes: %u, %u -> %u %u", x, y, texture->GetWidth(), texture->GetHeight());
            stbi_image_free(data);
            return false;
        }

        texture->SetData(data, texture->GetWidth(), texture->GetHeight(), 0, 0, layer);
        stbi_image_free(data);
        return true;
    }
    return true;
}

//****************************************************************************
//
//  Function:   Texture::NumLevels
//
//  Purpose:    Calculate the number of mip-map levels for the given dimensions,
//				Used for the autoMip stuff so the caller doesn't have to fully
//				fill TextureTraits if they want to mip and can just say 
//				`autoMip_ = true` to make it the systems problem ... poorly 
//				resolves API confusion where mips_ is read as bool for mips at all.
//
//  Return:     Expected number of mips.
//
//	TODO:		Consider overhauling mip level handling, it's a PITA.
//
//****************************************************************************
int Texture::NumLevels(int x, int y, int z)
{
	int maxDim = std::max(x, std::max(y, z));
	return 1 + floor(log2(maxDim));
}

}